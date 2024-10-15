/* $Id: Intel_Pentium_M_processor_2_00GHz.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "Intel Pentium M processor 2.00GHz".
 * Generated at 2013-12-09T14:18:00Z by VBoxCpuReport v4.3.51r91027 on win.x86.
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

#ifndef VBOX_CPUDB_Intel_Pentium_M_processor_2_00GHz_h
#define VBOX_CPUDB_Intel_Pentium_M_processor_2_00GHz_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for Intel(R) Pentium(R) M processor 2.00GHz.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_Intel_Pentium_M_processor_2_00GHz[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x756e6547, 0x6c65746e, 0x49656e69, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x000006d6, 0x00000816, 0x00000180, 0xafe9f9bf, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x02b3b001, 0x000000f0, 0x00000000, 0x2c04307d, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000004, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000002, 0x00000000, 0x00000000, 0x20202020, 0x20202020, 0x65746e49, 0x2952286c, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x6e655020, 0x6d756974, 0x20295228, 0x7270204d, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x7365636f, 0x20726f73, 0x30302e32, 0x007a4847, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for Intel(R) Pentium(R) M processor 2.00GHz.
 */
static CPUMMSRRANGE const g_aMsrRanges_Intel_Pentium_M_processor_2_00GHz[] =
{
    MFI(0x00000000, "IA32_P5_MC_ADDR", Ia32P5McAddr), /* value=0x0 */
    MFI(0x00000001, "IA32_P5_MC_TYPE", Ia32P5McType), /* value=0x0 */
    MFX(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x22`4d44782e */
    MFV(0x00000017, "IA32_PLATFORM_ID", Ia32PlatformId, ReadOnly, UINT64_C(0x140000d0248a28)),
    MVX(0x00000018, "P6_UNK_0000_0018", 0, 0, 0),
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00100), UINT64_C(0xffffffff00000600), 0xff),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, IntelEblCrPowerOn, 0x45080000, UINT64_C(0xfffffffffff7ff7e), 0), /* value=0x45080000 */
    MVX(0x0000002f, "P6_UNK_0000_002f", 0, UINT64_C(0xfffffffffffffff5), 0),
    MVX(0x00000032, "P6_UNK_0000_0032", 0, UINT64_C(0xfffffffffffe0000), 0),
    MVX(0x00000033, "TEST_CTL", 0, UINT64_C(0xffffffff40000000), 0),
    MVX(0x00000034, "P6_UNK_0000_0034", 0x77ff, ~(uint64_t)UINT32_MAX, UINT32_C(0xfff80000)),
    MVO(0x00000035, "P6_UNK_0000_0035", 0x300008),
    MVX(0x0000003b, "P6_UNK_0000_003b", 0, UINT64_C(0xafffffffe), UINT64_C(0xfffffff500000001)),
    MVO(0x0000003f, "P6_UNK_0000_003f", 0x4),
    RFN(0x00000040, 0x00000047, "MSR_LASTBRANCH_n", IntelLastBranchFromToN, ReadOnly),
    MVX(0x0000004a, "P6_UNK_0000_004a", 0, 0, 0), /* value=0x0 */
    MVX(0x0000004b, "P6_UNK_0000_004b", 0, 0, 0), /* value=0x0 */
    MVX(0x0000004c, "P6_UNK_0000_004c", 0, 0, 0), /* value=0x0 */
    MVX(0x0000004d, "P6_UNK_0000_004d", 0, 0, 0), /* value=0xeb1cffbf`8918200a */
    MVX(0x0000004e, "P6_UNK_0000_004e", 0, 0, 0), /* value=0x8204c60a`e8009512 */
    MVX(0x0000004f, "P6_UNK_0000_004f", 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MVI(0x00000050, "P6_UNK_0000_0050", 0), /* Villain? value=0x0 */
    MVI(0x00000051, "P6_UNK_0000_0051", 0), /* Villain? value=0x0 */
    MVI(0x00000052, "P6_UNK_0000_0052", 0), /* Villain? value=0x0 */
    MVI(0x00000053, "P6_UNK_0000_0053", 0), /* Villain? value=0x0 */
    MVI(0x00000054, "P6_UNK_0000_0054", 0), /* Villain? value=0x0 */
    MVX(0x0000006c, "P6_UNK_0000_006c", 0, UINT64_C(0xffffffff00000082), 0),
    MVX(0x0000006d, "P6_UNK_0000_006d", 0, UINT64_C(0xffffffff00000082), 0),
    MVX(0x0000006e, "P6_UNK_0000_006e", 0, UINT64_C(0xffffffff00000082), 0),
    MVO(0x0000006f, "P6_UNK_0000_006f", 0xadb),
    MFN(0x00000079, "IA32_BIOS_UPDT_TRIG", WriteOnly, Ia32BiosUpdateTrigger),
    MVX(0x00000088, "BBL_CR_D0", 0, 0, 0), /* value=0xfcaeffff`d779fd3e */
    MVX(0x00000089, "BBL_CR_D1", 0, 0, 0), /* value=0xefffbcb7`ff77fbef */
    MVX(0x0000008a, "BBL_CR_D2", 0, 0, 0), /* value=0xdfff3f2f`fb367d9f */
    MVX(0x0000008b, "BBL_CR_D3|BIOS_SIGN", UINT64_C(0x1800000000), 0, 0),
    MVX(0x0000008c, "P6_UNK_0000_008c", 0, 0, 0), /* value=0xeffff3ff`ef39bfff */
    MVX(0x0000008d, "P6_UNK_0000_008d", 0, 0, 0), /* value=0xf773adfb`ef3ff3fc */
    MVX(0x0000008e, "P6_UNK_0000_008e", 0, 0, 0), /* value=0xfeb7f6ff`ebbffeff */
    MVX(0x0000008f, "P6_UNK_0000_008f", 0, 0, 0), /* value=0xd6ffb7af`ffad9e7e */
    MVX(0x00000090, "P6_UNK_0000_0090", 0, UINT64_C(0xfffffffffffffffa), 0), /* value=0x9ebdb4b5 */
    MVX(0x000000ae, "P6_UNK_0000_00ae", UINT64_C(0x1000000000007efc), 0, 0),
    MFX(0x000000c1, "IA32_PMC0", Ia32PmcN, Ia32PmcN, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x000000c2, "IA32_PMC1", Ia32PmcN, Ia32PmcN, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MVI(0x000000c7, "P6_UNK_0000_00c7", UINT64_C(0x5a000000ac000000)),
    MFX(0x000000cd, "MSR_FSB_FREQ", IntelP6FsbFrequency, ReadOnly, 0, 0, 0),
    MVO(0x000000ce, "P6_UNK_0000_00ce", UINT64_C(0x2812140000000000)),
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MVX(0x00000116, "BBL_CR_ADDR", UINT32_C(0xfe7efff0), UINT64_C(0xffffffff0000000f), 0),
    MVX(0x00000118, "BBL_CR_DECC", UINT64_C(0xc0000000c1ae9fda), UINT64_C(0xfffffff00000000), 0),
    MFX(0x00000119, "BBL_CR_CTL", IntelBblCrCtl, IntelBblCrCtl, 0x8, UINT64_C(0xffffffffc00001ff), 0), /* value=0x8 */
    MVI(0x0000011b, "P6_UNK_0000_011b", 0),
    MFX(0x0000011e, "BBL_CR_CTL3", IntelBblCrCtl3, IntelBblCrCtl3, 0x34272b, UINT64_C(0xfffffffffffbfc1f), 0), /* value=0x34272b */
    MVI(0x00000131, "P6_UNK_0000_0131", 0),
    MVX(0x0000014e, "P6_UNK_0000_014e", 0xd31f40, UINT64_C(0xfffffffff000008f), 0),
    MVI(0x0000014f, "P6_UNK_0000_014f", 0xd31f40),
    MVX(0x00000150, "P6_UNK_0000_0150", 0, UINT64_C(0xffffffffdfffe07f), 0x20000000),
    MVX(0x00000151, "P6_UNK_0000_0151", 0x3c531fc6, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x00000154, "P6_UNK_0000_0154", 0),
    MVX(0x0000015b, "P6_UNK_0000_015b", 0, ~(uint64_t)UINT32_MAX, 0),
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x8 */
    MFX(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0xf78af000 */
    MFX(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x804de6f0 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x5, 0, 0), /* value=0x5 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    RSN(0x00000186, 0x00000187, "IA32_PERFEVTSELn", Ia32PerfEvtSelN, Ia32PerfEvtSelN, 0x0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00000194, "CLOCK_FLEX_MAX", 0, UINT64_C(0xfffffffffffee0c0), 0),
    MFX(0x00000198, "IA32_PERF_STATUS", Ia32PerfStatus, ReadOnly, UINT64_C(0x612142806000612), 0, 0), /* value=0x6121428`06000612 */
    MFX(0x00000199, "IA32_PERF_CTL", Ia32PerfCtl, Ia32PerfCtl, 0x612, 0, 0), /* Might bite. value=0x612 */
    MFX(0x0000019a, "IA32_CLOCK_MODULATION", Ia32ClockModulation, Ia32ClockModulation, 0x2, UINT64_C(0xffffffffffffffe1), 0), /* value=0x2 */
    MFX(0x0000019b, "IA32_THERM_INTERRUPT", Ia32ThermInterrupt, Ia32ThermInterrupt, 0, UINT64_C(0xfffffffffffffffc), 0), /* value=0x0 */
    MFX(0x0000019c, "IA32_THERM_STATUS", Ia32ThermStatus, Ia32ThermStatus, 0, UINT64_C(0xfffffffffffffffd), 0), /* value=0x0 */
    MFX(0x0000019d, "IA32_THERM2_CTL", Ia32Therm2Ctl, Ia32Therm2Ctl, 0x10612, UINT64_C(0xfffffffffffee0c0), 0), /* value=0x10612 */
    MVX(0x0000019e, "P6_UNK_0000_019e", 0, UINT64_C(0xffffffffffff0000), 0),
    MVI(0x0000019f, "P6_UNK_0000_019f", 0),
    MFX(0x000001a0, "IA32_MISC_ENABLE", Ia32MiscEnable, Ia32MiscEnable, 0x111088, UINT64_C(0xffffffff001ffb77), 0), /* value=0x111088 */
    MVX(0x000001a1, "P6_UNK_0000_01a1", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x000001aa, "P6_PIC_SENS_CFG", 0x3, UINT64_C(0xfffffffffffffffc), 0),
    MVX(0x000001ae, "P6_UNK_0000_01ae", 0, UINT64_C(0xfffffffffffffe00), 0),
    MVX(0x000001af, "P6_UNK_0000_01af", 0x3ff, UINT64_C(0xfffffffffffffc00), 0),
    MVO(0x000001c9, "TODO_0000_01c9", 0x8000000),
    MVX(0x000001d3, "P6_UNK_0000_01d3", 0x8000, UINT64_C(0xffffffffffff7fff), 0),
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, UINT64_C(0xffffffffffffc200), 0), /* value=0x1 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0xaad05fa1 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xaad06480 */
    MFO(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp), /* value=0x7dba1245 */
    MFO(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp), /* value=0x806f5d54 */
    MVO(0x000001e0, "MSR_ROB_CR_BKUPTMPDR6", 0xff0),
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`c0000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x40000006 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`e0000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x5ff80000 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`fff80800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`00000000 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`00000000 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`00000000 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`00000000 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, UINT64_C(0xf00000000), UINT64_C(0xfffffff000000fff)), /* value=0xf`00000000 */
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
    RFN(0x00000400, 0x00000413, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFX(0x00000600, "IA32_DS_AREA", Ia32DsArea, Ia32DsArea, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MVX(0x00001000, "P6_DEBUG_REGISTER_0", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001001, "P6_DEBUG_REGISTER_1", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001002, "P6_DEBUG_REGISTER_2", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001003, "P6_DEBUG_REGISTER_3", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001004, "P6_DEBUG_REGISTER_4", UINT32_C(0xffff0ff0), ~(uint64_t)UINT32_MAX, 0),
    MVX(0x00001005, "P6_DEBUG_REGISTER_5", 0x400, ~(uint64_t)UINT32_MAX, 0),
    MVI(0x00001006, "P6_DEBUG_REGISTER_6", UINT32_C(0xffff0ff0)), /* Villain? */
    MVI(0x00001007, "P6_DEBUG_REGISTER_7", 0x400), /* Villain? */
    MVO(0x0000103f, "P6_UNK_0000_103f", 0x4),
    MVO(0x000010cd, "P6_UNK_0000_10cd", 0),
    MFW(0x00002000, "P6_CR0", IntelP6CrN, IntelP6CrN, UINT64_C(0xffffffff00000010)), /* value=0x8001003b */
    MFX(0x00002002, "P6_CR2", IntelP6CrN, IntelP6CrN, 0x2, ~(uint64_t)UINT32_MAX, 0), /* value=0xc30000 */
    MFX(0x00002003, "P6_CR3", IntelP6CrN, IntelP6CrN, 0x3, ~(uint64_t)UINT32_MAX, 0), /* value=0x29765000 */
    MFX(0x00002004, "P6_CR4", IntelP6CrN, IntelP6CrN, 0x4, ~(uint64_t)UINT32_MAX, 0), /* value=0x6d9 */
    MVO(0x0000203f, "P6_UNK_0000_203f", 0x4),
    MVO(0x000020cd, "P6_UNK_0000_20cd", 0),
    MVO(0x0000303f, "P6_UNK_0000_303f", 0x4),
    MVO(0x000030cd, "P6_UNK_0000_30cd", 0),
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for Intel(R) Pentium(R) M processor 2.00GHz.
 */
static CPUMDBENTRY const g_Entry_Intel_Pentium_M_processor_2_00GHz =
{
    /*.pszName          = */ "Intel Pentium M processor 2.00GHz",
    /*.pszFullName      = */ "Intel(R) Pentium(R) M processor 2.00GHz",
    /*.enmVendor        = */ CPUMCPUVENDOR_INTEL,
    /*.uFamily          = */ 6,
    /*.uModel           = */ 13,
    /*.uStepping        = */ 6,
    /*.enmMicroarch     = */ kCpumMicroarch_Intel_P6_M_Dothan,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_UNKNOWN,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 32,
    /*.fMxCsrMask       = */ 0xffbf, ///< @todo check this
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_Intel_Pentium_M_processor_2_00GHz),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_Intel_Pentium_M_processor_2_00GHz)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /*.DefUnknownCpuId  = */ { 0x02b3b001, 0x000000f0, 0x00000000, 0x2c04307d },
    /*.fMsrMask         = */ UINT32_C(0x3fff),
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_Intel_Pentium_M_processor_2_00GHz)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_Intel_Pentium_M_processor_2_00GHz),
};

#endif /* !VBOX_CPUDB_Intel_Pentium_M_processor_2_00GHz_h */

