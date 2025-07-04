/* $Id: ZHAOXIN_KaiXian_KX_U5581_1_8GHz.h 109259 2025-04-16 20:59:36Z knut.osmundsen@oracle.com $ */
/** @file
 * CPU database entry "ZHAOXIN KaiXian KX-U5581 1.8GHz"
 * Generated at 2019-01-15T08:37:25Z by VBoxCpuReport v5.2.22r126460 on linux.amd64.
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


#ifndef VBOX_CPUDB_ZHAOXIN_KaiXian_KX_U5581_1_8GHz_h
#define VBOX_CPUDB_ZHAOXIN_KaiXian_KX_U5581_1_8GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for ZHAOXIN KaiXian KX-U5581@1.8GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_ZHAOXIN_KaiXian_KX_U5581_1_8GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000d, 0x68532020, 0x20206961, 0x68676e61, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000107b5, 0x07080800, 0x7eda63eb, 0xbfcbfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x635af001, 0x00000000, 0x00000000, 0x000000ff, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x1c000121, 0x01c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x1c000122, 0x01c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x1c00c143, 0x03c0003f, 0x00000fff, 0x00000003, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00022220, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000007, 0x00000000, UINT32_MAX, 0x00000000, 0x000c258b, 0x00000000, 0x24000000, 0 },
    { 0x00000007, 0x00000001, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07300402, 0x00000000, 0x00000000, 0x00000603, 0 },
    { 0x0000000b, 0x00000000, UINT32_MAX, 0x00000000, 0x00000001, 0x00000100, 0x00000007, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000b, 0x00000001, UINT32_MAX, 0x00000003, 0x00000008, 0x00000201, 0x00000007, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000b, 0x00000002, UINT32_MAX, 0x00000000, 0x00000000, 0x00000002, 0x00000007, 0 | CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
    { 0x0000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000000, UINT32_MAX, 0x00000007, 0x00000340, 0x00000340, 0x00000000, 0 },
    { 0x0000000d, 0x00000001, UINT32_MAX, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000002, UINT32_MAX, 0x00000100, 0x00000240, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000121, 0x2c100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x20202020, 0x20202020, 0x20202020, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x4f41485a, 0x204e4958, 0x5869614b, 0x206e6169, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x552d584b, 0x31383535, 0x382e3140, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x04200420, 0x06600660, 0x20080140, 0x20080140, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10008140, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000100, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003028, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0xc0000000, 0x00000000, 0x00000000, 0xc0000004, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0xc0000001, 0x00000000, 0x00000000, 0x000107b5, 0x00000000, 0x00000000, 0x1ec33dfc, 0 },
    { 0xc0000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0xc0000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0xc0000004, 0x00000000, 0x00000000, 0x00000025, 0x18002463, 0x18502461, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for ZHAOXIN KaiXian KX-U5581@1.8GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_ZHAOXIN_KaiXian_KX_U5581_1_8GHz[] =
{
    RVI(0x00000000, 0x00000005, "ZERO_0000_0000_THRU_0000_0005", 0),
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, UINT64_C(0xffffffffffff0000), 0), /* value=0x40 */
    RVI(0x00000007, 0x0000000f, "ZERO_0000_0007_THRU_0000_000f", 0),
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x965`912e15ac */
    RVI(0x00000011, 0x0000001a, "ZERO_0000_0011_THRU_0000_001a", 0),
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0x600, UINT64_C(0xfffffff0000000ff)),
    RVI(0x0000001c, 0x00000029, "ZERO_0000_001c_THRU_0000_0029", 0),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, IntelEblCrPowerOn, 0x2580000, UINT64_MAX, 0), /* value=0x2580000 */
    RVI(0x0000002b, 0x00000039, "ZERO_0000_002b_THRU_0000_0039", 0),
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    RVI(0x0000003b, 0x00000078, "ZERO_0000_003b_THRU_0000_0078", 0),
    RVI(0x0000007a, 0x0000008a, "ZERO_0000_007a_THRU_0000_008a", 0),
    MFN(0x0000008b, "BBL_CR_D3|BIOS_SIGN", Ia32BiosSignId, Ia32BiosSignId), /* value=0xc`00000000 */
    RVI(0x0000008c, 0x0000009a, "ZERO_0000_008c_THRU_0000_009a", 0),
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    RVI(0x0000009c, 0x000000c0, "ZERO_0000_009c_THRU_0000_00c0", 0),
    RSN(0x000000c1, 0x000000c3, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, UINT64_C(0xffffff0000000000), 0), /* XXX: The range ended earlier than expected! */
    RVI(0x000000c4, 0x000000cc, "ZERO_0000_00c4_THRU_0000_00cc", 0),
    MFX(0x000000cd, "MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0, 0, 0),
    RVI(0x000000ce, 0x000000e1, "ZERO_0000_00ce_THRU_0000_00e1", 0),
    MFI(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl), /* value=0x6a204 */
    MFX(0x000000e3, "C2_SMM_CST_MISC_INFO", IntelCore2SmmCStMiscInfo, IntelCore2SmmCStMiscInfo, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    RVI(0x000000e5, 0x000000e6, "ZERO_0000_00e5_THRU_0000_00e6", 0),
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0x2f4 */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x2f2 */
    RVI(0x000000e9, 0x000000fd, "ZERO_0000_00e9_THRU_0000_00fd", 0),
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0xd08, 0, 0), /* value=0xd08 */
    RVI(0x000000ff, 0x0000011d, "ZERO_0000_00ff_THRU_0000_011d", 0),
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, IntelBblCrCtl3, 0, UINT64_MAX, 0), /* value=0x0 */
    RVI(0x0000011f, 0x00000173, "ZERO_0000_011f_THRU_0000_0173", 0),
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x10 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0x0 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0xffffffff`8166bfa0 */
    RVI(0x00000177, 0x00000178, "ZERO_0000_0177_THRU_0000_0178", 0),
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, UINT64_C(0xfffffffffffffff8), 0), /* value=0x0 */
    RVI(0x0000017b, 0x00000185, "ZERO_0000_017b_THRU_0000_0185", 0),
    RSN(0x00000186, 0x00000188, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, UINT64_C(0xfffffffff8280000), 0), /* XXX: The range ended earlier than expected! */
    RVI(0x00000189, 0x00000197, "ZERO_0000_0189_THRU_0000_0197", 0),
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, Ia32PerfStatus, UINT64_C(0x853095408000955), UINT64_MAX, 0), /* value=0x8530954`08000955 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x954, 0, 0), /* Might bite. value=0x954 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0x2, UINT64_C(0xffffffffffffffe1), 0), /* value=0x2 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0, UINT64_C(0xffffffffff0000e0), 0), /* value=0x0 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, 0x8320000, UINT64_MAX, 0), /* value=0x8320000 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0x853, 0, 0), /* value=0x853 */
    RVI(0x0000019e, 0x0000019f, "ZERO_0000_019e_THRU_0000_019f", 0),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x173c89, UINT64_C(0xffffffb87939c176), 0), /* value=0x173c89 */
    RVI(0x000001a1, 0x000001d8, "ZERO_0000_01a1_THRU_0000_01d8", 0),
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffffe03c)), /* value=0x1 */
    RVI(0x000001da, 0x000001f1, "ZERO_0000_01da_THRU_0000_01f1", 0),
    MFO(0x000001f2, "IA32_SMRR_PHYSBASE", Ia32SmrrPhysBase), /* value=0x0 */
    MFO(0x000001f3, "IA32_SMRR_PHYSMASK", Ia32SmrrPhysMask), /* value=0x0 */
    RVI(0x000001f4, 0x000001ff, "ZERO_0000_01f4_THRU_0000_01ff", 0),
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`80000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x70000000 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`f0000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xd0000001 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`ff800800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    RVI(0x00000210, 0x0000024f, "ZERO_0000_0210_THRU_0000_024f", 0),
    MFS(0x00000250, "IA32_MTRR_FIX64K_00000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix64K_00000),
    RVI(0x00000251, 0x00000257, "ZERO_0000_0251_THRU_0000_0257", 0),
    MFS(0x00000258, "IA32_MTRR_FIX16K_80000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix16K_80000),
    MFS(0x00000259, "IA32_MTRR_FIX16K_A0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix16K_A0000),
    RVI(0x0000025a, 0x00000267, "ZERO_0000_025a_THRU_0000_0267", 0),
    MFS(0x00000268, "IA32_MTRR_FIX4K_C0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_C0000),
    MFS(0x00000269, "IA32_MTRR_FIX4K_C8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_C8000),
    MFS(0x0000026a, "IA32_MTRR_FIX4K_D0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_D0000),
    MFS(0x0000026b, "IA32_MTRR_FIX4K_D8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_D8000),
    MFS(0x0000026c, "IA32_MTRR_FIX4K_E0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_E0000),
    MFS(0x0000026d, "IA32_MTRR_FIX4K_E8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_E8000),
    MFS(0x0000026e, "IA32_MTRR_FIX4K_F0000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_F0000),
    MFS(0x0000026f, "IA32_MTRR_FIX4K_F8000", Ia32MtrrFixed, Ia32MtrrFixed, GuestMsrs.msr.MtrrFix4K_F8000),
    RVI(0x00000270, 0x00000276, "ZERO_0000_0270_THRU_0000_0276", 0),
    MFS(0x00000277, "IA32_PAT", Ia32Pat, Ia32Pat, Guest.msrPAT),
    RVI(0x00000278, 0x000002fe, "ZERO_0000_0278_THRU_0000_02fe", 0),
    MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE", Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, UINT64_C(0xfffffffffffff3f8)),
    RVI(0x00000300, 0x00000308, "ZERO_0000_0300_THRU_0000_0308", 0),
    RSN(0x00000309, 0x0000030a, "IA32_FIXED_CTRn", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, UINT64_C(0xffffff0000000000), 0),
    MFX(0x0000030b, "IA32_FIXED_CTR2", Ia32FixedCtrN, Ia32FixedCtrN, 0x2, UINT64_C(0xfffff8020a068061), 0), /* value=0x2d4 */
    RVI(0x0000030c, 0x0000038c, "ZERO_0000_030c_THRU_0000_038c", 0),
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, UINT64_C(0xfffffffffffff444), 0), /* value=0x0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFN(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl), /* value=0xffffffff`ffffffff */
    RVI(0x00000390, 0x0000047f, "ZERO_0000_0390_THRU_0000_047f", 0),
    MFX(0x00000480, "IA32_VMX_BASIC", Ia32VmxBasic, ReadOnly, UINT64_C(0x1a040000000007), 0, 0), /* value=0x1a0400`00000007 */
    MFX(0x00000481, "IA32_VMX_PINBASED_CTLS", Ia32VmxPinbasedCtls, ReadOnly, UINT64_C(0x3f00000016), 0, 0), /* value=0x3f`00000016 */
    MFX(0x00000482, "IA32_VMX_PROCBASED_CTLS", Ia32VmxProcbasedCtls, ReadOnly, UINT64_C(0x77f9fffe0401e172), 0, 0), /* value=0x77f9fffe`0401e172 */
    MFX(0x00000483, "IA32_VMX_EXIT_CTLS", Ia32VmxExitCtls, ReadOnly, UINT64_C(0x3efff00036dff), 0, 0), /* value=0x3efff`00036dff */
    MFX(0x00000484, "IA32_VMX_ENTRY_CTLS", Ia32VmxEntryCtls, ReadOnly, UINT64_C(0x1fff000011ff), 0, 0), /* value=0x1fff`000011ff */
    MFX(0x00000485, "IA32_VMX_MISC", Ia32VmxMisc, ReadOnly, 0x403c0, 0, 0), /* value=0x403c0 */
    MFX(0x00000486, "IA32_VMX_CR0_FIXED0", Ia32VmxCr0Fixed0, ReadOnly, UINT32_C(0x80000021), 0, 0), /* value=0x80000021 */
    MFX(0x00000487, "IA32_VMX_CR0_FIXED1", Ia32VmxCr0Fixed1, ReadOnly, UINT32_MAX, 0, 0), /* value=0xffffffff */
    MFX(0x00000488, "IA32_VMX_CR4_FIXED0", Ia32VmxCr4Fixed0, ReadOnly, 0x2000, 0, 0), /* value=0x2000 */
    MFX(0x00000489, "IA32_VMX_CR4_FIXED1", Ia32VmxCr4Fixed1, ReadOnly, 0x27ff, 0, 0), /* value=0x27ff */
    MFX(0x0000048a, "IA32_VMX_VMCS_ENUM", Ia32VmxVmcsEnum, ReadOnly, 0x2c, 0, 0), /* value=0x2c */
    RVI(0x0000048b, 0x000005ff, "ZERO_0000_048b_THRU_0000_05ff", 0),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0x0 */
    RVI(0x00000601, 0x00001106, "ZERO_0000_0601_THRU_0000_1106", 0),
    MVI(0x00001107, "VIA_UNK_0000_1107", 0x2),
    RVI(0x00001108, 0x0000110e, "ZERO_0000_1108_THRU_0000_110e", 0),
    MVI(0x0000110f, "VIA_UNK_0000_110f", 0x2),
    RVI(0x00001110, 0x00001152, "ZERO_0000_1110_THRU_0000_1152", 0),
    MVO(0x00001153, "VIA_UNK_0000_1153", 0),
    RVI(0x00001154, 0x000011ff, "ZERO_0000_1154_THRU_0000_11ff", 0),
    MVX(0x00001200, "VIA_UNK_0000_1200", UINT64_C(0x8863a9bfc9fbff), 0x40000, 0),
    MVX(0x00001201, "VIA_UNK_0000_1201", UINT64_C(0x120100800), UINT64_C(0xfffffff000000000), 0),
    MVX(0x00001202, "VIA_UNK_0000_1202", 0x3dcc, UINT64_C(0xffffffffffffc233), 0),
    MVX(0x00001203, "VIA_UNK_0000_1203", 0x18, 0, 0),
    MVX(0x00001204, "VIA_UNK_0000_1204", UINT64_C(0x6fd00000424), 0, 0),
    MVX(0x00001205, "VIA_UNK_0000_1205", UINT64_C(0x9890000000001), 0, 0),
    MVX(0x00001206, "VIA_ALT_VENDOR_EBX", 0, 0, 0),
    MVX(0x00001207, "VIA_ALT_VENDOR_ECDX", 0, 0, 0),
    MVX(0x00001208, "VIA_UNK_0000_1208", 0, 0, 0),
    MVX(0x00001209, "VIA_UNK_0000_1209", 0, 0, 0),
    MVX(0x0000120a, "VIA_UNK_0000_120a", 0, 0, 0),
    MVX(0x0000120b, "VIA_UNK_0000_120b", 0, 0, 0),
    MVX(0x0000120c, "VIA_UNK_0000_120c", 0, 0, 0),
    MVX(0x0000120d, "VIA_UNK_0000_120d", 0, 0, 0),
    MVI(0x0000120e, "VIA_UNK_0000_120e", UINT64_C(0x820007b100002080)), /* Villain? */
    MVX(0x0000120f, "VIA_UNK_0000_120f", UINT64_C(0x200000001a000000), 0x18000000, 0),
    MVI(0x00001210, "ZERO_0000_1210", 0),
    MVX(0x00001211, "VIA_UNK_0000_1211", 0, 0, 0),
    MVX(0x00001212, "VIA_UNK_0000_1212", 0, 0, 0),
    MVX(0x00001213, "VIA_UNK_0000_1213", 0, ~(uint64_t)UINT32_MAX, 0),
    MVO(0x00001214, "VIA_UNK_0000_1214", UINT64_C(0x5dd89e10ffffffff)),
    RVI(0x00001215, 0x0000121f, "ZERO_0000_1215_THRU_0000_121f", 0),
    MVO(0x00001220, "VIA_UNK_0000_1220", 0),
    MVO(0x00001221, "VIA_UNK_0000_1221", 0x4dd2e713),
    RVI(0x00001222, 0x0000122f, "ZERO_0000_1222_THRU_0000_122f", 0),
    MVX(0x00001230, "VIA_UNK_0000_1230", UINT64_C(0x5dd89e10ffffffff), UINT32_C(0xfffffd68), 0),
    MVX(0x00001231, "VIA_UNK_0000_1231", UINT64_C(0x7f9110bdc740), 0x200, 0),
    MVO(0x00001232, "VIA_UNK_0000_1232", UINT64_C(0x2603448430479888)),
    MVI(0x00001233, "VIA_UNK_0000_1233", UINT64_C(0xb39acda158793c27)), /* Villain? */
    MVX(0x00001234, "VIA_UNK_0000_1234", 0, 0, 0),
    MVX(0x00001235, "VIA_UNK_0000_1235", 0, 0, 0),
    MVX(0x00001236, "VIA_UNK_0000_1236", UINT64_C(0x5dd89e10ffffffff), UINT32_C(0xfffffd68), 0),
    MVX(0x00001237, "VIA_UNK_0000_1237", UINT32_C(0xffc00026), UINT64_C(0xffffffff06000001), 0),
    MVO(0x00001238, "VIA_UNK_0000_1238", 0x2),
    MVI(0x00001239, "VIA_UNK_0000_1239", 0), /* Villain? */
    RVI(0x0000123a, 0x0000123f, "ZERO_0000_123a_THRU_0000_123f", 0),
    MVO(0x00001240, "VIA_UNK_0000_1240", 0),
    MVO(0x00001241, "VIA_UNK_0000_1241", UINT64_C(0x5dd89e10ffffffff)),
    MVI(0x00001242, "ZERO_0000_1242", 0),
    MVX(0x00001243, "VIA_UNK_0000_1243", 0, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x00001244, "ZERO_0000_1244", 0),
    MVX(0x00001245, "VIA_UNK_0000_1245", UINT64_C(0x3020400000000064), UINT64_C(0xf000000000000000), 0),
    MVX(0x00001246, "VIA_UNK_0000_1246", UINT64_C(0x10000000000), 0, 0),
    MVX(0x00001247, "VIA_UNK_0000_1247", 0, 0, 0),
    MVX(0x00001248, "VIA_UNK_0000_1248", 0, 0, 0),
    MVI(0x00001249, "VIA_UNK_0000_1249", 0), /* Villain? */
    MVI(0x0000124a, "VIA_UNK_0000_124a", 0), /* Villain? */
    RVI(0x0000124b, 0x00001300, "ZERO_0000_124b_THRU_0000_1300", 0),
    MVX(0x00001301, "VIA_UNK_0000_1301", 0, 0, 0),
    MVX(0x00001302, "VIA_UNK_0000_1302", 0, 0, 0),
    MVX(0x00001303, "VIA_UNK_0000_1303", 0, 0, 0),
    MVX(0x00001304, "VIA_UNK_0000_1304", 0, 0, 0),
    MVX(0x00001305, "VIA_UNK_0000_1305", 0, 0, 0),
    MVX(0x00001306, "VIA_UNK_0000_1306", 0, 0, 0),
    MVX(0x00001307, "VIA_UNK_0000_1307", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00001308, "VIA_UNK_0000_1308", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001309, "VIA_UNK_0000_1309", 0, ~(uint64_t)UINT32_MAX, 0),
    RVI(0x0000130a, 0x0000130c, "ZERO_0000_130a_THRU_0000_130c", 0),
    MVX(0x0000130d, "VIA_UNK_0000_130d", 0, UINT64_C(0xffffffffffff0000), 0),
    MVX(0x0000130e, "VIA_UNK_0000_130e", UINT64_MAX, 0, 0),
    RVI(0x0000130f, 0x00001311, "ZERO_0000_130f_THRU_0000_1311", 0),
    MVX(0x00001312, "VIA_UNK_0000_1312", 0, 0, 0),
    RVI(0x00001313, 0x00001314, "ZERO_0000_1313_THRU_0000_1314", 0),
    MVX(0x00001315, "VIA_UNK_0000_1315", 0, 0, 0),
    MVI(0x00001316, "ZERO_0000_1316", 0),
    MVX(0x00001317, "VIA_UNK_0000_1317", 0, 0, 0),
    MVX(0x00001318, "VIA_UNK_0000_1318", 0, 0, 0),
    MVI(0x00001319, "ZERO_0000_1319", 0),
    MVX(0x0000131a, "VIA_UNK_0000_131a", 0, 0, 0),
    MVX(0x0000131b, "VIA_UNK_0000_131b", 0x3c20954, 0, 0),
    RVI(0x0000131c, 0x00001401, "ZERO_0000_131c_THRU_0000_1401", 0),
    MVO(0x00001402, "VIA_UNK_0000_1402", 0x148c48),
    MVX(0x00001403, "VIA_UNK_0000_1403", 0, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x00001404, "VIA_UNK_0000_1404", 0), /* Villain? */
    MVI(0x00001405, "VIA_UNK_0000_1405", UINT32_C(0x80fffffc)), /* Villain? */
    MVX(0x00001406, "VIA_UNK_0000_1406", UINT32_C(0xc842c800), ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001407, "VIA_UNK_0000_1407", UINT32_C(0x880400c0), ~(uint64_t)UINT32_MAX, 0),
    RVI(0x00001408, 0x0000140f, "ZERO_0000_1408_THRU_0000_140f", 0),
    MVX(0x00001410, "VIA_UNK_0000_1410", 0xfa0, UINT64_C(0xfffffffffff00000), 0),
    MVX(0x00001411, "VIA_UNK_0000_1411", 0xa5a, UINT64_C(0xfffffffffff00000), 0),
    MVI(0x00001412, "VIA_UNK_0000_1412", 0x4090),
    MVI(0x00001413, "VIA_UNK_0000_1413", 0), /* Villain? */
    MVX(0x00001414, "VIA_UNK_0000_1414", 0x5a, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x00001415, "VIA_UNK_0000_1415", 0x5a, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x00001416, "VIA_UNK_0000_1416", 0x6e, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x00001417, "VIA_UNK_0000_1417", 0x32, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x00001418, "VIA_UNK_0000_1418", 0xa, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x00001419, "VIA_UNK_0000_1419", 0x14, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000141a, "VIA_UNK_0000_141a", 0x28, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000141b, "VIA_UNK_0000_141b", 0x3c, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000141c, "VIA_UNK_0000_141c", 0x69, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000141d, "VIA_UNK_0000_141d", 0x69, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000141e, "VIA_UNK_0000_141e", 0x69, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000141f, "VIA_UNK_0000_141f", 0x32, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x00001420, "VIA_UNK_0000_1420", 0x3, UINT64_C(0xffffffffffffc000), 0),
    MVX(0x00001421, "VIA_UNK_0000_1421", 0x1f8, UINT64_C(0xfffffffffffc0000), 0),
    MVX(0x00001422, "VIA_UNK_0000_1422", 0x1f4, UINT64_C(0xfffffffffffc0000), 0),
    MVI(0x00001423, "VIA_UNK_0000_1423", 0xfffb7),
    MVI(0x00001424, "VIA_UNK_0000_1424", 0x5b6),
    MVI(0x00001425, "VIA_UNK_0000_1425", 0x65508),
    MVI(0x00001426, "VIA_UNK_0000_1426", 0x843b),
    MVX(0x00001427, "VIA_UNK_0000_1427", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001428, "VIA_UNK_0000_1428", 0x1ffffff, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001429, "VIA_UNK_0000_1429", 0, UINT64_C(0xfffffffffff00000), 0),
    MVI(0x0000142a, "VIA_UNK_0000_142a", 0x1c85d),
    MVO(0x0000142b, "VIA_UNK_0000_142b", 0xf7e),
    MVI(0x0000142c, "VIA_UNK_0000_142c", 0x20080), /* Villain? */
    MVI(0x0000142d, "ZERO_0000_142d", 0),
    MVI(0x0000142e, "VIA_UNK_0000_142e", 0x8000000), /* Villain? */
    MVX(0x0000142f, "VIA_UNK_0000_142f", UINT64_C(0xffe57bea2ff3fdff), 0, 0),
    RVI(0x00001430, 0x00001433, "ZERO_0000_1430_THRU_0000_1433", 0),
    MVX(0x00001434, "VIA_UNK_0000_1434", 0x853f0e0, UINT64_C(0xffffffff7e7b0000), 0),
    MVI(0x00001435, "VIA_UNK_0000_1435", 0x8000838), /* Villain? */
    MVI(0x00001436, "VIA_UNK_0000_1436", 0x200004f), /* Villain? */
    MVX(0x00001437, "VIA_UNK_0000_1437", 0, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x00001438, "VIA_UNK_0000_1438", 0x7004801c), /* Villain? */
    MVI(0x00001439, "ZERO_0000_1439", 0),
    MVX(0x0000143a, "VIA_UNK_0000_143a", 0x20000, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x0000143b, "ZERO_0000_143b", 0),
    MVX(0x0000143c, "VIA_UNK_0000_143c", 0, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x0000143d, "VIA_UNK_0000_143d", 0, UINT64_C(0xfffffffffffffe00), 0),
    RVI(0x0000143e, 0x0000143f, "ZERO_0000_143e_THRU_0000_143f", 0),
    MVX(0x00001440, "VIA_UNK_0000_1440", UINT32_C(0x80e00954), ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001441, "VIA_UNK_0000_1441", 0xf00954, UINT64_C(0xffffffff00ff7f7f), 0),
    MVX(0x00001442, "VIA_UNK_0000_1442", 0xf00954, UINT64_C(0xffffffff00ff7f7f), 0),
    RVI(0x00001443, 0x00001448, "ZERO_0000_1443_THRU_0000_1448", 0),
    MVI(0x00001449, "VIA_UNK_0000_1449", UINT64_C(0xfffff7e247)),
    RVI(0x0000144a, 0x0000144f, "ZERO_0000_144a_THRU_0000_144f", 0),
    MVX(0x00001450, "VIA_UNK_0000_1450", 0, UINT64_C(0xffffffffffffe000), 0),
    MVX(0x00001451, "VIA_UNK_0000_1451", 0, UINT64_C(0xffffffffff000000), 0),
    MVX(0x00001452, "VIA_UNK_0000_1452", 0, UINT64_C(0xffffffffff000000), 0),
    MVI(0x00001453, "VIA_UNK_0000_1453", 0x3fffffff),
    RVI(0x00001454, 0x0000145f, "ZERO_0000_1454_THRU_0000_145f", 0),
    MVX(0x00001460, "VIA_UNK_0000_1460", 0, UINT64_C(0xffffffffffffffc0), 0),
    MVX(0x00001461, "VIA_UNK_0000_1461", 0x7b, UINT64_C(0xffffffffffffff00), 0),
    MVX(0x00001462, "VIA_UNK_0000_1462", 0x76, UINT64_C(0xffffffffffffff00), 0),
    MVI(0x00001463, "VIA_UNK_0000_1463", 0x4a),
    MVI(0x00001464, "ZERO_0000_1464", 0),
    MVI(0x00001465, "VIA_UNK_0000_1465", 0xc6),
    MVI(0x00001466, "VIA_UNK_0000_1466", UINT64_C(0x800000053)),
    RVI(0x00001467, 0x0000146f, "ZERO_0000_1467_THRU_0000_146f", 0),
    MVX(0x00001470, "VIA_UNK_0000_1470", UINT64_C(0x5dd89e10ffffffff), UINT32_C(0xfffffd68), 0),
    MVI(0x00001471, "VIA_UNK_0000_1471", 0x2a000000),
    RVI(0x00001472, 0x0000147f, "ZERO_0000_1472_THRU_0000_147f", 0),
    MVI(0x00001480, "VIA_UNK_0000_1480", 0x3907),
    MVI(0x00001481, "VIA_UNK_0000_1481", 0x12c0),
    MVI(0x00001482, "VIA_UNK_0000_1482", 0x320),
    MVI(0x00001483, "VIA_UNK_0000_1483", 0x3),
    MVI(0x00001484, "VIA_UNK_0000_1484", 0x1647),
    MVI(0x00001485, "VIA_UNK_0000_1485", 0x3b7),
    MVI(0x00001486, "VIA_UNK_0000_1486", 0x443),
    RVI(0x00001487, 0x0000148f, "ZERO_0000_1487_THRU_0000_148f", 0),
    MVX(0x00001490, "VIA_UNK_0000_1490", 0xf5, UINT64_C(0xffffffffffffc000), 0),
    MVX(0x00001491, "VIA_UNK_0000_1491", 0x200, UINT64_C(0xffffffffff000000), 0),
    MVX(0x00001492, "VIA_UNK_0000_1492", 0, UINT64_C(0xffffffffff000000), 0),
    MVX(0x00001493, "VIA_UNK_0000_1493", 0x4, UINT64_C(0xffffffffffff0000), 0),
    MVX(0x00001494, "VIA_UNK_0000_1494", 0x100, UINT64_C(0xffffffffffff0000), 0),
    MVX(0x00001495, "VIA_UNK_0000_1495", 0x100, UINT64_C(0xffffffffff000000), 0),
    MVX(0x00001496, "VIA_UNK_0000_1496", 0x8, UINT64_C(0xffffffffffff0000), 0),
    MVX(0x00001497, "VIA_UNK_0000_1497", 0, UINT64_C(0xffffffffff000000), 0),
    MVX(0x00001498, "VIA_UNK_0000_1498", 0xffffff, UINT64_C(0xfffffffffffffe3c), 0),
    MVI(0x00001499, "VIA_UNK_0000_1499", 0x2c5),
    MVI(0x0000149a, "VIA_UNK_0000_149a", 0x1c1),
    MVI(0x0000149b, "VIA_UNK_0000_149b", 0x2c5a),
    MVI(0x0000149c, "VIA_UNK_0000_149c", 0x1c8f),
    RVI(0x0000149d, 0x0000149e, "ZERO_0000_149d_THRU_0000_149e", 0),
    MVI(0x0000149f, "VIA_UNK_0000_149f", 0x1c9),
    RVI(0x000014a0, 0x00001522, "ZERO_0000_14a0_THRU_0000_1522", 0),
    MFN(0x00001523, "VIA_UNK_0000_1523", WriteOnly, IgnoreWrite),
    RVI(0x00001524, 0x00003179, "ZERO_0000_1524_THRU_0000_3179", 0),
    MVO(0x0000317a, "VIA_UNK_0000_317a", UINT64_C(0x139f29749595b8)),
    MVO(0x0000317b, "VIA_UNK_0000_317b", UINT64_C(0x5dd89e10ffffffff)),
    MVI(0x0000317c, "ZERO_0000_317c", 0),
    MFN(0x0000317d, "VIA_UNK_0000_317d", WriteOnly, IgnoreWrite),
    MFN(0x0000317e, "VIA_UNK_0000_317e", WriteOnly, IgnoreWrite),
    MVI(0x0000317f, "VIA_UNK_0000_317f", 0), /* Villain? */
    RVI(0x00003180, 0x00003fff, "ZERO_0000_3180_THRU_0000_3fff", 0),
    RVI(0x40000000, 0x40003fff, "ZERO_4000_0000_THRU_4000_3fff", 0),
    RVI(0x80000000, 0x80000197, "ZERO_8000_0000_THRU_8000_0197", 0),
    RVI(0x80000199, 0x80003fff, "ZERO_8000_0199_THRU_8000_3fff", 0),
    RVI(0xc0000000, 0xc000007f, "ZERO_c000_0000_THRU_c000_007f", 0),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xffffffffffffd2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xffffffff`81669af0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0xffffffff`8166c1d0 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x3700 */
    RVI(0xc0000085, 0xc00000ff, "ZERO_c000_0085_THRU_c000_00ff", 0),
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0x7f91`10bdc740 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xffff8800`6fd80000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x0 */
    RVI(0xc0000104, 0xc0003fff, "ZERO_c000_0104_THRU_c000_3fff", 0),
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for ZHAOXIN KaiXian KX-U5581@1.8GHz.
 */
static CPUMDBENTRYX86 const g_Entry_ZHAOXIN_KaiXian_KX_U5581_1_8GHz =
{
    {
        /*.pszName      = */ "ZHAOXIN KaiXian KX-U5581 1.8GHz",
        /*.pszFullName  = */ "ZHAOXIN KaiXian KX-U5581@1.8GHz",
        /*.enmVendor    = */ CPUMCPUVENDOR_SHANGHAI,
        /*.enmMicroarch = */ kCpumMicroarch_Shanghai_Wudaokou,
        /*.fFlags       = */ 0,
        /*.enmEntryType = */ CPUMDBENTRYTYPE_X86,
    },
    /*.uFamily          = */ 7,
    /*.uModel           = */ 11,
    /*.uStepping        = */ 5,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_UNKNOWN,
    /*.cMaxPhysAddrWidth= */ 40,
    /*.fMxCsrMask       = */ 0x0000ffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_ZHAOXIN_KaiXian_KX_U5581_1_8GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_ZHAOXIN_KaiXian_KX_U5581_1_8GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_DEFAULTS,
    /*.DefUnknownCpuId  = */ { 0x00000000, 0x00000000, 0x00000000, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX /** @todo */,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_ZHAOXIN_KaiXian_KX_U5581_1_8GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_ZHAOXIN_KaiXian_KX_U5581_1_8GHz),
};

#endif /* !VBOX_CPUDB_ZHAOXIN_KaiXian_KX_U5581_1_8GHz_h */

