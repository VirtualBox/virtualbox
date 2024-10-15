/* $Id: Intel_Core_i7_3960X.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Core i7-3960X".
 * Generated at 2013-12-12T15:29:11Z by VBoxCpuReport v4.3.53r91237 on win.amd64.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_CPUDB_Intel_Core_i7_3960X_h
#define VBOX_CPUDB_Intel_Core_i7_3960X_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Core(TM) i7-3960X CPU @ 3.30GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Core_i7_3960X[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000d, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000206d6, 0x02200800, 0x1fbee3bf, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x76035a01, 0x00f0b2ff, 0x00000000, 0x00ca0000, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x3c004121, 0x01c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x3c004122, 0x01c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x3c004143, 0x01c0003f, 0x000001ff, 0x00000000, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x3c07c163, 0x04c0003f, 0x00002fff, 0x00000006, 0 },
    { 0x00000004, 0x00000004, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00021120, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000077, 0x00000002, 0x00000001, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07300403, 0x00000000, 0x00000000, 0x00000603, 0 },
    { 0x0000000b, 0x00000000, UINT32_MAX, 0x00000001, 0x00000002, 0x00000100, 0x00000002, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000b, 0x00000001, UINT32_MAX, 0x00000005, 0x0000000c, 0x00000201, 0x00000002, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000b, 0x00000002, UINT32_MAX, 0x00000000, 0x00000000, 0x00000002, 0x00000002, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000000, UINT32_MAX, 0x00000007, 0x00000340, 0x00000340, 0x00000000, 0 },
    { 0x0000000d, 0x00000001, UINT32_MAX, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000002, UINT32_MAX, 0x00000100, 0x00000240, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x2c100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x49202020, 0x6c65746e, 0x20295228, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x65726f43, 0x294d5428, 0x2d376920, 0x30363933, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x50432058, 0x20402055, 0x30332e33, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01006040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000100, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x0000302e, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Core(TM) i7-3960X CPU @ 3.30GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Core_i7_3960X[] =
{
    MFX(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr, Ia32P5McAddr, 0, UINT64_C(0xffffffffffffffe0), 0), /* value=0x1f */
    MFX(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType, Ia32P5McType, 0, 0, UINT64_MAX), /* value=0x0 */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, 0, UINT64_C(0xffffffffffff0000)), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x177ab4`48466b19 */
    MFV(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0x8000000000000)),
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0, UINT64_C(0xffffc000000002ff)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, ReadOnly, 0, 0, 0), /* value=0x0 */
    MVX(0x0000002e, "I7_UNK_0000_002e", 0, 0x400, UINT64_C(0xfffffffffffffbff)),
    MVX(0x00000033, "TEST_CTL", 0, 0, UINT64_C(0xffffffff7fffffff)),
    MVO(0x00000034, "P6_UNK_0000_0034", 0x4cb),
    MFO(0x00000035, "MSR_CORE_THREAD_COUNT", IntelI7CoreThreadCount), /* value=0x6000c*/
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    MVX(0x0000003e, "I7_UNK_0000_003e", 0x1, 0, UINT64_C(0xfffffffffffffffe)),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, Ia32BiosUpdateTrigger),
    MVX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", UINT64_C(0x61600000000), 0, UINT32_C(0xfffffffe)),
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    RSN(0x000000c1, 0x000000c4, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MFO(0x000000ce, "MSR_PLATFORM_INFO", IntelPlatformInfo), /* value=0xc00'70012100*/
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, 0, UINT64_C(0xffffffffe1ffffff)), /* value=0x1e008400 */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, 0, UINT64_C(0xfffffffffff80000)), /* value=0x20414 */
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0x2be98e4 */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x2d84ced */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0xd0a, 0, 0), /* value=0xd0a */
    MFN(0x00000132, "CPUID1_FEATURE_MASK", IntelCpuId1FeatureMaskEax, IntelCpuId1FeatureMaskEax), /* value=0xffffffff`ffffffff */
    MFN(0x00000133, "CPUIDD_01_FEATURE_MASK", IntelCpuId1FeatureMaskEcdx, IntelCpuId1FeatureMaskEcdx), /* value=0xffffffff`ffffffff */
    MFN(0x00000134, "CPUID80000001_FEATURE_MASK", IntelCpuId80000001FeatureMaskEcdx, IntelCpuId80000001FeatureMaskEcdx), /* value=0xffffffff`ffffffff */
    MFO(0x0000013c, "I7_SB_AES_NI_CTL", IntelI7SandyAesNiCtl), /* value=0x1 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0x0 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0x0 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0xc12, 0, 0), /* value=0xc12 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, 0, UINT64_C(0xfffffffffffffff8)), /* value=0x0 */
    MFX(0x0000017f, "I7_SB_ERROR_CONTROL", IntelI7SandyErrorControl, IntelI7SandyErrorControl, 0, 0xc, UINT64_C(0xffffffffffffffe1)), /* value=0x0 */
    RSN(0x00000186, 0x00000189, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, 0, UINT64_C(0xffffffff00080000)),
    MFX(0x00000194, "CLOCK_FLEX_MAX", IntelFlexRatio, IntelFlexRatio, 0xf2100, 0xe0000, UINT64_C(0xfffffffffff00000)),
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0x288300002400), 0, 0), /* value=0x2883`00002400 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x2700, 0, 0), /* Might bite. value=0x2700 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0, 0, UINT64_C(0xffffffffffffffe0)), /* value=0x0 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0, 0, UINT64_C(0xfffffffffe0000e8)), /* value=0x0 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, UINT32_C(0x88380000), UINT32_C(0xf87f0fff), UINT64_C(0xffffffff0780f000)), /* value=0x88380000 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x850089, 0x1080, UINT64_C(0xffffffbbff3aef72)), /* value=0x850089 */
    MFX(0x000001a2, "I7_MSR_TEMPERATURE_TARGET", IntelI7TemperatureTarget, IntelI7TemperatureTarget, 0x5b0a00, 0xffff00, UINT64_C(0xfffffffff00000ff)), /* value=0x5b0a00 */
    MVX(0x000001a4, "I7_UNK_0000_01a4", 0, 0, UINT64_C(0xfffffffffffff7f0)),
    RSN(0x000001a6, 0x000001a7, "I7_MSR_OFFCORE_RSP_n", IntelI7MsrOffCoreResponseN, IntelI7MsrOffCoreResponseN, 0x0, 0, UINT64_C(0xffffffc000007000)),
    MVX(0x000001a8, "I7_UNK_0000_01a8", 0, 0, UINT64_C(0xfffffffffffffffc)),
    MFX(0x000001aa, "MSR_MISC_PWR_MGMT", IntelI7MiscPwrMgmt, IntelI7MiscPwrMgmt, 0, 0, UINT64_C(0xffffffffffbffffe)), /* value=0x400000 */
    MFX(0x000001ad, "I7_MSR_TURBO_RATIO_LIMIT", IntelI7TurboRatioLimit, IntelI7TurboRatioLimit, UINT64_C(0x2424242425252727), 0, 0), /* value=0x24242424`25252727 */
    MVX(0x000001b1, "IA32_PACKAGE_THERM_STATUS", UINT32_C(0x88310000), UINT32_C(0xf87f0fff), UINT64_C(0xffffffff0780f000)),
    MVX(0x000001b2, "IA32_PACKAGE_THERM_INTERRUPT", 0, 0, UINT64_C(0xfffffffffe0000e8)),
    MVO(0x000001c6, "I7_UNK_0000_01c6", 0x3),
    MFX(0x000001c8, "MSR_LBR_SELECT", IntelI7LbrSelect, IntelI7LbrSelect, 0, 0, UINT64_C(0xfffffffffffffe00)), /* value=0x0 */
    MFX(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, 0, UINT64_C(0xfffffffffffffff0)), /* value=0xc */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffff803c)), /* value=0x0 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0x7ffff880`093814ea */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xfffff880`093a60e0 */
    MFN(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp, P6LastIntFromIp), /* value=0x0 */
    MFN(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp, P6LastIntToIp), /* value=0x0 */
    MVO(0x000001e1, "I7_SB_UNK_0000_01e1", 0x2),
    MVX(0x000001ef, "I7_SB_UNK_0000_01ef", 0xff, 0, UINT64_MAX),
    MFO(0x000001f0, "I7_VLW_CAPABILITY", IntelI7VirtualLegacyWireCap), /* value=0x74 */
    MFO(0x000001f2, "IA32_SMRR_PHYSBASE", Ia32SmrrPhysBase), /* value=0xad800006 */
    MFO(0x000001f3, "IA32_SMRR_PHYSMASK", Ia32SmrrPhysMask), /* value=0xff800800 */
    MFX(0x000001f8, "IA32_PLATFORM_DCA_CAP", Ia32PlatformDcaCap, Ia32PlatformDcaCap, 0, 0, UINT64_C(0xfffffffffffffffe)), /* value=0x1 */
    MFO(0x000001f9, "IA32_CPU_DCA_CAP", Ia32CpuDcaCap), /* value=0x1 */
    MFX(0x000001fa, "IA32_DCA_0_CAP", Ia32Dca0Cap, Ia32Dca0Cap, 0, 0x40007ff, UINT64_C(0xfffffffffafe1800)), /* value=0x1e489 */
    MFX(0x000001fc, "I7_MSR_POWER_CTL", IntelI7PowerCtl, IntelI7PowerCtl, 0, 0, UINT64_C(0xffffffff00320020)), /* value=0x2500005b */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xffffc000000007ff)), /* value=0x3ffc`00000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x4`00000006 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xffffc000000007ff)), /* value=0x3fff`c0000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x4`40000006 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xffffc000000007ff)), /* value=0x3fff`f0000800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xffffc00000000ff8)), /* value=0xae000000 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xffffc000000007ff)), /* value=0x3fff`fe000800 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xffffc00000000ff8)), /* value=0xb0000000 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xffffc000000007ff)), /* value=0x3fff`f0000800 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xffffc00000000ff8)), /* value=0xc0000000 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xffffc000000007ff)), /* value=0x3fff`c0000800 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xffffc000000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xffffc000000007ff)), /* value=0x0 */
    MFX(0x00000210, "IA32_MTRR_PHYS_BASE8", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x8, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x0 */
    MFX(0x00000211, "IA32_MTRR_PHYS_MASK8", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x8, 0, UINT64_C(0xffffc000000007ff)), /* value=0x0 */
    MFX(0x00000212, "IA32_MTRR_PHYS_BASE9", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x9, 0, UINT64_C(0xffffc00000000ff8)), /* value=0x0 */
    MFX(0x00000213, "IA32_MTRR_PHYS_MASK9", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x9, 0, UINT64_C(0xffffc000000007ff)), /* value=0x0 */
    MFS(0x00000250, "IA32_MTRR_FIX64K_00000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix64K_00000),
    MFS(0x00000258, "IA32_MTRR_FIX16K_80000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix16K_80000),
    MFS(0x00000259, "IA32_MTRR_FIX16K_A0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix16K_A0000),
    MFS(0x00000268, "IA32_MTRR_FIX4K_C0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_C0000),
    MFS(0x00000269, "IA32_MTRR_FIX4K_C8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_C8000),
    MFS(0x0000026a, "IA32_MTRR_FIX4K_D0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_D0000),
    MFS(0x0000026b, "IA32_MTRR_FIX4K_D8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_D8000),
    MFS(0x0000026c, "IA32_MTRR_FIX4K_E0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_E0000),
    MFS(0x0000026d, "IA32_MTRR_FIX4K_E8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_E8000),
    MFS(0x0000026e, "IA32_MTRR_FIX4K_F0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_F0000),
    MFS(0x0000026f, "IA32_MTRR_FIX4K_F8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_F8000),
    MFS(0x00000277, "IA32_PAT", Ia32Pat, Ia32Pat, Guest.msrPAT),
    RSN(0x00000280, 0x00000281, "IA32_MC0_CTLn", Ia32McNCtl2, Ia32McNCtl2, 0x0, 0, UINT64_C(0xffffffffbfff8000)),
    MFX(0x00000282, "IA32_MC2_CTL2", Ia32McNCtl2, Ia32McNCtl2, 0x2, 0x40007fff, UINT64_C(0xffffffffbfff8000)), /* value=0x0 */
    MFX(0x00000283, "IA32_MC3_CTL2", Ia32McNCtl2, Ia32McNCtl2, 0x3, 0, UINT64_C(0xffffffffbfff8000)), /* value=0x40000001 */
    MFX(0x00000284, "IA32_MC4_CTL2", Ia32McNCtl2, Ia32McNCtl2, 0x4, 0x40007fff, UINT64_C(0xffffffffbfff8000)), /* value=0x0 */
    RSN(0x00000285, 0x00000287, "IA32_MC5_CTLn", Ia32McNCtl2, Ia32McNCtl2, 0x5, 0, UINT64_C(0xffffffffbfff8000)),
    RSN(0x00000288, 0x0000028b, "IA32_MC8_CTLn", Ia32McNCtl2, Ia32McNCtl2, 0x8, 0x1, UINT64_C(0xffffffffbfff8000)),
    RSN(0x0000028c, 0x00000291, "IA32_MC12_CTLn", Ia32McNCtl2, Ia32McNCtl2, 0xc, 0, UINT64_C(0xffffffffbfff8000)),
    MVX(0x000002e0, "I7_SB_NO_EVICT_MODE", 0, 0, UINT64_C(0xfffffffffffffffc)),
    MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE", Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, UINT64_C(0xfffffffffffff3f8)),
    MVO(0x00000300, "I7_SB_UNK_0000_0300", UINT32_C(0x8000ff00)),
    MVO(0x00000305, "I7_SB_UNK_0000_0305", 0),
    RSN(0x00000309, 0x0000030b, "IA32_FIXED_CTRn", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, 0, UINT64_C(0xffff000000000000)),
    MFX(0x00000345, "IA32_PERF_CAPABILITIES", Ia32PerfCapabilities, ReadOnly, 0x31c3, 0, 0), /* value=0x31c3 */
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, 0, UINT64_C(0xfffffffffffff000)), /* value=0x0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl, 0, 0, UINT64_C(0xfffffff8fffffff0)), /* value=0xf */
    MFX(0x00000390, "IA32_PERF_GLOBAL_OVF_CTRL", Ia32PerfGlobalOvfCtrl, Ia32PerfGlobalOvfCtrl, 0, UINT64_C(0xe00000070000000f), UINT64_C(0x1ffffff8fffffff0)), /* value=0x0 */
    MFX(0x0000039c, "I7_SB_MSR_PEBS_NUM_ALT", IntelI7SandyPebsNumAlt, IntelI7SandyPebsNumAlt, 0, 0, UINT64_C(0xfffffffffffffffe)), /* value=0x0 */
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, 0, UINT64_C(0x7ffffff0fffffff0)), /* value=0x0 */
    MFX(0x000003f6, "I7_MSR_PEBS_LD_LAT", IntelI7PebsLdLat, IntelI7PebsLdLat, 0, UINT64_C(0xffffffffffff0000), 0), /* value=0xffff */
    MFX(0x000003f8, "I7_MSR_PKG_C3_RESIDENCY", IntelI7PkgCnResidencyN, ReadOnly, 0x3, 0, UINT64_MAX), /* value=0x0 */
    RSN(0x000003f9, 0x000003fa, "I7_MSR_PKG_Cn_RESIDENCY", IntelI7PkgCnResidencyN, ReadOnly, 0x6, 0, UINT64_MAX),
    MFX(0x000003fc, "I7_MSR_CORE_C3_RESIDENCY", IntelI7CoreCnResidencyN, ReadOnly, 0x3, 0, UINT64_MAX), /* value=0x3f8f`5718a87c */
    RSN(0x000003fd, 0x000003fe, "I7_MSR_CORE_Cn_RESIDENCY", IntelI7CoreCnResidencyN, ReadOnly, 0x6, 0, UINT64_MAX),
    RFN(0x00000400, 0x00000447, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFX(0x00000480, "IA32_VMX_BASIC", Ia32VmxBasic, ReadOnly, UINT64_C(0xda040000000010), 0, 0), /* value=0xda0400`00000010 */
    MFX(0x00000481, "IA32_VMX_PINBASED_CTLS", Ia32VmxPinbasedCtls, ReadOnly, UINT64_C(0x7f00000016), 0, 0), /* value=0x7f`00000016 */
    MFX(0x00000482, "IA32_VMX_PROCBASED_CTLS", Ia32VmxProcbasedCtls, ReadOnly, UINT64_C(0xfff9fffe0401e172), 0, 0), /* value=0xfff9fffe`0401e172 */
    MFX(0x00000483, "IA32_VMX_EXIT_CTLS", Ia32VmxExitCtls, ReadOnly, UINT64_C(0x7fffff00036dff), 0, 0), /* value=0x7fffff`00036dff */
    MFX(0x00000484, "IA32_VMX_ENTRY_CTLS", Ia32VmxEntryCtls, ReadOnly, UINT64_C(0xffff000011ff), 0, 0), /* value=0xffff`000011ff */
    MFX(0x00000485, "IA32_VMX_MISC", Ia32VmxMisc, ReadOnly, 0x100401e5, 0, 0), /* value=0x100401e5 */
    MFX(0x00000486, "IA32_VMX_CR0_FIXED0", Ia32VmxCr0Fixed0, ReadOnly, UINT32_C(0x80000021), 0, 0), /* value=0x80000021 */
    MFX(0x00000487, "IA32_VMX_CR0_FIXED1", Ia32VmxCr0Fixed1, ReadOnly, UINT32_MAX, 0, 0), /* value=0xffffffff */
    MFX(0x00000488, "IA32_VMX_CR4_FIXED0", Ia32VmxCr4Fixed0, ReadOnly, 0x2000, 0, 0), /* value=0x2000 */
    MFX(0x00000489, "IA32_VMX_CR4_FIXED1", Ia32VmxCr4Fixed1, ReadOnly, 0x627ff, 0, 0), /* value=0x627ff */
    MFX(0x0000048a, "IA32_VMX_VMCS_ENUM", Ia32VmxVmcsEnum, ReadOnly, 0x2a, 0, 0), /* value=0x2a */
    MFX(0x0000048b, "IA32_VMX_PROCBASED_CTLS2", Ia32VmxProcBasedCtls2, ReadOnly, UINT64_C(0x4ff00000000), 0, 0), /* value=0x4ff`00000000 */
    MFX(0x0000048c, "IA32_VMX_EPT_VPID_CAP", Ia32VmxEptVpidCap, ReadOnly, UINT64_C(0xf0106134141), 0, 0), /* value=0xf01`06134141 */
    MFX(0x0000048d, "IA32_VMX_TRUE_PINBASED_CTLS", Ia32VmxTruePinbasedCtls, ReadOnly, UINT64_C(0x7f00000016), 0, 0), /* value=0x7f`00000016 */
    MFX(0x0000048e, "IA32_VMX_TRUE_PROCBASED_CTLS", Ia32VmxTrueProcbasedCtls, ReadOnly, UINT64_C(0xfff9fffe04006172), 0, 0), /* value=0xfff9fffe`04006172 */
    MFX(0x0000048f, "IA32_VMX_TRUE_EXIT_CTLS", Ia32VmxTrueExitCtls, ReadOnly, UINT64_C(0x7fffff00036dfb), 0, 0), /* value=0x7fffff`00036dfb */
    MFX(0x00000490, "IA32_VMX_TRUE_ENTRY_CTLS", Ia32VmxTrueEntryCtls, ReadOnly, UINT64_C(0xffff000011fb), 0, 0), /* value=0xffff`000011fb */
    RSN(0x000004c1, 0x000004c4, "IA32_A_PMCn", Ia32PmcN, Ia32PmcN, 0x0, 0, UINT64_C(0xffff000000000000)),
    MVO(0x00000502, "I7_SB_UNK_0000_0502", 0),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0x0 */
    MFX(0x00000601, "I7_SB_MSR_VR_CURRENT_CONFIG", IntelI7SandyVrCurrentConfig, IntelI7SandyVrCurrentConfig, 0, UINT32_C(0x80001fff), 0x7fffe000), /* value=0x141494`80000640 */
    MFX(0x00000603, "I7_SB_MSR_VR_MISC_CONFIG", IntelI7SandyVrMiscConfig, IntelI7SandyVrMiscConfig, 0, UINT32_C(0x80ffffff), UINT64_C(0xffffffff7f000000)), /* value=0x80151515 */
    MFO(0x00000606, "I7_SB_MSR_RAPL_POWER_UNIT", IntelI7SandyRaplPowerUnit), /* value=0xa1003 */
    MFX(0x0000060a, "I7_SB_MSR_PKGC3_IRTL", IntelI7SandyPkgCnIrtlN, IntelI7SandyPkgCnIrtlN, 0x3, 0, UINT64_C(0xffffffffffff6000)), /* value=0x0 */
    RSN(0x0000060b, 0x0000060c, "I7_SB_MSR_PKGC6_IRTn", IntelI7SandyPkgCnIrtlN, IntelI7SandyPkgCnIrtlN, 0x6, 0, UINT64_C(0xffffffffffff6000)),
    MFO(0x0000060d, "I7_SB_MSR_PKG_C2_RESIDENCY", IntelI7SandyPkgC2Residency), /* value=0x0 */
    MFX(0x00000610, "I7_SB_MSR_PKG_POWER_LIMIT", IntelI7RaplPkgPowerLimit, IntelI7RaplPkgPowerLimit, 0, UINT64_C(0x80ffffff00ffffff), UINT64_C(0x7f000000ff000000)), /* value=0x80068960`005affff */
    MFO(0x00000611, "I7_SB_MSR_PKG_ENERGY_STATUS", IntelI7RaplPkgEnergyStatus), /* value=0xc120ff02 */
    MFO(0x00000613, "I7_SB_MSR_PKG_PERF_STATUS", IntelI7RaplPkgPerfStatus), /* value=0x0 */
    MFO(0x00000614, "I7_SB_MSR_PKG_POWER_INFO", IntelI7RaplPkgPowerInfo), /* value=0x1a80410 */
    MFX(0x00000618, "I7_SB_MSR_DRAM_POWER_LIMIT", IntelI7RaplDramPowerLimit, IntelI7RaplDramPowerLimit, 0, UINT32_C(0x80feffff), UINT64_C(0xffffffff7f010000)), /* value=0x80000000 */
    MFO(0x00000619, "I7_SB_MSR_DRAM_ENERGY_STATUS", IntelI7RaplDramEnergyStatus), /* value=0x0 */
    MFO(0x0000061b, "I7_SB_MSR_DRAM_PERF_STATUS", IntelI7RaplDramPerfStatus), /* value=0x0 */
    MFO(0x0000061c, "I7_SB_MSR_DRAM_POWER_INFO", IntelI7RaplDramPowerInfo), /* value=0x280258`00780118 */
    MFX(0x00000638, "I7_SB_MSR_PP0_POWER_LIMIT", IntelI7RaplPp0PowerLimit, IntelI7RaplPp0PowerLimit, 0, UINT32_C(0x80ffffff), UINT64_C(0xffffffff7f000000)), /* value=0x80000000 */
    MFO(0x00000639, "I7_SB_MSR_PP0_ENERGY_STATUS", IntelI7RaplPp0EnergyStatus), /* value=0x448bc04 */
    MFX(0x0000063a, "I7_SB_MSR_PP0_POLICY", IntelI7RaplPp0Policy, IntelI7RaplPp0Policy, 0, 0, UINT64_C(0xffffffffffffffe0)), /* value=0x0 */
    MFO(0x0000063b, "I7_SB_MSR_PP0_PERF_STATUS", IntelI7RaplPp0PerfStatus), /* value=0x0 */
    RFN(0x00000680, 0x0000068f, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    RFN(0x000006c0, 0x000006cf, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    MFI(0x000006e0, "IA32_TSC_DEADLINE", Ia32TscDeadline), /* value=0x0 */
    MVX(0x00000a00, "I7_SB_UNK_0000_0a00", 0, 0, UINT64_C(0xfffffffffffffec0)),
    MVX(0x00000a01, "I7_SB_UNK_0000_0a01", 0x178fa000, 0, UINT64_C(0xffffffff00000f80)),
    MVX(0x00000a02, "I7_SB_UNK_0000_0a02", 0, 0, UINT64_C(0xffffffff20002000)),
    MVX(0x00000c00, "I7_SB_UNK_0000_0c00", 0, 0, UINT64_C(0xffffffffbfffff00)),
    MVX(0x00000c01, "I7_SB_UNK_0000_0c01", 0, 0x9229fe7, UINT64_C(0xfffffffff6dd6018)),
    MVO(0x00000c06, "I7_SB_UNK_0000_0c06", 0x6),
    MVX(0x00000c08, "I7_SB_UNK_0000_0c08", 0, 0, UINT64_C(0xffffffffffafffff)),
    MVX(0x00000c09, "I7_SB_UNK_0000_0c09", 0x301a, 0, UINT64_C(0xffff000000000000)),
    MVX(0x00000c10, "I7_SB_UNK_0000_0c10", 0, 0x20000, UINT64_C(0xffffffffe0210000)),
    MVX(0x00000c11, "I7_SB_UNK_0000_0c11", 0, 0x20000, UINT64_C(0xffffffffe0210000)),
    MVX(0x00000c14, "I7_SB_UNK_0000_0c14", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000c15, "I7_SB_UNK_0000_0c15", 0, 0x3, UINT64_C(0xfffffffffffffffc)),
    MVX(0x00000c16, "I7_SB_UNK_0000_0c16", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000c17, "I7_SB_UNK_0000_0c17", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000c24, "I7_SB_UNK_0000_0c24", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000c30, "I7_SB_UNK_0000_0c30", 0, 0x20000, UINT64_C(0xffffffff20013f00)),
    MVX(0x00000c31, "I7_SB_UNK_0000_0c31", 0, 0x20000, UINT64_C(0xffffffff20013f00)),
    MVX(0x00000c32, "I7_SB_UNK_0000_0c32", 0, 0x20000, UINT64_C(0xffffffff20013f00)),
    MVX(0x00000c33, "I7_SB_UNK_0000_0c33", 0, 0x20000, UINT64_C(0xffffffff20013f00)),
    MVX(0x00000c34, "I7_SB_UNK_0000_0c34", 0, 0, ~(uint64_t)UINT32_MAX),
    MVX(0x00000c35, "I7_SB_UNK_0000_0c35", 0, 0x7f, UINT64_C(0xffffffffffffff80)),
    MVX(0x00000c36, "I7_SB_UNK_0000_0c36", 0x203, 0, UINT64_C(0xffff000000000000)),
    MVX(0x00000c37, "I7_SB_UNK_0000_0c37", 0x203, 0, UINT64_C(0xffff000000000000)),
    MVX(0x00000c38, "I7_SB_UNK_0000_0c38", 0x20c, 0, UINT64_C(0xffff000000000000)),
    MVX(0x00000c39, "I7_SB_UNK_0000_0c39", 0x203, 0, UINT64_C(0xffff000000000000)),
    MVX(0x00000d04, "I7_SB_UNK_0000_0d04", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000d10, "I7_SB_UNK_0000_0d10", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d11, "I7_SB_UNK_0000_0d11", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d12, "I7_SB_UNK_0000_0d12", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d13, "I7_SB_UNK_0000_0d13", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d14, "I7_SB_UNK_0000_0d14", 0x20, 0, UINT64_C(0xffffffff00000300)),
    MVX(0x00000d15, "I7_SB_UNK_0000_0d15", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000d16, "I7_SB_UNK_0000_0d16", 0x81c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d17, "I7_SB_UNK_0000_0d17", 0x80c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d18, "I7_SB_UNK_0000_0d18", 0x80c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d19, "I7_SB_UNK_0000_0d19", 0x810, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d24, "I7_SB_UNK_0000_0d24", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000d30, "I7_SB_UNK_0000_0d30", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d31, "I7_SB_UNK_0000_0d31", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d32, "I7_SB_UNK_0000_0d32", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d33, "I7_SB_UNK_0000_0d33", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d34, "I7_SB_UNK_0000_0d34", 0x20, 0, UINT64_C(0xffffffff00000300)),
    MVX(0x00000d35, "I7_SB_UNK_0000_0d35", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000d36, "I7_SB_UNK_0000_0d36", 0x864, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d37, "I7_SB_UNK_0000_0d37", 0x804, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d38, "I7_SB_UNK_0000_0d38", 0x822, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d39, "I7_SB_UNK_0000_0d39", 0x81c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d44, "I7_SB_UNK_0000_0d44", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000d50, "I7_SB_UNK_0000_0d50", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d51, "I7_SB_UNK_0000_0d51", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d52, "I7_SB_UNK_0000_0d52", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d53, "I7_SB_UNK_0000_0d53", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d54, "I7_SB_UNK_0000_0d54", 0x20, 0, UINT64_C(0xffffffff00000300)),
    MVX(0x00000d55, "I7_SB_UNK_0000_0d55", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000d56, "I7_SB_UNK_0000_0d56", 0x848, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d57, "I7_SB_UNK_0000_0d57", 0x866, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d58, "I7_SB_UNK_0000_0d58", 0x83c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d59, "I7_SB_UNK_0000_0d59", 0x83c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d64, "I7_SB_UNK_0000_0d64", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000d70, "I7_SB_UNK_0000_0d70", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d71, "I7_SB_UNK_0000_0d71", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d72, "I7_SB_UNK_0000_0d72", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d73, "I7_SB_UNK_0000_0d73", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d74, "I7_SB_UNK_0000_0d74", 0x20, 0, UINT64_C(0xffffffff00000300)),
    MVX(0x00000d75, "I7_SB_UNK_0000_0d75", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000d76, "I7_SB_UNK_0000_0d76", 0x846, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d77, "I7_SB_UNK_0000_0d77", 0x90c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d78, "I7_SB_UNK_0000_0d78", 0x846, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d79, "I7_SB_UNK_0000_0d79", 0x842, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d84, "I7_SB_UNK_0000_0d84", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000d90, "I7_SB_UNK_0000_0d90", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d91, "I7_SB_UNK_0000_0d91", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d92, "I7_SB_UNK_0000_0d92", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d93, "I7_SB_UNK_0000_0d93", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000d94, "I7_SB_UNK_0000_0d94", 0x20, 0, UINT64_C(0xffffffff00000300)),
    MVX(0x00000d95, "I7_SB_UNK_0000_0d95", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000d96, "I7_SB_UNK_0000_0d96", 0x8c6, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d97, "I7_SB_UNK_0000_0d97", 0x840, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d98, "I7_SB_UNK_0000_0d98", 0x81a, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000d99, "I7_SB_UNK_0000_0d99", 0x910, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000da4, "I7_SB_UNK_0000_0da4", 0, 0x3, UINT64_C(0xfffffffffffcfefc)),
    MVX(0x00000db0, "I7_SB_UNK_0000_0db0", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000db1, "I7_SB_UNK_0000_0db1", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000db2, "I7_SB_UNK_0000_0db2", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000db3, "I7_SB_UNK_0000_0db3", 0, 0x30000, UINT64_C(0xffffffff00200000)),
    MVX(0x00000db4, "I7_SB_UNK_0000_0db4", 0x20, 0, UINT64_C(0xffffffff00000300)),
    MVX(0x00000db5, "I7_SB_UNK_0000_0db5", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000db6, "I7_SB_UNK_0000_0db6", 0x80c, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000db7, "I7_SB_UNK_0000_0db7", 0x81e, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000db8, "I7_SB_UNK_0000_0db8", 0x810, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000db9, "I7_SB_UNK_0000_0db9", 0x80a, 0, UINT64_C(0xfffff00000000000)),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xfffff800`030dac00 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0xfffff800`030da940 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x4700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0xfffe0000 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xfffff880`061e6000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x7ff`fffde000 */
    MFX(0xc0000103, "AMD64_TSC_AUX", Amd64TscAux, Amd64TscAux, 0, 0, ~(uint64_t)UINT32_MAX), /* value=0x0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Core(TM) i7-3960X CPU @ 3.30GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Core_i7_3960X =
{
    /*.pszName          = */ "Intel Core i7-3960X",
    /*.pszFullName      = */ "Intel(R) Core(TM) i7-3960X CPU @ 3.30GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 45,
    /*.uStepping        = */ 6,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Core7_SandyBridge,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_100MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 46,
    /*.fMxCsrMask       = */ 0xffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Core_i7_3960X),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Core_i7_3960X)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX,
    /*.DefUnknownCpuId  = */ { 0x00000007, 0x00000340, 0x00000340, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Core_i7_3960X)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Core_i7_3960X),
};

#endif /* !VBOX_CPUDB_Intel_Core_i7_3960X_h */

