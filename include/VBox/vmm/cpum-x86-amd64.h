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

#ifndef VBOX_INCLUDED_vmm_cpum_x86_amd64_h
#define VBOX_INCLUDED_vmm_cpum_x86_amd64_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/x86.h>
#include <VBox/vmm/hm_svm.h>
#include <VBox/vmm/hm_vmx.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_cpum      The CPU Monitor / Manager API
 * @ingroup grp_vmm
 * @{
 */

/**
 * CPUID feature to set or clear.
 */
typedef enum CPUMCPUIDFEATURE
{
    CPUMCPUIDFEATURE_INVALID = 0,
    /** The APIC feature bit. (Std+Ext)
     * Note! There is a per-cpu flag for masking this CPUID feature bit when the
     *       APICBASE.ENABLED bit is zero.  So, this feature is only set/cleared
     *       at VM construction time like all the others.  This didn't used to be
     *       that way, this is new with 5.1. */
    CPUMCPUIDFEATURE_APIC,
    /** The sysenter/sysexit feature bit. (Std) */
    CPUMCPUIDFEATURE_SEP,
    /** The SYSCALL/SYSEXIT feature bit (64 bits mode only for Intel CPUs). (Ext) */
    CPUMCPUIDFEATURE_SYSCALL,
    /** The PAE feature bit. (Std+Ext) */
    CPUMCPUIDFEATURE_PAE,
    /** The NX feature bit. (Ext) */
    CPUMCPUIDFEATURE_NX,
    /** The LAHF/SAHF feature bit (64 bits mode only). (Ext) */
    CPUMCPUIDFEATURE_LAHF,
    /** The LONG MODE feature bit. (Ext) */
    CPUMCPUIDFEATURE_LONG_MODE,
    /** The x2APIC  feature bit. (Std) */
    CPUMCPUIDFEATURE_X2APIC,
    /** The RDTSCP feature bit. (Ext) */
    CPUMCPUIDFEATURE_RDTSCP,
    /** The Hypervisor Present bit. (Std) */
    CPUMCPUIDFEATURE_HVP,
    /** The speculation control feature bits. (StExt) */
    CPUMCPUIDFEATURE_SPEC_CTRL,
    /** 32bit hackishness. */
    CPUMCPUIDFEATURE_32BIT_HACK = 0x7fffffff
} CPUMCPUIDFEATURE;


/**
 * CPUID leaf.
 *
 * @remarks This structure is used by the patch manager and is therefore
 *          more or less set in stone.
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
 * Method used to deal with unknown CPUID leaves.
 * @remarks Used in patch code.
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
 * The register set returned by a CPUID operation.
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
 * MSR read functions.
 */
typedef enum CPUMMSRRDFN
{
    /** Invalid zero value. */
    kCpumMsrRdFn_Invalid = 0,
    /** Return the CPUMMSRRANGE::uValue. */
    kCpumMsrRdFn_FixedValue,
    /** Alias to the MSR range starting at the MSR given by
     * CPUMMSRRANGE::uValue.  Must be used in pair with
     * kCpumMsrWrFn_MsrAlias. */
    kCpumMsrRdFn_MsrAlias,
    /** Write only register, GP all read attempts. */
    kCpumMsrRdFn_WriteOnly,

    kCpumMsrRdFn_Ia32P5McAddr,
    kCpumMsrRdFn_Ia32P5McType,
    kCpumMsrRdFn_Ia32TimestampCounter,
    kCpumMsrRdFn_Ia32PlatformId,            /**< Takes real CPU value for reference. */
    kCpumMsrRdFn_Ia32ApicBase,
    kCpumMsrRdFn_Ia32FeatureControl,
    kCpumMsrRdFn_Ia32BiosSignId,            /**< Range value returned. */
    kCpumMsrRdFn_Ia32SmmMonitorCtl,
    kCpumMsrRdFn_Ia32PmcN,
    kCpumMsrRdFn_Ia32MonitorFilterLineSize,
    kCpumMsrRdFn_Ia32MPerf,
    kCpumMsrRdFn_Ia32APerf,
    kCpumMsrRdFn_Ia32MtrrCap,               /**< Takes real CPU value for reference.  */
    kCpumMsrRdFn_Ia32MtrrPhysBaseN,         /**< Takes register number. */
    kCpumMsrRdFn_Ia32MtrrPhysMaskN,         /**< Takes register number. */
    kCpumMsrRdFn_Ia32MtrrFixed,             /**< Takes CPUMCPU offset. */
    kCpumMsrRdFn_Ia32MtrrDefType,
    kCpumMsrRdFn_Ia32Pat,
    kCpumMsrRdFn_Ia32SysEnterCs,
    kCpumMsrRdFn_Ia32SysEnterEsp,
    kCpumMsrRdFn_Ia32SysEnterEip,
    kCpumMsrRdFn_Ia32McgCap,
    kCpumMsrRdFn_Ia32McgStatus,
    kCpumMsrRdFn_Ia32McgCtl,
    kCpumMsrRdFn_Ia32DebugCtl,
    kCpumMsrRdFn_Ia32SmrrPhysBase,
    kCpumMsrRdFn_Ia32SmrrPhysMask,
    kCpumMsrRdFn_Ia32PlatformDcaCap,
    kCpumMsrRdFn_Ia32CpuDcaCap,
    kCpumMsrRdFn_Ia32Dca0Cap,
    kCpumMsrRdFn_Ia32PerfEvtSelN,           /**< Range value indicates the register number. */
    kCpumMsrRdFn_Ia32PerfStatus,            /**< Range value returned. */
    kCpumMsrRdFn_Ia32PerfCtl,               /**< Range value returned. */
    kCpumMsrRdFn_Ia32FixedCtrN,             /**< Takes register number of start of range. */
    kCpumMsrRdFn_Ia32PerfCapabilities,      /**< Takes reference value. */
    kCpumMsrRdFn_Ia32FixedCtrCtrl,
    kCpumMsrRdFn_Ia32PerfGlobalStatus,      /**< Takes reference value. */
    kCpumMsrRdFn_Ia32PerfGlobalCtrl,
    kCpumMsrRdFn_Ia32PerfGlobalOvfCtrl,
    kCpumMsrRdFn_Ia32PebsEnable,
    kCpumMsrRdFn_Ia32ClockModulation,       /**< Range value returned. */
    kCpumMsrRdFn_Ia32ThermInterrupt,        /**< Range value returned. */
    kCpumMsrRdFn_Ia32ThermStatus,           /**< Range value returned. */
    kCpumMsrRdFn_Ia32Therm2Ctl,             /**< Range value returned. */
    kCpumMsrRdFn_Ia32MiscEnable,            /**< Range value returned. */
    kCpumMsrRdFn_Ia32McCtlStatusAddrMiscN,  /**< Takes bank number. */
    kCpumMsrRdFn_Ia32McNCtl2,               /**< Takes register number of start of range. */
    kCpumMsrRdFn_Ia32DsArea,
    kCpumMsrRdFn_Ia32TscDeadline,
    kCpumMsrRdFn_Ia32X2ApicN,
    kCpumMsrRdFn_Ia32DebugInterface,
    kCpumMsrRdFn_Ia32VmxBasic,              /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxPinbasedCtls,       /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxProcbasedCtls,      /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxExitCtls,           /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxEntryCtls,          /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxMisc,               /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxCr0Fixed0,          /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxCr0Fixed1,          /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxCr4Fixed0,          /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxCr4Fixed1,          /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxVmcsEnum,           /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxProcBasedCtls2,     /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxEptVpidCap,         /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxTruePinbasedCtls,   /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxTrueProcbasedCtls,  /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxTrueExitCtls,       /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxTrueEntryCtls,      /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32VmxVmFunc,             /**< Takes real value as reference. */
    kCpumMsrRdFn_Ia32SpecCtrl,
    kCpumMsrRdFn_Ia32ArchCapabilities,

    kCpumMsrRdFn_Amd64Efer,
    kCpumMsrRdFn_Amd64SyscallTarget,
    kCpumMsrRdFn_Amd64LongSyscallTarget,
    kCpumMsrRdFn_Amd64CompSyscallTarget,
    kCpumMsrRdFn_Amd64SyscallFlagMask,
    kCpumMsrRdFn_Amd64FsBase,
    kCpumMsrRdFn_Amd64GsBase,
    kCpumMsrRdFn_Amd64KernelGsBase,
    kCpumMsrRdFn_Amd64TscAux,

    kCpumMsrRdFn_IntelEblCrPowerOn,
    kCpumMsrRdFn_IntelI7CoreThreadCount,
    kCpumMsrRdFn_IntelP4EbcHardPowerOn,
    kCpumMsrRdFn_IntelP4EbcSoftPowerOn,
    kCpumMsrRdFn_IntelP4EbcFrequencyId,
    kCpumMsrRdFn_IntelP6FsbFrequency,       /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelPlatformInfo,
    kCpumMsrRdFn_IntelFlexRatio,            /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelPkgCStConfigControl,
    kCpumMsrRdFn_IntelPmgIoCaptureBase,
    kCpumMsrRdFn_IntelLastBranchFromToN,
    kCpumMsrRdFn_IntelLastBranchFromN,
    kCpumMsrRdFn_IntelLastBranchToN,
    kCpumMsrRdFn_IntelLastBranchTos,
    kCpumMsrRdFn_IntelBblCrCtl,
    kCpumMsrRdFn_IntelBblCrCtl3,
    kCpumMsrRdFn_IntelI7TemperatureTarget,  /**< Range value returned. */
    kCpumMsrRdFn_IntelI7MsrOffCoreResponseN,/**< Takes register number. */
    kCpumMsrRdFn_IntelI7MiscPwrMgmt,
    kCpumMsrRdFn_IntelP6CrN,
    kCpumMsrRdFn_IntelCpuId1FeatureMaskEcdx,
    kCpumMsrRdFn_IntelCpuId1FeatureMaskEax,
    kCpumMsrRdFn_IntelCpuId80000001FeatureMaskEcdx,
    kCpumMsrRdFn_IntelI7SandyAesNiCtl,
    kCpumMsrRdFn_IntelI7TurboRatioLimit,    /**< Returns range value. */
    kCpumMsrRdFn_IntelI7LbrSelect,
    kCpumMsrRdFn_IntelI7SandyErrorControl,
    kCpumMsrRdFn_IntelI7VirtualLegacyWireCap,/**< Returns range value. */
    kCpumMsrRdFn_IntelI7PowerCtl,
    kCpumMsrRdFn_IntelI7SandyPebsNumAlt,
    kCpumMsrRdFn_IntelI7PebsLdLat,
    kCpumMsrRdFn_IntelI7PkgCnResidencyN,     /**< Takes C-state number. */
    kCpumMsrRdFn_IntelI7CoreCnResidencyN,    /**< Takes C-state number. */
    kCpumMsrRdFn_IntelI7SandyVrCurrentConfig,/**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7SandyVrMiscConfig,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7SandyRaplPowerUnit,  /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7SandyPkgCnIrtlN,     /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7SandyPkgC2Residency, /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPkgPowerLimit,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPkgEnergyStatus, /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPkgPerfStatus,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPkgPowerInfo,    /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplDramPowerLimit,  /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplDramEnergyStatus,/**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplDramPerfStatus,  /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplDramPowerInfo,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp0PowerLimit,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp0EnergyStatus, /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp0Policy,       /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp0PerfStatus,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp1PowerLimit,   /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp1EnergyStatus, /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7RaplPp1Policy,       /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7IvyConfigTdpNominal, /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7IvyConfigTdpLevel1,  /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7IvyConfigTdpLevel2,  /**< Takes real value as reference. */
    kCpumMsrRdFn_IntelI7IvyConfigTdpControl,
    kCpumMsrRdFn_IntelI7IvyTurboActivationRatio,
    kCpumMsrRdFn_IntelI7UncPerfGlobalCtrl,
    kCpumMsrRdFn_IntelI7UncPerfGlobalStatus,
    kCpumMsrRdFn_IntelI7UncPerfGlobalOvfCtrl,
    kCpumMsrRdFn_IntelI7UncPerfFixedCtrCtrl,
    kCpumMsrRdFn_IntelI7UncPerfFixedCtr,
    kCpumMsrRdFn_IntelI7UncCBoxConfig,
    kCpumMsrRdFn_IntelI7UncArbPerfCtrN,
    kCpumMsrRdFn_IntelI7UncArbPerfEvtSelN,
    kCpumMsrRdFn_IntelI7SmiCount,
    kCpumMsrRdFn_IntelCore2EmttmCrTablesN,  /**< Range value returned. */
    kCpumMsrRdFn_IntelCore2SmmCStMiscInfo,
    kCpumMsrRdFn_IntelCore1ExtConfig,
    kCpumMsrRdFn_IntelCore1DtsCalControl,
    kCpumMsrRdFn_IntelCore2PeciControl,
    kCpumMsrRdFn_IntelAtSilvCoreC1Recidency,

    kCpumMsrRdFn_P6LastBranchFromIp,
    kCpumMsrRdFn_P6LastBranchToIp,
    kCpumMsrRdFn_P6LastIntFromIp,
    kCpumMsrRdFn_P6LastIntToIp,

    kCpumMsrRdFn_AmdFam15hTscRate,
    kCpumMsrRdFn_AmdFam15hLwpCfg,
    kCpumMsrRdFn_AmdFam15hLwpCbAddr,
    kCpumMsrRdFn_AmdFam10hMc4MiscN,
    kCpumMsrRdFn_AmdK8PerfCtlN,
    kCpumMsrRdFn_AmdK8PerfCtrN,
    kCpumMsrRdFn_AmdK8SysCfg,               /**< Range value returned. */
    kCpumMsrRdFn_AmdK8HwCr,
    kCpumMsrRdFn_AmdK8IorrBaseN,
    kCpumMsrRdFn_AmdK8IorrMaskN,
    kCpumMsrRdFn_AmdK8TopOfMemN,
    kCpumMsrRdFn_AmdK8NbCfg1,
    kCpumMsrRdFn_AmdK8McXcptRedir,
    kCpumMsrRdFn_AmdK8CpuNameN,
    kCpumMsrRdFn_AmdK8HwThermalCtrl,        /**< Range value returned. */
    kCpumMsrRdFn_AmdK8SwThermalCtrl,
    kCpumMsrRdFn_AmdK8FidVidControl,        /**< Range value returned. */
    kCpumMsrRdFn_AmdK8FidVidStatus,         /**< Range value returned. */
    kCpumMsrRdFn_AmdK8McCtlMaskN,
    kCpumMsrRdFn_AmdK8SmiOnIoTrapN,
    kCpumMsrRdFn_AmdK8SmiOnIoTrapCtlSts,
    kCpumMsrRdFn_AmdK8IntPendingMessage,
    kCpumMsrRdFn_AmdK8SmiTriggerIoCycle,
    kCpumMsrRdFn_AmdFam10hMmioCfgBaseAddr,
    kCpumMsrRdFn_AmdFam10hTrapCtlMaybe,
    kCpumMsrRdFn_AmdFam10hPStateCurLimit,   /**< Returns range value. */
    kCpumMsrRdFn_AmdFam10hPStateControl,    /**< Returns range value. */
    kCpumMsrRdFn_AmdFam10hPStateStatus,     /**< Returns range value. */
    kCpumMsrRdFn_AmdFam10hPStateN,          /**< Returns range value. This isn't an register index! */
    kCpumMsrRdFn_AmdFam10hCofVidControl,    /**< Returns range value. */
    kCpumMsrRdFn_AmdFam10hCofVidStatus,     /**< Returns range value. */
    kCpumMsrRdFn_AmdFam10hCStateIoBaseAddr,
    kCpumMsrRdFn_AmdFam10hCpuWatchdogTimer,
    kCpumMsrRdFn_AmdK8SmmBase,
    kCpumMsrRdFn_AmdK8SmmAddr,
    kCpumMsrRdFn_AmdK8SmmMask,
    kCpumMsrRdFn_AmdK8VmCr,
    kCpumMsrRdFn_AmdK8IgnNe,
    kCpumMsrRdFn_AmdK8SmmCtl,
    kCpumMsrRdFn_AmdK8VmHSavePa,
    kCpumMsrRdFn_AmdFam10hVmLockKey,
    kCpumMsrRdFn_AmdFam10hSmmLockKey,
    kCpumMsrRdFn_AmdFam10hLocalSmiStatus,
    kCpumMsrRdFn_AmdFam10hOsVisWrkIdLength,
    kCpumMsrRdFn_AmdFam10hOsVisWrkStatus,
    kCpumMsrRdFn_AmdFam16hL2IPerfCtlN,
    kCpumMsrRdFn_AmdFam16hL2IPerfCtrN,
    kCpumMsrRdFn_AmdFam15hNorthbridgePerfCtlN,
    kCpumMsrRdFn_AmdFam15hNorthbridgePerfCtrN,
    kCpumMsrRdFn_AmdK7MicrocodeCtl,         /**< Returns range value. */
    kCpumMsrRdFn_AmdK7ClusterIdMaybe,       /**< Returns range value. */
    kCpumMsrRdFn_AmdK8CpuIdCtlStd07hEbax,
    kCpumMsrRdFn_AmdK8CpuIdCtlStd06hEcx,
    kCpumMsrRdFn_AmdK8CpuIdCtlStd01hEdcx,
    kCpumMsrRdFn_AmdK8CpuIdCtlExt01hEdcx,
    kCpumMsrRdFn_AmdK8PatchLevel,           /**< Returns range value. */
    kCpumMsrRdFn_AmdK7DebugStatusMaybe,
    kCpumMsrRdFn_AmdK7BHTraceBaseMaybe,
    kCpumMsrRdFn_AmdK7BHTracePtrMaybe,
    kCpumMsrRdFn_AmdK7BHTraceLimitMaybe,
    kCpumMsrRdFn_AmdK7HardwareDebugToolCfgMaybe,
    kCpumMsrRdFn_AmdK7FastFlushCountMaybe,
    kCpumMsrRdFn_AmdK7NodeId,
    kCpumMsrRdFn_AmdK7DrXAddrMaskN,      /**< Takes register index. */
    kCpumMsrRdFn_AmdK7Dr0DataMatchMaybe,
    kCpumMsrRdFn_AmdK7Dr0DataMaskMaybe,
    kCpumMsrRdFn_AmdK7LoadStoreCfg,
    kCpumMsrRdFn_AmdK7InstrCacheCfg,
    kCpumMsrRdFn_AmdK7DataCacheCfg,
    kCpumMsrRdFn_AmdK7BusUnitCfg,
    kCpumMsrRdFn_AmdK7DebugCtl2Maybe,
    kCpumMsrRdFn_AmdFam15hFpuCfg,
    kCpumMsrRdFn_AmdFam15hDecoderCfg,
    kCpumMsrRdFn_AmdFam10hBusUnitCfg2,
    kCpumMsrRdFn_AmdFam15hCombUnitCfg,
    kCpumMsrRdFn_AmdFam15hCombUnitCfg2,
    kCpumMsrRdFn_AmdFam15hCombUnitCfg3,
    kCpumMsrRdFn_AmdFam15hExecUnitCfg,
    kCpumMsrRdFn_AmdFam15hLoadStoreCfg2,
    kCpumMsrRdFn_AmdFam10hIbsFetchCtl,
    kCpumMsrRdFn_AmdFam10hIbsFetchLinAddr,
    kCpumMsrRdFn_AmdFam10hIbsFetchPhysAddr,
    kCpumMsrRdFn_AmdFam10hIbsOpExecCtl,
    kCpumMsrRdFn_AmdFam10hIbsOpRip,
    kCpumMsrRdFn_AmdFam10hIbsOpData,
    kCpumMsrRdFn_AmdFam10hIbsOpData2,
    kCpumMsrRdFn_AmdFam10hIbsOpData3,
    kCpumMsrRdFn_AmdFam10hIbsDcLinAddr,
    kCpumMsrRdFn_AmdFam10hIbsDcPhysAddr,
    kCpumMsrRdFn_AmdFam10hIbsCtl,
    kCpumMsrRdFn_AmdFam14hIbsBrTarget,

    kCpumMsrRdFn_Gim,

    /** End of valid MSR read function indexes. */
    kCpumMsrRdFn_End
} CPUMMSRRDFN;

/**
 * MSR write functions.
 */
typedef enum CPUMMSRWRFN
{
    /** Invalid zero value. */
    kCpumMsrWrFn_Invalid = 0,
    /** Writes are ignored, the fWrGpMask is observed though. */
    kCpumMsrWrFn_IgnoreWrite,
    /** Writes cause GP(0) to be raised, the fWrGpMask should be UINT64_MAX. */
    kCpumMsrWrFn_ReadOnly,
    /** Alias to the MSR range starting at the MSR given by
     * CPUMMSRRANGE::uValue.  Must be used in pair with
     * kCpumMsrRdFn_MsrAlias. */
    kCpumMsrWrFn_MsrAlias,

    kCpumMsrWrFn_Ia32P5McAddr,
    kCpumMsrWrFn_Ia32P5McType,
    kCpumMsrWrFn_Ia32TimestampCounter,
    kCpumMsrWrFn_Ia32ApicBase,
    kCpumMsrWrFn_Ia32FeatureControl,
    kCpumMsrWrFn_Ia32BiosSignId,
    kCpumMsrWrFn_Ia32BiosUpdateTrigger,
    kCpumMsrWrFn_Ia32SmmMonitorCtl,
    kCpumMsrWrFn_Ia32PmcN,
    kCpumMsrWrFn_Ia32MonitorFilterLineSize,
    kCpumMsrWrFn_Ia32MPerf,
    kCpumMsrWrFn_Ia32APerf,
    kCpumMsrWrFn_Ia32MtrrPhysBaseN,         /**< Takes register number. */
    kCpumMsrWrFn_Ia32MtrrPhysMaskN,         /**< Takes register number. */
    kCpumMsrWrFn_Ia32MtrrFixed,             /**< Takes CPUMCPU offset. */
    kCpumMsrWrFn_Ia32MtrrDefType,
    kCpumMsrWrFn_Ia32Pat,
    kCpumMsrWrFn_Ia32SysEnterCs,
    kCpumMsrWrFn_Ia32SysEnterEsp,
    kCpumMsrWrFn_Ia32SysEnterEip,
    kCpumMsrWrFn_Ia32McgStatus,
    kCpumMsrWrFn_Ia32McgCtl,
    kCpumMsrWrFn_Ia32DebugCtl,
    kCpumMsrWrFn_Ia32SmrrPhysBase,
    kCpumMsrWrFn_Ia32SmrrPhysMask,
    kCpumMsrWrFn_Ia32PlatformDcaCap,
    kCpumMsrWrFn_Ia32Dca0Cap,
    kCpumMsrWrFn_Ia32PerfEvtSelN,           /**< Range value indicates the register number. */
    kCpumMsrWrFn_Ia32PerfStatus,
    kCpumMsrWrFn_Ia32PerfCtl,
    kCpumMsrWrFn_Ia32FixedCtrN,             /**< Takes register number of start of range. */
    kCpumMsrWrFn_Ia32PerfCapabilities,
    kCpumMsrWrFn_Ia32FixedCtrCtrl,
    kCpumMsrWrFn_Ia32PerfGlobalStatus,
    kCpumMsrWrFn_Ia32PerfGlobalCtrl,
    kCpumMsrWrFn_Ia32PerfGlobalOvfCtrl,
    kCpumMsrWrFn_Ia32PebsEnable,
    kCpumMsrWrFn_Ia32ClockModulation,
    kCpumMsrWrFn_Ia32ThermInterrupt,
    kCpumMsrWrFn_Ia32ThermStatus,
    kCpumMsrWrFn_Ia32Therm2Ctl,
    kCpumMsrWrFn_Ia32MiscEnable,
    kCpumMsrWrFn_Ia32McCtlStatusAddrMiscN,  /**< Takes bank number. */
    kCpumMsrWrFn_Ia32McNCtl2,               /**< Takes register number of start of range. */
    kCpumMsrWrFn_Ia32DsArea,
    kCpumMsrWrFn_Ia32TscDeadline,
    kCpumMsrWrFn_Ia32X2ApicN,
    kCpumMsrWrFn_Ia32DebugInterface,
    kCpumMsrWrFn_Ia32SpecCtrl,
    kCpumMsrWrFn_Ia32PredCmd,
    kCpumMsrWrFn_Ia32FlushCmd,

    kCpumMsrWrFn_Amd64Efer,
    kCpumMsrWrFn_Amd64SyscallTarget,
    kCpumMsrWrFn_Amd64LongSyscallTarget,
    kCpumMsrWrFn_Amd64CompSyscallTarget,
    kCpumMsrWrFn_Amd64SyscallFlagMask,
    kCpumMsrWrFn_Amd64FsBase,
    kCpumMsrWrFn_Amd64GsBase,
    kCpumMsrWrFn_Amd64KernelGsBase,
    kCpumMsrWrFn_Amd64TscAux,
    kCpumMsrWrFn_IntelEblCrPowerOn,
    kCpumMsrWrFn_IntelP4EbcHardPowerOn,
    kCpumMsrWrFn_IntelP4EbcSoftPowerOn,
    kCpumMsrWrFn_IntelP4EbcFrequencyId,
    kCpumMsrWrFn_IntelFlexRatio,
    kCpumMsrWrFn_IntelPkgCStConfigControl,
    kCpumMsrWrFn_IntelPmgIoCaptureBase,
    kCpumMsrWrFn_IntelLastBranchFromToN,
    kCpumMsrWrFn_IntelLastBranchFromN,
    kCpumMsrWrFn_IntelLastBranchToN,
    kCpumMsrWrFn_IntelLastBranchTos,
    kCpumMsrWrFn_IntelBblCrCtl,
    kCpumMsrWrFn_IntelBblCrCtl3,
    kCpumMsrWrFn_IntelI7TemperatureTarget,
    kCpumMsrWrFn_IntelI7MsrOffCoreResponseN, /**< Takes register number. */
    kCpumMsrWrFn_IntelI7MiscPwrMgmt,
    kCpumMsrWrFn_IntelP6CrN,
    kCpumMsrWrFn_IntelCpuId1FeatureMaskEcdx,
    kCpumMsrWrFn_IntelCpuId1FeatureMaskEax,
    kCpumMsrWrFn_IntelCpuId80000001FeatureMaskEcdx,
    kCpumMsrWrFn_IntelI7SandyAesNiCtl,
    kCpumMsrWrFn_IntelI7TurboRatioLimit,
    kCpumMsrWrFn_IntelI7LbrSelect,
    kCpumMsrWrFn_IntelI7SandyErrorControl,
    kCpumMsrWrFn_IntelI7PowerCtl,
    kCpumMsrWrFn_IntelI7SandyPebsNumAlt,
    kCpumMsrWrFn_IntelI7PebsLdLat,
    kCpumMsrWrFn_IntelI7SandyVrCurrentConfig,
    kCpumMsrWrFn_IntelI7SandyVrMiscConfig,
    kCpumMsrWrFn_IntelI7SandyRaplPowerUnit,  /**< R/O but found writable bits on a Silvermont CPU here. */
    kCpumMsrWrFn_IntelI7SandyPkgCnIrtlN,
    kCpumMsrWrFn_IntelI7SandyPkgC2Residency, /**< R/O but found writable bits on a Silvermont CPU here. */
    kCpumMsrWrFn_IntelI7RaplPkgPowerLimit,
    kCpumMsrWrFn_IntelI7RaplDramPowerLimit,
    kCpumMsrWrFn_IntelI7RaplPp0PowerLimit,
    kCpumMsrWrFn_IntelI7RaplPp0Policy,
    kCpumMsrWrFn_IntelI7RaplPp1PowerLimit,
    kCpumMsrWrFn_IntelI7RaplPp1Policy,
    kCpumMsrWrFn_IntelI7IvyConfigTdpControl,
    kCpumMsrWrFn_IntelI7IvyTurboActivationRatio,
    kCpumMsrWrFn_IntelI7UncPerfGlobalCtrl,
    kCpumMsrWrFn_IntelI7UncPerfGlobalStatus,
    kCpumMsrWrFn_IntelI7UncPerfGlobalOvfCtrl,
    kCpumMsrWrFn_IntelI7UncPerfFixedCtrCtrl,
    kCpumMsrWrFn_IntelI7UncPerfFixedCtr,
    kCpumMsrWrFn_IntelI7UncArbPerfCtrN,
    kCpumMsrWrFn_IntelI7UncArbPerfEvtSelN,
    kCpumMsrWrFn_IntelCore2EmttmCrTablesN,
    kCpumMsrWrFn_IntelCore2SmmCStMiscInfo,
    kCpumMsrWrFn_IntelCore1ExtConfig,
    kCpumMsrWrFn_IntelCore1DtsCalControl,
    kCpumMsrWrFn_IntelCore2PeciControl,

    kCpumMsrWrFn_P6LastIntFromIp,
    kCpumMsrWrFn_P6LastIntToIp,

    kCpumMsrWrFn_AmdFam15hTscRate,
    kCpumMsrWrFn_AmdFam15hLwpCfg,
    kCpumMsrWrFn_AmdFam15hLwpCbAddr,
    kCpumMsrWrFn_AmdFam10hMc4MiscN,
    kCpumMsrWrFn_AmdK8PerfCtlN,
    kCpumMsrWrFn_AmdK8PerfCtrN,
    kCpumMsrWrFn_AmdK8SysCfg,
    kCpumMsrWrFn_AmdK8HwCr,
    kCpumMsrWrFn_AmdK8IorrBaseN,
    kCpumMsrWrFn_AmdK8IorrMaskN,
    kCpumMsrWrFn_AmdK8TopOfMemN,
    kCpumMsrWrFn_AmdK8NbCfg1,
    kCpumMsrWrFn_AmdK8McXcptRedir,
    kCpumMsrWrFn_AmdK8CpuNameN,
    kCpumMsrWrFn_AmdK8HwThermalCtrl,
    kCpumMsrWrFn_AmdK8SwThermalCtrl,
    kCpumMsrWrFn_AmdK8FidVidControl,
    kCpumMsrWrFn_AmdK8McCtlMaskN,
    kCpumMsrWrFn_AmdK8SmiOnIoTrapN,
    kCpumMsrWrFn_AmdK8SmiOnIoTrapCtlSts,
    kCpumMsrWrFn_AmdK8IntPendingMessage,
    kCpumMsrWrFn_AmdK8SmiTriggerIoCycle,
    kCpumMsrWrFn_AmdFam10hMmioCfgBaseAddr,
    kCpumMsrWrFn_AmdFam10hTrapCtlMaybe,
    kCpumMsrWrFn_AmdFam10hPStateControl,
    kCpumMsrWrFn_AmdFam10hPStateStatus,
    kCpumMsrWrFn_AmdFam10hPStateN,
    kCpumMsrWrFn_AmdFam10hCofVidControl,
    kCpumMsrWrFn_AmdFam10hCofVidStatus,
    kCpumMsrWrFn_AmdFam10hCStateIoBaseAddr,
    kCpumMsrWrFn_AmdFam10hCpuWatchdogTimer,
    kCpumMsrWrFn_AmdK8SmmBase,
    kCpumMsrWrFn_AmdK8SmmAddr,
    kCpumMsrWrFn_AmdK8SmmMask,
    kCpumMsrWrFn_AmdK8VmCr,
    kCpumMsrWrFn_AmdK8IgnNe,
    kCpumMsrWrFn_AmdK8SmmCtl,
    kCpumMsrWrFn_AmdK8VmHSavePa,
    kCpumMsrWrFn_AmdFam10hVmLockKey,
    kCpumMsrWrFn_AmdFam10hSmmLockKey,
    kCpumMsrWrFn_AmdFam10hLocalSmiStatus,
    kCpumMsrWrFn_AmdFam10hOsVisWrkIdLength,
    kCpumMsrWrFn_AmdFam10hOsVisWrkStatus,
    kCpumMsrWrFn_AmdFam16hL2IPerfCtlN,
    kCpumMsrWrFn_AmdFam16hL2IPerfCtrN,
    kCpumMsrWrFn_AmdFam15hNorthbridgePerfCtlN,
    kCpumMsrWrFn_AmdFam15hNorthbridgePerfCtrN,
    kCpumMsrWrFn_AmdK7MicrocodeCtl,
    kCpumMsrWrFn_AmdK7ClusterIdMaybe,
    kCpumMsrWrFn_AmdK8CpuIdCtlStd07hEbax,
    kCpumMsrWrFn_AmdK8CpuIdCtlStd06hEcx,
    kCpumMsrWrFn_AmdK8CpuIdCtlStd01hEdcx,
    kCpumMsrWrFn_AmdK8CpuIdCtlExt01hEdcx,
    kCpumMsrWrFn_AmdK8PatchLoader,
    kCpumMsrWrFn_AmdK7DebugStatusMaybe,
    kCpumMsrWrFn_AmdK7BHTraceBaseMaybe,
    kCpumMsrWrFn_AmdK7BHTracePtrMaybe,
    kCpumMsrWrFn_AmdK7BHTraceLimitMaybe,
    kCpumMsrWrFn_AmdK7HardwareDebugToolCfgMaybe,
    kCpumMsrWrFn_AmdK7FastFlushCountMaybe,
    kCpumMsrWrFn_AmdK7NodeId,
    kCpumMsrWrFn_AmdK7DrXAddrMaskN,      /**< Takes register index. */
    kCpumMsrWrFn_AmdK7Dr0DataMatchMaybe,
    kCpumMsrWrFn_AmdK7Dr0DataMaskMaybe,
    kCpumMsrWrFn_AmdK7LoadStoreCfg,
    kCpumMsrWrFn_AmdK7InstrCacheCfg,
    kCpumMsrWrFn_AmdK7DataCacheCfg,
    kCpumMsrWrFn_AmdK7BusUnitCfg,
    kCpumMsrWrFn_AmdK7DebugCtl2Maybe,
    kCpumMsrWrFn_AmdFam15hFpuCfg,
    kCpumMsrWrFn_AmdFam15hDecoderCfg,
    kCpumMsrWrFn_AmdFam10hBusUnitCfg2,
    kCpumMsrWrFn_AmdFam15hCombUnitCfg,
    kCpumMsrWrFn_AmdFam15hCombUnitCfg2,
    kCpumMsrWrFn_AmdFam15hCombUnitCfg3,
    kCpumMsrWrFn_AmdFam15hExecUnitCfg,
    kCpumMsrWrFn_AmdFam15hLoadStoreCfg2,
    kCpumMsrWrFn_AmdFam10hIbsFetchCtl,
    kCpumMsrWrFn_AmdFam10hIbsFetchLinAddr,
    kCpumMsrWrFn_AmdFam10hIbsFetchPhysAddr,
    kCpumMsrWrFn_AmdFam10hIbsOpExecCtl,
    kCpumMsrWrFn_AmdFam10hIbsOpRip,
    kCpumMsrWrFn_AmdFam10hIbsOpData,
    kCpumMsrWrFn_AmdFam10hIbsOpData2,
    kCpumMsrWrFn_AmdFam10hIbsOpData3,
    kCpumMsrWrFn_AmdFam10hIbsDcLinAddr,
    kCpumMsrWrFn_AmdFam10hIbsDcPhysAddr,
    kCpumMsrWrFn_AmdFam10hIbsCtl,
    kCpumMsrWrFn_AmdFam14hIbsBrTarget,

    kCpumMsrWrFn_Gim,

    /** End of valid MSR write function indexes. */
    kCpumMsrWrFn_End
} CPUMMSRWRFN;

/**
 * MSR range.
 */
typedef struct CPUMMSRRANGE
{
    /** The first MSR. [0] */
    uint32_t    uFirst;
    /** The last MSR. [4] */
    uint32_t    uLast;
    /** The read function (CPUMMSRRDFN). [8] */
    uint16_t    enmRdFn;
    /** The write function (CPUMMSRWRFN). [10] */
    uint16_t    enmWrFn;
    /** The offset of the 64-bit MSR value relative to the start of CPUMCPU.
     * UINT16_MAX if not used by the read and write functions.  [12] */
    uint32_t    offCpumCpu : 24;
    /** Reserved for future hacks. [15] */
    uint32_t    fReserved : 8;
    /** The init/read value. [16]
     * When enmRdFn is kCpumMsrRdFn_INIT_VALUE, this is the value returned on RDMSR.
     * offCpumCpu must be UINT16_MAX in that case, otherwise it must be a valid
     * offset into CPUM. */
    uint64_t    uValue;
    /** The bits to ignore when writing. [24]   */
    uint64_t    fWrIgnMask;
    /** The bits that will cause a GP(0) when writing. [32]
     * This is always checked prior to calling the write function.  Using
     * UINT64_MAX effectively marks the MSR as read-only. */
    uint64_t    fWrGpMask;
    /** The register name, if applicable. [40] */
    char        szName[56];

    /** The number of reads. */
    STAMCOUNTER cReads;
    /** The number of writes. */
    STAMCOUNTER cWrites;
    /** The number of times ignored bits were written. */
    STAMCOUNTER cIgnoredBits;
    /** The number of GPs generated. */
    STAMCOUNTER cGps;
} CPUMMSRRANGE;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(CPUMMSRRANGE, 128);
#endif
/** Pointer to an MSR range. */
typedef CPUMMSRRANGE *PCPUMMSRRANGE;
/** Pointer to a const MSR range. */
typedef CPUMMSRRANGE const *PCCPUMMSRRANGE;


/**
 * MSRs which are required while exploding features.
 */
typedef struct CPUMMSRS
{
    union
    {
        VMXMSRS         vmx;
        SVMMSRS         svm;
    } hwvirt;
} CPUMMSRS;
/** Pointer to an CPUMMSRS struct. */
typedef CPUMMSRS *PCPUMMSRS;
/** Pointer to a const CPUMMSRS struct. */
typedef CPUMMSRS const *PCCPUMMSRS;


/**
 * CPU features and quirks.
 * This is mostly exploded CPUID info.
 */
typedef struct CPUMFEATURES
{
    /** The CPU vendor (CPUMCPUVENDOR). */
    uint8_t         enmCpuVendor;
    /** The CPU family. */
    uint8_t         uFamily;
    /** The CPU model. */
    uint8_t         uModel;
    /** The CPU stepping. */
    uint8_t         uStepping;
    /** The microarchitecture. */
#ifndef VBOX_FOR_DTRACE_LIB
    CPUMMICROARCH   enmMicroarch;
#else
    uint32_t        enmMicroarch;
#endif
    /** The maximum physical address width of the CPU. */
    uint8_t         cMaxPhysAddrWidth;
    /** The maximum linear address width of the CPU. */
    uint8_t         cMaxLinearAddrWidth;
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
    /** Supports IA32_SPEC_CTRL.IBRS. */
    uint32_t        fIbrs : 1;
    /** Supports IA32_SPEC_CTRL.STIBP. */
    uint32_t        fStibp : 1;
    /** Supports IA32_FLUSH_CMD. */
    uint32_t        fFlushCmd : 1;
    /** Supports IA32_ARCH_CAP. */
    uint32_t        fArchCap : 1;
    /** Supports MD_CLEAR functionality (VERW, IA32_FLUSH_CMD). */
    uint32_t        fMdsClear : 1;
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
    uint32_t        fPadding0 : 28;


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
} CPUMFEATURES;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(CPUMFEATURES, 48);
#endif
/** Pointer to a CPU feature structure. */
typedef CPUMFEATURES *PCPUMFEATURES;
/** Pointer to a const CPU feature structure. */
typedef CPUMFEATURES const *PCCPUMFEATURES;

/**
 * Chameleon wrapper structure for the host CPU features.
 *
 * This is used for the globally readable g_CpumHostFeatures variable, which is
 * initialized once during VMMR0 load for ring-0 and during CPUMR3Init in
 * ring-3.  To reflect this immutability after load/init, we use this wrapper
 * structure to switch it between const and non-const depending on the context.
 * Only two files sees it as non-const (CPUMR0.cpp and CPUM.cpp).
 */
typedef struct CPUHOSTFEATURES
{
    CPUMFEATURES
#ifndef CPUM_WITH_NONCONST_HOST_FEATURES
    const
#endif
                    s;
} CPUHOSTFEATURES;
/** Pointer to a const host CPU feature structure. */
typedef CPUHOSTFEATURES const *PCCPUHOSTFEATURES;

/** Host CPU features.
 * @note In ring-3, only valid after CPUMR3Init.  In ring-0, valid after
 *       module init. */
extern CPUHOSTFEATURES g_CpumHostFeatures;


/**
 * CPU database entry.
 */
typedef struct CPUMDBENTRY
{
    /** The CPU name. */
    const char     *pszName;
    /** The full CPU name. */
    const char     *pszFullName;
    /** The CPU vendor (CPUMCPUVENDOR). */
    uint8_t         enmVendor;
    /** The CPU family. */
    uint8_t         uFamily;
    /** The CPU model. */
    uint8_t         uModel;
    /** The CPU stepping. */
    uint8_t         uStepping;
    /** The microarchitecture. */
    CPUMMICROARCH   enmMicroarch;
    /** Scalable bus frequency used for reporting other frequencies. */
    uint64_t        uScalableBusFreq;
    /** Flags - CPUMDB_F_XXX. */
    uint32_t        fFlags;
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
    PCCPUMMSRRANGE  paMsrRanges;
} CPUMDBENTRY;
/** Pointer to a const CPU database entry. */
typedef CPUMDBENTRY const *PCCPUMDBENTRY;

/** @name CPUMDB_F_XXX - CPUDBENTRY::fFlags
 * @{ */
/** Should execute all in IEM.
 * @todo Implement this - currently done in Main...  */
#define CPUMDB_F_EXECUTE_ALL_IN_IEM         RT_BIT_32(0)
/** @} */



#ifndef VBOX_FOR_DTRACE_LIB

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
VMMDECL(int)            CPUMCpuIdCollectLeavesX86(PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves);
VMMDECL(CPUMCPUVENDOR)  CPUMCpuIdDetectX86VendorEx(uint32_t uEAX, uint32_t uEBX, uint32_t uECX, uint32_t uEDX);
#endif

VMM_INT_DECL(bool)      CPUMAssertGuestRFlagsCookie(PVM pVM, PVMCPU pVCpu);


/** @name Guest Register Getters.
 * @{ */
VMMDECL(void)           CPUMGetGuestGDTR(PCVMCPU pVCpu, PVBOXGDTR pGDTR);
VMMDECL(RTGCPTR)        CPUMGetGuestIDTR(PCVMCPU pVCpu, uint16_t *pcbLimit);
VMMDECL(RTSEL)          CPUMGetGuestTR(PCVMCPU pVCpu, PCPUMSELREGHID pHidden);
VMMDECL(RTSEL)          CPUMGetGuestLDTR(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestLdtrEx(PCVMCPU pVCpu, uint64_t *pGCPtrBase, uint32_t *pcbLimit);
VMMDECL(uint64_t)       CPUMGetGuestCR0(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestCR2(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestCR3(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestCR4(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestCR8(PCVMCPUCC pVCpu);
VMMDECL(int)            CPUMGetGuestCRx(PCVMCPUCC pVCpu, unsigned iReg, uint64_t *pValue);
VMMDECL(uint32_t)       CPUMGetGuestEFlags(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestEIP(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestRIP(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestEAX(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestEBX(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestECX(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestEDX(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestESI(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestEDI(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestESP(PCVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestEBP(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestCS(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestDS(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestES(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestFS(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestGS(PCVMCPU pVCpu);
VMMDECL(RTSEL)          CPUMGetGuestSS(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestDR0(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestDR1(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestDR2(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestDR3(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestDR6(PCVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestDR7(PCVMCPU pVCpu);
VMMDECL(int)            CPUMGetGuestDRx(PCVMCPU pVCpu, uint32_t iReg, uint64_t *pValue);
VMMDECL(void)           CPUMGetGuestCpuId(PVMCPUCC pVCpu, uint32_t iLeaf, uint32_t iSubLeaf, int f64BitMode,
                                          uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx);
VMMDECL(uint64_t)       CPUMGetGuestEFER(PCVMCPU pVCpu);
VMM_INT_DECL(uint64_t)  CPUMGetGuestIa32FeatCtrl(PCVMCPUCC pVCpu);
VMM_INT_DECL(uint64_t)  CPUMGetGuestIa32MtrrCap(PCVMCPUCC pVCpu);
VMM_INT_DECL(uint64_t)  CPUMGetGuestIa32SmmMonitorCtl(PCVMCPUCC pVCpu);
VMM_INT_DECL(uint64_t)  CPUMGetGuestIa32VmxEptVpidCap(PCVMCPUCC pVCpu);
VMMDECL(VBOXSTRICTRC)   CPUMQueryGuestMsr(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t *puValue);
VMMDECL(VBOXSTRICTRC)   CPUMSetGuestMsr(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t uValue);
/** @} */

/** @name Guest Register Setters.
 * @{ */
VMMDECL(int)           CPUMSetGuestGDTR(PVMCPU pVCpu, uint64_t GCPtrBase, uint16_t cbLimit);
VMMDECL(int)           CPUMSetGuestIDTR(PVMCPU pVCpu, uint64_t GCPtrBase, uint16_t cbLimit);
VMMDECL(int)           CPUMSetGuestTR(PVMCPU pVCpu, uint16_t tr);
VMMDECL(int)           CPUMSetGuestLDTR(PVMCPU pVCpu, uint16_t ldtr);
VMMDECL(int)           CPUMSetGuestCR0(PVMCPUCC pVCpu, uint64_t cr0);
VMMDECL(int)           CPUMSetGuestCR2(PVMCPU pVCpu, uint64_t cr2);
VMMDECL(int)           CPUMSetGuestCR3(PVMCPU pVCpu, uint64_t cr3);
VMMDECL(int)           CPUMSetGuestCR4(PVMCPU pVCpu, uint64_t cr4);
VMMDECL(int)           CPUMSetGuestDR0(PVMCPUCC pVCpu, uint64_t uDr0);
VMMDECL(int)           CPUMSetGuestDR1(PVMCPUCC pVCpu, uint64_t uDr1);
VMMDECL(int)           CPUMSetGuestDR2(PVMCPUCC pVCpu, uint64_t uDr2);
VMMDECL(int)           CPUMSetGuestDR3(PVMCPUCC pVCpu, uint64_t uDr3);
VMMDECL(int)           CPUMSetGuestDR6(PVMCPU pVCpu, uint64_t uDr6);
VMMDECL(int)           CPUMSetGuestDR7(PVMCPUCC pVCpu, uint64_t uDr7);
VMMDECL(int)           CPUMSetGuestDRx(PVMCPUCC pVCpu, uint32_t iReg, uint64_t Value);
VMM_INT_DECL(int)      CPUMSetGuestXcr0(PVMCPUCC pVCpu, uint64_t uNewValue);
VMMDECL(int)           CPUMSetGuestEFlags(PVMCPU pVCpu, uint32_t eflags);
VMMDECL(int)           CPUMSetGuestEIP(PVMCPU pVCpu, uint32_t eip);
VMMDECL(int)           CPUMSetGuestEAX(PVMCPU pVCpu, uint32_t eax);
VMMDECL(int)           CPUMSetGuestEBX(PVMCPU pVCpu, uint32_t ebx);
VMMDECL(int)           CPUMSetGuestECX(PVMCPU pVCpu, uint32_t ecx);
VMMDECL(int)           CPUMSetGuestEDX(PVMCPU pVCpu, uint32_t edx);
VMMDECL(int)           CPUMSetGuestESI(PVMCPU pVCpu, uint32_t esi);
VMMDECL(int)           CPUMSetGuestEDI(PVMCPU pVCpu, uint32_t edi);
VMMDECL(int)           CPUMSetGuestESP(PVMCPU pVCpu, uint32_t esp);
VMMDECL(int)           CPUMSetGuestEBP(PVMCPU pVCpu, uint32_t ebp);
VMMDECL(int)           CPUMSetGuestCS(PVMCPU pVCpu, uint16_t cs);
VMMDECL(int)           CPUMSetGuestDS(PVMCPU pVCpu, uint16_t ds);
VMMDECL(int)           CPUMSetGuestES(PVMCPU pVCpu, uint16_t es);
VMMDECL(int)           CPUMSetGuestFS(PVMCPU pVCpu, uint16_t fs);
VMMDECL(int)           CPUMSetGuestGS(PVMCPU pVCpu, uint16_t gs);
VMMDECL(int)           CPUMSetGuestSS(PVMCPU pVCpu, uint16_t ss);
VMMDECL(void)          CPUMSetGuestEFER(PVMCPU pVCpu, uint64_t val);
VMMR3_INT_DECL(void)   CPUMR3SetGuestCpuIdFeature(PVM pVM, CPUMCPUIDFEATURE enmFeature);
VMMR3_INT_DECL(void)   CPUMR3ClearGuestCpuIdFeature(PVM pVM, CPUMCPUIDFEATURE enmFeature);
VMMR3_INT_DECL(bool)   CPUMR3GetGuestCpuIdFeature(PVM pVM, CPUMCPUIDFEATURE enmFeature);
VMMDECL(bool)          CPUMSetGuestCpuIdPerCpuApicFeature(PVMCPU pVCpu, bool fVisible);
VMMDECL(void)          CPUMSetGuestCtx(PVMCPU pVCpu, const PCPUMCTX pCtx);
VMM_INT_DECL(void)     CPUMSetGuestTscAux(PVMCPUCC pVCpu, uint64_t uValue);
VMM_INT_DECL(uint64_t) CPUMGetGuestTscAux(PVMCPUCC pVCpu);
VMM_INT_DECL(void)     CPUMSetGuestSpecCtrl(PVMCPUCC pVCpu, uint64_t uValue);
VMM_INT_DECL(uint64_t) CPUMGetGuestSpecCtrl(PVMCPUCC pVCpu);
VMM_INT_DECL(uint64_t) CPUMGetGuestCR4ValidMask(PVM pVM);
VMM_INT_DECL(void)     CPUMSetGuestPaePdpes(PVMCPU pVCpu, PCX86PDPE paPaePdpes);
VMM_INT_DECL(void)     CPUMGetGuestPaePdpes(PVMCPU pVCpu, PX86PDPE paPaePdpes);
/** @} */


/** @name Misc Guest Predicate Functions.
 * @{  */
VMMDECL(bool)       CPUMIsGuestNXEnabled(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestPageSizeExtEnabled(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestPagingEnabled(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestR0WriteProtEnabled(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestInRealMode(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestInRealOrV86Mode(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestInProtectedMode(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestInPagedProtectedMode(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestInLongMode(PCVMCPU pVCpu);
VMMDECL(bool)       CPUMIsGuestInPAEMode(PCVMCPU pVCpu);
/** @} */

/** @name Nested Hardware-Virtualization Helpers.
 * @{  */
VMM_INT_DECL(bool)      CPUMIsGuestPhysIntrEnabled(PVMCPU pVCpu);
VMM_INT_DECL(bool)      CPUMIsGuestVirtIntrEnabled(PVMCPU pVCpu);
VMM_INT_DECL(uint64_t)  CPUMApplyNestedGuestTscOffset(PCVMCPU pVCpu, uint64_t uTscValue);
VMM_INT_DECL(uint64_t)  CPUMRemoveNestedGuestTscOffset(PCVMCPU pVCpu, uint64_t uTscValue);

/* SVM helpers. */
VMM_INT_DECL(bool)      CPUMIsGuestSvmPhysIntrEnabled(PCVMCPU pVCpu, PCCPUMCTX pCtx);
VMM_INT_DECL(bool)      CPUMIsGuestSvmVirtIntrEnabled(PCVMCPU pVCpu, PCCPUMCTX pCtx);
VMM_INT_DECL(uint8_t)   CPUMGetGuestSvmVirtIntrVector(PCCPUMCTX pCtx);
VMM_INT_DECL(void)      CPUMSvmVmExitRestoreHostState(PVMCPUCC pVCpu, PCPUMCTX pCtx);
VMM_INT_DECL(void)      CPUMSvmVmRunSaveHostState(PCPUMCTX pCtx, uint8_t cbInstr);
VMM_INT_DECL(bool)      CPUMIsSvmIoInterceptSet(void *pvIoBitmap, uint16_t u16Port, SVMIOIOTYPE enmIoType, uint8_t cbReg,
                                                uint8_t cAddrSizeBits, uint8_t iEffSeg, bool fRep, bool fStrIo,
                                                PSVMIOIOEXITINFO pIoExitInfo);
VMM_INT_DECL(int)       CPUMGetSvmMsrpmOffsetAndBit(uint32_t idMsr, uint16_t *pbOffMsrpm, uint8_t *puMsrpmBit);

/* VMX helpers. */
VMM_INT_DECL(bool)      CPUMIsGuestVmxVmcsFieldValid(PVMCC pVM, uint64_t u64VmcsField);
VMM_INT_DECL(bool)      CPUMIsGuestVmxIoInterceptSet(PCVMCPU pVCpu, uint16_t u16Port, uint8_t cbAccess);
VMM_INT_DECL(bool)      CPUMIsGuestVmxMovToCr3InterceptSet(PVMCPU pVCpu, uint64_t uNewCr3);
VMM_INT_DECL(bool)      CPUMIsGuestVmxVmreadVmwriteInterceptSet(PCVMCPU pVCpu, uint32_t uExitReason, uint64_t u64FieldEnc);
VMM_INT_DECL(int)       CPUMStartGuestVmxPremptTimer(PVMCPUCC pVCpu, uint32_t uTimer, uint8_t cShift, uint64_t *pu64EntryTick);
VMM_INT_DECL(int)       CPUMStopGuestVmxPremptTimer(PVMCPUCC pVCpu);
VMM_INT_DECL(uint32_t)  CPUMGetVmxMsrPermission(void const *pvMsrBitmap, uint32_t idMsr);
VMM_INT_DECL(bool)      CPUMIsGuestVmxEptPagingEnabled(PCVMCPUCC pVCpu);
VMM_INT_DECL(bool)      CPUMIsGuestVmxEptPaePagingEnabled(PCVMCPUCC pVCpu);
VMM_INT_DECL(uint64_t)  CPUMGetGuestVmxApicAccessPageAddr(PCVMCPUCC pVCpu);
/** @} */

#if !defined(IPRT_WITHOUT_NAMED_UNIONS_AND_STRUCTS) || defined(DOXYGEN_RUNNING)
/** @name Inlined Guest Getters and predicates Functions.
 * @{ */

/**
 * Gets valid CR0 bits for the guest.
 *
 * @returns Valid CR0 bits.
 */
DECLINLINE(uint64_t) CPUMGetGuestCR0ValidMask(void)
{
    return (  X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS
            | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | X86_CR0_AM
            | X86_CR0_NW | X86_CR0_CD | X86_CR0_PG);
}

/**
 * Tests if the guest is running in real mode or not.
 *
 * @returns true if in real mode, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInRealModeEx(PCCPUMCTX pCtx)
{
    return !(pCtx->cr0 & X86_CR0_PE);
}

/**
 * Tests if the guest is running in real or virtual 8086 mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInRealOrV86ModeEx(PCCPUMCTX pCtx)
{
    return !(pCtx->cr0 & X86_CR0_PE)
        || pCtx->eflags.Bits.u1VM;  /* Cannot be set in long mode. Intel spec 2.3.1 "System Flags and Fields in IA-32e Mode". */
}

/**
 * Tests if the guest is running in virtual 8086 mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInV86ModeEx(PCCPUMCTX pCtx)
{
    return (pCtx->eflags.Bits.u1VM == 1);
}

/**
 * Tests if the guest is running in paged protected or not.
 *
 * @returns true if in paged protected mode, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInPagedProtectedModeEx(PCPUMCTX pCtx)
{
    return (pCtx->cr0 & (X86_CR0_PE | X86_CR0_PG)) == (X86_CR0_PE | X86_CR0_PG);
}

/**
 * Tests if the guest is running in long mode or not.
 *
 * @returns true if in long mode, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInLongModeEx(PCCPUMCTX pCtx)
{
    return (pCtx->msrEFER & MSR_K6_EFER_LMA) == MSR_K6_EFER_LMA;
}

VMM_INT_DECL(bool) CPUMIsGuestIn64BitCodeSlow(PCCPUMCTX pCtx);

/**
 * Tests if the guest is running in 64 bits mode or not.
 *
 * @returns true if in 64 bits protected mode, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestIn64BitCodeEx(PCCPUMCTX pCtx)
{
    if (!(pCtx->msrEFER & MSR_K6_EFER_LMA))
        return false;
    if (!CPUMSELREG_ARE_HIDDEN_PARTS_VALID(NULL, &pCtx->cs))
        return CPUMIsGuestIn64BitCodeSlow(pCtx);
    return pCtx->cs.Attr.n.u1Long;
}

/**
 * Tests if the guest has paging enabled or not.
 *
 * @returns true if paging is enabled, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestPagingEnabledEx(PCCPUMCTX pCtx)
{
    return !!(pCtx->cr0 & X86_CR0_PG);
}

/**
 * Tests if PAE paging is enabled given the relevant control registers.
 *
 * @returns @c true if in PAE mode, @c false otherwise.
 * @param   uCr0        The CR0 value.
 * @param   uCr4        The CR4 value.
 * @param   uEferMsr    The EFER value.
 */
DECLINLINE(bool) CPUMIsPaePagingEnabled(uint64_t uCr0, uint64_t uCr4, uint64_t uEferMsr)
{
    /* Intel mentions EFER.LMA and EFER.LME in different parts of their spec. We shall use EFER.LMA rather
       than EFER.LME as it reflects if the CPU has entered paging with EFER.LME set.  */
    return (   (uCr4 & X86_CR4_PAE)
            && (uCr0 & X86_CR0_PG)
            && !(uEferMsr & MSR_K6_EFER_LMA));
}

/**
 * Tests if the guest is running in PAE mode or not.
 *
 * @returns @c true if in PAE mode, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInPAEModeEx(PCCPUMCTX pCtx)
{
    return CPUMIsPaePagingEnabled(pCtx->cr0, pCtx->cr4, pCtx->msrEFER);
}

/**
 * Tests if the guest has AMD SVM enabled or not.
 *
 * @returns true if SMV is enabled, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestSvmEnabled(PCCPUMCTX pCtx)
{
    return RT_BOOL(pCtx->msrEFER & MSR_K6_EFER_SVME);
}

/**
 * Tests if the guest has Intel VT-x enabled or not.
 *
 * @returns true if VMX is enabled, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxEnabled(PCCPUMCTX pCtx)
{
    return RT_BOOL(pCtx->cr4 & X86_CR4_VMXE);
}

/**
 * Returns the guest's global-interrupt (GIF) flag.
 *
 * @returns true when global-interrupts are enabled, otherwise false.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMGetGuestGif(PCCPUMCTX pCtx)
{
    return pCtx->hwvirt.fGif;
}

/**
 * Sets the guest's global-interrupt flag (GIF).
 *
 * @param   pCtx    Current CPU context.
 * @param   fGif    The value to set.
 */
DECLINLINE(void) CPUMSetGuestGif(PCPUMCTX pCtx, bool fGif)
{
    pCtx->hwvirt.fGif = fGif;
}

/**
 * Checks if we're in an "interrupt shadow", i.e. after a STI, POP SS or MOV SS.
 *
 * This also inhibit NMIs, except perhaps for nested guests.
 *
 * @returns true if interrupts are inhibited by interrupt shadow, false if not.
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 * @note    Does NOT clear CPUMCTX_INHIBIT_SHADOW when CPUMCTX::uRipInhibitInt
 *          differs from CPUMCTX::rip.
 */
DECLINLINE(bool) CPUMIsInInterruptShadow(PCCPUMCTX pCtx)
{
    if (!(pCtx->eflags.uBoth & CPUMCTX_INHIBIT_SHADOW))
        return false;

    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    return pCtx->uRipInhibitInt == pCtx->rip;
}

/**
 * Checks if we're in an "interrupt shadow", i.e. after a STI, POP SS or MOV SS,
 * updating the state if stale.
 *
 * This also inhibit NMIs, except perhaps for nested guests.
 *
 * @retval  true if interrupts are inhibited by interrupt shadow.
 * @retval  false if not.
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 */
DECLINLINE(bool) CPUMIsInInterruptShadowWithUpdate(PCPUMCTX pCtx)
{
    if (!(pCtx->eflags.uBoth & CPUMCTX_INHIBIT_SHADOW))
        return false;

    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    if (pCtx->uRipInhibitInt == pCtx->rip)
        return true;

    pCtx->eflags.uBoth &= ~CPUMCTX_INHIBIT_SHADOW;
    return false;
}

/**
 * Checks if we're in an "interrupt shadow", i.e. after a STI, POP SS or MOV SS,
 * updating the state if stale while also returning the reason for the interrupt
 * inhibition.
 *
 * This also inhibit NMIs, except perhaps for nested guests.
 *
 * @retval  true if interrupts are inhibited by interrupt shadow.
 * @retval  false if not.
 * @param   pCtx            Current guest CPU context.
 * @param   pfInhibitShw    Where to store which type of interrupt inhibition was
 *                          active (see CPUMCTX_INHIBIT_XXX).
 * @note    Requires pCtx->rip to be up to date.
 */
DECLINLINE(bool) CPUMIsInInterruptShadowWithUpdateEx(PCPUMCTX pCtx, uint32_t *pfInhibitShw)
{
    Assert(pfInhibitShw);
    *pfInhibitShw = pCtx->eflags.uBoth & CPUMCTX_INHIBIT_SHADOW;
    return CPUMIsInInterruptShadowWithUpdate(pCtx);
}

/**
 * Checks if we're in an "interrupt shadow" due to a POP SS or MOV SS
 * instruction.
 *
 * This also inhibit NMIs, except perhaps for nested guests.
 *
 * @retval  true if interrupts are inhibited due to POP/MOV SS.
 * @retval  false if not.
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 * @note    Does NOT clear CPUMCTX_INHIBIT_SHADOW when CPUMCTX::uRipInhibitInt
 *          differs from CPUMCTX::rip.
 * @note    Both CPUMIsInInterruptShadowAfterSti() and this function may return
 *          true depending on the execution engine being used.
 */
DECLINLINE(bool) CPUMIsInInterruptShadowAfterSs(PCCPUMCTX pCtx)
{
    if (!(pCtx->eflags.uBoth & CPUMCTX_INHIBIT_SHADOW_SS))
        return false;

    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    return pCtx->uRipInhibitInt == pCtx->rip;
}

/**
 * Checks if we're in an "interrupt shadow" due to an STI instruction.
 *
 * This also inhibit NMIs, except perhaps for nested guests.
 *
 * @retval  true if interrupts are inhibited due to STI.
 * @retval  false if not.
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 * @note    Does NOT clear CPUMCTX_INHIBIT_SHADOW when CPUMCTX::uRipInhibitInt
 *          differs from CPUMCTX::rip.
 * @note    Both CPUMIsInInterruptShadowAfterSs() and this function may return
 *          true depending on the execution engine being used.
 */
DECLINLINE(bool) CPUMIsInInterruptShadowAfterSti(PCCPUMCTX pCtx)
{
    if (!(pCtx->eflags.uBoth & CPUMCTX_INHIBIT_SHADOW_STI))
        return false;

    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    return pCtx->uRipInhibitInt == pCtx->rip;
}

/**
 * Sets the "interrupt shadow" flag, after a STI, POP SS or MOV SS instruction.
 *
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 */
DECLINLINE(void) CPUMSetInInterruptShadow(PCPUMCTX pCtx)
{
    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    pCtx->eflags.uBoth |= CPUMCTX_INHIBIT_SHADOW;
    pCtx->uRipInhibitInt = pCtx->rip;
}

/**
 * Sets the "interrupt shadow" flag, after a STI, POP SS or MOV SS instruction,
 * extended version.
 *
 * @param   pCtx    Current guest CPU context.
 * @param   rip     The RIP for which it is inhibited.
 */
DECLINLINE(void) CPUMSetInInterruptShadowEx(PCPUMCTX pCtx, uint64_t rip)
{
    pCtx->eflags.uBoth  |= CPUMCTX_INHIBIT_SHADOW;
    pCtx->uRipInhibitInt = rip;
}

/**
 * Sets the "interrupt shadow" flag after a POP SS or MOV SS instruction.
 *
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 */
DECLINLINE(void) CPUMSetInInterruptShadowSs(PCPUMCTX pCtx)
{
    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    pCtx->eflags.uBoth  |= CPUMCTX_INHIBIT_SHADOW_SS;
    pCtx->uRipInhibitInt = pCtx->rip;
}

/**
 * Sets the "interrupt shadow" flag after an STI instruction.
 *
 * @param   pCtx    Current guest CPU context.
 * @note    Requires pCtx->rip to be up to date.
 */
DECLINLINE(void) CPUMSetInInterruptShadowSti(PCPUMCTX pCtx)
{
    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    pCtx->eflags.uBoth  |= CPUMCTX_INHIBIT_SHADOW_STI;
    pCtx->uRipInhibitInt = pCtx->rip;
}

/**
 * Clears the "interrupt shadow" flag.
 *
 * @param   pCtx    Current guest CPU context.
 */
DECLINLINE(void) CPUMClearInterruptShadow(PCPUMCTX pCtx)
{
    pCtx->eflags.uBoth &= ~CPUMCTX_INHIBIT_SHADOW;
}

/**
 * Update the "interrupt shadow" flag.
 *
 * @param   pCtx        Current guest CPU context.
 * @param   fInhibited  The new state.
 * @note    Requires pCtx->rip to be up to date.
 */
DECLINLINE(void) CPUMUpdateInterruptShadow(PCPUMCTX pCtx, bool fInhibited)
{
    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RIP);
    if (!fInhibited)
        pCtx->eflags.uBoth  &= ~CPUMCTX_INHIBIT_SHADOW;
    else
    {
        pCtx->eflags.uBoth  |= CPUMCTX_INHIBIT_SHADOW;
        pCtx->uRipInhibitInt = pCtx->rip;
    }
}

/**
 * Update the "interrupt shadow" flag, extended version.
 *
 * @returns fInhibited.
 * @param   pCtx        Current guest CPU context.
 * @param   fInhibited  The new state.
 * @param   rip         The RIP for which it is inhibited.
 */
DECLINLINE(bool) CPUMUpdateInterruptShadowEx(PCPUMCTX pCtx, bool fInhibited, uint64_t rip)
{
    if (!fInhibited)
        pCtx->eflags.uBoth  &= ~CPUMCTX_INHIBIT_SHADOW;
    else
    {
        pCtx->eflags.uBoth  |= CPUMCTX_INHIBIT_SHADOW;
        pCtx->uRipInhibitInt = rip;
    }
    return fInhibited;
}

/**
 * Update the two "interrupt shadow" flags separately, extended version.
 *
 * @param   pCtx            Current guest CPU context.
 * @param   fInhibitedBySs  The new state for the MOV SS & POP SS aspect.
 * @param   fInhibitedBySti The new state for the STI aspect.
 * @param   rip             The RIP for which it is inhibited.
 */
DECLINLINE(void) CPUMUpdateInterruptShadowSsStiEx(PCPUMCTX pCtx, bool fInhibitedBySs, bool fInhibitedBySti, uint64_t rip)
{
    if (!(fInhibitedBySs | fInhibitedBySti))
        pCtx->eflags.uBoth &= ~CPUMCTX_INHIBIT_SHADOW;
    else
    {
        pCtx->eflags.uBoth |= (fInhibitedBySs  ? CPUMCTX_INHIBIT_SHADOW_SS  : UINT32_C(0))
                           |  (fInhibitedBySti ? CPUMCTX_INHIBIT_SHADOW_STI : UINT32_C(0));
        pCtx->uRipInhibitInt = rip;
    }
}

/* VMX forward declarations used by extended function versions: */
DECLINLINE(bool) CPUMIsGuestInVmxNonRootMode(PCCPUMCTX pCtx);
DECLINLINE(bool) CPUMIsGuestVmxPinCtlsSet(PCCPUMCTX pCtx, uint32_t uPinCtls);
DECLINLINE(bool) CPUMIsGuestVmxVirtNmiBlocking(PCCPUMCTX pCtx);
DECLINLINE(void) CPUMSetGuestVmxVirtNmiBlocking(PCPUMCTX pCtx, bool fBlocking);

/**
 * Checks whether interrupts, include NMIs, are inhibited by pending NMI
 * delivery.
 *
 * This only checks the inhibit mask.
 *
 * @retval  true if interrupts are inhibited by NMI handling.
 * @retval  false if interrupts are not inhibited by NMI handling.
 * @param   pCtx        Current guest CPU context.
 */
DECLINLINE(bool) CPUMAreInterruptsInhibitedByNmi(PCCPUMCTX pCtx)
{
    return (pCtx->eflags.uBoth & CPUMCTX_INHIBIT_NMI) != 0;
}

/**
 * Extended version of CPUMAreInterruptsInhibitedByNmi() that takes VMX non-root
 * mode into account when check whether interrupts are inhibited by NMI.
 *
 * @retval  true if interrupts are inhibited by NMI handling.
 * @retval  false if interrupts are not inhibited by NMI handling.
 * @param   pCtx        Current guest CPU context.
 */
DECLINLINE(bool) CPUMAreInterruptsInhibitedByNmiEx(PCCPUMCTX pCtx)
{
    /* See CPUMUpdateInterruptInhibitingByNmiEx for comments. */
    if (   !CPUMIsGuestInVmxNonRootMode(pCtx)
        || !CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_VIRT_NMI))
        return CPUMAreInterruptsInhibitedByNmi(pCtx);
    return CPUMIsGuestVmxVirtNmiBlocking(pCtx);
}

/**
 * Marks interrupts, include NMIs, as inhibited by pending NMI delivery.
 *
 * @param   pCtx        Current guest CPU context.
 */
DECLINLINE(void) CPUMSetInterruptInhibitingByNmi(PCPUMCTX pCtx)
{
    pCtx->eflags.uBoth |= CPUMCTX_INHIBIT_NMI;
}

/**
 * Extended version of CPUMSetInterruptInhibitingByNmi() that takes VMX non-root
 * mode into account when marking interrupts as inhibited by NMI.
 *
 * @param   pCtx        Current guest CPU context.
 */
DECLINLINE(void) CPUMSetInterruptInhibitingByNmiEx(PCPUMCTX pCtx)
{
    /* See CPUMUpdateInterruptInhibitingByNmiEx for comments. */
    if (   !CPUMIsGuestInVmxNonRootMode(pCtx)
        || !CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_VIRT_NMI))
        CPUMSetInterruptInhibitingByNmi(pCtx);
    else
        CPUMSetGuestVmxVirtNmiBlocking(pCtx, true);
}

/**
 * Marks interrupts, include NMIs, as no longer inhibited by pending NMI
 * delivery.
 *
 * @param   pCtx        Current guest CPU context.
 */
DECLINLINE(void) CPUMClearInterruptInhibitingByNmi(PCPUMCTX pCtx)
{
    pCtx->eflags.uBoth &= ~CPUMCTX_INHIBIT_NMI;
}

/**
 * Extended version of CPUMClearInterruptInhibitingByNmi() that takes VMX
 * non-root mode into account when doing the updating.
 *
 * @param   pCtx        Current guest CPU context.
 */
DECLINLINE(void) CPUMClearInterruptInhibitingByNmiEx(PCPUMCTX pCtx)
{
    /* See CPUMUpdateInterruptInhibitingByNmiEx for comments. */
    if (   !CPUMIsGuestInVmxNonRootMode(pCtx)
        || !CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_VIRT_NMI))
        CPUMClearInterruptInhibitingByNmi(pCtx);
    else
        CPUMSetGuestVmxVirtNmiBlocking(pCtx, false);
}

/**
 * Update whether interrupts, include NMIs, are inhibited by pending NMI
 * delivery.
 *
 * @param   pCtx        Current guest CPU context.
 * @param   fInhibited  The new state.
 */
DECLINLINE(void) CPUMUpdateInterruptInhibitingByNmi(PCPUMCTX pCtx, bool fInhibited)
{
    if (!fInhibited)
        pCtx->eflags.uBoth &= ~CPUMCTX_INHIBIT_NMI;
    else
        pCtx->eflags.uBoth |= CPUMCTX_INHIBIT_NMI;
}

/**
 * Extended version of CPUMUpdateInterruptInhibitingByNmi() that takes VMX
 * non-root mode into account when doing the updating.
 *
 * @param   pCtx        Current guest CPU context.
 * @param   fInhibited  The new state.
 */
DECLINLINE(void) CPUMUpdateInterruptInhibitingByNmiEx(PCPUMCTX pCtx, bool fInhibited)
{
    /*
     * Set the state of guest-NMI blocking in any of the following cases:
     *   - We're not executing a nested-guest.
     *   - We're executing an SVM nested-guest[1].
     *   - We're executing a VMX nested-guest without virtual-NMIs enabled.
     *
     * [1] -- SVM does not support virtual-NMIs or virtual-NMI blocking.
     *        SVM hypervisors must track NMI blocking themselves by intercepting
     *        the IRET instruction after injection of an NMI.
     */
    if (   !CPUMIsGuestInVmxNonRootMode(pCtx)
        || !CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_VIRT_NMI))
        CPUMUpdateInterruptInhibitingByNmi(pCtx, fInhibited);
    /*
     * Set the state of virtual-NMI blocking, if we are executing a
     * VMX nested-guest with virtual-NMIs enabled.
     */
    else
        CPUMSetGuestVmxVirtNmiBlocking(pCtx, fInhibited);
}


/**
 * Checks if we are executing inside an SVM nested hardware-virtualized guest.
 *
 * @returns @c true if in SVM nested-guest mode, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInSvmNestedHwVirtMode(PCCPUMCTX pCtx)
{
    /*
     * With AMD-V, the VMRUN intercept is a pre-requisite to entering SVM guest-mode.
     * See AMD spec. 15.5 "VMRUN instruction" subsection "Canonicalization and Consistency Checks".
     */
#ifndef IN_RC
    if (   pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM
        || !(pCtx->hwvirt.svm.Vmcb.ctrl.u64InterceptCtrl & SVM_CTRL_INTERCEPT_VMRUN))
        return false;
    return true;
#else
    NOREF(pCtx);
    return false;
#endif
}

/**
 * Checks if the guest is in VMX non-root operation.
 *
 * @returns @c true if in VMX non-root operation, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInVmxNonRootMode(PCCPUMCTX pCtx)
{
#ifndef IN_RC
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_VMX)
        return false;
    Assert(!pCtx->hwvirt.vmx.fInVmxNonRootMode || pCtx->hwvirt.vmx.fInVmxRootMode);
    return pCtx->hwvirt.vmx.fInVmxNonRootMode;
#else
    NOREF(pCtx);
    return false;
#endif
}

/**
 * Checks if we are executing inside an SVM or VMX nested hardware-virtualized
 * guest.
 *
 * @returns @c true if in nested-guest mode, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInNestedHwvirtMode(PCCPUMCTX pCtx)
{
#if 0
    return CPUMIsGuestInVmxNonRootMode(pCtx) || CPUMIsGuestInSvmNestedHwVirtMode(pCtx);
#else
    if (pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_NONE)
        return false;
    if (pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_VMX)
    {
        Assert(!pCtx->hwvirt.vmx.fInVmxNonRootMode || pCtx->hwvirt.vmx.fInVmxRootMode);
        return pCtx->hwvirt.vmx.fInVmxNonRootMode;
    }
    Assert(pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_SVM);
    return RT_BOOL(pCtx->hwvirt.svm.Vmcb.ctrl.u64InterceptCtrl & SVM_CTRL_INTERCEPT_VMRUN);
#endif
}

/**
 * Checks if we are executing inside an SVM or VMX nested hardware-virtualized
 * guest.
 *
 * @retval  CPUMHWVIRT_NONE if not in SVM or VMX non-root mode.
 * @retval  CPUMHWVIRT_VMX if in VMX non-root mode.
 * @retval  CPUMHWVIRT_SVM if in SVM non-root mode.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(CPUMHWVIRT) CPUMGetGuestInNestedHwvirtMode(PCCPUMCTX pCtx)
{
    if (pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_NONE)
        return CPUMHWVIRT_NONE;
    if (pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_VMX)
    {
        Assert(!pCtx->hwvirt.vmx.fInVmxNonRootMode || pCtx->hwvirt.vmx.fInVmxRootMode);
        return pCtx->hwvirt.vmx.fInVmxNonRootMode ? CPUMHWVIRT_VMX : CPUMHWVIRT_NONE;
    }
    Assert(pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_SVM);
    return pCtx->hwvirt.svm.Vmcb.ctrl.u64InterceptCtrl & SVM_CTRL_INTERCEPT_VMRUN ? CPUMHWVIRT_SVM : CPUMHWVIRT_NONE;
}

/**
 * Checks if the guest is in VMX root operation.
 *
 * @returns @c true if in VMX root operation, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestInVmxRootMode(PCCPUMCTX pCtx)
{
#ifndef IN_RC
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_VMX)
        return false;
    return pCtx->hwvirt.vmx.fInVmxRootMode;
#else
    NOREF(pCtx);
    return false;
#endif
}

# ifndef IN_RC

/**
 * Checks if the nested-guest VMCB has the specified ctrl/instruction intercept
 * active.
 *
 * @returns @c true if in intercept is set, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx        Current CPU context.
 * @param   fIntercept  The SVM control/instruction intercept, see
 *                      SVM_CTRL_INTERCEPT_*.
 */
DECLINLINE(bool) CPUMIsGuestSvmCtrlInterceptSet(PCVMCPU pVCpu, PCCPUMCTX pCtx, uint64_t fIntercept)
{
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint64_t u64Intercepts;
    if (!HMGetGuestSvmCtrlIntercepts(pVCpu, &u64Intercepts))
        u64Intercepts = pCtx->hwvirt.svm.Vmcb.ctrl.u64InterceptCtrl;
    return RT_BOOL(u64Intercepts & fIntercept);
}

/**
 * Checks if the nested-guest VMCB has the specified CR read intercept active.
 *
 * @returns @c true if in intercept is set, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 * @param   uCr     The CR register number (0 to 15).
 */
DECLINLINE(bool) CPUMIsGuestSvmReadCRxInterceptSet(PCVMCPU pVCpu, PCCPUMCTX pCtx, uint8_t uCr)
{
    Assert(uCr < 16);
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint16_t u16Intercepts;
    if (!HMGetGuestSvmReadCRxIntercepts(pVCpu, &u16Intercepts))
        u16Intercepts = pCtx->hwvirt.svm.Vmcb.ctrl.u16InterceptRdCRx;
    return RT_BOOL(u16Intercepts & (UINT16_C(1) << uCr));
}

/**
 * Checks if the nested-guest VMCB has the specified CR write intercept active.
 *
 * @returns @c true if in intercept is set, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 * @param   uCr     The CR register number (0 to 15).
 */
DECLINLINE(bool) CPUMIsGuestSvmWriteCRxInterceptSet(PCVMCPU pVCpu, PCCPUMCTX pCtx, uint8_t uCr)
{
    Assert(uCr < 16);
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint16_t u16Intercepts;
    if (!HMGetGuestSvmWriteCRxIntercepts(pVCpu, &u16Intercepts))
        u16Intercepts = pCtx->hwvirt.svm.Vmcb.ctrl.u16InterceptWrCRx;
    return RT_BOOL(u16Intercepts & (UINT16_C(1) << uCr));
}

/**
 * Checks if the nested-guest VMCB has the specified DR read intercept active.
 *
 * @returns @c true if in intercept is set, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 * @param   uDr     The DR register number (0 to 15).
 */
DECLINLINE(bool) CPUMIsGuestSvmReadDRxInterceptSet(PCVMCPU pVCpu, PCCPUMCTX pCtx, uint8_t uDr)
{
    Assert(uDr < 16);
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint16_t u16Intercepts;
    if (!HMGetGuestSvmReadDRxIntercepts(pVCpu, &u16Intercepts))
        u16Intercepts = pCtx->hwvirt.svm.Vmcb.ctrl.u16InterceptRdDRx;
    return RT_BOOL(u16Intercepts & (UINT16_C(1) << uDr));
}

/**
 * Checks if the nested-guest VMCB has the specified DR write intercept active.
 *
 * @returns @c true if in intercept is set, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 * @param   uDr     The DR register number (0 to 15).
 */
DECLINLINE(bool) CPUMIsGuestSvmWriteDRxInterceptSet(PCVMCPU pVCpu, PCCPUMCTX pCtx, uint8_t uDr)
{
    Assert(uDr < 16);
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint16_t u16Intercepts;
    if (!HMGetGuestSvmWriteDRxIntercepts(pVCpu, &u16Intercepts))
        u16Intercepts = pCtx->hwvirt.svm.Vmcb.ctrl.u16InterceptWrDRx;
    return RT_BOOL(u16Intercepts & (UINT16_C(1) << uDr));
}

/**
 * Checks if the nested-guest VMCB has the specified exception intercept active.
 *
 * @returns @c true if in intercept is active, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx        Current CPU context.
 * @param   uVector     The exception / interrupt vector.
 */
DECLINLINE(bool) CPUMIsGuestSvmXcptInterceptSet(PCVMCPU pVCpu, PCCPUMCTX pCtx, uint8_t uVector)
{
    Assert(uVector <= X86_XCPT_LAST);
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint32_t u32Intercepts;
    if (!HMGetGuestSvmXcptIntercepts(pVCpu, &u32Intercepts))
        u32Intercepts = pCtx->hwvirt.svm.Vmcb.ctrl.u32InterceptXcpt;
    return RT_BOOL(u32Intercepts & RT_BIT(uVector));
}

/**
 * Checks if the nested-guest VMCB has virtual-interrupt masking enabled.
 *
 * @returns @c true if virtual-interrupts are masked, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 *
 * @remarks Should only be called when SVM feature is exposed to the guest.
 */
DECLINLINE(bool) CPUMIsGuestSvmVirtIntrMasking(PCVMCPU pVCpu, PCCPUMCTX pCtx)
{
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    bool fVIntrMasking;
    if (!HMGetGuestSvmVirtIntrMasking(pVCpu, &fVIntrMasking))
        fVIntrMasking = pCtx->hwvirt.svm.Vmcb.ctrl.IntCtrl.n.u1VIntrMasking;
    return fVIntrMasking;
}

/**
 * Checks if the nested-guest VMCB has nested-paging enabled.
 *
 * @returns @c true if nested-paging is enabled, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 *
 * @remarks Should only be called when SVM feature is exposed to the guest.
 */
DECLINLINE(bool) CPUMIsGuestSvmNestedPagingEnabled(PCVMCPU pVCpu, PCCPUMCTX pCtx)
{
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    bool fNestedPaging;
    if (!HMGetGuestSvmNestedPaging(pVCpu, &fNestedPaging))
        fNestedPaging = pCtx->hwvirt.svm.Vmcb.ctrl.NestedPagingCtrl.n.u1NestedPaging;
    return fNestedPaging;
}

/**
 * Gets the nested-guest VMCB pause-filter count.
 *
 * @returns The pause-filter count.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    Current CPU context.
 *
 * @remarks Should only be called when SVM feature is exposed to the guest.
 */
DECLINLINE(uint16_t) CPUMGetGuestSvmPauseFilterCount(PCVMCPU pVCpu, PCCPUMCTX pCtx)
{
    if (pCtx->hwvirt.enmHwvirt != CPUMHWVIRT_SVM)
        return false;
    uint16_t u16PauseFilterCount;
    if (!HMGetGuestSvmPauseFilterCount(pVCpu, &u16PauseFilterCount))
        u16PauseFilterCount = pCtx->hwvirt.svm.Vmcb.ctrl.u16PauseFilterCount;
    return u16PauseFilterCount;
}

/**
 * Updates the NextRIP (NRIP) field in the nested-guest VMCB.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx        Current CPU context.
 * @param   cbInstr     The length of the current instruction in bytes.
 *
 * @remarks Should only be called when SVM feature is exposed to the guest.
 */
DECLINLINE(void) CPUMGuestSvmUpdateNRip(PVMCPU pVCpu, PCPUMCTX pCtx, uint8_t cbInstr)
{
    RT_NOREF(pVCpu);
    Assert(pCtx->hwvirt.enmHwvirt == CPUMHWVIRT_SVM);
    pCtx->hwvirt.svm.Vmcb.ctrl.u64NextRIP = pCtx->rip + cbInstr;
}

/**
 * Checks whether one of the given Pin-based VM-execution controls are set when
 * executing a nested-guest.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uPinCtls    The Pin-based VM-execution controls to check.
 *
 * @remarks This does not check if all given controls are set if more than one
 *          control is passed in @a uPinCtl.
 */
DECLINLINE(bool) CPUMIsGuestVmxPinCtlsSet(PCCPUMCTX pCtx, uint32_t uPinCtls)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u32PinCtls & uPinCtls);
}

/**
 * Checks whether one of the given Processor-based VM-execution controls are set
 * when executing a nested-guest.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uProcCtls   The Processor-based VM-execution controls to check.
 *
 * @remarks This does not check if all given controls are set if more than one
 *          control is passed in @a uProcCtls.
 */
DECLINLINE(bool) CPUMIsGuestVmxProcCtlsSet(PCCPUMCTX pCtx, uint32_t uProcCtls)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u32ProcCtls & uProcCtls);
}

/**
 * Checks whether one of the given Secondary Processor-based VM-execution controls
 * are set when executing a nested-guest.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uProcCtls2  The Secondary Processor-based VM-execution controls to
 *                      check.
 *
 * @remarks This does not check if all given controls are set if more than one
 *          control is passed in @a uProcCtls2.
 */
DECLINLINE(bool) CPUMIsGuestVmxProcCtls2Set(PCCPUMCTX pCtx, uint32_t uProcCtls2)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u32ProcCtls2 & uProcCtls2);
}

/**
 * Checks whether one of the given Tertiary Processor-based VM-execution controls
 * are set when executing a nested-guest.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uProcCtls3  The Tertiary Processor-based VM-execution controls to
 *                      check.
 *
 * @remarks This does not check if all given controls are set if more than one
 *          control is passed in @a uProcCtls3.
 */
DECLINLINE(bool) CPUMIsGuestVmxProcCtls3Set(PCCPUMCTX pCtx, uint64_t uProcCtls3)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u64ProcCtls3.u & uProcCtls3);
}

/**
 * Checks whether one of the given VM-exit controls are set when executing a
 * nested-guest.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uExitCtls   The VM-exit controls to check.
 *
 * @remarks This does not check if all given controls are set if more than one
 *          control is passed in @a uExitCtls.
 */
DECLINLINE(bool) CPUMIsGuestVmxExitCtlsSet(PCCPUMCTX pCtx, uint32_t uExitCtls)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u32ExitCtls & uExitCtls);
}

/**
 * Checks whether one of the given VM-entry controls are set when executing a
 * nested-guest.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uEntryCtls  The VM-entry controls to check.
 *
 * @remarks This does not check if all given controls are set if more than one
 *          control is passed in @a uEntryCtls.
 */
DECLINLINE(bool) CPUMIsGuestVmxEntryCtlsSet(PCCPUMCTX pCtx, uint32_t uEntryCtls)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u32EntryCtls & uEntryCtls);
}

/**
 * Checks whether events injected in the nested-guest are subject to VM-exit checks.
 *
 * @returns @c true if set, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxInterceptEvents(PCCPUMCTX pCtx)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return pCtx->hwvirt.vmx.fInterceptEvents;
}

/**
 * Sets whether events injected in the nested-guest are subject to VM-exit checks.
 *
 * @param   pCtx        Current CPU context.
 * @param   fIntercept  Whether to subject injected events to VM-exits or not.
 */
DECLINLINE(void) CPUMSetGuestVmxInterceptEvents(PCPUMCTX pCtx, bool fInterceptEvents)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    pCtx->hwvirt.vmx.fInterceptEvents = fInterceptEvents;
}

/**
 * Checks whether the given exception causes a VM-exit.
 *
 * The exception type include hardware exceptions, software exceptions (#BP, #OF)
 * and privileged software exceptions (#DB generated by INT1/ICEBP).
 *
 * Software interrupts do -not- cause VM-exits and hence must not be used with this
 * function.
 *
 * @returns @c true if the exception causes a VM-exit, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uVector     The exception vector.
 * @param   uErrCode    The error code associated with the exception. Pass 0 if not
 *                      applicable.
 */
DECLINLINE(bool) CPUMIsGuestVmxXcptInterceptSet(PCCPUMCTX pCtx, uint8_t uVector, uint32_t uErrCode)
{
    Assert(uVector <= X86_XCPT_LAST);

    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));

    /* NMIs have a dedicated VM-execution control for causing VM-exits. */
    if (uVector == X86_XCPT_NMI)
        return RT_BOOL(pCtx->hwvirt.vmx.Vmcs.u32PinCtls & VMX_PIN_CTLS_NMI_EXIT);

    /* Page-faults are subject to masking using its error code. */
    uint32_t fXcptBitmap = pCtx->hwvirt.vmx.Vmcs.u32XcptBitmap;
    if (uVector == X86_XCPT_PF)
    {
        uint32_t const fXcptPFMask  = pCtx->hwvirt.vmx.Vmcs.u32XcptPFMask;
        uint32_t const fXcptPFMatch = pCtx->hwvirt.vmx.Vmcs.u32XcptPFMatch;
        if ((uErrCode & fXcptPFMask) != fXcptPFMatch)
            fXcptBitmap ^= RT_BIT(X86_XCPT_PF);
    }

    /* Consult the exception bitmap for all other exceptions. */
    if (fXcptBitmap & RT_BIT(uVector))
        return true;
    return false;
}


/**
 * Checks whether the guest is in VMX non-root mode and using EPT paging.
 *
 * @returns @c true if in VMX non-root operation with EPT, @c false otherwise.
 * @param   pCtx   Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxEptPagingEnabledEx(PCCPUMCTX pCtx)
{
    return    CPUMIsGuestInVmxNonRootMode(pCtx)
           && CPUMIsGuestVmxProcCtls2Set(pCtx, VMX_PROC_CTLS2_EPT);
}


/**
 * Implements VMSucceed for VMX instruction success.
 *
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(void) CPUMSetGuestVmxVmSucceed(PCPUMCTX pCtx)
{
    pCtx->eflags.uBoth &= ~(X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF);
}

/**
 * Implements VMFailInvalid for VMX instruction failure.
 *
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(void) CPUMSetGuestVmxVmFailInvalid(PCPUMCTX pCtx)
{
    pCtx->eflags.uBoth &= ~(X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF);
    pCtx->eflags.uBoth |= X86_EFL_CF;
}

/**
 * Implements VMFailValid for VMX instruction failure.
 *
 * @param   pCtx        Current CPU context.
 * @param   enmInsErr   The VM instruction error.
 */
DECLINLINE(void) CPUMSetGuestVmxVmFailValid(PCPUMCTX pCtx, VMXINSTRERR enmInsErr)
{
    pCtx->eflags.uBoth &= ~(X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF);
    pCtx->eflags.uBoth |= X86_EFL_ZF;
    pCtx->hwvirt.vmx.Vmcs.u32RoVmInstrError = enmInsErr;
}

/**
 * Implements VMFail for VMX instruction failure.
 *
 * @param   pCtx        Current CPU context.
 * @param   enmInsErr   The VM instruction error.
 */
DECLINLINE(void) CPUMSetGuestVmxVmFail(PCPUMCTX pCtx, VMXINSTRERR enmInsErr)
{
    if (pCtx->hwvirt.vmx.GCPhysVmcs != NIL_RTGCPHYS)
        CPUMSetGuestVmxVmFailValid(pCtx, enmInsErr);
    else
        CPUMSetGuestVmxVmFailInvalid(pCtx);
}

/**
 * Returns the guest-physical address of the APIC-access page when executing a
 * nested-guest.
 *
 * @returns The APIC-access page guest-physical address.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(uint64_t) CPUMGetGuestVmxApicAccessPageAddrEx(PCCPUMCTX pCtx)
{
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return pCtx->hwvirt.vmx.Vmcs.u64AddrApicAccess.u;
}

/**
 * Gets the nested-guest CR0 subject to the guest/host mask and the read-shadow.
 *
 * @returns The nested-guest CR0.
 * @param   pCtx            Current CPU context.
 * @param   fGstHostMask    The CR0 guest/host mask to use.
 */
DECLINLINE(uint64_t) CPUMGetGuestVmxMaskedCr0(PCCPUMCTX pCtx, uint64_t fGstHostMask)
{
    /*
     * For each CR0 bit owned by the host, the corresponding bit from the
     * CR0 read shadow is loaded. For each CR0 bit that is not owned by the host,
     * the corresponding bit from the guest CR0 is loaded.
     *
     * See Intel Spec. 25.3 "Changes To Instruction Behavior In VMX Non-root Operation".
     */
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    uint64_t const uGstCr0      = pCtx->cr0;
    uint64_t const fReadShadow  = pCtx->hwvirt.vmx.Vmcs.u64Cr0ReadShadow.u;
    return (fReadShadow & fGstHostMask) | (uGstCr0 & ~fGstHostMask);
}

/**
 * Gets the nested-guest CR4 subject to the guest/host mask and the read-shadow.
 *
 * @returns The nested-guest CR4.
 * @param   pCtx            Current CPU context.
 * @param   fGstHostMask    The CR4 guest/host mask to use.
 */
DECLINLINE(uint64_t) CPUMGetGuestVmxMaskedCr4(PCCPUMCTX pCtx, uint64_t fGstHostMask)
{
    /*
     * For each CR4 bit owned by the host, the corresponding bit from the
     * CR4 read shadow is loaded. For each CR4 bit that is not owned by the host,
     * the corresponding bit from the guest CR4 is loaded.
     *
     * See Intel Spec. 25.3 "Changes To Instruction Behavior In VMX Non-root Operation".
     */
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    uint64_t const uGstCr4      = pCtx->cr4;
    uint64_t const fReadShadow  = pCtx->hwvirt.vmx.Vmcs.u64Cr4ReadShadow.u;
    return (fReadShadow & fGstHostMask) | (uGstCr4 & ~fGstHostMask);
}

/**
 * Checks whether the LMSW access causes a VM-exit or not.
 *
 * @returns @c true if the LMSW access causes a VM-exit, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   uNewMsw     The LMSW source operand (the Machine Status Word).
 */
DECLINLINE(bool) CPUMIsGuestVmxLmswInterceptSet(PCCPUMCTX pCtx, uint16_t uNewMsw)
{
    /*
     * LMSW VM-exits are subject to the CR0 guest/host mask and the CR0 read shadow.
     *
     * See Intel spec. 24.6.6 "Guest/Host Masks and Read Shadows for CR0 and CR4".
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));

    uint32_t const fGstHostMask = (uint32_t)pCtx->hwvirt.vmx.Vmcs.u64Cr0Mask.u;
    uint32_t const fReadShadow  = (uint32_t)pCtx->hwvirt.vmx.Vmcs.u64Cr0ReadShadow.u;

    /*
     * LMSW can never clear CR0.PE but it may set it. Hence, we handle the
     * CR0.PE case first, before the rest of the bits in the MSW.
     *
     * If CR0.PE is owned by the host and CR0.PE differs between the
     * MSW (source operand) and the read-shadow, we must cause a VM-exit.
     */
    if (    (fGstHostMask & X86_CR0_PE)
        &&  (uNewMsw      & X86_CR0_PE)
        && !(fReadShadow  & X86_CR0_PE))
        return true;

    /*
     * If CR0.MP, CR0.EM or CR0.TS is owned by the host, and the corresponding
     * bits differ between the MSW (source operand) and the read-shadow, we must
     * cause a VM-exit.
     */
    uint32_t const fGstHostLmswMask = fGstHostMask & (X86_CR0_MP | X86_CR0_EM | X86_CR0_TS);
    if ((fReadShadow & fGstHostLmswMask) != (uNewMsw & fGstHostLmswMask))
        return true;

    return false;
}

/**
 * Checks whether the Mov-to-CR0/CR4 access causes a VM-exit or not.
 *
 * @returns @c true if the Mov CRX access causes a VM-exit, @c false otherwise.
 * @param   pCtx        Current CPU context.
 * @param   iCrReg      The control register number (must be 0 or 4).
 * @param   uNewCrX     The CR0/CR4 value being written.
 */
DECLINLINE(bool) CPUMIsGuestVmxMovToCr0Cr4InterceptSet(PCCPUMCTX pCtx, uint8_t iCrReg, uint64_t uNewCrX)
{
    /*
     * For any CR0/CR4 bit owned by the host (in the CR0/CR4 guest/host mask), if the
     * corresponding bits differ between the source operand and the read-shadow,
     * we must cause a VM-exit.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    Assert(iCrReg == 0 || iCrReg == 4);

    uint64_t fGstHostMask;
    uint64_t fReadShadow;
    if (iCrReg == 0)
    {
        fGstHostMask = pCtx->hwvirt.vmx.Vmcs.u64Cr0Mask.u;
        fReadShadow  = pCtx->hwvirt.vmx.Vmcs.u64Cr0ReadShadow.u;
    }
    else
    {
        fGstHostMask = pCtx->hwvirt.vmx.Vmcs.u64Cr4Mask.u;
        fReadShadow  = pCtx->hwvirt.vmx.Vmcs.u64Cr4ReadShadow.u;
    }

    if ((fReadShadow & fGstHostMask) != (uNewCrX & fGstHostMask))
    {
        Assert(fGstHostMask != 0);
        return true;
    }

    return false;
}

/**
 * Returns whether the guest has an active, current VMCS.
 *
 * @returns @c true if the guest has an active, current VMCS, @c false otherwise.
 * @param   pCtx    Current CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxCurrentVmcsValid(PCCPUMCTX pCtx)
{
    return pCtx->hwvirt.vmx.GCPhysVmcs != NIL_RTGCPHYS;
}

# endif /* !IN_RC */

/**
 * Checks whether the VMX nested-guest is in a state to receive physical (APIC)
 * interrupts.
 *
 * @returns @c true if it's ready, @c false otherwise.
 * @param   pCtx    The guest-CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxPhysIntrEnabled(PCCPUMCTX pCtx)
{
#ifdef IN_RC
    AssertReleaseFailedReturn(false);
#else
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    if (CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_EXT_INT_EXIT))
        return true;
    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RFLAGS);
    return RT_BOOL(pCtx->eflags.u & X86_EFL_IF);
#endif
}

/**
 * Checks whether the VMX nested-guest is blocking virtual-NMIs.
 *
 * @returns @c true if it's blocked, @c false otherwise.
 * @param   pCtx    The guest-CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxVirtNmiBlocking(PCCPUMCTX pCtx)
{
#ifdef IN_RC
    RT_NOREF(pCtx);
    AssertReleaseFailedReturn(false);
#else
    /*
     * Return the state of virtual-NMI blocking, if we are executing a
     * VMX nested-guest with virtual-NMIs enabled.
     */
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    Assert(CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_VIRT_NMI));
    return pCtx->hwvirt.vmx.fVirtNmiBlocking;
#endif
}

/**
 * Sets or clears VMX nested-guest virtual-NMI blocking.
 *
 * @param   pCtx        The guest-CPU context.
 * @param   fBlocking   Whether virtual-NMI blocking is in effect or not.
 */
DECLINLINE(void) CPUMSetGuestVmxVirtNmiBlocking(PCPUMCTX pCtx, bool fBlocking)
{
#ifdef IN_RC
    RT_NOREF2(pCtx, fBlocking);
    AssertReleaseFailedReturnVoid();
#else
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    Assert(CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_VIRT_NMI));
    pCtx->hwvirt.vmx.fVirtNmiBlocking = fBlocking;
#endif
}

/**
 * Checks whether the VMX nested-guest is in a state to receive virtual interrupts
 * (those injected with the "virtual-interrupt delivery" feature).
 *
 * @returns @c true if it's ready, @c false otherwise.
 * @param   pCtx    The guest-CPU context.
 */
DECLINLINE(bool) CPUMIsGuestVmxVirtIntrEnabled(PCCPUMCTX pCtx)
{
#ifdef IN_RC
    RT_NOREF2(pCtx);
    AssertReleaseFailedReturn(false);
#else
    Assert(CPUMIsGuestInVmxNonRootMode(pCtx));
    return RT_BOOL(pCtx->eflags.u & X86_EFL_IF);
#endif
}

/** @} */
#endif /* !IPRT_WITHOUT_NAMED_UNIONS_AND_STRUCTS || DOXYGEN_RUNNING */



/** @name Hypervisor Register Getters.
 * @{ */
VMMDECL(RTGCUINTREG)    CPUMGetHyperDR0(PVMCPU pVCpu);
VMMDECL(RTGCUINTREG)    CPUMGetHyperDR1(PVMCPU pVCpu);
VMMDECL(RTGCUINTREG)    CPUMGetHyperDR2(PVMCPU pVCpu);
VMMDECL(RTGCUINTREG)    CPUMGetHyperDR3(PVMCPU pVCpu);
VMMDECL(RTGCUINTREG)    CPUMGetHyperDR6(PVMCPU pVCpu);
VMMDECL(RTGCUINTREG)    CPUMGetHyperDR7(PVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetHyperCR3(PVMCPU pVCpu);
/** @} */

/** @name Hypervisor Register Setters.
 * @{ */
VMMDECL(void)           CPUMSetHyperCR3(PVMCPU pVCpu, uint32_t cr3);
VMMDECL(void)           CPUMSetHyperDR6(PVMCPU pVCpu, RTGCUINTREG uDr6);
VMMDECL(void)           CPUMSetHyperDR7(PVMCPU pVCpu, RTGCUINTREG uDr7);
VMMDECL(int)            CPUMRecalcHyperDRx(PVMCPUCC pVCpu, uint8_t iGstReg);
/** @} */

#ifdef VBOX_INCLUDED_vmm_cpumctx_h
VMM_INT_DECL(PCPUMCTXMSRS) CPUMQueryGuestCtxMsrsPtr(PVMCPU pVCpu);
#endif

/** @name Changed flags.
 * These flags are used to keep track of which important register that
 * have been changed since last they were reset. The only one allowed
 * to clear them is REM!
 *
 * @todo This is obsolete, but remains as it will be refactored for coordinating
 *       IEM and NEM/HM later. Probably.
 * @{
 */
#define CPUM_CHANGED_FPU_REM                    RT_BIT(0)
#define CPUM_CHANGED_CR0                        RT_BIT(1)
#define CPUM_CHANGED_CR4                        RT_BIT(2)
#define CPUM_CHANGED_GLOBAL_TLB_FLUSH           RT_BIT(3)
#define CPUM_CHANGED_CR3                        RT_BIT(4)
#define CPUM_CHANGED_GDTR                       RT_BIT(5)
#define CPUM_CHANGED_IDTR                       RT_BIT(6)
#define CPUM_CHANGED_LDTR                       RT_BIT(7)
#define CPUM_CHANGED_TR                         RT_BIT(8)  /**@< Currently unused. */
#define CPUM_CHANGED_SYSENTER_MSR               RT_BIT(9)
#define CPUM_CHANGED_HIDDEN_SEL_REGS            RT_BIT(10) /**@< Currently unused. */
#define CPUM_CHANGED_CPUID                      RT_BIT(11)
#define CPUM_CHANGED_ALL                        (  CPUM_CHANGED_FPU_REM \
                                                 | CPUM_CHANGED_CR0 \
                                                 | CPUM_CHANGED_CR4 \
                                                 | CPUM_CHANGED_GLOBAL_TLB_FLUSH \
                                                 | CPUM_CHANGED_CR3 \
                                                 | CPUM_CHANGED_GDTR \
                                                 | CPUM_CHANGED_IDTR \
                                                 | CPUM_CHANGED_LDTR \
                                                 | CPUM_CHANGED_TR \
                                                 | CPUM_CHANGED_SYSENTER_MSR \
                                                 | CPUM_CHANGED_HIDDEN_SEL_REGS \
                                                 | CPUM_CHANGED_CPUID )
/** @} */

VMMDECL(bool)           CPUMSupportsXSave(PVM pVM);
VMMDECL(bool)           CPUMIsHostUsingSysEnter(PVM pVM);
VMMDECL(bool)           CPUMIsHostUsingSysCall(PVM pVM);
VMMDECL(bool)           CPUMIsGuestFPUStateActive(PVMCPU pVCpu);
VMMDECL(bool)           CPUMIsGuestFPUStateLoaded(PVMCPU pVCpu);
VMMDECL(bool)           CPUMIsHostFPUStateSaved(PVMCPU pVCpu);
VMMDECL(bool)           CPUMIsGuestDebugStateActive(PVMCPU pVCpu);
VMMDECL(void)           CPUMDeactivateGuestDebugState(PVMCPU pVCpu);
VMMDECL(bool)           CPUMIsHyperDebugStateActive(PVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestCPL(PVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestMxCsrMask(PVM pVM);
VMMDECL(uint64_t)       CPUMGetGuestScalableBusFrequency(PVM pVM);
VMMDECL(uint64_t)       CPUMGetGuestEferMsrValidMask(PVM pVM);
VMMDECL(int)            CPUMIsGuestEferMsrWriteValid(PVM pVM, uint64_t uCr0, uint64_t uOldEfer, uint64_t uNewEfer,
                                                     uint64_t *puValidEfer);
VMMDECL(void)           CPUMSetGuestEferMsrNoChecks(PVMCPUCC pVCpu, uint64_t uOldEfer, uint64_t uValidEfer);
VMMDECL(bool)           CPUMIsPatMsrValid(uint64_t uValue);


/** Guest CPU interruptibility level, see CPUMGetGuestInterruptibility(). */
typedef enum CPUMINTERRUPTIBILITY
{
    CPUMINTERRUPTIBILITY_INVALID = 0,
    CPUMINTERRUPTIBILITY_UNRESTRAINED,
    CPUMINTERRUPTIBILITY_VIRT_INT_DISABLED,
    CPUMINTERRUPTIBILITY_INT_DISABLED,
    CPUMINTERRUPTIBILITY_INT_INHIBITED, /**< @todo rename as it inhibits NMIs too. */
    CPUMINTERRUPTIBILITY_NMI_INHIBIT,
    CPUMINTERRUPTIBILITY_GLOBAL_INHIBIT,
    CPUMINTERRUPTIBILITY_END,
    CPUMINTERRUPTIBILITY_32BIT_HACK = 0x7fffffff
} CPUMINTERRUPTIBILITY;

VMM_INT_DECL(CPUMINTERRUPTIBILITY) CPUMGetGuestInterruptibility(PVMCPU pVCpu);

/** @name Typical scalable bus frequency values.
 * @{ */
/** Special internal value indicating that we don't know the frequency.
 *  @internal  */
#define CPUM_SBUSFREQ_UNKNOWN       UINT64_C(1)
#define CPUM_SBUSFREQ_100MHZ        UINT64_C(100000000)
#define CPUM_SBUSFREQ_133MHZ        UINT64_C(133333333)
#define CPUM_SBUSFREQ_167MHZ        UINT64_C(166666666)
#define CPUM_SBUSFREQ_200MHZ        UINT64_C(200000000)
#define CPUM_SBUSFREQ_267MHZ        UINT64_C(266666666)
#define CPUM_SBUSFREQ_333MHZ        UINT64_C(333333333)
#define CPUM_SBUSFREQ_400MHZ        UINT64_C(400000000)
/** @} */


#ifdef IN_RING3
/** @defgroup grp_cpum_r3    The CPUM ring-3 API
 * @{
 */

VMMR3DECL(int)          CPUMR3SetCR4Feature(PVM pVM, RTHCUINTREG fOr, RTHCUINTREG fAnd);

VMMR3DECL(int)              CPUMR3CpuIdInsert(PVM pVM, PCPUMCPUIDLEAF pNewLeaf);
VMMR3DECL(int)              CPUMR3CpuIdGetLeaf(PVM pVM, PCPUMCPUIDLEAF pLeaf, uint32_t uLeaf, uint32_t uSubLeaf);
VMMR3_INT_DECL(PCCPUMCPUIDLEAF) CPUMR3CpuIdGetPtr(PVM pVM, uint32_t *pcLeaves);
VMMDECL(CPUMMICROARCH)      CPUMCpuIdDetermineX86MicroarchEx(CPUMCPUVENDOR enmVendor, uint8_t bFamily,
                                                             uint8_t bModel, uint8_t bStepping);
VMMDECL(const char *)       CPUMMicroarchName(CPUMMICROARCH enmMicroarch);
VMMR3DECL(int)              CPUMR3CpuIdDetectUnknownLeafMethod(PCPUMUNKNOWNCPUID penmUnknownMethod, PCPUMCPUID pDefUnknown);
VMMR3DECL(const char *)     CPUMR3CpuIdUnknownLeafMethodName(CPUMUNKNOWNCPUID enmUnknownMethod);
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
VMMR3DECL(uint32_t)         CPUMR3DeterminHostMxCsrMask(void);
#endif

VMMR3DECL(int)              CPUMR3MsrRangesInsert(PVM pVM, PCCPUMMSRRANGE pNewRange);

VMMR3_INT_DECL(void)    CPUMR3NemActivateGuestDebugState(PVMCPUCC pVCpu);
VMMR3_INT_DECL(void)    CPUMR3NemActivateHyperDebugState(PVMCPUCC pVCpu);
/** @} */
#endif /* IN_RING3 */

#ifdef IN_RING0
/** @defgroup grp_cpum_r0    The CPUM ring-0 API
 * @{
 */
VMMR0_INT_DECL(int)     CPUMR0ModuleInit(void);
VMMR0_INT_DECL(int)     CPUMR0ModuleTerm(void);
VMMR0_INT_DECL(void)    CPUMR0InitPerVMData(PGVM pGVM);
VMMR0_INT_DECL(int)     CPUMR0InitVM(PVMCC pVM);
DECLASM(void)           CPUMR0RegisterVCpuThread(PVMCPUCC pVCpu);
DECLASM(void)           CPUMR0TouchHostFpu(void);
VMMR0_INT_DECL(int)     CPUMR0Trap07Handler(PVMCC pVM, PVMCPUCC pVCpu);
VMMR0_INT_DECL(int)     CPUMR0LoadGuestFPU(PVMCC pVM, PVMCPUCC pVCpu);
VMMR0_INT_DECL(bool)    CPUMR0FpuStateMaybeSaveGuestAndRestoreHost(PVMCPUCC pVCpu);
VMMR0_INT_DECL(int)     CPUMR0SaveHostDebugState(PVMCC pVM, PVMCPUCC pVCpu);
VMMR0_INT_DECL(bool)    CPUMR0DebugStateMaybeSaveGuestAndRestoreHost(PVMCPUCC pVCpu, bool fDr6);
VMMR0_INT_DECL(bool)    CPUMR0DebugStateMaybeSaveGuest(PVMCPUCC pVCpu, bool fDr6);

VMMR0_INT_DECL(void)    CPUMR0LoadGuestDebugState(PVMCPUCC pVCpu, bool fDr6);
VMMR0_INT_DECL(void)    CPUMR0LoadHyperDebugState(PVMCPUCC pVCpu, bool fDr6);
/** @} */
#endif /* IN_RING0 */

/** @defgroup grp_cpum_rz    The CPUM raw-mode and ring-0 context API
 * @{
 */
VMMRZ_INT_DECL(void)    CPUMRZFpuStatePrepareHostCpuForUse(PVMCPUCC pVCpu);
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeForRead(PVMCPUCC pVCpu);
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeForChange(PVMCPUCC pVCpu);
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeSseForRead(PVMCPUCC pVCpu);
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeAvxForRead(PVMCPUCC pVCpu);
/** @} */


#endif /* !VBOX_FOR_DTRACE_LIB */
/** @} */
RT_C_DECLS_END


#endif /* !VBOX_INCLUDED_vmm_cpum_x86_amd64_h */

