/* $Id: Intel_Atom_330_1_60GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Atom 330 1.60GHz".
 * Generated at 2015-11-04T12:58:59Z by VBoxCpuReport v5.0.51r103818 on linux.amd64.
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

#ifndef VBOX_CPUDB_Intel_Atom_330_1_60GHz_h
#define VBOX_CPUDB_Intel_Atom_330_1_60GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Atom(TM) CPU  330   @ 1.60GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Atom_330_1_60GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000a, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000106c2, 0x01040800, 0x0040e31d, 0xbfe9fbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x4fba5901, 0x0e3080c0, 0x00000000, 0x00000000, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x04004121, 0x0140003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x04004122, 0x01c0003f, 0x0000003f, 0x00000001, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x04004143, 0x01c0003f, 0x000003ff, 0x00000001, 0 },
    { 0x00000004, 0x00000003, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00000010, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000001, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x07280203, 0x00000000, 0x00000000, 0x00002501, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x20100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x20202020, 0x746e4920, 0x52286c65, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x74412029, 0x54286d6f, 0x4320294d, 0x20205550, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x20303333, 0x20402020, 0x30362e31, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x02008040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003020, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Atom(TM) CPU  330   @ 1.60GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Atom_330_1_60GHz[] =
{
    MFI(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr), /* value=0x0 */
    MFX(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType, Ia32P5McType, 0, 0, UINT64_MAX), /* value=0x0 */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, 0, UINT64_C(0xffffffffffff0000)), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x5a7`e94bd2c0 */
    MFX(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0xc00008836ac1b), 0, 0), /* value=0xc0000`8836ac1b */
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0, UINT64_C(0xffffffff000006ff)),
    MVX(0x00000033, "TEST_CTL", 0, 0, UINT64_C(0xffffffff7fffffff)),
    MVO(0x00000039, "C2_UNK_0000_0039", 0x1),
    MFO(0x0000003a, "IA32_FEATURE_CONTROL", Ia32FeatureControl), /* value=0x1 */
    MVO(0x0000003f, "P6_UNK_0000_003f", 0),
    RFN(0x00000040, 0x00000047, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchToN, IntelLastBranchToN),
    RFN(0x00000060, 0x00000067, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MFX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", Ia32BiosSignId, Ia32BiosSignId, 0, 0, UINT32_MAX), /* value=0x20d`00000000 */
    RSN(0x000000c1, 0x000000c2, "IA32_PMCn", Ia32PmcN, Ia32PmcN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MFX(0x000000c7, "IA32_PMC6", Ia32PmcN, Ia32PmcN, 0, UINT64_C(0xfff7bdefff7df7df), 0), /* value=0x16101c00`00000000 */
    MFX(0x000000cd, "MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0x101, 0, 0), /* value=0x101 */
    MVO(0x000000ce, "IA32_PLATFORM_INFO", UINT64_C(0x1b1b0c004e4e0000)),
    MVO(0x000000cf, "C2_UNK_0000_00cf", 0x1f),
    MVO(0x000000e0, "C2_UNK_0000_00e0", 0x6800f0),
    MVO(0x000000e1, "C2_UNK_0000_00e1", UINT32_C(0xf0f00000)),
    MFX(0x000000e2, "MSR_PKG_CST_CONFIG_CONTROL", IntelPkgCStConfigControl, IntelPkgCStConfigControl, 0, 0xbfff, UINT64_C(0xfffffffffc804000)), /* value=0x26b001 */
    MFX(0x000000e3, "C2_SMM_CST_MISC_INFO", IntelCore2SmmCStMiscInfo, IntelCore2SmmCStMiscInfo, 0, 0, ~(uint64_t)UINT32_MAX), /* value=0x0 */
    MFX(0x000000e4, "MSR_PMG_IO_CAPTURE_BASE", IntelPmgIoCaptureBase, IntelPmgIoCaptureBase, 0, 0, UINT64_C(0xffffffffff800000)), /* value=0x0 */
    MVO(0x000000e5, "C2_UNK_0000_00e5", UINT32_C(0xd00a00f8)),
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf), /* value=0x63`19743600 */
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf), /* value=0x63`199424b8 */
    MFX(0x000000ee, "C1_EXT_CONFIG", IntelCore1ExtConfig, IntelCore1ExtConfig, 0, UINT64_C(0xff7bdeffffc5ffff), 0), /* value=0x3384103 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MVX(0x00000116, "BBL_CR_ADDR", 0x3fc0, UINT64_C(0xfffffff00000001f), 0),
    MVX(0x00000118, "BBL_CR_DECC", 0, UINT64_C(0xfffc0000fffc0000), 0),
    MFX(0x00000119, "BBL_CR_CTL", IntelBblCrCtl, IntelBblCrCtl, 0x938008, 0x4080017f, ~(uint64_t)UINT32_MAX), /* value=0x938008 */
    MFN(0x0000011a, "BBL_CR_TRIG", WriteOnly, IgnoreWrite),
    MVX(0x0000011b, "P6_UNK_0000_011b", 0, 0x1, UINT64_C(0xfffffffffffffffe)),
    MVX(0x0000011c, "C2_UNK_0000_011c", 0xd96000, 0, UINT64_C(0xfffffffff0000000)),
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, IntelBblCrCtl3, 0x7f00011f, UINT32_C(0xff83f81f), UINT64_C(0xffffffff007c06e0)), /* value=0x7f00011f */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x10 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0x0 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0xffffffff`81573970 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x805, 0, 0), /* value=0x805 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, 0, UINT64_MAX), /* value=0x0 */
    RSN(0x00000186, 0x00000187, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, 0, ~(uint64_t)UINT32_MAX),
    MFX(0x00000194, "CLOCK_FLEX_MAX", IntelFlexRatio, IntelFlexRatio, 0, UINT32_C(0xfffee0c0), ~(uint64_t)UINT32_MAX), /* value=0x0 */
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0xc1b0c1b06000c1b), 0, 0), /* value=0xc1b0c1b`06000c1b */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0xc1b, 0, 0), /* Might bite. value=0xc1b */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0x2, 0, UINT64_C(0xffffffffffffffe1)), /* value=0x2 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0x3, 0, UINT64_C(0xffffffffff0000e0)), /* value=0x3 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, UINT32_C(0x884c0000), UINT32_C(0xf87f03ff), UINT64_C(0xffffffff0780fc00)), /* value=0x884c0000 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0x61b, 0, 0), /* value=0x61b */
    MVX(0x0000019e, "P6_UNK_0000_019e", 0, UINT32_C(0xffff0000), ~(uint64_t)UINT32_MAX),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x60940488, UINT64_C(0x366131884), UINT64_C(0xfffffff89908c372)), /* value=0x60940488 */
    MVX(0x000001aa, "P6_PIC_SENS_CFG", UINT32_C(0x800f0421), UINT64_C(0xffffffffff80000e), 0),
    MFX(0x000001c9, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, 0, UINT64_C(0xfffffffffffffff8)), /* value=0x0 */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffffe03c)), /* value=0x0 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0xffffffff`a07ac16e */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xffffffff`8105c4f0 */
    MFN(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp, P6LastIntFromIp), /* value=0x0 */
    MFN(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp, P6LastIntToIp), /* value=0x0 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xffffffff00000ff8)), /* value=0xe0000000 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xfffffff0000007ff)), /* value=0xe0000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x6 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x0 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x0 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x0 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xfffffff0000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xffffffff00000ff8)), /* value=0x0 */
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
    MVX(0x000002e0, "I7_SB_NO_EVICT_MODE", 0, 0, UINT64_C(0xffffffff7ffffffc)),
    MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE", Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, UINT64_C(0xfffffffffffff3f8)),
    MFX(0x00000309, "IA32_FIXED_CTR0", Ia32FixedCtrN, Ia32FixedCtrN, 0x0, 0, UINT64_C(0xffffff0000000000)), /* value=0x8c */
    MFX(0x0000030a, "IA32_FIXED_CTR1", Ia32FixedCtrN, Ia32FixedCtrN, 0x1, 0x81201, UINT64_C(0xffffff0000000000)), /* value=0xff`ad893763 */
    MFX(0x0000030b, "IA32_FIXED_CTR2", Ia32FixedCtrN, Ia32FixedCtrN, 0x2, 0, UINT64_C(0xffffff0000000000)), /* value=0x8f4 */
    MFX(0x00000345, "IA32_PERF_CAPABILITIES", Ia32PerfCapabilities, ReadOnly, 0xc1, 0, 0), /* value=0xc1 */
    MFX(0x0000038d, "IA32_FIXED_CTR_CTRL", Ia32FixedCtrCtrl, Ia32FixedCtrCtrl, 0, 0, UINT64_C(0xfffffffffffff000)), /* value=0xb0 */
    MFX(0x0000038e, "IA32_PERF_GLOBAL_STATUS", Ia32PerfGlobalStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x0000038f, "IA32_PERF_GLOBAL_CTRL", Ia32PerfGlobalCtrl, Ia32PerfGlobalCtrl, 0, 0, UINT64_C(0xfffffff8fffffffc)), /* value=0x7`00000003 */
    MFX(0x00000390, "IA32_PERF_GLOBAL_OVF_CTRL", Ia32PerfGlobalOvfCtrl, Ia32PerfGlobalOvfCtrl, 0, UINT64_C(0xc000000700000003), UINT64_C(0x3ffffff8fffffffc)), /* value=0x0 */
    MVX(0x000003ca, "TODO_0000_03ca", 0x10510, 0, UINT64_C(0xffffffffffe00000)),
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, 0, UINT64_C(0xfffffffffffffffe)), /* value=0x0 */
    RFN(0x00000400, 0x00000417, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MVX(0x000004f8, "C2_UNK_0000_04f8", 0, 0, 0),
    MVX(0x000004f9, "C2_UNK_0000_04f9", 0, 0, 0),
    MVX(0x000004fa, "C2_UNK_0000_04fa", 0, 0, 0),
    MVX(0x000004fb, "C2_UNK_0000_04fb", 0, 0, 0),
    MVX(0x000004fc, "C2_UNK_0000_04fc", 0, 0, 0),
    MVX(0x000004fd, "C2_UNK_0000_04fd", 0, 0, 0),
    MVX(0x000004fe, "C2_UNK_0000_04fe", 0, 0, 0),
    MVX(0x000004ff, "C2_UNK_0000_04ff", 0, 0, 0),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0xffff8800`d6ee1c00 */
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xffffffff`815715d0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0xffffffff`81573ad0 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x47700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0x7fe4`93136740 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xffff8800`db500000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Atom(TM) CPU  330   @ 1.60GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Atom_330_1_60GHz =
{
    /*.pszName          = */ "Intel Atom 330 1.60GHz",
    /*.pszFullName      = */ "Intel(R) Atom(TM) CPU  330   @ 1.60GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 28,
    /*.uStepping        = */ 2,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_Atom_Bonnell,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_133MHZ,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 32,
    /*.fMxCsrMask       = */ 0xffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Atom_330_1_60GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Atom_330_1_60GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /*.DefUnknownCpuId  = */ { 0x07280203, 0x00000000, 0x00000000, 0x00002501 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Atom_330_1_60GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Atom_330_1_60GHz),
};

#endif /* !VBOX_CPUDB_Intel_Atom_330_1_60GHz_h */

