/* $Id: Intel_Xeon_X5482_3_20GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Xeon X5482 3.20GHz".
 * Generated at 2013-12-16T12:10:52Z by VBoxCpuReport v4.3.53r91299 on darwin.amd64.
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

#ifndef VBOX_CPUDB_Intel_Xeon_X5482_3_20GHz_h
#define VBOX_CPUDB_Intel_Xeon_X5482_3_20GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Xeon(R) CPU           X5482  @ 3.20GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Xeon_X5482_3_20GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000a, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x00010676, 0x04040800, 0x000ce3bd, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x05b0b101, 0x005657f0, 0x00000000, 0x2cb4304e, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x0c000121, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x0c000122, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x0c004143, 0x05c0003f, 0x00000fff, 0x00000001, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00002220, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000001, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000400, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07280202, 0x00000000, 0x00000000, 0x00000503, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x20100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x65746e49, 0x2952286c, 0x6f655820, 0x2952286e, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x55504320, 0x20202020, 0x20202020, 0x58202020, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x32383435, 0x20402020, 0x30322e33, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x18008040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003026, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Xeon(R) CPU           X5482  @ 3.20GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Xeon_X5482_3_20GHz[] =
{
    MFO(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr), /* value=0x610010 */
    MFX(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType, Ia32P5McType, 0, 0, UINT64_MAX), /* value=0x0 */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, 0, UINT64_C(0xffffffffffff0000)), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x1358`d28c2c60 */
    MFV(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0x18000088e40822)),
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0, UINT64_C(0xffffffc0000006ff)),
    MVX(0x00000021, "C2_UNK_0000_0021", 0, 0, UINT64_C(0xffffffffffffffc0)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, IntelEblCrPowerOn, UINT32_C(0xc2383400), UINT64_C(0xffffffffdff7df00), 0), /* value=0xc2383400 */
    MVX(0x00000032, "P6_UNK_0000_0032", 0, UINT64_C(0xffffffff01fe0000), 0),
    MVX(0x00000033, "TEST_CTL", 0, UINT64_C(0xffffffff7fffffff), 0),
    MVO(0x00000039, "C2_UNK_0000_0039", 0x7),
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    MVO(0x0000003f, "P6_UNK_0000_003f", 0),
    RFN(0x00000040, 0x00000043, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchToN, IntelLastBranchToN),
    RFN(0x00000060, 0x00000063, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MVX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", UINT64_C(0x60b00000000), UINT32_MAX, 0),
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    MFX(0x000000a8, "C2_EMTTM_CR_TABLES_0", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x612, UINT64_C(0xffffffffffff8000), 0), /* value=0x612 */
    MFX(0x000000a9, "C2_EMTTM_CR_TABLES_1", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x612, UINT64_C(0xffffffffffff8000), 0), /* value=0x612 */
    MFX(0x000000aa, "C2_EMTTM_CR_TABLES_2", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x612, UINT64_C(0xffffffffffff8000), 0), /* value=0x612 */
    MFX(0x000000ab, "C2_EMTTM_CR_TABLES_3", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x612, UINT64_C(0xffffffffffff8000), 0), /* value=0x612 */
    MFX(0x000000ac, "C2_EMTTM_CR_TABLES_4", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x612, UINT64_C(0xffffffffffff8000), 0), /* value=0x612 */
    MFX(0x000000ad, "C2_EMTTM_CR_TABLES_5", IntelCore2EmttmCrTablesN, IntelCore2EmttmCrTablesN, 0x612, ~(uint64_t)UINT32_MAX, 0), /* value=0x612 */
    RSN(0x000000c1, 0x000000c2, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x000000c7, "P6_UNK_0000_00c7", UINT64_C(0x2300000052000000)),
    MFX(0x000000cd, "P6_MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0x806, 0, 0),
    MVO(0x000000ce, "P6_UNK_0000_00ce", UINT64_C(0x1208227f7f0710)),
    MVO(0x000000cf, "C2_UNK_0000_00cf", 0),
    MVO(0x000000e0, "C2_UNK_0000_00e0", 0x18820f0),
    MVO(0x000000e1, "C2_UNK_0000_00e1", UINT32_C(0xf0f00000)),
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, 0x404000, UINT64_C(0xfffffffffc001000)), /* value=0x202a01 */
    MFX(0x000000e3, "C2_SMM_CST_MISC_INFO", IntelCore2SmmCStMiscInfo, IntelCore2SmmCStMiscInfo, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, 0, UINT64_C(0xffffffffff800000)), /* value=0x0 */
    MVO(0x000000e5, "C2_UNK_0000_00e5", UINT32_C(0xd00208c8)),
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0x40`a0a41c60 */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x3a`cc470b98 */
    MFX(0x000000ee, "C1_EXT_CONFIG", IntelCore1ExtConfig, IntelCore1ExtConfig, 0, UINT64_C(0xffffffffefc5ffff), 0), /* value=0x4000000`877d4b01 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0xd08, 0, 0), /* value=0xd08 */
    MVX(0x00000116, "BBL_CR_ADDR", 0x3fc0, UINT64_C(0xffffff000000001f), 0),
    MVX(0x00000118, "BBL_CR_DECC", 0xa7f99, UINT64_C(0xfffffffffff00000), 0),
    MFN(0x0000011a, "BBL_CR_TRIG", WriteOnly, IgnoreWrite),
    MVI(0x0000011b, "P6_UNK_0000_011b", 0),
    MVX(0x0000011c, "C2_UNK_0000_011c", UINT32_C(0xe003b94d), UINT64_C(0xffffffff07c00000), 0),
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, IntelBblCrCtl3, UINT32_C(0xbe702111), UINT64_C(0xfffffffffef3fe9f), 0), /* value=0xbe702111 */
    MVX(0x0000014e, "P6_UNK_0000_014e", 0x70375245, UINT64_C(0xffffffff00000080), 0),
    MVI(0x0000014f, "P6_UNK_0000_014f", UINT32_C(0xffffba7f)),
    MVX(0x00000151, "P6_UNK_0000_0151", 0x6b929082, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x0000015e, "C2_UNK_0000_015e", 0x6, 0, UINT64_C(0xfffffffffffffff0)),
    MFX(0x0000015f, "C1_DTS_CAL_CTRL", IntelCore1DtsCalControl, IntelCore1DtsCalControl, 0, UINT64_C(0xffffffffffc0ffff), 0), /* value=0x822 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0xb */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0xffffff82`0dce9190 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0xffffff80`0d2ce720 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x806, 0, 0), /* value=0x806 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    RSN(0x00000186, 0x00000187, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, 0, UINT64_C(0xffffffff00200000)),
    MVO(0x00000193, "C2_UNK_0000_0193", 0),
    MVX(0x00000194, "CLOCK_FLEX_MAX", 0x14822, UINT64_C(0xfffffffffffea0c0), 0),
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0x822082206300622), 0, 0), /* value=0x8220822`06300622 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x822, 0, 0), /* Might bite. value=0x822 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0x2, 0, UINT64_C(0xffffffffffffffe1)), /* value=0x2 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0x10, 0, UINT64_C(0xffffffffff0000e0)), /* value=0x10 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, UINT32_C(0x883c0000), UINT32_C(0xf87f017f), UINT64_C(0xffffffff0780fc00)), /* value=0x883c0000 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0x612, 0, 0), /* value=0x612 */
    MVX(0x0000019e, "P6_UNK_0000_019e", 0x2120000, UINT64_C(0xffffffffffff0000), 0),
    MVI(0x0000019f, "P6_UNK_0000_019f", 0),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, UINT64_C(0x4066a52489), UINT64_C(0x52600099f6), UINT64_C(0xffffff0019004000)), /* value=0x40`66a52489 */
    MVX(0x000001a1, "P6_UNK_0000_01a1", 0, UINT64_C(0xffff000000000000), 0),
    MFX(0x000001a2, "I7_MSR_TEMPERATURE_TARGET", IntelI7TemperatureTarget, ReadOnly, 0x1400, 0, 0), /* value=0x1400 */
    MVX(0x000001aa, "P6_PIC_SENS_CFG", UINT32_C(0xfe7f042f), UINT64_C(0xffffffff7faf00af), 0),
    MVX(0x000001bf, "C2_UNK_0000_01bf", 0x404, UINT64_C(0xffffffffffff0000), 0),
    MFX(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x0 */
    MVX(0x000001d3, "P6_UNK_0000_01d3", 0x8000, UINT64_C(0xffffffffffff7fff), 0),
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffffa03c)), /* value=0x1 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0xffffff7f`8f47ca6b */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xffffff80`0d2b24c0 */
    MFN(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp, P6LastIntFromIp), /* value=0xffffff80`0d2ba20f */
    MFN(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp, P6LastIntToIp), /* value=0xffffff80`0d2ba200 */
    MVO(0x000001e0, "MSR_ROB_CR_BKUPTMPDR6", 0xff0),
    MFX(0x000001f8, "IA32_PLATFORM_DCA_CAP", Ia32PlatformDcaCap, Ia32PlatformDcaCap, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x0 */
    MFO(0x000001f9, "IA32_CPU_DCA_CAP", Ia32CpuDcaCap), /* value=0x1 */
    MFX(0x000001fa, "IA32_DCA_0_CAP", Ia32Dca0Cap, Ia32Dca0Cap, 0, UINT64_C(0xfffffffffefe17ff), 0), /* value=0xc01e489 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x80000000 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x3f`80000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x7fc00000 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x3f`ffc00800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x6 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x30`00000800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x0 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x0 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x0 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x0 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x0 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xffffffc000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xffffffc0000007ff)), /* value=0x0 */
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
    RSN(0x00000309, 0x0000030b, "IA32_FIXED_CTRn", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, 0, UINT64_C(0xffffff0000000000)),
    MFX(0x00000345, "IA32_PERF_CAPABILITIES", Ia32PerfCapabilities, ReadOnly, 0x10c2, 0, 0), /* value=0x10c2 */
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, 0, UINT64_C(0xfffffffffffff444)), /* value=0x0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFN(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl), /* value=0xffffffff`ffffffff */
    MFO(0x00000390, "IA32_PERF_GLOBAL_OVF_CTRL", Ia32PerfGlobalOvfCtrl), /* value=0xffffffff`ffffffff */
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x0 */
    RFN(0x00000400, 0x00000417, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFN(0x00000478, "CPUID1_FEATURE_MASK", IntelCpuId1FeatureMaskEcdx, IntelCpuId1FeatureMaskEcdx), /* value=0xffffffff`ffffffff */
    MFX(0x00000480, "IA32_VMX_BASIC", Ia32VmxBasic, ReadOnly, UINT64_C(0x5a08000000000d), 0, 0), /* value=0x5a0800`0000000d */
    MFX(0x00000481, "IA32_VMX_PINBASED_CTLS", Ia32VmxPinbasedCtls, ReadOnly, UINT64_C(0x3f00000016), 0, 0), /* value=0x3f`00000016 */
    MFX(0x00000482, "IA32_VMX_PROCBASED_CTLS", Ia32VmxProcbasedCtls, ReadOnly, UINT64_C(0xf7f9fffe0401e172), 0, 0), /* value=0xf7f9fffe`0401e172 */
    MFX(0x00000483, "IA32_VMX_EXIT_CTLS", Ia32VmxExitCtls, ReadOnly, UINT64_C(0x3ffff00036dff), 0, 0), /* value=0x3ffff`00036dff */
    MFX(0x00000484, "IA32_VMX_ENTRY_CTLS", Ia32VmxEntryCtls, ReadOnly, UINT64_C(0x3fff000011ff), 0, 0), /* value=0x3fff`000011ff */
    MFX(0x00000485, "IA32_VMX_MISC", Ia32VmxMisc, ReadOnly, 0x403c0, 0, 0), /* value=0x403c0 */
    MFX(0x00000486, "IA32_VMX_CR0_FIXED0", Ia32VmxCr0Fixed0, ReadOnly, UINT32_C(0x80000021), 0, 0), /* value=0x80000021 */
    MFX(0x00000487, "IA32_VMX_CR0_FIXED1", Ia32VmxCr0Fixed1, ReadOnly, UINT32_MAX, 0, 0), /* value=0xffffffff */
    MFX(0x00000488, "IA32_VMX_CR4_FIXED0", Ia32VmxCr4Fixed0, ReadOnly, 0x2000, 0, 0), /* value=0x2000 */
    MFX(0x00000489, "IA32_VMX_CR4_FIXED1", Ia32VmxCr4Fixed1, ReadOnly, 0x27ff, 0, 0), /* value=0x27ff */
    MFX(0x0000048a, "IA32_VMX_VMCS_ENUM", Ia32VmxVmcsEnum, ReadOnly, 0x2c, 0, 0), /* value=0x2c */
    MFX(0x0000048b, "IA32_VMX_PROCBASED_CTLS2", Ia32VmxProcBasedCtls2, ReadOnly, UINT64_C(0x4100000000), 0, 0), /* value=0x41`00000000 */
    MVX(0x000004f8, "C2_UNK_0000_04f8", 0, 0, 0),
    MVX(0x000004f9, "C2_UNK_0000_04f9", 0, 0, 0),
    MVX(0x000004fa, "C2_UNK_0000_04fa", 0, 0, 0),
    MVX(0x000004fb, "C2_UNK_0000_04fb", 0, 0, 0),
    MVX(0x000004fc, "C2_UNK_0000_04fc", 0, 0, 0),
    MVX(0x000004fd, "C2_UNK_0000_04fd", 0, 0, 0),
    MVX(0x000004fe, "C2_UNK_0000_04fe", 0, 0, 0),
    MVX(0x000004ff, "C2_UNK_0000_04ff", 0, 0, 0),
    MVX(0x00000590, "C2_UNK_0000_0590", 0, 0, 0),
    MVX(0x00000591, "C2_UNK_0000_0591", 0, ~(uint64_t)UINT32_MAX, 0),
    MFX(0x000005a0, "C2_PECI_CTL", IntelCore2PeciControl, IntelCore2PeciControl, 0, UINT64_C(0xfffffffffffffffe), 0), /* value=0x1 */
    MVI(0x000005a1, "C2_UNK_0000_05a1", 0x1),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0x0 */
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x1b0008`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xffffff80`0d2ce6c0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0x0 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x4700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0x0 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xffffff82`0dcfd000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x7fff`7c7511e0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Xeon(R) CPU           X5482  @ 3.20GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Xeon_X5482_3_20GHz =
{
    /*.pszName          = */ "Intel Xeon X5482 3.20GHz",
    /*.pszFullName      = */ "Intel(R) Xeon(R) CPU           X5482  @ 3.20GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 23,
    /*.uStepping        = */ 6,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Core2_Penryn,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_400MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 38,
    /*.fMxCsrMask       = */ 0xffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Xeon_X5482_3_20GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Xeon_X5482_3_20GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /*.DefUnknownCpuId  = */ { 0x07280202, 0x00000000, 0x00000000, 0x00000503 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Xeon_X5482_3_20GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Xeon_X5482_3_20GHz),
};

#endif /* !VBOX_CPUDB_Intel_Xeon_X5482_3_20GHz_h */

