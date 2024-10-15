/* $Id: Intel_Core2_X6800_2_93GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Core2 X6800 2.93GHz".
 * Generated at 2017-11-04T22:32:41Z by VBoxCpuReport v5.2.1r118907 on linux.amd64.
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

#ifndef VBOX_CPUDB_Intel_Core2_X6800_2_93GHz_h
#define VBOX_CPUDB_Intel_Core2_X6800_2_93GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Core(TM)2 CPU         X6800  @ 2.93GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Core2_X6800_2_93GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000a, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000006f6, 0x00020800, 0x0000e3bd, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x05b0b101, 0x005657f0, 0x00000000, 0x2cb43049, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x04000121, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x04000122, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x04004143, 0x03c0003f, 0x00000fff, 0x00000001, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00000020, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000001, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000400, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07280202, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x20100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x65746e49, 0x2952286c, 0x726f4320, 0x4d542865, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x43203229, 0x20205550, 0x20202020, 0x58202020, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x30303836, 0x20402020, 0x33392e32, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10008040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003024, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Core(TM)2 CPU         X6800  @ 2.93GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Core2_X6800_2_93GHz[] =
{
    MFX(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr, Ia32P5McAddr, 0, UINT64_C(0xfffffffffffbffff), 0), /* value=0x1398780 */
    MFI(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType), /* value=0x0 */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* Villain? value=0x11d1`f468a982 */
    MFX(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT32_C(0x88040b27), 0, 0), /* value=0x88040b27 */
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00900), 0x600, UINT64_C(0xfffffff0000000ff)),
    MVX(0x00000021, "C2_UNK_0000_0021", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0xffffffe0)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, IntelEblCrPowerOn, 0x41880000, UINT64_C(0xffffffffdff7ffbe), 0), /* value=0x41880000 */
    MVI(0x0000002f, "P6_UNK_0000_002f", 0),
    MVX(0x00000032, "P6_UNK_0000_0032", 0, UINT64_C(0xffffffff01fe0000), 0),
    MVX(0x00000033, "TEST_CTL", 0, UINT64_C(0xffffffff7fffffff), 0),
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    MVO(0x0000003f, "P6_UNK_0000_003f", 0xea),
    RFN(0x00000040, 0x00000043, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchToN, IntelLastBranchToN),
    MVX(0x0000004a, "P6_UNK_0000_004a", 0, UINT64_C(0xffffff0000000000), 0), /* value=0x0 */
    MVX(0x0000004b, "P6_UNK_0000_004b", 0, UINT64_C(0xffffff0000000000), 0), /* value=0x0 */
    MVX(0x0000004c, "P6_UNK_0000_004c", 0, UINT64_C(0xffffff0000000000), 0), /* value=0x0 */
    MVX(0x0000004d, "P6_UNK_0000_004d", 0, 0, 0), /* value=0xf53ed6ff`f9f9e16e */
    MVX(0x0000004e, "P6_UNK_0000_004e", 0, 0, 0), /* value=0xf7ffbdfb`bfbfabeb */
    MVX(0x0000004f, "P6_UNK_0000_004f", 0, UINT64_C(0xffffffffffffff00), 0), /* value=0xff */
    RFN(0x00000060, 0x00000063, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    MVX(0x0000006c, "P6_UNK_0000_006c", 0, UINT64_C(0xffffffff00000080), 0),
    MVX(0x0000006d, "P6_UNK_0000_006d", 0, UINT64_C(0xffffffff00000080), 0),
    MVX(0x0000006e, "P6_UNK_0000_006e", 0, UINT64_C(0xffffffff00000080), 0),
    MVO(0x0000006f, "P6_UNK_0000_006f", 0xadb),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MFX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", Ia32BiosSignId, Ia32BiosSignId, 0, UINT32_MAX, 0), /* value=0xc6`00000000 */
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    MFX(0x000000a8, "C2_EMTTM_CR_TABLES_0", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x61b, UINT64_MAX, 0), /* value=0x61b */
    MFX(0x000000a9, "C2_EMTTM_CR_TABLES_1", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x61b, UINT64_MAX, 0), /* value=0x61b */
    MFX(0x000000aa, "C2_EMTTM_CR_TABLES_2", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x61b, UINT64_MAX, 0), /* value=0x61b */
    MFX(0x000000ab, "C2_EMTTM_CR_TABLES_3", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x61b, UINT64_MAX, 0), /* value=0x61b */
    MFX(0x000000ac, "C2_EMTTM_CR_TABLES_4", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x61b, UINT64_MAX, 0), /* value=0x61b */
    MFX(0x000000ad, "C2_EMTTM_CR_TABLES_5", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, UINT32_C(0x8000061b), UINT64_MAX, 0), /* value=0x8000061b */
    RSN(0x000000c1, 0x000000c2, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x000000c7, "P6_UNK_0000_00c7", UINT64_C(0x3200000058000000)),
    MFX(0x000000cd, "MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0x800, 0, 0), /* value=0x800 */
    MVO(0x000000ce, "P6_UNK_0000_00ce", UINT64_C(0x1b0b277f7f071b)),
    MVO(0x000000e0, "C2_UNK_0000_00e0", 0x7820f0),
    MVO(0x000000e1, "C2_UNK_0000_00e1", UINT32_C(0xf0f00000)),
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, UINT64_C(0xffffffff0000ffff), UINT32_C(0xff000000)), /* value=0x26b204 */
    MFX(0x000000e3, "C2_SMM_CST_MISC_INFO", IntelCore2SmmCStMiscInfo, IntelCore2SmmCStMiscInfo, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, ~(uint64_t)UINT32_MAX, UINT32_C(0xff800000)), /* value=0x0 */
    MVO(0x000000e5, "C2_UNK_0000_00e5", UINT32_C(0xd00201c8)),
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0xa0`16e07631 */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x7e`79c4e805 */
    MFX(0x000000ee, "C1_EXT_CONFIG", IntelCore1ExtConfig, IntelCore1ExtConfig, 0, UINT64_C(0xffffffffefc5ffff), 0), /* value=0xa8000000`c17d4300 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MVX(0x00000116, "BBL_CR_ADDR", 0, UINT64_C(0xffffff000000001f), 0),
    MVX(0x00000118, "BBL_CR_DECC", 0xffdfe, UINT64_C(0xfffffffffff00000), 0),
    MFN(0x0000011a, "BBL_CR_TRIG", WriteOnly, IgnoreWrite),
    MVI(0x0000011b, "P6_UNK_0000_011b", 0),
    MVX(0x0000011c, "C2_UNK_0000_011c", UINT32_C(0xe003cf6f), UINT64_C(0xffffffff07f80000), 0),
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, IntelBblCrCtl3, UINT32_C(0xbf702109), UINT64_C(0xfffffffffff3fe9f), 0), /* value=0xbf702109 */
    MVX(0x0000014a, "TODO_0000_014a", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x0000014b, "TODO_0000_014b", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x0000014c, "TODO_0000_014c", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x0000014e, "P6_UNK_0000_014e", 0x7ab9f777, UINT64_C(0xffffffff00000080), 0),
    MVI(0x0000014f, "P6_UNK_0000_014f", 0xf000),
    MVX(0x00000151, "P6_UNK_0000_0151", 0x42100400, ~(uint64_t)UINT32_MAX, 0),
    MFX(0x0000015f, "C1_DTS_CAL_CTRL", IntelCore1DtsCalControl, IntelCore1DtsCalControl, 0, UINT64_C(0xffffffffffc0ffff), 0), /* value=0x230820 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x10 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0x0 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0xffffffff`81846c20 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x6, 0, 0), /* value=0x6 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    RSN(0x00000186, 0x00000187, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MVO(0x00000193, "C2_UNK_0000_0193", 0),
    MFX(0x00000194, "CLOCK_FLEX_MAX", IntelFlexRatio, IntelFlexRatio, 0, UINT64_C(0xfffffffffffee0c0), 0), /* value=0x0 */
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0xb270b2786320620), 0, 0), /* value=0xb270b27`86320620 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x820, 0, 0), /* Might bite. value=0x820 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0x2, UINT64_C(0xffffffffffffffe1), 0), /* value=0x2 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0x3, UINT64_C(0xffffffff00010100), UINT32_C(0xff0000e0)), /* value=0x3 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, UINT32_C(0x881c0000), UINT64_C(0xfffffffff87f017f), 0x780fc00), /* value=0x881c0000 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0x61b, 0, 0), /* value=0x61b */
    MVX(0x0000019e, "P6_UNK_0000_019e", 0x6930000, UINT64_C(0xffffffffffff0000), 0),
    MVI(0x0000019f, "P6_UNK_0000_019f", 0),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, UINT64_C(0x4062972489), UINT64_C(0x52603199f6), 0), /* value=0x40`62972489 */
    MVX(0x000001a1, "P6_UNK_0000_01a1", 0, UINT64_C(0xffff000000000000), 0),
    MFX(0x000001a2, "I7_MSR_TEMPERATURE_TARGET", IntelI7TemperatureTarget, ReadOnly, 0x1000, 0, 0), /* value=0x1000 */
    MVX(0x000001aa, "P6_PIC_SENS_CFG", 0x7e1f042f, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x000001bf, "C2_UNK_0000_01bf", 0x404, UINT64_C(0xffffffffffff0000), 0),
    MFX(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x3 */
    MVX(0x000001d3, "P6_UNK_0000_01d3", 0x8000, UINT64_C(0xffffffffffff7fff), 0),
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffffe03c)), /* value=0x1 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0xffffffff`8142d5f6 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xffffffff`810644e0 */
    MFN(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp, P6LastIntFromIp), /* value=0xffffffff`81039669 */
    MFN(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp, P6LastIntToIp), /* value=0xffffffff`81039020 */
    MVO(0x000001e0, "MSR_ROB_CR_BKUPTMPDR6", 0xff0),
    MFX(0x000001f8, "IA32_PLATFORM_DCA_CAP", Ia32PlatformDcaCap, Ia32PlatformDcaCap, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x0 */
    MFO(0x000001f9, "IA32_CPU_DCA_CAP", Ia32CpuDcaCap), /* value=0x0 */
    MFX(0x000001fa, "IA32_DCA_0_CAP", Ia32Dca0Cap, Ia32Dca0Cap, 0, UINT64_C(0xfffffffffefe17ff), 0), /* value=0xc01e488 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`80000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x80000006 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`c0000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xc0000006 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`f0000800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xfffffff000000ff8)), /* value=0xcff00000 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`fff00800 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x1`00000006 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`e0000800 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x1`20000006 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xf`f0000800 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
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
    MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE", Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, UINT64_C(0xfffffffffffff3f8)),
    MFX(0x00000309, "IA32_FIXED_CTR0", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, UINT64_C(0xffffff0000000000), 0), /* value=0xc4e */
    MFX(0x0000030a, "IA32_FIXED_CTR1", Ia32FixedCtrN, Ia32FixedCtrN, 0x1, UINT64_C(0xffffff0000000c00), 0), /* value=0xff`9dd0e550 */
    MFX(0x0000030b, "IA32_FIXED_CTR2", Ia32FixedCtrN, Ia32FixedCtrN, 0x2, UINT64_C(0xffffff0000000000), 0), /* value=0x205b */
    MFX(0x00000345, "IA32_PERF_CAPABILITIES", Ia32PerfCapabilities, ReadOnly, 0x82, 0, 0), /* value=0x82 */
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, UINT64_C(0xfffffffffffff000), 0), /* value=0xf0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFN(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl), /* value=0x7`00000003 */
    MFI(0x00000390, "IA32_PERF_GLOBAL_OVF_CTRL", Ia32PerfGlobalOvfCtrl), /* value=0x7`00000007 */
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x0 */
    RFN(0x00000400, 0x00000417, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MVX(0x00000478, "TODO_0000_0478", 0, 0, 0),
    MFX(0x00000480, "IA32_VMX_BASIC", Ia32VmxBasic, ReadOnly, UINT64_C(0x1a040000000007), 0, 0), /* value=0x1a0400`00000007 */
    MFX(0x00000481, "IA32_VMX_PINBASED_CTLS", Ia32VmxPinbasedCtls, ReadOnly, UINT64_C(0x1f00000016), 0, 0), /* value=0x1f`00000016 */
    MFX(0x00000482, "IA32_VMX_PROCBASED_CTLS", Ia32VmxProcbasedCtls, ReadOnly, UINT64_C(0x77b9fffe0401e172), 0, 0), /* value=0x77b9fffe`0401e172 */
    MFX(0x00000483, "IA32_VMX_EXIT_CTLS", Ia32VmxExitCtls, ReadOnly, UINT64_C(0x3efff00036dff), 0, 0), /* value=0x3efff`00036dff */
    MFX(0x00000484, "IA32_VMX_ENTRY_CTLS", Ia32VmxEntryCtls, ReadOnly, UINT64_C(0x1fff000011ff), 0, 0), /* value=0x1fff`000011ff */
    MFX(0x00000485, "IA32_VMX_MISC", Ia32VmxMisc, ReadOnly, 0x403c0, 0, 0), /* value=0x403c0 */
    MFX(0x00000486, "IA32_VMX_CR0_FIXED0", Ia32VmxCr0Fixed0, ReadOnly, UINT32_C(0x80000021), 0, 0), /* value=0x80000021 */
    MFX(0x00000487, "IA32_VMX_CR0_FIXED1", Ia32VmxCr0Fixed1, ReadOnly, UINT32_MAX, 0, 0), /* value=0xffffffff */
    MFX(0x00000488, "IA32_VMX_CR4_FIXED0", Ia32VmxCr4Fixed0, ReadOnly, 0x2000, 0, 0), /* value=0x2000 */
    MFX(0x00000489, "IA32_VMX_CR4_FIXED1", Ia32VmxCr4Fixed1, ReadOnly, 0x27ff, 0, 0), /* value=0x27ff */
    MFX(0x0000048a, "IA32_VMX_VMCS_ENUM", Ia32VmxVmcsEnum, ReadOnly, 0x2c, 0, 0), /* value=0x2c */
    MVX(0x000004f8, "C2_UNK_0000_04f8", UINT64_C(0xf5d5fc5e567f6a8e), 0, 0),
    MVX(0x000004f9, "C2_UNK_0000_04f9", UINT64_C(0xb595ed5afff3a8ff), 0, 0),
    MVX(0x000004fa, "C2_UNK_0000_04fa", UINT64_C(0xfddfae7f5bfb7c47), 0, 0),
    MVX(0x000004fb, "C2_UNK_0000_04fb", UINT64_C(0xf7ffbc5f93fd6fde), 0, 0),
    MVX(0x000004fc, "C2_UNK_0000_04fc", UINT64_C(0xb7c5c95891fb71c6), 0, 0),
    MVX(0x000004fd, "C2_UNK_0000_04fd", UINT64_C(0xb5d5cc5c95799df6), 0, 0),
    MVX(0x000004fe, "C2_UNK_0000_04fe", UINT64_C(0xba95c85ad1fb3973), 0, 0),
    MVX(0x000004ff, "C2_UNK_0000_04ff", UINT64_C(0xf5bdda4f9aff3943), 0, 0),
    MVX(0x00000590, "C2_UNK_0000_0590", 0, 0, 0),
    MVX(0x00000591, "C2_UNK_0000_0591", 0, ~(uint64_t)UINT32_MAX, 0),
    MFN(0x000005a0, "C2_PECI_CTL", IntelCore2PeciControl, IntelCore2PeciControl), /* Might bite. value=0x1 */
    MVI(0x000005a1, "C2_UNK_0000_05a1", 0), /* Might bite. */
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0xffff8801`2aaeba00 */
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* Might bite. value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* Might bite. value=0xffffffff`81844650 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* Might bite. value=0xffffffff`81846c90 */
    /** @todo r=bird: This seems wrong, all others CPUs \#GP if any of thie high
     *        bits are set. */
    MFN(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask), /* Might bite. value=0x47700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* Might bite. value=0x7fb5`e58d0740 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* Might bite. value=0xffff8801`2fc00000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* Might bite. value=0x0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Core(TM)2 CPU         X6800  @ 2.93GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Core2_X6800_2_93GHz =
{
    /*.pszName          = */ "Intel Core2 X6800 2.93GHz",
    /*.pszFullName      = */ "Intel(R) Core(TM)2 CPU         X6800  @ 2.93GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 15,
    /*.uStepping        = */ 6,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Core2_Merom,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_267MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 36,
    /*.fMxCsrMask       = */ 0x0000ffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Core2_X6800_2_93GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Core2_X6800_2_93GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /*.DefUnknownCpuId  = */ { 0x07280202, 0x00000000, 0x00000000, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Core2_X6800_2_93GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Core2_X6800_2_93GHz),
};

#endif /* !VBOX_CPUDB_Intel_Core2_X6800_2_93GHz_h */

