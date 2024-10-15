/* $Id: Intel_Core2_T7600_2_33GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Core2 T7600 2.33GHz".
 * Generated at 2017-10-12T18:17:56Z by VBoxCpuReport v5.2.0_RC1r118339 on linux.x86.
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

#ifndef VBOX_CPUDB_Intel_Core2_T7600_2_33GHz_h
#define VBOX_CPUDB_Intel_Core2_T7600_2_33GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Core(TM)2 CPU         T7600  @ 2.33GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Core2_T7600_2_33GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000a, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000006f6, 0x00020800, 0x0000e3bd, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x05b0b101, 0x005657f0, 0x00000000, 0x2cb43049, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x04000121, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x04000122, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x04004143, 0x03c0003f, 0x00000fff, 0x00000001, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00022220, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000001, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000400, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07280202, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x20100000, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x65746e49, 0x2952286c, 0x726f4320, 0x4d542865, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x43203229, 0x20205550, 0x20202020, 0x54202020, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x30303637, 0x20402020, 0x33332e32, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x10008040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003024, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Core(TM)2 CPU         T7600  @ 2.33GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Core2_T7600_2_33GHz[] =
{
    MFO(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr), /* value=0x12c5e80 */
    MFO(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType), /* value=0x0 */
    MFO(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize), /* value=0x40 */
    MFO(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter), /* value=0x215`a3e44b5c */
    MFX(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0x14000098548e25), 0, 0), /* value=0x140000`98548e25 */
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00900), 0, UINT64_C(0xfffffffffffff7ff)),
    MVO(0x00000021, "C2_UNK_0000_0021", 0),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, ReadOnly, 0x41880000, 0, 0), /* value=0x41880000 */
    MVO(0x0000002f, "P6_UNK_0000_002f", 0),
    MVO(0x00000032, "P6_UNK_0000_0032", 0),
    MVO(0x00000033, "TEST_CTL", 0),
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x5 */
    MVO(0x0000003f, "P6_UNK_0000_003f", 0),
    RFN(0x00000040, 0x00000043, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchToN, ReadOnly),
    MVO(0x0000004a, "P6_UNK_0000_004a", 0), /* value=0x0 */
    MVO(0x0000004b, "P6_UNK_0000_004b", 0), /* value=0x0 */
    MVO(0x0000004c, "P6_UNK_0000_004c", 0), /* value=0x0 */
    MVO(0x0000004d, "P6_UNK_0000_004d", 0), /* value=0x3c3a9b64`1d8552bb */
    MVO(0x0000004e, "P6_UNK_0000_004e", 0), /* value=0x3b96f62f`156143b9 */
    MVO(0x0000004f, "P6_UNK_0000_004f", 0), /* value=0xb8 */
    RFN(0x00000060, 0x00000063, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchFromN, ReadOnly),
    MVO(0x0000006c, "P6_UNK_0000_006c", 0),
    MVO(0x0000006d, "P6_UNK_0000_006d", 0),
    MVO(0x0000006e, "P6_UNK_0000_006e", 0),
    MVO(0x0000006f, "P6_UNK_0000_006f", 0xadb),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MFO(0x0000008b, "BBL_CR_D3|BIOS_SIGN", Ia32BiosSignId), /* value=0xc7`00000000 */
    MFO(0x0000009b, "IA32_SMM_MONITOR_CTL", Ia32SmmMonitorCtl), /* value=0x0 */
    MFX(0x000000a8, "C2_EMTTM_CR_TABLES_0", IntelCore2EmttmCrTablesN, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    MFX(0x000000a9, "C2_EMTTM_CR_TABLES_1", IntelCore2EmttmCrTablesN, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    MFX(0x000000aa, "C2_EMTTM_CR_TABLES_2", IntelCore2EmttmCrTablesN, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    MFX(0x000000ab, "C2_EMTTM_CR_TABLES_3", IntelCore2EmttmCrTablesN, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    MFX(0x000000ac, "C2_EMTTM_CR_TABLES_4", IntelCore2EmttmCrTablesN, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    MFX(0x000000ad, "C2_EMTTM_CR_TABLES_5", IntelCore2EmttmCrTablesN, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    RFN(0x000000c1, 0x000000c2, "IA32_PMCn", Ia32PmcN, ReadOnly),
    MVO(0x000000c7, "P6_UNK_0000_00c7", UINT64_C(0x1e00000042000000)),
    MFX(0x000000cd, "MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0x933, 0, 0), /* value=0x933 */
    MVO(0x000000ce, "P6_UNK_0000_00ce", UINT64_C(0x130e253b530613)),
    MVO(0x000000e0, "C2_UNK_0000_00e0", 0x14860f0),
    MVO(0x000000e1, "C2_UNK_0000_00e1", UINT32_C(0xf0f00000)),
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, 0x404000, UINT64_C(0xfffffffffc001000)), /* value=0x202a01 */
    MFO(0x000000e3, "C2_SMM_CST_MISC_INFO", IntelCore2SmmCStMiscInfo), /* value=0x8040414 */
    MFO(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase), /* value=0x20414 */
    MVO(0x000000e5, "C2_UNK_0000_00e5", UINT32_C(0xd0220dc8)),
    MFO(0x000000e7, "IA32_MPERF", Ia32MPerf), /* value=0xc7`b82ef32a */
    MFO(0x000000e8, "IA32_APERF", Ia32APerf), /* value=0x55`9818510c */
    MFO(0x000000ee, "C1_EXT_CONFIG", IntelCore1ExtConfig), /* value=0x80b90400 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MVO(0x00000116, "BBL_CR_ADDR", 0),
    MVO(0x00000118, "BBL_CR_DECC", 0xffebe),
    MVO(0x0000011b, "P6_UNK_0000_011b", 0),
    MVO(0x0000011c, "C2_UNK_0000_011c", UINT32_C(0xe00000cc)),
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, ReadOnly, 0x74702109, 0, 0), /* value=0x74702109 */
    MVO(0x0000014a, "TODO_0000_014a", 0),
    MVO(0x0000014b, "TODO_0000_014b", 0),
    MVO(0x0000014c, "TODO_0000_014c", 0),
    MVO(0x0000014e, "P6_UNK_0000_014e", UINT32_C(0xe4dfe927)),
    MVO(0x0000014f, "P6_UNK_0000_014f", 0),
    MVO(0x00000151, "P6_UNK_0000_0151", 0x3bfcb56f),
    MFO(0x0000015f, "C1_DTS_CAL_CTRL", IntelCore1DtsCalControl), /* value=0x230613 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x60 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0xf5a07c40 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0xc15af09c */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x806, 0, 0), /* value=0x806 */
    MFO(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus), /* value=0x0 */
    RFN(0x00000186, 0x00000187, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, ReadOnly),
    MVO(0x00000193, "C2_UNK_0000_0193", 0),
    MFX(0x00000194, "CLOCK_FLEX_MAX", IntelFlexRatio, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0x6130e2506040613), 0, 0), /* value=0x6130e25`06040613 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, ReadOnly, 0x613, 0, UINT64_MAX), /* Might bite. value=0x613 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, ReadOnly, 0x2, 0, 0), /* value=0x2 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, ReadOnly, 0x3, 0, 0), /* value=0x3 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, ReadOnly, UINT32_C(0x8831000c), 0, 0), /* value=0x8831000c */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0x613, 0, 0), /* value=0x613 */
    MVO(0x0000019e, "P6_UNK_0000_019e", 0xb240000),
    MVO(0x0000019f, "P6_UNK_0000_019f", 0),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, UINT64_C(0x4066a52489), UINT64_C(0x52600099f6), UINT64_C(0xffffff0019004000)), /* value=0x40`66a52489 */
    MVO(0x000001a1, "P6_UNK_0000_01a1", 0),
    MFX(0x000001a2, "I7_MSR_TEMPERATURE_TARGET", IntelI7TemperatureTarget, ReadOnly, 0, 0, 0), /* value=0x0 */
    MVO(0x000001aa, "P6_PIC_SENS_CFG", 0x5ebf042f),
    MVO(0x000001bf, "C2_UNK_0000_01bf", 0x404),
    MFO(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos), /* value=0x3 */
    MVO(0x000001d3, "P6_UNK_0000_01d3", 0x8000),
    MFO(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl), /* value=0x1 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0xc12c5d73 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xc10357d0 */
    MFO(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp), /* value=0xc132a284 */
    MFO(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp), /* value=0xc1329543 */
    MVO(0x000001e0, "MSR_ROB_CR_BKUPTMPDR6", 0xff0),
    MFO(0x000001f8, "IA32_PLATFORM_DCA_CAP", Ia32PlatformDcaCap), /* value=0x0 */
    MFO(0x000001f9, "IA32_CPU_DCA_CAP", Ia32CpuDcaCap), /* value=0x0 */
    MFO(0x000001fa, "IA32_DCA_0_CAP", Ia32Dca0Cap), /* value=0xc01e488 */
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
 * Database entry for Intel(R) Core(TM)2 CPU         T7600  @ 2.33GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Core2_T7600_2_33GHz =
{
    /*.pszName          = */ "Intel Core2 T7600 2.33GHz",
    /*.pszFullName      = */ "Intel(R) Core(TM)2 CPU         T7600  @ 2.33GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 15,
    /*.uStepping        = */ 6,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Core2_Merom,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_167MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 36,
    /*.fMxCsrMask       = */ 0x0000ffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Core2_T7600_2_33GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Core2_T7600_2_33GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /*.DefUnknownCpuId  = */ { 0x07280202, 0x00000000, 0x00000000, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Core2_T7600_2_33GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Core2_T7600_2_33GHz),
};

#endif /* !VBOX_CPUDB_Intel_Core2_T7600_2_33GHz_h */

