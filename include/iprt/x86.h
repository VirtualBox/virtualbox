/** @file
 * IPRT - X86 and AMD64 Structures and Definitions.
 *
 * @note x86.mac is generated from this file by running 'kmk incs' in the root.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_x86_h
#define IPRT_INCLUDED_x86_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_FOR_DTRACE_LIB
# ifndef __ASSEMBLER__
#  include <iprt/types.h>
#  include <iprt/assert.h>
# else
#  include <iprt/stdint.h>
#  include <iprt/assertcompile.h>
# endif
#else
# pragma D depends_on library vbox-types.d
#endif

/** Workaround for Solaris sys/regset.h defining CS, DS and sys/controlregs.h
 * defining MSR_IA32_FLUSH_CMD and MSR_AMD_VIRT_SPEC_CTL */
#ifdef RT_OS_SOLARIS
# undef CS
# undef DS
# undef MSR_IA32_FLUSH_CMD
# undef MSR_AMD_VIRT_SPEC_CTL
#endif

/** @defgroup grp_rt_x86   x86 Types and Definitions
 * @ingroup grp_rt
 * @{
 */

#ifndef __ASSEMBLER__

# ifndef VBOX_FOR_DTRACE_LIB
/**
 * EFLAGS Bits.
 */
typedef struct X86EFLAGSBITS
{
    /** Bit 0 - CF - Carry flag - Status flag. */
    unsigned    u1CF : 1;
    /** Bit 1 -  1 - Reserved flag. */
    unsigned    u1Reserved0 : 1;
    /** Bit 2 - PF - Parity flag - Status flag. */
    unsigned    u1PF : 1;
    /** Bit 3 -  0 - Reserved flag. */
    unsigned    u1Reserved1 : 1;
    /** Bit 4 - AF - Auxiliary carry flag - Status flag. */
    unsigned    u1AF : 1;
    /** Bit 5 -  0 - Reserved flag. */
    unsigned    u1Reserved2 : 1;
    /** Bit 6 - ZF - Zero flag - Status flag. */
    unsigned    u1ZF : 1;
    /** Bit 7 - SF - Signed flag - Status flag. */
    unsigned    u1SF : 1;
    /** Bit 8 - TF - Trap flag - System flag. */
    unsigned    u1TF : 1;
    /** Bit 9 - IF - Interrupt flag - System flag. */
    unsigned    u1IF : 1;
    /** Bit 10 - DF - Direction flag - Control flag. */
    unsigned    u1DF : 1;
    /** Bit 11 - OF - Overflow flag - Status flag. */
    unsigned    u1OF : 1;
    /** Bit 12-13 - IOPL - I/O privilege level flag - System flag. */
    unsigned    u2IOPL : 2;
    /** Bit 14 - NT - Nested task flag - System flag. */
    unsigned    u1NT : 1;
    /** Bit 15 -  0 - Reserved flag. */
    unsigned    u1Reserved3 : 1;
    /** Bit 16 - RF - Resume flag - System flag. */
    unsigned    u1RF : 1;
    /** Bit 17 - VM - Virtual 8086 mode - System flag. */
    unsigned    u1VM : 1;
    /** Bit 18 - AC - Alignment check flag - System flag. Works with CR0.AM. */
    unsigned    u1AC : 1;
    /** Bit 19 - VIF - Virtual interrupt flag - System flag. */
    unsigned    u1VIF : 1;
    /** Bit 20 - VIP - Virtual interrupt pending flag - System flag. */
    unsigned    u1VIP : 1;
    /** Bit 21 - ID - CPUID flag - System flag. If this responds to flipping CPUID is supported. */
    unsigned    u1ID : 1;
    /** Bit 22-31 - 0 - Reserved flag. */
    unsigned    u10Reserved4 : 10;
} X86EFLAGSBITS;
/** Pointer to EFLAGS bits. */
typedef X86EFLAGSBITS *PX86EFLAGSBITS;
/** Pointer to const EFLAGS bits. */
typedef const X86EFLAGSBITS *PCX86EFLAGSBITS;
# endif /* !VBOX_FOR_DTRACE_LIB */

/**
 * EFLAGS.
 */
typedef union X86EFLAGS
{
    /** The plain unsigned view. */
    uint32_t        u;
# ifndef VBOX_FOR_DTRACE_LIB
    /** The bitfield view. */
    X86EFLAGSBITS   Bits;
# endif
    /** The 8-bit view. */
    uint8_t         au8[4];
    /** The 16-bit view. */
    uint16_t        au16[2];
    /** The 32-bit view. */
    uint32_t        au32[1];
    /** The 32-bit view. */
    uint32_t        u32;
} X86EFLAGS;
/** Pointer to EFLAGS. */
typedef X86EFLAGS *PX86EFLAGS;
/** Pointer to const EFLAGS. */
typedef const X86EFLAGS *PCX86EFLAGS;

/**
 * RFLAGS (32 upper bits are reserved).
 */
typedef union X86RFLAGS
{
    /** The plain unsigned view. */
    uint64_t        u;
# ifndef VBOX_FOR_DTRACE_LIB
    /** The bitfield view. */
    X86EFLAGSBITS   Bits;
# endif
    /** The 8-bit view. */
    uint8_t         au8[8];
    /** The 16-bit view. */
    uint16_t        au16[4];
    /** The 32-bit view. */
    uint32_t        au32[2];
    /** The 64-bit view. */
    uint64_t        au64[1];
    /** The 64-bit view. */
    uint64_t        u64;
} X86RFLAGS;
/** Pointer to RFLAGS. */
typedef X86RFLAGS *PX86RFLAGS;
/** Pointer to const RFLAGS. */
typedef const X86RFLAGS *PCX86RFLAGS;

#endif /* !__ASSEMBLER__ */


/** @name EFLAGS
 * @{
 */
/** Bit 0 - CF - Carry flag - Status flag. */
#define X86_EFL_CF          RT_BIT_32(0)
#define X86_EFL_CF_BIT      0
/** Bit 1 - Reserved, reads as 1. */
#define X86_EFL_1           RT_BIT_32(1)
#define X86_EFL_1_BIT       1
/** Bit 2 - PF - Parity flag - Status flag. */
#define X86_EFL_PF          RT_BIT_32(2)
#define X86_EFL_PF_BIT      2
/** Bit 4 - AF - Auxiliary carry flag - Status flag. */
#define X86_EFL_AF          RT_BIT_32(4)
#define X86_EFL_AF_BIT      4
/** Bit 6 - ZF - Zero flag - Status flag. */
#define X86_EFL_ZF          RT_BIT_32(6)
#define X86_EFL_ZF_BIT      6
/** Bit 7 - SF - Signed flag - Status flag. */
#define X86_EFL_SF          RT_BIT_32(7)
#define X86_EFL_SF_BIT      7
/** Bit 8 - TF - Trap flag - System flag. */
#define X86_EFL_TF          RT_BIT_32(8)
#define X86_EFL_TF_BIT      8
/** Bit 9 - IF - Interrupt flag - System flag. */
#define X86_EFL_IF          RT_BIT_32(9)
#define X86_EFL_IF_BIT      9
/** Bit 10 - DF - Direction flag - Control flag. */
#define X86_EFL_DF          RT_BIT_32(10)
#define X86_EFL_DF_BIT      10
/** Bit 11 - OF - Overflow flag - Status flag. */
#define X86_EFL_OF          RT_BIT_32(11)
#define X86_EFL_OF_BIT      11
/** Bit 12-13 - IOPL - I/O privilege level flag - System flag. */
#define X86_EFL_IOPL        (RT_BIT_32(12) | RT_BIT_32(13))
/** Bit 14 - NT - Nested task flag - System flag. */
#define X86_EFL_NT          RT_BIT_32(14)
#define X86_EFL_NT_BIT      14
/** Bit 16 - RF - Resume flag - System flag. */
#define X86_EFL_RF          RT_BIT_32(16)
#define X86_EFL_RF_BIT      16
/** Bit 17 - VM - Virtual 8086 mode - System flag. */
#define X86_EFL_VM          RT_BIT_32(17)
#define X86_EFL_VM_BIT      17
/** Bit 18 - AC - Alignment check flag - System flag. Works with CR0.AM. */
#define X86_EFL_AC          RT_BIT_32(18)
#define X86_EFL_AC_BIT      18
/** Bit 19 - VIF - Virtual interrupt flag - System flag. */
#define X86_EFL_VIF         RT_BIT_32(19)
#define X86_EFL_VIF_BIT     19
/** Bit 20 - VIP - Virtual interrupt pending flag - System flag. */
#define X86_EFL_VIP         RT_BIT_32(20)
#define X86_EFL_VIP_BIT     20
/** Bit 21 - ID - CPUID flag - System flag. If this responds to flipping CPUID is supported. */
#define X86_EFL_ID          RT_BIT_32(21)
#define X86_EFL_ID_BIT      21
/** All live bits. */
#define X86_EFL_LIVE_MASK   UINT32_C(0x003f7fd5)
/** Read as 1 bits. */
#define X86_EFL_RA1_MASK    RT_BIT_32(1)
/** Read as 0 bits, excluding bits 31:22.
 * Bits 3, 5, 15, and 22 thru 31. */
#define X86_EFL_RAZ_MASK    UINT32_C(0xffc08028)
/** Read as 0 bits, excluding bits 31:22.
 * Bits 3, 5 and 15. */
#define X86_EFL_RAZ_LO_MASK UINT32_C(0x00008028)
/** IOPL shift. */
#define X86_EFL_IOPL_SHIFT  12
/** The IOPL level from the flags. */
#define X86_EFL_GET_IOPL(efl)   (((efl) >> X86_EFL_IOPL_SHIFT) & 3)
/** Bits restored by popf */
#define X86_EFL_POPF_BITS       (  X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_TF | X86_EFL_IF \
                                 | X86_EFL_DF | X86_EFL_OF | X86_EFL_IOPL | X86_EFL_NT | X86_EFL_AC | X86_EFL_ID )
/** Bits restored by popf */
#define X86_EFL_POPF_BITS_386   (  X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_TF | X86_EFL_IF \
                                 | X86_EFL_DF | X86_EFL_OF | X86_EFL_IOPL | X86_EFL_NT )
/** The status bits commonly updated by arithmetic instructions. */
#define X86_EFL_STATUS_BITS     ( X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_OF )
/** @} */


#ifndef __ASSEMBLER__

/** CPUID Feature information - ECX.
 * CPUID query with EAX=1.
 */
# ifndef VBOX_FOR_DTRACE_LIB
typedef struct X86CPUIDFEATECX
{
    /** Bit 0 - SSE3 - Supports SSE3 or not. */
    unsigned    u1SSE3 : 1;
    /** Bit 1 - PCLMULQDQ. */
    unsigned    u1PCLMULQDQ : 1;
    /** Bit 2 - DS Area 64-bit layout. */
    unsigned    u1DTE64 : 1;
    /** Bit 3 - MONITOR - Supports MONITOR/MWAIT. */
    unsigned    u1Monitor : 1;
    /** Bit 4 - CPL-DS - CPL Qualified Debug Store. */
    unsigned    u1CPLDS : 1;
    /** Bit 5 - VMX - Virtual Machine Technology. */
    unsigned    u1VMX : 1;
    /** Bit 6 - SMX: Safer Mode Extensions. */
    unsigned    u1SMX : 1;
    /** Bit 7 - EST - Enh. SpeedStep Tech. */
    unsigned    u1EST : 1;
    /** Bit 8 - TM2 - Terminal Monitor 2. */
    unsigned    u1TM2 : 1;
    /** Bit 9 - SSSE3 - Supplemental Streaming SIMD Extensions 3. */
    unsigned    u1SSSE3 : 1;
    /** Bit 10 - CNTX-ID - L1 Context ID. */
    unsigned    u1CNTXID : 1;
    /** Bit 11 - Reserved. */
    unsigned    u1Reserved1 : 1;
    /** Bit 12 - FMA. */
    unsigned    u1FMA : 1;
    /** Bit 13 - CX16 - CMPXCHG16B. */
    unsigned    u1CX16 : 1;
    /** Bit 14 - xTPR Update Control. Processor supports changing IA32_MISC_ENABLES[bit 23]. */
    unsigned    u1TPRUpdate : 1;
    /** Bit 15 - PDCM - Perf/Debug Capability MSR. */
    unsigned    u1PDCM : 1;
    /** Bit 16 - Reserved. */
    unsigned    u1Reserved2 : 1;
    /** Bit 17 - PCID - Process-context identifiers. */
    unsigned    u1PCID : 1;
    /** Bit 18 - Direct Cache Access. */
    unsigned    u1DCA : 1;
    /** Bit 19 - SSE4_1 - Supports SSE4_1 or not. */
    unsigned    u1SSE4_1 : 1;
    /** Bit 20 - SSE4_2 - Supports SSE4_2 or not. */
    unsigned    u1SSE4_2 : 1;
    /** Bit 21 - x2APIC. */
    unsigned    u1x2APIC : 1;
    /** Bit 22 - MOVBE - Supports MOVBE. */
    unsigned    u1MOVBE : 1;
    /** Bit 23 - POPCNT - Supports POPCNT. */
    unsigned    u1POPCNT : 1;
    /** Bit 24 - TSC-Deadline. */
    unsigned    u1TSCDEADLINE : 1;
    /** Bit 25 - AES. */
    unsigned    u1AES : 1;
    /** Bit 26 - XSAVE - Supports XSAVE. */
    unsigned    u1XSAVE : 1;
    /** Bit 27 - OSXSAVE - Supports OSXSAVE. */
    unsigned    u1OSXSAVE : 1;
    /** Bit 28 - AVX - Supports AVX instruction extensions. */
    unsigned    u1AVX : 1;
    /** Bit 29 - F16C - Supports 16-bit floating point conversion instructions. */
    unsigned    u1F16C : 1;
    /** Bit 30 - RDRAND - Supports RDRAND. */
    unsigned    u1RDRAND : 1;
    /** Bit 31 - Hypervisor present (we're a guest). */
    unsigned    u1HVP : 1;
} X86CPUIDFEATECX;
# else  /* VBOX_FOR_DTRACE_LIB */
typedef uint32_t X86CPUIDFEATECX;
# endif /* VBOX_FOR_DTRACE_LIB */
/** Pointer to CPUID Feature Information - ECX. */
typedef X86CPUIDFEATECX *PX86CPUIDFEATECX;
/** Pointer to const CPUID Feature Information - ECX. */
typedef const X86CPUIDFEATECX *PCX86CPUIDFEATECX;


/** CPUID Feature Information - EDX.
 * CPUID query with EAX=1.
 */
# ifndef VBOX_FOR_DTRACE_LIB /* DTrace different (brain-dead from a C pov) bitfield implementation */
typedef struct X86CPUIDFEATEDX
{
    /** Bit 0 - FPU - x87 FPU on Chip. */
    unsigned    u1FPU : 1;
    /** Bit 1 - VME - Virtual 8086 Mode Enhancements. */
    unsigned    u1VME : 1;
    /** Bit 2 - DE - Debugging extensions. */
    unsigned    u1DE : 1;
    /** Bit 3 - PSE - Page Size Extension. */
    unsigned    u1PSE : 1;
    /** Bit 4 - TSC - Time Stamp Counter. */
    unsigned    u1TSC : 1;
    /** Bit 5 - MSR - Model Specific Registers RDMSR and WRMSR Instructions. */
    unsigned    u1MSR : 1;
    /** Bit 6 - PAE - Physical Address Extension. */
    unsigned    u1PAE : 1;
    /** Bit 7 - MCE - Machine Check Exception. */
    unsigned    u1MCE : 1;
    /** Bit 8 - CX8 - CMPXCHG8B instruction. */
    unsigned    u1CX8 : 1;
    /** Bit 9 - APIC - APIC On-Chip. */
    unsigned    u1APIC : 1;
    /** Bit 10 - Reserved. */
    unsigned    u1Reserved1 : 1;
    /** Bit 11 - SEP - SYSENTER and SYSEXIT. */
    unsigned    u1SEP : 1;
    /** Bit 12 - MTRR - Memory Type Range Registers. */
    unsigned    u1MTRR : 1;
    /** Bit 13 - PGE - PTE Global Bit. */
    unsigned    u1PGE : 1;
    /** Bit 14 - MCA - Machine Check Architecture. */
    unsigned    u1MCA : 1;
    /** Bit 15 - CMOV - Conditional Move Instructions. */
    unsigned    u1CMOV : 1;
    /** Bit 16 - PAT - Page Attribute Table. */
    unsigned    u1PAT : 1;
    /** Bit 17 - PSE-36 - 36-bit Page Size Extension. */
    unsigned    u1PSE36 : 1;
    /** Bit 18 - PSN - Processor Serial Number. */
    unsigned    u1PSN : 1;
    /** Bit 19 - CLFSH - CLFLUSH Instruction. */
    unsigned    u1CLFSH : 1;
    /** Bit 20 - Reserved. */
    unsigned    u1Reserved2 : 1;
    /** Bit 21 - DS - Debug Store. */
    unsigned    u1DS : 1;
    /** Bit 22 - ACPI - Thermal Monitor and Software Controlled Clock Facilities. */
    unsigned    u1ACPI : 1;
    /** Bit 23 - MMX - Intel MMX 'Technology'. */
    unsigned    u1MMX : 1;
    /** Bit 24 - FXSR - FXSAVE and FXRSTOR Instructions. */
    unsigned    u1FXSR : 1;
    /** Bit 25 - SSE - SSE Support. */
    unsigned    u1SSE : 1;
    /** Bit 26 - SSE2 - SSE2 Support. */
    unsigned    u1SSE2 : 1;
    /** Bit 27 - SS - Self Snoop. */
    unsigned    u1SS : 1;
    /** Bit 28 - HTT - Hyper-Threading Technology. */
    unsigned    u1HTT : 1;
    /** Bit 29 - TM - Thermal Monitor. */
    unsigned    u1TM : 1;
    /** Bit 30 - Reserved - . */
    unsigned    u1Reserved3 : 1;
    /** Bit 31 - PBE - Pending Break Enabled. */
    unsigned    u1PBE : 1;
} X86CPUIDFEATEDX;
# else  /* VBOX_FOR_DTRACE_LIB */
typedef uint32_t X86CPUIDFEATEDX;
# endif /* VBOX_FOR_DTRACE_LIB */
/** Pointer to CPUID Feature Information - EDX. */
typedef X86CPUIDFEATEDX *PX86CPUIDFEATEDX;
/** Pointer to const CPUID Feature Information - EDX. */
typedef const X86CPUIDFEATEDX *PCX86CPUIDFEATEDX;

#endif /* !__ASSEMBLER__ */


/** @name CPUID Vendor information.
 * CPUID query with EAX=0.
 * @{
 */
#define X86_CPUID_VENDOR_INTEL_EBX      0x756e6547      /* Genu */
#define X86_CPUID_VENDOR_INTEL_ECX      0x6c65746e      /* ntel */
#define X86_CPUID_VENDOR_INTEL_EDX      0x49656e69      /* ineI */

#define X86_CPUID_VENDOR_AMD_EBX        0x68747541      /* Auth */
#define X86_CPUID_VENDOR_AMD_ECX        0x444d4163      /* cAMD */
#define X86_CPUID_VENDOR_AMD_EDX        0x69746e65      /* enti */

#define X86_CPUID_VENDOR_VIA_EBX        0x746e6543      /* Cent */
#define X86_CPUID_VENDOR_VIA_ECX        0x736c7561      /* auls */
#define X86_CPUID_VENDOR_VIA_EDX        0x48727561      /* aurH */

#define X86_CPUID_VENDOR_SHANGHAI_EBX   0x68532020      /*   Sh */
#define X86_CPUID_VENDOR_SHANGHAI_ECX   0x20206961      /* ai   */
#define X86_CPUID_VENDOR_SHANGHAI_EDX   0x68676e61      /* angh */

#define X86_CPUID_VENDOR_HYGON_EBX      0x6f677948      /* Hygo */
#define X86_CPUID_VENDOR_HYGON_ECX      0x656e6975      /* uine */
#define X86_CPUID_VENDOR_HYGON_EDX      0x6e65476e      /* nGen */
/** @} */


/** @name CPUID Feature information.
 * CPUID query with EAX=1.
 * @{
 */
/** ECX Bit 0 - SSE3 - Supports SSE3 or not. */
#define X86_CPUID_FEATURE_ECX_SSE3      RT_BIT_32(0)
/** ECX Bit 1 - PCLMUL - PCLMULQDQ support (for AES-GCM). */
#define X86_CPUID_FEATURE_ECX_PCLMUL    RT_BIT_32(1)
/** ECX Bit 2 - DTES64 - DS Area 64-bit Layout. */
#define X86_CPUID_FEATURE_ECX_DTES64    RT_BIT_32(2)
/** ECX Bit 3 - MONITOR - Supports MONITOR/MWAIT. */
#define X86_CPUID_FEATURE_ECX_MONITOR   RT_BIT_32(3)
/** ECX Bit 4 - CPL-DS - CPL Qualified Debug Store. */
#define X86_CPUID_FEATURE_ECX_CPLDS     RT_BIT_32(4)
/** ECX Bit 5 - VMX - Virtual Machine Technology. */
#define X86_CPUID_FEATURE_ECX_VMX       RT_BIT_32(5)
/** ECX Bit 6 - SMX - Safer Mode Extensions. */
#define X86_CPUID_FEATURE_ECX_SMX       RT_BIT_32(6)
/** ECX Bit 7 - EST - Enh. SpeedStep Tech. */
#define X86_CPUID_FEATURE_ECX_EST       RT_BIT_32(7)
/** ECX Bit 8 - TM2 - Terminal Monitor 2. */
#define X86_CPUID_FEATURE_ECX_TM2       RT_BIT_32(8)
/** ECX Bit 9 - SSSE3 - Supplemental Streaming SIMD Extensions 3. */
#define X86_CPUID_FEATURE_ECX_SSSE3     RT_BIT_32(9)
/** ECX Bit 10 - CNTX-ID - L1 Context ID. */
#define X86_CPUID_FEATURE_ECX_CNTXID    RT_BIT_32(10)
/** ECX Bit 11 - SDBG - Sillicon debug interface (IA32_DEBUG_INTERFACE MSR).
 * See figure 3-6 and table 3-10, in intel Vol. 2A. from 2015-01-01. */
#define X86_CPUID_FEATURE_ECX_SDBG      RT_BIT_32(11)
/** ECX Bit 12 - FMA. */
#define X86_CPUID_FEATURE_ECX_FMA       RT_BIT_32(12)
/** ECX Bit 13 - CX16 - CMPXCHG16B. */
#define X86_CPUID_FEATURE_ECX_CX16      RT_BIT_32(13)
/** ECX Bit 14 - xTPR Update Control. Processor supports changing IA32_MISC_ENABLES[bit 23]. */
#define X86_CPUID_FEATURE_ECX_TPRUPDATE RT_BIT_32(14)
/** ECX Bit 15 - PDCM - Perf/Debug Capability MSR. */
#define X86_CPUID_FEATURE_ECX_PDCM      RT_BIT_32(15)
/** ECX Bit 17 - PCID - Process-context identifiers. */
#define X86_CPUID_FEATURE_ECX_PCID      RT_BIT_32(17)
/** ECX Bit 18 - DCA - Direct Cache Access. */
#define X86_CPUID_FEATURE_ECX_DCA       RT_BIT_32(18)
/** ECX Bit 19 - SSE4_1 - Supports SSE4_1 or not. */
#define X86_CPUID_FEATURE_ECX_SSE4_1    RT_BIT_32(19)
/** ECX Bit 20 - SSE4_2 - Supports SSE4_2 or not. */
#define X86_CPUID_FEATURE_ECX_SSE4_2    RT_BIT_32(20)
/** ECX Bit 21 - x2APIC support. */
#define X86_CPUID_FEATURE_ECX_X2APIC    RT_BIT_32(21)
/** ECX Bit 22 - MOVBE instruction. */
#define X86_CPUID_FEATURE_ECX_MOVBE     RT_BIT_32(22)
/** ECX Bit 23 - POPCNT instruction. */
#define X86_CPUID_FEATURE_ECX_POPCNT    RT_BIT_32(23)
/** ECX Bir 24 - TSC-Deadline. */
#define X86_CPUID_FEATURE_ECX_TSCDEADL  RT_BIT_32(24)
/** ECX Bit 25 - AES instructions. */
#define X86_CPUID_FEATURE_ECX_AES       RT_BIT_32(25)
/** ECX Bit 26 - XSAVE instruction. */
#define X86_CPUID_FEATURE_ECX_XSAVE     RT_BIT_32(26)
/** ECX Bit 27 - Copy of CR4.OSXSAVE. */
#define X86_CPUID_FEATURE_ECX_OSXSAVE   RT_BIT_32(27)
/** ECX Bit 28 - AVX. */
#define X86_CPUID_FEATURE_ECX_AVX       RT_BIT_32(28)
/** ECX Bit 29 - F16C - Half-precision convert instruction support. */
#define X86_CPUID_FEATURE_ECX_F16C      RT_BIT_32(29)
/** ECX Bit 30 - RDRAND instruction. */
#define X86_CPUID_FEATURE_ECX_RDRAND    RT_BIT_32(30)
/** ECX Bit 31 - Hypervisor Present (software only). */
#define X86_CPUID_FEATURE_ECX_HVP       RT_BIT_32(31)


/** Bit 0 - FPU - x87 FPU on Chip. */
#define X86_CPUID_FEATURE_EDX_FPU       RT_BIT_32(0)
/** Bit 1 - VME - Virtual 8086 Mode Enhancements. */
#define X86_CPUID_FEATURE_EDX_VME       RT_BIT_32(1)
/** Bit 2 - DE - Debugging extensions. */
#define X86_CPUID_FEATURE_EDX_DE        RT_BIT_32(2)
/** Bit 3 - PSE - Page Size Extension. */
#define X86_CPUID_FEATURE_EDX_PSE       RT_BIT_32(3)
#define X86_CPUID_FEATURE_EDX_PSE_BIT             3     /**< Bit number for X86_CPUID_FEATURE_EDX_PSE. */
/** Bit 4 - TSC - Time Stamp Counter. */
#define X86_CPUID_FEATURE_EDX_TSC       RT_BIT_32(4)
/** Bit 5 - MSR - Model Specific Registers RDMSR and WRMSR Instructions. */
#define X86_CPUID_FEATURE_EDX_MSR       RT_BIT_32(5)
/** Bit 6 - PAE - Physical Address Extension. */
#define X86_CPUID_FEATURE_EDX_PAE       RT_BIT_32(6)
#define X86_CPUID_FEATURE_EDX_PAE_BIT             6     /**< Bit number for X86_CPUID_FEATURE_EDX_PAE. */
/** Bit 7 - MCE - Machine Check Exception. */
#define X86_CPUID_FEATURE_EDX_MCE       RT_BIT_32(7)
/** Bit 8 - CX8 - CMPXCHG8B instruction. */
#define X86_CPUID_FEATURE_EDX_CX8       RT_BIT_32(8)
/** Bit 9 - APIC - APIC On-Chip. */
#define X86_CPUID_FEATURE_EDX_APIC      RT_BIT_32(9)
/** Bit 11 - SEP - SYSENTER and SYSEXIT Present. */
#define X86_CPUID_FEATURE_EDX_SEP       RT_BIT_32(11)
/** Bit 12 - MTRR - Memory Type Range Registers. */
#define X86_CPUID_FEATURE_EDX_MTRR      RT_BIT_32(12)
/** Bit 13 - PGE - PTE Global Bit. */
#define X86_CPUID_FEATURE_EDX_PGE       RT_BIT_32(13)
/** Bit 14 - MCA - Machine Check Architecture. */
#define X86_CPUID_FEATURE_EDX_MCA       RT_BIT_32(14)
/** Bit 15 - CMOV - Conditional Move Instructions. */
#define X86_CPUID_FEATURE_EDX_CMOV      RT_BIT_32(15)
/** Bit 16 - PAT - Page Attribute Table. */
#define X86_CPUID_FEATURE_EDX_PAT       RT_BIT_32(16)
/** Bit 17 - PSE-36 - 36-bit Page Size Extension. */
#define X86_CPUID_FEATURE_EDX_PSE36     RT_BIT_32(17)
/** Bit 18 - PSN - Processor Serial Number. */
#define X86_CPUID_FEATURE_EDX_PSN       RT_BIT_32(18)
/** Bit 19 - CLFSH - CLFLUSH Instruction. */
#define X86_CPUID_FEATURE_EDX_CLFSH     RT_BIT_32(19)
/** Bit 21 - DS - Debug Store. */
#define X86_CPUID_FEATURE_EDX_DS        RT_BIT_32(21)
/** Bit 22 - ACPI - Thermal Monitor and Software Controlled Clock Facilities. */
#define X86_CPUID_FEATURE_EDX_ACPI      RT_BIT_32(22)
/** Bit 23 - MMX - Intel MMX Technology. */
#define X86_CPUID_FEATURE_EDX_MMX       RT_BIT_32(23)
/** Bit 24 - FXSR - FXSAVE and FXRSTOR Instructions. */
#define X86_CPUID_FEATURE_EDX_FXSR      RT_BIT_32(24)
/** Bit 25 - SSE - SSE Support. */
#define X86_CPUID_FEATURE_EDX_SSE       RT_BIT_32(25)
/** Bit 26 - SSE2 - SSE2 Support. */
#define X86_CPUID_FEATURE_EDX_SSE2      RT_BIT_32(26)
/** Bit 27 - SS - Self Snoop. */
#define X86_CPUID_FEATURE_EDX_SS        RT_BIT_32(27)
/** Bit 28 - HTT - Hyper-Threading Technology. */
#define X86_CPUID_FEATURE_EDX_HTT       RT_BIT_32(28)
/** Bit 29 - TM - Therm. Monitor. */
#define X86_CPUID_FEATURE_EDX_TM        RT_BIT_32(29)
/** Bit 31 - PBE - Pending Break Enabled. */
#define X86_CPUID_FEATURE_EDX_PBE       RT_BIT_32(31)
/** @} */

/** @name CPUID mwait/monitor information.
 * CPUID query with EAX=5.
 * @{
 */
/** ECX Bit 0 - MWAITEXT - Supports mwait/monitor extensions or not. */
#define X86_CPUID_MWAIT_ECX_EXT            RT_BIT_32(0)
/** ECX Bit 1 - MWAITBREAK - Break mwait for external interrupt even if EFLAGS.IF=0. */
#define X86_CPUID_MWAIT_ECX_BREAKIRQIF0    RT_BIT_32(1)
/** @} */


/** @name CPUID Thermal and Power Management information.
 *  Generally Intel only unless noted otherwise.
 * CPUID query with EAX=5. @{
 */
/** EAX Bit 0 - DTS - Supports Digital Temperature Sensor. */
#define X86_CPUID_POWER_EAX_DTS            RT_BIT_32(0)
/** EAX Bit 1 - TURBOBOOST - Intel Turbo Boost available. */
#define X86_CPUID_POWER_EAX_TURBOBOOST     RT_BIT_32(1)
/** EAX Bit 2 - ARAT - Always Running APIC Timer. Intel and AMD. */
#define X86_CPUID_POWER_EAX_ARAT           RT_BIT_32(2)
/** EAX Bit 4 - PLN - Power Limit Notifications supported. */
#define X86_CPUID_POWER_EAX_PLN            RT_BIT_32(4)
/** EAX Bit 5 - ECMD - Clock modulation duty cycle extension supported. */
#define X86_CPUID_POWER_EAX_ECMD           RT_BIT_32(5)
/** EAX Bit 6 - PTM - Package Thermal Management supported. */
#define X86_CPUID_POWER_EAX_PTM            RT_BIT_32(6)
/** EAX Bit 7 - HWP - HWP base MSRs supported. */
#define X86_CPUID_POWER_EAX_HWP            RT_BIT_32(7)
/** EAX Bit 8 - HWP_NOTIFY - HWP notification MSR supported. */
#define X86_CPUID_POWER_EAX_HWP_NOTIFY     RT_BIT_32(8)
/** EAX Bit 9 - HWP_ACT_WIN - HWP activity window MSR bits supported. */
#define X86_CPUID_POWER_EAX_HWP_ACT_WIN    RT_BIT_32(9)
/** EAX Bit 10 - HWP_NRG_PP - HWP energy performae preference MSR bits supported. */
#define X86_CPUID_POWER_EAX_HWP_NRG_PP     RT_BIT_32(10)
/** EAX Bit 11 - HWP_PLR - HWP package level request MSR supported. */
#define X86_CPUID_POWER_EAX_HWP_PLR        RT_BIT_32(11)
/** EAX Bit 13 - HDC - HDC base MSRs supported. */
#define X86_CPUID_POWER_EAX_HDC            RT_BIT_32(13)
/** EAX Bit 14 - TBM30 - Turbo Boost Max Technology 3.0 supported. */
#define X86_CPUID_POWER_EAX_TBM30          RT_BIT_32(14)
/** EAX Bit 15 - HWP_HPC - HWP Highest Performance change supported. */
#define X86_CPUID_POWER_EAX_HWP_HPC        RT_BIT_32(15)
/** EAX Bit 16 - HWP_PECI - HWP PECI override supported. */
#define X86_CPUID_POWER_EAX_HWP_PECI       RT_BIT_32(16)
/** EAX Bit 17 - HWP_FLEX - Flexible HWP supported. */
#define X86_CPUID_POWER_EAX_HWP_FLEX       RT_BIT_32(17)

/** ECX Bit 1 - HCFC - Hardware Coordintion Feedback Capability supported. Intel and AMD. */
#define X86_CPUID_POWER_ECX_HCFC           RT_BIT_32(0)
/** @} */


/** @name CPUID Structured Extended Feature information.
 * CPUID query with EAX=7.
 * @{
 */
/** EBX Bit 0 - FSGSBASE - Supports RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE. */
#define X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE          RT_BIT_32(0)
/** EBX Bit 1 - TSCADJUST - Supports MSR_IA32_TSC_ADJUST. */
#define X86_CPUID_STEXT_FEATURE_EBX_TSC_ADJUST        RT_BIT_32(1)
/** EBX Bit 2 - SGX - Supports Software Guard Extensions . */
#define X86_CPUID_STEXT_FEATURE_EBX_SGX               RT_BIT_32(2)
/** EBX Bit 3 - BMI1 - Advanced Bit Manipulation extension 1. */
#define X86_CPUID_STEXT_FEATURE_EBX_BMI1              RT_BIT_32(3)
/** EBX Bit 4 - HLE - Hardware Lock Elision. */
#define X86_CPUID_STEXT_FEATURE_EBX_HLE               RT_BIT_32(4)
/** EBX Bit 5 - AVX2 - Advanced Vector Extensions 2. */
#define X86_CPUID_STEXT_FEATURE_EBX_AVX2              RT_BIT_32(5)
/** EBX Bit 6 - FDP_EXCPTN_ONLY - FPU data pointer only updated on exceptions if set. */
#define X86_CPUID_STEXT_FEATURE_EBX_FDP_EXCPTN_ONLY   RT_BIT_32(6)
/** EBX Bit 7 - SMEP - Supervisor Mode Execution Prevention. */
#define X86_CPUID_STEXT_FEATURE_EBX_SMEP              RT_BIT_32(7)
/** EBX Bit 8 - BMI2 - Advanced Bit Manipulation extension 2. */
#define X86_CPUID_STEXT_FEATURE_EBX_BMI2              RT_BIT_32(8)
/** EBX Bit 9 - ERMS - Supports Enhanced REP MOVSB/STOSB. */
#define X86_CPUID_STEXT_FEATURE_EBX_ERMS              RT_BIT_32(9)
/** EBX Bit 10 - INVPCID - Supports INVPCID. */
#define X86_CPUID_STEXT_FEATURE_EBX_INVPCID           RT_BIT_32(10)
/** EBX Bit 11 - RTM - Supports Restricted Transactional Memory. */
#define X86_CPUID_STEXT_FEATURE_EBX_RTM               RT_BIT_32(11)
/** EBX Bit 12 - PQM - Supports Platform Quality of Service Monitoring. */
#define X86_CPUID_STEXT_FEATURE_EBX_PQM               RT_BIT_32(12)
/** EBX Bit 13 - DEPFPU_CS_DS - Deprecates FPU CS, FPU DS values if set. */
#define X86_CPUID_STEXT_FEATURE_EBX_DEPR_FPU_CS_DS    RT_BIT_32(13)
/** EBX Bit 14 - MPE - Supports Intel Memory Protection Extensions. */
#define X86_CPUID_STEXT_FEATURE_EBX_MPE               RT_BIT_32(14)
/** EBX Bit 15 - PQE - Supports Platform Quality of Service Enforcement. */
#define X86_CPUID_STEXT_FEATURE_EBX_PQE               RT_BIT_32(15)
/** EBX Bit 16 - AVX512F - Supports AVX512F. */
#define X86_CPUID_STEXT_FEATURE_EBX_AVX512F           RT_BIT_32(16)
/** EBX Bit 18 - RDSEED - Supports RDSEED. */
#define X86_CPUID_STEXT_FEATURE_EBX_RDSEED            RT_BIT_32(18)
/** EBX Bit 19 - ADX - Supports ADCX/ADOX. */
#define X86_CPUID_STEXT_FEATURE_EBX_ADX               RT_BIT_32(19)
/** EBX Bit 20 - SMAP - Supports Supervisor Mode Access Prevention. */
#define X86_CPUID_STEXT_FEATURE_EBX_SMAP              RT_BIT_32(20)
/** EBX Bit 23 - CLFLUSHOPT - Supports CLFLUSHOPT (Cache Line Flush). */
#define X86_CPUID_STEXT_FEATURE_EBX_CLFLUSHOPT        RT_BIT_32(23)
/** EBX Bit 25 - INTEL_PT - Supports Intel Processor Trace. */
#define X86_CPUID_STEXT_FEATURE_EBX_INTEL_PT          RT_BIT_32(25)
/** EBX Bit 26 - AVX512PF - Supports AVX512PF. */
#define X86_CPUID_STEXT_FEATURE_EBX_AVX512PF          RT_BIT_32(26)
/** EBX Bit 27 - AVX512ER - Supports AVX512ER. */
#define X86_CPUID_STEXT_FEATURE_EBX_AVX512ER          RT_BIT_32(27)
/** EBX Bit 28 - AVX512CD - Supports AVX512CD. */
#define X86_CPUID_STEXT_FEATURE_EBX_AVX512CD          RT_BIT_32(28)
/** EBX Bit 29 - SHA - Supports Secure Hash Algorithm extensions. */
#define X86_CPUID_STEXT_FEATURE_EBX_SHA               RT_BIT_32(29)

/** ECX Bit 0 - PREFETCHWT1 - Supports the PREFETCHWT1 instruction. */
#define X86_CPUID_STEXT_FEATURE_ECX_PREFETCHWT1       RT_BIT_32(0)
/** ECX Bit 2 - UIMP - Supports user mode instruction prevention. */
#define X86_CPUID_STEXT_FEATURE_ECX_UMIP              RT_BIT_32(2)
/** ECX Bit 3 - PKU - Supports protection keys for user-mode pages. */
#define X86_CPUID_STEXT_FEATURE_ECX_PKU               RT_BIT_32(3)
/** ECX Bit 4 - OSPKE - Protection keys for user mode pages enabled. */
#define X86_CPUID_STEXT_FEATURE_ECX_OSPKE             RT_BIT_32(4)
/** ECX Bit 7 - CET_SS - Supports CET shadow stack features. */
#define X86_CPUID_STEXT_FEATURE_ECX_CET_SS            RT_BIT_32(7)
/** ECX Bits 17-21 - MAWAU - Value used by BNDLDX and BNDSTX. */
#define X86_CPUID_STEXT_FEATURE_ECX_MAWAU             UINT32_C(0x003e0000)
/** ECX Bit 22 - RDPID - Support pread process ID. */
#define X86_CPUID_STEXT_FEATURE_ECX_RDPID             RT_BIT_32(2)
/** ECX Bit 30 - SGX_LC - Supports SGX launch configuration. */
#define X86_CPUID_STEXT_FEATURE_ECX_SGX_LC            RT_BIT_32(30)

/** EDX bit 9 - SRBDS_CTRL - (Special Register Buffer Data Sample Control)
 *  Supports IA32_MCU_OPT_CTRL and IA32_MCU_OPT_CTRL.RNGDS_MITG_DIS. */
#define X86_CPUID_STEXT_FEATURE_EDX_SRBDS_CTRL        RT_BIT_32(9)
/** EDX Bit 10 - MD_CLEAR - Supports flushing MDS related buffers. */
#define X86_CPUID_STEXT_FEATURE_EDX_MD_CLEAR          RT_BIT_32(10)
/** EDX Bit 20 - CET_IBT - Supports CET indirect branch tracking features. */
#define X86_CPUID_STEXT_FEATURE_EDX_CET_IBT           RT_BIT_32(20)
/** EDX Bit 26 - IBRS & IBPB - Supports the IBRS flag in IA32_SPEC_CTRL and
 *  IBPB command in IA32_PRED_CMD. */
#define X86_CPUID_STEXT_FEATURE_EDX_IBRS_IBPB         RT_BIT_32(26)
/** EDX Bit 27 - IBRS & IBPB - Supports the STIBP flag in IA32_SPEC_CTRL. */
#define X86_CPUID_STEXT_FEATURE_EDX_STIBP             RT_BIT_32(27)
/** EDX Bit 28 - FLUSH_CMD - Supports IA32_FLUSH_CMD MSR. */
#define X86_CPUID_STEXT_FEATURE_EDX_FLUSH_CMD         RT_BIT_32(28)
/** EDX Bit 29 - ARCHCAP - Supports the IA32_ARCH_CAPABILITIES MSR. */
#define X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP           RT_BIT_32(29)
/** EDX Bit 30 - CORECAP - Supports the IA32_CORE_CAPABILITIES MSR. */
#define X86_CPUID_STEXT_FEATURE_EDX_CORECAP           RT_BIT_32(30)
/** EDX Bit 31 - SSBD - Supports the SSBD flag in IA32_SPEC_CTRL. */
#define X86_CPUID_STEXT_FEATURE_EDX_SSBD              RT_BIT_32(31)

/** @} */


/** @name CPUID Extended Feature information.
 *  CPUID query with EAX=0x80000001.
 *  @{
 */
/** ECX Bit 0 - LAHF/SAHF support in 64-bit mode. */
#define X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF     RT_BIT_32(0)

/** EDX Bit 11 - SYSCALL/SYSRET. */
#define X86_CPUID_EXT_FEATURE_EDX_SYSCALL       RT_BIT_32(11)
/** EDX Bit 20 - No-Execute/Execute-Disable. */
#define X86_CPUID_EXT_FEATURE_EDX_NX            RT_BIT_32(20)
/** EDX Bit 26 - 1 GB large page. */
#define X86_CPUID_EXT_FEATURE_EDX_PAGE1GB       RT_BIT_32(26)
/** EDX Bit 27 - RDTSCP. */
#define X86_CPUID_EXT_FEATURE_EDX_RDTSCP        RT_BIT_32(27)
/** EDX Bit 29 - AMD Long Mode/Intel-64 Instructions. */
#define X86_CPUID_EXT_FEATURE_EDX_LONG_MODE     RT_BIT_32(29)
/** @}*/

/** @name CPUID AMD Feature information.
 * CPUID query with EAX=0x80000001.
 * @{
 */
/** Bit 0 - FPU - x87 FPU on Chip. */
#define X86_CPUID_AMD_FEATURE_EDX_FPU       RT_BIT_32(0)
/** Bit 1 - VME - Virtual 8086 Mode Enhancements. */
#define X86_CPUID_AMD_FEATURE_EDX_VME       RT_BIT_32(1)
/** Bit 2 - DE - Debugging extensions. */
#define X86_CPUID_AMD_FEATURE_EDX_DE        RT_BIT_32(2)
/** Bit 3 - PSE - Page Size Extension. */
#define X86_CPUID_AMD_FEATURE_EDX_PSE       RT_BIT_32(3)
/** Bit 4 - TSC - Time Stamp Counter. */
#define X86_CPUID_AMD_FEATURE_EDX_TSC       RT_BIT_32(4)
/** Bit 5 - MSR - K86 Model Specific Registers RDMSR and WRMSR Instructions. */
#define X86_CPUID_AMD_FEATURE_EDX_MSR       RT_BIT_32(5)
/** Bit 6 - PAE - Physical Address Extension. */
#define X86_CPUID_AMD_FEATURE_EDX_PAE       RT_BIT_32(6)
/** Bit 7 - MCE - Machine Check Exception. */
#define X86_CPUID_AMD_FEATURE_EDX_MCE       RT_BIT_32(7)
/** Bit 8 - CX8 - CMPXCHG8B instruction. */
#define X86_CPUID_AMD_FEATURE_EDX_CX8       RT_BIT_32(8)
/** Bit 9 - APIC - APIC On-Chip. */
#define X86_CPUID_AMD_FEATURE_EDX_APIC      RT_BIT_32(9)
/** Bit 12 - MTRR - Memory Type Range Registers. */
#define X86_CPUID_AMD_FEATURE_EDX_MTRR      RT_BIT_32(12)
/** Bit 13 - PGE - PTE Global Bit. */
#define X86_CPUID_AMD_FEATURE_EDX_PGE       RT_BIT_32(13)
/** Bit 14 - MCA - Machine Check Architecture. */
#define X86_CPUID_AMD_FEATURE_EDX_MCA       RT_BIT_32(14)
/** Bit 15 - CMOV - Conditional Move Instructions. */
#define X86_CPUID_AMD_FEATURE_EDX_CMOV      RT_BIT_32(15)
/** Bit 16 - PAT - Page Attribute Table. */
#define X86_CPUID_AMD_FEATURE_EDX_PAT       RT_BIT_32(16)
/** Bit 17 - PSE-36 - 36-bit Page Size Extension. */
#define X86_CPUID_AMD_FEATURE_EDX_PSE36     RT_BIT_32(17)
/** Bit 22 - AXMMX - AMD Extensions to MMX Instructions. */
#define X86_CPUID_AMD_FEATURE_EDX_AXMMX     RT_BIT_32(22)
/** Bit 23 - MMX - Intel MMX Technology. */
#define X86_CPUID_AMD_FEATURE_EDX_MMX       RT_BIT_32(23)
/** Bit 24 - FXSR - FXSAVE and FXRSTOR Instructions. */
#define X86_CPUID_AMD_FEATURE_EDX_FXSR      RT_BIT_32(24)
/** Bit 25 - FFXSR - AMD fast FXSAVE and FXRSTOR Instructions. */
#define X86_CPUID_AMD_FEATURE_EDX_FFXSR     RT_BIT_32(25)
/** Bit 30 - 3DNOWEXT - AMD Extensions to 3DNow. */
#define X86_CPUID_AMD_FEATURE_EDX_3DNOW_EX  RT_BIT_32(30)
/** Bit 31 - 3DNOW - AMD 3DNow. */
#define X86_CPUID_AMD_FEATURE_EDX_3DNOW     RT_BIT_32(31)

/** Bit 1 - CmpLegacy - Core multi-processing legacy mode. */
#define X86_CPUID_AMD_FEATURE_ECX_CMPL      RT_BIT_32(1)
/** Bit 2 - SVM - AMD VM extensions. */
#define X86_CPUID_AMD_FEATURE_ECX_SVM       RT_BIT_32(2)
/** Bit 3 - EXTAPIC - AMD extended APIC registers starting at 0x400. */
#define X86_CPUID_AMD_FEATURE_ECX_EXT_APIC  RT_BIT_32(3)
/** Bit 4 - CR8L - AMD LOCK MOV CR0 means MOV CR8. */
#define X86_CPUID_AMD_FEATURE_ECX_CR8L      RT_BIT_32(4)
/** Bit 5 - ABM - AMD Advanced bit manipulation. LZCNT instruction support. */
#define X86_CPUID_AMD_FEATURE_ECX_ABM       RT_BIT_32(5)
/** Bit 6 - SSE4A - AMD EXTRQ, INSERTQ, MOVNTSS, and MOVNTSD instruction support. */
#define X86_CPUID_AMD_FEATURE_ECX_SSE4A     RT_BIT_32(6)
/** Bit 7 - MISALIGNSSE - AMD Misaligned SSE mode. */
#define X86_CPUID_AMD_FEATURE_ECX_MISALNSSE RT_BIT_32(7)
/** Bit 8 - 3DNOWPRF - AMD PREFETCH and PREFETCHW instruction support. */
#define X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF  RT_BIT_32(8)
/** Bit 9 - OSVW - AMD OS visible workaround. */
#define X86_CPUID_AMD_FEATURE_ECX_OSVW      RT_BIT_32(9)
/** Bit 10 - IBS - Instruct based sampling. */
#define X86_CPUID_AMD_FEATURE_ECX_IBS       RT_BIT_32(10)
/** Bit 11 - XOP - Extended operation support (see APM6). */
#define X86_CPUID_AMD_FEATURE_ECX_XOP       RT_BIT_32(11)
/** Bit 12 - SKINIT - AMD SKINIT: SKINIT, STGI, and DEV support. */
#define X86_CPUID_AMD_FEATURE_ECX_SKINIT    RT_BIT_32(12)
/** Bit 13 - WDT - AMD Watchdog timer support. */
#define X86_CPUID_AMD_FEATURE_ECX_WDT       RT_BIT_32(13)
/** Bit 15 - LWP - Lightweight profiling support. */
#define X86_CPUID_AMD_FEATURE_ECX_LWP       RT_BIT_32(15)
/** Bit 16 - FMA4 - Four operand FMA instruction support. */
#define X86_CPUID_AMD_FEATURE_ECX_FMA4      RT_BIT_32(16)
/** Bit 19 - NodeId - Indicates support for
 * MSR_C001_100C[NodeId,NodesPerProcessr]. */
#define X86_CPUID_AMD_FEATURE_ECX_NODEID    RT_BIT_32(19)
/** Bit 21 - TBM - Trailing bit manipulation instruction support. */
#define X86_CPUID_AMD_FEATURE_ECX_TBM       RT_BIT_32(21)
/** Bit 22 - TopologyExtensions - . */
#define X86_CPUID_AMD_FEATURE_ECX_TOPOEXT   RT_BIT_32(22)
/** @} */


/** @name CPUID AMD Feature information.
 * CPUID query with EAX=0x80000007.
 * @{
 */
/** Bit 0 - TS - Temperature Sensor. */
#define X86_CPUID_AMD_ADVPOWER_EDX_TS        RT_BIT_32(0)
/** Bit 1 - FID - Frequency ID Control. */
#define X86_CPUID_AMD_ADVPOWER_EDX_FID       RT_BIT_32(1)
/** Bit 2 - VID - Voltage ID Control. */
#define X86_CPUID_AMD_ADVPOWER_EDX_VID       RT_BIT_32(2)
/** Bit 3 - TTP - THERMTRIP. */
#define X86_CPUID_AMD_ADVPOWER_EDX_TTP       RT_BIT_32(3)
/** Bit 4 - TM - Hardware Thermal Control. */
#define X86_CPUID_AMD_ADVPOWER_EDX_TM        RT_BIT_32(4)
/** Bit 5 - STC - Software Thermal Control. */
#define X86_CPUID_AMD_ADVPOWER_EDX_STC       RT_BIT_32(5)
/** Bit 6 - MC - 100 Mhz Multiplier Control. */
#define X86_CPUID_AMD_ADVPOWER_EDX_MC        RT_BIT_32(6)
/** Bit 7 - HWPSTATE - Hardware P-State Control. */
#define X86_CPUID_AMD_ADVPOWER_EDX_HWPSTATE  RT_BIT_32(7)
/** Bit 8 - TSCINVAR - TSC Invariant. */
#define X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR  RT_BIT_32(8)
/** Bit 9 - CPB - TSC Invariant. */
#define X86_CPUID_AMD_ADVPOWER_EDX_CPB       RT_BIT_32(9)
/** Bit 10 - EffFreqRO - MPERF/APERF. */
#define X86_CPUID_AMD_ADVPOWER_EDX_EFRO      RT_BIT_32(10)
/** Bit 11 - PFI - Processor feedback interface (see EAX). */
#define X86_CPUID_AMD_ADVPOWER_EDX_PFI       RT_BIT_32(11)
/** Bit 12 - PA - Processor accumulator (MSR c001_007a). */
#define X86_CPUID_AMD_ADVPOWER_EDX_PA        RT_BIT_32(12)
/** @} */


/** @name CPUID AMD extended feature extensions ID (EBX).
 * CPUID query with EAX=0x80000008.
 * @{
 */
/** Bit 0 - CLZERO - Clear zero instruction. */
#define X86_CPUID_AMD_EFEID_EBX_CLZERO                      RT_BIT_32(0)
/** Bit 1 - IRPerf - Instructions retired count support. */
#define X86_CPUID_AMD_EFEID_EBX_IRPERF                      RT_BIT_32(1)
/** Bit 2 - XSaveErPtr - Always XSAVE* and XRSTR* error pointers. */
#define X86_CPUID_AMD_EFEID_EBX_XSAVE_ER_PTR                RT_BIT_32(2)
/** Bit 4 - RDPRU - Supports the RDPRU instruction. */
#define X86_CPUID_AMD_EFEID_EBX_RDPRU                       RT_BIT_32(4)
/** Bit 8 - MCOMMIT - Supports the MCOMMIT instruction. */
#define X86_CPUID_AMD_EFEID_EBX_MCOMMIT                     RT_BIT_32(8)
/* AMD pipeline length: 9 feature bits ;-) */
/** Bit 12 - IBPB - Supports the IBPB command in IA32_PRED_CMD. */
#define X86_CPUID_AMD_EFEID_EBX_IBPB                        RT_BIT_32(12)
/** Bit 14 - IBRS - Supports the IBRS bit in IA32_SPEC_CTRL. */
#define X86_CPUID_AMD_EFEID_EBX_IBRS                        RT_BIT_32(14)
/** Bit 15 - STIBP - Supports the STIBP bit in IA32_SPEC_CTRL. */
#define X86_CPUID_AMD_EFEID_EBX_STIBP                       RT_BIT_32(15)
/** Bit 16 - IBRS always on mode - IBRS should be set once during boot only. */
#define X86_CPUID_AMD_EFEID_EBX_IBRS_ALWAYS_ON              RT_BIT_32(16)
/** Bit 17 - STIBP always on mode - STIBP should be set once during boot only. */
#define X86_CPUID_AMD_EFEID_EBX_STIBP_ALWAYS_ON             RT_BIT_32(17)
/** Bit 18 - IBRS preferred - IBRS is preferred over software mitigations. */
#define X86_CPUID_AMD_EFEID_EBX_IBRS_PREFERRED              RT_BIT_32(18)
/** Bit 24 - Speculative Store Bypass Disable supported in SPEC_CTL. */
#define X86_CPUID_AMD_EFEID_EBX_SPEC_CTRL_SSBD              RT_BIT_32(24)
/** Bit 25 - Speculative Store Bypass Disable supported in VIRT_SPEC_CTL. */
#define X86_CPUID_AMD_EFEID_EBX_VIRT_SPEC_CTRL_SSBD         RT_BIT_32(25)
/** Bit 26 - Speculative Store Bypass Disable not required. */
#define X86_CPUID_AMD_EFEID_EBX_NO_SSBD_REQUIRED            RT_BIT_32(26)
/** @} */


/** @name CPUID AMD SVM Feature information.
 * CPUID query with EAX=0x8000000a.
 * @{
 */
/** Bit 0 - NP - Nested Paging supported. */
#define X86_CPUID_SVM_FEATURE_EDX_NESTED_PAGING             RT_BIT(0)
/** Bit 1 - LbrVirt - Support for saving five debug MSRs. */
#define X86_CPUID_SVM_FEATURE_EDX_LBR_VIRT                  RT_BIT(1)
/** Bit 2 - SVML - SVM locking bit supported. */
#define X86_CPUID_SVM_FEATURE_EDX_SVM_LOCK                  RT_BIT(2)
/** Bit 3 - NRIPS - Saving the next instruction pointer is supported. */
#define X86_CPUID_SVM_FEATURE_EDX_NRIP_SAVE                 RT_BIT(3)
/** Bit 4 - TscRateMsr - Support for MSR TSC ratio. */
#define X86_CPUID_SVM_FEATURE_EDX_TSC_RATE_MSR              RT_BIT(4)
/** Bit 5 - VmcbClean - Support VMCB clean bits. */
#define X86_CPUID_SVM_FEATURE_EDX_VMCB_CLEAN                RT_BIT(5)
/** Bit 6 - FlushByAsid - Indicate TLB flushing for current ASID only, and that
 *  VMCB.TLB_Control is supported. */
#define X86_CPUID_SVM_FEATURE_EDX_FLUSH_BY_ASID             RT_BIT(6)
/** Bit 7 - DecodeAssists - Indicate decode assists is supported. */
#define X86_CPUID_SVM_FEATURE_EDX_DECODE_ASSISTS            RT_BIT(7)
/** Bit 10 - PauseFilter - Indicates support for the PAUSE intercept filter. */
#define X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER              RT_BIT(10)
/** Bit 12 - PauseFilterThreshold - Indicates support for the PAUSE
 *  intercept filter cycle count threshold. */
#define X86_CPUID_SVM_FEATURE_EDX_PAUSE_FILTER_THRESHOLD    RT_BIT(12)
/** Bit 13 - AVIC - Advanced Virtual Interrupt Controller. */
#define X86_CPUID_SVM_FEATURE_EDX_AVIC                      RT_BIT(13)
/** Bit 15 - VMSAVEvirt - Supports virtualized VMSAVE/VMLOAD. */
#define X86_CPUID_SVM_FEATURE_EDX_VIRT_VMSAVE_VMLOAD        RT_BIT(15)
/** Bit 16 - VGIF - Supports virtualized GIF. */
#define X86_CPUID_SVM_FEATURE_EDX_VGIF                      RT_BIT(16)
/** Bit 17 - GMET - Supports Guest Mode Execute Trap Extensions. */
#define X86_CPUID_SVM_FEATURE_EDX_GMET                      RT_BIT(17)
/** Bit 18 - X2AVIC - Supports Advanced Virtual Interrupt Controller in x2APIC
 *  mode. */
#define X86_CPUID_SVM_FEATURE_EDX_X2AVIC                    RT_BIT(18)
/** Bit 19 - SSSCheck - SVM supervisor shadow stack restrictions. */
#define X86_CPUID_SVM_FEATURE_EDX_SSSCHECK                  RT_BIT(19)
/** Bit 20 - SpecCtrl - Supports SPEC_CTRL Virtualization. */
#define X86_CPUID_SVM_FEATURE_EDX_SPEC_CTRL                 RT_BIT(20)
/** Bit 21 - ROGPT - Read-Only Guest Page Table. */
#define X86_CPUID_SVM_FEATURE_EDX_ROGPT                     RT_BIT(21)
/** Bit 23 - HOST_MCE_OVERRIDE - Supports host \#MC exception override. */
#define X86_CPUID_SVM_FEATURE_EDX_HOST_MCE_OVERRIDE         RT_BIT(23)
/** Bit 24 - TlbiCtl - Supports INVLPGB/TLBSYNC in VMCB and TLBSYNC intercept. */
#define X86_CPUID_SVM_FEATURE_EDX_TLBICTL                   RT_BIT(24)
/** Bit 25 - TlbiCtl - Supports virtual NMIs. */
#define X86_CPUID_SVM_FEATURE_EDX_VNMI                      RT_BIT(25)
/** Bit 26 - TlbiCtl - Supports IBS virtualization. */
#define X86_CPUID_SVM_FEATURE_EDX_IBS_VIRT                  RT_BIT(26)
/** Bit 27 - TlbiCtl - Supports extended LVT AVIC access changes. */
#define X86_CPUID_SVM_FEATURE_EDX_EXT_LVT_AVIC_ACCESS_CHG   RT_BIT(27)
/** Bit 28 - TlbiCtl - Supports guest VMCB address check. */
#define X86_CPUID_SVM_FEATURE_EDX_NST_VIRT_VMCB_ADDR_CHK    RT_BIT(28)
/** Bit 29 - TlbiCtl - Supports INVLPGB/TLBSYNC in VMCB and TLBSYNC intercept. */
#define X86_CPUID_SVM_FEATURE_EDX_BUS_LOCK_THRESHOLD        RT_BIT(29)

/** @} */


/** @name CR0
 * @remarks The 286 (MSW), 386 and 486 ignores attempts at setting
 *          reserved flags.
 * @{ */
/** Bit 0 - PE - Protection Enabled */
#define X86_CR0_PE                          RT_BIT_32(0)
#define X86_CR0_PROTECTION_ENABLE           RT_BIT_32(0)
#define X86_CR0_PE_BIT                      0
/** Bit 1 - MP - Monitor Coprocessor */
#define X86_CR0_MP                          RT_BIT_32(1)
#define X86_CR0_MONITOR_COPROCESSOR         RT_BIT_32(1)
#define X86_CR0_MP_BIT                      1
/** Bit 2 - EM - Emulation. */
#define X86_CR0_EM                          RT_BIT_32(2)
#define X86_CR0_EMULATE_FPU                 RT_BIT_32(2)
#define X86_CR0_EM_BIT                      2
/** Bit 3 - TS - Task Switch. */
#define X86_CR0_TS                          RT_BIT_32(3)
#define X86_CR0_TASK_SWITCH                 RT_BIT_32(3)
#define X86_CR0_TS_BIT                      3
/** Bit 4 - ET - Extension flag. (386, 'hardcoded' to 1 on 486+) */
#define X86_CR0_ET                          RT_BIT_32(4)
#define X86_CR0_EXTENSION_TYPE              RT_BIT_32(4)
#define X86_CR0_ET_BIT                      4
/** Bit 5 - NE - Numeric error (486+). */
#define X86_CR0_NE                          RT_BIT_32(5)
#define X86_CR0_NUMERIC_ERROR               RT_BIT_32(5)
#define X86_CR0_NE_BIT                      5
/** Bit 16 - WP - Write Protect (486+). */
#define X86_CR0_WP                          RT_BIT_32(16)
#define X86_CR0_WRITE_PROTECT               RT_BIT_32(16)
#define X86_CR0_WP_BIT                      16
/** Bit 18 - AM - Alignment Mask (486+). */
#define X86_CR0_AM                          RT_BIT_32(18)
#define X86_CR0_ALIGMENT_MASK               RT_BIT_32(18)
#define X86_CR0_AM_BIT                      18
/** Bit 29 - NW - Not Write-though (486+). */
#define X86_CR0_NW                          RT_BIT_32(29)
#define X86_CR0_NOT_WRITE_THROUGH           RT_BIT_32(29)
#define X86_CR0_NW_BIT                      29
/** Bit 30 - WP - Cache Disable (486+). */
#define X86_CR0_CD                          RT_BIT_32(30)
#define X86_CR0_CACHE_DISABLE               RT_BIT_32(30)
#define X86_CR0_CD_BIT                      30
/** Bit 31 - PG - Paging. */
#define X86_CR0_PG                          RT_BIT_32(31)
#define X86_CR0_PAGING                      RT_BIT_32(31)
#define X86_CR0_BIT_PG                      31 /**< Bit number of X86_CR0_PG */
/** @} */


/** @name CR3
 * @{ */
/** Bit 3 - PWT - Page-level Writes Transparent. */
#define X86_CR3_PWT                         RT_BIT_32(3)
#define X86_CR3_PWT_BIT                     3
/** Bit 4 - PCD - Page-level Cache Disable. */
#define X86_CR3_PCD                         RT_BIT_32(4)
#define X86_CR3_PCD_BIT                     4
/** Bits 12-31 - - Page directory page number. */
#define X86_CR3_PAGE_MASK                   (0xfffff000)
/** Bits  5-31 - - PAE Page directory page number. */
#define X86_CR3_PAE_PAGE_MASK               (0xffffffe0)
/** Bits 12-51 - - AMD64 PML4 page number.
 * @note This is a maxed out mask, the actual acceptable CR3 value can
 *       be lower depending on the PhysAddrSize from CPUID Fn8000_0008. */
#define X86_CR3_AMD64_PAGE_MASK             UINT64_C(0x000ffffffffff000)
/** Bits 12-51 - - Intel EPT PML4 page number (EPTP).
 * @note This is a maxed out mask, the actual acceptable CR3/EPTP value can
 *       be lower depending on the PhysAddrSize from CPUID Fn8000_0008. */
#define X86_CR3_EPT_PAGE_MASK               UINT64_C(0x000ffffffffff000)
/** @} */


/** @name CR4
 * @{ */
/** Bit 0 - VME - Virtual-8086 Mode Extensions. */
#define X86_CR4_VME                         RT_BIT_32(0)
#define X86_CR4_VME_BIT                     0
/** Bit 1 - PVI - Protected-Mode Virtual Interrupts. */
#define X86_CR4_PVI                         RT_BIT_32(1)
#define X86_CR4_PVI_BIT                     1
/** Bit 2 - TSD - Time Stamp Disable. */
#define X86_CR4_TSD                         RT_BIT_32(2)
#define X86_CR4_TSD_BIT                     2
/** Bit 3 - DE - Debugging Extensions. */
#define X86_CR4_DE                          RT_BIT_32(3)
#define X86_CR4_DE_BIT                      3
/** Bit 4 - PSE - Page Size Extension. */
#define X86_CR4_PSE                         RT_BIT_32(4)
#define X86_CR4_PSE_BIT                     4
/** Bit 5 - PAE - Physical Address Extension. */
#define X86_CR4_PAE                         RT_BIT_32(5)
#define X86_CR4_PAE_BIT                     5
/** Bit 6 - MCE - Machine-Check Enable. */
#define X86_CR4_MCE                         RT_BIT_32(6)
#define X86_CR4_MCE_BIT                     6
/** Bit 7 - PGE - Page Global Enable. */
#define X86_CR4_PGE                         RT_BIT_32(7)
#define X86_CR4_PGE_BIT                     7
/** Bit 8 - PCE - Performance-Monitoring Counter Enable. */
#define X86_CR4_PCE                         RT_BIT_32(8)
#define X86_CR4_PCE_BIT                     8
/** Bit 9 - OSFXSR - Operating System Support for FXSAVE and FXRSTORE instructions. */
#define X86_CR4_OSFXSR                      RT_BIT_32(9)
#define X86_CR4_OSFXSR_BIT                  9
/** Bit 10 - OSXMMEEXCPT - Operating System Support for Unmasked SIMD Floating-Point Exceptions. */
#define X86_CR4_OSXMMEEXCPT                 RT_BIT_32(10)
#define X86_CR4_OSXMMEEXCPT_BIT             10
/** Bit 11 - UMIP - User-Mode Instruction Prevention. */
#define X86_CR4_UMIP                        RT_BIT_32(11)
#define X86_CR4_UMIP_BIT                    11
/** Bit 13 - VMXE - VMX mode is enabled. */
#define X86_CR4_VMXE                        RT_BIT_32(13)
#define X86_CR4_VMXE_BIT                    13
/** Bit 14 - SMXE - Safer Mode Extensions Enabled. */
#define X86_CR4_SMXE                        RT_BIT_32(14)
#define X86_CR4_SMXE_BIT                    14
/** Bit 16 - FSGSBASE - Read/write FSGSBASE instructions Enable. */
#define X86_CR4_FSGSBASE                    RT_BIT_32(16)
#define X86_CR4_FSGSBASE_BIT                16
/** Bit 17 - PCIDE - Process-Context Identifiers Enabled. */
#define X86_CR4_PCIDE                       RT_BIT_32(17)
#define X86_CR4_PCIDE_BIT                   17
/** Bit 18 - OSXSAVE - Operating System Support for XSAVE and processor
 * extended states. */
#define X86_CR4_OSXSAVE                     RT_BIT_32(18)
#define X86_CR4_OSXSAVE_BIT                 18
/** Bit 20 - SMEP - Supervisor-mode Execution Prevention enabled. */
#define X86_CR4_SMEP                        RT_BIT_32(20)
#define X86_CR4_SMEP_BIt                    20
/** Bit 21 - SMAP - Supervisor-mode Access Prevention enabled. */
#define X86_CR4_SMAP                        RT_BIT_32(21)
#define X86_CR4_SMAP_BIT                    21
/** Bit 22 - PKE - Protection Key Enable. */
#define X86_CR4_PKE                         RT_BIT_32(22)
#define X86_CR4_PKE_BIT                     22
/** Bit 23 - CET - Control-flow Enhancement Technology enabled. */
#define X86_CR4_CET                         RT_BIT_32(23)
#define X86_CR4_CET_BIT                     23
/** @} */


/** @name DR6
 * @{ */
/** Bit 0 - B0 - Breakpoint 0 condition detected. */
#define X86_DR6_B0                          RT_BIT_32(0)
/** Bit 1 - B1 - Breakpoint 1 condition detected. */
#define X86_DR6_B1                          RT_BIT_32(1)
/** Bit 2 - B2 - Breakpoint 2 condition detected. */
#define X86_DR6_B2                          RT_BIT_32(2)
/** Bit 3 - B3 - Breakpoint 3 condition detected. */
#define X86_DR6_B3                          RT_BIT_32(3)
/** Mask of all the Bx bits. */
#define X86_DR6_B_MASK                      UINT64_C(0x0000000f)
/** Bit 13 - BD - Debug register access detected. Corresponds to the X86_DR7_GD bit. */
#define X86_DR6_BD                          RT_BIT_32(13)
/** Bit 14 - BS - Single step */
#define X86_DR6_BS                          RT_BIT_32(14)
/** Bit 15 - BT - Task switch. (TSS T bit.) */
#define X86_DR6_BT                          RT_BIT_32(15)
/** Bit 16 - RTM - Cleared if debug exception inside RTM (@sa X86_DR7_RTM). */
#define X86_DR6_RTM                         RT_BIT_32(16)
/** Value of DR6 after powerup/reset. */
#define X86_DR6_INIT_VAL                    UINT64_C(0xffff0ff0)
/** Bits which must be 1s in DR6. */
#define X86_DR6_RA1_MASK                    UINT64_C(0xffff0ff0)
/** Bits which must be 1s in DR6, when RTM is supported. */
#define X86_DR6_RA1_MASK_RTM                UINT64_C(0xfffe0ff0)
/** Bits which must be 0s in DR6. */
#define X86_DR6_RAZ_MASK                    RT_BIT_64(12)
/** Bits which must be 0s on writes to DR6. */
#define X86_DR6_MBZ_MASK                    UINT64_C(0xffffffff00000000)
/** @} */

/** Get the DR6.Bx bit for a the given breakpoint. */
#define X86_DR6_B(iBp)                      RT_BIT_64(iBp)


/** @name DR7
 * @{ */
/** Bit 0 - L0 - Local breakpoint enable. Cleared on task switch. */
#define X86_DR7_L0                          RT_BIT_32(0)
/** Bit 1 - G0 - Global breakpoint enable. Not cleared on task switch. */
#define X86_DR7_G0                          RT_BIT_32(1)
/** Bit 2 - L1 - Local breakpoint enable. Cleared on task switch. */
#define X86_DR7_L1                          RT_BIT_32(2)
/** Bit 3 - G1 - Global breakpoint enable. Not cleared on task switch. */
#define X86_DR7_G1                          RT_BIT_32(3)
/** Bit 4 - L2 - Local breakpoint enable. Cleared on task switch. */
#define X86_DR7_L2                          RT_BIT_32(4)
/** Bit 5 - G2 - Global breakpoint enable. Not cleared on task switch. */
#define X86_DR7_G2                          RT_BIT_32(5)
/** Bit 6 - L3 - Local breakpoint enable. Cleared on task switch. */
#define X86_DR7_L3                          RT_BIT_32(6)
/** Bit 7 - G3 - Global breakpoint enable. Not cleared on task switch. */
#define X86_DR7_G3                          RT_BIT_32(7)
/** Bit 8 - LE - Local breakpoint exact. (Not supported (read ignored) by P6 and later.) */
#define X86_DR7_LE                          RT_BIT_32(8)
/** Bit 9 - GE - Global breakpoint exact. (Not supported (read ignored) by P6 and later.) */
#define X86_DR7_GE                          RT_BIT_32(9)

/** L0, L1, L2, and L3.  */
#define X86_DR7_LE_ALL                      UINT64_C(0x0000000000000055)
/** L0, L1, L2, and L3.  */
#define X86_DR7_GE_ALL                      UINT64_C(0x00000000000000aa)

/** Bit 11 - RTM - Enable advanced debugging of RTM transactions.
 * Requires IA32_DEBUGCTL.RTM=1 too, and RTM HW support of course.  */
#define X86_DR7_RTM                         RT_BIT_32(11)
/** Bit 12 - IR (ICE) - Interrupt redirection on Pentium.  When set, the in
 * Circuit Emulator (ICE) will break emulation on breakpoints and stuff.
 * May cause CPU hang if enabled without ICE attached when the ICEBP/INT1
 * instruction is executed.
 * @see http://www.rcollins.org/secrets/DR7.html  */
#define X86_DR7_ICE_IR                      RT_BIT_32(12)
/** Bit 13 - GD - General detect enable. Enables emulators to get exceptions when
 * any DR register is accessed. */
#define X86_DR7_GD                          RT_BIT_32(13)
/** Bit 14 - TR1 (ICE) - Code discontinuity trace for use with ICE on
 *  Pentium. */
#define X86_DR7_ICE_TR1                     RT_BIT_32(14)
/** Bit 15 - TR2 (ICE) - Controls unknown ICE trace feature of the pentium. */
#define X86_DR7_ICE_TR2                     RT_BIT_32(15)
/** Bit 16 & 17 - R/W0 - Read write field 0. Values X86_DR7_RW_*. */
#define X86_DR7_RW0_MASK                    (3 << 16)
/** Bit 18 & 19 - LEN0 - Length field 0. Values X86_DR7_LEN_*. */
#define X86_DR7_LEN0_MASK                   (3 << 18)
/** Bit 20 & 21 - R/W1 - Read write field 0. Values X86_DR7_RW_*. */
#define X86_DR7_RW1_MASK                    (3 << 20)
/** Bit 22 & 23 - LEN1 - Length field 0. Values X86_DR7_LEN_*. */
#define X86_DR7_LEN1_MASK                   (3 << 22)
/** Bit 24 & 25 - R/W2 - Read write field 0. Values X86_DR7_RW_*. */
#define X86_DR7_RW2_MASK                    (3 << 24)
/** Bit 26 & 27 - LEN2 - Length field 0. Values X86_DR7_LEN_*. */
#define X86_DR7_LEN2_MASK                   (3 << 26)
/** Bit 28 & 29 - R/W3 - Read write field 0. Values X86_DR7_RW_*. */
#define X86_DR7_RW3_MASK                    (3 << 28)
/** Bit 30 & 31 - LEN3 - Length field 0. Values X86_DR7_LEN_*. */
#define X86_DR7_LEN3_MASK                   (3 << 30)

/** Bits which reads as 1s. */
#define X86_DR7_RA1_MASK                    RT_BIT_32(10)
/** Bits which reads as zeros.  These are related to ICE (bits 12, 14, 15). */
#define X86_DR7_RAZ_MASK                    UINT64_C(0x0000d800)
/** Bits which must be 0s when writing to DR7. */
#define X86_DR7_MBZ_MASK                    UINT64_C(0xffffffff00000000)

/** Calcs the L bit of Nth breakpoint.
 * @param   iBp     The breakpoint number [0..3].
 */
#define X86_DR7_L(iBp)                      ( UINT32_C(1) << (iBp * 2) )

/** Calcs the G bit of Nth breakpoint.
 * @param   iBp     The breakpoint number [0..3].
 */
#define X86_DR7_G(iBp)                      ( UINT32_C(1) << (iBp * 2 + 1) )

/** Calcs the L and G bits of Nth breakpoint.
 * @param   iBp     The breakpoint number [0..3].
 */
#define X86_DR7_L_G(iBp)                    ( UINT32_C(3) << (iBp * 2) )

/** @name Read/Write values.
 * @{ */
/** Break on instruction fetch only. */
#define X86_DR7_RW_EO                       UINT32_C(0)
/** Break on write only. */
#define X86_DR7_RW_WO                       UINT32_C(1)
/** Break on I/O read/write. This is only defined if CR4.DE is set. */
#define X86_DR7_RW_IO                       UINT32_C(2)
/** Break on read or write (but not instruction fetches). */
#define X86_DR7_RW_RW                       UINT32_C(3)
/** @} */

/** Shifts a X86_DR7_RW_* value to its right place.
 * @param   iBp     The breakpoint number [0..3].
 * @param   fRw     One of the X86_DR7_RW_* value.
 */
#define X86_DR7_RW(iBp, fRw)                ( (fRw) << ((iBp) * 4 + 16) )

/** Fetch the R/Wx bits for a given breakpoint (so it can be compared with
 * one of the X86_DR7_RW_XXX constants).
 *
 * @returns X86_DR7_RW_XXX
 * @param   uDR7    DR7 value
 * @param   iBp     The breakpoint number [0..3].
 */
#define X86_DR7_GET_RW(uDR7, iBp)            ( ( (uDR7) >> ((iBp) * 4 + 16) ) & UINT32_C(3) )

/** R/W0, R/W1, R/W2, and R/W3. */
#define X86_DR7_RW_ALL_MASKS                UINT32_C(0x33330000)

#ifndef VBOX_FOR_DTRACE_LIB
/** Checks if the RW and LEN fields are set up for an instruction breakpoint.
 * @note This does not check if it's enabled. */
# define X86_DR7_IS_EO_CFG(a_uDR7, a_iBp)   ( ((a_uDR7) & (UINT32_C(0x000f0000) << ((a_iBp) * 4))) == 0 )
/** Checks if an instruction breakpoint is enabled and configured correctly.
 * @sa X86_DR7_IS_EO_CFG, X86_DR7_ANY_EO_ENABLED */
# define X86_DR7_IS_EO_ENABLED(a_uDR7, a_iBp) \
    ( ((a_uDR7) & (UINT32_C(0x03) << ((a_iBp) * 2))) != 0 && X86_DR7_IS_EO_CFG(a_uDR7, a_iBp) )
/** Checks if there are any instruction fetch breakpoint types configured in
 * the RW and LEN registers and enabled in the Lx/Gx bits.
 * @sa X86_DR7_IS_EO_CFG, X86_DR7_IS_EO_ENABLED */
# define X86_DR7_ANY_EO_ENABLED(a_uDR7) \
    (   (((a_uDR7) & UINT32_C(0x03)) != 0 && ((a_uDR7) & UINT32_C(0x000f0000)) == 0) \
     || (((a_uDR7) & UINT32_C(0x0c)) != 0 && ((a_uDR7) & UINT32_C(0x00f00000)) == 0) \
     || (((a_uDR7) & UINT32_C(0x30)) != 0 && ((a_uDR7) & UINT32_C(0x0f000000)) == 0) \
     || (((a_uDR7) & UINT32_C(0xc0)) != 0 && ((a_uDR7) & UINT32_C(0xf0000000)) == 0) )

/** Checks if the RW field is set up for a read-write data breakpoint.
 * @note This does not check if it's enabled. */
# define X86_DR7_IS_RW_CFG(a_uDR7, a_iBp)   ( ~((a_uDR7) & (UINT32_C(0x00030000) << ((a_iBp) * 4))) == 0)

/** Checks if there are any read-write data breakpoint types configured in the
 * RW registers and enabled in the Lx/Gx bits.
 *
 * @note We don't consider the LEN registers here, even if qword isn't
 *       techincally valid for older processors - see
 *       @sdmv3{082,645,18.2.4,Debug Control Register (DR7)} for details.
 */
# define X86_DR7_ANY_RW_ENABLED(a_uDR7) \
    (   (((a_uDR7) & UINT32_C(0x03)) != 0 && ((a_uDR7) & UINT32_C(0x00030000)) == UINT32_C(0x00030000)) \
     || (((a_uDR7) & UINT32_C(0x0c)) != 0 && ((a_uDR7) & UINT32_C(0x00300000)) == UINT32_C(0x00300000)) \
     || (((a_uDR7) & UINT32_C(0x30)) != 0 && ((a_uDR7) & UINT32_C(0x03000000)) == UINT32_C(0x03000000)) \
     || (((a_uDR7) & UINT32_C(0xc0)) != 0 && ((a_uDR7) & UINT32_C(0x30000000)) == UINT32_C(0x30000000)) )

/** Checks if the RW field is set up for a write-only or read-write data
 *  breakpoint.
 * @note This does not check if it's enabled. */
# define X86_DR7_IS_W_CFG(a_uDR7, a_iBp)   ( ((a_uDR7) & (UINT32_C(0x00010000) << ((a_iBp) * 4))) != 0)

/** Checks if there are any read-write or write-only data breakpoint types
 * configured in the the RW registers and enabled in the Lx/Gx bits.
 *
 * @note We don't consider the LEN registers here, even if qword isn't
 *       techincally valid for older processors - see
 *       @sdmv3{082,645,18.2.4,Debug Control Register (DR7)} for details.
 */
# define X86_DR7_ANY_W_ENABLED(a_uDR7) \
    (   (((a_uDR7) & UINT32_C(0x03)) != 0 && ((a_uDR7) & UINT32_C(0x00010000)) != 0) \
     || (((a_uDR7) & UINT32_C(0x0c)) != 0 && ((a_uDR7) & UINT32_C(0x00100000)) != 0) \
     || (((a_uDR7) & UINT32_C(0x30)) != 0 && ((a_uDR7) & UINT32_C(0x01000000)) != 0) \
     || (((a_uDR7) & UINT32_C(0xc0)) != 0 && ((a_uDR7) & UINT32_C(0x10000000)) != 0) )

/** Checks if there are any I/O breakpoint types configured in the RW
 * registers.  Does NOT check if these are enabled, sorry. */
# define X86_DR7_ANY_RW_IO(uDR7) \
    (   (    UINT32_C(0x22220000) & (uDR7) ) /* any candidates? */ \
     && ( ( (UINT32_C(0x22220000) & (uDR7) ) >> 1 )  &  ~(uDR7) ) )
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x33330000)) == 0);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x22220000)) == 1);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x32320000)) == 1);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x23230000)) == 1);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x00000000)) == 0);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x00010000)) == 0);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x00020000)) == 1);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x00030000)) == 0);
AssertCompile(X86_DR7_ANY_RW_IO(UINT32_C(0x00040000)) == 0);

#endif /* !VBOX_FOR_DTRACE_LIB */

/** @name Length values.
 * @{ */
#define X86_DR7_LEN_BYTE                    UINT32_C(0)
#define X86_DR7_LEN_WORD                    UINT32_C(1)
#define X86_DR7_LEN_QWORD                   UINT32_C(2) /**< AMD64 long mode only. */
#define X86_DR7_LEN_DWORD                   UINT32_C(3)
/** @} */

/** Shifts a X86_DR7_LEN_* value to its right place.
 * @param   iBp     The breakpoint number [0..3].
 * @param   cb      One of the X86_DR7_LEN_* values.
 */
#define X86_DR7_LEN(iBp, cb)                ( (cb) << ((iBp) * 4 + 18) )

/** Fetch the breakpoint length bits from the DR7 value.
 * @param   uDR7    DR7 value
 * @param   iBp     The breakpoint number [0..3].
 */
#define X86_DR7_GET_LEN(uDR7, iBp)          ( ( (uDR7) >> ((iBp) * 4 + 18) ) & UINT32_C(0x3) )

/** Mask used to check if any breakpoints are enabled. */
#define X86_DR7_ENABLED_MASK                UINT32_C(0x000000ff)

/** LEN0, LEN1, LEN2, and LEN3. */
#define X86_DR7_LEN_ALL_MASKS               UINT32_C(0xcccc0000)
/** R/W0, R/W1, R/W2, R/W3,LEN0, LEN1, LEN2, and LEN3. */
#define X86_DR7_RW_LEN_ALL_MASKS            UINT32_C(0xffff0000)

/** Value of DR7 after powerup/reset. */
#define X86_DR7_INIT_VAL                    0x400
/** @} */


/** @name Machine Specific Registers
 * @{
 */
/** Machine check address register (P5). */
#define MSR_P5_MC_ADDR                      UINT32_C(0x00000000)
/** Machine check type register (P5). */
#define MSR_P5_MC_TYPE                      UINT32_C(0x00000001)
/** Time Stamp Counter. */
#define MSR_IA32_TSC                        0x10
#define MSR_IA32_CESR                       UINT32_C(0x00000011)
#define MSR_IA32_CTR0                       UINT32_C(0x00000012)
#define MSR_IA32_CTR1                       UINT32_C(0x00000013)

#define MSR_IA32_PLATFORM_ID                0x17

#ifndef MSR_IA32_APICBASE /* qemu cpu.h kludge */
# define MSR_IA32_APICBASE                  0x1b
/** Local APIC enabled. */
# define MSR_IA32_APICBASE_EN               RT_BIT_64(11)
/** X2APIC enabled (requires the EN bit to be set). */
# define MSR_IA32_APICBASE_EXTD             RT_BIT_64(10)
/** The processor is the boot strap processor (BSP). */
# define MSR_IA32_APICBASE_BSP              RT_BIT_64(8)
/** Minimum base address mask, consult CPUID leaf 0x80000008 for the actual
 *  width. */
# define MSR_IA32_APICBASE_BASE_MIN         UINT64_C(0x0000000ffffff000)
/** The default physical base address of the APIC. */
# define MSR_IA32_APICBASE_ADDR             UINT64_C(0x00000000fee00000)
/** Gets the physical base address from the MSR. */
# define MSR_IA32_APICBASE_GET_ADDR(a_Msr)  ((a_Msr) & X86_PAGE_4K_BASE_MASK)
#endif

/** Memory Control (Intel-specific). */
#define MSR_MEMORY_CTRL                     0x33
/** Memory Control - UC-store throttle. */
#define MSR_MEMORY_CTRL_UC_STORE_THROTTLE           RT_BIT_64(27)
/** Memory Control - UC-lock disable. */
#define MSR_MEMORY_CTRL_UC_LOCK_DISABLE             RT_BIT_64(28)
/** Memory Control - Split-lock disable. */
#define MSR_MEMORY_CTRL_SPLIT_LOCK_DISABLE          RT_BIT_64(29)

/** Undocumented intel MSR for reporting thread and core counts.
 * Judging from the XNU sources, it seems to be introduced in Nehalem. The
 * first 16 bits is the thread count. The next 16 bits the core count, except
 * on Westmere where it seems it's only the next 4 bits for some reason. */
#define MSR_CORE_THREAD_COUNT               0x35

/** CPU Feature control. */
#define MSR_IA32_FEATURE_CONTROL                      0x3A
/** Feature control - Lock MSR from writes (R/W0). */
#define MSR_IA32_FEATURE_CONTROL_LOCK                 RT_BIT_64(0)
/** Feature control - Enable VMX inside SMX operation (R/WL). */
#define MSR_IA32_FEATURE_CONTROL_SMX_VMXON            RT_BIT_64(1)
/** Feature control - Enable VMX outside SMX operation (R/WL). */
#define MSR_IA32_FEATURE_CONTROL_VMXON                RT_BIT_64(2)
/** Feature control - SENTER local functions enable (R/WL).  */
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_0    RT_BIT_64(8)
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_1    RT_BIT_64(9)
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_2    RT_BIT_64(10)
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_3    RT_BIT_64(11)
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_4    RT_BIT_64(12)
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_5    RT_BIT_64(13)
#define MSR_IA32_FEATURE_CONTROL_SENTER_LOCAL_FN_6    RT_BIT_64(14)
/** Feature control - SENTER global enable (R/WL). */
#define MSR_IA32_FEATURE_CONTROL_SENTER_GLOBAL_EN     RT_BIT_64(15)
/** Feature control - SGX launch control enable (R/WL). */
#define MSR_IA32_FEATURE_CONTROL_SGX_LAUNCH_EN        RT_BIT_64(17)
/** Feature control - SGX global enable (R/WL). */
#define MSR_IA32_FEATURE_CONTROL_SGX_GLOBAL_EN        RT_BIT_64(18)
/** Feature control - LMCE on (R/WL). */
#define MSR_IA32_FEATURE_CONTROL_LMCE                 RT_BIT_64(20)

/** Per-processor TSC adjust MSR. */
#define MSR_IA32_TSC_ADJUST                 0x3B

/** Spectre control register.
 * Logical processor scope. Reset value 0, unaffected by SIPI & INIT. */
#define MSR_IA32_SPEC_CTRL                  0x48
/** @name MSR_IA32_SPEC_CTRL bits
 * @{ */
/** IBRS - Indirect branch restricted speculation. */
#define MSR_IA32_SPEC_CTRL_F_IBRS                   RT_BIT_64(0)
/** STIBP - Single thread indirect branch predictors. */
#define MSR_IA32_SPEC_CTRL_F_STIBP                  RT_BIT_64(1)
/** SSBD - Speculative Store Bypass Disable. */
#define MSR_IA32_SPEC_CTRL_F_SSBD                   RT_BIT_64(2)
#define MSR_IA32_SPEC_CTRL_F_IPRED_DIS_U            RT_BIT_64(3)
#define MSR_IA32_SPEC_CTRL_F_IPRED_DIS_S            RT_BIT_64(4)
#define MSR_IA32_SPEC_CTRL_F_RRSBA_DIS_U            RT_BIT_64(5)
#define MSR_IA32_SPEC_CTRL_F_RRSBA_DIS_S            RT_BIT_64(6)
#define MSR_IA32_SPEC_CTRL_F_PSFD                   RT_BIT_64(7)
#define MSR_IA32_SPEC_CTRL_F_DDPD_U                 RT_BIT_64(8)
/* 9 is reserved (for DDPD_S?) */
#define MSR_IA32_SPEC_CTRL_F_BHI_DIS_S              RT_BIT_64(9)
/** @} */

/** Prediction command register.
 * Write only, logical processor scope, no state since write only. */
#define MSR_IA32_PRED_CMD                   0x49
/** IBPB - Indirect branch prediction barrie when written as 1. */
#define MSR_IA32_PRED_CMD_F_IBPB                    RT_BIT_64(0)

/** BIOS update trigger (microcode update). */
#define MSR_IA32_BIOS_UPDT_TRIG             0x79

/** BIOS update signature (microcode). */
#define MSR_IA32_BIOS_SIGN_ID               0x8B

/** SMM monitor control. */
#define MSR_IA32_SMM_MONITOR_CTL            0x9B
/** SMM control - Valid. */
#define MSR_IA32_SMM_MONITOR_VALID                  RT_BIT_64(0)
/** SMM control - VMXOFF unblocks SMI. */
#define MSR_IA32_SMM_MONITOR_VMXOFF_UNBLOCK_SMI     RT_BIT_64(2)
/** SMM control - MSEG base physical address. */
#define MSR_IA32_SMM_MONITOR_MSGEG_PHYSADDR(a)      (((a) >> 12) & UINT64_C(0xfffff))

/** SMBASE - Base address of SMRANGE image (Read-only, SMM only). */
#define MSR_IA32_SMBASE                     0x9E

/** General performance counter no. 0. */
#define MSR_IA32_PMC0                       0xC1
/** General performance counter no. 1. */
#define MSR_IA32_PMC1                       0xC2
/** General performance counter no. 2. */
#define MSR_IA32_PMC2                       0xC3
/** General performance counter no. 3. */
#define MSR_IA32_PMC3                       0xC4
/** General performance counter no. 4. */
#define MSR_IA32_PMC4                       0xC5
/** General performance counter no. 5. */
#define MSR_IA32_PMC5                       0xC6
/** General performance counter no. 6. */
#define MSR_IA32_PMC6                       0xC7
/** General performance counter no. 7. */
#define MSR_IA32_PMC7                       0xC8

/** Nehalem power control. */
#define MSR_IA32_PLATFORM_INFO              0xCE

/** Core Capabilities (Intel-specific). */
#define MSR_IA32_CORE_CAPABILITIES          0xCF
/** STLB QoS feature supported. */
#define MSR_IA32_CORE_CAP_STLB_QOS                  RT_BIT_64(0)
/** FUSA feature supported. */
#define MSR_IA32_CORE_CAP_FUSA                      RT_BIT_64(2)
/** RSM instruction only allowed in CPL 0. */
#define MSR_IA32_CORE_CAP_RSM_CPL0                  RT_BIT_64(3)
/** UC lock disable supported. */
#define MSR_IA32_CORE_CAP_UC_LOCK_DISABLE           RT_BIT_64(4)
/** Split-lock disable supported. */
#define MSR_IA32_CORE_CAP_SPLIT_LOCK_DISABLE        RT_BIT_64(5)
/** Snoop filter QoS Mask MSRs supported. */
#define MSR_IA32_CORE_CAP_SNOOP_FILTER_QOS          RT_BIT_64(6)
/** UC store throttling supported. */
#define MSR_IA32_CORE_CAP_UC_STORE_THROTTLE         RT_BIT_64(7)

/** Get FSB clock status (Intel-specific). */
#define MSR_IA32_FSB_CLOCK_STS              0xCD

/** C-State configuration control. Intel specific: Nehalem, Sandy Bridge. */
#define MSR_PKG_CST_CONFIG_CONTROL          UINT32_C(0x000000e2)

/** C0 Maximum Frequency Clock Count */
#define MSR_IA32_MPERF                      0xE7
/** C0 Actual Frequency Clock Count */
#define MSR_IA32_APERF                      0xE8

/** MTRR Capabilities. */
#define MSR_IA32_MTRR_CAP                   0xFE
/** Bits 0-7 - VCNT - Variable range registers count. */
#define MSR_IA32_MTRR_CAP_VCNT_MASK         UINT64_C(0x00000000000000ff)
/** Bit 8 - FIX - Fixed range registers supported. */
#define MSR_IA32_MTRR_CAP_FIX               RT_BIT_64(8)
/** Bit 10 - WC - Write-Combining memory type supported. */
#define MSR_IA32_MTRR_CAP_WC                RT_BIT_64(10)
/** Bit 11 - SMRR - System Management Range Register supported. */
#define MSR_IA32_MTRR_CAP_SMRR              RT_BIT_64(11)
/** Bit 12 - PRMRR - Processor Reserved Memory Range Register supported. */
#define MSR_IA32_MTRR_CAP_PRMRR             RT_BIT_64(12)


#ifndef __ASSEMBLER__
/**
 * Variable-range MTRR MSR pair.
 */
typedef struct X86MTRRVAR
{
    uint64_t        MtrrPhysBase;           /**< IA32_MTRR_PHYSBASEn */
    uint64_t        MtrrPhysMask;           /**< IA32_MTRR_PHYSMASKn */
} X86MTRRVAR;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86MTRRVAR, 16);
# endif
/** Pointer to a variable-range MTRR MSR pair. */
typedef X86MTRRVAR *PX86MTRRVAR;
/** Pointer to a const variable-range MTRR MSR pair. */
typedef const X86MTRRVAR *PCX86MTRRVAR;
#endif /* __ASSEMBLER__ */


/** Memory types that can be encoded in MTRRs.
 * @{ */
/** Uncacheable. */
#define X86_MTRR_MT_UC                      0
/** Write Combining. */
#define X86_MTRR_MT_WC                      1
/** Write-through. */
#define X86_MTRR_MT_WT                      4
/** Write-protected. */
#define X86_MTRR_MT_WP                      5
/** Writeback. */
#define X86_MTRR_MT_WB                      6
/** @}*/

/** Architecture capabilities (bugfixes). */
#define MSR_IA32_ARCH_CAPABILITIES          UINT32_C(0x10a)
/** @name MSR_IA32_ARCH_CAPABILITIES bits
 * @{ */
/** CPU is no subject to meltdown problems. */
#define MSR_IA32_ARCH_CAP_F_RDCL_NO                 RT_BIT_64(0)
/** CPU has better IBRS and you can leave it on all the time. */
#define MSR_IA32_ARCH_CAP_F_IBRS_ALL                RT_BIT_64(1)
/** CPU has return stack buffer (RSB) override. */
#define MSR_IA32_ARCH_CAP_F_RSBO                    RT_BIT_64(2)
/** Virtual machine monitors need not flush the level 1 data cache on VM entry.
 * This is also the case when MSR_IA32_ARCH_CAP_F_RDCL_NO is set. */
#define MSR_IA32_ARCH_CAP_F_VMM_NEED_NOT_FLUSH_L1D  RT_BIT_64(3)
/** CPU does not suffer from speculative store bypass (SSB) issues.   */
#define MSR_IA32_ARCH_CAP_F_SSB_NO                  RT_BIT_64(4)
/** CPU does not suffer from microarchitectural data sampling (MDS) issues. */
#define MSR_IA32_ARCH_CAP_F_MDS_NO                  RT_BIT_64(5)
/** CPU does not suffer MCE after change code page size w/o invlpg issues. */
#define MSR_IA32_ARCH_CAP_F_IF_PSCHANGE_MC_NO       RT_BIT_64(6)
/** CPU has RTM_DISABLE and TXS_CPUID_CLEAR support. */
#define MSR_IA32_ARCH_CAP_F_TSX_CTRL                RT_BIT_64(7)
/** CPU does not suffer from transaction synchronization extensions (TSX)
 *  asyncrhonous abort (TAA) issues. */
#define MSR_IA32_ARCH_CAP_F_TAA_NO                  RT_BIT_64(8)
/* 9 is 'reserved' */
#define MSR_IA32_ARCH_CAP_F_MISC_PACKAGE_CTRLS      RT_BIT_64(10)
#define MSR_IA32_ARCH_CAP_F_ENERGY_FILTERING_CTL    RT_BIT_64(11)
#define MSR_IA32_ARCH_CAP_F_DOITM                   RT_BIT_64(12)
#define MSR_IA32_ARCH_CAP_F_SBDR_SSDP_NO            RT_BIT_64(13)
#define MSR_IA32_ARCH_CAP_F_FBSDP_NO                RT_BIT_64(14)
#define MSR_IA32_ARCH_CAP_F_PSDP_NO                 RT_BIT_64(15)
/* 16 is 'reserved' */
#define MSR_IA32_ARCH_CAP_F_FB_CLEAR                RT_BIT_64(17)
#define MSR_IA32_ARCH_CAP_F_FB_CLEAR_CTRL           RT_BIT_64(18)
#define MSR_IA32_ARCH_CAP_F_RRSBA                   RT_BIT_64(19)
#define MSR_IA32_ARCH_CAP_F_BHI_NO                  RT_BIT_64(20)
#define MSR_IA32_ARCH_CAP_F_XAPIC_DISABLE_STATUS    RT_BIT_64(21)
/* 22 is 'reserved' */
#define MSR_IA32_ARCH_CAP_F_OVERCLOCKING_STATUS     RT_BIT_64(23)
#define MSR_IA32_ARCH_CAP_F_PBRSB_NO                RT_BIT_64(24)
#define MSR_IA32_ARCH_CAP_F_GDS_CTRL                RT_BIT_64(25)
#define MSR_IA32_ARCH_CAP_F_GDS_NO                  RT_BIT_64(26)
#define MSR_IA32_ARCH_CAP_F_RFDS_NO                 RT_BIT_64(27)
#define MSR_IA32_ARCH_CAP_F_RFDS_CLEAR              RT_BIT_64(28)
#define MSR_IA32_ARCH_CAP_F_IGN_UMONITOR_SUPPORT    RT_BIT_64(29)
#define MSR_IA32_ARCH_CAP_F_MON_UMON_MITIG_SUPPORT  RT_BIT_64(30)
/** @} */

/** Flush command register. */
#define MSR_IA32_FLUSH_CMD                  UINT32_C(0x10b)
/** Flush the level 1 data cache when this bit is written. */
#define MSR_IA32_FLUSH_CMD_F_L1D                    RT_BIT_64(0)

/** Cache control/info. */
#define MSR_BBL_CR_CTL3                     UINT32_C(0x11e)

/** Microcode Update Option Control (R/W). */
#define MSR_IA32_MCU_OPT_CTRL                       0x123
/** MSR_IA32_MCU_OPT_CTRL[0]: RNGDS_MITG_DIS - disable SRBDS mitigations
 *  for RDRAND & RDSEED when set. */
#define MSR_IA32_MCU_OPT_CTRL_RNGDS_MITG_DIS        RT_BIT_64(0)
/** MSR_IA32_MCU_OPT_CTRL[1]: RTM_ALLOW - Allow TXS according to IA32_TSX_CTRL. */
#define MSR_IA32_MCU_OPT_CTRL_RTM_ALLOW             RT_BIT_64(1)
/** MSR_IA32_MCU_OPT_CTRL[2]: RTM_LOCKED - Lock RTM_ALLOW at zero. */
#define MSR_IA32_MCU_OPT_CTRL_RTM_LOCKED            RT_BIT_64(2)
/** MSR_IA32_MCU_OPT_CTRL[3]: FB_CLEAR_DIS - Disables FB_CLEAR part of VERW. */
#define MSR_IA32_MCU_OPT_CTRL_FB_CLEAR_DIS          RT_BIT_64(3)
/** MSR_IA32_MCU_OPT_CTRL[4]: GDS_MITG_DIS - Disables GDS mitigation on core. */
#define MSR_IA32_MCU_OPT_CTRL_GDS_MITG_DIS          RT_BIT_64(4)
/** MSR_IA32_MCU_OPT_CTRL[5]: GDS_MITG_DIS - Disables GDS mitigation on core. */
#define MSR_IA32_MCU_OPT_CTRL_GDS_MITG_LOCK         RT_BIT_64(5)
/** MSR_IA32_MCU_OPT_CTRL[6]: IGN_UMONITOR - Ignore UMONITOR & fail UMWAIT. */
#define MSR_IA32_MCU_OPT_CTRL_IGN_UMONITOR          RT_BIT_64(6)
/** MSR_IA32_MCU_OPT_CTRL[7]: MON_UMON_MITG - UMONITOR/MONITOR mitigation
 * (may affect sibling hyperthreads). */
#define MSR_IA32_MCU_OPT_CTRL_MON_UMON_MITG         RT_BIT_64(7)
/* Bits 63:7 reserved. */
#define MSR_IA32_MCU_OPT_CTRL_RSVD_MASK             UINT64_C(0xffffffffffffff80)

#ifndef MSR_IA32_SYSENTER_CS /* qemu cpu.h kludge */
/** SYSENTER_CS - the R0 CS, indirectly giving R0 SS, R3 CS and R3 DS.
 * R0 SS == CS + 8
 * R3 CS == CS + 16
 * R3 SS == CS + 24
 */
#define MSR_IA32_SYSENTER_CS                0x174
/** SYSENTER_ESP - the R0 ESP. */
#define MSR_IA32_SYSENTER_ESP               0x175
/** SYSENTER_EIP - the R0 EIP. */
#define MSR_IA32_SYSENTER_EIP               0x176
#endif

/** Machine Check Global Capabilities Register. */
#define MSR_IA32_MCG_CAP                    0x179
/** Machine Check Global Status Register. */
#define MSR_IA32_MCG_STATUS                 0x17A
/** Machine Check Global Control Register. */
#define MSR_IA32_MCG_CTRL                   0x17B

/** Page Attribute Table. */
#define MSR_IA32_CR_PAT                     0x277
/** Default PAT MSR value on processor powerup / reset (see Intel spec. 11.12.4
 *  "Programming the PAT", AMD spec. 7.8.2 "PAT Indexing") */
#define MSR_IA32_CR_PAT_INIT_VAL            UINT64_C(0x0007040600070406)

/** Memory types that can be encoded in the IA32_PAT MSR.
 * @{ */
/** Uncacheable. */
#define MSR_IA32_PAT_MT_UC                  0
/** Write Combining. */
#define MSR_IA32_PAT_MT_WC                  1
/** Reserved value 2. */
#define MSR_IA32_PAT_MT_RSVD_2              2
/** Reserved value 3. */
#define MSR_IA32_PAT_MT_RSVD_3              3
/** Write-through. */
#define MSR_IA32_PAT_MT_WT                  4
/** Write-protected. */
#define MSR_IA32_PAT_MT_WP                  5
/** Writeback. */
#define MSR_IA32_PAT_MT_WB                  6
/** Uncached (UC-). */
#define MSR_IA32_PAT_MT_UCD                 7
/** @}*/


/** Performance event select MSRs. (Intel only) */
#define MSR_IA32_PERFEVTSEL0                0x186
#define MSR_IA32_PERFEVTSEL1                0x187
#define MSR_IA32_PERFEVTSEL2                0x188
#define MSR_IA32_PERFEVTSEL3                0x189

/** Flexible ratio, seems to be undocumented, used by XNU (tsc.c).
 * The 16th bit whether flex ratio is being used, in which case bits 15:8
 * holds a ratio that Apple takes for TSC granularity.
 *
 * @note This MSR conflicts the P4 MSR_MCG_R12 register. */
#define MSR_FLEX_RATIO                      0x194
/** Performance state value and starting with Intel core more.
 * Apple uses the >=core features to determine TSC granularity on older CPUs. */
#define MSR_IA32_PERF_STATUS                0x198
#define MSR_IA32_PERF_CTL                   0x199
#define MSR_IA32_THERM_STATUS               0x19c

/** Offcore response event select registers. */
#define MSR_OFFCORE_RSP_0                   0x1a6
#define MSR_OFFCORE_RSP_1                   0x1a7

/** Enable misc. processor features (R/W). */
#define MSR_IA32_MISC_ENABLE                   0x1A0
/** Enable fast-strings feature (for REP MOVS and REP STORS). */
#define MSR_IA32_MISC_ENABLE_FAST_STRINGS      RT_BIT_64(0)
/** Automatic Thermal Control Circuit Enable (R/W). */
#define MSR_IA32_MISC_ENABLE_TCC               RT_BIT_64(3)
/** Performance Monitoring Available (R). */
#define MSR_IA32_MISC_ENABLE_PERF_MON          RT_BIT_64(7)
/** Branch Trace Storage Unavailable (R/O). */
#define MSR_IA32_MISC_ENABLE_BTS_UNAVAIL       RT_BIT_64(11)
/** Precise Event Based Sampling (PEBS) Unavailable (R/O). */
#define MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL      RT_BIT_64(12)
/** Enhanced Intel SpeedStep Technology Enable (R/W). */
#define MSR_IA32_MISC_ENABLE_SST_ENABLE        RT_BIT_64(16)
/** If MONITOR/MWAIT is supported (R/W). */
#define MSR_IA32_MISC_ENABLE_MONITOR           RT_BIT_64(18)
/** Limit CPUID Maxval to 3 leafs (R/W). */
#define MSR_IA32_MISC_ENABLE_LIMIT_CPUID       RT_BIT_64(22)
/** When set to 1, xTPR messages are disabled (R/W). */
#define MSR_IA32_MISC_ENABLE_XTPR_MSG_DISABLE  RT_BIT_64(23)
/** When set to 1, the Execute Disable Bit feature (XD Bit) is disabled (R/W). */
#define MSR_IA32_MISC_ENABLE_XD_DISABLE        RT_BIT_64(34)

/** Trace/Profile Resource Control (R/W) */
#define MSR_IA32_DEBUGCTL                   UINT32_C(0x000001d9)
/** Last branch record. */
#define MSR_IA32_DEBUGCTL_LBR               RT_BIT_64(0)
/** Branch trace flag (single step on branches). */
#define MSR_IA32_DEBUGCTL_BTF               RT_BIT_64(1)
/** Performance monitoring pin control (AMD only). */
#define MSR_IA32_DEBUGCTL_PB0               RT_BIT_64(2)
#define MSR_IA32_DEBUGCTL_PB1               RT_BIT_64(3)
#define MSR_IA32_DEBUGCTL_PB2               RT_BIT_64(4)
#define MSR_IA32_DEBUGCTL_PB3               RT_BIT_64(5)
/** Trace message enable (Intel only). */
#define MSR_IA32_DEBUGCTL_TR                RT_BIT_64(6)
/** Branch trace store (Intel only). */
#define MSR_IA32_DEBUGCTL_BTS               RT_BIT_64(7)
/** Branch trace interrupt (Intel only). */
#define MSR_IA32_DEBUGCTL_BTINT             RT_BIT_64(8)
/** Branch trace off in privileged code (Intel only). */
#define MSR_IA32_DEBUGCTL_BTS_OFF_OS        RT_BIT_64(9)
/** Branch trace off in user code (Intel only). */
#define MSR_IA32_DEBUGCTL_BTS_OFF_USER      RT_BIT_64(10)
/** Freeze LBR on PMI flag (Intel only). */
#define MSR_IA32_DEBUGCTL_FREEZE_LBR_ON_PMI       RT_BIT_64(11)
/** Freeze PERFMON on PMI flag (Intel only). */
#define MSR_IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI   RT_BIT_64(12)
/** Freeze while SMM enabled (Intel only). */
#define MSR_IA32_DEBUGCTL_FREEZE_WHILE_SMM_EM     RT_BIT_64(14)
/** Advanced debugging of RTM regions (Intel only). */
#define MSR_IA32_DEBUGCTL_RTM               RT_BIT_64(15)
/** Debug control MSR valid bits (Intel only). */
#define MSR_IA32_DEBUGCTL_VALID_MASK_INTEL  (  MSR_IA32_DEBUGCTL_LBR | MSR_IA32_DEBUGCTL_BTF   | MSR_IA32_DEBUGCTL_TR  \
                                             | MSR_IA32_DEBUGCTL_BTS | MSR_IA32_DEBUGCTL_BTINT | MSR_IA32_DEBUGCTL_BTS_OFF_OS \
                                             | MSR_IA32_DEBUGCTL_BTS_OFF_USER | MSR_IA32_DEBUGCTL_FREEZE_LBR_ON_PMI \
                                             | MSR_IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI | MSR_IA32_DEBUGCTL_FREEZE_WHILE_SMM_EM \
                                             | MSR_IA32_DEBUGCTL_RTM)

/** @name Last branch registers for P4 and Xeon, models 0 thru 2.
 * @{ */
#define MSR_P4_LASTBRANCH_0                 0x1db
#define MSR_P4_LASTBRANCH_1                 0x1dc
#define MSR_P4_LASTBRANCH_2                 0x1dd
#define MSR_P4_LASTBRANCH_3                 0x1de

/** LBR Top-of-stack MSR (index to most recent record). */
#define MSR_P4_LASTBRANCH_TOS               0x1da
/** @} */

/** @name Last branch registers for Core 2 and related Xeons.
 * @{ */
#define MSR_CORE2_LASTBRANCH_0_FROM_IP      0x40
#define MSR_CORE2_LASTBRANCH_1_FROM_IP      0x41
#define MSR_CORE2_LASTBRANCH_2_FROM_IP      0x42
#define MSR_CORE2_LASTBRANCH_3_FROM_IP      0x43

#define MSR_CORE2_LASTBRANCH_0_TO_IP        0x60
#define MSR_CORE2_LASTBRANCH_1_TO_IP        0x61
#define MSR_CORE2_LASTBRANCH_2_TO_IP        0x62
#define MSR_CORE2_LASTBRANCH_3_TO_IP        0x63

/** LBR Top-of-stack MSR (index to most recent record). */
#define MSR_CORE2_LASTBRANCH_TOS            0x1c9
/** @} */

/** @name Last branch registers.
 * @{ */
#define MSR_LASTBRANCH_0_FROM_IP            0x680
#define MSR_LASTBRANCH_1_FROM_IP            0x681
#define MSR_LASTBRANCH_2_FROM_IP            0x682
#define MSR_LASTBRANCH_3_FROM_IP            0x683
#define MSR_LASTBRANCH_4_FROM_IP            0x684
#define MSR_LASTBRANCH_5_FROM_IP            0x685
#define MSR_LASTBRANCH_6_FROM_IP            0x686
#define MSR_LASTBRANCH_7_FROM_IP            0x687
#define MSR_LASTBRANCH_8_FROM_IP            0x688
#define MSR_LASTBRANCH_9_FROM_IP            0x689
#define MSR_LASTBRANCH_10_FROM_IP           0x68a
#define MSR_LASTBRANCH_11_FROM_IP           0x68b
#define MSR_LASTBRANCH_12_FROM_IP           0x68c
#define MSR_LASTBRANCH_13_FROM_IP           0x68d
#define MSR_LASTBRANCH_14_FROM_IP           0x68e
#define MSR_LASTBRANCH_15_FROM_IP           0x68f
#define MSR_LASTBRANCH_16_FROM_IP           0x690
#define MSR_LASTBRANCH_17_FROM_IP           0x691
#define MSR_LASTBRANCH_18_FROM_IP           0x692
#define MSR_LASTBRANCH_19_FROM_IP           0x693
#define MSR_LASTBRANCH_20_FROM_IP           0x694
#define MSR_LASTBRANCH_21_FROM_IP           0x695
#define MSR_LASTBRANCH_22_FROM_IP           0x696
#define MSR_LASTBRANCH_23_FROM_IP           0x697
#define MSR_LASTBRANCH_24_FROM_IP           0x698
#define MSR_LASTBRANCH_25_FROM_IP           0x699
#define MSR_LASTBRANCH_26_FROM_IP           0x69a
#define MSR_LASTBRANCH_27_FROM_IP           0x69b
#define MSR_LASTBRANCH_28_FROM_IP           0x69c
#define MSR_LASTBRANCH_29_FROM_IP           0x69d
#define MSR_LASTBRANCH_30_FROM_IP           0x69e
#define MSR_LASTBRANCH_31_FROM_IP           0x69f

#define MSR_LASTBRANCH_0_TO_IP              0x6c0
#define MSR_LASTBRANCH_1_TO_IP              0x6c1
#define MSR_LASTBRANCH_2_TO_IP              0x6c2
#define MSR_LASTBRANCH_3_TO_IP              0x6c3
#define MSR_LASTBRANCH_4_TO_IP              0x6c4
#define MSR_LASTBRANCH_5_TO_IP              0x6c5
#define MSR_LASTBRANCH_6_TO_IP              0x6c6
#define MSR_LASTBRANCH_7_TO_IP              0x6c7
#define MSR_LASTBRANCH_8_TO_IP              0x6c8
#define MSR_LASTBRANCH_9_TO_IP              0x6c9
#define MSR_LASTBRANCH_10_TO_IP             0x6ca
#define MSR_LASTBRANCH_11_TO_IP             0x6cb
#define MSR_LASTBRANCH_12_TO_IP             0x6cc
#define MSR_LASTBRANCH_13_TO_IP             0x6cd
#define MSR_LASTBRANCH_14_TO_IP             0x6ce
#define MSR_LASTBRANCH_15_TO_IP             0x6cf
#define MSR_LASTBRANCH_16_TO_IP             0x6d0
#define MSR_LASTBRANCH_17_TO_IP             0x6d1
#define MSR_LASTBRANCH_18_TO_IP             0x6d2
#define MSR_LASTBRANCH_19_TO_IP             0x6d3
#define MSR_LASTBRANCH_20_TO_IP             0x6d4
#define MSR_LASTBRANCH_21_TO_IP             0x6d5
#define MSR_LASTBRANCH_22_TO_IP             0x6d6
#define MSR_LASTBRANCH_23_TO_IP             0x6d7
#define MSR_LASTBRANCH_24_TO_IP             0x6d8
#define MSR_LASTBRANCH_25_TO_IP             0x6d9
#define MSR_LASTBRANCH_26_TO_IP             0x6da
#define MSR_LASTBRANCH_27_TO_IP             0x6db
#define MSR_LASTBRANCH_28_TO_IP             0x6dc
#define MSR_LASTBRANCH_29_TO_IP             0x6dd
#define MSR_LASTBRANCH_30_TO_IP             0x6de
#define MSR_LASTBRANCH_31_TO_IP             0x6df

#define MSR_LASTBRANCH_0_INFO               0xdc0
#define MSR_LASTBRANCH_1_INFO               0xdc1
#define MSR_LASTBRANCH_2_INFO               0xdc2
#define MSR_LASTBRANCH_3_INFO               0xdc3
#define MSR_LASTBRANCH_4_INFO               0xdc4
#define MSR_LASTBRANCH_5_INFO               0xdc5
#define MSR_LASTBRANCH_6_INFO               0xdc6
#define MSR_LASTBRANCH_7_INFO               0xdc7
#define MSR_LASTBRANCH_8_INFO               0xdc8
#define MSR_LASTBRANCH_9_INFO               0xdc9
#define MSR_LASTBRANCH_10_INFO              0xdca
#define MSR_LASTBRANCH_11_INFO              0xdcb
#define MSR_LASTBRANCH_12_INFO              0xdcc
#define MSR_LASTBRANCH_13_INFO              0xdcd
#define MSR_LASTBRANCH_14_INFO              0xdce
#define MSR_LASTBRANCH_15_INFO              0xdcf
#define MSR_LASTBRANCH_16_INFO              0xdd0
#define MSR_LASTBRANCH_17_INFO              0xdd1
#define MSR_LASTBRANCH_18_INFO              0xdd2
#define MSR_LASTBRANCH_19_INFO              0xdd3
#define MSR_LASTBRANCH_20_INFO              0xdd4
#define MSR_LASTBRANCH_21_INFO              0xdd5
#define MSR_LASTBRANCH_22_INFO              0xdd6
#define MSR_LASTBRANCH_23_INFO              0xdd7
#define MSR_LASTBRANCH_24_INFO              0xdd8
#define MSR_LASTBRANCH_25_INFO              0xdd9
#define MSR_LASTBRANCH_26_INFO              0xdda
#define MSR_LASTBRANCH_27_INFO              0xddb
#define MSR_LASTBRANCH_28_INFO              0xddc
#define MSR_LASTBRANCH_29_INFO              0xddd
#define MSR_LASTBRANCH_30_INFO              0xdde
#define MSR_LASTBRANCH_31_INFO              0xddf

/** LBR branch tracking selection MSR. */
#define MSR_LASTBRANCH_SELECT               0x1c8
/** LBR Top-of-stack MSR (index to most recent record). */
#define MSR_LASTBRANCH_TOS                  0x1c9
/** @} */

/** @name Last event record registers.
 * @{ */
/** Last event record source IP register. */
#define MSR_LER_FROM_IP                     0x1dd
/** Last event record destination IP register. */
#define MSR_LER_TO_IP                       0x1de
/** @} */

/** Intel TSX (Transactional Synchronization Extensions) control MSR. */
#define MSR_IA32_TSX_CTRL                   0x122

/** Variable range MTRRs.
 * @{ */
#define MSR_IA32_MTRR_PHYSBASE0             0x200
#define MSR_IA32_MTRR_PHYSMASK0             0x201
#define MSR_IA32_MTRR_PHYSBASE1             0x202
#define MSR_IA32_MTRR_PHYSMASK1             0x203
#define MSR_IA32_MTRR_PHYSBASE2             0x204
#define MSR_IA32_MTRR_PHYSMASK2             0x205
#define MSR_IA32_MTRR_PHYSBASE3             0x206
#define MSR_IA32_MTRR_PHYSMASK3             0x207
#define MSR_IA32_MTRR_PHYSBASE4             0x208
#define MSR_IA32_MTRR_PHYSMASK4             0x209
#define MSR_IA32_MTRR_PHYSBASE5             0x20a
#define MSR_IA32_MTRR_PHYSMASK5             0x20b
#define MSR_IA32_MTRR_PHYSBASE6             0x20c
#define MSR_IA32_MTRR_PHYSMASK6             0x20d
#define MSR_IA32_MTRR_PHYSBASE7             0x20e
#define MSR_IA32_MTRR_PHYSMASK7             0x20f
#define MSR_IA32_MTRR_PHYSBASE8             0x210
#define MSR_IA32_MTRR_PHYSMASK8             0x211
#define MSR_IA32_MTRR_PHYSBASE9             0x212
#define MSR_IA32_MTRR_PHYSMASK9             0x213
/** @} */

/** Fixed range MTRRs.
 * @{ */
#define MSR_IA32_MTRR_FIX64K_00000          0x250
#define MSR_IA32_MTRR_FIX16K_80000          0x258
#define MSR_IA32_MTRR_FIX16K_A0000          0x259
#define MSR_IA32_MTRR_FIX4K_C0000           0x268
#define MSR_IA32_MTRR_FIX4K_C8000           0x269
#define MSR_IA32_MTRR_FIX4K_D0000           0x26a
#define MSR_IA32_MTRR_FIX4K_D8000           0x26b
#define MSR_IA32_MTRR_FIX4K_E0000           0x26c
#define MSR_IA32_MTRR_FIX4K_E8000           0x26d
#define MSR_IA32_MTRR_FIX4K_F0000           0x26e
#define MSR_IA32_MTRR_FIX4K_F8000           0x26f
/** @} */

/** MTRR Default Type.
 * @{ */
#define MSR_IA32_MTRR_DEF_TYPE              0x2FF
#define MSR_IA32_MTRR_DEF_TYPE_DEF_MT_MASK  0xFF
#define MSR_IA32_MTRR_DEF_TYPE_FIXED_EN     RT_BIT_64(10)
#define MSR_IA32_MTRR_DEF_TYPE_MTRR_EN      RT_BIT_64(11)
#define MSR_IA32_MTRR_DEF_TYPE_VALID_MASK   (  MSR_IA32_MTRR_DEF_TYPE_DEF_MT_MASK \
                                             | MSR_IA32_MTRR_DEF_TYPE_FIXED_EN    \
                                             | MSR_IA32_MTRR_DEF_TYPE_MTRR_EN)
/** @} */

/** Variable-range MTRR physical mask valid. */
#define MSR_IA32_MTRR_PHYSMASK_VALID        RT_BIT_64(11)

/** Variable-range MTRR memory type mask. */
#define MSR_IA32_MTRR_PHYSBASE_MT_MASK      UINT64_C(0xff)

/** Global performance counter control facilities (Intel only). */
#define MSR_IA32_PERF_GLOBAL_STATUS         0x38E
#define MSR_IA32_PERF_GLOBAL_CTRL           0x38F
#define MSR_IA32_PERF_GLOBAL_OVF_CTRL       0x390

/** Precise Event Based sampling (Intel only). */
#define MSR_IA32_PEBS_ENABLE                0x3F1

#define MSR_IA32_MC0_CTL                    0x400
#define MSR_IA32_MC0_STATUS                 0x401

/** Basic VMX information. */
#define MSR_IA32_VMX_BASIC                  0x480
/** Allowed settings for pin-based VM execution controls. */
#define MSR_IA32_VMX_PINBASED_CTLS          0x481
/** Allowed settings for proc-based VM execution controls. */
#define MSR_IA32_VMX_PROCBASED_CTLS         0x482
/** Allowed settings for the VM-exit controls. */
#define MSR_IA32_VMX_EXIT_CTLS              0x483
/** Allowed settings for the VM-entry controls. */
#define MSR_IA32_VMX_ENTRY_CTLS             0x484
/** Misc VMX info. */
#define MSR_IA32_VMX_MISC                   0x485
/** Fixed cleared bits in CR0. */
#define MSR_IA32_VMX_CR0_FIXED0             0x486
/** Fixed set bits in CR0. */
#define MSR_IA32_VMX_CR0_FIXED1             0x487
/** Fixed cleared bits in CR4. */
#define MSR_IA32_VMX_CR4_FIXED0             0x488
/** Fixed set bits in CR4. */
#define MSR_IA32_VMX_CR4_FIXED1             0x489
/** Information for enumerating fields in the VMCS. */
#define MSR_IA32_VMX_VMCS_ENUM              0x48A
/** Allowed settings for secondary processor-based VM-execution controls. */
#define MSR_IA32_VMX_PROCBASED_CTLS2        0x48B
/** EPT capabilities. */
#define MSR_IA32_VMX_EPT_VPID_CAP           0x48C
/** Allowed settings of all pin-based VM execution controls. */
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS     0x48D
/** Allowed settings of all proc-based VM execution controls. */
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS    0x48E
/** Allowed settings of all VMX exit controls. */
#define MSR_IA32_VMX_TRUE_EXIT_CTLS         0x48F
/** Allowed settings of all VMX entry controls. */
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS        0x490
/** Allowed settings for the VM-function controls. */
#define MSR_IA32_VMX_VMFUNC                 0x491
/** Tertiary processor-based VM execution controls. */
#define MSR_IA32_VMX_PROCBASED_CTLS3        0x492
/** Secondary VM-exit controls. */
#define MSR_IA32_VMX_EXIT_CTLS2             0x493

/** Intel PT - Enable and control for trace packet generation. */
#define MSR_IA32_RTIT_CTL                   0x570

/** DS Save Area (R/W). */
#define MSR_IA32_DS_AREA                    0x600
/** Running Average Power Limit (RAPL) power units. */
#define MSR_RAPL_POWER_UNIT                 0x606
/** Package C3 Interrupt Response Limit. */
#define MSR_PKGC3_IRTL                      0x60a
/** Package C6/C7S Interrupt Response Limit 1. */
#define MSR_PKGC_IRTL1                      0x60b
/** Package C6/C7S Interrupt Response Limit 2. */
#define MSR_PKGC_IRTL2                      0x60c
/** Package C2 Residency Counter. */
#define MSR_PKG_C2_RESIDENCY                0x60d
/** PKG RAPL Power Limit Control. */
#define MSR_PKG_POWER_LIMIT                 0x610
/** PKG Energy Status. */
#define MSR_PKG_ENERGY_STATUS               0x611
/** PKG Perf Status. */
#define MSR_PKG_PERF_STATUS                 0x613
/** PKG RAPL Parameters. */
#define MSR_PKG_POWER_INFO                  0x614
/** DRAM RAPL Power Limit Control. */
#define MSR_DRAM_POWER_LIMIT                0x618
/** DRAM Energy Status. */
#define MSR_DRAM_ENERGY_STATUS              0x619
/** DRAM Performance Throttling Status. */
#define MSR_DRAM_PERF_STATUS                0x61b
/** DRAM RAPL Parameters. */
#define MSR_DRAM_POWER_INFO                 0x61c
/** Package C10 Residency Counter. */
#define MSR_PKG_C10_RESIDENCY               0x632
/** PP0 Energy Status. */
#define MSR_PP0_ENERGY_STATUS               0x639
/** PP1 Energy Status. */
#define MSR_PP1_ENERGY_STATUS               0x641
/** Turbo Activation Ratio. */
#define MSR_TURBO_ACTIVATION_RATIO          0x64c
/** Core Performance Limit Reasons. */
#define MSR_CORE_PERF_LIMIT_REASONS         0x64f

/** Userspace Control flow Enforcement Technology setting. */
#define MSR_IA32_U_CET                      0x6a0
/** Supervisor space Control flow Enforcement Technology setting. */
#define MSR_IA32_S_CET                      0x6a2
/** @name Bit fields for both MSR_IA32_U_CET and MSR_IA32_S_CET
 * @{ */
/** Enables the Shadow stack. */
# define MSR_IA32_CET_SH_STK_EN             RT_BIT_64(0)
/** Enables WRSS{D,Q}W instructions. */
# define MSR_IA32_CET_WR_SHSTK_EN           RT_BIT_64(1)
/** Enables indirect branch tracking. */
# define MSR_IA32_CET_ENDBR_EN              RT_BIT_64(2)
/** Enable legacy compatibility treatment for indirect branch tracking. */
# define MSR_IA32_CET_LEG_IW_EN             RT_BIT_64(3)
/** Enables the use of no-track prefix for indirect branch tracking. */
# define MSR_IA32_CET_NO_TRACK_EN           RT_BIT_64(4)
/** Disables suppression of CET indirect branch tracking on legacy compatibility. */
# define MSR_IA32_CET_SUPPRESS_DIS          RT_BIT_64(5)
/** Suppresses indirect branch tracking. */
# define MSR_IA32_CET_SUPPRESS              RT_BIT_64(10)
/** Returns the value of the indirect branch tracking state machine: IDLE(0), WAIT_FOR_ENDBRANCH(1). */
# define MSR_IA32_CET_TRACKER               RT_BIT_64(11)
/** Linear address of memory containing a bitmap indicating valid pages as CALL/JMP targets not landing
 * on a ENDBRANCH instruction. */
# define MSR_IA32_CET_EB_LEG_BITMAP_BASE    UINT64_C(0xfffffffffffff000)
/** @} */

/** X2APIC MSR range start. */
#define MSR_IA32_X2APIC_START               0x800
/** X2APIC MSR -  APIC ID Register. */
#define MSR_IA32_X2APIC_ID                  0x802
/** X2APIC MSR -  APIC Version Register. */
#define MSR_IA32_X2APIC_VERSION             0x803
/** X2APIC MSR -  Task Priority Register. */
#define MSR_IA32_X2APIC_TPR                 0x808
/** X2APIC MSR -  Processor Priority register. */
#define MSR_IA32_X2APIC_PPR                 0x80A
/** X2APIC MSR -  End Of Interrupt register. */
#define MSR_IA32_X2APIC_EOI                 0x80B
/** X2APIC MSR -  Logical Destination Register. */
#define MSR_IA32_X2APIC_LDR                 0x80D
/** X2APIC MSR -  Spurious Interrupt Vector Register. */
#define MSR_IA32_X2APIC_SVR                 0x80F
/** X2APIC MSR -  In-service Register (bits 31:0). */
#define MSR_IA32_X2APIC_ISR0                0x810
/** X2APIC MSR -  In-service Register (bits 63:32). */
#define MSR_IA32_X2APIC_ISR1                0x811
/** X2APIC MSR -  In-service Register (bits 95:64). */
#define MSR_IA32_X2APIC_ISR2                0x812
/** X2APIC MSR -  In-service Register (bits 127:96). */
#define MSR_IA32_X2APIC_ISR3                0x813
/** X2APIC MSR -  In-service Register (bits 159:128). */
#define MSR_IA32_X2APIC_ISR4                0x814
/** X2APIC MSR -  In-service Register (bits 191:160). */
#define MSR_IA32_X2APIC_ISR5                0x815
/** X2APIC MSR -  In-service Register (bits 223:192). */
#define MSR_IA32_X2APIC_ISR6                0x816
/** X2APIC MSR -  In-service Register (bits 255:224). */
#define MSR_IA32_X2APIC_ISR7                0x817
/** X2APIC MSR -  Trigger Mode Register (bits 31:0). */
#define MSR_IA32_X2APIC_TMR0                0x818
/** X2APIC MSR -  Trigger Mode Register (bits 63:32). */
#define MSR_IA32_X2APIC_TMR1                0x819
/** X2APIC MSR -  Trigger Mode Register (bits 95:64). */
#define MSR_IA32_X2APIC_TMR2                0x81A
/** X2APIC MSR -  Trigger Mode Register (bits 127:96). */
#define MSR_IA32_X2APIC_TMR3                0x81B
/** X2APIC MSR -  Trigger Mode Register (bits 159:128). */
#define MSR_IA32_X2APIC_TMR4                0x81C
/** X2APIC MSR -  Trigger Mode Register (bits 191:160). */
#define MSR_IA32_X2APIC_TMR5                0x81D
/** X2APIC MSR -  Trigger Mode Register (bits 223:192). */
#define MSR_IA32_X2APIC_TMR6                0x81E
/** X2APIC MSR -  Trigger Mode Register (bits 255:224). */
#define MSR_IA32_X2APIC_TMR7                0x81F
/** X2APIC MSR -  Interrupt Request Register (bits 31:0). */
#define MSR_IA32_X2APIC_IRR0                0x820
/** X2APIC MSR -  Interrupt Request Register (bits 63:32). */
#define MSR_IA32_X2APIC_IRR1                0x821
/** X2APIC MSR -  Interrupt Request Register (bits 95:64). */
#define MSR_IA32_X2APIC_IRR2                0x822
/** X2APIC MSR -  Interrupt Request Register (bits 127:96). */
#define MSR_IA32_X2APIC_IRR3                0x823
/** X2APIC MSR -  Interrupt Request Register (bits 159:128). */
#define MSR_IA32_X2APIC_IRR4                0x824
/** X2APIC MSR -  Interrupt Request Register (bits 191:160). */
#define MSR_IA32_X2APIC_IRR5                0x825
/** X2APIC MSR -  Interrupt Request Register (bits 223:192). */
#define MSR_IA32_X2APIC_IRR6                0x826
/** X2APIC MSR -  Interrupt Request Register (bits 255:224). */
#define MSR_IA32_X2APIC_IRR7                0x827
/** X2APIC MSR -  Error Status Register. */
#define MSR_IA32_X2APIC_ESR                 0x828
/** X2APIC MSR - LVT CMCI Register. */
#define MSR_IA32_X2APIC_LVT_CMCI            0x82F
/** X2APIC MSR -  Interrupt Command Register. */
#define MSR_IA32_X2APIC_ICR                 0x830
/** X2APIC MSR -  LVT Timer Register. */
#define MSR_IA32_X2APIC_LVT_TIMER           0x832
/** X2APIC MSR -  LVT Thermal Sensor Register. */
#define MSR_IA32_X2APIC_LVT_THERMAL         0x833
/** X2APIC MSR -  LVT Performance Counter Register. */
#define MSR_IA32_X2APIC_LVT_PERF            0x834
/** X2APIC MSR -  LVT LINT0 Register. */
#define MSR_IA32_X2APIC_LVT_LINT0           0x835
/** X2APIC MSR -  LVT LINT1 Register. */
#define MSR_IA32_X2APIC_LVT_LINT1           0x836
/** X2APIC MSR -  LVT Error Register . */
#define MSR_IA32_X2APIC_LVT_ERROR           0x837
/** X2APIC MSR -  Timer Initial Count Register. */
#define MSR_IA32_X2APIC_TIMER_ICR           0x838
/** X2APIC MSR -  Timer Current Count Register. */
#define MSR_IA32_X2APIC_TIMER_CCR           0x839
/** X2APIC MSR -  Timer Divide Configuration Register. */
#define MSR_IA32_X2APIC_TIMER_DCR           0x83E
/** X2APIC MSR - Self IPI. */
#define MSR_IA32_X2APIC_SELF_IPI            0x83F
/** X2APIC MSR range end. */
#define MSR_IA32_X2APIC_END                 0x8FF
/** X2APIC MSR - LVT start range. */
#define MSR_IA32_X2APIC_LVT_START           MSR_IA32_X2APIC_LVT_TIMER
/** X2APIC MSR - LVT end range (inclusive). */
#define MSR_IA32_X2APIC_LVT_END             MSR_IA32_X2APIC_LVT_ERROR

/** K6 EFER - Extended Feature Enable Register. */
#define MSR_K6_EFER                         UINT32_C(0xc0000080)
/** @todo document EFER */
/** Bit 0 - SCE - System call extensions (SYSCALL / SYSRET). (R/W) */
#define  MSR_K6_EFER_SCE                     RT_BIT_32(0)
/** Bit 8 - LME - Long mode enabled. (R/W) */
#define  MSR_K6_EFER_LME                     RT_BIT_32(8)
#define  MSR_K6_EFER_BIT_LME                 8 /**< Bit number of MSR_K6_EFER_LME */
/** Bit 10 - LMA - Long mode active. (R) */
#define  MSR_K6_EFER_LMA                     RT_BIT_32(10)
#define  MSR_K6_EFER_BIT_LMA                 10 /**< Bit number of MSR_K6_EFER_LMA */
/** Bit 11 - NXE - No-Execute Page Protection Enabled. (R/W) */
#define  MSR_K6_EFER_NXE                     RT_BIT_32(11)
#define  MSR_K6_EFER_BIT_NXE                 11 /**< Bit number of MSR_K6_EFER_NXE */
/** Bit 12 - SVME - Secure VM Extension Enabled. (R/W) */
#define  MSR_K6_EFER_SVME                    RT_BIT_32(12)
/** Bit 13 - LMSLE - Long Mode Segment Limit Enable. (R/W?) */
#define  MSR_K6_EFER_LMSLE                   RT_BIT_32(13)
/** Bit 14 - FFXSR - Fast FXSAVE / FXRSTOR (skip XMM*). (R/W) */
#define  MSR_K6_EFER_FFXSR                   RT_BIT_32(14)
/** Bit 15 - TCE - Translation Cache Extension. (R/W) */
#define  MSR_K6_EFER_TCE                     RT_BIT_32(15)
/** Bit 17 - MCOMMIT - Commit Stores to memory. (R/W) */
#define  MSR_K6_EFER_MCOMMIT                 RT_BIT_32(17)

/** K6 STAR - SYSCALL/RET targets. */
#define MSR_K6_STAR                         UINT32_C(0xc0000081)
/** Shift value for getting the SYSRET CS and SS value. */
#define  MSR_K6_STAR_SYSRET_CS_SS_SHIFT     48
/** Shift value for getting the SYSCALL CS and SS value. */
#define  MSR_K6_STAR_SYSCALL_CS_SS_SHIFT    32
/** Selector mask for use after shifting. */
#define  MSR_K6_STAR_SEL_MASK               UINT32_C(0xffff)
/** The mask which give the SYSCALL EIP. */
#define  MSR_K6_STAR_SYSCALL_EIP_MASK       UINT32_C(0xffffffff)
/** K6 WHCR - Write Handling Control Register. */
#define MSR_K6_WHCR                         UINT32_C(0xc0000082)
/** K6 UWCCR - UC/WC Cacheability Control Register. */
#define MSR_K6_UWCCR                        UINT32_C(0xc0000085)
/** K6 PSOR - Processor State Observability Register. */
#define MSR_K6_PSOR                         UINT32_C(0xc0000087)
/** K6 PFIR - Page Flush/Invalidate Register. */
#define MSR_K6_PFIR                         UINT32_C(0xc0000088)

/** Performance counter MSRs. (AMD only) */
#define MSR_K7_EVNTSEL0                     UINT32_C(0xc0010000)
#define MSR_K7_EVNTSEL1                     UINT32_C(0xc0010001)
#define MSR_K7_EVNTSEL2                     UINT32_C(0xc0010002)
#define MSR_K7_EVNTSEL3                     UINT32_C(0xc0010003)
#define MSR_K7_PERFCTR0                     UINT32_C(0xc0010004)
#define MSR_K7_PERFCTR1                     UINT32_C(0xc0010005)
#define MSR_K7_PERFCTR2                     UINT32_C(0xc0010006)
#define MSR_K7_PERFCTR3                     UINT32_C(0xc0010007)

/** K8 LSTAR - Long mode SYSCALL target (RIP). */
#define MSR_K8_LSTAR                        UINT32_C(0xc0000082)
/** K8 CSTAR - Compatibility mode SYSCALL target (RIP). */
#define MSR_K8_CSTAR                        UINT32_C(0xc0000083)
/** K8 SF_MASK - SYSCALL flag mask. (aka SFMASK) */
#define MSR_K8_SF_MASK                      UINT32_C(0xc0000084)
/** K8 FS.base - The 64-bit base FS register. */
#define MSR_K8_FS_BASE                      UINT32_C(0xc0000100)
/** K8 GS.base - The 64-bit base GS register. */
#define MSR_K8_GS_BASE                      UINT32_C(0xc0000101)
/** K8 KernelGSbase - Used with SWAPGS. */
#define MSR_K8_KERNEL_GS_BASE               UINT32_C(0xc0000102)
/** K8 TSC_AUX - Used with RDTSCP. */
#define MSR_K8_TSC_AUX                      UINT32_C(0xc0000103)
#define MSR_K8_SYSCFG                       UINT32_C(0xc0010010)
#define MSR_K8_HWCR                         UINT32_C(0xc0010015)
#define MSR_K8_IORRBASE0                    UINT32_C(0xc0010016)
#define MSR_K8_IORRMASK0                    UINT32_C(0xc0010017)
#define MSR_K8_IORRBASE1                    UINT32_C(0xc0010018)
#define MSR_K8_IORRMASK1                    UINT32_C(0xc0010019)
#define MSR_K8_TOP_MEM1                     UINT32_C(0xc001001a)
#define MSR_K8_TOP_MEM2                     UINT32_C(0xc001001d)

/** SMM MSRs. */
#define MSR_K7_SMBASE                       UINT32_C(0xc0010111)
#define MSR_K7_SMM_ADDR                     UINT32_C(0xc0010112)
#define MSR_K7_SMM_MASK                     UINT32_C(0xc0010113)

/** North bridge config? See BIOS & Kernel dev guides for
 * details. */
#define MSR_K8_NB_CFG                       UINT32_C(0xc001001f)

/** Hypertransport interrupt pending register.
 * "BIOS and Kernel Developer's Guide for AMD NPT Family 0Fh Processors" */
#define MSR_K8_INT_PENDING                  UINT32_C(0xc0010055)

/** SVM Control. */
#define MSR_K8_VM_CR                        UINT32_C(0xc0010114)
/** Disables HDT (Hardware Debug Tool) and certain internal debug
 *  features. */
#define MSR_K8_VM_CR_DPD                    RT_BIT_32(0)
/** If set, non-intercepted INIT signals are converted to \#SX
 *  exceptions. */
#define MSR_K8_VM_CR_R_INIT                 RT_BIT_32(1)
/** Disables A20 masking.  */
#define MSR_K8_VM_CR_DIS_A20M               RT_BIT_32(2)
/** Lock bit for this MSR controlling bits 3 (LOCK) and 4 (SVMDIS). */
#define MSR_K8_VM_CR_LOCK                   RT_BIT_32(3)
/** SVM disable. When set, writes to EFER.SVME are treated as MBZ. When
 *  clear, EFER.SVME can be written normally. */
#define MSR_K8_VM_CR_SVM_DISABLE            RT_BIT_32(4)

#define MSR_K8_IGNNE                        UINT32_C(0xc0010115)
#define MSR_K8_SMM_CTL                      UINT32_C(0xc0010116)
/** SVM - VM_HSAVE_PA - Physical address for saving and restoring
 *                      host state during world switch. */
#define MSR_K8_VM_HSAVE_PA                  UINT32_C(0xc0010117)

/** Virtualized speculation control for AMD processors.
 *
 * Unified interface among different CPU generations.
 * The VMM will set any architectural MSRs based on the CPU.
 * See "White Paper: AMD64 Technology Speculative Store Bypass Disable 5.21.18"
 * (12441_AMD64_SpeculativeStoreBypassDisable_Whitepaper_final.pdf) */
#define MSR_AMD_VIRT_SPEC_CTL               UINT32_C(0xc001011f)
/** Speculative Store Bypass Disable. */
# define MSR_AMD_VIRT_SPEC_CTL_F_SSBD       RT_BIT(2)

/** @} */


/** @name Page Table / Directory / Directory Pointers / L4.
 * @{
 */

#ifndef __ASSEMBLER__
/** Page table/directory  entry as an unsigned integer. */
typedef uint32_t X86PGUINT;
/** Pointer to a page table/directory table entry as an unsigned integer. */
typedef X86PGUINT *PX86PGUINT;
/** Pointer to an const page table/directory table entry as an unsigned integer. */
typedef X86PGUINT const *PCX86PGUINT;
#endif

/** Number of entries in a 32-bit PT/PD. */
#define X86_PG_ENTRIES                      1024


#ifndef __ASSEMBLER__
/** PAE page table/page directory/pdpt/l4/l5 entry as an unsigned integer. */
typedef uint64_t X86PGPAEUINT;
/** Pointer to a PAE page table/page directory/pdpt/l4/l5 entry as an unsigned integer. */
typedef X86PGPAEUINT *PX86PGPAEUINT;
/** Pointer to an const PAE page table/page directory/pdpt/l4/l5 entry as an unsigned integer. */
typedef X86PGPAEUINT const *PCX86PGPAEUINT;
#endif

/** Number of entries in a PAE PT/PD. */
#define X86_PG_PAE_ENTRIES                  512
/** Number of entries in a PAE PDPT. */
#define X86_PG_PAE_PDPE_ENTRIES             4

/** Number of entries in an AMD64 PT/PD/PDPT/L4/L5. */
#define X86_PG_AMD64_ENTRIES                X86_PG_PAE_ENTRIES
/** Number of entries in an AMD64 PDPT.
 * Just for complementing X86_PG_PAE_PDPE_ENTRIES, using X86_PG_AMD64_ENTRIES for this is fine too. */
#define X86_PG_AMD64_PDPE_ENTRIES           X86_PG_AMD64_ENTRIES

/** The size of a default page. */
#define X86_PAGE_SIZE                       X86_PAGE_4K_SIZE
/** The page shift of a default page. */
#define X86_PAGE_SHIFT                      X86_PAGE_4K_SHIFT
/** The default page offset mask. */
#define X86_PAGE_OFFSET_MASK                X86_PAGE_4K_OFFSET_MASK
/** The default page base mask for virtual addresses. */
#define X86_PAGE_BASE_MASK                  X86_PAGE_4K_BASE_MASK
/** The default page base mask for virtual addresses - 32bit version. */
#define X86_PAGE_BASE_MASK_32               X86_PAGE_4K_BASE_MASK_32

/** The size of a 4KB page. */
#define X86_PAGE_4K_SIZE                    _4K
/** The page shift of a 4KB page. */
#define X86_PAGE_4K_SHIFT                   12
/** The 4KB page offset mask. */
#define X86_PAGE_4K_OFFSET_MASK             0xfff
/** The 4KB page base mask for virtual addresses. */
#define X86_PAGE_4K_BASE_MASK               0xfffffffffffff000ULL
/** The 4KB page base mask for virtual addresses - 32bit version. */
#define X86_PAGE_4K_BASE_MASK_32            0xfffff000U

/** The size of a 2MB page. */
#define X86_PAGE_2M_SIZE                    _2M
/** The page shift of a 2MB page. */
#define X86_PAGE_2M_SHIFT                   21
/** The 2MB page offset mask. */
#define X86_PAGE_2M_OFFSET_MASK             0x001fffff
/** The 2MB page base mask for virtual addresses. */
#define X86_PAGE_2M_BASE_MASK               0xffffffffffe00000ULL
/** The 2MB page base mask for virtual addresses - 32bit version. */
#define X86_PAGE_2M_BASE_MASK_32            0xffe00000U

/** The size of a 4MB page. */
#define X86_PAGE_4M_SIZE                    _4M
/** The page shift of a 4MB page. */
#define X86_PAGE_4M_SHIFT                   22
/** The 4MB page offset mask. */
#define X86_PAGE_4M_OFFSET_MASK             0x003fffff
/** The 4MB page base mask for virtual addresses. */
#define X86_PAGE_4M_BASE_MASK               0xffffffffffc00000ULL
/** The 4MB page base mask for virtual addresses - 32bit version. */
#define X86_PAGE_4M_BASE_MASK_32            0xffc00000U

/** The size of a 1GB page. */
#define X86_PAGE_1G_SIZE                    _1G
/** The page shift of a 1GB page. */
#define X86_PAGE_1G_SHIFT                   30
/** The 1GB page offset mask. */
#define X86_PAGE_1G_OFFSET_MASK             0x3fffffff
/** The 1GB page base mask for virtual addresses. */
#define X86_PAGE_1G_BASE_MASK               UINT64_C(0xffffffffc0000000)

/**
 * Check if the given address is canonical.
 */
#define X86_IS_CANONICAL(a_u64Addr)         ((uint64_t)(a_u64Addr) + UINT64_C(0x800000000000) < UINT64_C(0x1000000000000))

/**
 * Gets the page base mask given the page shift.
 */
#define X86_GET_PAGE_BASE_MASK(a_cShift)    (UINT64_C(0xffffffffffffffff) << (a_cShift))

/**
 * Gets the page offset mask given the page shift.
 */
#define X86_GET_PAGE_OFFSET_MASK(a_cShift)  (~X86_GET_PAGE_BASE_MASK(a_cShift))


/** @name Page Table Entry
 * @{
 */
/** Bit 0 -  P  - Present bit. */
#define X86_PTE_BIT_P                       0
/** Bit 1 - R/W - Read (clear) / Write (set) bit. */
#define X86_PTE_BIT_RW                      1
/** Bit 2 - U/S - User (set) / Supervisor (clear) bit. */
#define X86_PTE_BIT_US                      2
/** Bit 3 - PWT - Page level write thru bit. */
#define X86_PTE_BIT_PWT                     3
/** Bit 4 - PCD - Page level cache disable bit. */
#define X86_PTE_BIT_PCD                     4
/** Bit 5 -  A  - Access bit. */
#define X86_PTE_BIT_A                       5
/** Bit 6 -  D  - Dirty bit. */
#define X86_PTE_BIT_D                       6
/** Bit 7 - PAT - Page Attribute Table index bit. Reserved and 0 if not supported. */
#define X86_PTE_BIT_PAT                     7
/** Bit 8 -  G  - Global flag. */
#define X86_PTE_BIT_G                       8
/** Bits 63 - NX - PAE/LM - No execution flag. */
#define X86_PTE_PAE_BIT_NX                  63

/** Bit 0 -  P  - Present bit mask. */
#define X86_PTE_P                           RT_BIT_32(0)
/** Bit 1 - R/W - Read (clear) / Write (set) bit mask. */
#define X86_PTE_RW                          RT_BIT_32(1)
/** Bit 2 - U/S - User (set) / Supervisor (clear) bit mask. */
#define X86_PTE_US                          RT_BIT_32(2)
/** Bit 3 - PWT - Page level write thru bit mask. */
#define X86_PTE_PWT                         RT_BIT_32(3)
/** Bit 4 - PCD - Page level cache disable bit mask. */
#define X86_PTE_PCD                         RT_BIT_32(4)
/** Bit 5 -  A  - Access bit mask. */
#define X86_PTE_A                           RT_BIT_32(5)
/** Bit 6 -  D  - Dirty bit mask. */
#define X86_PTE_D                           RT_BIT_32(6)
/** Bit 7 - PAT - Page Attribute Table index bit mask. Reserved and 0 if not supported. */
#define X86_PTE_PAT                         RT_BIT_32(7)
/** Bit 8 -  G  - Global bit mask. */
#define X86_PTE_G                           RT_BIT_32(8)

/** Bits 9-11 - - Available for use to system software. */
#define X86_PTE_AVL_MASK                    (RT_BIT_32(9) | RT_BIT_32(10) | RT_BIT_32(11))
/** Bits 12-31 - - Physical Page number of the next level. */
#define X86_PTE_PG_MASK                     ( 0xfffff000 )

/** Bits 12-51 - - PAE - Physical Page number of the next level. */
#define X86_PTE_PAE_PG_MASK                 UINT64_C(0x000ffffffffff000)
/** Bits 63 - NX - PAE/LM - No execution flag. */
#define X86_PTE_PAE_NX                      RT_BIT_64(63)
/** Bits 62-52 - - PAE - MBZ bits when NX is active. */
#define X86_PTE_PAE_MBZ_MASK_NX             UINT64_C(0x7ff0000000000000)
/** Bits 63-52 - - PAE - MBZ bits when no NX. */
#define X86_PTE_PAE_MBZ_MASK_NO_NX          UINT64_C(0xfff0000000000000)
/** No bits -    - LM  - MBZ bits when NX is active. */
#define X86_PTE_LM_MBZ_MASK_NX              UINT64_C(0x0000000000000000)
/** Bits 63 -    - LM  - MBZ bits when no NX. */
#define X86_PTE_LM_MBZ_MASK_NO_NX           UINT64_C(0x8000000000000000)

#ifndef __ASSEMBLER__

/**
 * Page table entry.
 */
typedef struct X86PTEBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Dirty flag.
     * Indicates that the page has been written to. */
    uint32_t    u1Dirty : 1;
    /** Reserved / If PAT enabled, bit 2 of the index.  */
    uint32_t    u1PAT : 1;
    /** Global flag. (Ignored in all but final level.) */
    uint32_t    u1Global : 1;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level. */
    uint32_t    u20PageNo : 20;
} X86PTEBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PTEBITS, 4);
# endif
/** Pointer to a page table entry. */
typedef X86PTEBITS *PX86PTEBITS;
/** Pointer to a const page table entry. */
typedef const X86PTEBITS *PCX86PTEBITS;

/**
 * Page table entry.
 */
typedef union X86PTE
{
    /** Unsigned integer view */
    X86PGUINT       u;
# ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Bit field view. */
    X86PTEBITS      n;
# endif
    /** 32-bit view. */
    uint32_t        au32[1];
    /** 16-bit view. */
    uint16_t        au16[2];
    /** 8-bit view. */
    uint8_t         au8[4];
} X86PTE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PTE, 4);
# endif
/** Pointer to a page table entry. */
typedef X86PTE *PX86PTE;
/** Pointer to a const page table entry. */
typedef const X86PTE *PCX86PTE;


/**
 * PAE page table entry.
 */
typedef struct X86PTEPAEBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor(=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Dirty flag.
     * Indicates that the page has been written to. */
    uint32_t    u1Dirty : 1;
    /** Reserved / If PAT enabled, bit 2 of the index.  */
    uint32_t    u1PAT : 1;
    /** Global flag. (Ignored in all but final level.) */
    uint32_t    u1Global : 1;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level - Low Part. Don't use this. */
    uint32_t    u20PageNoLow : 20;
    /** Physical Page number of the next level - High Part. Don't use this. */
    uint32_t    u20PageNoHigh : 20;
    /** MBZ bits */
    uint32_t    u11Reserved : 11;
    /** No Execute flag. */
    uint32_t    u1NoExecute : 1;
} X86PTEPAEBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PTEPAEBITS, 8);
# endif
/** Pointer to a page table entry. */
typedef X86PTEPAEBITS *PX86PTEPAEBITS;
/** Pointer to a page table entry. */
typedef const X86PTEPAEBITS *PCX86PTEPAEBITS;

/**
 * PAE Page table entry.
 */
typedef union X86PTEPAE
{
    /** Unsigned integer view */
    X86PGPAEUINT    u;
# ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Bit field view. */
    X86PTEPAEBITS   n;
# endif
    /** 32-bit view. */
    uint32_t        au32[2];
    /** 16-bit view. */
    uint16_t        au16[4];
    /** 8-bit view. */
    uint8_t         au8[8];
} X86PTEPAE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PTEPAE, 8);
# endif
/** Pointer to a PAE page table entry. */
typedef X86PTEPAE *PX86PTEPAE;
/** Pointer to a const PAE page table entry. */
typedef const X86PTEPAE *PCX86PTEPAE;
/** @} */

/**
 * Page table.
 */
typedef struct X86PT
{
    /** PTE Array. */
    X86PTE     a[X86_PG_ENTRIES];
} X86PT;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PT, 4096);
# endif
/** Pointer to a page table. */
typedef X86PT *PX86PT;
/** Pointer to a const page table. */
typedef const X86PT *PCX86PT;

#endif /* !__ASSEMBLER__ */

/** The page shift to get the PT index. */
#define X86_PT_SHIFT                        12
/** The PT index mask (apply to a shifted page address). */
#define X86_PT_MASK                         0x3ff


#ifndef __ASSEMBLER__
/**
 * Page directory.
 */
typedef struct X86PTPAE
{
    /** PTE Array. */
    X86PTEPAE  a[X86_PG_PAE_ENTRIES];
} X86PTPAE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PTPAE, 4096);
# endif
/** Pointer to a page table. */
typedef X86PTPAE *PX86PTPAE;
/** Pointer to a const page table. */
typedef const X86PTPAE *PCX86PTPAE;
#endif /* !__ASSEMBLY__ */

/** The page shift to get the PA PTE index. */
#define X86_PT_PAE_SHIFT                    12
/** The PAE PT index mask (apply to a shifted page address). */
#define X86_PT_PAE_MASK                     0x1ff


/** @name 4KB Page Directory Entry
 * @{
 */
/** Bit 0 -  P  - Present bit. */
#define X86_PDE_P                           RT_BIT_32(0)
/** Bit 1 - R/W - Read (clear) / Write (set) bit. */
#define X86_PDE_RW                          RT_BIT_32(1)
/** Bit 2 - U/S - User (set) / Supervisor (clear) bit. */
#define X86_PDE_US                          RT_BIT_32(2)
/** Bit 3 - PWT - Page level write thru bit. */
#define X86_PDE_PWT                         RT_BIT_32(3)
/** Bit 4 - PCD - Page level cache disable bit. */
#define X86_PDE_PCD                         RT_BIT_32(4)
/** Bit 5 -  A  - Access bit. */
#define X86_PDE_A                           RT_BIT_32(5)
/** Bit 7 - PS  - Page size attribute.
 * Clear mean 4KB pages, set means large pages (2/4MB). */
#define X86_PDE_PS                          RT_BIT_32(7)
/** Bits 9-11 - - Available for use to system software. */
#define X86_PDE_AVL_MASK                    (RT_BIT_32(9) | RT_BIT_32(10) | RT_BIT_32(11))
/** Bits 12-31 -  - Physical Page number of the next level. */
#define X86_PDE_PG_MASK                     ( 0xfffff000 )

/** Bits 12-51 - - PAE - Physical Page number of the next level. */
#define X86_PDE_PAE_PG_MASK                 UINT64_C(0x000ffffffffff000)
/** Bits 63 - NX - PAE/LM - No execution flag. */
#define X86_PDE_PAE_NX                      RT_BIT_64(63)
/** Bits 62-52, 7 - - PAE - MBZ bits when NX is active. */
#define X86_PDE_PAE_MBZ_MASK_NX             UINT64_C(0x7ff0000000000080)
/** Bits 63-52, 7 - - PAE - MBZ bits when no NX. */
#define X86_PDE_PAE_MBZ_MASK_NO_NX          UINT64_C(0xfff0000000000080)
/** Bit 7 -         - LM  - MBZ bits when NX is active. */
#define X86_PDE_LM_MBZ_MASK_NX              UINT64_C(0x0000000000000080)
/** Bits 63, 7 -    - LM  - MBZ bits when no NX. */
#define X86_PDE_LM_MBZ_MASK_NO_NX           UINT64_C(0x8000000000000080)

#ifndef __ASSEMBLER__

/**
 * Page directory entry.
 */
typedef struct X86PDEBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page has been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Reserved / Ignored (dirty bit). */
    uint32_t    u1Reserved0 : 1;
    /** Size bit if PSE is enabled - in any event it's 0. */
    uint32_t    u1Size : 1;
    /** Reserved / Ignored (global bit). */
    uint32_t    u1Reserved1 : 1;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level. */
    uint32_t    u20PageNo : 20;
} X86PDEBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDEBITS, 4);
# endif
/** Pointer to a page directory entry. */
typedef X86PDEBITS *PX86PDEBITS;
/** Pointer to a const page directory entry. */
typedef const X86PDEBITS *PCX86PDEBITS;


/**
 * PAE page directory entry.
 */
typedef struct X86PDEPAEBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page has been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Reserved / Ignored (dirty bit). */
    uint32_t    u1Reserved0 : 1;
    /** Size bit if PSE is enabled - in any event it's 0. */
    uint32_t    u1Size : 1;
    /** Reserved / Ignored (global bit). /  */
    uint32_t    u1Reserved1 : 1;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level - Low Part. Don't use! */
    uint32_t    u20PageNoLow : 20;
    /** Physical Page number of the next level - High Part. Don't use! */
    uint32_t    u20PageNoHigh : 20;
    /** MBZ bits */
    uint32_t    u11Reserved : 11;
    /** No Execute flag. */
    uint32_t    u1NoExecute : 1;
} X86PDEPAEBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDEPAEBITS, 8);
# endif
/** Pointer to a page directory entry. */
typedef X86PDEPAEBITS *PX86PDEPAEBITS;
/** Pointer to a const page directory entry. */
typedef const X86PDEPAEBITS *PCX86PDEPAEBITS;

#endif /* !__ASSEMBLER__ */

/** @} */


/** @name 2/4MB Page Directory Entry
 * @{
 */
/** Bit 0 -  P  - Present bit. */
#define X86_PDE4M_P                         RT_BIT_32(0)
/** Bit 1 - R/W - Read (clear) / Write (set) bit. */
#define X86_PDE4M_RW                        RT_BIT_32(1)
/** Bit 2 - U/S - User (set) / Supervisor (clear) bit. */
#define X86_PDE4M_US                        RT_BIT_32(2)
/** Bit 3 - PWT - Page level write thru bit. */
#define X86_PDE4M_PWT                       RT_BIT_32(3)
/** Bit 4 - PCD - Page level cache disable bit. */
#define X86_PDE4M_PCD                       RT_BIT_32(4)
/** Bit 5 -  A  - Access bit. */
#define X86_PDE4M_A                         RT_BIT_32(5)
/** Bit 6 -  D  - Dirty bit. */
#define X86_PDE4M_D                         RT_BIT_32(6)
/** Bit 7 - PS  - Page size attribute. Clear mean 4KB pages, set means large pages (2/4MB). */
#define X86_PDE4M_PS                        RT_BIT_32(7)
/** Bit 8 -  G  - Global flag. */
#define X86_PDE4M_G                         RT_BIT_32(8)
/** Bits 9-11 - AVL - Available for use to system software. */
#define X86_PDE4M_AVL                       (RT_BIT_32(9) | RT_BIT_32(10) | RT_BIT_32(11))
/** Bit 12 - PAT - Page Attribute Table index bit. Reserved and 0 if not supported. */
#define X86_PDE4M_PAT                       RT_BIT_32(12)
/** Shift to get from X86_PTE_PAT to X86_PDE4M_PAT. */
#define X86_PDE4M_PAT_SHIFT                 (12 - 7)
/** Bits 22-31 - - Physical Page number. */
#define X86_PDE4M_PG_MASK                   ( 0xffc00000 )
/** Bits 20-13 - - Physical Page number high part (32-39 bits). AMD64 hack. */
#define X86_PDE4M_PG_HIGH_MASK              ( 0x001fe000 )
/** The number of bits to the high part of the page number. */
#define X86_PDE4M_PG_HIGH_SHIFT             19
/** Bit 21 -     - MBZ bits for AMD CPUs, no PSE36. */
#define X86_PDE4M_MBZ_MASK                  RT_BIT_32(21)

/** Bits 21-51 - - PAE/LM - Physical Page number.
 * (Bits 40-51 (long mode) & bits 36-51 (pae legacy) are reserved according to the Intel docs; AMD allows for more.) */
#define X86_PDE2M_PAE_PG_MASK               UINT64_C(0x000fffffffe00000)
/** Bits 63 - NX - PAE/LM - No execution flag. */
#define X86_PDE2M_PAE_NX                    RT_BIT_64(63)
/** Bits 62-52, 20-13 - - PAE - MBZ bits when NX is active. */
#define X86_PDE2M_PAE_MBZ_MASK_NX           UINT64_C(0x7ff00000001fe000)
/** Bits 63-52, 20-13 - - PAE - MBZ bits when no NX. */
#define X86_PDE2M_PAE_MBZ_MASK_NO_NX        UINT64_C(0xfff00000001fe000)
/** Bits 20-13        - - LM  - MBZ bits when NX is active. */
#define X86_PDE2M_LM_MBZ_MASK_NX            UINT64_C(0x00000000001fe000)
/** Bits 63, 20-13    - - LM  - MBZ bits when no NX. */
#define X86_PDE2M_LM_MBZ_MASK_NO_NX         UINT64_C(0x80000000001fe000)

#ifndef __ASSEMBLER__

/**
 * 4MB page directory entry.
 */
typedef struct X86PDE4MBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Dirty flag.
     * Indicates that the page has been written to. */
    uint32_t    u1Dirty : 1;
    /** Page size flag - always 1 for 4MB entries. */
    uint32_t    u1Size : 1;
    /** Global flag.  */
    uint32_t    u1Global : 1;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Reserved / If PAT enabled, bit 2 of the index.  */
    uint32_t    u1PAT : 1;
    /** Bits 32-39 of the page number on AMD64.
     * This AMD64 hack allows accessing 40bits of physical memory without PAE. */
    uint32_t    u8PageNoHigh : 8;
    /** Reserved. */
    uint32_t    u1Reserved : 1;
    /** Physical Page number of the page. */
    uint32_t    u10PageNo : 10;
} X86PDE4MBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDE4MBITS, 4);
# endif
/** Pointer to a page table entry. */
typedef X86PDE4MBITS *PX86PDE4MBITS;
/** Pointer to a const page table entry. */
typedef const X86PDE4MBITS *PCX86PDE4MBITS;


/**
 * 2MB PAE page directory entry.
 */
typedef struct X86PDE2MPAEBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor(=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Dirty flag.
     * Indicates that the page has been written to. */
    uint32_t    u1Dirty : 1;
    /** Page size flag - always 1 for 2MB entries. */
    uint32_t    u1Size : 1;
    /** Global flag.  */
    uint32_t    u1Global : 1;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Reserved / If PAT enabled, bit 2 of the index.  */
    uint32_t    u1PAT : 1;
    /** Reserved. */
    uint32_t    u9Reserved : 9;
    /** Physical Page number of the next level - Low part. Don't use! */
    uint32_t    u10PageNoLow : 10;
    /** Physical Page number of the next level - High part. Don't use! */
    uint32_t    u20PageNoHigh : 20;
    /** MBZ bits */
    uint32_t    u11Reserved : 11;
    /** No Execute flag. */
    uint32_t    u1NoExecute : 1;
} X86PDE2MPAEBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDE2MPAEBITS, 8);
# endif
/** Pointer to a 2MB PAE page table entry. */
typedef X86PDE2MPAEBITS *PX86PDE2MPAEBITS;
/** Pointer to a 2MB PAE page table entry. */
typedef const X86PDE2MPAEBITS *PCX86PDE2MPAEBITS;

#endif /* !__ASSEMBLER__ */

/** @} */

#ifndef __ASSEMBLER__

/**
 * Page directory entry.
 */
typedef union X86PDE
{
    /** Unsigned integer view. */
    X86PGUINT       u;
# ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    X86PDEBITS      n;
    /** 4MB view (big). */
    X86PDE4MBITS    b;
# endif
    /** 8 bit unsigned integer view. */
    uint8_t         au8[4];
    /** 16 bit unsigned integer view. */
    uint16_t        au16[2];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[1];
} X86PDE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDE, 4);
# endif
/** Pointer to a page directory entry. */
typedef X86PDE *PX86PDE;
/** Pointer to a const page directory entry. */
typedef const X86PDE *PCX86PDE;

/**
 * PAE page directory entry.
 */
typedef union X86PDEPAE
{
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
# ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    X86PDEPAEBITS   n;
    /** 2MB page view (big). */
    X86PDE2MPAEBITS b;
# endif
    /** 8 bit unsigned integer view. */
    uint8_t         au8[8];
    /** 16 bit unsigned integer view. */
    uint16_t        au16[4];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} X86PDEPAE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDEPAE, 8);
# endif
/** Pointer to a page directory entry. */
typedef X86PDEPAE *PX86PDEPAE;
/** Pointer to a const page directory entry. */
typedef const X86PDEPAE *PCX86PDEPAE;

/**
 * Page directory.
 */
typedef struct X86PD
{
    /** PDE Array. */
    X86PDE      a[X86_PG_ENTRIES];
} X86PD;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PD, 4096);
# endif
/** Pointer to a page directory. */
typedef X86PD *PX86PD;
/** Pointer to a const page directory. */
typedef const X86PD *PCX86PD;

#endif /* !__ASSEMBLER__ */

/** The page shift to get the PD index. */
#define X86_PD_SHIFT                        22
/** The PD index mask (apply to a shifted page address). */
#define X86_PD_MASK                         0x3ff


#ifndef __ASSEMBLER__
/**
 * PAE page directory.
 */
typedef struct X86PDPAE
{
    /** PDE Array. */
    X86PDEPAE   a[X86_PG_PAE_ENTRIES];
} X86PDPAE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDPAE, 4096);
# endif
/** Pointer to a PAE page directory. */
typedef X86PDPAE *PX86PDPAE;
/** Pointer to a const PAE page directory. */
typedef const X86PDPAE *PCX86PDPAE;
#endif /* !__ASSEMBLER__ */

/** The page shift to get the PAE PD index. */
#define X86_PD_PAE_SHIFT                    21
/** The PAE PD index mask (apply to a shifted page address). */
#define X86_PD_PAE_MASK                     0x1ff


/** @name Page Directory Pointer Table Entry (PAE)
 * @{
 */
/** Bit 0 -  P  - Present bit. */
#define X86_PDPE_P                          RT_BIT_32(0)
/** Bit 1 - R/W - Read (clear) / Write (set) bit. Long Mode only. */
#define X86_PDPE_RW                         RT_BIT_32(1)
/** Bit 2 - U/S - User (set) / Supervisor (clear) bit. Long Mode only. */
#define X86_PDPE_US                         RT_BIT_32(2)
/** Bit 3 - PWT - Page level write thru bit. */
#define X86_PDPE_PWT                        RT_BIT_32(3)
/** Bit 4 - PCD - Page level cache disable bit. */
#define X86_PDPE_PCD                        RT_BIT_32(4)
/** Bit 5 -  A  - Access bit. Long Mode only. */
#define X86_PDPE_A                          RT_BIT_32(5)
/** Bit 7 - PS  - Page size (1GB). Long Mode only. */
#define X86_PDPE_LM_PS                      RT_BIT_32(7)
/** Bits 9-11 - - Available for use to system software. */
#define X86_PDPE_AVL_MASK                   (RT_BIT_32(9) | RT_BIT_32(10) | RT_BIT_32(11))
/** Bits 12-51 - - PAE - Physical Page number of the next level. */
#define X86_PDPE_PG_MASK                    UINT64_C(0x000ffffffffff000)
/** Bits 30-51 - - PG - Physical address of the 1GB page referenced by this entry. */
#define X86_PDPE1G_PG_MASK                  UINT64_C(0x000fffffc0000000)
/** Bits 63-52, 8-5, 2-1 - - PAE - MBZ bits (NX is long mode only). */
#define X86_PDPE_PAE_MBZ_MASK               UINT64_C(0xfff00000000001e6)
/** Bits 63 - NX - LM - No execution flag. Long Mode only. */
#define X86_PDPE_LM_NX                      RT_BIT_64(63)
/** Bits 8, 7 - - LM - MBZ bits when NX is active. */
#define X86_PDPE_LM_MBZ_MASK_NX             UINT64_C(0x0000000000000180)
/** Bits 63, 8, 7 - - LM - MBZ bits when no NX. */
#define X86_PDPE_LM_MBZ_MASK_NO_NX          UINT64_C(0x8000000000000180)
/** Bits 29-13 - - LM - MBZ bits for 1GB page entry when NX is active. */
#define X86_PDPE1G_LM_MBZ_MASK_NX           UINT64_C(0x000000003fffe000)
/** Bits 63, 29-13 - - LM - MBZ bits for 1GB page entry when no NX. */
#define X86_PDPE1G_LM_MBZ_MASK_NO_NX        UINT64_C(0x800000003fffe000)

#ifndef __ASSEMBLER__

/**
 * Page directory pointer table entry.
 */
typedef struct X86PDPEBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Chunk of reserved bits. */
    uint32_t    u2Reserved : 2;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Chunk of reserved bits. */
    uint32_t    u4Reserved : 4;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level - Low Part. Don't use! */
    uint32_t    u20PageNoLow : 20;
    /** Physical Page number of the next level - High Part. Don't use! */
    uint32_t    u20PageNoHigh : 20;
    /** MBZ bits */
    uint32_t    u12Reserved : 12;
} X86PDPEBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDPEBITS, 8);
# endif
/** Pointer to a page directory pointer table entry. */
typedef X86PDPEBITS *PX86PTPEBITS;
/** Pointer to a const page directory pointer table entry. */
typedef const X86PDPEBITS *PCX86PTPEBITS;

/**
 * Page directory pointer table entry. AMD64 version
 */
typedef struct X86PDPEAMD64BITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Chunk of reserved bits. */
    uint32_t    u3Reserved : 3;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level - Low Part. Don't use! */
    uint32_t    u20PageNoLow : 20;
    /** Physical Page number of the next level - High Part. Don't use! */
    uint32_t    u20PageNoHigh : 20;
    /** MBZ bits */
    uint32_t    u11Reserved : 11;
    /** No Execute flag. */
    uint32_t    u1NoExecute : 1;
} X86PDPEAMD64BITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDPEAMD64BITS, 8);
# endif
/** Pointer to a page directory pointer table entry. */
typedef X86PDPEAMD64BITS *PX86PDPEAMD64BITS;
/** Pointer to a const page directory pointer table entry. */
typedef const X86PDPEAMD64BITS *PCX86PDPEAMD64BITS;

/**
 * Page directory pointer table entry for 1GB page. (AMD64 only)
 */
typedef struct X86PDPE1GB
{
    /** 0: Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** 1: Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** 2: User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** 3: Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** 4: Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** 5: Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** 6: Dirty flag for 1GB pages.  */
    uint32_t    u1Dirty : 1;
    /** 7: Indicates 1GB page if set. */
    uint32_t    u1Size : 1;
    /** 8: Global 1GB page. */
    uint32_t    u1Global: 1;
    /** 9-11: Available for use to system software. */
    uint32_t    u3Available : 3;
    /** 12: PAT bit for 1GB page. */
    uint32_t    u1PAT : 1;
    /** 13-29: MBZ bits. */
    uint32_t    u17Reserved : 17;
    /** 30-31: Physical page number - Low Part. Don't use! */
    uint32_t    u2PageNoLow : 2;
    /** 32-51: Physical Page number of the next level - High Part. Don't use! */
    uint32_t    u20PageNoHigh : 20;
    /** 52-62: MBZ bits */
    uint32_t    u11Reserved : 11;
    /** 63: No Execute flag. */
    uint32_t    u1NoExecute : 1;
} X86PDPE1GB;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDPE1GB, 8);
# endif
/** Pointer to a page directory pointer table entry for a 1GB page. */
typedef X86PDPE1GB *PX86PDPE1GB;
/** Pointer to a const page directory pointer table entry for a 1GB page. */
typedef const X86PDPE1GB *PCX86PDPE1GB;

/**
 * Page directory pointer table entry.
 */
typedef union X86PDPE
{
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
# ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    X86PDPEBITS     n;
    /** AMD64 view. */
    X86PDPEAMD64BITS lm;
    /** AMD64 big view. */
    X86PDPE1GB      b;
# endif
    /** 8 bit unsigned integer view. */
    uint8_t         au8[8];
    /** 16 bit unsigned integer view. */
    uint16_t        au16[4];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} X86PDPE;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDPE, 8);
# endif
/** Pointer to a page directory pointer table entry. */
typedef X86PDPE *PX86PDPE;
/** Pointer to a const page directory pointer table entry. */
typedef const X86PDPE *PCX86PDPE;


/**
 * Page directory pointer table.
 */
typedef struct X86PDPT
{
    /** PDE Array. */
    X86PDPE         a[X86_PG_AMD64_PDPE_ENTRIES];
} X86PDPT;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PDPT, 4096);
# endif
/** Pointer to a page directory pointer table. */
typedef X86PDPT *PX86PDPT;
/** Pointer to a const page directory pointer table. */
typedef const X86PDPT *PCX86PDPT;

#endif /* !__ASSEMBLER__ */

/** The page shift to get the PDPT index. */
#define X86_PDPT_SHIFT             30
/** The PDPT index mask (apply to a shifted page address). (32 bits PAE) */
#define X86_PDPT_MASK_PAE          0x3
/** The PDPT index mask (apply to a shifted page address). (64 bits PAE)*/
#define X86_PDPT_MASK_AMD64        0x1ff

/** @} */


/** @name Page Map Level-4 Entry (Long Mode PAE)
 * @{
 */
/** Bit 0 -  P  - Present bit. */
#define X86_PML4E_P                         RT_BIT_32(0)
/** Bit 1 - R/W - Read (clear) / Write (set) bit. */
#define X86_PML4E_RW                        RT_BIT_32(1)
/** Bit 2 - U/S - User (set) / Supervisor (clear) bit. */
#define X86_PML4E_US                        RT_BIT_32(2)
/** Bit 3 - PWT - Page level write thru bit. */
#define X86_PML4E_PWT                       RT_BIT_32(3)
/** Bit 4 - PCD - Page level cache disable bit. */
#define X86_PML4E_PCD                       RT_BIT_32(4)
/** Bit 5 -  A  - Access bit. */
#define X86_PML4E_A                         RT_BIT_32(5)
/** Bits 9-11 - - Available for use to system software. */
#define X86_PML4E_AVL_MASK                  (RT_BIT_32(9) | RT_BIT_32(10) | RT_BIT_32(11))
/** Bits 12-51 - - PAE - Physical Page number of the next level. */
#define X86_PML4E_PG_MASK                   UINT64_C(0x000ffffffffff000)
/** Bits 8, 7 - - MBZ bits when NX is active. */
#define X86_PML4E_MBZ_MASK_NX               UINT64_C(0x0000000000000080)
/** Bits 63, 7 - - MBZ bits when no NX. */
#define X86_PML4E_MBZ_MASK_NO_NX            UINT64_C(0x8000000000000080)
/** Bits 63 - NX - PAE - No execution flag. */
#define X86_PML4E_NX                        RT_BIT_64(63)

#ifndef __ASSEMBLER__

/**
 * Page Map Level-4 Entry
 */
typedef struct X86PML4EBITS
{
    /** Flags whether(=1) or not the page is present. */
    uint32_t    u1Present : 1;
    /** Read(=0) / Write(=1) flag. */
    uint32_t    u1Write : 1;
    /** User(=1) / Supervisor (=0) flag. */
    uint32_t    u1User : 1;
    /** Write Thru flag. If PAT enabled, bit 0 of the index. */
    uint32_t    u1WriteThru : 1;
    /** Cache disabled flag. If PAT enabled, bit 1 of the index. */
    uint32_t    u1CacheDisable : 1;
    /** Accessed flag.
     * Indicates that the page have been read or written to. */
    uint32_t    u1Accessed : 1;
    /** Chunk of reserved bits. */
    uint32_t    u3Reserved : 3;
    /** Available for use to system software. */
    uint32_t    u3Available : 3;
    /** Physical Page number of the next level - Low Part. Don't use! */
    uint32_t    u20PageNoLow : 20;
    /** Physical Page number of the next level - High Part. Don't use! */
    uint32_t    u20PageNoHigh : 20;
    /** MBZ bits */
    uint32_t    u11Reserved : 11;
    /** No Execute flag. */
    uint32_t    u1NoExecute : 1;
} X86PML4EBITS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PML4EBITS, 8);
# endif
/** Pointer to a page map level-4 entry. */
typedef X86PML4EBITS *PX86PML4EBITS;
/** Pointer to a const page map level-4 entry. */
typedef const X86PML4EBITS *PCX86PML4EBITS;

/**
 * Page Map Level-4 Entry.
 */
typedef union X86PML4E
{
    /** Unsigned integer view. */
    X86PGPAEUINT    u;
# ifndef VBOX_WITHOUT_PAGING_BIT_FIELDS
    /** Normal view. */
    X86PML4EBITS    n;
# endif
    /** 8 bit unsigned integer view. */
    uint8_t         au8[8];
    /** 16 bit unsigned integer view. */
    uint16_t        au16[4];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
} X86PML4E;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PML4E, 8);
# endif
/** Pointer to a page map level-4 entry. */
typedef X86PML4E *PX86PML4E;
/** Pointer to a const page map level-4 entry. */
typedef const X86PML4E *PCX86PML4E;


/**
 * Page Map Level-4.
 */
typedef struct X86PML4
{
    /** PDE Array. */
    X86PML4E        a[X86_PG_PAE_ENTRIES];
} X86PML4;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86PML4, 4096);
# endif
/** Pointer to a page map level-4. */
typedef X86PML4 *PX86PML4;
/** Pointer to a const page map level-4. */
typedef const X86PML4 *PCX86PML4;

#endif /* !__ASSEMBLER__ */

/** The page shift to get the PML4 index. */
#define X86_PML4_SHIFT              39
/** The PML4 index mask (apply to a shifted page address). */
#define X86_PML4_MASK               0x1ff

/** @} */

/** @} */

/**
 * Intel PCID invalidation types.
 */
/** Individual address invalidation. */
#define X86_INVPCID_TYPE_INDV_ADDR                  0
/** Single-context invalidation. */
#define X86_INVPCID_TYPE_SINGLE_CONTEXT             1
/** All-context including globals invalidation. */
#define X86_INVPCID_TYPE_ALL_CONTEXT_INCL_GLOBAL    2
/** All-context excluding globals invalidation. */
#define X86_INVPCID_TYPE_ALL_CONTEXT_EXCL_GLOBAL    3
/** The maximum valid invalidation type value.   */
#define X86_INVPCID_TYPE_MAX_VALID                  X86_INVPCID_TYPE_ALL_CONTEXT_EXCL_GLOBAL


/** @name Special FPU integer values.
 * @{ */
#define X86_FPU_INT64_INDEFINITE    INT64_MIN
#define X86_FPU_INT32_INDEFINITE    INT32_MIN
#define X86_FPU_INT16_INDEFINITE    INT16_MIN
/** @} */

#ifndef __ASSEMBLER__

/**
 * 32-bit protected mode FSTENV image.
 */
typedef struct X86FSTENV32P
{
    uint16_t    FCW;            /**< 0x00 */
    uint16_t    padding1;       /**< 0x02 */
    uint16_t    FSW;            /**< 0x04 */
    uint16_t    padding2;       /**< 0x06 */
    uint16_t    FTW;            /**< 0x08 */
    uint16_t    padding3;       /**< 0x0a */
    uint32_t    FPUIP;          /**< 0x0c */
    uint16_t    FPUCS;          /**< 0x10 */
    uint16_t    FOP;            /**< 0x12 */
    uint32_t    FPUDP;          /**< 0x14 */
    uint16_t    FPUDS;          /**< 0x18 */
    uint16_t    padding4;       /**< 0x1a */
} X86FSTENV32P;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86FSTENV32P, 0x1c);
# endif
/** Pointer to a 32-bit protected mode FSTENV image. */
typedef X86FSTENV32P *PX86FSTENV32P;
/** Pointer to a const 32-bit protected mode FSTENV image. */
typedef X86FSTENV32P const *PCX86FSTENV32P;


/**
 * 80-bit MMX/FPU register type.
 */
typedef struct X86FPUMMX
{
    uint8_t reg[10];
} X86FPUMMX;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86FPUMMX, 10);
# endif
/** Pointer to a 80-bit MMX/FPU register type. */
typedef X86FPUMMX *PX86FPUMMX;
/** Pointer to a const 80-bit MMX/FPU register type. */
typedef const X86FPUMMX *PCX86FPUMMX;

/** FPU (x87) register. */
typedef union X86FPUREG
{
    /** MMX view. */
    uint64_t    mmx;
    /** FPU view - todo. */
    X86FPUMMX   fpu;
    /** Extended precision floating point view. */
    RTFLOAT80U  r80;
    /** Extended precision floating point view v2 */
    RTFLOAT80U2 r80Ex;
    /** 8-bit view. */
    uint8_t     au8[16];
    /** 16-bit view. */
    uint16_t    au16[8];
    /** 32-bit view. */
    uint32_t    au32[4];
    /** 64-bit view. */
    uint64_t    au64[2];
    /** 128-bit view. (yeah, very helpful) */
    uint128_t   au128[1];
} X86FPUREG;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86FPUREG, 16);
# endif
/** Pointer to a FPU register. */
typedef X86FPUREG *PX86FPUREG;
/** Pointer to a const FPU register. */
typedef X86FPUREG const *PCX86FPUREG;

/** FPU (x87) register - v2 with correct size. */
# pragma pack(1)
typedef union X86FPUREG2
{
    /** MMX view. */
    uint64_t    mmx;
    /** FPU view - todo. */
    X86FPUMMX   fpu;
    /** Extended precision floating point view. */
    RTFLOAT80U  r80;
    /** 8-bit view. */
    uint8_t     au8[10];
    /** 16-bit view. */
    uint16_t    au16[5];
    /** 32-bit view. */
    uint32_t    au32[2];
    /** 64-bit view. */
    uint64_t    au64[1];
} X86FPUREG2;
# pragma pack()
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86FPUREG2, 10);
# endif
/** Pointer to a FPU register - v2. */
typedef X86FPUREG2 *PX86FPUREG2;
/** Pointer to a const FPU register - v2. */
typedef X86FPUREG2 const *PCX86FPUREG2;

/**
 * XMM register union.
 */
typedef union X86XMMREG
{
    /** XMM Register view. */
    uint128_t   xmm;
    /** 8-bit view. */
    uint8_t     au8[16];
    /** 16-bit view. */
    uint16_t    au16[8];
    /** 32-bit view. */
    uint32_t    au32[4];
    /** 64-bit view. */
    uint64_t    au64[2];
    /** Signed 8-bit view. */
    int8_t      ai8[16];
    /** Signed 16-bit view. */
    int16_t     ai16[8];
    /** Signed 32-bit view. */
    int32_t     ai32[4];
    /** Signed 64-bit view. */
    int64_t     ai64[2];
    /** 128-bit view. (yeah, very helpful) */
    uint128_t   au128[1];
    /** Single precision floating point view. */
    RTFLOAT32U  ar32[4];
    /** Double precision floating point view. */
    RTFLOAT64U  ar64[2];
# ifndef VBOX_FOR_DTRACE_LIB
    /** Confusing nested 128-bit union view (this is what xmm should've been). */
    RTUINT128U  uXmm;
# endif
} X86XMMREG;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XMMREG, 16);
# endif
/** Pointer to an XMM register state. */
typedef X86XMMREG *PX86XMMREG;
/** Pointer to a const XMM register state. */
typedef X86XMMREG const *PCX86XMMREG;

/**
 * YMM register union.
 */
typedef union X86YMMREG
{
    /** YMM register view. */
    RTUINT256U  ymm;
    /** 8-bit view. */
    uint8_t     au8[32];
    /** 16-bit view. */
    uint16_t    au16[16];
    /** 32-bit view. */
    uint32_t    au32[8];
    /** 64-bit view. */
    uint64_t    au64[4];
    /** Signed 8-bit view. */
    int8_t      ai8[32];
    /** Signed 16-bit view. */
    int16_t     ai16[16];
    /** Signed 32-bit view. */
    int32_t     ai32[8];
    /** Signed 64-bit view. */
    int64_t     ai64[4];
    /** 128-bit view. (yeah, very helpful) */
    uint128_t   au128[2];
    /** Single precision floating point view. */
    RTFLOAT32U  ar32[8];
    /** Double precision floating point view. */
    RTFLOAT64U  ar64[4];
    /** XMM sub register view. */
    X86XMMREG   aXmm[2];
} X86YMMREG;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86YMMREG, 32);
# endif
/** Pointer to an YMM register state. */
typedef X86YMMREG *PX86YMMREG;
/** Pointer to a const YMM register state. */
typedef X86YMMREG const *PCX86YMMREG;

/**
 * ZMM register union.
 */
typedef union X86ZMMREG
{
    /** 8-bit view. */
    uint8_t     au8[64];
    /** 16-bit view. */
    uint16_t    au16[32];
    /** 32-bit view. */
    uint32_t    au32[16];
    /** 64-bit view. */
    uint64_t    au64[8];
    /** Signed 8-bit view. */
    int8_t      ai8[64];
    /** Signed 16-bit view. */
    int16_t     ai16[32];
    /** Signed 32-bit view. */
    int32_t     ai32[16];
    /** Signed 64-bit view. */
    int64_t     ai64[8];
    /** 128-bit view. (yeah, very helpful) */
    uint128_t   au128[4];
    /** Single precision floating point view. */
    RTFLOAT32U  ar32[16];
    /** Double precision floating point view. */
    RTFLOAT64U  ar64[8];
    /** XMM sub register view. */
    X86XMMREG   aXmm[4];
    /** YMM sub register view. */
    X86YMMREG   aYmm[2];
} X86ZMMREG;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86ZMMREG, 64);
# endif
/** Pointer to an ZMM register state. */
typedef X86ZMMREG *PX86ZMMREG;
/** Pointer to a const ZMM register state. */
typedef X86ZMMREG const *PCX86ZMMREG;


/**
 * 32-bit FPU state (aka FSAVE/FRSTOR Memory Region).
 */
# pragma pack(1)
typedef struct X86FPUSTATE
{
    /** 0x00 - Control word. */
    uint16_t    FCW;
    /** 0x02 - Alignment word */
    uint16_t    Dummy1;
    /** 0x04 - Status word. */
    uint16_t    FSW;
    /** 0x06 - Alignment word */
    uint16_t    Dummy2;
    /** 0x08 - Tag word */
    uint16_t    FTW;
    /** 0x0a - Alignment word */
    uint16_t    Dummy3;

    /** 0x0c - Instruction pointer. */
    uint32_t    FPUIP;
    /** 0x10 - Code selector. */
    uint16_t    CS;
    /** 0x12 - Opcode. */
    uint16_t    FOP;
    /** 0x14 - Data pointer. */
    uint32_t    FPUOO;
    /** 0x18 - FOS. */
    uint16_t    FPUOS;
    /** 0x0a - Alignment word */
    uint16_t    Dummy4;
    /** 0x1c - FPU register. */
    X86FPUREG2  regs[8];
} X86FPUSTATE;
# pragma pack()
AssertCompileSize(X86FPUSTATE, 108);
/** Pointer to a FPU state. */
typedef X86FPUSTATE  *PX86FPUSTATE;
/** Pointer to a const FPU state. */
typedef const X86FPUSTATE  *PCX86FPUSTATE;

/**
 * FPU Extended state (aka FXSAVE/FXRSTORE Memory Region).
 */
# pragma pack(1)
typedef struct X86FXSTATE
{
    /** 0x00 - Control word. */
    uint16_t    FCW;
    /** 0x02 - Status word. */
    uint16_t    FSW;
    /** 0x04 - Tag word. (The upper byte is always zero.) */
    uint16_t    FTW;
    /** 0x06 - Opcode. */
    uint16_t    FOP;
    /** 0x08 - Instruction pointer. */
    uint32_t    FPUIP;
    /** 0x0c - Code selector. */
    uint16_t    CS;
    uint16_t    Rsrvd1;
    /** 0x10 - Data pointer. */
    uint32_t    FPUDP;
    /** 0x14 - Data segment */
    uint16_t    DS;
    /** 0x16 */
    uint16_t    Rsrvd2;
    /** 0x18 */
    uint32_t    MXCSR;
    /** 0x1c */
    uint32_t    MXCSR_MASK;
    /** 0x20 - FPU registers. */
    X86FPUREG   aRegs[8];
    /** 0xA0 - XMM registers - 8 registers in 32 bits mode, 16 in long mode. */
    X86XMMREG   aXMM[16];
    /* - offset 416 - */
    uint32_t    au32RsrvdRest[(464 - 416) / sizeof(uint32_t)];
    /* - offset 464 - Software usable reserved bits. */
    uint32_t    au32RsrvdForSoftware[(512 - 464) / sizeof(uint32_t)];
} X86FXSTATE;
# pragma pack()
/** Pointer to a FPU Extended state. */
typedef X86FXSTATE *PX86FXSTATE;
/** Pointer to a const FPU Extended state. */
typedef const X86FXSTATE *PCX86FXSTATE;

#endif /* !__ASSEMBLER__ */


/** Offset for software usable reserved bits (464:511) where we store a 32-bit
 *  magic. Don't forget to update x86.mac if you change this! */
#define X86_OFF_FXSTATE_RSVD            0x1d0
/** The 32-bit magic used to recognize if this a 32-bit FPU state. Don't
 *  forget to update x86.mac if you change this!
 * @todo r=bird: This has nothing what-so-ever to do here.... */
#define X86_FXSTATE_RSVD_32BIT_MAGIC    0x32b3232b
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86FXSTATE, 512);
AssertCompileMemberOffset(X86FXSTATE, au32RsrvdForSoftware, X86_OFF_FXSTATE_RSVD);
#endif

/** @name FPU status word flags.
 * @{ */
/** Exception Flag: Invalid operation.  */
#define X86_FSW_IE          RT_BIT_32(0)
#define X86_FSW_IE_BIT      0
/** Exception Flag: Denormalized operand.  */
#define X86_FSW_DE          RT_BIT_32(1)
#define X86_FSW_DE_BIT      1
/** Exception Flag: Zero divide.  */
#define X86_FSW_ZE          RT_BIT_32(2)
#define X86_FSW_ZE_BIT      2
/** Exception Flag: Overflow.  */
#define X86_FSW_OE          RT_BIT_32(3)
#define X86_FSW_OE_BIT      3
/** Exception Flag: Underflow.  */
#define X86_FSW_UE          RT_BIT_32(4)
#define X86_FSW_UE_BIT      4
/** Exception Flag: Precision.  */
#define X86_FSW_PE          RT_BIT_32(5)
#define X86_FSW_PE_BIT      5
/** Stack fault. */
#define X86_FSW_SF          RT_BIT_32(6)
#define X86_FSW_SF_BIT      6
/** Error summary status. */
#define X86_FSW_ES          RT_BIT_32(7)
#define X86_FSW_ES_BIT      7
/** Mask of exceptions flags, excluding the summary bit. */
#define X86_FSW_XCPT_MASK   UINT16_C(0x007f)
/** Mask of exceptions flags, including the summary bit. */
#define X86_FSW_XCPT_ES_MASK UINT16_C(0x00ff)
/** Condition code 0. */
#define X86_FSW_C0          RT_BIT_32(X86_FSW_C0_BIT)
#define X86_FSW_C0_BIT      8
/** Condition code 1. */
#define X86_FSW_C1          RT_BIT_32(X86_FSW_C1_BIT)
#define X86_FSW_C1_BIT      9
/** Condition code 2. */
#define X86_FSW_C2          RT_BIT_32(X86_FSW_C2_BIT)
#define X86_FSW_C2_BIT      10
/** Top of the stack mask. */
#define X86_FSW_TOP_MASK    UINT16_C(0x3800)
/** TOP shift value. */
#define X86_FSW_TOP_SHIFT   11
/** Mask for getting TOP value after shifting it right. */
#define X86_FSW_TOP_SMASK   UINT16_C(0x0007)
/** Get the TOP value. */
#define X86_FSW_TOP_GET(a_uFsw) (((a_uFsw) >> X86_FSW_TOP_SHIFT) & X86_FSW_TOP_SMASK)
/** Get the TOP value offsetted by a_iSt (0-7). */
#define X86_FSW_TOP_GET_ST(a_uFsw, a_iSt) ((((a_uFsw) >> X86_FSW_TOP_SHIFT) + (a_iSt)) & X86_FSW_TOP_SMASK)
/** Condition code 3. */
#define X86_FSW_C3          RT_BIT_32(X86_FSW_C3_BIT)
#define X86_FSW_C3_BIT      14
/** Mask of exceptions flags, including the summary bit. */
#define X86_FSW_C_MASK      UINT16_C(0x4700)
/** FPU busy. */
#define X86_FSW_B           RT_BIT_32(15)
/** For use with FPREM and FPREM1. */
#define X86_FSW_CX_TO_QUOTIENT(a_fFsw) \
    (  (((a_fFsw) & X86_FSW_C1) >> (X86_FSW_C1_BIT - 0)) \
     | (((a_fFsw) & X86_FSW_C3) >> (X86_FSW_C3_BIT - 1)) \
     | (((a_fFsw) & X86_FSW_C0) >> (X86_FSW_C0_BIT - 2)) )
/** For use with FPREM and FPREM1. */
#define X86_FSW_CX_FROM_QUOTIENT(a_uQuotient) \
    (  ((uint16_t)((a_uQuotient) & 1) << (X86_FSW_C1_BIT - 0)) \
     | ((uint16_t)((a_uQuotient) & 2) << (X86_FSW_C3_BIT - 1)) \
     | ((uint16_t)((a_uQuotient) & 4) << (X86_FSW_C0_BIT - 2)) )
/** @} */


/** @name FPU control word flags.
 * @{ */
/** Exception Mask: Invalid operation.  */
#define X86_FCW_IM          RT_BIT_32(0)
#define X86_FCW_IM_BIT      0
/** Exception Mask: Denormalized operand.  */
#define X86_FCW_DM          RT_BIT_32(1)
#define X86_FCW_DM_BIT      1
/** Exception Mask: Zero divide.  */
#define X86_FCW_ZM          RT_BIT_32(2)
#define X86_FCW_ZM_BIT      2
/** Exception Mask: Overflow.  */
#define X86_FCW_OM          RT_BIT_32(3)
#define X86_FCW_OM_BIT      3
/** Exception Mask: Underflow.  */
#define X86_FCW_UM          RT_BIT_32(4)
#define X86_FCW_UM_BIT      4
/** Exception Mask: Precision.  */
#define X86_FCW_PM          RT_BIT_32(5)
#define X86_FCW_PM_BIT      5
/** Mask all exceptions, the value typically loaded (by for instance fninit).
 * @remarks This includes reserved bit 6.  */
#define X86_FCW_MASK_ALL    UINT16_C(0x007f)
/** Mask all exceptions. Same as X86_FSW_XCPT_MASK. */
#define X86_FCW_XCPT_MASK   UINT16_C(0x003f)
/** Precision control mask. */
#define X86_FCW_PC_MASK     UINT16_C(0x0300)
/** Precision control shift. */
#define X86_FCW_PC_SHIFT    8
/** Precision control: 24-bit. */
#define X86_FCW_PC_24       UINT16_C(0x0000)
/** Precision control: Reserved. */
#define X86_FCW_PC_RSVD     UINT16_C(0x0100)
/** Precision control: 53-bit. */
#define X86_FCW_PC_53       UINT16_C(0x0200)
/** Precision control: 64-bit. */
#define X86_FCW_PC_64       UINT16_C(0x0300)
/** Rounding control mask. */
#define X86_FCW_RC_MASK     UINT16_C(0x0c00)
/** Rounding control shift. */
#define X86_FCW_RC_SHIFT    10
/** Rounding control: To nearest. */
#define X86_FCW_RC_NEAREST  UINT16_C(0x0000)
/** Rounding control: Down. */
#define X86_FCW_RC_DOWN     UINT16_C(0x0400)
/** Rounding control: Up. */
#define X86_FCW_RC_UP       UINT16_C(0x0800)
/** Rounding control: Towards zero. */
#define X86_FCW_RC_ZERO     UINT16_C(0x0c00)
/** Infinity control mask - obsolete, 8087 & 287 only. */
#define X86_FCW_IC_MASK     UINT16_C(0x1000)
/** Infinity control: Affine - positive infinity is distictly different from
 *  negative infinity.
 * @note 8087, 287 only */
#define X86_FCW_IC_AFFINE   UINT16_C(0x1000)
/** Infinity control: Projective - positive and negative infinity are the
 * same (sign ignored).
 * @note 8087, 287 only */
#define X86_FCW_IC_PROJECTIVE UINT16_C(0x0000)
/** Bits which should be zero, apparently. */
#define X86_FCW_ZERO_MASK   UINT16_C(0xf080)
/** @} */

/** @name SSE MXCSR
 * @{ */
/** Exception Flag: Invalid operation.  */
#define X86_MXCSR_IE          RT_BIT_32(0)
#define X86_MXCSR_IE_BIT      0
/** Exception Flag: Denormalized operand.  */
#define X86_MXCSR_DE          RT_BIT_32(1)
#define X86_MXCSR_DE_BIT      1
/** Exception Flag: Zero divide.  */
#define X86_MXCSR_ZE          RT_BIT_32(2)
#define X86_MXCSR_ZE_BIT      2
/** Exception Flag: Overflow.  */
#define X86_MXCSR_OE          RT_BIT_32(3)
#define X86_MXCSR_OE_BIT      3
/** Exception Flag: Underflow.  */
#define X86_MXCSR_UE          RT_BIT_32(4)
#define X86_MXCSR_UE_BIT      4
/** Exception Flag: Precision.  */
#define X86_MXCSR_PE          RT_BIT_32(5)
#define X86_MXCSR_PE_BIT      5
/** Exception Flags: mask */
#define X86_MXCSR_XCPT_FLAGS  UINT32_C(0x003f)

/** Denormals are zero. */
#define X86_MXCSR_DAZ         RT_BIT_32(6)
#define X86_MXCSR_DAZ_BIT     6

/** Exception Mask: Invalid operation. */
#define X86_MXCSR_IM          RT_BIT_32(7)
#define X86_MXCSR_IM_BIT      7
/** Exception Mask: Denormalized operand. */
#define X86_MXCSR_DM          RT_BIT_32(8)
#define X86_MXCSR_DM_BIT      8
/** Exception Mask: Zero divide.  */
#define X86_MXCSR_ZM          RT_BIT_32(9)
#define X86_MXCSR_ZM_BIT      9
/** Exception Mask: Overflow.  */
#define X86_MXCSR_OM          RT_BIT_32(10)
#define X86_MXCSR_OM_BIT      10
/** Exception Mask: Underflow.  */
#define X86_MXCSR_UM          RT_BIT_32(11)
#define X86_MXCSR_UM_BIT      11
/** Exception Mask: Precision.  */
#define X86_MXCSR_PM          RT_BIT_32(12)
#define X86_MXCSR_PM_BIT      12
/** Exception Mask: mask.  */
#define X86_MXCSR_XCPT_MASK   UINT32_C(0x1f80)
/** Exception Mask: shift.  */
#define X86_MXCSR_XCPT_MASK_SHIFT 7

/** Rounding control mask. */
#define X86_MXCSR_RC_MASK     UINT32_C(0x6000)
/** Rounding control shift. */
#define X86_MXCSR_RC_SHIFT    13
/** Rounding control: To nearest. */
#define X86_MXCSR_RC_NEAREST  UINT32_C(0x0000)
/** Rounding control: Down. */
#define X86_MXCSR_RC_DOWN     UINT32_C(0x2000)
/** Rounding control: Up. */
#define X86_MXCSR_RC_UP       UINT32_C(0x4000)
/** Rounding control: Towards zero. */
#define X86_MXCSR_RC_ZERO     UINT32_C(0x6000)

/** Flush-to-zero for masked underflow.  */
#define X86_MXCSR_FZ          RT_BIT_32(15)
#define X86_MXCSR_FZ_BIT      15

/** Misaligned Exception Mask (AMD MISALIGNSSE).  */
#define X86_MXCSR_MM          RT_BIT_32(17)
#define X86_MXCSR_MM_BIT      17
/** Bits which should be zero, apparently. */
#define X86_MXCSR_ZERO_MASK   UINT32_C(0xfffd0000)
/** @} */

#ifndef __ASSEMBLER__

/**
 * XSAVE header.
 */
typedef struct X86XSAVEHDR
{
    /** XTATE_BV - Bitmap indicating whether a component is in the state. */
    uint64_t        bmXState;
    /** XCOMP_BC - Bitmap used by instructions applying structure compaction. */
    uint64_t        bmXComp;
    /** Reserved for furture extensions, probably MBZ. */
    uint64_t        au64Reserved[6];
} X86XSAVEHDR;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEHDR, 64);
# endif
/** Pointer to an XSAVE header. */
typedef X86XSAVEHDR *PX86XSAVEHDR;
/** Pointer to a const XSAVE header. */
typedef X86XSAVEHDR const *PCX86XSAVEHDR;


/**
 * The high 128-bit YMM register state (XSAVE_C_YMM).
 * (The lower 128-bits being in X86FXSTATE.)
 */
typedef struct X86XSAVEYMMHI
{
    /** 16 registers in 64-bit mode, 8 in 32-bit mode. */
    X86XMMREG       aYmmHi[16];
} X86XSAVEYMMHI;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEYMMHI, 256);
# endif
/** Pointer to a high 128-bit YMM register state. */
typedef X86XSAVEYMMHI *PX86XSAVEYMMHI;
/** Pointer to a const high 128-bit YMM register state. */
typedef X86XSAVEYMMHI const *PCX86XSAVEYMMHI;

/**
 * Intel MPX bound registers state (XSAVE_C_BNDREGS).
 */
typedef struct X86XSAVEBNDREGS
{
    /** Array of registers (BND0...BND3). */
    struct
    {
        /** Lower bound. */
        uint64_t    uLowerBound;
        /** Upper bound. */
        uint64_t    uUpperBound;
    } aRegs[4];
} X86XSAVEBNDREGS;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEBNDREGS, 64);
# endif
/** Pointer to a MPX bound register state. */
typedef X86XSAVEBNDREGS *PX86XSAVEBNDREGS;
/** Pointer to a const MPX bound register state. */
typedef X86XSAVEBNDREGS const *PCX86XSAVEBNDREGS;

/**
 * Intel MPX bound config and status register state (XSAVE_C_BNDCSR).
 */
typedef struct X86XSAVEBNDCFG
{
    uint64_t        fConfig;
    uint64_t        fStatus;
} X86XSAVEBNDCFG;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEBNDCFG, 16);
# endif
/** Pointer to a MPX bound config and status register state. */
typedef X86XSAVEBNDCFG *PX86XSAVEBNDCFG;
/** Pointer to a const MPX bound config and status register state. */
typedef X86XSAVEBNDCFG *PCX86XSAVEBNDCFG;

/**
 * AVX-512 opmask state (XSAVE_C_OPMASK).
 */
typedef struct X86XSAVEOPMASK
{
    /** The K0..K7 values. */
    uint64_t    aKRegs[8];
} X86XSAVEOPMASK;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEOPMASK, 64);
# endif
/** Pointer to a AVX-512 opmask state. */
typedef X86XSAVEOPMASK *PX86XSAVEOPMASK;
/** Pointer to a const AVX-512 opmask state. */
typedef X86XSAVEOPMASK const *PCX86XSAVEOPMASK;

/**
 * ZMM0-15 upper 256 bits introduced in AVX-512 (XSAVE_C_ZMM_HI256).
 */
typedef struct X86XSAVEZMMHI256
{
    /** Upper 256-bits of ZMM0-15. */
    X86YMMREG   aHi256Regs[16];
} X86XSAVEZMMHI256;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEZMMHI256, 512);
# endif
/** Pointer to a state comprising the upper 256-bits of ZMM0-15. */
typedef X86XSAVEZMMHI256 *PX86XSAVEZMMHI256;
/** Pointer to a const state comprising the upper 256-bits of ZMM0-15. */
typedef X86XSAVEZMMHI256 const *PCX86XSAVEZMMHI256;

/**
 * ZMM16-31 register state introduced in AVX-512 (XSAVE_C_ZMM_16HI).
 */
typedef struct X86XSAVEZMM16HI
{
    /** ZMM16 thru ZMM31. */
    X86ZMMREG   aRegs[16];
} X86XSAVEZMM16HI;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEZMM16HI, 1024);
# endif
/** Pointer to a state comprising ZMM16-32. */
typedef X86XSAVEZMM16HI *PX86XSAVEZMM16HI;
/** Pointer to a const state comprising ZMM16-32. */
typedef X86XSAVEZMM16HI const *PCX86XSAVEZMM16HI;

/**
 * AMD Light weight profiling state (XSAVE_C_LWP).
 *
 * We probably won't play with this as AMD seems to be dropping from their "zen"
 * processor micro architecture.
 */
typedef struct X86XSAVELWP
{
    /** Details when needed. */
    uint64_t        auLater[128/8];
} X86XSAVELWP;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVELWP, 128);
# endif


/**
 * x86 FPU/SSE/AVX/XXXX state.
 *
 * Please bump DBGFCORE_FMT_VERSION by 1 in dbgfcorefmt.h if you make any
 * changes to this structure.
 */
typedef struct X86XSAVEAREA
{
    /** The x87 and SSE region (or legacy region if you like).  */
    X86FXSTATE      x87;
    /** The XSAVE header. */
    X86XSAVEHDR     Hdr;
    /** Beyond the header, there isn't really a fixed layout, but we can
       generally assume the YMM (AVX) register extensions are present and
       follows immediately. */
    union
    {
        /** The high 128-bit AVX registers for easy access by IEM.
         * @note This ASSUMES they will always be here...  */
        X86XSAVEYMMHI       YmmHi;

        /** This is a typical layout on intel CPUs (good for debuggers). */
        struct
        {
            X86XSAVEYMMHI       YmmHi;
            X86XSAVEBNDREGS     BndRegs;
            X86XSAVEBNDCFG      BndCfg;
            uint8_t             abFudgeToMatchDocs[0xB0];
            X86XSAVEOPMASK      Opmask;
            X86XSAVEZMMHI256    ZmmHi256;
            X86XSAVEZMM16HI     Zmm16Hi;
        } Intel;

        /** This is a typical layout on AMD Bulldozer type CPUs (good for debuggers). */
        struct
        {
            X86XSAVEYMMHI       YmmHi;
            X86XSAVELWP         Lwp;
        } AmdBd;

        /** To enbling static deployments that have a reasonable chance of working for
         * the next 3-6 CPU generations without running short on space, we allocate a
         * lot of extra space here, making the structure a round 8KB in size.  This
         * leaves us 7616 bytes for extended state.  The skylake xeons are likely to use
         * 2112 of these, leaving us with 5504 bytes for future Intel generations. */
        uint8_t         ab[8192 - 512 - 64];
    } u;
} X86XSAVEAREA;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86XSAVEAREA, 8192);
AssertCompileMemberSize(X86XSAVEAREA, u.Intel, 0x840 /*2112 => total 0xa80 (2688) */);
AssertCompileMemberOffset(X86XSAVEAREA, Hdr,                0x200);
AssertCompileMemberOffset(X86XSAVEAREA, u.Intel.YmmHi,      0x240);
AssertCompileMemberOffset(X86XSAVEAREA, u.Intel.BndRegs,    0x340);
AssertCompileMemberOffset(X86XSAVEAREA, u.Intel.BndCfg,     0x380);
AssertCompileMemberOffset(X86XSAVEAREA, u.Intel.Opmask,     0x440 /* 1088 */);
AssertCompileMemberOffset(X86XSAVEAREA, u.Intel.ZmmHi256,   0x480 /* 1152 */);
AssertCompileMemberOffset(X86XSAVEAREA, u.Intel.Zmm16Hi,    0x680 /* 1664 */);
# endif
/** Pointer to a XSAVE area. */
typedef X86XSAVEAREA *PX86XSAVEAREA;
/** Pointer to a const XSAVE area. */
typedef X86XSAVEAREA const *PCX86XSAVEAREA;

#endif /* __ASSEMBLER__ */


/** @name XSAVE_C_XXX - XSAVE State Components Bits (XCR0).
 * @{ */
/** Bit 0 - x87 - Legacy FPU state (bit number) */
#define XSAVE_C_X87_BIT         0
/** Bit 0 - x87 - Legacy FPU state. */
#define XSAVE_C_X87             RT_BIT_64(XSAVE_C_X87_BIT)
/** Bit 1 - SSE - 128-bit SSE state (bit number). */
#define XSAVE_C_SSE_BIT         1
/** Bit 1 - SSE - 128-bit SSE state. */
#define XSAVE_C_SSE             RT_BIT_64(XSAVE_C_SSE_BIT)
/** Bit 2 - YMM_Hi128 - Upper 128 bits of YMM0-15 (AVX) (bit number). */
#define XSAVE_C_YMM_BIT         2
/** Bit 2 - YMM_Hi128 - Upper 128 bits of YMM0-15 (AVX). */
#define XSAVE_C_YMM             RT_BIT_64(XSAVE_C_YMM_BIT)
/** Bit 3 - BNDREGS - MPX bound register state (bit number). */
#define XSAVE_C_BNDREGS_BIT     3
/** Bit 3 - BNDREGS - MPX bound register state. */
#define XSAVE_C_BNDREGS         RT_BIT_64(XSAVE_C_BNDREGS_BIT)
/** Bit 4 - BNDCSR - MPX bound config and status state (bit number). */
#define XSAVE_C_BNDCSR_BIT      4
/** Bit 4 - BNDCSR - MPX bound config and status state. */
#define XSAVE_C_BNDCSR          RT_BIT_64(XSAVE_C_BNDCSR_BIT)
/** Bit 5 - Opmask - opmask state (bit number). */
#define XSAVE_C_OPMASK_BIT      5
/** Bit 5 - Opmask - opmask state. */
#define XSAVE_C_OPMASK          RT_BIT_64(XSAVE_C_OPMASK_BIT)
/** Bit 6 - ZMM_Hi256 - Upper 256 bits of ZMM0-15 (AVX-512) (bit number). */
#define XSAVE_C_ZMM_HI256_BIT   6
/** Bit 6 - ZMM_Hi256 - Upper 256 bits of ZMM0-15 (AVX-512). */
#define XSAVE_C_ZMM_HI256       RT_BIT_64(XSAVE_C_ZMM_HI256_BIT)
/** Bit 7 - Hi16_ZMM - 512-bits ZMM16-31 state (AVX-512) (bit number). */
#define XSAVE_C_ZMM_16HI_BIT    7
/** Bit 7 - Hi16_ZMM - 512-bits ZMM16-31 state (AVX-512). */
#define XSAVE_C_ZMM_16HI        RT_BIT_64(XSAVE_C_ZMM_16HI_BIT)
/** Bit 9 - PKRU - Protection-key state (bit number). */
#define XSAVE_C_PKRU_BIT        9
/** Bit 9 - PKRU - Protection-key state. */
#define XSAVE_C_PKRU            RT_BIT_64(XSAVE_C_PKRU_BIT)
/** Bit 62 - LWP - Lightweight Profiling (AMD) (bit number). */
#define XSAVE_C_LWP_BIT         62
/** Bit 62 - LWP - Lightweight Profiling (AMD). */
#define XSAVE_C_LWP             RT_BIT_64(XSAVE_C_LWP_BIT)
/** Bit 63 - X - Reserved (MBZ) for extending XCR0 (bit number). */
#define XSAVE_C_X_BIT           63
/** Bit 63 - X - Reserved (MBZ) for extending XCR0 (AMD). */
#define XSAVE_C_X               RT_BIT_64(XSAVE_C_X_BIT)
/** @} */



/** @name Selector Descriptor
 * @{
 */

#ifndef __ASSEMBLER__
# ifndef VBOX_FOR_DTRACE_LIB
/**
 * Descriptor attributes (as seen by VT-x).
 */
typedef struct X86DESCATTRBITS
{
    /** 00 - Segment Type. */
    unsigned    u4Type : 4;
    /** 04 - Descriptor Type. System(=0) or code/data selector */
    unsigned    u1DescType : 1;
    /** 05 - Descriptor Privilege level. */
    unsigned    u2Dpl : 2;
    /** 07 - Flags selector present(=1) or not. */
    unsigned    u1Present : 1;
    /** 08 - Segment limit 16-19. */
    unsigned    u4LimitHigh : 4;
    /** 0c - Available for system software. */
    unsigned    u1Available : 1;
    /** 0d - 32 bits mode: Reserved - 0, long mode: Long Attribute Bit. */
    unsigned    u1Long : 1;
    /** 0e - This flags meaning depends on the segment type. Try make sense out
     * of the intel manual yourself.  */
    unsigned    u1DefBig : 1;
    /** 0f - Granularity of the limit. If set 4KB granularity is used, if
     * clear byte. */
    unsigned    u1Granularity : 1;
    /** 10 - "Unusable" selector, special Intel (VT-x only?) bit. */
    unsigned    u1Unusable : 1;
} X86DESCATTRBITS;
# endif /* !VBOX_FOR_DTRACE_LIB */
#endif /* !__ASSEMBLER__ */

/** @name X86DESCATTR masks
 * Fields X86DESCGENERIC::u4Type thru X86DESCGENERIC::u1Granularity (or
 * bits[55:40] if you like).  The X86DESCATTR_UNUSABLE bit is an Intel addition.
 * @{ */
#define X86DESCATTR_TYPE            UINT32_C(0x0000000f)
#define X86DESCATTR_DT              UINT32_C(0x00000010)    /**< Descriptor type: 0=system, 1=code/data */
#define X86DESCATTR_DPL             UINT32_C(0x00000060)
#define X86DESCATTR_DPL_SHIFT       5                       /**< Shift count for the DPL bitfield. */
#define X86DESCATTR_P               UINT32_C(0x00000080)
#define X86DESCATTR_LIMIT_HIGH      UINT32_C(0x00000f00)
#define X86DESCATTR_AVL             UINT32_C(0x00001000)
#define X86DESCATTR_L               UINT32_C(0x00002000)
#define X86DESCATTR_D               UINT32_C(0x00004000)
#define X86DESCATTR_G               UINT32_C(0x00008000)
#define X86DESCATTR_UNUSABLE        UINT32_C(0x00010000)
/** @}  */


#ifndef __ASSEMBLER__
# pragma pack(1)
typedef union X86DESCATTR
{
    /** Unsigned integer view. */
    uint32_t           u;
# ifndef VBOX_FOR_DTRACE_LIB
    /** Normal view. */
    X86DESCATTRBITS    n;
# endif
} X86DESCATTR;
# pragma pack()
/** Pointer to descriptor attributes. */
typedef X86DESCATTR *PX86DESCATTR;
/** Pointer to const descriptor attributes. */
typedef const X86DESCATTR *PCX86DESCATTR;
#endif /* !__ASSEMBLER__ */

#ifndef VBOX_FOR_DTRACE_LIB

#ifndef __ASSEMBLER__
/**
 * Generic descriptor table entry
 */
#  pragma pack(1)
typedef struct X86DESCGENERIC
{
    /** 00 - Limit - Low word. */
    unsigned    u16LimitLow : 16;
    /** 10 - Base address - low word.
     * Don't try set this to 24 because MSC is doing stupid things then. */
    unsigned    u16BaseLow : 16;
    /** 20 - Base address - first 8 bits of high word. */
    unsigned    u8BaseHigh1 : 8;
    /** 28 - Segment Type. */
    unsigned    u4Type : 4;
    /** 2c - Descriptor Type. System(=0) or code/data selector */
    unsigned    u1DescType : 1;
    /** 2d - Descriptor Privilege level. */
    unsigned    u2Dpl : 2;
    /** 2f - Flags selector present(=1) or not. */
    unsigned    u1Present : 1;
    /** 30 - Segment limit 16-19. */
    unsigned    u4LimitHigh : 4;
    /** 34 - Available for system software. */
    unsigned    u1Available : 1;
    /** 35 - 32 bits mode: Reserved - 0, long mode: Long Attribute Bit. */
    unsigned    u1Long : 1;
    /** 36 - This flags meaning depends on the segment type. Try make sense out
     * of the intel manual yourself.  */
    unsigned    u1DefBig : 1;
    /** 37 - Granularity of the limit. If set 4KB granularity is used, if
     * clear byte. */
    unsigned    u1Granularity : 1;
    /** 38 - Base address - highest 8 bits. */
    unsigned    u8BaseHigh2 : 8;
} X86DESCGENERIC;
#  pragma pack()
/** Pointer to a generic descriptor entry. */
typedef X86DESCGENERIC *PX86DESCGENERIC;
/** Pointer to a const generic descriptor entry. */
typedef const X86DESCGENERIC *PCX86DESCGENERIC;
# endif /* !__ASSEMBLER__ */


/** @name Bit offsets of X86DESCGENERIC members.
 * @{*/
# define X86DESCGENERIC_BIT_OFF_LIMIT_LOW       (0)   /**< Bit offset of X86DESCGENERIC::u16LimitLow. */
# define X86DESCGENERIC_BIT_OFF_BASE_LOW        (16)  /**< Bit offset of X86DESCGENERIC::u16BaseLow. */
# define X86DESCGENERIC_BIT_OFF_BASE_HIGH1      (32)  /**< Bit offset of X86DESCGENERIC::u8BaseHigh1. */
# define X86DESCGENERIC_BIT_OFF_TYPE            (40)  /**< Bit offset of X86DESCGENERIC::u4Type. */
# define X86DESCGENERIC_BIT_OFF_DESC_TYPE       (44)  /**< Bit offset of X86DESCGENERIC::u1DescType. */
# define X86DESCGENERIC_BIT_OFF_DPL             (45)  /**< Bit offset of X86DESCGENERIC::u2Dpl. */
# define X86DESCGENERIC_BIT_OFF_PRESENT         (47)  /**< Bit offset of X86DESCGENERIC::uu1Present. */
# define X86DESCGENERIC_BIT_OFF_LIMIT_HIGH      (48)  /**< Bit offset of X86DESCGENERIC::u4LimitHigh. */
# define X86DESCGENERIC_BIT_OFF_AVAILABLE       (52)  /**< Bit offset of X86DESCGENERIC::u1Available. */
# define X86DESCGENERIC_BIT_OFF_LONG            (53)  /**< Bit offset of X86DESCGENERIC::u1Long. */
# define X86DESCGENERIC_BIT_OFF_DEF_BIG         (54)  /**< Bit offset of X86DESCGENERIC::u1DefBig. */
# define X86DESCGENERIC_BIT_OFF_GRANULARITY     (55)  /**< Bit offset of X86DESCGENERIC::u1Granularity. */
# define X86DESCGENERIC_BIT_OFF_BASE_HIGH2      (56)  /**< Bit offset of X86DESCGENERIC::u8BaseHigh2. */
/** @}  */


/** @name LAR mask
 * @{ */
# define X86LAR_F_TYPE           UINT16_C(    0x0f00)
# define X86LAR_F_DT             UINT16_C(    0x1000)
# define X86LAR_F_DPL            UINT16_C(    0x6000)
# define X86LAR_F_DPL_SHIFT      13 /**< Shift count for the DPL value. */
# define X86LAR_F_P              UINT16_C(    0x8000)
# define X86LAR_F_AVL            UINT32_C(0x00100000)
# define X86LAR_F_L              UINT32_C(0x00200000)
# define X86LAR_F_D              UINT32_C(0x00400000)
# define X86LAR_F_G              UINT32_C(0x00800000)
/** @}  */


# ifndef __ASSEMBLER__
/**
 * Call-, Interrupt-, Trap- or Task-gate descriptor (legacy).
 */
typedef struct X86DESCGATE
{
    /** 00 - Target code segment offset - Low word.
     * Ignored if task-gate. */
    unsigned    u16OffsetLow : 16;
    /** 10 - Target code segment selector for call-, interrupt- and trap-gates,
     * TSS selector if task-gate. */
    unsigned    u16Sel : 16;
    /** 20 - Number of parameters for a call-gate.
     * Ignored if interrupt-, trap- or task-gate. */
    unsigned    u5ParmCount : 5;
    /** 25 - Reserved / ignored. */
    unsigned    u3Reserved : 3;
    /** 28 - Segment Type. */
    unsigned    u4Type : 4;
    /** 2c - Descriptor Type (0 = system). */
    unsigned    u1DescType : 1;
    /** 2d - Descriptor Privilege level. */
    unsigned    u2Dpl : 2;
    /** 2f - Flags selector present(=1) or not. */
    unsigned    u1Present : 1;
    /** 30 - Target code segment offset - High word.
     * Ignored if task-gate. */
    unsigned    u16OffsetHigh : 16;
} X86DESCGATE;
/** Pointer to a Call-, Interrupt-, Trap- or Task-gate descriptor entry. */
typedef X86DESCGATE *PX86DESCGATE;
/** Pointer to a const Call-, Interrupt-, Trap- or Task-gate descriptor entry. */
typedef const X86DESCGATE *PCX86DESCGATE;
# endif /* !__ASSEMBLER__ */

#endif /* VBOX_FOR_DTRACE_LIB */

#ifndef __ASSEMBLER__
/**
 * Descriptor table entry.
 */
# pragma pack(1)
typedef union X86DESC
{
# ifndef VBOX_FOR_DTRACE_LIB
    /** Generic descriptor view. */
    X86DESCGENERIC  Gen;
    /** Gate descriptor view. */
    X86DESCGATE     Gate;
# endif
    /** 8 bit unsigned integer view. */
    uint8_t         au8[8];
    /** 16 bit unsigned integer view. */
    uint16_t        au16[4];
    /** 32 bit unsigned integer view. */
    uint32_t        au32[2];
    /** 64 bit unsigned integer view. */
    uint64_t        au64[1];
    /** Unsigned integer view. */
    uint64_t        u;
} X86DESC;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86DESC, 8);
# endif
# pragma pack()
/** Pointer to descriptor table entry. */
typedef X86DESC *PX86DESC;
/** Pointer to const descriptor table entry. */
typedef const X86DESC *PCX86DESC;
#endif /* !__ASSEMBLER__ */

/** @def X86DESC_BASE
 * Return the base address of a descriptor.
 */
#define X86DESC_BASE(a_pDesc) /*ASM-NOINC*/ \
        (  ((uint32_t)((a_pDesc)->Gen.u8BaseHigh2) << 24) \
         | (           (a_pDesc)->Gen.u8BaseHigh1  << 16) \
         | (           (a_pDesc)->Gen.u16BaseLow        ) )

/** @def X86DESC_LIMIT
 * Return the limit of a descriptor.
 */
#define X86DESC_LIMIT(a_pDesc) /*ASM-NOINC*/ \
        (  ((uint32_t)((a_pDesc)->Gen.u4LimitHigh) << 16) \
         | (           (a_pDesc)->Gen.u16LimitLow       ) )

/** @def X86DESC_LIMIT_G
 * Return the limit of a descriptor with the granularity bit taken into account.
 * @returns Selector limit (uint32_t).
 * @param   a_pDesc     Pointer to the descriptor.
 */
#define X86DESC_LIMIT_G(a_pDesc) /*ASM-NOINC*/ \
        ( (a_pDesc)->Gen.u1Granularity \
         ? ( ( ((uint32_t)(a_pDesc)->Gen.u4LimitHigh << 16) | (a_pDesc)->Gen.u16LimitLow ) << 12 ) | UINT32_C(0xfff) \
         :     ((uint32_t)(a_pDesc)->Gen.u4LimitHigh << 16) | (a_pDesc)->Gen.u16LimitLow \
        )

/** @def X86DESC_GET_HID_ATTR
 * Get the descriptor attributes for the hidden register.
 */
#define X86DESC_GET_HID_ATTR(a_pDesc) /*ASM-NOINC*/ \
        ( ((a_pDesc)->u >> (16+16+8)) & UINT32_C(0xf0ff) ) /** @todo do we have a define for 0xf0ff? */

#ifndef __ASSEMBLER__
# ifndef VBOX_FOR_DTRACE_LIB

/**
 * 64 bits generic descriptor table entry
 * Note: most of these bits have no meaning in long mode.
 */
#  pragma pack(1)
typedef struct X86DESC64GENERIC
{
    /** Limit - Low word - *IGNORED*. */
    uint32_t    u16LimitLow : 16;
    /** Base address - low word. - *IGNORED*
     * Don't try set this to 24 because MSC is doing stupid things then. */
    uint32_t    u16BaseLow : 16;
    /** Base address - first 8 bits of high word. - *IGNORED* */
    uint32_t    u8BaseHigh1 : 8;
    /** Segment Type. */
    uint32_t    u4Type : 4;
    /** Descriptor Type. System(=0) or code/data selector */
    uint32_t    u1DescType : 1;
    /** Descriptor Privilege level. */
    uint32_t    u2Dpl : 2;
    /** Flags selector present(=1) or not. */
    uint32_t    u1Present : 1;
    /** Segment limit 16-19. - *IGNORED* */
    uint32_t    u4LimitHigh : 4;
    /** Available for system software. - *IGNORED* */
    uint32_t    u1Available : 1;
    /** Long mode flag. */
    uint32_t    u1Long : 1;
    /** This flags meaning depends on the segment type. Try make sense out
     * of the intel manual yourself.  */
    uint32_t    u1DefBig : 1;
    /** Granularity of the limit. If set 4KB granularity is used, if
     * clear byte. - *IGNORED* */
    uint32_t    u1Granularity : 1;
    /** Base address - highest 8 bits. - *IGNORED* */
    uint32_t    u8BaseHigh2 : 8;
    /** Base address - bits 63-32. */
    uint32_t    u32BaseHigh3    : 32;
    uint32_t    u8Reserved      : 8;
    uint32_t    u5Zeros         : 5;
    uint32_t    u19Reserved     : 19;
} X86DESC64GENERIC;
#  pragma pack()
/** Pointer to a generic descriptor entry. */
typedef X86DESC64GENERIC *PX86DESC64GENERIC;
/** Pointer to a const generic descriptor entry. */
typedef const X86DESC64GENERIC *PCX86DESC64GENERIC;

/**
 * System descriptor table entry (64 bits)
 *
 * @remarks This is, save a couple of comments, identical to X86DESC64GENERIC...
 */
#  pragma pack(1)
typedef struct X86DESC64SYSTEM
{
    /** Limit - Low word. */
    uint32_t    u16LimitLow     : 16;
    /** Base address - low word.
     * Don't try set this to 24 because MSC is doing stupid things then. */
    uint32_t    u16BaseLow      : 16;
    /** Base address - first 8 bits of high word. */
    uint32_t    u8BaseHigh1     : 8;
    /** Segment Type. */
    uint32_t    u4Type          : 4;
    /** Descriptor Type. System(=0) or code/data selector */
    uint32_t    u1DescType      : 1;
    /** Descriptor Privilege level. */
    uint32_t    u2Dpl           : 2;
    /** Flags selector present(=1) or not. */
    uint32_t    u1Present       : 1;
    /** Segment limit 16-19. */
    uint32_t    u4LimitHigh     : 4;
    /** Available for system software. */
    uint32_t    u1Available     : 1;
    /** Reserved - 0. */
    uint32_t    u1Reserved      : 1;
    /** This flags meaning depends on the segment type. Try make sense out
     * of the intel manual yourself.  */
    uint32_t    u1DefBig        : 1;
    /** Granularity of the limit. If set 4KB granularity is used, if
     * clear byte. */
    uint32_t    u1Granularity   : 1;
    /** Base address - bits 31-24. */
    uint32_t    u8BaseHigh2     : 8;
    /** Base address - bits 63-32. */
    uint32_t    u32BaseHigh3    : 32;
    uint32_t    u8Reserved      : 8;
    uint32_t    u5Zeros         : 5;
    uint32_t    u19Reserved     : 19;
} X86DESC64SYSTEM;
#  pragma pack()
/** Pointer to a system descriptor entry. */
typedef X86DESC64SYSTEM *PX86DESC64SYSTEM;
/** Pointer to a const system descriptor entry. */
typedef const X86DESC64SYSTEM *PCX86DESC64SYSTEM;

/**
 * Call-, Interrupt-, Trap- or Task-gate descriptor (64-bit).
 */
typedef struct X86DESC64GATE
{
    /** Target code segment offset - Low word. */
    uint32_t    u16OffsetLow : 16;
    /** Target code segment selector. */
    uint32_t    u16Sel : 16;
    /** Interrupt stack table for interrupt- and trap-gates.
     * Ignored by call-gates. */
    uint32_t    u3IST : 3;
    /** Reserved / ignored. */
    uint32_t    u5Reserved : 5;
    /** Segment Type. */
    uint32_t    u4Type : 4;
    /** Descriptor Type (0 = system). */
    uint32_t    u1DescType : 1;
    /** Descriptor Privilege level. */
    uint32_t    u2Dpl : 2;
    /** Flags selector present(=1) or not. */
    uint32_t    u1Present : 1;
    /** Target code segment offset - High word.
     * Ignored if task-gate. */
    uint32_t    u16OffsetHigh : 16;
    /** Target code segment offset - Top dword.
     * Ignored if task-gate. */
    uint32_t    u32OffsetTop : 32;
    /** Reserved / ignored / must be zero.
     * For call-gates bits 8 thru 12 must be zero, the other gates ignores this. */
    uint32_t    u32Reserved : 32;
} X86DESC64GATE;
AssertCompileSize(X86DESC64GATE, 16);
/** Pointer to a Call-, Interrupt-, Trap- or Task-gate descriptor entry. */
typedef X86DESC64GATE *PX86DESC64GATE;
/** Pointer to a const Call-, Interrupt-, Trap- or Task-gate descriptor entry. */
typedef const X86DESC64GATE *PCX86DESC64GATE;

# endif /* VBOX_FOR_DTRACE_LIB */

/**
 * Descriptor table entry.
 */
# pragma pack(1)
typedef union X86DESC64
{
# ifndef VBOX_FOR_DTRACE_LIB
    /** Generic descriptor view. */
    X86DESC64GENERIC    Gen;
    /** System descriptor view. */
    X86DESC64SYSTEM     System;
    /** Gate descriptor view. */
    X86DESC64GATE       Gate;
# endif

    /** 8 bit unsigned integer view. */
    uint8_t             au8[16];
    /** 16 bit unsigned integer view. */
    uint16_t            au16[8];
    /** 32 bit unsigned integer view. */
    uint32_t            au32[4];
    /** 64 bit unsigned integer view. */
    uint64_t            au64[2];
} X86DESC64;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86DESC64, 16);
# endif
# pragma pack()
/** Pointer to descriptor table entry. */
typedef X86DESC64 *PX86DESC64;
/** Pointer to const descriptor table entry. */
typedef const X86DESC64 *PCX86DESC64;

/** @def X86DESC64_BASE
 * Return the base of a 64-bit descriptor.
 */
#define X86DESC64_BASE(a_pDesc) /*ASM-NOINC*/ \
        (  ((uint64_t)((a_pDesc)->Gen.u32BaseHigh3) << 32) \
         | ((uint32_t)((a_pDesc)->Gen.u8BaseHigh2)  << 24) \
         | (           (a_pDesc)->Gen.u8BaseHigh1   << 16) \
         | (           (a_pDesc)->Gen.u16BaseLow         ) )



/** @name Host system descriptor table entry - Use with care!
 * @{ */
/** Host system descriptor table entry. */
#if HC_ARCH_BITS == 64
typedef X86DESC64   X86DESCHC;
#else
typedef X86DESC     X86DESCHC;
#endif
/** Pointer to a host system descriptor table entry. */
#if HC_ARCH_BITS == 64
typedef PX86DESC64  PX86DESCHC;
#else
typedef PX86DESC    PX86DESCHC;
#endif
/** Pointer to a const host system descriptor table entry. */
#if HC_ARCH_BITS == 64
typedef PCX86DESC64 PCX86DESCHC;
#else
typedef PCX86DESC   PCX86DESCHC;
#endif
/** @} */

#endif /* !__ASSEMBLER__ */


/** @name Selector Descriptor Types.
 * @{
 */

/** @name Non-System Selector Types.
 * @{ */
/** Code(=set)/Data(=clear) bit. */
#define X86_SEL_TYPE_CODE                   8
/** Memory(=set)/System(=clear) bit. */
#define X86_SEL_TYPE_MEMORY                 RT_BIT_32(4)
/** Accessed bit. */
#define X86_SEL_TYPE_ACCESSED               1
/** Expand down bit (for data selectors only). */
#define X86_SEL_TYPE_DOWN                   4
/** Conforming bit (for code selectors only). */
#define X86_SEL_TYPE_CONF                   4
/** Write bit (for data selectors only). */
#define X86_SEL_TYPE_WRITE                  2
/** Read bit (for code selectors only). */
#define X86_SEL_TYPE_READ                   2
/** The bit number of the code segment read bit (relative to u4Type). */
#define X86_SEL_TYPE_READ_BIT               1

/** Read only selector type. */
#define X86_SEL_TYPE_RO                     0
/** Accessed read only selector type. */
#define X86_SEL_TYPE_RO_ACC                (0 | X86_SEL_TYPE_ACCESSED)
/** Read write selector type. */
#define X86_SEL_TYPE_RW                     2
/** Accessed read write selector type. */
#define X86_SEL_TYPE_RW_ACC                (2 | X86_SEL_TYPE_ACCESSED)
/** Expand down read only selector type. */
#define X86_SEL_TYPE_RO_DOWN                4
/** Accessed expand down read only selector type. */
#define X86_SEL_TYPE_RO_DOWN_ACC           (4 | X86_SEL_TYPE_ACCESSED)
/** Expand down read write selector type. */
#define X86_SEL_TYPE_RW_DOWN                6
/** Accessed expand down read write selector type. */
#define X86_SEL_TYPE_RW_DOWN_ACC           (6 | X86_SEL_TYPE_ACCESSED)
/** Execute only selector type. */
#define X86_SEL_TYPE_EO                    (0 | X86_SEL_TYPE_CODE)
/** Accessed execute only selector type. */
#define X86_SEL_TYPE_EO_ACC                (0 | X86_SEL_TYPE_CODE | X86_SEL_TYPE_ACCESSED)
/** Execute and read selector type. */
#define X86_SEL_TYPE_ER                    (2 | X86_SEL_TYPE_CODE)
/** Accessed execute and read selector type. */
#define X86_SEL_TYPE_ER_ACC                (2 | X86_SEL_TYPE_CODE | X86_SEL_TYPE_ACCESSED)
/** Conforming execute only selector type. */
#define X86_SEL_TYPE_EO_CONF               (4 | X86_SEL_TYPE_CODE)
/** Accessed Conforming execute only selector type. */
#define X86_SEL_TYPE_EO_CONF_ACC           (4 | X86_SEL_TYPE_CODE | X86_SEL_TYPE_ACCESSED)
/** Conforming execute and write selector type. */
#define X86_SEL_TYPE_ER_CONF               (6 | X86_SEL_TYPE_CODE)
/** Accessed Conforming execute and write selector type. */
#define X86_SEL_TYPE_ER_CONF_ACC           (6 | X86_SEL_TYPE_CODE | X86_SEL_TYPE_ACCESSED)
/** @} */


/** @name System Selector Types.
 * @{ */
/** The TSS busy bit mask. */
#define X86_SEL_TYPE_SYS_TSS_BUSY_MASK      2

/** Undefined system selector type. */
#define X86_SEL_TYPE_SYS_UNDEFINED          0
/** 286 TSS selector. */
#define X86_SEL_TYPE_SYS_286_TSS_AVAIL      1
/** LDT selector. */
#define X86_SEL_TYPE_SYS_LDT                2
/** 286 TSS selector - Busy. */
#define X86_SEL_TYPE_SYS_286_TSS_BUSY       3
/** 286 Callgate selector. */
#define X86_SEL_TYPE_SYS_286_CALL_GATE      4
/** Taskgate selector. */
#define X86_SEL_TYPE_SYS_TASK_GATE          5
/** 286 Interrupt gate selector. */
#define X86_SEL_TYPE_SYS_286_INT_GATE       6
/** 286 Trapgate selector. */
#define X86_SEL_TYPE_SYS_286_TRAP_GATE      7
/** Undefined system selector. */
#define X86_SEL_TYPE_SYS_UNDEFINED2         8
/** 386 TSS selector. */
#define X86_SEL_TYPE_SYS_386_TSS_AVAIL      9
/** Undefined system selector. */
#define X86_SEL_TYPE_SYS_UNDEFINED3         0xA
/** 386 TSS selector - Busy. */
#define X86_SEL_TYPE_SYS_386_TSS_BUSY       0xB
/** 386 Callgate selector. */
#define X86_SEL_TYPE_SYS_386_CALL_GATE      0xC
/** Undefined system selector. */
#define X86_SEL_TYPE_SYS_UNDEFINED4         0xD
/** 386 Interruptgate selector. */
#define X86_SEL_TYPE_SYS_386_INT_GATE       0xE
/** 386 Trapgate selector. */
#define X86_SEL_TYPE_SYS_386_TRAP_GATE      0xF
/** @} */

/** @name AMD64 System Selector Types.
 * @{ */
/** LDT selector. */
#define AMD64_SEL_TYPE_SYS_LDT              2
/** TSS selector - Busy. */
#define AMD64_SEL_TYPE_SYS_TSS_AVAIL        9
/** TSS selector - Busy. */
#define AMD64_SEL_TYPE_SYS_TSS_BUSY         0xB
/** Callgate selector. */
#define AMD64_SEL_TYPE_SYS_CALL_GATE        0xC
/** Interruptgate selector. */
#define AMD64_SEL_TYPE_SYS_INT_GATE         0xE
/** Trapgate selector. */
#define AMD64_SEL_TYPE_SYS_TRAP_GATE        0xF
/** @} */

/** @} */


/** @name Descriptor Table Entry Flag Masks.
 * These are for the 2nd 32-bit word of a descriptor.
 * @{ */
/** Bits 8-11 - TYPE - Descriptor type mask. */
#define X86_DESC_TYPE_MASK                  (RT_BIT_32(8) | RT_BIT_32(9) | RT_BIT_32(10) | RT_BIT_32(11))
/** Bit 12 - S - System (=0) or Code/Data (=1). */
#define X86_DESC_S                          RT_BIT_32(12)
/** Bits 13-14 - DPL - Descriptor Privilege Level. */
#define X86_DESC_DPL                       (RT_BIT_32(13) | RT_BIT_32(14))
/** Bit 15 - P - Present. */
#define X86_DESC_P                          RT_BIT_32(15)
/** Bit 20 - AVL - Available for system software. */
#define X86_DESC_AVL                        RT_BIT_32(20)
/** Bit 22 - DB - Default operation size. 0 = 16 bit, 1 = 32 bit. */
#define X86_DESC_DB                         RT_BIT_32(22)
/** Bit 23 - G - Granularity of the limit. If set 4KB granularity is
 * used, if clear byte. */
#define X86_DESC_G                          RT_BIT_32(23)
/** @} */

/** @} */


/** @name Task Segments.
 * @{
 */

/**
 * The minimum TSS descriptor limit for 286 tasks.
 */
#define X86_SEL_TYPE_SYS_286_TSS_LIMIT_MIN      0x2b

/**
 * The minimum TSS descriptor segment limit for 386 tasks.
 */
#define X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN      0x67

#ifndef __ASSEMBLER__

/**
 * 16-bit Task Segment (TSS).
 */
# pragma pack(1)
typedef struct X86TSS16
{
    /** Back link to previous task. (static) */
    RTSEL       selPrev;
    /** Ring-0 stack pointer. (static) */
    uint16_t    sp0;
    /** Ring-0 stack segment. (static) */
    RTSEL       ss0;
    /** Ring-1 stack pointer. (static) */
    uint16_t    sp1;
    /** Ring-1 stack segment. (static) */
    RTSEL       ss1;
    /** Ring-2 stack pointer. (static) */
    uint16_t    sp2;
    /** Ring-2 stack segment. (static) */
    RTSEL       ss2;
    /** IP before task switch. */
    uint16_t    ip;
    /** FLAGS before task switch. */
    uint16_t    flags;
    /** AX before task switch. */
    uint16_t    ax;
    /** CX before task switch. */
    uint16_t    cx;
    /** DX before task switch. */
    uint16_t    dx;
    /** BX before task switch. */
    uint16_t    bx;
    /** SP before task switch. */
    uint16_t    sp;
    /** BP before task switch. */
    uint16_t    bp;
    /** SI before task switch. */
    uint16_t    si;
    /** DI before task switch. */
    uint16_t    di;
    /** ES before task switch. */
    RTSEL       es;
    /** CS before task switch. */
    RTSEL       cs;
    /** SS before task switch. */
    RTSEL       ss;
    /** DS before task switch. */
    RTSEL       ds;
    /** LDTR before task switch. */
    RTSEL       selLdt;
} X86TSS16;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86TSS16, X86_SEL_TYPE_SYS_286_TSS_LIMIT_MIN + 1);
# endif
# pragma pack()
/** Pointer to a 16-bit task segment. */
typedef X86TSS16 *PX86TSS16;
/** Pointer to a const 16-bit task segment. */
typedef const X86TSS16 *PCX86TSS16;


/**
 * 32-bit Task Segment (TSS).
 */
# pragma pack(1)
typedef struct X86TSS32
{
    /** Back link to previous task. (static) */
    RTSEL       selPrev;
    uint16_t    padding1;
    /** Ring-0 stack pointer. (static) */
    uint32_t    esp0;
    /** Ring-0 stack segment. (static) */
    RTSEL       ss0;
    uint16_t    padding_ss0;
    /** Ring-1 stack pointer. (static) */
    uint32_t    esp1;
    /** Ring-1 stack segment. (static) */
    RTSEL       ss1;
    uint16_t    padding_ss1;
    /** Ring-2 stack pointer. (static) */
    uint32_t    esp2;
    /** Ring-2 stack segment. (static) */
    RTSEL       ss2;
    uint16_t    padding_ss2;
    /** Page directory for the task. (static) */
    uint32_t    cr3;
    /** EIP before task switch. */
    uint32_t    eip;
    /** EFLAGS before task switch. */
    uint32_t    eflags;
    /** EAX before task switch. */
    uint32_t    eax;
    /** ECX before task switch. */
    uint32_t    ecx;
    /** EDX before task switch. */
    uint32_t    edx;
    /** EBX before task switch. */
    uint32_t    ebx;
    /** ESP before task switch. */
    uint32_t    esp;
    /** EBP before task switch. */
    uint32_t    ebp;
    /** ESI before task switch. */
    uint32_t    esi;
    /** EDI before task switch. */
    uint32_t    edi;
    /** ES before task switch. */
    RTSEL       es;
    uint16_t    padding_es;
    /** CS before task switch. */
    RTSEL       cs;
    uint16_t    padding_cs;
    /** SS before task switch. */
    RTSEL       ss;
    uint16_t    padding_ss;
    /** DS before task switch. */
    RTSEL       ds;
    uint16_t    padding_ds;
    /** FS before task switch. */
    RTSEL       fs;
    uint16_t    padding_fs;
    /** GS before task switch. */
    RTSEL       gs;
    uint16_t    padding_gs;
    /** LDTR before task switch. */
    RTSEL       selLdt;
    uint16_t    padding_ldt;
    /** Debug trap flag */
    uint16_t    fDebugTrap;
    /** Offset relative to the TSS of the start of the I/O Bitmap
     * and the end of the interrupt redirection bitmap. */
    uint16_t    offIoBitmap;
} X86TSS32;
# pragma pack()
/** Pointer to task segment. */
typedef X86TSS32 *PX86TSS32;
/** Pointer to const task segment. */
typedef const X86TSS32 *PCX86TSS32;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86TSS32, X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN + 1);
AssertCompileMemberOffset(X86TSS32, cr3, 28);
AssertCompileMemberOffset(X86TSS32, offIoBitmap, 102);
# endif

/**
 * 64-bit Task segment.
 */
# pragma pack(1)
typedef struct X86TSS64
{
    /** Reserved. */
    uint32_t    u32Reserved;
    /** Ring-0 stack pointer. (static) */
    uint64_t    rsp0;
    /** Ring-1 stack pointer. (static) */
    uint64_t    rsp1;
    /** Ring-2 stack pointer. (static) */
    uint64_t    rsp2;
    /** Reserved. */
    uint32_t    u32Reserved2[2];
    /* IST */
    uint64_t    ist1;
    uint64_t    ist2;
    uint64_t    ist3;
    uint64_t    ist4;
    uint64_t    ist5;
    uint64_t    ist6;
    uint64_t    ist7;
    /* Reserved. */
    uint16_t    u16Reserved[5];
    /** Offset relative to the TSS of the start of the I/O Bitmap
     * and the end of the interrupt redirection bitmap. */
    uint16_t    offIoBitmap;
} X86TSS64;
# pragma pack()
/** Pointer to a 64-bit task segment. */
typedef X86TSS64 *PX86TSS64;
/** Pointer to a const 64-bit task segment. */
typedef const X86TSS64 *PCX86TSS64;
# ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(X86TSS64, X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN + 1);
# endif

#endif /* !__ASSEMBLER__ */

/** @} */


/** @name Selectors.
 * @{
 */

/**
 * The shift used to convert a selector from and to index an index (C).
 */
#define X86_SEL_SHIFT           3

/**
 * The mask used to mask off the table indicator and RPL of an selector.
 */
#define X86_SEL_MASK            0xfff8U

/**
 * The mask used to mask off the RPL of an selector.
 * This is suitable for checking for NULL selectors.
 */
#define X86_SEL_MASK_OFF_RPL    0xfffcU

/**
 * The bit indicating that a selector is in the LDT and not in the GDT.
 */
#define X86_SEL_LDT             0x0004U

/**
 * The bit mask for getting the RPL of a selector.
 */
#define X86_SEL_RPL             0x0003U

/**
 * The mask covering both RPL and LDT.
 * This is incidentally the same as sizeof(X86DESC) - 1, so good for limit
 * checks.
 */
#define X86_SEL_RPL_LDT         0x0007U

/** @} */


#ifndef __ASSEMBLER__
/**
 * x86 Exceptions/Faults/Traps.
 */
typedef enum X86XCPT
{
    /** \#DE - Divide error. */
    X86_XCPT_DE = 0x00,
    /** \#DB - Debug event (single step, DRx, ..) */
    X86_XCPT_DB = 0x01,
    /** NMI - Non-Maskable Interrupt */
    X86_XCPT_NMI = 0x02,
    /** \#BP - Breakpoint (INT3). */
    X86_XCPT_BP = 0x03,
    /** \#OF - Overflow (INTO). */
    X86_XCPT_OF = 0x04,
    /** \#BR - Bound range exceeded (BOUND). */
    X86_XCPT_BR = 0x05,
    /** \#UD - Undefined opcode. */
    X86_XCPT_UD = 0x06,
    /** \#NM - Device not available (math coprocessor device). */
    X86_XCPT_NM = 0x07,
    /** \#DF - Double fault. */
    X86_XCPT_DF = 0x08,
    /** ??? - Coprocessor segment overrun (obsolete). */
    X86_XCPT_CO_SEG_OVERRUN = 0x09,
    /** \#TS - Taskswitch (TSS). */
    X86_XCPT_TS = 0x0a,
    /** \#NP - Segment no present. */
    X86_XCPT_NP = 0x0b,
    /** \#SS - Stack segment fault. */
    X86_XCPT_SS = 0x0c,
    /** \#GP - General protection fault. */
    X86_XCPT_GP = 0x0d,
    /** \#PF - Page fault. */
    X86_XCPT_PF = 0x0e,
    /* 0x0f is reserved (to avoid conflict with spurious interrupts in BIOS setup). */
    /** \#MF - Math fault (FPU). */
    X86_XCPT_MF = 0x10,
    /** \#AC - Alignment check. */
    X86_XCPT_AC = 0x11,
    /** \#MC - Machine check. */
    X86_XCPT_MC = 0x12,
    /** \#XF - SIMD Floating-Point Exception. */
    X86_XCPT_XF = 0x13,
    /** \#VE - Virtualization Exception (Intel only). */
    X86_XCPT_VE = 0x14,
    /** \#CP - Control Protection Exception. */
    X86_XCPT_CP = 0x15,
    /** \#VC - VMM Communication Exception (AMD only). */
    X86_XCPT_VC = 0x1d,
    /** \#SX - Security Exception (AMD only). */
    X86_XCPT_SX = 0x1e
} X86XCPT;
/** Pointer to a x86 exception code. */
typedef X86XCPT *PX86XCPT;
/** Pointer to a const x86 exception code. */
typedef const X86XCPT *PCX86XCPT;
#endif /* !__ASSEMBLER__ */
/** The last valid (currently reserved) exception value. */
#define X86_XCPT_LAST               0x1f


/** @name Trap Error Codes
 * @{
 */
/** External indicator. */
#define X86_TRAP_ERR_EXTERNAL       1
/** IDT indicator. */
#define X86_TRAP_ERR_IDT            2
/** Descriptor table indicator - If set LDT, if clear GDT. */
#define X86_TRAP_ERR_TI             4
/** Mask for getting the selector. */
#define X86_TRAP_ERR_SEL_MASK       0xfff8
/** Shift for getting the selector table index (C type index). */
#define X86_TRAP_ERR_SEL_SHIFT      3
/** @} */


/** @name \#PF Trap Error Codes
 * @{
 */
/** Bit 0 -   P - Not present (clear) or page level protection (set) fault. */
#define X86_TRAP_PF_P               RT_BIT_32(0)
/** Bit 1 - R/W - Read (clear) or write (set) access. */
#define X86_TRAP_PF_RW              RT_BIT_32(1)
/** Bit 2 - U/S - CPU executing in user mode (set) or supervisor mode (clear). */
#define X86_TRAP_PF_US              RT_BIT_32(2)
/** Bit 3 - RSVD- Reserved bit violation (set), i.e. reserved bit was set to 1. */
#define X86_TRAP_PF_RSVD            RT_BIT_32(3)
/** Bit 4 - I/D - Instruction fetch (set) / Data access (clear) - PAE + NXE. */
#define X86_TRAP_PF_ID              RT_BIT_32(4)
/** Bit 5 - PK - Protection-key violation (AMD64 mode only). */
#define X86_TRAP_PF_PK              RT_BIT_32(5)
/** @} */

#ifndef __ASSEMBLER__

# pragma pack(1)
/**
 * 16-bit IDTR.
 */
typedef struct X86IDTR16
{
    /** Offset. */
    uint16_t    offSel;
    /** Selector. */
    uint16_t    uSel;
} X86IDTR16, *PX86IDTR16;
# pragma pack()

# pragma pack(1)
/**
 * 32-bit IDTR/GDTR.
 */
typedef struct X86XDTR32
{
    /** Size of the descriptor table. */
    uint16_t    cb;
    /** Address of the descriptor table. */
# ifndef VBOX_FOR_DTRACE_LIB
    uint32_t    uAddr;
# else
    uint16_t    au16Addr[2];
# endif
} X86XDTR32, *PX86XDTR32;
# pragma pack()

# pragma pack(1)
/**
 * 64-bit IDTR/GDTR.
 */
typedef struct X86XDTR64
{
    /** Size of the descriptor table. */
    uint16_t    cb;
    /** Address of the descriptor table. */
# ifndef VBOX_FOR_DTRACE_LIB
    uint64_t    uAddr;
# else
    uint16_t    au16Addr[4];
# endif
} X86XDTR64, *PX86XDTR64;
# pragma pack()

#endif /* !__ASSEMBLER__ */


/** @name ModR/M
 * @{ */
#define X86_MODRM_RM_MASK       UINT8_C(0x07)
#define X86_MODRM_REG_MASK      UINT8_C(0x38)
#define X86_MODRM_REG_SMASK     UINT8_C(0x07)
#define X86_MODRM_REG_SHIFT     3
#define X86_MODRM_MOD_MASK      UINT8_C(0xc0)
#define X86_MODRM_MOD_SMASK     UINT8_C(0x03)
#define X86_MODRM_MOD_SHIFT     6

#define X86_MOD_MEM0            0   /**< Indirect addressing without displacement (except RM=4 (SIB) and RM=5 (disp32)). */
#define X86_MOD_MEM1            1   /**< Indirect addressing with 8-bit displacement. */
#define X86_MOD_MEM4            2   /**< Indirect addressing with 32-bit displacement. */
#define X86_MOD_REG             3   /**< Registers. */

#ifndef VBOX_FOR_DTRACE_LIB
AssertCompile((X86_MODRM_RM_MASK | X86_MODRM_REG_MASK | X86_MODRM_MOD_MASK) == 0xff);
AssertCompile((X86_MODRM_REG_MASK >> X86_MODRM_REG_SHIFT) == X86_MODRM_REG_SMASK);
AssertCompile((X86_MODRM_MOD_MASK >> X86_MODRM_MOD_SHIFT) == X86_MODRM_MOD_SMASK);
/** @def X86_MODRM_MAKE
 * @param   a_Mod       The mod value (0..3) - X86_MOD_XXX.
 * @param   a_Reg       The register value (0..7).
 * @param   a_RegMem    The register or memory value (0..7). */
# define X86_MODRM_MAKE(a_Mod, a_Reg, a_RegMem) (((a_Mod) << X86_MODRM_MOD_SHIFT) | ((a_Reg) << X86_MODRM_REG_SHIFT) | (a_RegMem))
#endif

/** @} */

/** @name SIB
 * @{ */
#define X86_SIB_BASE_MASK     UINT8_C(0x07)
#define X86_SIB_INDEX_MASK    UINT8_C(0x38)
#define X86_SIB_INDEX_SMASK   UINT8_C(0x07)
#define X86_SIB_INDEX_SHIFT   3
#define X86_SIB_SCALE_MASK    UINT8_C(0xc0)
#define X86_SIB_SCALE_SMASK   UINT8_C(0x03)
#define X86_SIB_SCALE_SHIFT   6
#ifndef VBOX_FOR_DTRACE_LIB
/** @def X86_SIB_MAKE
 * @param   a_BaseReg   The base register value (0..7).
 * @param   a_IndexReg  The index register value (0..7).
 * @param   a_Scale     The left shift (0..3) to be applied to the index
 *                      register (0 = none, 1 = x2, 2 = x4, 3 = x8).
 *  */
# define X86_SIB_MAKE(a_BaseReg, a_IndexReg, a_Scale) \
    (((a_Scale) << X86_SIB_SCALE_SHIFT) | ((a_IndexReg) << X86_SIB_INDEX_SHIFT) | (a_BaseReg))

AssertCompile((X86_SIB_BASE_MASK | X86_SIB_INDEX_MASK | X86_SIB_SCALE_MASK) == 0xff);
AssertCompile((X86_SIB_INDEX_MASK >> X86_SIB_INDEX_SHIFT) == X86_SIB_INDEX_SMASK);
AssertCompile((X86_SIB_SCALE_MASK >> X86_SIB_SCALE_SHIFT) == X86_SIB_SCALE_SMASK);
#endif
/** @} */

/** @name General register indexes.
 * @{ */
#define X86_GREG_xAX            0
#define X86_GREG_xCX            1
#define X86_GREG_xDX            2
#define X86_GREG_xBX            3
#define X86_GREG_xSP            4
#define X86_GREG_xBP            5
#define X86_GREG_xSI            6
#define X86_GREG_xDI            7
#define X86_GREG_x8             8
#define X86_GREG_x9             9
#define X86_GREG_x10            10
#define X86_GREG_x11            11
#define X86_GREG_x12            12
#define X86_GREG_x13            13
#define X86_GREG_x14            14
#define X86_GREG_x15            15
/** @} */
/** General register count. */
#define X86_GREG_COUNT          16

/** @name X86_SREG_XXX - Segment register indexes.
 * @{ */
#define X86_SREG_ES             0
#define X86_SREG_CS             1
#define X86_SREG_SS             2
#define X86_SREG_DS             3
#define X86_SREG_FS             4
#define X86_SREG_GS             5
/** @} */
/** Segment register count. */
#define X86_SREG_COUNT          6


/** @name X86_OP_XXX - Prefixes
 * @{ */
#define X86_OP_PRF_CS           UINT8_C(0x2e)
#define X86_OP_PRF_SS           UINT8_C(0x36)
#define X86_OP_PRF_DS           UINT8_C(0x3e)
#define X86_OP_PRF_ES           UINT8_C(0x26)
#define X86_OP_PRF_FS           UINT8_C(0x64)
#define X86_OP_PRF_GS           UINT8_C(0x65)
#define X86_OP_PRF_SIZE_OP      UINT8_C(0x66)
#define X86_OP_PRF_SIZE_ADDR    UINT8_C(0x67)
#define X86_OP_PRF_LOCK         UINT8_C(0xf0)
#define X86_OP_PRF_REPZ         UINT8_C(0xf3)
#define X86_OP_PRF_REPNZ        UINT8_C(0xf2)
#define X86_OP_REX              UINT8_C(0x40)
#define X86_OP_REX_B            UINT8_C(0x41)
#define X86_OP_REX_X            UINT8_C(0x42)
#define X86_OP_REX_R            UINT8_C(0x44)
#define X86_OP_REX_W            UINT8_C(0x48)
#define X86_OP_VEX3             UINT8_C(0xc4)
#define X86_OP_VEX2             UINT8_C(0xc5)
/** @} */

/** @name X86_OP_VEX2_XXX - 2-byte VEX prefix helpers.
 * @{ */
#define X86_OP_VEX2_BYTE1_P_MASK        0x3
# define X86_OP_VEX2_BYTE1_P_NO_PRF     0
# define X86_OP_VEX2_BYTE1_P_066H       1
# define X86_OP_VEX2_BYTE1_P_0F3H       2
# define X86_OP_VEX2_BYTE1_P_0F2H       3
#define X86_OP_VEX2_BYTE1_L             RT_BIT(2)
#define X86_OP_VEX2_BYTE1_VVVV_MASK     0x78
#define X86_OP_VEX2_BYTE1_VVVV_SHIFT    3
#define X86_OP_VEX2_BYTE1_VVVV_NONE     15
#define X86_OP_VEX2_BYTE1_R             RT_BIT(7)

#define X86_OP_VEX2_BYTE1_MAKE(a_fRegW, a_iSrcReg, a_f256BitAvx, a_fPrf) \
    (  ((a_fRegW) ? 0 : X86_OP_VEX2_BYTE1_R) \
     | (~((uint8_t)(a_iSrcReg) & 0xf) << X86_OP_VEX2_BYTE1_VVVV_SHIFT) \
     | ((a_f256BitAvx) ? X86_OP_VEX2_BYTE1_L : 0) \
     | ((a_fPrf) & X86_OP_VEX2_BYTE1_P_MASK))

#define X86_OP_VEX2_BYTE1_MAKE_NO_VVVV(a_fRegW, a_f256BitAvx, a_fPrf) \
    (  ((a_fRegW) ? 0 : X86_OP_VEX2_BYTE1_R) \
     | (X86_OP_VEX2_BYTE1_VVVV_NONE << X86_OP_VEX2_BYTE1_VVVV_SHIFT) \
     | ((a_f256BitAvx) ? X86_OP_VEX2_BYTE1_L : 0) \
     | ((a_fPrf) & X86_OP_VEX2_BYTE1_P_MASK))
/** @} */

/** @name X86_OP_VEX3_XXX - 3-byte VEX prefix helpers.
 * @{ */
#define X86_OP_VEX3_BYTE1_MAP_MASK      0x1f
#define X86_OP_VEX3_BYTE1_B             RT_BIT(5)
#define X86_OP_VEX3_BYTE1_X             RT_BIT(6)
#define X86_OP_VEX3_BYTE1_R             RT_BIT(7)
#define X86_OP_VEX3_BYTE1_MAKE(a_idxMap, a_B, a_X, a_R) \
     (  (uint8_t)(a_idxMap) \
      | ((a_B) ? 0 : X86_OP_VEX3_BYTE1_B) \
      | ((a_X) ? 0 : X86_OP_VEX3_BYTE1_X) \
      | ((a_R) ? 0 : X86_OP_VEX3_BYTE1_R))

#define X86_OP_VEX3_BYTE2_P_MASK        0x3
# define X86_OP_VEX3_BYTE2_P_NO_PRF     0
# define X86_OP_VEX3_BYTE2_P_066H       1
# define X86_OP_VEX3_BYTE2_P_0F3H       2
# define X86_OP_VEX3_BYTE2_P_0F2H       3
#define X86_OP_VEX3_BYTE2_L             RT_BIT(2)
#define X86_OP_VEX3_BYTE2_VVVV_MASK     0x78
#define X86_OP_VEX3_BYTE2_VVVV_SHIFT    3
#define X86_OP_VEX3_BYTE2_VVVV_NONE     15
#define X86_OP_VEX3_BYTE2_W             RT_BIT(7)

/** @todo r=bird: Is the '& UINT8_C(0xf)' bit needed? You mask it again after
 *        shifting. */
#define X86_OP_VEX3_BYTE2_MAKE(a_f64BitOpSize, a_iSrcReg, a_f256BitAvx, a_fPrf) \
    (  ((a_f64BitOpSize) ? X86_OP_VEX3_BYTE2_W : 0) \
     | ((~((uint8_t)(a_iSrcReg) & UINT8_C(0xf)) << X86_OP_VEX3_BYTE2_VVVV_SHIFT) & X86_OP_VEX3_BYTE2_VVVV_MASK) \
     | ((a_f256BitAvx) ? X86_OP_VEX3_BYTE2_L : 0) \
     | ((a_fPrf) & X86_OP_VEX3_BYTE2_P_MASK))

#define X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(a_f64BitOpSize, a_f256BitAvx, a_fPrf) \
    (  ((a_f64BitOpSize) ? X86_OP_VEX3_BYTE2_W : 0) \
     | (X86_OP_VEX3_BYTE2_VVVV_NONE << X86_OP_VEX3_BYTE2_VVVV_SHIFT) \
     | ((a_f256BitAvx) ? X86_OP_VEX3_BYTE2_L : 0) \
     | ((a_fPrf) & X86_OP_VEX3_BYTE2_P_MASK))
/** @} */

/** @} */

#endif /* !IPRT_INCLUDED_x86_h */

