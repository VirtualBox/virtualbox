/* $Id: Intel_Pentium_4_3_00GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Pentium 4 3.00GHz".
 * Generated at 2013-12-18T06:37:54Z by VBoxCpuReport v4.3.53r91376 on win.amd64.
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

#ifndef VBOX_CPUDB_Intel_Pentium_4_3_00GHz_h
#define VBOX_CPUDB_Intel_Pentium_4_3_00GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Pentium(R) 4 CPU 3.00GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Pentium_4_3_00GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x00000005, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x00000f43, 0x00020800, 0x0000649d, 0xbfebfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x605b5001, 0x00000000, 0x00000000, 0x007d7040, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, UINT32_MAX, 0x00004121, 0x01c0003f, 0x0000001f, 0x00000000, 0 },
    { 0x00000004, 0x00000001, UINT32_MAX, 0x00004143, 0x01c0103f, 0x000007ff, 0x00000000, 0 },
    { 0x00000004, 0x00000002, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000008, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x20100800, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x20202020, 0x20202020, 0x6e492020, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x286c6574, 0x50202952, 0x69746e65, 0x52286d75, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x20342029, 0x20555043, 0x30302e33, 0x007a4847, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x08006040, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003024, 0x00000000, 0x00000000, 0x00000000, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Pentium(R) 4 CPU 3.00GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Pentium_4_3_00GHz[] =
{
    MFO(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr), /* value=0xc55df88 */
    MFO(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType), /* value=0xbe000300`1008081f */
    MFX(0x00000006, "IA32_MONITOR_FILTER_LINE_SIZE", Ia32MonitorFilterLineSize, Ia32MonitorFilterLineSize, 0, UINT64_C(0xffffffffffff0000), 0), /* value=0x40 */
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x1ac`2077a134 */
    MFV(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0x12000000000000)),
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0x600, UINT64_C(0xffffff00000000ff)),
    MFX(0x0000002a, "P4_EBC_HARD_POWERON", IntelP4EbcHardPowerOn, IntelP4EbcHardPowerOn, 0, UINT64_MAX, 0), /* value=0x0 */
    MFX(0x0000002b, "P4_EBC_SOFT_POWERON", IntelP4EbcSoftPowerOn, IntelP4EbcSoftPowerOn, 0x7e, UINT64_C(0xffffffffffffff80), 0), /* value=0x7e */
    MFX(0x0000002c, "P4_EBC_FREQUENCY_ID", IntelP4EbcFrequencyId, IntelP4EbcFrequencyId, 0xf12010f, UINT64_MAX, 0), /* value=0xf12010f */
    MVX(0x00000039, "C2_UNK_0000_0039", 0x1, 0x1f, ~(uint64_t)UINT32_MAX),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, IgnoreWrite),
    MVX(0x00000080, "P4_UNK_0000_0080", 0, ~(uint64_t)UINT32_MAX, UINT32_MAX),
    MFX(0x0000008b, "IA32_BIOS_SIGN_ID", Ia32BiosSignId, Ia32BiosSignId, 0, UINT32_MAX, 0), /* value=0x5`00000000 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MFX(0x00000119, "BBL_CR_CTL", IntelBblCrCtl, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFN(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp), /* value=0x0 */
    MFN(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip), /* value=0x0 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x180204, 0, 0), /* value=0x180204 */
    MFN(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus), /* value=0x0 */
    MVX(0x00000180, "MSR_MCG_RAX", 0, 0, UINT64_MAX),
    MVX(0x00000181, "MSR_MCG_RBX", 0, 0, UINT64_MAX),
    MVX(0x00000182, "MSR_MCG_RCX", 0, 0, UINT64_MAX),
    MVX(0x00000183, "MSR_MCG_RDX", 0, 0, UINT64_MAX),
    MVX(0x00000184, "MSR_MCG_RSI", 0, 0, UINT64_MAX),
    MVX(0x00000185, "MSR_MCG_RDI", 0, 0, UINT64_MAX),
    MFX(0x00000186, "MSR_MCG_RBP", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0, 0, UINT64_MAX), /* value=0x0 */
    MFX(0x00000187, "MSR_MCG_RSP", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0, 0, UINT64_MAX), /* value=0x0 */
    MVX(0x00000188, "MSR_MCG_RFLAGS", 0, 0, UINT64_MAX),
    MVX(0x00000189, "MSR_MCG_RIP", 0, 0, UINT64_MAX),
    MVX(0x0000018a, "MSR_MCG_MISC", 0, 0, UINT64_MAX),
    MVX(0x0000018b, "MSR_MCG_RESERVED1", 0, 0, UINT64_MAX),
    MVX(0x0000018c, "MSR_MCG_RESERVED2", 0, 0, UINT64_MAX),
    MVX(0x0000018d, "MSR_MCG_RESERVED3", 0, 0, UINT64_MAX),
    MVX(0x0000018e, "MSR_MCG_RESERVED4", 0, 0, UINT64_MAX),
    MVX(0x0000018f, "MSR_MCG_RESERVED5", 0, 0, UINT64_MAX),
    MVX(0x00000190, "MSR_MCG_R8", 0, 0, UINT64_MAX),
    MVX(0x00000191, "MSR_MCG_R9", 0, 0, UINT64_MAX),
    MVX(0x00000192, "MSR_MCG_R10", 0, 0, UINT64_MAX),
    MVX(0x00000193, "MSR_MCG_R11", 0, 0, UINT64_MAX),
    MVX(0x00000194, "MSR_MCG_R12", 0, 0, UINT64_MAX),
    MVX(0x00000195, "MSR_MCG_R13", 0, 0, UINT64_MAX),
    MVX(0x00000196, "MSR_MCG_R14", 0, 0, UINT64_MAX),
    MVX(0x00000197, "MSR_MCG_R15", 0, 0, UINT64_MAX),
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, Ia32PerfStatus, UINT64_C(0xf2d00000f2d), UINT64_MAX, 0), /* value=0xf2d`00000f2d */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0xf2d, 0, 0), /* Might bite. value=0xf2d */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0, UINT64_C(0xffffffffffffffe1), 0), /* value=0x0 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0, UINT64_C(0xfffffffffffffffc), 0), /* value=0x0 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, 0, UINT64_C(0xfffffffffffffff5), 0), /* value=0x0 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, ReadOnly, 0xe2d, 0, 0), /* value=0xe2d */
    MVX(0x0000019e, "P6_UNK_0000_019e", 0, UINT64_C(0xffffffffffff0000), 0),
    MVX(0x0000019f, "P6_UNK_0000_019f", UINT64_C(0x32050500000101), UINT64_C(0xff000000fff0c0c0), 0),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x22850089, 0x20800080, UINT64_C(0xfffffffbdc10f800)), /* value=0x22850089 */
    MVX(0x000001a1, "MSR_PLATFORM_BRV", 0, UINT64_C(0xfffffffffffcc0c0), 0),
    MFX(0x000001a2, "P4_UNK_0000_01a2", IntelI7TemperatureTarget, ReadOnly, 0x61048, 0, 0), /* value=0x61048 */
    MFO(0x000001d7, "MSR_LER_FROM_LIP", P6LastIntFromIp), /* value=0x0 */
    MFO(0x000001d8, "MSR_LER_TO_LIP", P6LastIntToIp), /* value=0x0 */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, 0, UINT64_C(0xffffffffffffff80)), /* value=0x0 */
    MFX(0x000001da, "MSR_LASTBRANCH_TOS", IntelLastBranchTos, IntelLastBranchTos, 0, UINT64_C(0xfffffffffffffff0), 0), /* value=0x0 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xffffff00000007ff)), /* value=0xf`c0000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x3f600000 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xffffff00000007ff)), /* value=0xf`ffe00800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x3f800000 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xffffff00000007ff)), /* value=0xf`ff800800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x0 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x0 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
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
    MVX(0x00000300, "P4_MSR_BPU_COUNTER0", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000301, "P4_MSR_BPU_COUNTER1", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000302, "P4_MSR_BPU_COUNTER2", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000303, "P4_MSR_BPU_COUNTER3", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000304, "P4_MSR_MS_COUNTER0", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000305, "P4_MSR_MS_COUNTER1", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000306, "P4_MSR_MS_COUNTER2", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000307, "P4_MSR_MS_COUNTER3", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000308, "P4_MSR_FLAME_COUNTER0", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000309, "P4_MSR_FLAME_COUNTER1", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x0000030a, "P4_MSR_FLAME_COUNTER2", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x0000030b, "P4_MSR_FLAME_COUNTER3", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x0000030c, "P4_MSR_IQ_COUNTER0", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x0000030d, "P4_MSR_IQ_COUNTER1", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x0000030e, "P4_MSR_IQ_COUNTER2", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x0000030f, "P4_MSR_IQ_COUNTER3", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000310, "P4_MSR_IQ_COUNTER4", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000311, "P4_MSR_IQ_COUNTER5", 0, UINT64_C(0xffffff0000000000), 0),
    MVX(0x00000360, "P4_MSR_BPU_CCCR0", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000361, "P4_MSR_BPU_CCCR1", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000362, "P4_MSR_BPU_CCCR2", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000363, "P4_MSR_BPU_CCCR3", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000364, "P4_MSR_MS_CCCR0", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000365, "P4_MSR_MS_CCCR1", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000366, "P4_MSR_MS_CCCR2", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000367, "P4_MSR_MS_CCCR3", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000368, "P4_MSR_FLAME_CCCR0", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x00000369, "P4_MSR_FLAME_CCCR1", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x0000036a, "P4_MSR_FLAME_CCCR2", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x0000036b, "P4_MSR_FLAME_CCCR3", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x0000036c, "P4_MSR_IQ_CCCR0", 0, UINT64_C(0xffffffff000007ff), 0),
    MVX(0x0000036d, "P4_MSR_IQ_CCCR1", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x0000036e, "P4_MSR_IQ_CCCR2", 0, UINT64_C(0xffffffff00000fff), 0),
    MVX(0x0000036f, "P4_MSR_IQ_CCCR3", 0, UINT64_C(0xffffffff000007ff), 0),
    MVX(0x00000370, "P4_MSR_IQ_CCCR4", 0, UINT64_C(0xffffffff000000ff), 0),
    MVX(0x00000371, "P4_MSR_IQ_CCCR5", 0, UINT64_C(0xffffffff000000ff), 0),
    MVX(0x000003a0, "P4_MSR_BSU_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003a1, "P4_MSR_BSU_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003a2, "P4_MSR_FSB_ESCR0", 0, UINT64_C(0xffffffff40000000), UINT32_C(0x80000000)),
    MVX(0x000003a3, "P4_MSR_FSB_ESCR1", 0, UINT64_C(0xffffffff40000000), UINT32_C(0x80000000)),
    MVX(0x000003a4, "P4_MSR_FIRM_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003a5, "P4_MSR_FIRM_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003a6, "P4_MSR_FLAME_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003a7, "P4_MSR_FLAME_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003a8, "P4_MSR_DAC_ESCR0", 0, UINT64_C(0xffffffff61fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003a9, "P4_MSR_DAC_ESCR1", 0, UINT64_C(0xffffffff61fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003aa, "P4_MSR_MOB_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003ab, "P4_MSR_MOB_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003ac, "P4_MSR_PMH_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003ad, "P4_MSR_PMH_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003ae, "P4_MSR_SAAT_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003af, "P4_MSR_SAAT_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003b0, "P4_MSR_U2L_ESCR0", 0, UINT64_C(0xffffffff71c001f0), UINT32_C(0x80000000)),
    MVX(0x000003b1, "P4_MSR_U2L_ESCR1", 0, UINT64_C(0xffffffff71c001f0), UINT32_C(0x80000000)),
    MVX(0x000003b2, "P4_MSR_BPU_ESCR0", 0, UINT64_C(0xffffffff61fc0000), UINT32_C(0x80000000)),
    MVX(0x000003b3, "P4_MSR_BPU_ESCR1", 0, UINT64_C(0xffffffff61fc0000), UINT32_C(0x80000000)),
    MVX(0x000003b4, "P4_MSR_IS_ESCR0", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003b5, "P4_MSR_IS_ESCR1", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003b6, "P4_MSR_ITLB_ESCR0", 0, UINT64_C(0xffffffff0ffff1e0), UINT32_C(0x80000000)),
    MVX(0x000003b7, "P4_MSR_ITLB_ESCR1", 0, UINT64_C(0xffffffff0ffff1e0), UINT32_C(0x80000000)),
    MVX(0x000003b8, "P4_MSR_CRU_ESCR0", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003b9, "P4_MSR_CRU_ESCR1", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003ba, "P4_MSR_IQ_ESCR0", 0, UINT64_C(0xffffffff7fffffff), UINT32_C(0x80000000)),
    MVX(0x000003bb, "P4_MSR_IQ_ESCR1", 0, UINT64_C(0xffffffff7fffffff), UINT32_C(0x80000000)),
    MVX(0x000003bc, "P4_MSR_RAT_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003bd, "P4_MSR_RAT_ESCR1", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003be, "P4_MSR_SSU_ESCR0", 0, ~(uint64_t)UINT32_MAX, UINT32_C(0x80000000)),
    MVX(0x000003c0, "P4_MSR_MS_ESCR0", 0, UINT64_C(0xffffffff61ff81e0), UINT32_C(0x80000000)),
    MVX(0x000003c1, "P4_MSR_MS_ESCR1", 0, UINT64_C(0xffffffff61ff81e0), UINT32_C(0x80000000)),
    MVX(0x000003c2, "P4_MSR_TBPU_ESCR0", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003c3, "P4_MSR_TBPU_ESCR1", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003c4, "P4_MSR_TC_ESCR0", 0, UINT64_C(0xffffffff61f801f0), UINT32_C(0x80000000)),
    MVX(0x000003c5, "P4_MSR_TC_ESCR1", 0, UINT64_C(0xffffffff61f801f0), UINT32_C(0x80000000)),
    MVX(0x000003c8, "P4_MSR_IX_ESCR0", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003c9, "P4_MSR_IX_ESCR0", 0, UINT64_C(0xffffffff71fe01f0), UINT32_C(0x80000000)),
    MVX(0x000003ca, "P4_MSR_ALF_ESCR0", 0, UINT64_C(0xffffffff700001f0), UINT32_C(0x80000000)),
    MVX(0x000003cb, "P4_MSR_ALF_ESCR1", 0, UINT64_C(0xffffffff700001f0), UINT32_C(0x80000000)),
    MVX(0x000003cc, "P4_MSR_CRU_ESCR2", 0, UINT64_C(0xffffffff61f001f0), UINT32_C(0x80000000)),
    MVX(0x000003cd, "P4_MSR_CRU_ESCR3", 0, UINT64_C(0xffffffff61f001f0), UINT32_C(0x80000000)),
    MVX(0x000003e0, "P4_MSR_CRU_ESCR4", 0, UINT64_C(0xffffffff71ff01f0), UINT32_C(0x80000000)),
    MVX(0x000003e1, "P4_MSR_CRU_ESCR5", 0, UINT64_C(0xffffffff71ff01f0), UINT32_C(0x80000000)),
    MVX(0x000003f0, "P4_MSR_TC_PRECISE_EVENT", 0xfc00, UINT64_C(0xfffffffffffc001f), 0),
    MFX(0x000003f1, "IA32_PEBS_ENABLE", Ia32PebsEnable, Ia32PebsEnable, 0, UINT64_C(0xfffffffff8000000), 0), /* value=0x0 */
    MVX(0x000003f2, "P4_MSR_PEBS_MATRIX_VERT", 0, UINT64_C(0xffffffffffffe000), 0),
    MVX(0x000003f5, "P4_UNK_0000_03f5", 0, UINT64_C(0xffffffffffff0000), 0),
    MVX(0x000003f6, "P4_UNK_0000_03f6", 0, UINT64_C(0xffffffffffe00000), 0),
    MVX(0x000003f7, "P4_UNK_0000_03f7", 0, UINT64_C(0xfffe000000000000), 0),
    MVX(0x000003f8, "P4_UNK_0000_03f8", 0, UINT64_C(0xffffff000000003f), 0),
    RFN(0x00000400, 0x0000040f, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFN(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea), /* value=0x0 */
    RFN(0x00000680, 0x0000068f, "MSR_LASTBRANCH_n_FROM_IP", IntelLastBranchFromN, IntelLastBranchFromN),
    RFN(0x000006c0, 0x000006cf, "MSR_LASTBRANCH_n_TO_IP", IntelLastBranchToN, IntelLastBranchToN),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0xd01, 0x400, UINT64_C(0xfffffffffffff2fe)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xfffff800`654efdc0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0xfffff800`654efb00 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x4700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0xeed1e000 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xfffff880`009bf000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x7f7`eed1c000 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Pentium(R) 4 CPU 3.00GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Pentium_4_3_00GHz =
{
    /*.pszName          = */ "Intel Pentium 4 3.00GHz",
    /*.pszFullName      = */ "Intel(R) Pentium(R) 4 CPU 3.00GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 15,
    /*.uModel           = */ 4,
    /*.uStepping        = */ 3,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_NB_Prescott2M,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_UNKNOWN,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 36,
    /*.fMxCsrMask       = */ 0xffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Pentium_4_3_00GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Pentium_4_3_00GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /*.DefUnknownCpuId  = */ { 0x00000040, 0x00000040, 0x00000000, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Pentium_4_3_00GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Pentium_4_3_00GHz),
};

#endif /* !VBOX_CPUDB_Intel_Pentium_4_3_00GHz_h */

