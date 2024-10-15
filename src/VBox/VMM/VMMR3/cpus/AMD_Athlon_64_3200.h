/* $Id: AMD_Athlon_64_3200.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU database entry "AMD Athlon 64 3200+".
 * Generated at 2013-07-12T02:09:05Z by VBoxCpuReport v4.3.53r91376 on win.x86.
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

#ifndef VBOX_CPUDB_AMD_Athlon_64_3200_h
#define VBOX_CPUDB_AMD_Athlon_64_3200_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef CPUM_DB_STANDALONE
/**
 * CPUID leaves for AMD Athlon(tm) 64 Processor 3200+.
 */
static CPUMCPUIDLEAF const g_aCpuIdLeaves_AMD_Athlon_64_3200[] =
{
    { 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x68747541, 0x444d4163, 0x69746e65, 0 },
    { 0x00000001, 0x00000000, 0x00000000, 0x00000f48, 0x00000800, 0x00000000, 0x078bfbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC_ID | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x80000000, 0x00000000, 0x00000000, 0x80000018, 0x68747541, 0x444d4163, 0x69746e65, 0 },
    { 0x80000001, 0x00000000, 0x00000000, 0x00000f48, 0x0000010a, 0x00000000, 0xe1d3fbff, 0 | CPUMCPUIDLEAF_F_CONTAINS_APIC },
    { 0x80000002, 0x00000000, 0x00000000, 0x20444d41, 0x6c687441, 0x74286e6f, 0x3620296d, 0 },
    { 0x80000003, 0x00000000, 0x00000000, 0x72502034, 0x7365636f, 0x20726f73, 0x30303233, 0 },
    { 0x80000004, 0x00000000, 0x00000000, 0x0000002b, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000005, 0x00000000, 0x00000000, 0xff08ff08, 0xff20ff20, 0x40020140, 0x40020140, 0 },
    { 0x80000006, 0x00000000, 0x00000000, 0x00000000, 0x42004200, 0x04008140, 0x00000000, 0 },
    { 0x80000007, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000000f, 0 },
    { 0x80000008, 0x00000000, 0x00000000, 0x00003028, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x80000009, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
    { 0x8000000a, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0 },
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
    { 0x8fffffff, 0x00000000, 0x00000000, 0x53275449, 0x4d414820, 0x2052454d, 0x454d4954, 0 },
};
#endif /* !CPUM_DB_STANDALONE */


#ifndef CPUM_DB_STANDALONE
/**
 * MSR ranges for AMD Athlon(tm) 64 Processor 3200+.
 */
static CPUMMSRRANGE const g_aMsrRanges_AMD_Athlon_64_3200[] =
{
    MAL(0x00000000, "IA32_P5_MC_ADDR", 0x00000402),
    MAL(0x00000001, "IA32_P5_MC_TYPE", 0x00000401),
    MFN(0x00000010, "IA32_TIME_STAMP_COUNTER", Ia32TimestampCounter, Ia32TimestampCounter), /* value=0x28`4505cb65 */
    MFX(0x0000001b, "IA32_APIC_BASE", Ia32ApicBase, Ia32ApicBase, UINT32_C(0xfee00900), 0, UINT64_C(0xffffff00000006ff)),
    MFX(0x0000002a, "EBL_CR_POWERON", IntelEblCrPowerOn, ReadOnly, 0, 0, 0), /* value=0x0 */
    MFO(0x0000008b, "AMD_K8_PATCH_LEVEL", AmdK8PatchLevel), /* value=0x39 */
    MFX(0x000000fe, "IA32_MTRRCAP", Ia32MtrrCap, ReadOnly, 0x508, 0, 0), /* value=0x508 */
    MFX(0x00000174, "IA32_SYSENTER_CS", Ia32SysEnterCs, Ia32SysEnterCs, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x8 */
    MFX(0x00000175, "IA32_SYSENTER_ESP", Ia32SysEnterEsp, Ia32SysEnterEsp, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x8059e000 */
    MFX(0x00000176, "IA32_SYSENTER_EIP", Ia32SysEnterEip, Ia32SysEnterEip, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x81872950 */
    MFX(0x00000179, "IA32_MCG_CAP", Ia32McgCap, ReadOnly, 0x105, 0, 0), /* value=0x105 */
    MFX(0x0000017a, "IA32_MCG_STATUS", Ia32McgStatus, Ia32McgStatus, 0, UINT64_C(0xfffffffffffffff8), 0), /* value=0x0 */
    MFX(0x0000017b, "IA32_MCG_CTL", Ia32McgCtl, Ia32McgCtl, 0, UINT64_C(0xffffffffffffffe0), 0), /* value=0x1f */
    MFX(0x000001d9, "IA32_DEBUGCTL", Ia32DebugCtl, Ia32DebugCtl, 0, UINT64_C(0xffffffffffffff80), 0x40), /* value=0x0 */
    MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP", P6LastBranchFromIp), /* value=0xffffffed`bf1be178 */
    MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP", P6LastBranchToIp), /* value=0xffff7f49`bf1bedec */
    MFO(0x000001dd, "P6_LAST_INT_FROM_IP", P6LastIntFromIp), /* value=0x0 */
    MFO(0x000001de, "P6_LAST_INT_TO_IP", P6LastIntToIp), /* value=0x0 */
    MFX(0x00000200, "IA32_MTRR_PHYS_BASE0", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x0, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x6 */
    MFX(0x00000201, "IA32_MTRR_PHYS_MASK0", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x0, 0, UINT64_C(0xffffff00000007ff)), /* value=0xff`c0000800 */
    MFX(0x00000202, "IA32_MTRR_PHYS_BASE1", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x1, 0, UINT64_C(0xffffff0000000ff8)), /* value=0xf8000001 */
    MFX(0x00000203, "IA32_MTRR_PHYS_MASK1", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x1, 0, UINT64_C(0xffffff00000007ff)), /* value=0xff`fc000800 */
    MFX(0x00000204, "IA32_MTRR_PHYS_BASE2", Ia32MtrrPhysBaseN, Ia32MtrrPhysBaseN, 0x2, 0, UINT64_C(0xffffff0000000ff8)), /* value=0x0 */
    MFX(0x00000205, "IA32_MTRR_PHYS_MASK2", Ia32MtrrPhysMaskN, Ia32MtrrPhysMaskN, 0x2, 0, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
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
    RFN(0x00000400, 0x00000413, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    MFX(0xc0000080, "AMD64_EFER", Amd64Efer, Amd64Efer, 0x800, 0xfe, UINT64_C(0xfffffffffffff200)),
    MFN(0xc0000081, "AMD64_STAR", Amd64SyscallTarget, Amd64SyscallTarget), /* value=0x0 */
    MFN(0xc0000082, "AMD64_STAR64", Amd64LongSyscallTarget, Amd64LongSyscallTarget), /* value=0x0 */
    MFN(0xc0000083, "AMD64_STARCOMPAT", Amd64CompSyscallTarget, Amd64CompSyscallTarget), /* value=0x0 */
    MFX(0xc0000084, "AMD64_SYSCALL_FLAG_MASK", Amd64SyscallFlagMask, Amd64SyscallFlagMask, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFN(0xc0000100, "AMD64_FS_BASE", Amd64FsBase, Amd64FsBase), /* value=0x81913800 */
    MFN(0xc0000101, "AMD64_GS_BASE", Amd64GsBase, Amd64GsBase), /* value=0x0 */
    MFN(0xc0000102, "AMD64_KERNEL_GS_BASE", Amd64KernelGsBase, Amd64KernelGsBase), /* value=0x0 */
    RSN(0xc0010000, 0xc0010003, "AMD_K8_PERF_CTL_n", AmdK8PerfCtlN, AmdK8PerfCtlN, 0x0, UINT64_C(0xffffffff00200000), 0),
    RSN(0xc0010004, 0xc0010007, "AMD_K8_PERF_CTR_n", AmdK8PerfCtrN, AmdK8PerfCtrN, 0x0, UINT64_C(0xffff000000000000), 0),
    MFX(0xc0010010, "AMD_K8_SYS_CFG", AmdK8SysCfg, AmdK8SysCfg, 0x160601, UINT64_C(0xffffffffffc0f800), 0), /* value=0x160601 */
    MFX(0xc0010015, "AMD_K8_HW_CFG", AmdK8HwCr, AmdK8HwCr, 0xc000000, UINT64_C(0xffffffff3ff00000), 0), /* value=0xc000000 */
    MFW(0xc0010016, "AMD_K8_IORR_BASE_0", AmdK8IorrBaseN, AmdK8IorrBaseN, UINT64_C(0xffffff0000000fe7)), /* value=0x0 */
    MFW(0xc0010017, "AMD_K8_IORR_MASK_0", AmdK8IorrMaskN, AmdK8IorrMaskN, UINT64_C(0xffffff00000007ff)), /* value=0x0 */
    MFX(0xc0010018, "AMD_K8_IORR_BASE_1", AmdK8IorrBaseN, AmdK8IorrBaseN, 0x1, UINT64_C(0xffffff0000000fe7), 0), /* value=0xf8000018 */
    MFX(0xc0010019, "AMD_K8_IORR_MASK_1", AmdK8IorrMaskN, AmdK8IorrMaskN, 0x1, UINT64_C(0xffffff00000007ff), 0), /* value=0xff`fc000800 */
    MFW(0xc001001a, "AMD_K8_TOP_MEM", AmdK8TopOfMemN, AmdK8TopOfMemN, UINT64_C(0xffffff00007fffff)), /* value=0x40000000 */
    MFX(0xc001001d, "AMD_K8_TOP_MEM2", AmdK8TopOfMemN, AmdK8TopOfMemN, 0x1, UINT64_C(0xffffff00007fffff), 0), /* value=0x0 */
    MVI(0xc001001e, "AMD_K8_MANID", 0x20),
    MFX(0xc001001f, "AMD_K8_NB_CFG1", AmdK8NbCfg1, AmdK8NbCfg1, 0, UINT64_C(0xffffff0000000000), 0), /* value=0x11`00000008 */
    MFN(0xc0010020, "AMD_K8_PATCH_LOADER", WriteOnly, AmdK8PatchLoader),
    MVX(0xc0010021, "AMD_K8_UNK_c001_0021", 0, UINT64_C(0xfffffffe00000000), 0),
    MFX(0xc0010022, "AMD_K8_MC_XCPT_REDIR", AmdK8McXcptRedir, AmdK8McXcptRedir, 0, UINT64_C(0xfffffffeffffffff), 0), /* value=0x0 */
    RFN(0xc0010030, 0xc0010035, "AMD_K8_CPU_NAME_n", AmdK8CpuNameN, AmdK8CpuNameN),
    MFX(0xc001003e, "AMD_K8_HTC", AmdK8HwThermalCtrl, AmdK8HwThermalCtrl, 0, UINT64_MAX, 0), /* value=0x0 */
    MFI(0xc001003f, "AMD_K8_STC", AmdK8SwThermalCtrl), /* value=0x0 */
    MFX(0xc0010041, "AMD_K8_FIDVID_CTL", AmdK8FidVidControl, AmdK8FidVidControl, UINT64_C(0x4e200000000c), 0x33, UINT64_C(0xfff00000fffee0c0)), /* value=0x4e20`0000000c */
    MFX(0xc0010042, "AMD_K8_FIDVID_STATUS", AmdK8FidVidStatus, ReadOnly, UINT64_C(0x200000c0c0c), 0, 0), /* value=0x200`000c0c0c */
    MVO(0xc0010043, "AMD_K8_THERMTRIP_STATUS", 0x521020),
    RFN(0xc0010044, 0xc0010048, "AMD_K8_MC_CTL_MASK_n", AmdK8McCtlMaskN, AmdK8McCtlMaskN),
    RSN(0xc0010050, 0xc0010053, "AMD_K8_SMI_ON_IO_TRAP_n", AmdK8SmiOnIoTrapN, AmdK8SmiOnIoTrapN, 0x0, 0, UINT64_C(0x1f00000000000000)),
    MFX(0xc0010054, "AMD_K8_SMI_ON_IO_TRAP_CTL_STS", AmdK8SmiOnIoTrapCtlSts, AmdK8SmiOnIoTrapCtlSts, 0, 0, UINT64_C(0xffffffffffff1f00)), /* value=0x0 */
    MFX(0xc0010111, "AMD_K8_SMM_BASE", AmdK8SmmBase, AmdK8SmmBase, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x98000 */
    MFX(0xc0010112, "AMD_K8_SMM_ADDR", AmdK8SmmAddr, AmdK8SmmAddr, 0, UINT64_C(0xffffff000001ffff), 0), /* value=0x0 */
    MFX(0xc0010113, "AMD_K8_SMM_MASK", AmdK8SmmMask, AmdK8SmmMask, 0, UINT64_C(0xffffff00000188c0), 0), /* value=0x1 */
    MVX(0xc0010114, "AMD_K8_UNK_c001_0114", 0, 0, UINT64_C(0xffffffffffffffe4)),
    MVX(0xc0010115, "AMD_K8_UNK_c001_0115", 0, 0, UINT64_C(0xffff800000000000)),
    MVX(0xc0010116, "AMD_K8_UNK_c001_0116", 0, 0, UINT64_C(0xffff0000ffff0000)),
    MVX(0xc0010117, "AMD_K8_UNK_c001_0117", 0, 0, UINT64_C(0xffff800000000000)),
    MVX(0xc0010118, "AMD_K8_UNK_c001_0118",0,0,0),
    MVX(0xc0010119, "AMD_K8_UNK_c001_0119",0,0,0),
    MVX(0xc001011a, "AMD_K8_UNK_c001_011a", 0, 0, UINT64_C(0xffffffff00000fff)),
    MVX(0xc001011b, "AMD_K8_UNK_c001_011b", 0, 0, ~(uint64_t)UINT32_MAX),
    MVX(0xc001011c, "AMD_K8_UNK_c001_011c", UINT32_C(0xdb1f5000), 0, UINT64_C(0xffffffff00000fff)),
    MFX(0xc0011000, "AMD_K7_MCODE_CTL", AmdK7MicrocodeCtl, AmdK7MicrocodeCtl, 0, ~(uint64_t)UINT32_MAX, 0x204), /* value=0x0 */
    MFX(0xc0011001, "AMD_K7_APIC_CLUSTER_ID", AmdK7ClusterIdMaybe, AmdK7ClusterIdMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0xc0011004, "AMD_K8_CPUID_CTL_STD01", AmdK8CpuIdCtlStd01hEdcx, AmdK8CpuIdCtlStd01hEdcx, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x78bfbff */
    MFX(0xc0011005, "AMD_K8_CPUID_CTL_EXT01", AmdK8CpuIdCtlExt01hEdcx, AmdK8CpuIdCtlExt01hEdcx, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0xf1f3fbff */
    MFX(0xc0011006, "AMD_K7_DEBUG_STS?", AmdK7DebugStatusMaybe, AmdK7DebugStatusMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFN(0xc0011007, "AMD_K7_BH_TRACE_BASE?", AmdK7BHTraceBaseMaybe, AmdK7BHTraceBaseMaybe), /* value=0x0 */
    MFN(0xc0011008, "AMD_K7_BH_TRACE_PTR?", AmdK7BHTracePtrMaybe, AmdK7BHTracePtrMaybe), /* value=0x0 */
    MFN(0xc0011009, "AMD_K7_BH_TRACE_LIM?", AmdK7BHTraceLimitMaybe, AmdK7BHTraceLimitMaybe), /* value=0x0 */
    MFX(0xc001100a, "AMD_K7_HDT_CFG?", AmdK7HardwareDebugToolCfgMaybe, AmdK7HardwareDebugToolCfgMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x0 */
    MFX(0xc001100b, "AMD_K7_FAST_FLUSH_COUNT?", AmdK7FastFlushCountMaybe, AmdK7FastFlushCountMaybe, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x7c0 */
    MFX(0xc001100c, "AMD_K7_NODE_ID", AmdK7NodeId, AmdK7NodeId, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x20906 */
    MVX(0xc001100d, "AMD_K8_LOGICAL_CPUS_NUM?", 0x10a, 0, 0),
    MVX(0xc001100e, "AMD_K8_WRMSR_BP?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc001100f, "AMD_K8_WRMSR_BP_MASK?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc0011010, "AMD_K8_BH_TRACE_CTL?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc0011011, "AMD_K8_BH_TRACE_USRD?", 0, 0, 0), /* value=0xc0011011`00000283 */
    MVX(0xc0011014, "AMD_K8_XCPT_BP_RIP?", 0, 0, 0),
    MVX(0xc0011015, "AMD_K8_XCPT_BP_RIP_MASK?", 0, 0, 0),
    MVX(0xc0011016, "AMD_K8_COND_HDT_VAL?", 0, 0, 0),
    MVX(0xc0011017, "AMD_K8_COND_HDT_VAL_MASK?", 0, 0, 0),
    MVX(0xc0011018, "AMD_K8_XCPT_BP_CTL?", 0, ~(uint64_t)UINT32_MAX, 0),
    MVX(0xc001101d, "AMD_K8_NB_BIST?", 0, UINT64_C(0xfffffffffc000000), 0),
    MVI(0xc001101e, "AMD_K8_THERMTRIP_2?", 0x521020), /* Villain? */
    MVX(0xc001101f, "AMD_K8_NB_CFG?", UINT64_C(0x1100000008), UINT64_C(0xffffff0000000000), 0),
    MFX(0xc0011020, "AMD_K7_LS_CFG", AmdK7LoadStoreCfg, AmdK7LoadStoreCfg, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x1000 */
    MFX(0xc0011021, "AMD_K7_IC_CFG", AmdK7InstrCacheCfg, AmdK7InstrCacheCfg, 0x800, ~(uint64_t)UINT32_MAX, 0), /* value=0x800 */
    MFX(0xc0011022, "AMD_K7_DC_CFG", AmdK7DataCacheCfg, AmdK7DataCacheCfg, 0, ~(uint64_t)UINT32_MAX, 0), /* value=0x24000008 */
    MFN(0xc0011023, "AMD_K7_BU_CFG", AmdK7BusUnitCfg, AmdK7BusUnitCfg), /* Villain? value=0x2020 */
    MFX(0xc0011024, "AMD_K7_DEBUG_CTL_2?", AmdK7DebugCtl2Maybe, AmdK7DebugCtl2Maybe, 0, UINT64_C(0xffffffffffffff00), 0), /* value=0x0 */
    MFN(0xc0011025, "AMD_K7_DR0_DATA_MATCH?", AmdK7Dr0DataMatchMaybe, AmdK7Dr0DataMatchMaybe), /* value=0x0 */
    MFN(0xc0011026, "AMD_K7_DR0_DATA_MATCH?", AmdK7Dr0DataMaskMaybe, AmdK7Dr0DataMaskMaybe), /* value=0x0 */
    MFX(0xc0011027, "AMD_K7_DR0_ADDR_MASK", AmdK7DrXAddrMaskN, AmdK7DrXAddrMaskN, 0x0, UINT64_C(0xfffffffffffff000), 0), /* value=0x0 */
};
#endif /* !CPUM_DB_STANDALONE */


/**
 * Database entry for AMD Athlon(tm) 64 Processor 3200+.
 */
static CPUMDBENTRY const g_Entry_AMD_Athlon_64_3200 =
{
    /*.pszName          = */ "AMD Athlon 64 3200+",
    /*.pszFullName      = */ "AMD Athlon(tm) 64 Processor 3200+",
    /*.enmVendor        = */ CPUMCPUVENDOR_AMD,
    /*.uFamily          = */ 15,
    /*.uModel           = */ 4,
    /*.uStepping        = */ 8,
    /*.enmMicroarch     = */ kCpumMicroarch_AMD_K8_130nm,
    /*.uScalableBusFreq = */ CPUM_SBUSFREQ_UNKNOWN,
    /*.fFlags           = */ 0,
    /*.cMaxPhysAddrWidth= */ 40,
    /*.fMxCsrMask       = */ 0xffff, ///< @todo check.
    /*.paCpuIdLeaves    = */ NULL_ALONE(g_aCpuIdLeaves_AMD_Athlon_64_3200),
    /*.cCpuIdLeaves     = */ ZERO_ALONE(RT_ELEMENTS(g_aCpuIdLeaves_AMD_Athlon_64_3200)),
    /*.enmUnknownCpuId  = */ CPUMUNKNOWNCPUID_DEFAULTS,
    /*.DefUnknownCpuId  = */ { 0x00000000, 0x00000000, 0x00000000, 0x00000000 },
    /*.fMsrMask         = */ UINT32_MAX,
    /*.cMsrRanges       = */ ZERO_ALONE(RT_ELEMENTS(g_aMsrRanges_AMD_Athlon_64_3200)),
    /*.paMsrRanges      = */ NULL_ALONE(g_aMsrRanges_AMD_Athlon_64_3200),
};

#endif /* !VBOX_CPUDB_AMD_Athlon_64_3200_h */

