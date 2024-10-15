/* $Id: Intel_Core_i7_2635QM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Core i7-2635QM".
 * Generated at 2014-02-28T18:53:09Z by VBoxCpuReport v4.3.53r92586 on darwin.amd64.
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

#ifndef VBOX_CPUDB_Intel_Core_i7_2635QM_h
#define VBOX_CPUDB_Intel_Core_i7_2635QM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Core(TM) i7-2635QM CPU @ 2.00GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Core_i7_2635QM[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000d, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000206a7, 0x04100800, 0x1fbae3bf, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x76035a01, 0x00f0b2ff, 0x00000000, 0x00ca0000, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x1c004121, 0x01c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x1c004122, 0x01c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x1c004143, 0x01c0003f, 0x000001ff, 0x00000000, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x1c03c163, 0x02c0003f, 0x00001fff, 0x00000006, 0 },
    { 0x00000004, 0x00000004, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00021120, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000077, 0x00000002, 0x00000009, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07300403, 0x00000000, 0x00000000, 0x00000603, 0 },
    /** @todo the b entry here is WRONG!   */
    { 0x0000000b, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000000, UINT32_MAX, 0x00000007, 0x00000340, 0x00000340, 0x00000000, 0 },
    { 0x0000000d, 0x00000001, UINT32_MAX, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000002, UINT32_MAX, 0x00000100, 0x00000240, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x28100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x6e492020, 0x286c6574, 0x43202952, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x2865726f, 0x20294d54, 0x322d3769, 0x51353336, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x5043204d, 0x20402055, 0x30302e32, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x01006040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000100, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003024, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Core(TM) i7-2635QM CPU @ 2.00GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Core_i7_2635QM[] =
{
    MFX(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr, Ia32P5McAddr, 0, UINT64_C(0xffffffffffffffe0), 0), /* value=0x1f */
    MFX(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType, Ia32P5McType, 0, 0, UINT64_MAX), /* value=0x0 */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, 0, UINT64_C(0xffffffffffff0000)), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x94d`1967512c */
    MFX(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0x10000000000000), 0, 0), /* value=0x100000`00000000 */
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0, UINT64_C(0xfffffff0000002ff)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, ReadOnly, 0, 0, 0), /* value=0x0 */
    MVX(0x0000002e, "I7_UNK_0000_002e", 0, 0x400, UINT64_C(0xfffffffffffffbff)),
    MVX(0x00000033, "TEST_CTL", 0, 0, UINT64_C(0xffffffff7fffffff)),
    MVO(0x00000034, "P6_UNK_0000_0034", 0x5),
    MFO(0x00000035, "MSR_CORE_THREAD_COUNT", IntelI7CoreThreadCount), /* value=0x40008 */
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    MVX(0x0000003e, "I7_UNK_0000_003e", 0, 0, UINT64_C(0xfffffffffffffffe)),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MFX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", Ia32BiosSignId, Ia32BiosSignId, 0, 0, UINT32_C(0xfffffffe)), /* value=0x28`00000000 */
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    RSN(0x000000c1, 0x000000c4, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MFO(0x000000ce, "IA32_PLATFORM_INFO", IntelPlatformInfo), /* value=0x800`60011400 */
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, 0, UINT64_C(0xffffffffe1ffffff)), /* value=0x405 */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, 0, UINT64_C(0xfffffffffff80000)), /* value=0x20414 */
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0x6a`9190b14b */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x69`df4de05c */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0xd0a, 0, 0), /* value=0xd0a */
    MFN(0x00000132, "CPUID1_FEATURE_MASK", IntelCpuId1FeatureMaskEax, IntelCpuId1FeatureMaskEax), /* value=0xffffffff`ffffffff */
    MFN(0x00000133, "CPUIDD_01_FEATURE_MASK", IntelCpuId1FeatureMaskEcdx, IntelCpuId1FeatureMaskEcdx), /* value=0xffffffff`ffffffff */
    MFN(0x00000134, "CPUID80000001_FEATURE_MASK", IntelCpuId80000001FeatureMaskEcdx, IntelCpuId80000001FeatureMaskEcdx), /* value=0xffffffff`ffffffff */
    MFX(0x0000013c, "I7_SB_AES_NI_CTL", IntelI7SandyAesNiCtl, IntelI7SandyAesNiCtl, 0, 0, UINT64_C(0xfffffffffffffffc)), /* value=0x0 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0xb */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0xffffff80`22904080 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0xffffff80`222f3030 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0xc09, 0, 0), /* value=0xc09 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, 0, UINT64_C(0xfffffffffffffff8)), /* value=0x0 */
    RSN(0x00000186, 0x00000189, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, 0, UINT64_C(0xffffffff00080000)),
    MFX(0x00000194, "CLOCK_FLEX_MAX", IntelFlexRatio, IntelFlexRatio, 0, 0xe0000, UINT64_C(0xfffffffffff00000)), /* value=0x0 */
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0x1d4d00000e00), 0, 0), /* value=0x1d4d`00000e00 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x1d00, 0, 0), /* Might bite. value=0x1d00 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0, 0, UINT64_C(0xffffffffffffffe0)), /* value=0x0 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0, 0, UINT64_C(0xfffffffffe0000e8)), /* value=0x0 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, UINT32_C(0x883d0000), UINT32_C(0xf87f0fff), UINT64_C(0xffffffff0780f000)), /* value=0x883d0000 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x850089, 0x1080, UINT64_C(0xffffffbbff3aef72)), /* value=0x850089 */
    MFX(0x000001a2, "I7_MSR_TEMPERATURE_TARGET", IntelI7TemperatureTarget, IntelI7TemperatureTarget, 0x640e00, 0xffff00, UINT64_C(0xfffffffff00000ff)), /* value=0x640e00 */
    MVX(0x000001a4, "I7_UNK_0000_01a4", 0, 0, UINT64_C(0xfffffffffffff7f0)),
    RSN(0x000001a6, 0x000001a7, "I7_MSR_OFFCORE_RSP_n", IntelI7MsrOffCoreResponseN, IntelI7MsrOffCoreResponseN, 0x0, 0, UINT64_C(0xffffffc000007000)),
    MVX(0x000001a8, "I7_UNK_0000_01a8", 0, 0, UINT64_C(0xfffffffffffffffc)),
    MFX(0x000001aa, "MSR_MISC_PWR_MGMT", IntelI7MiscPwrMgmt, IntelI7MiscPwrMgmt, 0, 0, UINT64_C(0xffffffffffbffffe)), /* value=0x400001 */
    MFX(0x000001ad, "I7_MSR_TURBO_RATIO_LIMIT", IntelI7TurboRatioLimit, ReadOnly, 0x1a1a1c1d, 0, 0), /* value=0x1a1a1c1d */
    MVX(0x000001b0, "IA32_ENERGY_PERF_BIAS", 0x4, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x000001b1, "IA32_PACKAGE_THERM_STATUS", UINT32_C(0x883a0000), UINT32_C(0xf87f0fff), UINT64_C(0xffffffff0780f000)),
    MVX(0x000001b2, "IA32_PACKAGE_THERM_INTERRUPT", 0, 0, UINT64_C(0xfffffffffe0000e8)),
    MVO(0x000001c6, "I7_UNK_0000_01c6", 0x3),
    MFX(0x000001c8, "MSR_LBR_SELECT", IntelI7LbrSelect, IntelI7LbrSelect, 0, 0, UINT64_C(0xfffffffffffffe00)), /* value=0x0 */
    MFX(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, 0, UINT64_C(0xfffffffffffffff0)), /* value=0xc */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffff803c)), /* value=0x0 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0x7fffff7f`a4a6e188 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xffffff80`222d5ad0 */
    MFN(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp, P6LastIntFromIp), /* value=0x0 */
    MFN(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp, P6LastIntToIp), /* value=0x0 */
    MVO(0x000001e1, "I7_SB_UNK_0000_01e1", 0x2),
    MFO(0x000001f0, "I7_VLW_CAPABILITY", IntelI7VirtualLegacyWireCap), /* value=0x74 */
    MFO(0x000001f2, "IA32_SMRR_PHYSBASE", Ia32SmrrPhysBase), /* value=0x0 */
    MFO(0x000001f3, "IA32_SMRR_PHYSMASK", Ia32SmrrPhysMask), /* value=0x0 */
    MFX(0x000001fc, "I7_MSR_POWER_CTL", IntelI7PowerCtl, IntelI7PowerCtl, 0, 0x20, UINT64_C(0xfffffffffff20000)), /* value=0x4005f */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xc0000000 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`c0000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xa0000000 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`e0000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x90000000 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`f0000800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x8c000000 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`fc000800 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x8b800000 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`ff800800 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x00000210, "IA32_MTRR_PHYS_BASE8", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x8, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x00000211, "IA32_MTRR_PHYS_MASK8", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x8, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x00000212, "IA32_MTRR_PHYS_BASE9", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x9, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x00000213, "IA32_MTRR_PHYS_MASK9", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x9, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
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
    MFX(0x00000283, "IA32_MC3_CTL2", Ia32McNCtl2, Ia32McNCtl2, 0x3, 0, UINT64_C(0xffffffffbfff8000)), /* value=0x0 */
    MFX(0x00000284, "IA32_MC4_CTL2", Ia32McNCtl2, Ia32McNCtl2, 0x4, 0x40007fff, UINT64_C(0xffffffffbfff8000)), /* value=0x0 */
    RSN(0x00000285, 0x00000288, "IA32_MC5_CTLn", Ia32McNCtl2, Ia32McNCtl2, 0x5, 0, UINT64_C(0xffffffffbfff8000)),
    MVX(0x000002e0, "I7_SB_NO_EVICT_MODE", 0, 0, UINT64_C(0xfffffffffffffffc)),
    MFN(0x000002e6, "I7_IB_UNK_0000_02e6", WriteOnly, IgnoreWrite),
    MVX(0x000002e7, "I7_IB_UNK_0000_02e7", 0x1, 0x1, UINT64_C(0xfffffffffffffffe)),
    MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE", Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, UINT64_C(0xfffffffffffff3f8)),
    MVO(0x00000305, "I7_SB_UNK_0000_0305", 0),
    RSN(0x00000309, 0x0000030b, "IA32_FIXED_CTRn", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, 0, UINT64_C(0xffff000000000000)),
    MFX(0x00000345, "IA32_PERF_CAPABILITIES", Ia32PerfCapabilities, ReadOnly, 0x31c3, 0, 0), /* value=0x31c3 */
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, 0, UINT64_C(0xfffffffffffff000)), /* value=0x0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl, 0, 0, UINT64_C(0xfffffff8fffffff0)), /* value=0xf */
    MFX(0x00000390, "IA32_PERF_GLOBAL_OVF_CTRL", Ia32PerfGlobalOvfCtrl, Ia32PerfGlobalOvfCtrl, 0, UINT64_C(0xe00000070000000f), UINT64_C(0x1ffffff8fffffff0)), /* value=0x0 */
    MFX(0x00000391, "I7_UNC_PERF_GLOBAL_CTRL", IntelI7UncPerfGlobalCtrl, IntelI7UncPerfGlobalCtrl, 0, 0, UINT64_C(0xffffffff1fffffe0)), /* value=0x0 */
    MFX(0x00000392, "I7_UNC_PERF_GLOBAL_STATUS", IntelI7UncPerfGlobalStatus, IntelI7UncPerfGlobalStatus, 0, 0xf, UINT64_C(0xfffffffffffffff0)), /* value=0x0 */
    MFX(0x00000393, "I7_UNC_PERF_GLOBAL_OVF_CTRL", IntelI7UncPerfGlobalOvfCtrl, IntelI7UncPerfGlobalOvfCtrl, 0, 0x3, UINT64_C(0xfffffffffffffffc)), /* value=0x0 */
    MFX(0x00000394, "I7_UNC_PERF_FIXED_CTR_CTRL", IntelI7UncPerfFixedCtrCtrl, IntelI7UncPerfFixedCtrCtrl, 0, 0, UINT64_C(0xffffffffffafffff)), /* value=0x0 */
    MFX(0x00000395, "I7_UNC_PERF_FIXED_CTR", IntelI7UncPerfFixedCtr, IntelI7UncPerfFixedCtr, 0, 0, UINT64_C(0xffff000000000000)), /* value=0x0 */
    MFO(0x00000396, "I7_UNC_CBO_CONFIG", IntelI7UncCBoxConfig), /* value=0x5 */
    MVX(0x00000397, "I7_SB_UNK_0000_0397", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MFX(0x000003b0, "I7_UNC_ARB_PERF_CTR0", IntelI7UncArbPerfCtrN, IntelI7UncArbPerfCtrN, 0, 0, UINT64_C(0xfffff00000000000)), /* value=0x0 */
    MFX(0x000003b1, "I7_UNC_ARB_PERF_CTR1", IntelI7UncArbPerfCtrN, IntelI7UncArbPerfCtrN, 0, 0, UINT64_C(0xfffff00000000000)), /* value=0x0 */
    MFX(0x000003b2, "I7_UNC_ARB_PERF_EVT_SEL0", IntelI7UncArbPerfEvtSelN, IntelI7UncArbPerfEvtSelN, 0, 0, UINT64_C(0xffffffffe0230000)), /* value=0x0 */
    MFX(0x000003b3, "I7_UNC_ARB_PERF_EVT_SEL1", IntelI7UncArbPerfEvtSelN, IntelI7UncArbPerfEvtSelN, 0, 0, UINT64_C(0xffffffffe0230000)), /* value=0x0 */
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, 0, UINT64_C(0x7ffffff0fffffff0)), /* value=0x0 */
    MFX(0x000003f6, "I7_MSR_PEBS_LD_LAT", IntelI7PebsLdLat, IntelI7PebsLdLat, 0, UINT64_C(0xffffffffffff0000), 0), /* value=0xffff */
    MFX(0x000003f8, "I7_MSR_PKG_C3_RESIDENCY", IntelI7PkgCnResidencyN, ReadOnly, 0x3, 0, UINT64_MAX), /* value=0x0 */
    RSN(0x000003f9, 0x000003fa, "I7_MSR_PKG_Cn_RESIDENCY", IntelI7PkgCnResidencyN, ReadOnly, 0x6, 0, UINT64_MAX),
    MFX(0x000003fc, "I7_MSR_CORE_C3_RESIDENCY", IntelI7CoreCnResidencyN, ReadOnly, 0x3, 0, UINT64_MAX), /* value=0x278ad50 */
    RSN(0x000003fd, 0x000003fe, "I7_MSR_CORE_Cn_RESIDENCY", IntelI7CoreCnResidencyN, ReadOnly, 0x6, 0, UINT64_MAX),
    RFN(0x00000400, 0x00000423, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
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
    MFX(0x0000048b, "IA32_VMX_PROCBASED_CTLS2", Ia32VmxProcBasedCtls2, ReadOnly, UINT64_C(0xff00000000), 0, 0), /* value=0xff`00000000 */
    MFX(0x0000048c, "IA32_VMX_EPT_VPID_CAP", Ia32VmxEptVpidCap, ReadOnly, UINT64_C(0xf0106114141), 0, 0), /* value=0xf01`06114141 */
    MFX(0x0000048d, "IA32_VMX_TRUE_PINBASED_CTLS", Ia32VmxTruePinbasedCtls, ReadOnly, UINT64_C(0x7f00000016), 0, 0), /* value=0x7f`00000016 */
    MFX(0x0000048e, "IA32_VMX_TRUE_PROCBASED_CTLS", Ia32VmxTrueProcbasedCtls, ReadOnly, UINT64_C(0xfff9fffe04006172), 0, 0), /* value=0xfff9fffe`04006172 */
    MFX(0x0000048f, "IA32_VMX_TRUE_EXIT_CTLS", Ia32VmxTrueExitCtls, ReadOnly, UINT64_C(0x7fffff00036dfb), 0, 0), /* value=0x7fffff`00036dfb */
    MFX(0x00000490, "IA32_VMX_TRUE_ENTRY_CTLS", Ia32VmxTrueEntryCtls, ReadOnly, UINT64_C(0xffff000011fb), 0, 0), /* value=0xffff`000011fb */
    RSN(0x000004c1, 0x000004c4, "IA32_A_PMCn", Ia32PmcN, Ia32PmcN, 0x0, 0, UINT64_C(0xffff000000000000)),
    MVO(0x00000502, "I7_SB_UNK_0000_0502", 0),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0x0 */
    MFX(0x00000601, "I7_SB_MSR_VR_CURRENT_CONFIG", IntelI7SandyVrCurrentConfig, IntelI7SandyVrCurrentConfig, 0, UINT32_C(0x80001fff), 0x7fffe000), /* value=0x18141494`8000030c */
    MVX(0x00000602, "I7_IB_UNK_0000_0602", UINT64_C(0x1814149480000104), UINT32_C(0x80001fff), 0x7fffe000),
    MFX(0x00000603, "I7_SB_MSR_VR_MISC_CONFIG", IntelI7SandyVrMiscConfig, IntelI7SandyVrMiscConfig, 0, UINT32_C(0x80ffffff), UINT64_C(0xffffffff7f000000)), /* value=0x80303030 */
    MVX(0x00000604, "I7_IB_UNK_0000_0602", UINT32_C(0x80646464), UINT32_C(0x80ffffff), UINT64_C(0xffffffff7f000000)),
    MFO(0x00000606, "I7_SB_MSR_RAPL_POWER_UNIT", IntelI7SandyRaplPowerUnit), /* value=0xa1003 */
    MVX(0x00000609, "I7_SB_UNK_0000_0609", 0, 0, UINT64_C(0xffffffffffffff00)),
    MFX(0x0000060a, "I7_SB_MSR_PKGC3_IRTL", IntelI7SandyPkgCnIrtlN, IntelI7SandyPkgCnIrtlN, 0x3, 0, UINT64_C(0xffffffffffff6000)), /* value=0x8c02 */
    RSN(0x0000060b, 0x0000060c, "I7_SB_MSR_PKGC6_IRTn", IntelI7SandyPkgCnIrtlN, IntelI7SandyPkgCnIrtlN, 0x6, 0, UINT64_C(0xffffffffffff6000)),
    MFO(0x0000060d, "I7_SB_MSR_PKG_C2_RESIDENCY", IntelI7SandyPkgC2Residency), /* value=0x11`06f311d4 */
    MFX(0x00000610, "I7_SB_MSR_PKG_POWER_LIMIT", IntelI7RaplPkgPowerLimit, IntelI7RaplPkgPowerLimit, 0, UINT64_C(0x80ffffff00ffffff), UINT64_C(0x7f000000ff000000)), /* value=0x800001c2`00dc8168 */
    MFO(0x00000611, "I7_SB_MSR_PKG_ENERGY_STATUS", IntelI7RaplPkgEnergyStatus), /* value=0x55a9ec99 */
    MFO(0x00000614, "I7_SB_MSR_PKG_POWER_INFO", IntelI7RaplPkgPowerInfo), /* value=0x100240`01200168 */
    MFX(0x00000638, "I7_SB_MSR_PP0_POWER_LIMIT", IntelI7RaplPp0PowerLimit, IntelI7RaplPp0PowerLimit, 0, UINT32_C(0x80ffffff), UINT64_C(0xffffffff7f000000)), /* value=0x80000000 */
    MFO(0x00000639, "I7_SB_MSR_PP0_ENERGY_STATUS", IntelI7RaplPp0EnergyStatus), /* value=0x1dcdc9a0 */
    MFX(0x0000063a, "I7_SB_MSR_PP0_POLICY", IntelI7RaplPp0Policy, IntelI7RaplPp0Policy, 0, 0, UINT64_C(0xffffffffffffffe0)), /* value=0x0 */
    MFX(0x00000640, "I7_HW_MSR_PP0_POWER_LIMIT", IntelI7RaplPp1PowerLimit, IntelI7RaplPp1PowerLimit, 0, UINT32_C(0x80ffffff), UINT64_C(0xffffffff7f000000)), /* value=0x80000000 */
    MFO(0x00000641, "I7_HW_MSR_PP0_ENERGY_STATUS", IntelI7RaplPp1EnergyStatus), /* value=0x39748b6 */
    MFX(0x00000642, "I7_HW_MSR_PP0_POLICY", IntelI7RaplPp1Policy, IntelI7RaplPp1Policy, 0, 0, UINT64_C(0xffffffffffffffe0)), /* value=0x10 */
    RFN(0x00000680, 0x0000068f, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    RFN(0x000006c0, 0x000006cf, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchToN, IntelLastBranchToN),
    MFX(0x000006e0, "IA32_TSC_DEADLINE", Ia32TscDeadline, Ia32TscDeadline, 0, UINT64_C(0x1000000018), 0), /* value=0x94d`402e841f */
    MVX(0x00000700, "MSR_UNC_CBO_0_PERFEVTSEL0", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000701, "MSR_UNC_CBO_0_PERFEVTSEL1", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000702, "MSR_UNC_CBO_0_PERFEVTSEL2?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000703, "MSR_UNC_CBO_0_PERFEVTSEL3?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000704, "MSR_UNC_CBO_0_UNK_4", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000705, "MSR_UNC_CBO_0_UNK_5", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000706, "MSR_UNC_CBO_0_PER_CTR0", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000707, "MSR_UNC_CBO_0_PER_CTR1", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000708, "MSR_UNC_CBO_0_PER_CTR2?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000709, "MSR_UNC_CBO_0_PER_CTR3?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000710, "MSR_UNC_CBO_1_PERFEVTSEL0", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000711, "MSR_UNC_CBO_1_PERFEVTSEL1", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000712, "MSR_UNC_CBO_1_PERFEVTSEL2?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000713, "MSR_UNC_CBO_1_PERFEVTSEL3?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000714, "MSR_UNC_CBO_1_UNK_4", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000715, "MSR_UNC_CBO_1_UNK_5", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000716, "MSR_UNC_CBO_1_PER_CTR0", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000717, "MSR_UNC_CBO_1_PER_CTR1", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000718, "MSR_UNC_CBO_1_PER_CTR2?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000719, "MSR_UNC_CBO_1_PER_CTR3?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000720, "MSR_UNC_CBO_2_PERFEVTSEL0", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000721, "MSR_UNC_CBO_2_PERFEVTSEL1", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000722, "MSR_UNC_CBO_2_PERFEVTSEL2?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000723, "MSR_UNC_CBO_2_PERFEVTSEL3?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000724, "MSR_UNC_CBO_2_UNK_4", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000725, "MSR_UNC_CBO_2_UNK_5", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000726, "MSR_UNC_CBO_2_PER_CTR0", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000727, "MSR_UNC_CBO_2_PER_CTR1", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000728, "MSR_UNC_CBO_2_PER_CTR2?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000729, "MSR_UNC_CBO_2_PER_CTR3?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000730, "MSR_UNC_CBO_3_PERFEVTSEL0", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000731, "MSR_UNC_CBO_3_PERFEVTSEL1", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000732, "MSR_UNC_CBO_3_PERFEVTSEL2?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000733, "MSR_UNC_CBO_3_PERFEVTSEL3?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000734, "MSR_UNC_CBO_3_UNK_4", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000735, "MSR_UNC_CBO_3_UNK_5", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000736, "MSR_UNC_CBO_3_PER_CTR0", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000737, "MSR_UNC_CBO_3_PER_CTR1", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000738, "MSR_UNC_CBO_3_PER_CTR2?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000739, "MSR_UNC_CBO_3_PER_CTR3?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000740, "MSR_UNC_CBO_4_PERFEVTSEL0?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000741, "MSR_UNC_CBO_4_PERFEVTSEL1?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000742, "MSR_UNC_CBO_4_PERFEVTSEL2?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000743, "MSR_UNC_CBO_4_PERFEVTSEL3?", 0, 0, UINT64_C(0xffffffffe0230000)),
    MVX(0x00000744, "MSR_UNC_CBO_4_UNK_4", 0, 0, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000745, "MSR_UNC_CBO_4_UNK_5", 0, 0xf, UINT64_C(0xfffffffffffffff0)),
    MVX(0x00000746, "MSR_UNC_CBO_4_PER_CTR0?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000747, "MSR_UNC_CBO_4_PER_CTR1?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000748, "MSR_UNC_CBO_4_PER_CTR2?", 0, 0, UINT64_C(0xfffff00000000000)),
    MVX(0x00000749, "MSR_UNC_CBO_4_PER_CTR3?", 0, 0, UINT64_C(0xfffff00000000000)),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x1b0008`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xffffff80`222f2fd0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0x0 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x4700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0x0 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xffffff81`0500f000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x7fff`7b14d3f0 */
    MFX(0xc0000103, "AMD64_TSC_AUX", Amd64TscAux, Amd64TscAux, 0, 0, ~(uint64_t)UINT32_MAX), /* value=0x0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Core(TM) i7-2635QM CPU @ 2.00GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Core_i7_2635QM =
{
    /*.pszName          = */ "Intel Core i7-2635QM",
    /*.pszFullName      = */ "Intel(R) Core(TM) i7-2635QM CPU @ 2.00GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 42,
    /*.uStepping        = */ 7,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Core7_SandyBridge,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_100MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 36,
    /*.fMxCsrMask       = */ 0xffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Core_i7_2635QM),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Core_i7_2635QM)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX,
    /*.DefUnknownCpuId  = */ { 0x00000007, 0x00000340, 0x00000340, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Core_i7_2635QM)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Core_i7_2635QM),
};

#endif /* !VBOX_CPUDB_Intel_Core_i7_2635QM_h */

