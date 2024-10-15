/* $Id: Intel_Pentium_N3530_2_16GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Pentium N3530 2.16GHz".
 * Generated at 2016-04-29T13:34:27Z by VBoxCpuReport v5.0.51r106929 on win.amd64.
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

#ifndef VBOX_CPUDB_Intel_Pentium_N3530_2_16GHz_h
#define VBOX_CPUDB_Intel_Pentium_N3530_2_16GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Pentium(R) CPU  N3530  @ 2.16GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Pentium_N3530_2_16GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000b, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x00030678, 0x02100800, 0x41d8e3bf, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x61b3a001, 0x0000ffc2, 0x00000000, 0x00000000, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x1c000121, 0x0140003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x1c000122, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x1c00c143, 0x03c0003f, 0x000003ff, 0x00000001, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x33000020, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000007, 0x00000002, 0x00000009, 0x00000000, 0 },
    { 0x00000007, 0x00000000, UINT32_MAX, 0x00000000, 0x00002282, 0x00000000, 0x00000000, 0 },
    { 0x00000007, 0x00000001, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07280203, 0x00000000, 0x00000000, 0x00004503, 0 },
    { 0x0000000b, 0x00000000, UINT32_MAX, 0x00000001, 0x00000001, 0x00000100, 0x00000002, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000b, 0x00000001, UINT32_MAX, 0x00000004, 0x00000004, 0x00000201, 0x00000002, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000b, 0x00000002, UINT32_MAX, 0x00000000, 0x00000000, 0x00000002, 0x00000002, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000101, 0x28100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x6e492020, 0x286c6574, 0x50202952, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x69746e65, 0x52286d75, 0x50432029, 0x4e202055, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x30333533, 0x20402020, 0x36312e32, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x04008040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000100, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003024, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Pentium(R) CPU  N3530  @ 2.16GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Pentium_N3530_2_16GHz[] =
{
    MFI(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr), /* value=0x0 */
    MFX(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType, Ia32P5McType, 0, 0, UINT64_MAX), /* value=0x0 */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, 0, UINT64_C(0xffffffffffff0000)), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x4c5e`43033c62 */
    MFX(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0xc000090341f52), 0, 0), /* value=0xc0000`90341f52 */
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0, UINT64_C(0xfffffff0000006ff)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, IntelEblCrPowerOn, 0x40080000, UINT32_C(0xfff7ffff), ~(uint64_t)UINT32_MAX), /* value=0x40080000 */
    MVX(0x00000033, "TEST_CTL", 0, 0, UINT64_C(0xffffffff7fffffff)),
    MFO(0x00000034, "MSR_SMI_COUNT", IntelI7SmiCount), /* value=0xa */
    MVO(0x00000039, "C2_UNK_0000_0039", 0x2),
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    MVX(0x0000003b, "P6_UNK_0000_003b", UINT64_C(0x4c27f41f3066), 0x800, 0),
    RFN(0x00000040, 0x00000047, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchToN, IntelLastBranchToN),
    RFN(0x00000060, 0x00000067, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MFX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", Ia32BiosSignId, Ia32BiosSignId, 0, 0, UINT32_MAX), /* value=0x809`00000000 */
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    RSN(0x000000c1, 0x000000c2, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MFI(0x000000c7, "IA32_PMC6", Ia32PmcN), /* value=0x36c9 */
    MFX(0x000000cd, "MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0, 0, 0), /* value=0x0 */
    MVO(0x000000ce, "IA32_PLATFORM_INFO", UINT64_C(0x60000001a00)),
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, 0, UINT64_C(0xffffffffffc073f0)), /* value=0x1a000f */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, 0, UINT64_C(0xffffffffff800000)), /* value=0x20000 */
    MVO(0x000000e5, "C2_UNK_0000_00e5", UINT32_C(0x80031838)),
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0x1f8f8 */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x9875 */
    MFX(0x000000ee, "C1_EXT_CONFIG", IntelCore1ExtConfig, IntelCore1ExtConfig, 0, UINT32_C(0xefc5ffff), UINT64_C(0xffffffff10000000)), /* value=0x2380002 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0xd08, 0, 0), /* value=0xd08 */
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, IntelBblCrCtl3, 0x7e2801ff, UINT32_C(0xfe83f8ff), UINT64_C(0xffffffff00400600)), /* value=0x7e2801ff */
    MVX(0x00000120, "SILV_UNK_0000_0120", 0x44, 0x40, UINT64_C(0xffffffffffffff33)),
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0x0 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0x0 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x806, 0, 0), /* value=0x806 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, 0, UINT64_C(0xfffffffffffffff8)), /* value=0x0 */
    RSN(0x00000186, 0x00000187, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, 0, ~(uint64_t)UINT32_MAX),
    MFX(0x00000194, "CLOCK_FLEX_MAX", IntelFlexRatio, IntelFlexRatio, 0, UINT32_C(0xfffec080), ~(uint64_t)UINT32_MAX), /* value=0x0 */
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0x880000001f52), 0, 0), /* value=0x8800`00001f52 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x1f52, 0, 0), /* Might bite. value=0x1f52 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0, 0, UINT64_C(0xffffffffffffffe1)), /* value=0x0 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0xcbb700, 0, UINT64_C(0xfffffffffe0000e8)), /* value=0xcbb700 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, UINT32_C(0x88420100), UINT32_C(0xfffff555), ~(uint64_t)UINT32_MAX), /* value=0x88420100 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0x623, 0, 0), /* value=0x623 */
    MVX(0x0000019e, "P6_UNK_0000_019e", 0, UINT32_MAX, ~(uint64_t)UINT32_MAX),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x850089, 0x1080, UINT64_C(0xffffffbbff3aef76)), /* value=0x850089 */
    MFX(0x000001a2, "I7_MSR_TEMPERATURE_TARGET", IntelI7TemperatureTarget, IntelI7TemperatureTarget, 0x690000, 0xff0000, UINT64_C(0xffffffffc000ffff)), /* value=0x690000 */
    MFX(0x000001a6, "I7_MSR_OFFCORE_RSP_0", IntelI7MsrOffCoreResponseN, IntelI7MsrOffCoreResponseN, 0x0, 0, UINT64_C(0xffffff897ffa0000)), /* XXX: The range ended earlier than expected! */
    MFX(0x000001a7, "I7_MSR_OFFCORE_RSP_1", IntelI7MsrOffCoreResponseN, IntelI7MsrOffCoreResponseN, 0x0, 0, UINT64_C(0xffffffc97ffa0000)), /* value=0x0 */
    MFX(0x000001ad, "I7_MSR_TURBO_RATIO_LIMIT", IntelI7TurboRatioLimit, IntelI7TurboRatioLimit, 0, UINT64_C(0x3f3f3f3f00000000), UINT64_C(0xc0c0c0c0c0c0c0c0)), /* value=0x0 */
    MVX(0x000001b0, "IA32_ENERGY_PERF_BIAS", 0x6, 0, UINT64_C(0xfffffffffffffff0)),
    MVO(0x000001c6, "I7_UNK_0000_01c6", 0x3),
    MFX(0x000001c8, "MSR_LBR_SELECT", IntelI7LbrSelect, IntelI7LbrSelect, 0, 0x200, UINT64_C(0xfffffffffffffc00)), /* value=0x0 */
    MFX(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, 0, UINT64_C(0xfffffffffffffff8)), /* value=0x0 */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffffa03c)), /* value=0x0 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0x0 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0x0 */
    MFN(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp, P6LastIntFromIp), /* value=0x0 */
    MFN(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp, P6LastIntToIp), /* value=0x0 */
    MFO(0x000001f2, "IA32_SMRR_PHYSBASE", Ia32SmrrPhysBase), /* value=0x7a000006 */
    MFO(0x000001f3, "IA32_SMRR_PHYSMASK", Ia32SmrrPhysMask), /* value=0xff800800 */
    MFX(0x000001fc, "I7_MSR_POWER_CTL", IntelI7PowerCtl, IntelI7PowerCtl, 0, 0, UINT64_C(0xfffffffffffffffd)), /* value=0x0 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xffc00005 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`ffc00800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xffb80000 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`fff80800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x6 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`80000800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x7c000000 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`fc000800 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x7b000000 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`ff000800 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x7ae00000 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`ffe00800 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x1`00000006 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`80000800 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
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
    MVX(0x000002e0, "I7_SB_NO_EVICT_MODE", 0, 0, UINT64_C(0xfffffffffffffffc)),
    MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE", Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, UINT64_C(0xfffffffffffff3f8)),
    RSN(0x00000309, 0x0000030b, "IA32_FIXED_CTRn", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, 0, UINT64_C(0xffffff0000000000)),
    MFX(0x00000345, "IA32_PERF_CAPABILITIES", Ia32PerfCapabilities, ReadOnly, 0x32c1, 0, 0), /* value=0x32c1 */
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, 0, UINT64_C(0xfffffffffffff000)), /* value=0x0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl, 0, 0, UINT64_C(0xfffffff8fffffffc)), /* value=0x3 */
    MFX(0x00000390, "IA32_PERF_GLOBAL_OVF_CTRL", Ia32PerfGlobalOvfCtrl, Ia32PerfGlobalOvfCtrl, 0, UINT64_C(0xc000000700000003), UINT64_C(0x3ffffff8fffffffc)), /* value=0x0 */
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, 0, UINT64_C(0xfffffffffffffffe)), /* value=0x0 */
    MFX(0x000003f8, "I7_MSR_PKG_C3_RESIDENCY", IntelI7PkgCnResidencyN, ReadOnly, 0x3, 0, UINT64_MAX), /* value=0x0 */
    RSN(0x000003f9, 0x000003fa, "I7_MSR_PKG_Cn_RESIDENCY", IntelI7PkgCnResidencyN, ReadOnly, 0x6, 0, UINT64_MAX),
    MFX(0x000003fc, "I7_MSR_CORE_C3_RESIDENCY", IntelI7CoreCnResidencyN, ReadOnly, 0x3, 0, UINT64_MAX), /* value=0x80000000`0000ad5b */
    MFX(0x000003fd, "I7_MSR_CORE_C6_RESIDENCY", IntelI7CoreCnResidencyN, ReadOnly, 0x6, 0, UINT64_MAX), /* value=0x5`51eddedc */
    RFN(0x00000400, 0x00000417, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFX(0x00000480, "IA32_VMX_BASIC", Ia32VmxBasic, ReadOnly, UINT64_C(0xda040000000002), 0, 0), /* value=0xda0400`00000002 */
    MFX(0x00000481, "IA32_VMX_PINBASED_CTLS", Ia32VmxPinbasedCtls, ReadOnly, UINT64_C(0x7f00000016), 0, 0), /* value=0x7f`00000016 */
    MFX(0x00000482, "IA32_VMX_PROCBASED_CTLS", Ia32VmxProcbasedCtls, ReadOnly, UINT64_C(0xfff9fffe0401e172), 0, 0), /* value=0xfff9fffe`0401e172 */
    MFX(0x00000483, "IA32_VMX_EXIT_CTLS", Ia32VmxExitCtls, ReadOnly, UINT64_C(0x7fffff00036dff), 0, 0), /* value=0x7fffff`00036dff */
    MFX(0x00000484, "IA32_VMX_ENTRY_CTLS", Ia32VmxEntryCtls, ReadOnly, UINT64_C(0xffff000011ff), 0, 0), /* value=0xffff`000011ff */
    MFX(0x00000485, "IA32_VMX_MISC", Ia32VmxMisc, ReadOnly, 0x481e6, 0, 0), /* value=0x481e6 */
    MFX(0x00000486, "IA32_VMX_CR0_FIXED0", Ia32VmxCr0Fixed0, ReadOnly, UINT32_C(0x80000021), 0, 0), /* value=0x80000021 */
    MFX(0x00000487, "IA32_VMX_CR0_FIXED1", Ia32VmxCr0Fixed1, ReadOnly, UINT32_MAX, 0, 0), /* value=0xffffffff */
    MFX(0x00000488, "IA32_VMX_CR4_FIXED0", Ia32VmxCr4Fixed0, ReadOnly, 0x2000, 0, 0), /* value=0x2000 */
    MFX(0x00000489, "IA32_VMX_CR4_FIXED1", Ia32VmxCr4Fixed1, ReadOnly, 0x1027ff, 0, 0), /* value=0x1027ff */
    MFX(0x0000048a, "IA32_VMX_VMCS_ENUM", Ia32VmxVmcsEnum, ReadOnly, 0x2e, 0, 0), /* value=0x2e */
    MFX(0x0000048b, "IA32_VMX_PROCBASED_CTLS2", Ia32VmxProcBasedCtls2, ReadOnly, UINT64_C(0x28ef00000000), 0, 0), /* value=0x28ef`00000000 */
    MFX(0x0000048c, "IA32_VMX_EPT_VPID_CAP", Ia32VmxEptVpidCap, ReadOnly, UINT64_C(0xf0106114141), 0, 0), /* value=0xf01`06114141 */
    MFX(0x0000048d, "IA32_VMX_TRUE_PINBASED_CTLS", Ia32VmxTruePinbasedCtls, ReadOnly, UINT64_C(0x7f00000016), 0, 0), /* value=0x7f`00000016 */
    MFX(0x0000048e, "IA32_VMX_TRUE_PROCBASED_CTLS", Ia32VmxTrueProcbasedCtls, ReadOnly, UINT64_C(0xfff9fffe04006172), 0, 0), /* value=0xfff9fffe`04006172 */
    MFX(0x0000048f, "IA32_VMX_TRUE_EXIT_CTLS", Ia32VmxTrueExitCtls, ReadOnly, UINT64_C(0x7fffff00036dfb), 0, 0), /* value=0x7fffff`00036dfb */
    MFX(0x00000490, "IA32_VMX_TRUE_ENTRY_CTLS", Ia32VmxTrueEntryCtls, ReadOnly, UINT64_C(0xffff000011fb), 0, 0), /* value=0xffff`000011fb */
    MFX(0x00000491, "IA32_VMX_VMFUNC", Ia32VmxVmFunc, ReadOnly, 0x1, 0, 0), /* value=0x1 */
    RSN(0x000004c1, 0x000004c2, "IA32_A_PMCn", Ia32PmcN, Ia32PmcN, 0x0, 0, UINT64_C(0xffffff0000000000)),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0x0 */
    MFX(0x00000601, "I7_SB_MSR_VR_CURRENT_CONFIG", IntelI7SandyVrCurrentConfig, IntelI7SandyVrCurrentConfig, 0, UINT64_C(0xc00000007fffe000), 0), /* value=0x0 */
    MFX(0x00000606, "I7_SB_MSR_RAPL_POWER_UNIT", IntelI7SandyRaplPowerUnit, IntelI7SandyRaplPowerUnit, 0x505, 0, UINT64_C(0xfffffffffff0e0f0)), /* value=0x505 */
    MFN(0x0000060d, "I7_SB_MSR_PKG_C2_RESIDENCY", IntelI7SandyPkgC2Residency, IntelI7SandyPkgC2Residency), /* value=0x0 */
    MFX(0x00000610, "I7_SB_MSR_PKG_POWER_LIMIT", IntelI7RaplPkgPowerLimit, IntelI7RaplPkgPowerLimit, 0x3880fa, 0x8000, UINT64_C(0xff000000ff000000)), /* value=0x3880fa */
    MFX(0x00000611, "I7_SB_MSR_PKG_ENERGY_STATUS", IntelI7RaplPkgEnergyStatus, ReadOnly, 0x21823a, 0, 0), /* value=0x21823a */
    MFX(0x00000638, "I7_SB_MSR_PP0_POWER_LIMIT", IntelI7RaplPp0PowerLimit, IntelI7RaplPp0PowerLimit, 0x20000, 0, UINT64_C(0xffffffffff000000)), /* value=0x20000 */
    MFX(0x00000639, "I7_SB_MSR_PP0_ENERGY_STATUS", IntelI7RaplPp0EnergyStatus, ReadOnly, 0x792fa, 0, 0), /* value=0x792fa */
    MFO(0x00000660, "SILV_CORE_C1_RESIDENCY", IntelAtSilvCoreC1Recidency), /* value=0x22`70ff1790 */
    MVO(0x00000661, "SILV_UNK_0000_0661", 0),
    MVO(0x00000662, "SILV_UNK_0000_0662", 0),
    MVO(0x00000663, "SILV_UNK_0000_0663", 0),
    MVO(0x00000664, "SILV_UNK_0000_0664", 0),
    MVO(0x00000665, "SILV_UNK_0000_0665", 0),
    MVO(0x00000666, "SILV_UNK_0000_0666", 0),
    MVO(0x00000667, "SILV_UNK_0000_0667", 0x9),
    MVX(0x00000668, "SILV_UNK_0000_0668", 0x13130f0b, 0, ~(uint64_t)UINT32_MAX),
    MVX(0x00000669, "SILV_UNK_0000_0669", 0x1010f20, 0, ~(uint64_t)UINT32_MAX),
    MVO(0x0000066a, "SILV_UNK_0000_066a", 0x1a0602),
    MVO(0x0000066b, "SILV_UNK_0000_066b", 0x442323),
    MVO(0x0000066c, "SILV_UNK_0000_066c", 0x1f1f1f1f),
    MVO(0x0000066d, "SILV_UNK_0000_066d", 0x52525252),
    MVX(0x0000066e, "SILV_UNK_0000_066e", 0, 0, ~(uint64_t)UINT32_MAX),
    MVX(0x0000066f, "SILV_UNK_0000_066f", 0, 0, ~(uint64_t)UINT32_MAX),
    MVX(0x00000670, "SILV_UNK_0000_0670", 0, 0, ~(uint64_t)UINT32_MAX),
    MVX(0x00000671, "SILV_UNK_0000_0671", 0, 0, UINT64_C(0xffffffff000000c0)),
    MVX(0x00000672, "SILV_UNK_0000_0672", 0, 0, UINT64_C(0xffffffffc0000000)),
    MVX(0x00000673, "SILV_UNK_0000_0673", 0x205, 0, UINT64_C(0xffffffffffffc000)),
    MVX(0x00000674, "SILV_UNK_0000_0674", 0x4050006, 0, UINT64_C(0xfffffffff8000000)),
    MVX(0x00000675, "SILV_UNK_0000_0675", 0x27, 0x20, UINT64_C(0xffffffffffffffc0)),
    MVX(0x00000676, "SILV_UNK_0000_0676", 0, UINT64_C(0x7f7f7f7f00000000), UINT64_C(0x8080808080808080)),
    MVX(0x00000677, "SILV_UNK_0000_0677", 0, 0, ~(uint64_t)UINT32_MAX),
    MFI(0x000006e0, "IA32_TSC_DEADLINE", Ia32TscDeadline), /* value=0x0 */
    MVX(0x00000768, "SILV_UNK_0000_0768", 0, 0, UINT64_C(0xffffffffffff0060)),
    MVX(0x00000769, "SILV_UNK_0000_0769", 0, 0x6, UINT64_C(0xfffffffffffffff0)),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xfffff802`6e9de200 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0xfffff802`6e9ddf40 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x4700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0x9b440000 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xffffd000`20661000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x7ff7`9b43e000 */
    MFX(0xc0000103, "AMD64_TSC_AUX", Amd64TscAux, Amd64TscAux, 0, 0, ~(uint64_t)UINT32_MAX), /* value=0x0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Pentium(R) CPU  N3530  @ 2.16GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Pentium_N3530_2_16GHz =
{
    /*.pszName          = */ "Intel Pentium N3530 2.16GHz",
    /*.pszFullName      = */ "Intel(R) Pentium(R) CPU  N3530  @ 2.16GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 55,
    /*.uStepping        = */ 8,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Atom_Silvermont,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_267MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 36,
    /*.fMxCsrMask       = */ 0xffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Pentium_N3530_2_16GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Pentium_N3530_2_16GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX,
    /*.DefUnknownCpuId  = */ { 0x00000001, 0x00000001, 0x00000100, 0x00000004 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Pentium_N3530_2_16GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Pentium_N3530_2_16GHz),
};

#endif /* !VBOX_CPUDB_Intel_Pentium_N3530_2_16GHz_h */

