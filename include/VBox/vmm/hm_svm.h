/** @file
 * HM - SVM (AMD-V) Structures and Definitions. (VMM)
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_vmm_hm_svm_h
#define VBOX_INCLUDED_vmm_hm_svm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <iprt/assert.h>

#ifdef RT_OS_SOLARIS
# undef ES
# undef CS
# undef DS
# undef SS
# undef FS
# undef GS
#endif

/** @defgroup grp_hm_svm    SVM (AMD-V) Types and Definitions
 * @ingroup grp_hm
 * @{
 */

/** @name SVM generic / convenient defines.
 * @{
 */
/** Number of pages required for the VMCB. */
#define SVM_VMCB_PAGES                  1
/** Number of pages required for the MSR permission bitmap. */
#define SVM_MSRPM_PAGES                 2
/** Number of pages required for the IO permission bitmap. */
#define SVM_IOPM_PAGES                  3
/** Number of pages required for the AVIC per-VM (APIC access, logical, physical tables). */
#define SVM_AVIC_PAGES                  3
/** @} */

/*
 * Ugly!
 * When compiling the recompiler, its own svm.h defines clash with
 * the following defines. Avoid just the duplicates here as we still
 * require other definitions and structures in this header.
 */
#ifndef IN_REM_R3
/** @name SVM_EXIT_XXX - SVM Basic Exit Reasons.
 * @{
 */
/** Invalid guest state in VMCB. */
# define SVM_EXIT_INVALID                (uint64_t)(-1)
/** Read from CR0-CR15. */
# define SVM_EXIT_READ_CR0               0x0
# define SVM_EXIT_READ_CR1               0x1
# define SVM_EXIT_READ_CR2               0x2
# define SVM_EXIT_READ_CR3               0x3
# define SVM_EXIT_READ_CR4               0x4
# define SVM_EXIT_READ_CR5               0x5
# define SVM_EXIT_READ_CR6               0x6
# define SVM_EXIT_READ_CR7               0x7
# define SVM_EXIT_READ_CR8               0x8
# define SVM_EXIT_READ_CR9               0x9
# define SVM_EXIT_READ_CR10              0xa
# define SVM_EXIT_READ_CR11              0xb
# define SVM_EXIT_READ_CR12              0xc
# define SVM_EXIT_READ_CR13              0xd
# define SVM_EXIT_READ_CR14              0xe
# define SVM_EXIT_READ_CR15              0xf
/** Writes to CR0-CR15. */
# define SVM_EXIT_WRITE_CR0              0x10
# define SVM_EXIT_WRITE_CR1              0x11
# define SVM_EXIT_WRITE_CR2              0x12
# define SVM_EXIT_WRITE_CR3              0x13
# define SVM_EXIT_WRITE_CR4              0x14
# define SVM_EXIT_WRITE_CR5              0x15
# define SVM_EXIT_WRITE_CR6              0x16
# define SVM_EXIT_WRITE_CR7              0x17
# define SVM_EXIT_WRITE_CR8              0x18
# define SVM_EXIT_WRITE_CR9              0x19
# define SVM_EXIT_WRITE_CR10             0x1a
# define SVM_EXIT_WRITE_CR11             0x1b
# define SVM_EXIT_WRITE_CR12             0x1c
# define SVM_EXIT_WRITE_CR13             0x1d
# define SVM_EXIT_WRITE_CR14             0x1e
# define SVM_EXIT_WRITE_CR15             0x1f
/** Read from DR0-DR15. */
# define SVM_EXIT_READ_DR0               0x20
# define SVM_EXIT_READ_DR1               0x21
# define SVM_EXIT_READ_DR2               0x22
# define SVM_EXIT_READ_DR3               0x23
# define SVM_EXIT_READ_DR4               0x24
# define SVM_EXIT_READ_DR5               0x25
# define SVM_EXIT_READ_DR6               0x26
# define SVM_EXIT_READ_DR7               0x27
# define SVM_EXIT_READ_DR8               0x28
# define SVM_EXIT_READ_DR9               0x29
# define SVM_EXIT_READ_DR10              0x2a
# define SVM_EXIT_READ_DR11              0x2b
# define SVM_EXIT_READ_DR12              0x2c
# define SVM_EXIT_READ_DR13              0x2d
# define SVM_EXIT_READ_DR14              0x2e
# define SVM_EXIT_READ_DR15              0x2f
/** Writes to DR0-DR15. */
# define SVM_EXIT_WRITE_DR0              0x30
# define SVM_EXIT_WRITE_DR1              0x31
# define SVM_EXIT_WRITE_DR2              0x32
# define SVM_EXIT_WRITE_DR3              0x33
# define SVM_EXIT_WRITE_DR4              0x34
# define SVM_EXIT_WRITE_DR5              0x35
# define SVM_EXIT_WRITE_DR6              0x36
# define SVM_EXIT_WRITE_DR7              0x37
# define SVM_EXIT_WRITE_DR8              0x38
# define SVM_EXIT_WRITE_DR9              0x39
# define SVM_EXIT_WRITE_DR10             0x3a
# define SVM_EXIT_WRITE_DR11             0x3b
# define SVM_EXIT_WRITE_DR12             0x3c
# define SVM_EXIT_WRITE_DR13             0x3d
# define SVM_EXIT_WRITE_DR14             0x3e
# define SVM_EXIT_WRITE_DR15             0x3f
/* Exception 0-31. */
# define SVM_EXIT_XCPT_0                 0x40
# define SVM_EXIT_XCPT_1                 0x41
# define SVM_EXIT_XCPT_2                 0x42
# define SVM_EXIT_XCPT_3                 0x43
# define SVM_EXIT_XCPT_4                 0x44
# define SVM_EXIT_XCPT_5                 0x45
# define SVM_EXIT_XCPT_6                 0x46
# define SVM_EXIT_XCPT_7                 0x47
# define SVM_EXIT_XCPT_8                 0x48
# define SVM_EXIT_XCPT_9                 0x49
# define SVM_EXIT_XCPT_10                0x4a
# define SVM_EXIT_XCPT_11                0x4b
# define SVM_EXIT_XCPT_12                0x4c
# define SVM_EXIT_XCPT_13                0x4d
# define SVM_EXIT_XCPT_14                0x4e
# define SVM_EXIT_XCPT_15                0x4f
# define SVM_EXIT_XCPT_16                0x50
# define SVM_EXIT_XCPT_17                0x51
# define SVM_EXIT_XCPT_18                0x52
# define SVM_EXIT_XCPT_19                0x53
# define SVM_EXIT_XCPT_20                0x54
# define SVM_EXIT_XCPT_21                0x55
# define SVM_EXIT_XCPT_22                0x56
# define SVM_EXIT_XCPT_23                0x57
# define SVM_EXIT_XCPT_24                0x58
# define SVM_EXIT_XCPT_25                0x59
# define SVM_EXIT_XCPT_26                0x5a
# define SVM_EXIT_XCPT_27                0x5b
# define SVM_EXIT_XCPT_28                0x5c
# define SVM_EXIT_XCPT_29                0x5d
# define SVM_EXIT_XCPT_30                0x5e
# define SVM_EXIT_XCPT_31                0x5f
/* Exception (more readable) */
# define SVM_EXIT_XCPT_DE                SVM_EXIT_XCPT_0
# define SVM_EXIT_XCPT_DB                SVM_EXIT_XCPT_1
# define SVM_EXIT_XCPT_NMI               SVM_EXIT_XCPT_2
# define SVM_EXIT_XCPT_BP                SVM_EXIT_XCPT_3
# define SVM_EXIT_XCPT_OF                SVM_EXIT_XCPT_4
# define SVM_EXIT_XCPT_BR                SVM_EXIT_XCPT_5
# define SVM_EXIT_XCPT_UD                SVM_EXIT_XCPT_6
# define SVM_EXIT_XCPT_NM                SVM_EXIT_XCPT_7
# define SVM_EXIT_XCPT_DF                SVM_EXIT_XCPT_8
# define SVM_EXIT_XCPT_CO_SEG_OVERRUN    SVM_EXIT_XCPT_9
# define SVM_EXIT_XCPT_TS                SVM_EXIT_XCPT_10
# define SVM_EXIT_XCPT_NP                SVM_EXIT_XCPT_11
# define SVM_EXIT_XCPT_SS                SVM_EXIT_XCPT_12
# define SVM_EXIT_XCPT_GP                SVM_EXIT_XCPT_13
# define SVM_EXIT_XCPT_PF                SVM_EXIT_XCPT_14
# define SVM_EXIT_XCPT_MF                SVM_EXIT_XCPT_16
# define SVM_EXIT_XCPT_AC                SVM_EXIT_XCPT_17
# define SVM_EXIT_XCPT_MC                SVM_EXIT_XCPT_18
# define SVM_EXIT_XCPT_XF                SVM_EXIT_XCPT_19
# define SVM_EXIT_XCPT_VE                SVM_EXIT_XCPT_20
# define SVM_EXIT_XCPT_SX                SVM_EXIT_XCPT_30
/** Physical maskable interrupt. */
# define SVM_EXIT_INTR                   0x60
/** Non-maskable interrupt. */
# define SVM_EXIT_NMI                    0x61
/** System Management interrupt. */
# define SVM_EXIT_SMI                    0x62
/** Physical INIT signal. */
# define SVM_EXIT_INIT                   0x63
/** Virtual interrupt. */
# define SVM_EXIT_VINTR                  0x64
/** Write to CR0 that changed any bits other than CR0.TS or CR0.MP. */
# define SVM_EXIT_CR0_SEL_WRITE          0x65
/** IDTR read. */
# define SVM_EXIT_IDTR_READ              0x66
/** GDTR read. */
# define SVM_EXIT_GDTR_READ              0x67
/** LDTR read. */
# define SVM_EXIT_LDTR_READ              0x68
/** TR read. */
# define SVM_EXIT_TR_READ                0x69
/** IDTR write. */
# define SVM_EXIT_IDTR_WRITE             0x6a
/** GDTR write. */
# define SVM_EXIT_GDTR_WRITE             0x6b
/** LDTR write. */
# define SVM_EXIT_LDTR_WRITE             0x6c
/** TR write. */
# define SVM_EXIT_TR_WRITE               0x6d
/** RDTSC instruction. */
# define SVM_EXIT_RDTSC                  0x6e
/** RDPMC instruction. */
# define SVM_EXIT_RDPMC                  0x6f
/** PUSHF instruction. */
# define SVM_EXIT_PUSHF                  0x70
/** POPF instruction. */
# define SVM_EXIT_POPF                   0x71
/** CPUID instruction. */
# define SVM_EXIT_CPUID                  0x72
/** RSM instruction. */
# define SVM_EXIT_RSM                    0x73
/** IRET instruction. */
# define SVM_EXIT_IRET                   0x74
/** Software interrupt (INTn instructions). */
# define SVM_EXIT_SWINT                  0x75
/** INVD instruction. */
# define SVM_EXIT_INVD                   0x76
/** PAUSE instruction. */
# define SVM_EXIT_PAUSE                  0x77
/** HLT instruction. */
# define SVM_EXIT_HLT                    0x78
/** INVLPG instructions. */
# define SVM_EXIT_INVLPG                 0x79
/** INVLPGA instruction. */
# define SVM_EXIT_INVLPGA                0x7a
/** IN or OUT accessing protected port (the EXITINFO1 field provides more information). */
# define SVM_EXIT_IOIO                   0x7b
/** RDMSR or WRMSR access to protected MSR. */
# define SVM_EXIT_MSR                    0x7c
/** task switch. */
# define SVM_EXIT_TASK_SWITCH            0x7d
/** FP legacy handling enabled, and processor is frozen in an x87/mmx instruction waiting for an interrupt. */
# define SVM_EXIT_FERR_FREEZE            0x7e
/** Shutdown. */
# define SVM_EXIT_SHUTDOWN               0x7f
/** VMRUN instruction. */
# define SVM_EXIT_VMRUN                  0x80
/** VMMCALL instruction. */
# define SVM_EXIT_VMMCALL                0x81
/** VMLOAD instruction. */
# define SVM_EXIT_VMLOAD                 0x82
/** VMSAVE instruction. */
# define SVM_EXIT_VMSAVE                 0x83
/** STGI instruction. */
# define SVM_EXIT_STGI                   0x84
/** CLGI instruction. */
# define SVM_EXIT_CLGI                   0x85
/** SKINIT instruction. */
# define SVM_EXIT_SKINIT                 0x86
/** RDTSCP instruction. */
# define SVM_EXIT_RDTSCP                 0x87
/** ICEBP instruction. */
# define SVM_EXIT_ICEBP                  0x88
/** WBINVD instruction. */
# define SVM_EXIT_WBINVD                 0x89
/** MONITOR instruction. */
# define SVM_EXIT_MONITOR                0x8a
/** MWAIT instruction. */
# define SVM_EXIT_MWAIT                  0x8b
/** MWAIT instruction, when armed. */
# define SVM_EXIT_MWAIT_ARMED            0x8c
/** XSETBV instruction. */
# define SVM_EXIT_XSETBV                 0x8d
/** RDPRU instruction. */
# define SVM_EXIT_RDPRU                  0x8e
/** Write to EFER (after guest instruction completes). */
# define SVM_EXIT_WRITE_EFER_TRAP        0x8f
/** Write to CR0-CR15 (after guest instruction completes). */
# define SVM_EXIT_WRITE_CR0_TRAP         0x90
# define SVM_EXIT_WRITE_CR1_TRAP         0x91
# define SVM_EXIT_WRITE_CR2_TRAP         0x92
# define SVM_EXIT_WRITE_CR3_TRAP         0x93
# define SVM_EXIT_WRITE_CR4_TRAP         0x94
# define SVM_EXIT_WRITE_CR5_TRAP         0x95
# define SVM_EXIT_WRITE_CR6_TRAP         0x96
# define SVM_EXIT_WRITE_CR7_TRAP         0x97
# define SVM_EXIT_WRITE_CR8_TRAP         0x98
# define SVM_EXIT_WRITE_CR9_TRAP         0x99
# define SVM_EXIT_WRITE_CR10_TRAP        0x9a
# define SVM_EXIT_WRITE_CR11_TRAP        0x9b
# define SVM_EXIT_WRITE_CR12_TRAP        0x9c
# define SVM_EXIT_WRITE_CR13_TRAP        0x9d
# define SVM_EXIT_WRITE_CR14_TRAP        0x9e
# define SVM_EXIT_WRITE_CR15_TRAP        0x9f
/** MCOMMIT instruction. */
# define SVM_EXIT_MCOMMIT                0xa3
/** TLBSYNC instruction. */
# define SVM_EXIT_TLBSYNC                0xa4
/** Bus lock event happened (also called split lock). */
# define SVM_EXIT_BUSLOCK                0xa5
/** HLT instruction executed while no virtual interrupt is pending. */
# define SVM_EXIT_IDLE_HLT               0xa6

/** Nested paging: host-level page fault occurred (EXITINFO1 contains fault errorcode; EXITINFO2 contains the guest physical address causing the fault). */
# define SVM_EXIT_NPF                    0x400
/** AVIC: Virtual IPI delivery not completed. */
# define SVM_EXIT_AVIC_INCOMPLETE_IPI    0x401
/** AVIC: Attempted access by guest to a vAPIC register not handled by AVIC
 *  hardware. */
# define SVM_EXIT_AVIC_NOACCEL           0x402
/** The maximum possible exit value. */
# define SVM_EXIT_MAX                    (SVM_EXIT_AVIC_NOACCEL)
/** @} */
#endif /* !IN_REM_R3*/


/** @name SVMVMCB.u64ExitInfo2 for AVIC Incomplete IPI.
 * @{
 */
#define SVM_EXIT2_INC_IPI_INDEX_MASK                UINT64_C(0xfff)
#define SVM_EXIT2_INC_IPI_ID_SHIFT                  32
#define SVM_EXIT2_INC_IPI_INDEX_INVALID_INTR_TYPE   0
#define SVM_EXIT2_INC_IPI_INDEX_TARGET_NOT_RUNNING  1
#define SVM_EXIT2_INC_IPI_INDEX_INVALID_TARGET      2
#define SVM_EXIT2_INC_IPI_INDEX_INVALID_PTR         3
#define SVM_EXIT2_INC_IPI_INDEX_INVALID_IPI_VECTOR  4
#define SVM_EXIT2_INC_IPI_INDEX_UNACCEL_IPI         5
/** @} */


/** @name SVMVMCB.u64ExitInfo2 for task switches
 * @{
 */
/** Set to 1 if the task switch was caused by an IRET; else cleared to 0. */
#define SVM_EXIT2_TASK_SWITCH_IRET            RT_BIT_64(36)
/** Set to 1 if the task switch was caused by a far jump; else cleared to 0. */
#define SVM_EXIT2_TASK_SWITCH_JUMP            RT_BIT_64(38)
/** Set to 1 if the task switch has an error code; else cleared to 0. */
#define SVM_EXIT2_TASK_SWITCH_HAS_ERROR_CODE  RT_BIT_64(44)
/** The value of EFLAGS.RF that would be saved in the outgoing TSS if the task switch were not intercepted. */
#define SVM_EXIT2_TASK_SWITCH_EFLAGS_RF       RT_BIT_64(48)
/** @} */

/** @name SVMVMCB.u64ExitInfo1 for MSR accesses
 * @{
 */
/** The access was a read MSR. */
#define SVM_EXIT1_MSR_READ                    0x0
/** The access was a write MSR. */
#define SVM_EXIT1_MSR_WRITE                   0x1
/** @} */

/** @name SVMVMCB.u64ExitInfo1 for Mov CRx accesses.
 * @{
 */
/** The mask of whether the access was via a Mov CRx instruction. */
#define SVM_EXIT1_MOV_CRX_MASK                RT_BIT_64(63)
/** The mask for the GPR number of the Mov CRx instruction.  */
#define SVM_EXIT1_MOV_CRX_GPR_NUMBER          0xf
/** @} */

/** @name SVMVMCB.u64ExitInfo1 for Mov DRx accesses.
 * @{
 */
/** The mask for the GPR number of the Mov DRx instruction.  */
#define SVM_EXIT1_MOV_DRX_GPR_NUMBER          0xf
/** @} */

/** @name SVMVMCB.ctrl.u64InterceptCtrl
 * @{
 */
/** Intercept INTR (physical maskable interrupt). */
#define SVM_CTRL_INTERCEPT_INTR               RT_BIT_64(0)
/** Intercept NMI. */
#define SVM_CTRL_INTERCEPT_NMI                RT_BIT_64(1)
/** Intercept SMI. */
#define SVM_CTRL_INTERCEPT_SMI                RT_BIT_64(2)
/** Intercept INIT. */
#define SVM_CTRL_INTERCEPT_INIT               RT_BIT_64(3)
/** Intercept VINTR (virtual maskable interrupt). */
#define SVM_CTRL_INTERCEPT_VINTR              RT_BIT_64(4)
/** Intercept CR0 writes that change bits other than CR0.TS or CR0.MP */
#define SVM_CTRL_INTERCEPT_CR0_SEL_WRITE      RT_BIT_64(5)
/** Intercept reads of IDTR. */
#define SVM_CTRL_INTERCEPT_IDTR_READS         RT_BIT_64(6)
/** Intercept reads of GDTR. */
#define SVM_CTRL_INTERCEPT_GDTR_READS         RT_BIT_64(7)
/** Intercept reads of LDTR. */
#define SVM_CTRL_INTERCEPT_LDTR_READS         RT_BIT_64(8)
/** Intercept reads of TR. */
#define SVM_CTRL_INTERCEPT_TR_READS           RT_BIT_64(9)
/** Intercept writes of IDTR. */
#define SVM_CTRL_INTERCEPT_IDTR_WRITES        RT_BIT_64(10)
/** Intercept writes of GDTR. */
#define SVM_CTRL_INTERCEPT_GDTR_WRITES        RT_BIT_64(11)
/** Intercept writes of LDTR. */
#define SVM_CTRL_INTERCEPT_LDTR_WRITES        RT_BIT_64(12)
/** Intercept writes of TR. */
#define SVM_CTRL_INTERCEPT_TR_WRITES          RT_BIT_64(13)
/** Intercept RDTSC instruction. */
#define SVM_CTRL_INTERCEPT_RDTSC              RT_BIT_64(14)
/** Intercept RDPMC instruction. */
#define SVM_CTRL_INTERCEPT_RDPMC              RT_BIT_64(15)
/** Intercept PUSHF instruction. */
#define SVM_CTRL_INTERCEPT_PUSHF              RT_BIT_64(16)
/** Intercept POPF instruction. */
#define SVM_CTRL_INTERCEPT_POPF               RT_BIT_64(17)
/** Intercept CPUID instruction. */
#define SVM_CTRL_INTERCEPT_CPUID              RT_BIT_64(18)
/** Intercept RSM instruction. */
#define SVM_CTRL_INTERCEPT_RSM                RT_BIT_64(19)
/** Intercept IRET instruction. */
#define SVM_CTRL_INTERCEPT_IRET               RT_BIT_64(20)
/** Intercept INTn instruction. */
#define SVM_CTRL_INTERCEPT_INTN               RT_BIT_64(21)
/** Intercept INVD instruction. */
#define SVM_CTRL_INTERCEPT_INVD               RT_BIT_64(22)
/** Intercept PAUSE instruction. */
#define SVM_CTRL_INTERCEPT_PAUSE              RT_BIT_64(23)
/** Intercept HLT instruction. */
#define SVM_CTRL_INTERCEPT_HLT                RT_BIT_64(24)
/** Intercept INVLPG instruction. */
#define SVM_CTRL_INTERCEPT_INVLPG             RT_BIT_64(25)
/** Intercept INVLPGA instruction. */
#define SVM_CTRL_INTERCEPT_INVLPGA            RT_BIT_64(26)
/** IOIO_PROT Intercept IN/OUT accesses to selected ports. */
#define SVM_CTRL_INTERCEPT_IOIO_PROT          RT_BIT_64(27)
/** MSR_PROT Intercept RDMSR or WRMSR accesses to selected MSRs. */
#define SVM_CTRL_INTERCEPT_MSR_PROT           RT_BIT_64(28)
/** Intercept task switches. */
#define SVM_CTRL_INTERCEPT_TASK_SWITCH        RT_BIT_64(29)
/** FERR_FREEZE: intercept processor "freezing" during legacy FERR handling. */
#define SVM_CTRL_INTERCEPT_FERR_FREEZE        RT_BIT_64(30)
/** Intercept shutdown events. */
#define SVM_CTRL_INTERCEPT_SHUTDOWN           RT_BIT_64(31)
/** Intercept VMRUN instruction. */
#define SVM_CTRL_INTERCEPT_VMRUN              RT_BIT_64(32 + 0)
/** Intercept VMMCALL instruction. */
#define SVM_CTRL_INTERCEPT_VMMCALL            RT_BIT_64(32 + 1)
/** Intercept VMLOAD instruction. */
#define SVM_CTRL_INTERCEPT_VMLOAD             RT_BIT_64(32 + 2)
/** Intercept VMSAVE instruction. */
#define SVM_CTRL_INTERCEPT_VMSAVE             RT_BIT_64(32 + 3)
/** Intercept STGI instruction. */
#define SVM_CTRL_INTERCEPT_STGI               RT_BIT_64(32 + 4)
/** Intercept CLGI instruction. */
#define SVM_CTRL_INTERCEPT_CLGI               RT_BIT_64(32 + 5)
/** Intercept SKINIT instruction. */
#define SVM_CTRL_INTERCEPT_SKINIT             RT_BIT_64(32 + 6)
/** Intercept RDTSCP instruction. */
#define SVM_CTRL_INTERCEPT_RDTSCP             RT_BIT_64(32 + 7)
/** Intercept ICEBP instruction. */
#define SVM_CTRL_INTERCEPT_ICEBP              RT_BIT_64(32 + 8)
/** Intercept WBINVD instruction. */
#define SVM_CTRL_INTERCEPT_WBINVD             RT_BIT_64(32 + 9)
/** Intercept MONITOR instruction. */
#define SVM_CTRL_INTERCEPT_MONITOR            RT_BIT_64(32 + 10)
/** Intercept MWAIT instruction unconditionally. */
#define SVM_CTRL_INTERCEPT_MWAIT              RT_BIT_64(32 + 11)
/** Intercept MWAIT instruction when armed. */
#define SVM_CTRL_INTERCEPT_MWAIT_ARMED        RT_BIT_64(32 + 12)
/** Intercept XSETBV instruction. */
#define SVM_CTRL_INTERCEPT_XSETBV             RT_BIT_64(32 + 13)
/** Intercept RDPRU instruction. */
#define SVM_CTRL_INTERCEPT_RDPRU              RT_BIT_64(32 + 14)
/** Intercept EFER writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_EFER_WRITES_TRAP   RT_BIT_64(32 + 15)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR0_WRITES_TRAP    RT_BIT_64(32 + 16)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR1_WRITES_TRAP    RT_BIT_64(32 + 17)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR2_WRITES_TRAP    RT_BIT_64(32 + 18)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR3_WRITES_TRAP    RT_BIT_64(32 + 19)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR4_WRITES_TRAP    RT_BIT_64(32 + 20)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR5_WRITES_TRAP    RT_BIT_64(32 + 21)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR6_WRITES_TRAP    RT_BIT_64(32 + 22)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR7_WRITES_TRAP    RT_BIT_64(32 + 23)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR8_WRITES_TRAP    RT_BIT_64(32 + 24)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR9_WRITES_TRAP    RT_BIT_64(32 + 25)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR10_WRITES_TRAP   RT_BIT_64(32 + 26)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR11_WRITES_TRAP   RT_BIT_64(32 + 27)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR12_WRITES_TRAP   RT_BIT_64(32 + 28)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR13_WRITES_TRAP   RT_BIT_64(32 + 29)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR14_WRITES_TRAP   RT_BIT_64(32 + 30)
/** Intercept CR0 writes after guest instruction finishes. */
#define SVM_CTRL_INTERCEPT_CR15_WRITES_TRAP   RT_BIT_64(32 + 31)
/** @} */

/** @name SVMVMCB.ctrl.u64InterceptCtrl2
 * @{
 */
/** Intercept all INVLPGB instructions. */
#define SVM_CTRL_INTERCEPT_INVLPGB              RT_BIT_64(0)
/** Intercept only illegal INVLPGB instructions. */
#define SVM_CTRL_INTERCEPT_ILLEGAL_INVLPGB      RT_BIT_64(1)
/** Intercept INVPCID instructions. */
#define SVM_CTRL_INTERCEPT_INVPCID              RT_BIT_64(2)
/** Intercept MCOMMIT instructions. */
#define SVM_CTRL_INTERCEPT_MCOMMIT              RT_BIT_64(3)
/** Intercept TLBSYNC instructions. */
#define SVM_CTRL_INTERCEPT_TLBSYNC              RT_BIT_64(4)
/** Intercept bus lock operations exceeding the bus lock threshold. */
#define SVM_CTRL_INTERCEPT_BUSLOCK              RT_BIT_64(5)
/** Intercept HLT instructions if no virtual interrupt is pending. */
#define SVM_CTRL_INTERCEPT_HLT_NO_INTR_PENDING  RT_BIT_64(6)
/** @} */

/** @name SVMINTCTRL.u3Type
 * @{
 */
/** External or virtual interrupt. */
#define SVM_EVENT_EXTERNAL_IRQ                0
/** Non-maskable interrupt. */
#define SVM_EVENT_NMI                         2
/** Exception; fault or trap. */
#define SVM_EVENT_EXCEPTION                   3
/** Software interrupt. */
#define SVM_EVENT_SOFTWARE_INT                4
/** @} */

/** @name SVMVMCB.ctrl.TLBCtrl.n.u8TLBFlush
 * @{
 */
/** Flush nothing. */
#define SVM_TLB_FLUSH_NOTHING                           0
/** Flush entire TLB (host+guest entries) */
#define SVM_TLB_FLUSH_ENTIRE                            1
/** Flush this guest's TLB entries (by ASID) */
#define SVM_TLB_FLUSH_SINGLE_CONTEXT                    3
/** Flush this guest's non-global TLB entries (by ASID) */
#define SVM_TLB_FLUSH_SINGLE_CONTEXT_RETAIN_GLOBALS     7
/** @} */

/**
 * SVM selector/segment register type.
 */
typedef struct
{
    uint16_t    u16Sel;
    uint16_t    u16Attr;
    uint32_t    u32Limit;
    uint64_t    u64Base;        /**< Only lower 32 bits are implemented for CS, DS, ES & SS. */
} SVMSELREG;
AssertCompileSize(SVMSELREG, 16);
/** Pointer to the SVMSELREG struct. */
typedef SVMSELREG *PSVMSELREG;
/** Pointer to a const SVMSELREG struct. */
typedef const SVMSELREG *PCSVMSELREG;

/**
 * SVM GDTR/IDTR type.
 */
typedef struct
{
    uint16_t    u16Reserved0;
    uint16_t    u16Reserved1;
    uint32_t    u32Limit;       /**< Only lower 16 bits are implemented. */
    uint64_t    u64Base;
} SVMXDTR;
AssertCompileSize(SVMXDTR, 16);
typedef SVMXDTR SVMIDTR;
typedef SVMXDTR SVMGDTR;
/** Pointer to the SVMXDTR struct. */
typedef SVMXDTR *PSVMXDTR;
/** Pointer to a const SVMXDTR struct. */
typedef const SVMXDTR *PCSVMXDTR;

/**
 * SVM Event injection structure (EVENTINJ and EXITINTINFO).
 */
typedef union
{
    struct
    {
        uint32_t    u8Vector            : 8;
        uint32_t    u3Type              : 3;
        uint32_t    u1ErrorCodeValid    : 1;
        uint32_t    u19Reserved         : 19;
        uint32_t    u1Valid             : 1;
        uint32_t    u32ErrorCode        : 32;
    } n;
    uint64_t    u;
} SVMEVENT;
/** Pointer to the SVMEVENT union. */
typedef SVMEVENT *PSVMEVENT;
/** Pointer to a const SVMEVENT union. */
typedef const SVMEVENT *PCSVMEVENT;

/** Gets the event type given an SVMEVENT parameter. */
#define SVM_EVENT_GET_TYPE(a_SvmEvent)  (((a_SvmEvent) >> 8) & 7)

/**
 * SVM Interrupt control structure (Virtual Interrupt Control).
 */
typedef union
{
    struct
    {
        uint32_t    u8VTPR              : 8;    /* V_TPR */
        uint32_t    u1VIrqPending       : 1;    /* V_IRQ */
        uint32_t    u1VGif              : 1;    /* VGIF */
        uint32_t    u6Reserved          : 6;
        uint32_t    u4VIntrPrio         : 4;    /* V_INTR_PRIO */
        uint32_t    u1IgnoreTPR         : 1;    /* V_IGN_TPR */
        uint32_t    u3Reserved          : 3;
        uint32_t    u1VIntrMasking      : 1;    /* V_INTR_MASKING */
        uint32_t    u1VGifEnable        : 1;    /* VGIF enable */
        uint32_t    u4Reserved          : 4;
        uint32_t    u1X2AvicEnable      : 1;    /* X2AVIC enable */
        uint32_t    u1AvicEnable        : 1;    /* AVIC enable */
        uint32_t    u8VIntrVector       : 8;    /* V_INTR_VECTOR */
        uint32_t    u24Reserved         : 24;
    } n;
    uint64_t    u;
} SVMINTCTRL;
/** Pointer to an SVMINTCTRL structure. */
typedef SVMINTCTRL *PSVMINTCTRL;
/** Pointer to a const SVMINTCTRL structure. */
typedef const SVMINTCTRL *PCSVMINTCTRL;

/**
 * SVM TLB control structure.
 */
typedef union
{
    struct
    {
        uint32_t    u32ASID             : 32;
        uint32_t    u8TLBFlush          : 8;
        uint32_t    u24Reserved         : 24;
    } n;
    uint64_t    u;
} SVMTLBCTRL;

/**
 * SVM IOIO exit info. structure (EXITINFO1 for IOIO intercepts).
 */
typedef union
{
    struct
    {
        uint32_t    u1Type              : 1;   /**< Bit 0: 0 = out, 1 = in */
        uint32_t    u1Reserved          : 1;   /**< Bit 1: Reserved */
        uint32_t    u1Str               : 1;   /**< Bit 2: String I/O (1) or not (0). */
        uint32_t    u1Rep               : 1;   /**< Bit 3: Repeat prefixed string I/O. */
        uint32_t    u1Op8               : 1;   /**< Bit 4: 8-bit operand. */
        uint32_t    u1Op16              : 1;   /**< Bit 5: 16-bit operand. */
        uint32_t    u1Op32              : 1;   /**< Bit 6: 32-bit operand. */
        uint32_t    u1Addr16            : 1;   /**< Bit 7: 16-bit address size. */
        uint32_t    u1Addr32            : 1;   /**< Bit 8: 32-bit address size. */
        uint32_t    u1Addr64            : 1;   /**< Bit 9: 64-bit address size. */
        uint32_t    u3Seg               : 3;   /**< Bits 12:10: Effective segment number. Added w/ decode assist in APM v3.17. */
        uint32_t    u3Reserved          : 3;
        uint32_t    u16Port             : 16;  /**< Bits 31:16: Port number. */
    } n;
    uint32_t    u;
} SVMIOIOEXITINFO;
/** Pointer to an SVM IOIO exit info. structure. */
typedef SVMIOIOEXITINFO *PSVMIOIOEXITINFO;
/** Pointer to a const SVM IOIO exit info. structure. */
typedef const SVMIOIOEXITINFO *PCSVMIOIOEXITINFO;

/** 8-bit IO transfer. */
#define SVM_IOIO_8_BIT_OP               RT_BIT_32(4)
/** 16-bit IO transfer. */
#define SVM_IOIO_16_BIT_OP              RT_BIT_32(5)
/** 32-bit IO transfer. */
#define SVM_IOIO_32_BIT_OP              RT_BIT_32(6)
/** Number of bits to shift right to get the operand sizes. */
#define SVM_IOIO_OP_SIZE_SHIFT          4
/** Mask of all possible IO transfer sizes. */
#define SVM_IOIO_OP_SIZE_MASK           (SVM_IOIO_8_BIT_OP | SVM_IOIO_16_BIT_OP | SVM_IOIO_32_BIT_OP)
/** 16-bit address for the IO buffer. */
#define SVM_IOIO_16_BIT_ADDR            RT_BIT_32(7)
/** 32-bit address for the IO buffer. */
#define SVM_IOIO_32_BIT_ADDR            RT_BIT_32(8)
/** 64-bit address for the IO buffer. */
#define SVM_IOIO_64_BIT_ADDR            RT_BIT_32(9)
/** Number of bits to shift right to get the address sizes. */
#define SVM_IOIO_ADDR_SIZE_SHIFT        7
/** Mask of all the IO address sizes. */
#define SVM_IOIO_ADDR_SIZE_MASK         (SVM_IOIO_16_BIT_ADDR | SVM_IOIO_32_BIT_ADDR | SVM_IOIO_64_BIT_ADDR)
/** Number of bits to shift right to get the IO port number. */
#define SVM_IOIO_PORT_SHIFT             16
/** IO write. */
#define SVM_IOIO_WRITE                  0
/** IO read. */
#define SVM_IOIO_READ                   1
/**
 * SVM IOIO transfer type.
 */
typedef enum
{
    SVMIOIOTYPE_OUT = SVM_IOIO_WRITE,
    SVMIOIOTYPE_IN  = SVM_IOIO_READ
} SVMIOIOTYPE;

/**
 * SVM AVIC.
 */
typedef union
{
    struct
    {
        RT_GCC_EXTENSION uint64_t u12Reserved0        : 12;
        RT_GCC_EXTENSION uint64_t u40Addr             : 40;
        RT_GCC_EXTENSION uint64_t u12Reserved1        : 12;
    } n;
    uint64_t    u;
} SVMAVIC;
AssertCompileSize(SVMAVIC, 8);

/**
 * SVM AVIC PHYSICAL_TABLE pointer.
 */
typedef union
{
    struct
    {
        RT_GCC_EXTENSION uint64_t u8LastGuestCoreId   : 8;
        RT_GCC_EXTENSION uint64_t u4Reserved          : 4;
        RT_GCC_EXTENSION uint64_t u40Addr             : 40;
        RT_GCC_EXTENSION uint64_t u12Reserved         : 12;
    } n;
    uint64_t    u;
} SVMAVICPHYS;
AssertCompileSize(SVMAVICPHYS, 8);

/**
 * SVM VMSA pointer.
 */
typedef union
{
    struct
    {
        RT_GCC_EXTENSION uint64_t u12Reserved0        : 12;
        RT_GCC_EXTENSION uint64_t u40Addr             : 40;
        RT_GCC_EXTENSION uint64_t u12Reserved1        : 12;
    } n;
    uint64_t    u;
} SVMVMSA;
AssertCompileSize(SVMVMSA, 8);

/**
 * SVM Nested Paging struct.
 */
typedef union
{
    struct
    {
        uint32_t    u1NestedPaging :  1;
        uint32_t    u1Sev          :  1;
        uint32_t    u1SevEs        :  1;
        uint32_t    u29Reserved    : 29;
    } n;
    uint64_t    u;
} SVMNP;
AssertCompileSize(SVMNP, 8);

/**
 * SVM Interrupt shadow struct.
 */
typedef union
{
    struct
    {
        uint32_t    u1IntShadow    :  1;
        uint32_t    u1GuestIntMask :  1;
        uint32_t    u30Reserved    : 30;
    } n;
    uint64_t    u;
} SVMINTSHADOW;
AssertCompileSize(SVMINTSHADOW, 8);

/**
 * SVM LBR virtualization struct.
 */
typedef union
{
    struct
    {
        uint32_t    u1LbrVirt          :  1;
        uint32_t    u1VirtVmsaveVmload :  1;
        uint32_t    u30Reserved        : 30;
    } n;
    uint64_t    u;
} SVMLBRVIRT;
AssertCompileSize(SVMLBRVIRT, 8);

/** Maximum number of bytes in the Guest-instruction bytes field. */
#define SVM_CTRL_GUEST_INSTR_BYTES_MAX      15

/**
 * SVM VMCB control area.
 */
#pragma pack(1)
typedef struct
{
    /** Offset 0x00 - Intercept reads of CR0-CR15. */
    uint16_t        u16InterceptRdCRx;
    /** Offset 0x02 - Intercept writes to CR0-CR15. */
    uint16_t        u16InterceptWrCRx;
    /** Offset 0x04 - Intercept reads of DR0-DR15. */
    uint16_t        u16InterceptRdDRx;
    /** Offset 0x06 - Intercept writes to DR0-DR15. */
    uint16_t        u16InterceptWrDRx;
    /** Offset 0x08 - Intercept exception vectors 0-31. */
    uint32_t        u32InterceptXcpt;
    /** Offset 0x0c - Intercept control. */
    uint64_t        u64InterceptCtrl;
    /** Offset 0x14 - Intercept control 2. */
    uint64_t        u64InterceptCtrl2;
    /** Offset 0x14-0x3f - Reserved. */
    uint8_t         u8Reserved0[0x3c - 0x1c];
    /** Offset 0x3c - PAUSE filter threshold.  */
    uint16_t        u16PauseFilterThreshold;
    /** Offset 0x3e - PAUSE intercept filter count. */
    uint16_t        u16PauseFilterCount;
    /** Offset 0x40 - Physical address of IOPM. */
    uint64_t        u64IOPMPhysAddr;
    /** Offset 0x48 - Physical address of MSRPM. */
    uint64_t        u64MSRPMPhysAddr;
    /** Offset 0x50 - TSC Offset. */
    uint64_t        u64TSCOffset;
    /** Offset 0x58 - TLB control field. */
    SVMTLBCTRL      TLBCtrl;
    /** Offset 0x60 - Interrupt control field. */
    SVMINTCTRL      IntCtrl;
    /** Offset 0x68 - Interrupt shadow. */
    SVMINTSHADOW    IntShadow;
    /** Offset 0x70 - Exit code. */
    uint64_t        u64ExitCode;
    /** Offset 0x78 - Exit info 1. */
    uint64_t        u64ExitInfo1;
    /** Offset 0x80 - Exit info 2. */
    uint64_t        u64ExitInfo2;
    /** Offset 0x88 - Exit Interrupt info. */
    SVMEVENT        ExitIntInfo;
    /** Offset 0x90 - Nested Paging control. */
    SVMNP           NestedPagingCtrl;
    /** Offset 0x98 - AVIC APIC BAR.  */
    SVMAVIC         AvicBar;
    /** Offset 0xa0-0xa7 - Reserved. */
    uint8_t         u8Reserved1[0xa8 - 0xa0];
    /** Offset 0xa8 - Event injection. */
    SVMEVENT        EventInject;
    /** Offset 0xb0 - Host CR3 for nested paging. */
    uint64_t        u64NestedPagingCR3;
    /** Offset 0xb8 - LBR Virtualization. */
    SVMLBRVIRT      LbrVirt;
    /** Offset 0xc0 - VMCB Clean Bits. */
    uint32_t        u32VmcbCleanBits;
    uint32_t        u32Reserved0;
    /** Offset 0xc8 - Next sequential instruction pointer. */
    uint64_t        u64NextRIP;
    /** Offset 0xd0 - Number of bytes fetched. */
    uint8_t         cbInstrFetched;
    /** Offset 0xd1 - Guest instruction bytes. */
    uint8_t         abInstr[SVM_CTRL_GUEST_INSTR_BYTES_MAX];
    /** Offset 0xe0 - AVIC APIC_BACKING_PAGE pointer. */
    SVMAVIC         AvicBackingPagePtr;
    /** Offset 0xe8-0xef - Reserved. */
    uint8_t         u8Reserved2[0xf0 - 0xe8];
    /** Offset 0xf0 - AVIC LOGICAL_TABLE pointer. */
    SVMAVIC         AvicLogicalTablePtr;
    /** Offset 0xf8 - AVIC PHYSICAL_TABLE pointer. */
    SVMAVICPHYS     AvicPhysicalTablePtr;
    /** Offset 0x100-0x107 - Reserved. */
    uint8_t         u8Reserved3[0x108 - 0x100];
    /** Offset 0x108 - VMSA pointer. */
    SVMVMSA         VmsaPtr;
    /** Offset 0x110 - VMGEXIT RAX register. */
    uint64_t        u64VmgExitRax;
    /** Offset 0x118 - VMGEXIT CPL value. */
    uint8_t         u8VmgExitCpl;
    /** Offset 0x119-0x11f - Reserved. */
    uint8_t         u8Reserved4[0x120 - 0x119];
    /** Offset 0x120 - Bus Lock Threshold Counter. */
    uint16_t        u16BusLockThresholdCnt;
    /** Offset 0x122-0x3ff - Reserved. */
    uint8_t         u8Reserved5[0x400 - 0x122];
} SVMVMCBCTRL;
#pragma pack()
/** Pointer to the SVMVMCBSTATESAVE structure. */
typedef SVMVMCBCTRL *PSVMVMCBCTRL;
/** Pointer to a const SVMVMCBSTATESAVE structure. */
typedef const SVMVMCBCTRL *PCSVMVMCBCTRL;
AssertCompileSize(SVMVMCBCTRL, 0x400);
AssertCompileMemberOffset(SVMVMCBCTRL, u16InterceptRdCRx,       0x00);
AssertCompileMemberOffset(SVMVMCBCTRL, u16InterceptWrCRx,       0x02);
AssertCompileMemberOffset(SVMVMCBCTRL, u16InterceptRdDRx,       0x04);
AssertCompileMemberOffset(SVMVMCBCTRL, u16InterceptWrDRx,       0x06);
AssertCompileMemberOffset(SVMVMCBCTRL, u32InterceptXcpt,        0x08);
AssertCompileMemberOffset(SVMVMCBCTRL, u64InterceptCtrl,        0x0c);
AssertCompileMemberOffset(SVMVMCBCTRL, u64InterceptCtrl2,       0x14);
AssertCompileMemberOffset(SVMVMCBCTRL, u8Reserved0,             0x1c);
AssertCompileMemberOffset(SVMVMCBCTRL, u16PauseFilterThreshold, 0x3c);
AssertCompileMemberOffset(SVMVMCBCTRL, u16PauseFilterCount,     0x3e);
AssertCompileMemberOffset(SVMVMCBCTRL, u64IOPMPhysAddr,         0x40);
AssertCompileMemberOffset(SVMVMCBCTRL, u64MSRPMPhysAddr,        0x48);
AssertCompileMemberOffset(SVMVMCBCTRL, u64TSCOffset,            0x50);
AssertCompileMemberOffset(SVMVMCBCTRL, TLBCtrl,                 0x58);
AssertCompileMemberOffset(SVMVMCBCTRL, IntCtrl,                 0x60);
AssertCompileMemberOffset(SVMVMCBCTRL, IntShadow,               0x68);
AssertCompileMemberOffset(SVMVMCBCTRL, u64ExitCode,             0x70);
AssertCompileMemberOffset(SVMVMCBCTRL, u64ExitInfo1,            0x78);
AssertCompileMemberOffset(SVMVMCBCTRL, u64ExitInfo2,            0x80);
AssertCompileMemberOffset(SVMVMCBCTRL, ExitIntInfo,             0x88);
AssertCompileMemberOffset(SVMVMCBCTRL, NestedPagingCtrl,        0x90);
AssertCompileMemberOffset(SVMVMCBCTRL, AvicBar,                 0x98);
AssertCompileMemberOffset(SVMVMCBCTRL, u8Reserved1,             0xa0);
AssertCompileMemberOffset(SVMVMCBCTRL, EventInject,             0xa8);
AssertCompileMemberOffset(SVMVMCBCTRL, u64NestedPagingCR3,      0xb0);
AssertCompileMemberOffset(SVMVMCBCTRL, LbrVirt,                 0xb8);
AssertCompileMemberOffset(SVMVMCBCTRL, u32VmcbCleanBits,        0xc0);
AssertCompileMemberOffset(SVMVMCBCTRL, u64NextRIP,              0xc8);
AssertCompileMemberOffset(SVMVMCBCTRL, cbInstrFetched,          0xd0);
AssertCompileMemberOffset(SVMVMCBCTRL, abInstr,                 0xd1);
AssertCompileMemberOffset(SVMVMCBCTRL, AvicBackingPagePtr,      0xe0);
AssertCompileMemberOffset(SVMVMCBCTRL, u8Reserved2,             0xe8);
AssertCompileMemberOffset(SVMVMCBCTRL, AvicLogicalTablePtr,     0xf0);
AssertCompileMemberOffset(SVMVMCBCTRL, AvicPhysicalTablePtr,    0xf8);
AssertCompileMemberOffset(SVMVMCBCTRL, u8Reserved3,             0x100);
AssertCompileMemberOffset(SVMVMCBCTRL, VmsaPtr,                 0x108);
AssertCompileMemberOffset(SVMVMCBCTRL, u64VmgExitRax,           0x110);
AssertCompileMemberOffset(SVMVMCBCTRL, u8VmgExitCpl,            0x118);
AssertCompileMemberOffset(SVMVMCBCTRL, u8Reserved4,             0x119);
AssertCompileMemberOffset(SVMVMCBCTRL, u16BusLockThresholdCnt,  0x120);
AssertCompileMemberOffset(SVMVMCBCTRL, u8Reserved5,             0x122);
AssertCompileMemberSize(SVMVMCBCTRL,   abInstr,                 0x0f);

/**
 * SVM VMCB state save area.
 */
#pragma pack(1)
typedef struct
{
    /** Offset 0x400 - Guest ES register + hidden parts. */
    SVMSELREG   ES;
    /** Offset 0x410 - Guest CS register + hidden parts. */
    SVMSELREG   CS;
    /** Offset 0x420 - Guest SS register + hidden parts. */
    SVMSELREG   SS;
    /** Offset 0x430 - Guest DS register + hidden parts. */
    SVMSELREG   DS;
    /** Offset 0x440 - Guest FS register + hidden parts. */
    SVMSELREG   FS;
    /** Offset 0x450 - Guest GS register + hidden parts. */
    SVMSELREG   GS;
    /** Offset 0x460 - Guest GDTR register. */
    SVMGDTR     GDTR;
    /** Offset 0x470 - Guest LDTR register + hidden parts. */
    SVMSELREG   LDTR;
    /** Offset 0x480 - Guest IDTR register. */
    SVMIDTR     IDTR;
    /** Offset 0x490 - Guest TR register + hidden parts. */
    SVMSELREG   TR;
    /** Offset 0x4A0-0x4CA - Reserved. */
    uint8_t     u8Reserved0[0x4cb - 0x4a0];
    /** Offset 0x4CB - CPL. */
    uint8_t     u8CPL;
    /** Offset 0x4CC-0x4CF - Reserved. */
    uint8_t     u8Reserved1[0x4d0 - 0x4cc];
    /** Offset 0x4D0 - EFER. */
    uint64_t    u64EFER;
    /** Offset 0x4D8-0x547 - Reserved. */
    uint8_t     u8Reserved2[0x548 - 0x4d8];
    /** Offset 0x548 - CR4. */
    uint64_t    u64CR4;
    /** Offset 0x550 - CR3. */
    uint64_t    u64CR3;
    /** Offset 0x558 - CR0. */
    uint64_t    u64CR0;
    /** Offset 0x560 - DR7. */
    uint64_t    u64DR7;
    /** Offset 0x568 - DR6. */
    uint64_t    u64DR6;
    /** Offset 0x570 - RFLAGS. */
    uint64_t    u64RFlags;
    /** Offset 0x578 - RIP. */
    uint64_t    u64RIP;
    /** Offset 0x580-0x5D7 - Reserved. */
    uint8_t     u8Reserved3[0x5d8 - 0x580];
    /** Offset 0x5D8 - RSP. */
    uint64_t    u64RSP;
    /** Offset 0x5E0-0x5F7 - Reserved. */
    uint8_t     u8Reserved4[0x5f8 - 0x5e0];
    /** Offset 0x5F8 - RAX. */
    uint64_t    u64RAX;
    /** Offset 0x600 - STAR. */
    uint64_t    u64STAR;
    /** Offset 0x608 - LSTAR. */
    uint64_t    u64LSTAR;
    /** Offset 0x610 - CSTAR. */
    uint64_t    u64CSTAR;
    /** Offset 0x618 - SFMASK. */
    uint64_t    u64SFMASK;
    /** Offset 0x620 - KernelGSBase. */
    uint64_t    u64KernelGSBase;
    /** Offset 0x628 - SYSENTER_CS. */
    uint64_t    u64SysEnterCS;
    /** Offset 0x630 - SYSENTER_ESP. */
    uint64_t    u64SysEnterESP;
    /** Offset 0x638 - SYSENTER_EIP. */
    uint64_t    u64SysEnterEIP;
    /** Offset 0x640 - CR2. */
    uint64_t    u64CR2;
    /** Offset 0x648-0x667 - Reserved. */
    uint8_t     u8Reserved5[0x668 - 0x648];
    /** Offset 0x668 - PAT (Page Attribute Table) MSR. */
    uint64_t    u64PAT;
    /** Offset 0x670 - DBGCTL. */
    uint64_t    u64DBGCTL;
    /** Offset 0x678 - BR_FROM. */
    uint64_t    u64BR_FROM;
    /** Offset 0x680 - BR_TO. */
    uint64_t    u64BR_TO;
    /** Offset 0x688 - LASTEXCPFROM. */
    uint64_t    u64LASTEXCPFROM;
    /** Offset 0x690 - LASTEXCPTO. */
    uint64_t    u64LASTEXCPTO;
} SVMVMCBSTATESAVE;
#pragma pack()
/** Pointer to the SVMVMCBSTATESAVE structure. */
typedef SVMVMCBSTATESAVE *PSVMVMCBSTATESAVE;
/** Pointer to a const SVMVMCBSTATESAVE structure. */
typedef const SVMVMCBSTATESAVE *PCSVMVMCBSTATESAVE;
AssertCompileSize(SVMVMCBSTATESAVE, 0x298);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, ES,              0x400 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, CS,              0x410 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, SS,              0x420 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, DS,              0x430 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, FS,              0x440 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, GS,              0x450 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, GDTR,            0x460 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, LDTR,            0x470 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, IDTR,            0x480 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, TR,              0x490 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8Reserved0,     0x4a0 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8CPL,           0x4cb - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8Reserved1,     0x4cc - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64EFER,         0x4d0 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8Reserved2,     0x4d8 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64CR4,          0x548 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64CR3,          0x550 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64CR0,          0x558 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64DR7,          0x560 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64DR6,          0x568 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64RFlags,       0x570 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64RIP,          0x578 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8Reserved3,     0x580 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64RSP,          0x5d8 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8Reserved4,     0x5e0 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64RAX,          0x5f8 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64STAR,         0x600 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64LSTAR,        0x608 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64CSTAR,        0x610 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64SFMASK,       0x618 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64KernelGSBase, 0x620 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64SysEnterCS,   0x628 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64SysEnterESP,  0x630 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64SysEnterEIP,  0x638 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64CR2,          0x640 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u8Reserved5,     0x648 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64PAT,          0x668 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64DBGCTL,       0x670 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64BR_FROM,      0x678 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64BR_TO,        0x680 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64LASTEXCPFROM, 0x688 - 0x400);
AssertCompileMemberOffset(SVMVMCBSTATESAVE, u64LASTEXCPTO,   0x690 - 0x400);

/**
 * SVM VM Control Block. (VMCB)
 */
#pragma pack(1)
typedef struct SVMVMCB
{
    /** Offset 0x00 - Control area. */
    SVMVMCBCTRL ctrl;
    /** Offset 0x400 - State save area. */
    SVMVMCBSTATESAVE guest;
    /** Offset 0x698-0xFFF- Reserved. */
    uint8_t     u8Reserved1[0x1000 - 0x698];
} SVMVMCB;
#pragma pack()
/** Pointer to the SVMVMCB structure. */
typedef SVMVMCB *PSVMVMCB;
/** Pointer to a const SVMVMCB structure. */
typedef const SVMVMCB *PCSVMVMCB;
AssertCompileMemberOffset(SVMVMCB, ctrl,         0x00);
AssertCompileMemberOffset(SVMVMCB, guest,        0x400);
AssertCompileMemberOffset(SVMVMCB, u8Reserved1,  0x698);
AssertCompileSize(SVMVMCB, 0x1000);

/**
 * SVM MSRs.
 */
typedef struct SVMMSRS
{
    /** HWCR MSR. */
    uint64_t        u64MsrHwcr;
    /** Reserved for future. */
    uint64_t        u64Padding[27];
} SVMMSRS;
AssertCompileSizeAlignment(SVMMSRS, 8);
AssertCompileSize(SVMMSRS, 224);
/** Pointer to a SVMMSRS struct. */
typedef SVMMSRS *PSVMMSRS;
/** Pointer to a const SVMMSRS struct. */
typedef const SVMMSRS *PCSVMMSRS;

/**
 * SVM VM-exit auxiliary information.
 *
 * This includes information that isn't necessarily stored in the guest-CPU
 * context but provided as part of \#VMEXITs.
 */
typedef struct
{
    uint64_t        u64ExitCode;
    uint64_t        u64ExitInfo1;
    uint64_t        u64ExitInfo2;
    SVMEVENT        ExitIntInfo;
} SVMEXITAUX;
/** Pointer to a SVMEXITAUX struct. */
typedef SVMEXITAUX *PSVMEXITAUX;
/** Pointer to a const SVMEXITAUX struct. */
typedef const SVMEXITAUX *PCSVMEXITAUX;

/**
 * Segment attribute conversion between CPU and AMD-V VMCB format.
 *
 * The CPU format of the segment attribute is described in X86DESCATTRBITS
 * which is 16-bits (i.e. includes 4 bits of the segment limit).
 *
 * The AMD-V VMCB format the segment attribute is compact 12-bits (strictly
 * only the attribute bits and nothing else). Upper 4-bits are unused.
 */
#define HMSVM_CPU_2_VMCB_SEG_ATTR(a)       ( ((a) & 0xff) | (((a) & 0xf000) >> 4) )
#define HMSVM_VMCB_2_CPU_SEG_ATTR(a)       ( ((a) & 0xff) | (((a) & 0x0f00) << 4) )

/** @def HMSVM_SEG_REG_COPY_TO_VMCB
 * Copies the specified segment register to a VMCB from a virtual CPU context.
 *
 * @param   a_pCtx              The virtual-CPU context.
 * @param   a_pVmcbStateSave    Pointer to the VMCB state-save area.
 * @param   a_REG               The segment register in the VMCB state-save
 *                              struct (ES/CS/SS/DS).
 * @param   a_reg               The segment register in the virtual CPU struct
 *                              (es/cs/ss/ds).
 */
#define HMSVM_SEG_REG_COPY_TO_VMCB(a_pCtx, a_pVmcbStateSave, a_REG, a_reg) \
    do \
    { \
        Assert((a_pCtx)->a_reg.fFlags & CPUMSELREG_FLAGS_VALID);       \
        Assert((a_pCtx)->a_reg.ValidSel == (a_pCtx)->a_reg.Sel);       \
        (a_pVmcbStateSave)->a_REG.u16Sel   = (a_pCtx)->a_reg.Sel;      \
        (a_pVmcbStateSave)->a_REG.u32Limit = (a_pCtx)->a_reg.u32Limit; \
        (a_pVmcbStateSave)->a_REG.u64Base  = (a_pCtx)->a_reg.u64Base;  \
        (a_pVmcbStateSave)->a_REG.u16Attr  = HMSVM_CPU_2_VMCB_SEG_ATTR((a_pCtx)->a_reg.Attr.u); \
    } while (0)

/** @def HMSVM_SEG_REG_COPY_FROM_VMCB
 * Copies the specified segment register from the VMCB to a virtual CPU
 * context.
 *
 * @param   a_pCtx              The virtual-CPU context.
 * @param   a_pVmcbStateSave    Pointer to the VMCB state-save area.
 * @param   a_REG               The segment register in the VMCB state-save
 *                              struct (ES/CS/SS/DS).
 * @param   a_reg               The segment register in the virtual CPU struct
 *                              (es/ds/ss/ds).
 */
#define HMSVM_SEG_REG_COPY_FROM_VMCB(a_pCtx, a_pVmcbStateSave, a_REG, a_reg) \
    do \
    { \
        (a_pCtx)->a_reg.Sel      = (a_pVmcbStateSave)->a_REG.u16Sel;   \
        (a_pCtx)->a_reg.ValidSel = (a_pVmcbStateSave)->a_REG.u16Sel;   \
        (a_pCtx)->a_reg.fFlags   = CPUMSELREG_FLAGS_VALID;             \
        (a_pCtx)->a_reg.u32Limit = (a_pVmcbStateSave)->a_REG.u32Limit; \
        (a_pCtx)->a_reg.u64Base  = (a_pVmcbStateSave)->a_REG.u64Base;  \
        (a_pCtx)->a_reg.Attr.u   = HMSVM_VMCB_2_CPU_SEG_ATTR((a_pVmcbStateSave)->a_REG.u16Attr); \
    } while (0)


/** @defgroup grp_hm_svm_hwexec    SVM Hardware-assisted execution Helpers
 *
 * These functions are only here because the inline functions in cpum.h calls them.
 * Don't add any more functions here unless there is no other option.
 * @{
 */
VMM_INT_DECL(bool)     HMGetGuestSvmCtrlIntercepts(PCVMCPU pVCpu, uint64_t *pu64Intercepts);
VMM_INT_DECL(bool)     HMGetGuestSvmReadCRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts);
VMM_INT_DECL(bool)     HMGetGuestSvmWriteCRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts);
VMM_INT_DECL(bool)     HMGetGuestSvmReadDRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts);
VMM_INT_DECL(bool)     HMGetGuestSvmWriteDRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts);
VMM_INT_DECL(bool)     HMGetGuestSvmXcptIntercepts(PCVMCPU pVCpu, uint32_t *pu32Intercepts);
VMM_INT_DECL(bool)     HMGetGuestSvmVirtIntrMasking(PCVMCPU pVCpu, bool *pfVIntrMasking);
VMM_INT_DECL(bool)     HMGetGuestSvmNestedPaging(PCVMCPU pVCpu, bool *pfNestedPagingCtrl);
VMM_INT_DECL(bool)     HMGetGuestSvmPauseFilterCount(PCVMCPU pVCpu, uint16_t *pu16PauseFilterCount);
VMM_INT_DECL(bool)     HMGetGuestSvmTscOffset(PCVMCPU pVCpu, uint64_t *pu64TscOffset);
/** @} */


/** @} */

#endif /* !VBOX_INCLUDED_vmm_hm_svm_h */

