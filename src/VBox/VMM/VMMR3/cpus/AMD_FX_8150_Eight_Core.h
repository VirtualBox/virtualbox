/* $Id: AMD_FX_8150_Eight_Core.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "AMD FX-8150 Eight-Core".
 * Generated at 2013-12-09T11:27:04Z by VBoxCpuReport v4.3.51r91084 on win.amd64.
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

#ifndef VBOX_CPUDB_AMD_FX_8150_Eight_Core_h
#define VBOX_CPUDB_AMD_FX_8150_Eight_Core_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for AMD FX(tm)-8150 Eight-Core Processor.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_AMD_FX_8150_Eight_Core[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x0000000d, 0x68747541, 0x444d4163, 0x69746e65, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x00600f12, 0x02080800, 0x1e98220b, 0x178bfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000005, 0x00000000, 0x00000000, 0x00000040, 0x00000040, 0x00000003, 0x00000000, 0 },
    { 0x00000006, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0 },
    { 0x00000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x00000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000b, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x0000000d, 0x00000000, UINT32_MAX, 0x00000007, 0x00000340, 0x000003c0, 0x40000000, 0 },
    { 0x0000000d, 0x00000001, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000000, 0x00000000, 0x00000000, 0x8000001e, 0x68747541, 0x444d4163, 0x69746e65, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00600f12, 0x10000000, 0x01c9bfff, 0x2fd3fbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x80000002, 0x00000000, 0x00000000, 0x20444d41, 0x74285846, 0x382d296d, 0x20303531, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x68676945, 0x6f432d74, 0x50206572, 0x65636f72, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x726f7373, 0x20202020, 0x20202020, 0x00202020, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0xff20ff18, 0xff20ff30, 0x10040140, 0x40020140, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x64000000, 0x64004200, 0x08008140, 0x0040c140, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000003d9, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003030, 0x00000000, 0x00004007, 0x00000000, 0 },
    { 0x80000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000000a, 0x00000000, 0x00000000, 0x00000001, 0x00010000, 0x00000000, 0x000014ff, 0 },
    { 0x8000000b, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000000c, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000000d, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000000e, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000000f, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000011, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000012, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000013, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000014, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000015, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000016, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000017, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000018, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000019, 0x00000000, 0x00000000, 0xf020f018, 0x64000000, 0x00000000, 0x00000000, 0 },
    { 0x8000001a, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000001b, 0x00000000, 0x00000000, 0x000000ff, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000001c, 0x00000000, 0x00000000, 0x00000000, 0x80032013, 0x00010200, 0x8000000f, 0 },
    { 0x8000001d, 0x00000000, UINT32_MAX, 0x00000121, 0x00c0003f, 0x0000003f, 0x00000000, 0 },
    { 0x8000001d, 0x00000001, UINT32_MAX, 0x00004122, 0x0040003f, 0x000001ff, 0x00000000, 0 },
    { 0x8000001d, 0x00000002, UINT32_MAX, 0x00004143, 0x03c0003f, 0x000007ff, 0x00000001, 0 },
    { 0x8000001d, 0x00000003, UINT32_MAX, 0x0001c163, 0x0fc0003f, 0x000007ff, 0x00000001, 0 },
    { 0x8000001d, 0x00000004, UINT32_MAX, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000001e, 0x00000000, 0x00000000, 0x00000012, 0x00000101, 0x00000000, 0x00000000, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for AMD FX(tm)-8150 Eight-Core Processor.
 */
static CPUMMSRRANGE const g_aMsrRanges_AMD_FX_8150_Eight_Core[] =
{
    MAL(0x00000000, "IA32_P5_MC_ADDR", 0x00000402),
    MAL(0x00000001, "IA32_P5_MC_TYPE", 0x00000401),
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter),
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00800), 0, UINT64_C(0xffff0000000006ff)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, ReadOnly, 0, 0, 0), /* value=0x0 */
    MVO(0x0000008b, "BBL_CR_D3|BIOS_SIGN", 0x6000626),
    MFN(0x000000e7, "IA32_MPERF", Ia32MPerf, Ia32MPerf),
    MFN(0x000000e8, "IA32_APERF", Ia32APerf, Ia32APerf),
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x107, 0, 0), /* value=0x107 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, UINT64_C(0xfffffffffffffff8), 0), /* value=0x0 */
    MFX(0x0000017b, "IA32_MCG_CTL", Ia32McgCtl, Ia32McgCtl, 0, UINT64_C(0xffffffffffffff88), 0), /* value=0x77 */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, UINT64_C(0xffffffffffffff80), 0x40), /* value=0x0 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0x0 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0x0 */
    MFO(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp), /* value=0x0 */
    MFO(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp), /* value=0x0 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xffff000000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xffff0000000007ff)), /* value=0xffff`80000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xffff000000000ff8)), /* value=0x80000006 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xffff0000000007ff)), /* value=0xffff`c0000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xffff000000000ff8)), /* value=0xc0000006 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xffff0000000007ff)), /* value=0xffff`f0000800 */
    MFX(0x00000206, "IA32_MTRR_PHYS_BASE3", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x3, 0, UINT64_C(0xffff000000000ff8)), /* value=0xcdf00000 */
    MFX(0x00000207, "IA32_MTRR_PHYS_MASK3", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x3, 0, UINT64_C(0xffff0000000007ff)), /* value=0xffff`fff00800 */
    MFX(0x00000208, "IA32_MTRR_PHYS_BASE4", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x4, 0, UINT64_C(0xffff000000000ff8)), /* value=0xce000000 */
    MFX(0x00000209, "IA32_MTRR_PHYS_MASK4", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x4, 0, UINT64_C(0xffff0000000007ff)), /* value=0xffff`fe000800 */
    MFX(0x0000020a, "IA32_MTRR_PHYS_BASE5", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x5, 0, UINT64_C(0xffff000000000ff8)), /* value=0x0 */
    MFX(0x0000020b, "IA32_MTRR_PHYS_MASK5", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x5, 0, UINT64_C(0xffff0000000007ff)), /* value=0x0 */
    MFX(0x0000020c, "IA32_MTRR_PHYS_BASE6", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x6, 0, UINT64_C(0xffff000000000ff8)), /* value=0x0 */
    MFX(0x0000020d, "IA32_MTRR_PHYS_MASK6", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x6, 0, UINT64_C(0xffff0000000007ff)), /* value=0x0 */
    MFX(0x0000020e, "IA32_MTRR_PHYS_BASE7", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x7, 0, UINT64_C(0xffff000000000ff8)), /* value=0x0 */
    MFX(0x0000020f, "IA32_MTRR_PHYS_MASK7", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x7, 0, UINT64_C(0xffff0000000007ff)), /* value=0x0 */
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
    RFN(0x00000400, 0x0000041b, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0x4d01, 0xfe, UINT64_C(0xffffffffffff8200)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x230010`00000000 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0xfffff800`02ed0bc0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0xfffff800`02ed0900 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x4700 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0xfffe0000 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0xfffff880`02f65000 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x7ff`fffde000 */
    MFX(0xc0000103, "AMD64_TSC_AUX", Amd64TscAux, Amd64TscAux, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0xc0000104, "AMD_15H_TSC_RATE", AmdFam15hTscRate, AmdFam15hTscRate, 0, 0, UINT64_C(0xffffff0000000000)), /* value=0x1`00000000 */
    MFX(0xc0000105, "AMD_15H_LWP_CFG", AmdFam15hLwpCfg, AmdFam15hLwpCfg, 0, UINT64_C(0xffff000000000001), 0x7ffffff0), /* value=0x0 */
    MFX(0xc0000106, "AMD_15H_LWP_CBADDR", AmdFam15hLwpCbAddr, AmdFam15hLwpCbAddr, 0, 0, UINT64_MAX), /* value=0x0 */
    RSN(0xc0000408, 0xc0000409, "AMD_10H_MC4_MISCn", AmdFam10hMc4MiscN, AmdFam10hMc4MiscN, 0, UINT64_C(0xff00f000ffffffff), 0),
    RVI(0xc000040a, 0xc000040f, "AMD_10H_MC4_MISCn", 0),
    MAL(0xc0010000, "AMD_K8_PERF_CTL_0", 0xc0010200),
    MAL(0xc0010001, "AMD_K8_PERF_CTL_1", 0xc0010202),
    MAL(0xc0010002, "AMD_K8_PERF_CTL_2", 0xc0010204),
    MAL(0xc0010003, "AMD_K8_PERF_CTL_3", 0xc0010206),
    MAL(0xc0010004, "AMD_K8_PERF_CTR_0", 0xc0010201),
    MAL(0xc0010005, "AMD_K8_PERF_CTR_1", 0xc0010203),
    MAL(0xc0010006, "AMD_K8_PERF_CTR_2", 0xc0010205),
    MAL(0xc0010007, "AMD_K8_PERF_CTR_3", 0xc0010207),
    MFX(0xc0010010, "AMD_K8_SYS_CFG", AmdK8SysCfg, AmdK8SysCfg, 0x740000, UINT64_C(0xffffffffff82ffff), 0), /* value=0x740000 */
    MFX(0xc0010015, "AMD_K8_HW_CFG", AmdK8HwCr, AmdK8HwCr, 0, UINT64_C(0xffffffff01006020), 0), /* value=0x1001031 */
    MFW(0xc0010016, "AMD_K8_IORR_BASE_0", AmdK8IorrBaseN, AmdK8IorrBaseN, UINT64_C(0xffff000000000fe7)), /* value=0x0 */
    MFW(0xc0010017, "AMD_K8_IORR_MASK_0", AmdK8IorrMaskN, AmdK8IorrMaskN, UINT64_C(0xffff0000000007ff)), /* value=0x0 */
    MFX(0xc0010018, "AMD_K8_IORR_BASE_1", AmdK8IorrBaseN, AmdK8IorrBaseN, 0x1, UINT64_C(0xffff000000000fe7), 0), /* value=0x0 */
    MFX(0xc0010019, "AMD_K8_IORR_MASK_1", AmdK8IorrMaskN, AmdK8IorrMaskN, 0x1, UINT64_C(0xffff0000000007ff), 0), /* value=0x0 */
    MFW(0xc001001a, "AMD_K8_TOP_MEM", AmdK8TopOfMemN, AmdK8TopOfMemN, UINT64_C(0xffff0000007fffff)), /* value=0xd0000000 */
    MFX(0xc001001d, "AMD_K8_TOP_MEM2", AmdK8TopOfMemN, AmdK8TopOfMemN, 0x1, UINT64_C(0xffff0000007fffff), 0), /* value=0x4`2f000000 */
    MFN(0xc001001f, "AMD_K8_NB_CFG1", AmdK8NbCfg1, AmdK8NbCfg1), /* value=0x400000`00810008 */
    MFN(0xc0010020, "AMD_K8_PATCH_LOADER", WriteOnly, AmdK8PatchLoader),
    MFX(0xc0010022, "AMD_K8_MC_XCPT_REDIR", AmdK8McXcptRedir, AmdK8McXcptRedir, 0, UINT64_C(0xffffffffffff0000), 0), /* value=0x0 */
    MVO(0xc0010028, "AMD_K8_UNK_c001_0028", 0),
    MVO(0xc0010029, "AMD_K8_UNK_c001_0029", 0),
    MVO(0xc001002a, "AMD_K8_UNK_c001_002a", 0),
    MVO(0xc001002b, "AMD_K8_UNK_c001_002b", 0),
    MVO(0xc001002c, "AMD_K8_UNK_c001_002c", 0),
    MVO(0xc001002d, "AMD_K8_UNK_c001_002d", 0),
    RFN(0xc0010030, 0xc0010035, "AMD_K8_CPU_NAME_n", AmdK8CpuNameN, AmdK8CpuNameN),
    MFX(0xc001003e, "AMD_K8_HTC", AmdK8HwThermalCtrl, AmdK8HwThermalCtrl, 0x664c0005, UINT64_C(0xffffffff90008838), 0), /* value=0x664c0005 */
    MFX(0xc001003f, "AMD_K8_STC", AmdK8SwThermalCtrl, AmdK8SwThermalCtrl, 0, UINT64_C(0xffffffff9fffffdf), 0), /* value=0x60000000 */
    MVO(0xc0010043, "AMD_K8_THERMTRIP_STATUS", 0x20),
    MFX(0xc0010044, "AMD_K8_MC_CTL_MASK_0", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x0, UINT64_C(0xfffffffffffffc00), 0), /* value=0x0 */
    MFX(0xc0010045, "AMD_K8_MC_CTL_MASK_1", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x1, UINT64_C(0xffffffffff004d01), 0), /* value=0x48080 */
    MFX(0xc0010046, "AMD_K8_MC_CTL_MASK_2", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x2, UINT64_C(0xffffffffffff8000), 0), /* value=0x0 */
    MFX(0xc0010047, "AMD_K8_MC_CTL_MASK_3", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x3, UINT64_MAX, 0), /* value=0x0 */
    MFX(0xc0010048, "AMD_K8_MC_CTL_MASK_4", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x4, ~(uint64_t)UINT32_MAX, 0), /* value=0x780400 */
    MFX(0xc0010049, "AMD_K8_MC_CTL_MASK_5", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x5, UINT64_C(0xffffffffffffe000), 0), /* value=0x0 */
    MFX(0xc001004a, "AMD_K8_MC_CTL_MASK_6", AmdK8McCtlMaskN, AmdK8McCtlMaskN, 0x6, UINT64_C(0xffffffffffffffc0), 0), /* value=0x0 */
    RFN(0xc0010050, 0xc0010053, "AMD_K8_SMI_ON_IO_TRAP_n", AmdK8SmiOnIoTrapN, AmdK8SmiOnIoTrapN),
    MFX(0xc0010054, "AMD_K8_SMI_ON_IO_TRAP_CTL_STS", AmdK8SmiOnIoTrapCtlSts, AmdK8SmiOnIoTrapCtlSts, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0xc0010055, "AMD_K8_INT_PENDING_MSG", AmdK8IntPendingMessage, AmdK8IntPendingMessage, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x20000800 */
    MFX(0xc0010056, "AMD_K8_SMI_TRIGGER_IO_CYCLE", AmdK8SmiTriggerIoCycle, AmdK8SmiTriggerIoCycle, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x2000000 */
    MFX(0xc0010058, "AMD_10H_MMIO_CFG_BASE_ADDR", AmdFam10hMmioCfgBaseAddr, AmdFam10hMmioCfgBaseAddr, 0, UINT64_C(0xffff0000000fffc0), 0), /* value=0xe0000021 */
    MFX(0xc0010059, "AMD_10H_TRAP_CTL?", AmdFam10hTrapCtlMaybe, AmdFam10hTrapCtlMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MVX(0xc001005a, "AMD_10H_UNK_c001_005a", 0, 0, 0),
    MVX(0xc001005b, "AMD_10H_UNK_c001_005b", 0, 0, 0),
    MVX(0xc001005c, "AMD_10H_UNK_c001_005c", 0, 0, 0),
    MVX(0xc001005d, "AMD_10H_UNK_c001_005d", 0, 0, 0),
    MVO(0xc0010060, "AMD_K8_BIST_RESULT", 0),
    MFX(0xc0010061, "AMD_10H_P_ST_CUR_LIM", AmdFam10hPStateCurLimit, ReadOnly, 0x40, 0, 0), /* value=0x40 */
    MFX(0xc0010062, "AMD_10H_P_ST_CTL", AmdFam10hPStateControl, AmdFam10hPStateControl, 0, 0, UINT64_C(0xfffffffffffffff8)), /* value=0x0 */
    MFX(0xc0010063, "AMD_10H_P_ST_STS", AmdFam10hPStateStatus, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFX(0xc0010064, "AMD_10H_P_ST_0", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x800001b10000161a), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x800001b1`0000161a */
    MFX(0xc0010065, "AMD_10H_P_ST_1", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x800001b100001a17), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x800001b1`00001a17 */
    MFX(0xc0010066, "AMD_10H_P_ST_2", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x8000017300003014), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x80000173`00003014 */
    MFX(0xc0010067, "AMD_10H_P_ST_3", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x8000016300003a11), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x80000163`00003a11 */
    MFX(0xc0010068, "AMD_10H_P_ST_4", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x8000014900004c0b), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x80000149`00004c0b */
    MFX(0xc0010069, "AMD_10H_P_ST_5", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x8000013100006205), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x80000131`00006205 */
    MFX(0xc001006a, "AMD_10H_P_ST_6", AmdFam10hPStateN, AmdFam10hPStateN, UINT64_C(0x800001200000724c), UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x80000120`0000724c */
    MFX(0xc001006b, "AMD_10H_P_ST_7", AmdFam10hPStateN, AmdFam10hPStateN, 0, UINT64_C(0x7ffffc00ffbf0000), 0), /* value=0x0 */
    MFX(0xc0010070, "AMD_10H_COFVID_CTL", AmdFam10hCofVidControl, AmdFam10hCofVidControl, 0x40011a17, UINT64_C(0xffffffff00b80000), 0), /* value=0x40011a17 */
    MFX(0xc0010071, "AMD_10H_COFVID_STS", AmdFam10hCofVidStatus, AmdFam10hCofVidStatus, UINT64_C(0x18000064006724c), UINT64_MAX, 0), /* value=0x1800006`4006724c */
    MFX(0xc0010073, "AMD_10H_C_ST_IO_BASE_ADDR", AmdFam10hCStateIoBaseAddr, AmdFam10hCStateIoBaseAddr, 0, UINT64_C(0xffffffffffff0000), 0), /* value=0x814 */
    MFX(0xc0010074, "AMD_10H_CPU_WD_TMR_CFG", AmdFam10hCpuWatchdogTimer, AmdFam10hCpuWatchdogTimer, 0, UINT64_C(0xffffffffffffff80), 0), /* value=0x0 */
    MFX(0xc0010111, "AMD_K8_SMM_BASE", AmdK8SmmBase, AmdK8SmmBase, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0xcdef8800 */
    MFX(0xc0010112, "AMD_K8_SMM_ADDR", AmdK8SmmAddr, AmdK8SmmAddr, 0, UINT64_C(0xffff00000001ffff), 0), /* value=0xcdf00000 */
    MFX(0xc0010113, "AMD_K8_SMM_MASK", AmdK8SmmMask, AmdK8SmmMask, 0, UINT64_C(0xffff0000000188c0), 0), /* value=0xffff`fff00003 */
    MFX(0xc0010114, "AMD_K8_VM_CR", AmdK8VmCr, AmdK8VmCr, 0, ~(uint64_t)UINT32_MAX, UINT32_C(0xffffffe0)), /* value=0x8 */
    MFX(0xc0010115, "AMD_K8_IGNNE", AmdK8IgnNe, AmdK8IgnNe, 0, ~(uint64_t)UINT32_MAX, UINT32_C(0xfffffffe)), /* value=0x0 */
    MFX(0xc0010117, "AMD_K8_VM_HSAVE_PA", AmdK8VmHSavePa, AmdK8VmHSavePa, 0, 0, UINT64_C(0xffff000000000fff)), /* value=0x0 */
    MFN(0xc0010118, "AMD_10H_VM_LOCK_KEY", AmdFam10hVmLockKey, AmdFam10hVmLockKey), /* value=0x0 */
    MFN(0xc0010119, "AMD_10H_SSM_LOCK_KEY", AmdFam10hSmmLockKey, AmdFam10hSmmLockKey), /* value=0x0 */
    MFX(0xc001011a, "AMD_10H_LOCAL_SMI_STS", AmdFam10hLocalSmiStatus, AmdFam10hLocalSmiStatus, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0xc0010140, "AMD_10H_OSVW_ID_LEN", AmdFam10hOsVisWrkIdLength, AmdFam10hOsVisWrkIdLength, 0x4, 0, 0), /* value=0x4 */
    MFN(0xc0010141, "AMD_10H_OSVW_STS", AmdFam10hOsVisWrkStatus, AmdFam10hOsVisWrkStatus), /* value=0x0 */
    MFX(0xc0010200, "AMD_K8_PERF_CTL_0", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x0, UINT64_C(0xfffffcf000200000), 0), /* value=0x0 */
    MFX(0xc0010201, "AMD_K8_PERF_CTR_0", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x0, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010202, "AMD_K8_PERF_CTL_1", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x1, UINT64_C(0xfffffcf000200000), 0), /* value=0x0 */
    MFX(0xc0010203, "AMD_K8_PERF_CTR_1", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x1, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010204, "AMD_K8_PERF_CTL_2", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x2, UINT64_C(0xfffffcf000200000), 0), /* value=0x0 */
    MFX(0xc0010205, "AMD_K8_PERF_CTR_2", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x2, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010206, "AMD_K8_PERF_CTL_3", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x3, UINT64_C(0xfffffcf000200000), 0), /* value=0x0 */
    MFX(0xc0010207, "AMD_K8_PERF_CTR_3", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x3, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010208, "AMD_K8_PERF_CTL_4", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x4, UINT64_C(0xfffffcf000200000), 0), /* value=0x0 */
    MFX(0xc0010209, "AMD_K8_PERF_CTR_4", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x4, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc001020a, "AMD_K8_PERF_CTL_5", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x5, UINT64_C(0xfffffcf000200000), 0), /* value=0x0 */
    MFX(0xc001020b, "AMD_K8_PERF_CTR_5", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x5, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010240, "AMD_15H_NB_PERF_CTL_0", AmdFam15hNorthbridgePerfCtlN, AmdFam15hNorthbridgePerfCtlN, 0x0, UINT64_C(0xfffffe00ffa70000), 0), /* value=0x0 */
    MFX(0xc0010241, "AMD_15H_NB_PERF_CTR_0", AmdFam15hNorthbridgePerfCtrN, AmdFam15hNorthbridgePerfCtrN, 0x0, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010242, "AMD_15H_NB_PERF_CTL_1", AmdFam15hNorthbridgePerfCtlN, AmdFam15hNorthbridgePerfCtlN, 0x1, UINT64_C(0xfffffe00ffa70000), 0), /* value=0x0 */
    MFX(0xc0010243, "AMD_15H_NB_PERF_CTR_1", AmdFam15hNorthbridgePerfCtrN, AmdFam15hNorthbridgePerfCtrN, 0x1, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010244, "AMD_15H_NB_PERF_CTL_2", AmdFam15hNorthbridgePerfCtlN, AmdFam15hNorthbridgePerfCtlN, 0x2, UINT64_C(0xfffffe00ffa70000), 0), /* value=0x0 */
    MFX(0xc0010245, "AMD_15H_NB_PERF_CTR_2", AmdFam15hNorthbridgePerfCtrN, AmdFam15hNorthbridgePerfCtrN, 0x2, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0010246, "AMD_15H_NB_PERF_CTL_3", AmdFam15hNorthbridgePerfCtlN, AmdFam15hNorthbridgePerfCtlN, 0x3, UINT64_C(0xfffffe00ffa70000), 0), /* value=0x0 */
    MFX(0xc0010247, "AMD_15H_NB_PERF_CTR_3", AmdFam15hNorthbridgePerfCtrN, AmdFam15hNorthbridgePerfCtrN, 0x3, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFX(0xc0011000, "AMD_K7_MCODE_CTL", AmdK7MicrocodeCtl, AmdK7MicrocodeCtl, 0x30000, ~(uint64_t)UINT32_MAX, 0x204), /* value=0x30000 */
    MFX(0xc0011001, "AMD_K7_APIC_CLUSTER_ID", AmdK7ClusterIdMaybe, AmdK7ClusterIdMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0xc0011003, "AMD_K8_CPUID_CTL_STD06", AmdK8CpuIdCtlStd06hEcx, AmdK8CpuIdCtlStd06hEcx, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x1 */
    MFN(0xc0011004, "AMD_K8_CPUID_CTL_STD01", AmdK8CpuIdCtlStd01hEdcx, AmdK8CpuIdCtlStd01hEdcx), /* value=0x1e98220b`178bfbff */
    MFN(0xc0011005, "AMD_K8_CPUID_CTL_EXT01", AmdK8CpuIdCtlExt01hEdcx, AmdK8CpuIdCtlExt01hEdcx), /* value=0x1c9ffff`2fd3fbff */
    MFX(0xc0011006, "AMD_K7_DEBUG_STS?", AmdK7DebugStatusMaybe, AmdK7DebugStatusMaybe, 0, UINT64_C(0xffffffff00000080), 0), /* value=0x10 */
    MFN(0xc0011007, "AMD_K7_BH_TRACE_BASE?", AmdK7BHTraceBaseMaybe, AmdK7BHTraceBaseMaybe), /* value=0x0 */
    MFN(0xc0011008, "AMD_K7_BH_TRACE_PTR?", AmdK7BHTracePtrMaybe, AmdK7BHTracePtrMaybe), /* value=0x0 */
    MFN(0xc0011009, "AMD_K7_BH_TRACE_LIM?", AmdK7BHTraceLimitMaybe, AmdK7BHTraceLimitMaybe), /* value=0x0 */
    MFX(0xc001100a, "AMD_K7_HDT_CFG?", AmdK7HardwareDebugToolCfgMaybe, AmdK7HardwareDebugToolCfgMaybe, 0, UINT64_C(0xffffffff00800000), 0), /* value=0x0 */
    MFX(0xc001100b, "AMD_K7_FAST_FLUSH_COUNT?", AmdK7FastFlushCountMaybe, AmdK7FastFlushCountMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x7c0 */
    MFX(0xc001100c, "AMD_K7_NODE_ID", AmdK7NodeId, AmdK7NodeId, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x80 */
    MVX(0xc001100e, "AMD_K8_WRMSR_BP?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc001100f, "AMD_K8_WRMSR_BP_MASK?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc0011010, "AMD_K8_BH_TRACE_CTL?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc0011011, "AMD_K8_BH_TRACE_USRD?", 0, 0, 0), /* value=0xfffffcf0`093634f0 */
    MVI(0xc0011012, "AMD_K7_UNK_c001_1012", UINT32_MAX),
    MVI(0xc0011013, "AMD_K7_UNK_c001_1013", UINT64_MAX),
    MVX(0xc0011014, "AMD_K8_XCPT_BP_RIP?", 0, 0, 0),
    MVX(0xc0011015, "AMD_K8_XCPT_BP_RIP_MASK?", 0, 0, 0),
    MVX(0xc0011016, "AMD_K8_COND_HDT_VAL?", 0, 0, 0),
    MVX(0xc0011017, "AMD_K8_COND_HDT_VAL_MASK?", 0, 0, 0),
    MVX(0xc0011018, "AMD_K8_XCPT_BP_CTL?", 0, 0, 0),
    MVX(0xc001101d, "AMD_K8_NB_BIST?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVI(0xc001101e, "AMD_K8_THERMTRIP_2?", 0x20), /* Villain? */
    MVX(0xc001101f, "AMD_K8_NB_CFG?", UINT64_C(0x40000000810008), 0, 0),
    MFX(0xc0011020, "AMD_K7_LS_CFG", AmdK7LoadStoreCfg, AmdK7LoadStoreCfg, 0, UINT64_C(0x3fffedafbffe2a), 0), /* value=0x0 */
    MFW(0xc0011021, "AMD_K7_IC_CFG", AmdK7InstrCacheCfg, AmdK7InstrCacheCfg, UINT64_C(0xffffff0000000000)), /* value=0x0 */
    MFX(0xc0011022, "AMD_K7_DC_CFG", AmdK7DataCacheCfg, AmdK7DataCacheCfg, 0, UINT64_C(0x1ffffbfffff13e0), 0), /* value=0x0 */
    MFX(0xc0011023, "AMD_15H_CU_CFG", AmdFam15hCombUnitCfg, AmdFam15hCombUnitCfg, 0x220, UINT64_C(0x3ff03c760042000), 0), /* value=0x80004000`00000220 */
    MFX(0xc0011024, "AMD_K7_DEBUG_CTL_2?", AmdK7DebugCtl2Maybe, AmdK7DebugCtl2Maybe, 0, UINT64_C(0xfffffffffffffe04), 0), /* value=0x0 */
    MFN(0xc0011025, "AMD_K7_DR0_DATA_MATCH?", AmdK7Dr0DataMatchMaybe, AmdK7Dr0DataMatchMaybe), /* value=0x0 */
    MFN(0xc0011026, "AMD_K7_DR0_DATA_MATCH?", AmdK7Dr0DataMaskMaybe, AmdK7Dr0DataMaskMaybe), /* value=0x0 */
    MFX(0xc0011027, "AMD_K7_DR0_ADDR_MASK", AmdK7DrXAddrMaskN, AmdK7DrXAddrMaskN, 0x0, UINT64_C(0xfffffffffffff000), 0), /* value=0x0 */
    MFX(0xc0011028, "AMD_15H_FP_CFG", AmdFam15hFpuCfg, AmdFam15hFpuCfg, 0, UINT64_C(0xffffe000000000ff), 0), /* value=0x40`e91d0000 */
    MFX(0xc0011029, "AMD_15H_DC_CFG", AmdFam15hDecoderCfg, AmdFam15hDecoderCfg, 0, UINT64_C(0xffffffffc0188001), 0), /* value=0x488400 */
    MFX(0xc001102a, "AMD_15H_CU_CFG2", AmdFam15hCombUnitCfg2, AmdFam15hCombUnitCfg2, 0, UINT64_C(0xfffbfb8ff2fc623f), 0), /* value=0x40040`00000cc0 */
    MFX(0xc001102b, "AMD_15H_CU_CFG3", AmdFam15hCombUnitCfg3, AmdFam15hCombUnitCfg3, 0, UINT64_C(0xffe0027afff00000), 0), /* value=0x33400`00002b93 */
    MFX(0xc001102c, "AMD_15H_EX_CFG", AmdFam15hExecUnitCfg, AmdFam15hExecUnitCfg, 0x7aac0, UINT64_C(0xffb0c003fbe00024), 0), /* value=0x400`0007aac0 */
    MFX(0xc0011030, "AMD_10H_IBS_FETCH_CTL", AmdFam10hIbsFetchCtl, AmdFam10hIbsFetchCtl, 0, UINT64_C(0xfdfeffffffff0000), 0), /* value=0x0 */
    MFI(0xc0011031, "AMD_10H_IBS_FETCH_LIN_ADDR", AmdFam10hIbsFetchLinAddr), /* value=0x0 */
    MFI(0xc0011032, "AMD_10H_IBS_FETCH_PHYS_ADDR", AmdFam10hIbsFetchPhysAddr), /* value=0x0 */
    MFX(0xc0011033, "AMD_10H_IBS_OP_EXEC_CTL", AmdFam10hIbsOpExecCtl, AmdFam10hIbsOpExecCtl, 0, UINT64_C(0xf8000000f8010000), 0), /* value=0x0 */
    MFN(0xc0011034, "AMD_10H_IBS_OP_RIP", AmdFam10hIbsOpRip, AmdFam10hIbsOpRip), /* value=0x0 */
    MFX(0xc0011035, "AMD_10H_IBS_OP_DATA", AmdFam10hIbsOpData, AmdFam10hIbsOpData, 0, UINT64_C(0xffffffc000000000), 0), /* value=0x0 */
    MFX(0xc0011036, "AMD_10H_IBS_OP_DATA2", AmdFam10hIbsOpData2, AmdFam10hIbsOpData2, 0, UINT64_C(0xffffffffffffffc8), 0), /* value=0x0 */
    MFX(0xc0011037, "AMD_10H_IBS_OP_DATA3", AmdFam10hIbsOpData3, AmdFam10hIbsOpData3, 0, UINT64_C(0xffff0000fff00400), 0), /* value=0x0 */
    MFN(0xc0011038, "AMD_10H_IBS_DC_LIN_ADDR", AmdFam10hIbsDcLinAddr, AmdFam10hIbsDcLinAddr), /* value=0x0 */
    MFX(0xc0011039, "AMD_10H_IBS_DC_PHYS_ADDR", AmdFam10hIbsDcPhysAddr, AmdFam10hIbsDcPhysAddr, 0, UINT64_C(0xffff000000000000), 0), /* value=0x0 */
    MFO(0xc001103a, "AMD_10H_IBS_CTL", AmdFam10hIbsCtl), /* value=0x100 */
    MFN(0xc001103b, "AMD_14H_IBS_BR_TARGET", AmdFam14hIbsBrTarget, AmdFam14hIbsBrTarget), /* value=0x0 */
    MVX(0xc0011040, "AMD_15H_UNK_c001_1040", 0, UINT64_C(0xffe0000000000003), 0),
    MVX(0xc0011041, "AMD_15H_UNK_c001_1041", UINT64_C(0x99dd57b219), 0xa0c820, 0),
    MVX(0xc0011042, "AMD_15H_UNK_c001_1042", 0, 0, 0),
    MVX(0xc0011043, "AMD_15H_UNK_c001_1043", UINT64_C(0x300000438), 0, 0),
    MVX(0xc0011044, "AMD_15H_UNK_c001_1044", UINT64_C(0x300000438), 0, 0),
    MVX(0xc0011045, "AMD_15H_UNK_c001_1045", UINT64_C(0x300000420), 0, 0),
    MVX(0xc0011046, "AMD_15H_UNK_c001_1046", UINT64_C(0x300000420), 0, 0),
    MVX(0xc0011047, "AMD_15H_UNK_c001_1047", 0, UINT64_C(0xffff000000000000), 0),
    MVX(0xc0011048, "AMD_15H_UNK_c001_1048", 0xc000001, UINT64_C(0xffff000000000000), 0),
    MVX(0xc0011049, "AMD_15H_UNK_c001_1049", 0, UINT64_C(0xffff000000000000), 0),
    MVX(0xc001104a, "AMD_15H_UNK_c001_104a", 0, UINT64_C(0xffff000000000000), 0),
    MVX(0xc001104b, "AMD_15H_UNK_c001_104b", 0, 0, 0),
    MVX(0xc001104c, "AMD_15H_UNK_c001_104c", 0, 0, 0),
    MVX(0xc001104d, "AMD_15H_UNK_c001_104d", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc001104e, "AMD_15H_UNK_c001_104e", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc001104f, "AMD_15H_UNK_c001_104f", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011050, "AMD_15H_UNK_c001_1050", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011051, "AMD_15H_UNK_c001_1051", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011052, "AMD_15H_UNK_c001_1052", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011053, "AMD_15H_UNK_c001_1053", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011054, "AMD_15H_UNK_c001_1054", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011055, "AMD_15H_UNK_c001_1055", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011056, "AMD_15H_UNK_c001_1056", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011057, "AMD_15H_UNK_c001_1057", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011058, "AMD_15H_UNK_c001_1058", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc0011059, "AMD_15H_UNK_c001_1059", 0, UINT64_C(0xfffffc0000000000), 0),
    MVX(0xc001105a, "AMD_15H_UNK_c001_105a", UINT64_C(0x3060c183060c183), UINT64_C(0x8000000000000000), 0),
    MVX(0xc001105b, "AMD_15H_UNK_c001_105b", UINT64_C(0x318c6318c60c183), UINT64_C(0xe000000000000000), 0),
    MVX(0xc001105c, "AMD_15H_UNK_c001_105c", 0, UINT64_C(0xff00000000000000), 0),
    MVX(0xc001105d, "AMD_15H_UNK_c001_105d", 0, UINT64_C(0xff00000000000000), 0),
    MVX(0xc001105e, "AMD_15H_UNK_c001_105e", 0, UINT64_C(0xfffffffffffffc00), 0),
    MVX(0xc001105f, "AMD_15H_UNK_c001_105f", 0, UINT64_C(0xffff000000000000), 0),
    MVX(0xc0011060, "AMD_15H_UNK_c001_1060", 0, UINT64_C(0xffff000000000000), 0),
    MVX(0xc0011061, "AMD_15H_UNK_c001_1061", 0, 0, 0),
    MVX(0xc0011062, "AMD_15H_UNK_c001_1062", 0, UINT64_C(0xffffffffffffe000), 0),
    MVX(0xc0011063, "AMD_15H_UNK_c001_1063", 0, UINT64_C(0xfffffffffffe4000), 0),
    MVX(0xc0011064, "AMD_15H_UNK_c001_1064", 0x1, UINT64_C(0xfffffffffffff000), 0),
    MVX(0xc0011065, "AMD_15H_UNK_c001_1065", 0x1, UINT64_C(0xfffffffff0000000), 0),
    MVX(0xc0011066, "AMD_15H_UNK_c001_1066", 0, 0, 0),
    MVX(0xc0011067, "AMD_15H_UNK_c001_1067", 0x1, UINT64_C(0xffffffffffffff80), 0),
    MVX(0xc0011068, "AMD_15H_UNK_c001_1068", 0, 0, 0),
    MVX(0xc0011069, "AMD_15H_UNK_c001_1069", 0, UINT64_C(0xffffffffffff0000), 0),
    MVX(0xc001106a, "AMD_15H_UNK_c001_106a", 0x1, 0, 0),
    MVX(0xc001106b, "AMD_15H_UNK_c001_106b", 0, UINT64_C(0xfffffffffffffff0), 0),
    MVX(0xc001106c, "AMD_15H_UNK_c001_106c", 0x1, UINT64_C(0xffffffffffff0000), 0),
    MVX(0xc001106d, "AMD_15H_UNK_c001_106d", 0x1, UINT64_C(0xf000000000000080), 0),
    MVX(0xc001106e, "AMD_15H_UNK_c001_106e", 0x1, UINT64_C(0xffffffffffff0000), 0),
    MVX(0xc001106f, "AMD_15H_UNK_c001_106f", 0x1, UINT64_C(0xfffffffffffff800), 0),
    MVI(0xc0011070, "AMD_15H_UNK_c001_1070", UINT64_C(0x20000000000)),
    MVX(0xc0011071, "AMD_15H_UNK_c001_1071", 0x400000, UINT64_C(0xffffffff01ffffff), 0),
    MVI(0xc0011072, "AMD_15H_UNK_c001_1072", UINT64_C(0x101592c00000021)),
    MVI(0xc0011073, "AMD_15H_UNK_c001_1073", UINT64_C(0xec541c0050000000)),
    MVX(0xc0011080, "AMD_15H_UNK_c001_1080", 0, 0, 0),
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for AMD FX(tm)-8150 Eight-Core Processor.
 */
static CPUMDBENTRY const g_Entry_AMD_FX_8150_Eight_Core =
{
    /*.pszName          = */ "AMD FX-8150 Eight-Core",
    /*.pszFullName      = */ "AMD FX(tm)-8150 Eight-Core Processor",
    /*.enmVendor        = */ CPUMCPUVENDOR_AMD,
    /*.uFamily          = */ 21,
    /*.uModel           = */ 1,
    /*.uStepping        = */ 2,
    /*.enmMicroarch     = */ kCpumMicroarch_AMD_15h_Bulldozer,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_UNKNOWN,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 48,
    /*.fMxCsrMask       = */ 0x2ffff,
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_AMD_FX_8150_Eight_Core),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_AMD_FX_8150_Eight_Core)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_DEFAULTS,
    /*.DefUnknownCpuId  = */ { 0x00000000, 0x00000000, 0x00000000, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_AMD_FX_8150_Eight_Core)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_AMD_FX_8150_Eight_Core),
};

#endif /* !VBOX_CPUDB_AMD_FX_8150_Eight_Core_h */

