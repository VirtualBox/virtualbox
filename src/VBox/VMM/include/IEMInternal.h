/* $Id: IEMInternal.h 110467 2025-07-30 08:31:12Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Internal header file.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_IEMInternal_h
#define VMM_INCLUDED_SRC_include_IEMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef RT_IN_ASSEMBLER
# include <VBox/vmm/cpum.h>
# include <VBox/vmm/iem.h>
# include <VBox/vmm/pgm.h>
# include <VBox/vmm/stam.h>
# include <VBox/param.h>

# include <iprt/setjmp-without-sigmask.h>
# include <iprt/list.h>
#endif /* !RT_IN_ASSEMBLER */


RT_C_DECLS_BEGIN


/** @defgroup grp_iem_int       Internals
 * @ingroup grp_iem
 * @internal
 * @{
 */

/* Make doxygen happy w/o overcomplicating the #if checks. */
#ifdef DOXYGEN_RUNNING
# define IEM_WITH_THROW_CATCH
# define VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
#endif

/** For expanding symbol in slickedit and other products tagging and
 *  crossreferencing IEM symbols. */
#ifndef IEM_STATIC
# define IEM_STATIC static
#endif

/** @def IEM_WITH_THROW_CATCH
 * Enables using C++ throw/catch as an alternative to setjmp/longjmp in user
 * mode code.
 *
 * With GCC 11.3.1 and code TLB on linux, using throw/catch instead of
 * setjmp/long resulted in bs2-test-1 running 3.00% faster and all but on test
 * result value improving by more than 1%. (Best out of three.)
 *
 * With Visual C++ 2019 and code TLB on windows, using throw/catch instead of
 * setjmp/long resulted in bs2-test-1 running 3.68% faster and all but some of
 * the MMIO and CPUID tests ran noticeably faster. Variation is greater than on
 * Linux, but it should be quite a bit faster for normal code.
 */
#if defined(__cplusplus) && defined(IN_RING3) && (defined(__GNUC__) || defined(_MSC_VER)) /* ASM-NOINC-START */
# define IEM_WITH_THROW_CATCH
#endif /*ASM-NOINC-END*/

/** @def IEM_WITH_ADAPTIVE_TIMER_POLLING
 * Enables the adaptive timer polling code.
 */
#if defined(DOXYGEN_RUNNING) || 1
# define IEM_WITH_ADAPTIVE_TIMER_POLLING
#endif

/** @def IEM_WITH_INTRA_TB_JUMPS
 * Enables loop-jumps within a TB (currently only to the first call).
 */
#if defined(DOXYGEN_RUNNING) || 1
# define IEM_WITH_INTRA_TB_JUMPS
#endif

/** @def IEMNATIVE_WITH_DELAYED_PC_UPDATING
 * Enables the delayed PC updating optimization (see @bugref{10373}).
 */
#if defined(DOXYGEN_RUNNING) || 1
# define IEMNATIVE_WITH_DELAYED_PC_UPDATING
#endif
/** @def IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
 * Enabled delayed PC updating debugging code.
 * This is an alternative to the ARM64-only IEMNATIVE_REG_FIXED_PC_DBG. */
#if defined(DOXYGEN_RUNNING) || 0
# define IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
#endif

/** Enables access to even callee saved registers. */
/*# define IEMNATIVE_WITH_SIMD_REG_ACCESS_ALL_REGISTERS*/

#if defined(DOXYGEN_RUNNING) || 1
/** @def IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
 * Delay the writeback or dirty registers as long as possible. */
# define IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
#endif

/** @def IEM_WITH_TLB_STATISTICS
 * Enables all TLB statistics. */
#if defined(VBOX_WITH_STATISTICS) || defined(DOXYGEN_RUNNING)
# define IEM_WITH_TLB_STATISTICS
#endif

/** @def IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
 * Enable this to use native emitters for certain SIMD FP operations. */
#if 1 || defined(DOXYGEN_RUNNING)
# define IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
#endif

/** @def VBOX_WITH_SAVE_THREADED_TBS_FOR_PROFILING
 * Enable this to create a saved state file with the threaded translation
 * blocks fed to the native recompiler on VCPU \#0.  The resulting file can
 * then be fed into the native recompiler for code profiling purposes.
 * This is not a feature that should be normally be enabled! */
#if 0 || defined(DOXYGEN_RUNNING)
# define VBOX_WITH_SAVE_THREADED_TBS_FOR_PROFILING
#endif

/** @def VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
 * Enables a quicker alternative to throw/longjmp for IEM_DO_LONGJMP when
 * executing native translation blocks.
 *
 * This exploits the fact that we save all non-volatile registers in the TB
 * prologue and thus just need to do the same as the TB epilogue to get the
 * effect of a longjmp/throw.  Since MSC marks XMM6 thru XMM15 as
 * non-volatile (and does something even more crazy for ARM), this probably
 * won't work reliably on Windows. */
#ifdef RT_ARCH_ARM64
# ifndef RT_OS_WINDOWS
#  define VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
# endif
#endif
/* ASM-NOINC-START */
#ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
# if !defined(IN_RING3) \
  || !defined(VBOX_WITH_IEM_RECOMPILER) \
  || !defined(VBOX_WITH_IEM_NATIVE_RECOMPILER)
#  undef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
# elif defined(RT_OS_WINDOWS)
#  pragma message("VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP is not safe to use on windows")
# endif
#endif


/** @def IEM_DO_LONGJMP
 *
 * Wrapper around longjmp / throw.
 *
 * @param   a_pVCpu     The CPU handle.
 * @param   a_rc        The status code jump back with / throw.
 */
#ifdef IEM_WITH_THROW_CATCH
# ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
#  define IEM_DO_LONGJMP(a_pVCpu, a_rc) do { \
            if ((a_pVCpu)->iem.s.pvTbFramePointerR3) \
                iemNativeTbLongJmp((a_pVCpu)->iem.s.pvTbFramePointerR3, (a_rc)); \
            throw int(a_rc); \
        } while (0)
# else
#  define IEM_DO_LONGJMP(a_pVCpu, a_rc) throw int(a_rc)
# endif
#else
# define IEM_DO_LONGJMP(a_pVCpu, a_rc)  longjmp(*(a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf), (a_rc))
#endif

/** For use with IEM function that may do a longjmp (when enabled).
 *
 * Visual C++ has trouble longjmp'ing from/over functions with the noexcept
 * attribute.  So, we indicate that function that may be part of a longjmp may
 * throw "exceptions" and that the compiler should definitely not generate and
 * std::terminate calling unwind code.
 *
 * Here is one example of this ending in std::terminate:
 * @code{.txt}
00 00000041`cadfda10 00007ffc`5d5a1f9f     ucrtbase!abort+0x4e
01 00000041`cadfda40 00007ffc`57af229a     ucrtbase!terminate+0x1f
02 00000041`cadfda70 00007ffb`eec91030     VCRUNTIME140!__std_terminate+0xa [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\ehhelpers.cpp @ 192]
03 00000041`cadfdaa0 00007ffb`eec92c6d     VCRUNTIME140_1!_CallSettingFrame+0x20 [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\amd64\handlers.asm @ 50]
04 00000041`cadfdad0 00007ffb`eec93ae5     VCRUNTIME140_1!__FrameHandler4::FrameUnwindToState+0x241 [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\frame.cpp @ 1085]
05 00000041`cadfdc00 00007ffb`eec92258     VCRUNTIME140_1!__FrameHandler4::FrameUnwindToEmptyState+0x2d [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\risctrnsctrl.cpp @ 218]
06 00000041`cadfdc30 00007ffb`eec940e9     VCRUNTIME140_1!__InternalCxxFrameHandler<__FrameHandler4>+0x194 [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\frame.cpp @ 304]
07 00000041`cadfdcd0 00007ffc`5f9f249f     VCRUNTIME140_1!__CxxFrameHandler4+0xa9 [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\risctrnsctrl.cpp @ 290]
08 00000041`cadfdd40 00007ffc`5f980939     ntdll!RtlpExecuteHandlerForUnwind+0xf
09 00000041`cadfdd70 00007ffc`5f9a0edd     ntdll!RtlUnwindEx+0x339
0a 00000041`cadfe490 00007ffc`57aff976     ntdll!RtlUnwind+0xcd
0b 00000041`cadfea00 00007ffb`e1b5de01     VCRUNTIME140!__longjmp_internal+0xe6 [d:\agent\_work\1\s\src\vctools\crt\vcruntime\src\eh\amd64\longjmp.asm @ 140]
0c (Inline Function) --------`--------     VBoxVMM!iemOpcodeGetNextU8SlowJmp+0x95 [L:\vbox-intern\src\VBox\VMM\VMMAll\IEMAll.cpp @ 1155]
0d 00000041`cadfea50 00007ffb`e1b60f6b     VBoxVMM!iemOpcodeGetNextU8Jmp+0xc1 [L:\vbox-intern\src\VBox\VMM\include\IEMInline.h @ 402]
0e 00000041`cadfea90 00007ffb`e1cc6201     VBoxVMM!IEMExecForExits+0xdb [L:\vbox-intern\src\VBox\VMM\VMMAll\IEMAll.cpp @ 10185]
0f 00000041`cadfec70 00007ffb`e1d0df8d     VBoxVMM!EMHistoryExec+0x4f1 [L:\vbox-intern\src\VBox\VMM\VMMAll\EMAll.cpp @ 452]
10 00000041`cadfed60 00007ffb`e1d0d4c0     VBoxVMM!nemR3WinHandleExitCpuId+0x79d [L:\vbox-intern\src\VBox\VMM\VMMAll\NEMAllNativeTemplate-win.cpp.h @ 1829]    @encode
   @endcode
 *
 * @see https://developercommunity.visualstudio.com/t/fragile-behavior-of-longjmp-called-from-noexcept-f/1532859
 */
#if defined(_MSC_VER) || defined(IEM_WITH_THROW_CATCH)
# define IEM_NOEXCEPT_MAY_LONGJMP           RT_NOEXCEPT_EX(false)
# if RT_CPLUSPLUS_PREREQ(201700)
#  define IEM_NOEXCEPT_MAY_LONGJMP_TYPEDEF  RT_NOEXCEPT_EX(false)
# else
#  define IEM_NOEXCEPT_MAY_LONGJMP_TYPEDEF
# endif
#else
# define IEM_NOEXCEPT_MAY_LONGJMP           RT_NOEXCEPT
# if RT_CPLUSPLUS_PREREQ(201700)
#  define IEM_NOEXCEPT_MAY_LONGJMP_TYPEDEF  RT_NOEXCEPT
# else
#  define IEM_NOEXCEPT_MAY_LONGJMP_TYPEDEF
# endif
#endif
/* ASM-NOINC-END */


//#define IEM_WITH_CODE_TLB // - work in progress
//#define IEM_WITH_DATA_TLB // - work in progress


/** @def IEM_USE_UNALIGNED_DATA_ACCESS
 * Use unaligned accesses instead of elaborate byte assembly. */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(DOXYGEN_RUNNING)  /*ASM-NOINC*/
# define IEM_USE_UNALIGNED_DATA_ACCESS
#endif                                                                          /*ASM-NOINC*/

//#define IEM_LOG_MEMORY_WRITES


/** @def IEM_CFG_TARGET_CPU
 * The minimum target CPU for the IEM emulation (IEMTARGETCPU_XXX value).
 *
 * By default we allow this to be configured by the user via the
 * CPUM/GuestCpuName config string, but this comes at a slight cost during
 * decoding.  So, for applications of this code where there is no need to
 * be dynamic wrt target CPU, just modify this define.
 */
#if !defined(IEM_CFG_TARGET_CPU) || defined(DOXYGEN_RUNNING)
# define IEM_CFG_TARGET_CPU     IEMTARGETCPU_DYNAMIC
#endif


/*
 * X86 config.
 */

#define IEM_IMPLEMENTS_TASKSWITCH

/** @def IEM_WITH_3DNOW
 * Includes the 3DNow decoding.  */
#if !defined(IEM_WITH_3DNOW) || defined(DOXYGEN_RUNNING)   /* For doxygen, set in Config.kmk. */
# ifndef IEM_WITHOUT_3DNOW
#  define IEM_WITH_3DNOW
# endif
#endif

/** @def IEM_WITH_THREE_0F_38
 * Includes the three byte opcode map for instrs starting with 0x0f 0x38. */
#if !defined(IEM_WITH_THREE_0F_38) || defined(DOXYGEN_RUNNING) /* For doxygen, set in Config.kmk. */
# ifndef IEM_WITHOUT_THREE_0F_38
#  define IEM_WITH_THREE_0F_38
# endif
#endif

/** @def IEM_WITH_THREE_0F_3A
 * Includes the three byte opcode map for instrs starting with 0x0f 0x38. */
#if !defined(IEM_WITH_THREE_0F_3A) || defined(DOXYGEN_RUNNING) /* For doxygen, set in Config.kmk. */
# ifndef IEM_WITHOUT_THREE_0F_3A
#  define IEM_WITH_THREE_0F_3A
# endif
#endif

/** @def IEM_WITH_VEX
 * Includes the VEX decoding. */
#if !defined(IEM_WITH_VEX) || defined(DOXYGEN_RUNNING)       /* For doxygen, set in Config.kmk. */
# ifndef IEM_WITHOUT_VEX
#  define IEM_WITH_VEX
# endif
#endif


#ifndef RT_IN_ASSEMBLER /* ASM-NOINC-START - the rest of the file */

# if !defined(IEM_WITHOUT_INSTRUCTION_STATS) && !defined(DOXYGEN_RUNNING)
/** Instruction statistics.   */
typedef struct IEMINSTRSTATS
{
# define IEM_DO_INSTR_STAT(a_Name, a_szDesc) uint32_t a_Name;
# include "IEMInstructionStatisticsTmpl.h"
# undef IEM_DO_INSTR_STAT
} IEMINSTRSTATS;
#else
struct IEMINSTRSTATS;
typedef struct IEMINSTRSTATS IEMINSTRSTATS;
#endif
/** Pointer to IEM instruction statistics. */
typedef IEMINSTRSTATS *PIEMINSTRSTATS;


/** @name IEMTARGETCPU_EFL_BEHAVIOR_XXX - IEMCPU::aidxTargetCpuEflFlavour
 * @{ */
#define IEMTARGETCPU_EFL_BEHAVIOR_NATIVE      0     /**< Native x86 EFLAGS result; Intel EFLAGS when on non-x86 hosts. */
#define IEMTARGETCPU_EFL_BEHAVIOR_INTEL       1     /**< Intel EFLAGS result. */
#define IEMTARGETCPU_EFL_BEHAVIOR_AMD         2     /**< AMD EFLAGS result */
#define IEMTARGETCPU_EFL_BEHAVIOR_RESERVED    3     /**< Reserved/dummy entry slot that's the same as 0. */
#define IEMTARGETCPU_EFL_BEHAVIOR_MASK        3     /**< For masking the index before use. */
/** Selects the right variant from a_aArray.
 * pVCpu is implicit in the caller context. */
#define IEMTARGETCPU_EFL_BEHAVIOR_SELECT(a_aArray) \
    (a_aArray[pVCpu->iem.s.aidxTargetCpuEflFlavour[1] & IEMTARGETCPU_EFL_BEHAVIOR_MASK])
/** Variation of IEMTARGETCPU_EFL_BEHAVIOR_SELECT for when no native worker can
 * be used because the host CPU does not support the operation. */
#define IEMTARGETCPU_EFL_BEHAVIOR_SELECT_NON_NATIVE(a_aArray) \
    (a_aArray[pVCpu->iem.s.aidxTargetCpuEflFlavour[0] & IEMTARGETCPU_EFL_BEHAVIOR_MASK])
/** Variation of IEMTARGETCPU_EFL_BEHAVIOR_SELECT for a two dimentional
 *  array paralleling IEMCPU::aidxTargetCpuEflFlavour and a single bit index
 *  into the two.
 * @sa IEM_SELECT_NATIVE_OR_FALLBACK */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# define IEMTARGETCPU_EFL_BEHAVIOR_SELECT_EX(a_aaArray, a_fNative) \
    (a_aaArray[a_fNative][pVCpu->iem.s.aidxTargetCpuEflFlavour[a_fNative] & IEMTARGETCPU_EFL_BEHAVIOR_MASK])
#else
# define IEMTARGETCPU_EFL_BEHAVIOR_SELECT_EX(a_aaArray, a_fNative) \
    (a_aaArray[0][pVCpu->iem.s.aidxTargetCpuEflFlavour[0] & IEMTARGETCPU_EFL_BEHAVIOR_MASK])
#endif
/** @} */

/**
 * Picks @a a_pfnNative or @a a_pfnFallback according to the host CPU feature
 * indicator given by @a a_fCpumFeatureMember (CPUMFEATURES member).
 *
 * On non-x86 hosts, this will shortcut to the fallback w/o checking the
 * indicator.
 *
 * @sa IEMTARGETCPU_EFL_BEHAVIOR_SELECT_EX
 */
#if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
# define IEM_SELECT_HOST_OR_FALLBACK(a_fCpumFeatureMember, a_pfnNative, a_pfnFallback) \
    (g_CpumHostFeatures.s.a_fCpumFeatureMember ? a_pfnNative : a_pfnFallback)
#else
# define IEM_SELECT_HOST_OR_FALLBACK(a_fCpumFeatureMember, a_pfnNative, a_pfnFallback) (a_pfnFallback)
#endif

/** @name Helpers for passing C++ template arguments to an
 *        IEM_MC_NATIVE_EMIT_3/4/5 style macro.
 * @{
 */
#define IEM_TEMPL_ARG_1(a1)             <a1>
#define IEM_TEMPL_ARG_2(a1, a2)         <a1,a2>
#define IEM_TEMPL_ARG_3(a1, a2, a3)     <a1,a2,a3>
/** @} */


/**
 * IEM TLB entry.
 *
 * Lookup assembly:
 * @code{.asm}
        ; Calculate tag.
        mov     rax, [VA]
        shl     rax, 16
        shr     rax, 16 + X86_PAGE_SHIFT
        or      rax, [uTlbRevision]

        ; Do indexing.
        movzx   ecx, al
        lea     rcx, [pTlbEntries + rcx]

        ; Check tag.
        cmp     [rcx + IEMTLBENTRY.uTag], rax
        jne     .TlbMiss

        ; Check access.
        mov     rax, ACCESS_FLAGS | MAPPING_R3_NOT_VALID | 0xffffff00
        and     rax, [rcx + IEMTLBENTRY.fFlagsAndPhysRev]
        cmp     rax, [uTlbPhysRev]
        jne     .TlbMiss

        ; Calc address and we're done.
        mov     eax, X86_PAGE_OFFSET_MASK
        and     eax, [VA]
        or      rax, [rcx + IEMTLBENTRY.pMappingR3]
    %ifdef VBOX_WITH_STATISTICS
        inc     qword [cTlbHits]
    %endif
        jmp     .Done

    .TlbMiss:
        mov     r8d, ACCESS_FLAGS
        mov     rdx, [VA]
        mov     rcx, [pVCpu]
        call    iemTlbTypeMiss
    .Done:

   @endcode
 *
 */
typedef struct IEMTLBENTRY
{
    /** The TLB entry tag.
     * Bits 35 thru 0 are made up of the virtual address shifted right 12 bits, this
     * is ASSUMING a virtual address width of 48 bits.
     *
     * Bits 63 thru 36 are made up of the TLB revision (zero means invalid).
     *
     * The TLB lookup code uses the current TLB revision, which won't ever be zero,
     * enabling an extremely cheap TLB invalidation most of the time.  When the TLB
     * revision wraps around though, the tags needs to be zeroed.
     *
     * @note    Try use SHRD instruction?  After seeing
     *          https://gmplib.org/~tege/x86-timing.pdf, maybe not.
     *
     * @todo    This will need to be reorganized for 57-bit wide virtual address and
     *          PCID (currently 12 bits) and ASID (currently 6 bits) support.  We'll
     *          have to move the TLB entry versioning entirely to the
     *          fFlagsAndPhysRev member then, 57 bit wide VAs means we'll only have
     *          19 bits left (64 - 57 + 12 = 19) and they'll almost entire be
     *          consumed by PCID and ASID (12 + 6 = 18).
     *          Update: Put the PCID + ASID in fFlagsAndPhysRev; that doesn't solve
     *          the 57-bit problem, though.
     */
    uint64_t                uTag;
    /** Access flags and physical TLB revision.
     *
     * @x86
     * @parblock
     * - Bit  0 - page tables   - not executable (X86_PTE_PAE_NX).
     * - Bit  1 - page tables   - not writable (complemented X86_PTE_RW).
     * - Bit  2 - page tables   - not user (complemented X86_PTE_US).
     * - Bit  3 - pgm phys/virt - not directly writable.
     * - Bit  4 - pgm phys page - not directly readable.
     * - Bit  5 - page tables   - not accessed (complemented X86_PTE_A).
     * - Bit  6 - page tables   - not dirty (complemented X86_PTE_D).
     * - Bit  7 - page tables   - large page.
     * - Bit  8 - tlb entry     - pMappingR3 member not valid.
     * - Bit  9 - phys          - Unassigned memory.
     * - Bit 10 - phys          - Code page.
     * - Bits 63:11 - phys      - Physical TLB revision number.
     *
     * We're using complemented bit meanings here because it makes it easy to check
     * whether special action is required.  For instance a user mode write access
     * would do a "TEST fFlags, (X86_PTE_RW | X86_PTE_US | X86_PTE_D)" and a
     * non-zero result would mean special handling needed because either it wasn't
     * writable, or it wasn't user, or the page wasn't dirty.  A user mode read
     * access would do "TEST fFlags, X86_PTE_US"; and a kernel mode read wouldn't
     * need to check any PTE flag.
     * @endparblock
     *
     * @arm
     * @parblock
     * update this:
     * - Bit  0 - stage 1+2       - not unprivileged read accessible.
     * - Bit  1 - stage 1+2       - not unprivileged write accessible.
     * - Bit  2 - stage 1+2       - not unprivileged execute accessible.
     * - Bit  3 - stage 1+2       - not unprivileged guarded control stack accessible.
     * - Bit  4 - stage 1+2       - not privileged readable accessible.
     * - Bit  5 - stage 1+2       - not privileged writable accessible.
     * - Bit  6 - stage 1+2       - not privileged executable accessible.
     * - Bit  7 - stage 1+2       - not privileged guarded control stack accessible.
     * - Bit  8 - stage 2         - no limited write access (?).
     * - Bit  9 - stage 2         - TopLevel0 (?)
     * - Bit 10 - stage 2         - TopLevel1 (?)
     * - Bit 11 - stage 1+2 leaf  - not dirty.
     * - Bit 12 - stage 1+2 leaf  - alternate MECID (AMEC).
     * - Bit 13 - pgm phys page   - not directly readable.
     * - Bit 14 - pgm phys/virt   - not directly writable.
     * - Bit 15 - tlb entry       - pMappingR3 member not valid.
     * - Bit 16 - phys            - Unassigned memory.
     * - Bit 17 - phys            - Code page.
     * - Bit 18 - stage 1 leaf    - NS (not-secure).
     * - Bit 19 - stage 1 leaf    - NSE (root).
     * - Bit 20 - stage 1+2       - Device memory.
     * - Bit 21 - stage 1         - Guarded page (FEAT_BTI).
     * - Bit 22 - stage 1+2       - TODO: Page size (?).
     * - Bits 37:22 - stage 1 reg - Address space ID (ASID).
     * - Bits 53:38 - stage 2 reg - Virtual Machine ID (VMID).
     * - Bits 63:54 - tlb entry   - physical TLB revision number.
     *
     * The ASIDs and VMIDs are kept with the physical TLB revision number, so
     * there is no extra overhead there.  How the NSE:NS stuff will be handled
     * is a question for later.
     *
     * The above is a preliminary sketch...
     * @endparblock
     *
     * @todo arm64: Not sure if we can combine the stage 1 and 2 AMEC bits,
     *       but hope so... Doubt we'll be needing this any time soon.
     */
    uint64_t                fFlagsAndPhysRev;
    /** The guest physical page address.
     * @todo ARM: We'll have to put more stuff here on ARM.  Anything that is
     *       specific to TLB invalidation, like TTL, page size, etc.  The lower 12
     *       bits are certainly unused.  Max address space size defined by the
     *       current specification is 56 bits (AMD64: 52 bits IIRC), though I expect
     *       implementations with this limit won't be seen for a long long time,
     *       given that the M2&M3 only did 42 bits, Snapdragon X 44 bits and
     *       Neoverse V3 is 48 bits.  So, assuming that we had 52 bits like on
     *       AMD64, probably wouldn't cause trouble for a long while.  That would
     *       yield us 24 bits in total if we need then.  */
    uint64_t                GCPhys;
    /** Pointer to the ring-3 mapping. */
    R3PTRTYPE(uint8_t *)    pbMappingR3;
#if HC_ARCH_BITS == 32
    uint32_t                u32Padding1;
#endif
} IEMTLBENTRY;
AssertCompileSize(IEMTLBENTRY, 32);
/** Pointer to an IEM TLB entry. */
typedef IEMTLBENTRY *PIEMTLBENTRY;
/** Pointer to a const IEM TLB entry. */
typedef IEMTLBENTRY const *PCIEMTLBENTRY;

/** @name IEMTLBE_F_XXX - TLB entry flags (IEMTLBENTRY::fFlagsAndPhysRev)
 * @{  */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEMTLBE_F_PT_NO_EXEC       RT_BIT_64(0)  /**< Page tables: Not executable. */
# define IEMTLBE_F_PT_NO_WRITE      RT_BIT_64(1)  /**< Page tables: Not writable. */
# define IEMTLBE_F_PT_NO_USER       RT_BIT_64(2)  /**< Page tables: Not user accessible (supervisor only). */
# define IEMTLBE_F_PG_NO_WRITE      RT_BIT_64(3)  /**< Phys page:   Not writable (access handler, ROM, whatever). */
# define IEMTLBE_F_PG_NO_READ       RT_BIT_64(4)  /**< Phys page:   Not readable (MMIO / access handler, ROM) */
# define IEMTLBE_F_PT_NO_ACCESSED   RT_BIT_64(5)  /**< Phys tables: Not accessed (need to be marked accessed). */
# define IEMTLBE_F_PT_NO_DIRTY      RT_BIT_64(6)  /**< Page tables: Not dirty (needs to be made dirty on write). */
# define IEMTLBE_F_PT_LARGE_PAGE    RT_BIT_64(7)  /**< Page tables: Large 2 or 4 MiB page (for flushing). */
# define IEMTLBE_F_NO_MAPPINGR3     RT_BIT_64(8)  /**< TLB entry:   The IEMTLBENTRY::pMappingR3 member is invalid. */
# define IEMTLBE_F_PG_UNASSIGNED    RT_BIT_64(9)  /**< Phys page:   Unassigned memory (not RAM, ROM, MMIO2 or MMIO). */
# define IEMTLBE_F_PG_CODE_PAGE     RT_BIT_64(10) /**< Phys page:   Code page. */
# define IEMTLBE_F_PHYS_REV         UINT64_C(0xfffffffffffff800) /**< Physical revision mask. @sa IEMTLB_PHYS_REV_INCR */
#endif
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(DOXYGEN_RUNNING)
/** Stage 1+2: No unprivileged read access. */
# define IEMTLBE_F_EFF_P_NO_READ_BIT        0
# define IEMTLBE_F_EFF_P_NO_READ            RT_BIT_64(IEMTLBE_F_EFF_P_NO_READ_BIT)
/** Stage 1+2: No privileged write access. */
# define IEMTLBE_F_EFF_P_NO_WRITE_BIT       1
# define IEMTLBE_F_EFF_P_NO_WRITE           RT_BIT_64(IEMTLBE_F_EFF_P_NO_WRITE_BIT)
/** Stage 1+2: No privileged execute access. */
# define IEMTLBE_F_EFF_P_NO_EXEC_BIT        2
# define IEMTLBE_F_EFF_P_NO_EXEC            RT_BIT_64(IEMTLBE_F_EFF_P_NO_EXEC_BIT)
/** Stage 1+2: No privileged guard control stack access. */
# define IEMTLBE_F_EFF_P_NO_GCS_BIT         3
# define IEMTLBE_F_EFF_P_NO_GCS             RT_BIT_64(IEMTLBE_F_EFF_P_NO_GCS_BIT)
/** Stage 1+2: No unprivileged read access. */
# define IEMTLBE_F_EFF_U_NO_READ_BIT        4
# define IEMTLBE_F_EFF_U_NO_READ            RT_BIT_64(IEMTLBE_F_EFF_U_NO_READ_BIT)
/** Stage 1+2: No unprivileged write access. */
# define IEMTLBE_F_EFF_U_NO_WRITE_BIT       5
# define IEMTLBE_F_EFF_U_NO_WRITE           RT_BIT_64(IEMTLBE_F_EFF_U_NO_WRITE_BIT)
/** Stage 1+2: No unprivileged execute access. */
# define IEMTLBE_F_EFF_U_NO_EXEC_BIT        6
# define IEMTLBE_F_EFF_U_NO_EXEC            RT_BIT_64(IEMTLBE_F_EFF_U_NO_EXEC_BIT)
/** Stage 1+2: No unprivileged guard control stack access. */
# define IEMTLBE_F_EFF_U_NO_GCS_BIT         7
# define IEMTLBE_F_EFF_U_NO_GCS             RT_BIT_64(IEMTLBE_F_EFF_U_NO_GCS_BIT)
/** Stage 1+2: Translation regime (IEM_ARM_REGIME_XXX). */
# define IEMTLBE_F_REGIME_MASK              UINT64_C(0xf00)
# define IEMTLBE_F_REGIME_SHIFT             8
/** Stage 1+2: Not dirty. */
# define IEMTLBE_F_EFF_NO_DIRTY_BIT         12
# define IEMTLBE_F_EFF_NO_DIRTY             RT_BIT_64(IEMTLBE_F_EFF_NO_DIRTY_BIT)
/** Stage 1+2: Alternative MECID. */
# define IEMTLBE_F_EFF_AMEC_BIT             13
# define IEMTLBE_F_EFF_AMEC                 RT_BIT_64(IEMTLBE_F_EFF_AMEC_BIT)
/** Stage 1+2: Device memory type (clear if normal memory type). */
# define IEMTLBE_F_EFF_DEVICE_BIT           14
# define IEMTLBE_F_EFF_DEVICE               RT_BIT_64(IEMTLBE_F_EFF_DEVICE_BIT)
/** Phys page: Not readable (MMIO / access handler, ROM) */
# define IEMTLBE_F_PG_NO_READ_BIT           15
# define IEMTLBE_F_PG_NO_READ               RT_BIT_64(IEMTLBE_F_PG_NO_READ_BIT)
/** Phys page: Not writable (access handler, ROM, whatever). */
# define IEMTLBE_F_PG_NO_WRITE_BIT          16
# define IEMTLBE_F_PG_NO_WRITE              RT_BIT_64(IEMTLBE_F_PG_NO_WRITE_BIT)
/** TLB entry: The IEMTLBENTRY::pMappingR3 member is invalid. */
# define IEMTLBE_F_NO_MAPPINGR3_BIT         17
# define IEMTLBE_F_NO_MAPPINGR3             RT_BIT_64(IEMTLBE_F_NO_MAPPINGR3_BIT)
/** Phys page: Unassigned memory (not RAM, ROM, MMIO2 or MMIO). */
# define IEMTLBE_F_PG_UNASSIGNED_BIT        18
# define IEMTLBE_F_PG_UNASSIGNED            RT_BIT_64(IEMTLBE_F_PG_UNASSIGNED_BIT)
/** Phys page: Code page. */
# define IEMTLBE_F_PG_CODE_PAGE_BIT         19
# define IEMTLBE_F_PG_CODE_PAGE             RT_BIT_64(IEMTLBE_F_PG_CODE_PAGE_BIT)
/** Stage 1:   Guarded page - branch target identification (FEAT_BTI) related. */
# define IEMTLBE_F_GP_BIT                   20
# define IEMTLBE_F_GP                       RT_BIT_64(IEMTLBE_F_GP_BIT)
/** Stage 1:   Not-global page - ignore ASID if clear. */
# define IEMTLBE_F_NG_BIT                   21
# define IEMTLBE_F_NG                       RT_BIT_64(IEMTLBE_F_NG_BIT)
/** Stage 1:   Address space ID (from stage 1 root register). */
# define IEMTLBE_F_S1_ASID                  (UINT64_C(0xffff) << IEMTLBE_F_S1_ASID_SHIFT)
/** @see IEMTLBE_F_S1_ASID */
# define IEMTLBE_F_S1_ASID_SHIFT            22
/** Stage 2:   Virtual machine ID (from stage 2 root register). */
# define IEMTLBE_F_S2_VMID                  (UINT64_C(0xffff) << IEMTLBE_F_S2_VMID_SHIFT)
/** @see IEMTLBE_F_S2_VMID */
# define IEMTLBE_F_S2_VMID_SHIFT            38
# ifndef DOXYGEN_RUNNING
/** Physical revision mask. @sa IEMTLB_PHYS_REV_INCR */
#  define IEMTLBE_F_PHYS_REV                UINT64_C(0xffc0000000000000)
# endif

# if 0 /* We probably won't need these. Or if we do, put them in GCPhys? */
/** Stage 2:   No limited write access. */
# define IEMTLBE_F_S2_NO_LIM_WRITE_BIT      8
# define IEMTLBE_F_S2_NO_LIM_WRITE          RT_BIT_64(IEMTLBE_F_S2_NO_LIM_WRITE_BIT)
/** Stage 2:   TopLevel0. */
# define IEMTLBE_F_S2_TL0_BIT               9
# define IEMTLBE_F_S2_TL0                   RT_BIT_64(IEMTLBE_F_S2_TL0_BIT)
/** Stage 2:   TopLevel1. */
# define IEMTLBE_F_S2_TL1_BIT               10
# define IEMTLBE_F_S2_TL1                   RT_BIT_64(IEMTLBE_F_S2_TL1_BIT)
# endif
#endif
/** @} */
/** The bits set by PGMPhysIemGCPhys2PtrNoLock. */
#define IEMTLBE_GCPHYS2PTR_MASK     (  PGMIEMGCPHYS2PTR_F_NO_WRITE \
                                     | PGMIEMGCPHYS2PTR_F_NO_READ \
                                     | PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 \
                                     | PGMIEMGCPHYS2PTR_F_UNASSIGNED \
                                     | PGMIEMGCPHYS2PTR_F_CODE_PAGE \
                                     | IEMTLBE_F_PHYS_REV )
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_ARMV8)
AssertCompile(PGMIEMGCPHYS2PTR_F_NO_WRITE     == IEMTLBE_F_PG_NO_WRITE);
AssertCompile(PGMIEMGCPHYS2PTR_F_NO_READ      == IEMTLBE_F_PG_NO_READ);
AssertCompile(PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 == IEMTLBE_F_NO_MAPPINGR3);
AssertCompile(PGMIEMGCPHYS2PTR_F_UNASSIGNED   == IEMTLBE_F_PG_UNASSIGNED);
AssertCompile(PGMIEMGCPHYS2PTR_F_CODE_PAGE    == IEMTLBE_F_PG_CODE_PAGE);
# ifdef VBOX_VMM_TARGET_X86
AssertCompile(PGM_WALKINFO_BIG_PAGE           == IEMTLBE_F_PT_LARGE_PAGE);
# endif
#endif

/** @name IEMTLBE_GCPHYS_F_XXX - TLB entry GCPhys flags (IEMTLBENTRY::GCPhys)
 * @{ */
/** The physical address mask. */
#define IEMTLBE_GCPHYS_F_PHYS_MASK          UINT64_C(0x000ffffffffff000)
AssertCompile(((RT_BIT_64(52) - 1) & ~(uint64_t)GUEST_MIN_PAGE_OFFSET_MASK) == IEMTLBE_GCPHYS_F_PHYS_MASK);
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(DOXYGEN_RUNNING)
/** ARM: The translation level (TTL).
 * Required for abort codes and TLBI filtering (follows TLBI TTL[1:0] encoding). */
# define IEMTLBE_GCPHYS_F_TTL_MASK          UINT64_C(0x0000000000000003)
# define IEMTLBE_GCPHYS_F_TTL_SHIFT         UINT64_C(0x0000000000000000)
# define IEMTLBE_GCPHYS_F_TTL_LEVEL1        UINT64_C(0x0000000000000001)
# define IEMTLBE_GCPHYS_F_TTL_LEVEL2        UINT64_C(0x0000000000000002)
# define IEMTLBE_GCPHYS_F_TTL_LEVEL3        UINT64_C(0x0000000000000003)
/** ARM: The granule size.
 * This is required for TLBI filter (follows TLBI TTL[3:2]/TG encoding). */
# define IEMTLBE_GCPHYS_F_GRANULE_MASK      UINT64_C(0x000000000000000c)
# define IEMTLBE_GCPHYS_F_GRANULE_SHIFT     2
# define IEMTLBE_GCPHYS_F_GRANULE_4K        UINT64_C(0x0000000000000004)
# define IEMTLBE_GCPHYS_F_GRANULE_16K       UINT64_C(0x0000000000000008)
# define IEMTLBE_GCPHYS_F_GRANULE_64K       UINT64_C(0x000000000000000c)
/** ARM: The NS bit (stage 1).
 * This is for filtering purposes for TLBI instruction with an NS field. */
# define IEMTLBE_GCPHYS_F_NS                UINT64_C(0x0000000000000010)
# define IEMTLBE_GCPHYS_F_NS_BIT            4
/** ARM: The NSE bit (stage 1).
 * This is for filtering purposes for TLBI instruction with an NS field. */
# define IEMTLBE_GCPHYS_F_NSE               UINT64_C(0x0000000000000020)
# define IEMTLBE_GCPHYS_F_NSE_BIT           5
#endif
/** @} */

/** The TLB size (power of two).
 * We initially chose 256 because that way we can obtain the result directly
 * from a 8-bit register without an additional AND instruction.
 * See also @bugref{10687}. */
#if defined(RT_ARCH_AMD64)
# define IEMTLB_ENTRY_COUNT                     256
# define IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO     8
#else
# define IEMTLB_ENTRY_COUNT                     8192
# define IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO     13
#endif
AssertCompile(RT_BIT_32(IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO) == IEMTLB_ENTRY_COUNT);

/** The TLB size factor.
 * This is two if we use the even/odd approach to non-global/global entries,
 * otherwise it'll be one. */
#ifdef VBOX_VMM_TARGET_X86
# define IEMTLB_ENTRY_COUNT_FACTOR              2
#else
# define IEMTLB_ENTRY_COUNT_FACTOR              1
#endif

/** TLB slot format spec (assumes uint32_t or unsigned value). */
#if IEMTLB_ENTRY_COUNT * IEMTLB_ENTRY_COUNT_FACTOR <= 0x100
# define IEMTLB_SLOT_FMT    "%02x"
#elif IEMTLB_ENTRY_COUNT * IEMTLB_ENTRY_COUNT_FACTOR <= 0x1000
# define IEMTLB_SLOT_FMT    "%03x"
#elif IEMTLB_ENTRY_COUNT * IEMTLB_ENTRY_COUNT_FACTOR <= 0x10000
# define IEMTLB_SLOT_FMT    "%04x"
#else
# define IEMTLB_SLOT_FMT    "%05x"
#endif

/** Enable the large page bitmap TLB optimization.
 *
 * The idea here is to avoid scanning the full 32 KB (2MB pages, 2*512 TLB
 * entries) or 64 KB (4MB pages, 2*1024 TLB entries) worth of TLB entries during
 * invlpg when large pages are used, and instead just scan 128 or 256 bytes of
 * the bmLargePage bitmap to determin which TLB entires that might be containing
 * large pages and actually require checking.
 *
 * There is a good posibility of false positives since we currently don't clear
 * the bitmap when flushing the TLB, but it should help reduce the workload when
 * the large pages aren't fully loaded into the TLB in their entirity...
 */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEMTLB_WITH_LARGE_PAGE_BITMAP
#endif

#if (IEMTLB_ENTRY_COUNT > 1 && defined(VBOX_VMM_TARGET_X86)) || defined(DOXYGEN_RUNNING)
/** Tests if the TLB entry is global (odd). */
# define IEMTLBE_IS_GLOBAL(a_pTlbe)      (((uintptr_t)(a_pTlbe) / sizeof(IEMTLBENTRY)) & 1)
#endif

#if defined(VBOX_VMM_TARGET_ARMV8) || defined(DOXYGEN_RUNNING)
/** The 'stuff' bits in uTlbPhysRevAndStuff0 and uTlbPhysRevAndStuff1.
 *
 * These are copies of translation regime, ASID and VMID as well as a
 * permantently set IEMTLBE_F_NG bit.  This reduces the fetches and compares
 * necessary to check whether a TLB entry matches or not. */
# define IEMTLB_STUFF_BITS  (IEMTLBE_F_REGIME_MASK | IEMTLBE_F_NG | IEMTLBE_F_S2_VMID | IEMTLBE_F_S1_ASID)
#endif


/**
 * An IEM TLB.
 *
 * We've got two of these, one for data and one for instructions.
 */
typedef struct IEMTLB
{
#ifdef VBOX_VMM_TARGET_ARMV8
    /** Same as uTlbPhysRevAndStuff1, but with the ASID for the 2nd translation root
     *  (i.e. negative addresses).
     * @note This is placed before uTlbRevision, so we can use a load pair on
     *       ARM. */
    uint64_t            uTlbPhysRevAndStuff0;
#endif
    /** The non-global TLB revision.
     * This is actually only 28 bits wide (see IEMTLBENTRY::uTag) and is incremented
     * by adding RT_BIT_64(36) to it.  When it wraps around and becomes zero, all
     * the tags in the TLB must be zeroed and the revision set to RT_BIT_64(36).
     * (The revision zero indicates an invalid TLB entry.)
     *
     * The initial value is choosen to cause an early wraparound. */
    uint64_t            uTlbRevision;
    /** The TLB physical address revision - shadow of PGM variable.
     *
     * The revision number is the top x bits (target dependent, see
     * IEMTLBENTRY::fFlagsAndPhysRev) and is incremented by adding RT_BIT_64(x).
     * When it wraps around and becomes zero, a rendezvous is called and each CPU
     * wipe the IEMTLBENTRY::pMappingR3 as well as many of the
     * IEMTLBENTRY::fFlagsAndPhysRev bits.
     *
     * The initial value is choosen to cause an early wraparound.
     *
     * @arm  This includes the current translation regime value, IEMTLBE_F_NG, ASID,
     *       and VMID. The ASID is for the first translation root (i.e.
     *       positive addresses).
     *
     * @note This is placed between the two TLB revisions when targeting x86 because
     *       we load it in pair with one or the other on arm64.  While for arm
     *       targets, we put uTlbRevision in the middle since we may have two
     *       different ASID values there (soon).
     * @todo Why is this volatile again?
     */
#ifdef VBOX_VMM_TARGET_ARMV8
    uint64_t            uTlbPhysRevAndStuff1;
#else
    uint64_t volatile   uTlbPhysRev;
    /** The global TLB revision.
     * Same as uTlbRevision, but only increased for global flushes. */
    uint64_t            uTlbRevisionGlobal;
#endif

    /** Large page tag range.
     *
     * This is used to avoid scanning a large page's worth of TLB entries for each
     * INVLPG instruction, and only to do so iff we've loaded any and when the
     * address is in this range.  This is kept up to date when we loading new TLB
     * entries.
     */
    struct LARGEPAGERANGE
    {
        /** The lowest large page address tag, UINT64_MAX if none. */
        uint64_t        uFirstTag;
        /** The highest large page address tag (with offset mask part set), 0 if none. */
        uint64_t        uLastTag;
    }
#ifdef VBOX_VMM_TARGET_X86
    /** Large page range for non-global pages. */
                        NonGlobalLargePageRange,
    /** Large page range for global pages. */
                        GlobalLargePageRange;
#else
    /** Large page range. */
                        LargePageRange;
#endif
#ifdef VBOX_VMM_TARGET_X86
    /** Number of non-global entries for large pages loaded since last TLB flush. */
    uint32_t            cTlbNonGlobalLargePageCurLoads;
    /** Number of global entries for large pages loaded since last TLB flush. */
    uint32_t            cTlbGlobalLargePageCurLoads;
#else
    /** Number of entries for large pages loaded since last TLB flush. */
    uint32_t            cTlbLargePageCurLoads;
    uint32_t            uPadding;
#endif

    /* Statistics: */

    /** TLB hits in IEMAll.cpp code (IEM_WITH_TLB_STATISTICS only; both).
     * @note For the data TLB this is only used in iemMemMap and and for direct (i.e.
     *       not via safe read/write path) calls to iemMemMapJmp. */
    uint64_t            cTlbCoreHits;
    /** Safe read/write TLB hits in iemMemMapJmp (IEM_WITH_TLB_STATISTICS
     *  only; data tlb only). */
    uint64_t            cTlbSafeHits;
    /** TLB hits in IEMAllMemRWTmplInline.cpp.h (data + IEM_WITH_TLB_STATISTICS only). */
    uint64_t            cTlbInlineCodeHits;

    /** TLB misses in IEMAll.cpp code (both).
     * @note For the data TLB this is only used in iemMemMap and for direct (i.e.
     *       not via safe read/write path) calls to iemMemMapJmp. So,
     *       for the data TLB this more like 'other misses', while for the code
     *       TLB is all misses. */
    uint64_t            cTlbCoreMisses;
#ifdef VBOX_VMM_TARGET_X86
    /** Subset of cTlbCoreMisses that results in PTE.G=1 loads (odd entries). */
    uint64_t            cTlbCoreGlobalLoads;
#endif
    /** Safe read/write TLB misses in iemMemMapJmp (so data only). */
    uint64_t            cTlbSafeMisses;
#ifdef VBOX_VMM_TARGET_X86
    /** Subset of cTlbSafeMisses that results in PTE.G=1 loads (odd entries). */
    uint64_t            cTlbSafeGlobalLoads;
#endif
    /** Safe read path taken (data only).  */
    uint64_t            cTlbSafeReadPath;
    /** Safe write path taken (data only).  */
    uint64_t            cTlbSafeWritePath;

    /** @name Details for native code TLB misses.
     * @note These counts are included in the above counters (cTlbSafeReadPath,
     *       cTlbSafeWritePath, cTlbInlineCodeHits).
     * @{ */
    /** TLB misses in native code due to tag mismatch.   */
    STAMCOUNTER         cTlbNativeMissTag;
    /** TLB misses in native code due to flags or physical revision mismatch. */
    STAMCOUNTER         cTlbNativeMissFlagsAndPhysRev;
    /** TLB misses in native code due to misaligned access. */
    STAMCOUNTER         cTlbNativeMissAlignment;
    /** TLB misses in native code due to cross page access. */
    uint32_t            cTlbNativeMissCrossPage;
    /** TLB misses in native code due to non-canonical address. */
    uint32_t            cTlbNativeMissNonCanonical;
    /** @} */

    /** Slow read path (code only).  */
    uint32_t            cTlbSlowCodeReadPath;

    /** Regular TLB flush count. */
    uint32_t            cTlsFlushes;
    /** Global TLB flush count. */
    uint32_t            cTlsGlobalFlushes;
    /** Revision rollovers. */
    uint32_t            cTlbRevisionRollovers;
    /** Physical revision flushes. */
    uint32_t            cTlbPhysRevFlushes;
    /** Physical revision rollovers. */
    uint32_t            cTlbPhysRevRollovers;

    /** Number of INVLPG (and similar) operations. */
    uint32_t            cTlbInvlPg;
    /** Subset of cTlbInvlPg that involved non-global large pages. */
    uint32_t            cTlbInvlPgLargeNonGlobal;
    /** Subset of cTlbInvlPg that involved global large pages. */
    uint32_t            cTlbInvlPgLargeGlobal;

#ifdef VBOX_VMM_TARGET_X86
    uint32_t            au32Padding[13];
#else
    uint32_t            au32Padding[13 + 8];
#endif

    /** The TLB entries.
     * @x86 Even entries are for PTE.G=0 and uses uTlbRevision.
     *      Odd  entries are for PTE.G=1 and uses uTlbRevisionGlobal. */
    IEMTLBENTRY         aEntries[IEMTLB_ENTRY_COUNT * IEMTLB_ENTRY_COUNT_FACTOR];
#ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
    /** Bitmap tracking TLB entries for large pages.
     * This duplicates IEMTLBE_F_PT_LARGE_PAGE for each TLB entry. */
    uint64_t            bmLargePage[IEMTLB_ENTRY_COUNT * IEMTLB_ENTRY_COUNT_FACTOR / 64];
#endif
} IEMTLB;
AssertCompileMemberAlignment(IEMTLB, aEntries, 64);
AssertCompileSizeAlignment(IEMTLB, 64);
#ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
AssertCompile(IEMTLB_ENTRY_COUNT * IEMTLB_ENTRY_COUNT_FACTOR >= 64 /* bmLargePage ASSUMPTION */);
#endif
/** The width (in bits) of the address portion of the TLB tag.   */
#define IEMTLB_TAG_ADDR_WIDTH   36
/** IEMTLB::uTlbRevision increment.  */
#define IEMTLB_REVISION_INCR    RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH)
/** IEMTLB::uTlbRevision mask.  */
#define IEMTLB_REVISION_MASK    (~(RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH) - 1))

/** IEMTLB::uTlbPhysRev increment.
 * @sa IEMTLBE_F_PHYS_REV */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEMTLB_PHYS_REV_INCR   RT_BIT_64(11)
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define IEMTLB_PHYS_REV_INCR   RT_BIT_64(54)
#endif
#ifdef IEMTLBE_F_PHYS_REV
AssertCompile(IEMTLBE_F_PHYS_REV == ~(IEMTLB_PHYS_REV_INCR - 1U));
#endif

/**
 * Calculates the TLB tag for a virtual address but without TLB revision.
 * @returns Tag value for indexing and comparing with IEMTLB::uTag.
 * @param   a_pVCpu     The CPU handle (for ARM targets to deal with
 *                      configurable page size).
 * @param   a_GCPtr     The virtual address.  Must be RTGCPTR or same size or
 *                      the clearing of the top 16 bits won't work (if 32-bit
 *                      we'll end up with mostly zeros).
 * @todo ARM: Support 52-bit and 56-bit address space size (FEAT_LVA,
 *       FEAT_LVA3) when we see hardware supporting such.  */
#ifdef VBOX_VMM_TARGET_ARMV8
# if 0 /** @todo ARMv8: page size and TLB */
# define IEMTLB_CALC_TAG_NO_REV(a_pVCpu, a_GCPtr)   ( (((a_GCPtr) << 16) >> (IEM_F_ARM_GET_TLB_PAGE_SHIFT(pVCpu->iem.s.fExec) + 16)) )
# else
# define IEMTLB_CALC_TAG_NO_REV(a_pVCpu, a_GCPtr)   ( (((a_GCPtr) << 16) >> (GUEST_MIN_PAGE_SHIFT + 16)) )
# endif
#else
# define IEMTLB_CALC_TAG_NO_REV(a_pVCpu, a_GCPtr)   ( (((a_GCPtr) << 16) >> (GUEST_PAGE_SHIFT + 16)) )
#endif

#if IEMTLB_ENTRY_COUNT_FACTOR == 2
/**
 * Converts a TLB tag value into an even TLB index.
 * @returns Index into IEMTLB::aEntries.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG.
 * @deprecated
 */
# if IEMTLB_ENTRY_COUNT == 256
#  define IEMTLB_TAG_TO_EVEN_INDEX(a_uTag)          ( (uint8_t)(a_uTag) * (unsigned)(IEMTLB_ENTRY_COUNT_FACTOR) )
# else
#  define IEMTLB_TAG_TO_EVEN_INDEX(a_uTag)          ( ((a_uTag) & (IEMTLB_ENTRY_COUNT - 1U)) * (unsigned)(IEMTLB_ENTRY_COUNT_FACTOR) )
AssertCompile(RT_IS_POWER_OF_TWO(IEMTLB_ENTRY_COUNT));
# endif

/**
 * Converts a TLB tag value into an even TLB entry pointer.
 * @returns Pointer into IEMTLB::aEntries corresponding to @a a_uTag.
 * @param   a_pTlb      The TLB.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG or
 *                      IEMTLB_CALC_TAG_NO_REV.
 * @deprecated
 */
# define IEMTLB_TAG_TO_EVEN_ENTRY(a_pTlb, a_uTag)    ( &(a_pTlb)->aEntries[IEMTLB_TAG_TO_EVEN_INDEX(a_uTag)] )

#endif /* IEMTLB_ENTRY_COUNT_FACTOR == 2 */

/**
 * Converts a TLB tag value into a (base) TLB index.
 * @returns Index into IEMTLB::aEntries.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG.
 * @note    The index is multiplied by IEMTLB_ENTRY_COUNT_FACTOR, so for factors
 *          other than 1 this will be the base (even) entry for a set of variations.
 */
#if IEMTLB_ENTRY_COUNT == 256
# if IEMTLB_ENTRY_COUNT_FACTOR == 1
#  define IEMTLB_TAG_TO_INDEX(a_uTag)           ( (unsigned)(uint8_t)(a_uTag) )
# else
#  define IEMTLB_TAG_TO_INDEX(a_uTag)           ( (uint8_t)(a_uTag) * (unsigned)IEMTLB_ENTRY_COUNT_FACTOR )
# endif
#else
# if IEMTLB_ENTRY_COUNT_FACTOR == 1
#  define IEMTLB_TAG_TO_INDEX(a_uTag)           ( (unsigned)((a_uTag) & (IEMTLB_ENTRY_COUNT - 1U)) )
# else
#  define IEMTLB_TAG_TO_INDEX(a_uTag)           (   (unsigned)((a_uTag) & (IEMTLB_ENTRY_COUNT - 1U)) \
                                                  * (unsigned)IEMTLB_ENTRY_COUNT_FACTOR )
# endif
AssertCompile(RT_IS_POWER_OF_TWO(IEMTLB_ENTRY_COUNT));
#endif

/**
 * Converts a TLB tag value into a (base) TLB entry pointer.
 * @returns Pointer into IEMTLB::aEntries corresponding to @a a_uTag.
 * @param   a_pTlb      The TLB.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG or
 *                      IEMTLB_CALC_TAG_NO_REV.
 * @note    The index is multiplied by IEMTLB_ENTRY_COUNT_FACTOR, so for factors
 *          other than 1 this will be the base (even) entry for a set of variations.
 */
#define IEMTLB_TAG_TO_ENTRY(a_pTlb, a_uTag)     ( &(a_pTlb)->aEntries[IEMTLB_TAG_TO_INDEX(a_uTag)] )

/** Converts a GC address to an even TLB index.
 * @note    The index is multiplied by IEMTLB_ENTRY_COUNT_FACTOR, so for factors
 *          other than 1 this will be the base (even) entry for a set of variations.
 */
#define IEMTLB_ADDR_TO_INDEX(a_pVCpu, a_GCPtr)  IEMTLB_TAG_TO_INDEX(IEMTLB_CALC_TAG_NO_REV(a_pVCpu, a_GCPtr))



/** @def IEM_WITH_TLB_TRACE
 * Enables the TLB tracing.
 * Adjust buffer size in IEMR3Init. */
#if defined(DOXYGEN_RUNNING) || 0
# define IEM_WITH_TLB_TRACE
#endif

#ifdef IEM_WITH_TLB_TRACE

/** TLB trace entry types. */
typedef enum : uint8_t
{
    kIemTlbTraceType_Invalid,
    kIemTlbTraceType_InvlPg,
    kIemTlbTraceType_EvictSlot,
    kIemTlbTraceType_LargeEvictSlot,
    kIemTlbTraceType_LargeScan,
    kIemTlbTraceType_Flush,
# if IEMTLB_ENTRY_COUNT_FACTOR > 1
    kIemTlbTraceType_FlushGlobal,   /**< x86 specific */
# endif
    kIemTlbTraceType_Load,
# if IEMTLB_ENTRY_COUNT_FACTOR > 1
    kIemTlbTraceType_LoadGlobal,    /**< x86 specific */
# endif
    kIemTlbTraceType_Load_Cr0,      /**< x86 specific */
    kIemTlbTraceType_Load_Cr3,      /**< x86 specific */
    kIemTlbTraceType_Load_Cr4,      /**< x86 specific */
    kIemTlbTraceType_Load_Efer,     /**< x86 specific */
    kIemTlbTraceType_Irq,
    kIemTlbTraceType_Xcpt,
    kIemTlbTraceType_IRet,          /**< x86 specific */
    kIemTlbTraceType_Tb_Compile,
    kIemTlbTraceType_Tb_Exec_Threaded,
    kIemTlbTraceType_Tb_Exec_Native,
    kIemTlbTraceType_User0,
    kIemTlbTraceType_User1,
    kIemTlbTraceType_User2,
    kIemTlbTraceType_User3,
} IEMTLBTRACETYPE;

/** TLB trace entry. */
typedef struct IEMTLBTRACEENTRY
{
    /** The flattened RIP for the event. */
    uint64_t            rip;
    /** The event type. */
    IEMTLBTRACETYPE     enmType;
    /** Byte parameter - typically used as 'bool fDataTlb'.  */
    uint8_t             bParam;
    /** 16-bit parameter value. */
    uint16_t            u16Param;
    /** 32-bit parameter value. */
    uint32_t            u32Param;
    /** 64-bit parameter value. */
    uint64_t            u64Param;
    /** 64-bit parameter value. */
    uint64_t            u64Param2;
} IEMTLBTRACEENTRY;
AssertCompileSize(IEMTLBTRACEENTRY, 32);
/** Pointer to a TLB trace entry. */
typedef IEMTLBTRACEENTRY *PIEMTLBTRACEENTRY;
/** Pointer to a const TLB trace entry. */
typedef IEMTLBTRACEENTRY const *PCIEMTLBTRACEENTRY;
#endif /* !IEM_WITH_TLB_TRACE */

#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3) && 1
# define IEMTLBTRACE_INVLPG(a_pVCpu, a_GCPtr) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_InvlPg, a_GCPtr)
# define IEMTLBTRACE_EVICT_SLOT(a_pVCpu, a_GCPtrTag, a_GCPhys, a_idxSlot, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_EvictSlot, a_GCPtrTag, a_GCPhys, a_fDataTlb, a_idxSlot)
# define IEMTLBTRACE_LARGE_EVICT_SLOT(a_pVCpu, a_GCPtrTag, a_GCPhys, a_idxSlot, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_LargeEvictSlot, a_GCPtrTag, a_GCPhys, a_fDataTlb, a_idxSlot)
# define IEMTLBTRACE_LARGE_SCAN(a_pVCpu, a_fGlobal, a_fNonGlobal, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_LargeScan, 0, 0, a_fDataTlb, (uint8_t)a_fGlobal | ((uint8_t)a_fNonGlobal << 1))
# define IEMTLBTRACE_FLUSH(a_pVCpu, a_uRev, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Flush, a_uRev, 0, a_fDataTlb)
# define IEMTLBTRACE_FLUSH_GLOBAL(a_pVCpu, a_uRev, a_uGRev, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_FlushGlobal, a_uRev, a_uGRev, a_fDataTlb)
# define IEMTLBTRACE_LOAD(a_pVCpu, a_GCPtr, a_GCPhys, a_fTlb, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Load, a_GCPtr, a_GCPhys, a_fDataTlb, a_fTlb)
# define IEMTLBTRACE_LOAD_GLOBAL(a_pVCpu, a_GCPtr, a_GCPhys, a_fTlb, a_fDataTlb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_LoadGlobal, a_GCPtr, a_GCPhys, a_fDataTlb, a_fTlb)
#else
# define IEMTLBTRACE_INVLPG(a_pVCpu, a_GCPtr)                                               do { } while (0)
# define IEMTLBTRACE_EVICT_SLOT(a_pVCpu, a_GCPtrTag, a_GCPhys, a_idxSlot, a_fDataTlb)       do { } while (0)
# define IEMTLBTRACE_LARGE_EVICT_SLOT(a_pVCpu, a_GCPtrTag, a_GCPhys, a_idxSlot, a_fDataTlb) do { } while (0)
# define IEMTLBTRACE_LARGE_SCAN(a_pVCpu, a_fGlobal, a_fNonGlobal, a_fDataTlb)               do { } while (0)
# define IEMTLBTRACE_FLUSH(a_pVCpu, a_uRev, a_fDataTlb)                                     do { } while (0)
# define IEMTLBTRACE_FLUSH_GLOBAL(a_pVCpu, a_uRev, a_uGRev, a_fDataTlb)                     do { } while (0)
# define IEMTLBTRACE_LOAD(a_pVCpu, a_GCPtr, a_GCPhys, a_fTlb, a_fDataTlb)                   do { } while (0)
# define IEMTLBTRACE_LOAD_GLOBAL(a_pVCpu, a_GCPtr, a_GCPhys, a_fTlb, a_fDataTlb)            do { } while (0)
#endif

#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3) && 1
# define IEMTLBTRACE_LOAD_CR0(a_pVCpu, a_uNew, a_uOld)      iemTlbTrace(a_pVCpu, kIemTlbTraceType_Load_Cr0, a_uNew, a_uOld)
# define IEMTLBTRACE_LOAD_CR3(a_pVCpu, a_uNew, a_uOld)      iemTlbTrace(a_pVCpu, kIemTlbTraceType_Load_Cr3, a_uNew, a_uOld)
# define IEMTLBTRACE_LOAD_CR4(a_pVCpu, a_uNew, a_uOld)      iemTlbTrace(a_pVCpu, kIemTlbTraceType_Load_Cr4, a_uNew, a_uOld)
# define IEMTLBTRACE_LOAD_EFER(a_pVCpu, a_uNew, a_uOld)     iemTlbTrace(a_pVCpu, kIemTlbTraceType_Load_Efer, a_uNew, a_uOld)
#else
# define IEMTLBTRACE_LOAD_CR0(a_pVCpu, a_uNew, a_uOld)      do { } while (0)
# define IEMTLBTRACE_LOAD_CR3(a_pVCpu, a_uNew, a_uOld)      do { } while (0)
# define IEMTLBTRACE_LOAD_CR4(a_pVCpu, a_uNew, a_uOld)      do { } while (0)
# define IEMTLBTRACE_LOAD_EFER(a_pVCpu, a_uNew, a_uOld)     do { } while (0)
#endif

#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3) && 1
# define IEMTLBTRACE_IRQ(a_pVCpu, a_uVector, a_fFlags, a_fEFlags) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Irq, a_fEFlags, 0, a_uVector, a_fFlags)
# define IEMTLBTRACE_XCPT(a_pVCpu, a_uVector, a_uErr, a_uCr2, a_fFlags) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Xcpt, a_uErr, a_uCr2, a_uVector, a_fFlags)
# define IEMTLBTRACE_IRET(a_pVCpu, a_uRetCs, a_uRetRip, a_fEFlags) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_IRet, a_uRetRip, a_fEFlags, 0, a_uRetCs)
#else
# define IEMTLBTRACE_IRQ(a_pVCpu, a_uVector, a_fFlags, a_fEFlags)       do { } while (0)
# define IEMTLBTRACE_XCPT(a_pVCpu, a_uVector, a_uErr, a_uCr2, a_fFlags) do { } while (0)
# define IEMTLBTRACE_IRET(a_pVCpu, a_uRetCs, a_uRetRip, a_fEFlags)      do { } while (0)
#endif

#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3) && 1
# define IEMTLBTRACE_TB_COMPILE(a_pVCpu, a_GCPhysPc) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Tb_Compile, a_GCPhysPc)
# define IEMTLBTRACE_TB_EXEC_THRD(a_pVCpu, a_pTb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Tb_Exec_Threaded, (a_pTb)->GCPhysPc, (uintptr_t)a_pTb, 0, (a_pTb)->cUsed)
# define IEMTLBTRACE_TB_EXEC_N8VE(a_pVCpu, a_pTb) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_Tb_Exec_Native,   (a_pTb)->GCPhysPc, (uintptr_t)a_pTb, 0, (a_pTb)->cUsed)
#else
# define IEMTLBTRACE_TB_COMPILE(a_pVCpu, a_GCPhysPc)                    do { } while (0)
# define IEMTLBTRACE_TB_EXEC_THRD(a_pVCpu, a_pTb)                       do { } while (0)
# define IEMTLBTRACE_TB_EXEC_N8VE(a_pVCpu, a_pTb)                       do { } while (0)
#endif

#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3) && 1
# define IEMTLBTRACE_USER0(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_User0, a_u64Param1, a_u64Param2, a_bParam, a_u32Param)
# define IEMTLBTRACE_USER1(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_User1, a_u64Param1, a_u64Param2, a_bParam, a_u32Param)
# define IEMTLBTRACE_USER2(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_User2, a_u64Param1, a_u64Param2, a_bParam, a_u32Param)
# define IEMTLBTRACE_USER3(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) \
    iemTlbTrace(a_pVCpu, kIemTlbTraceType_User3, a_u64Param1, a_u64Param2, a_bParam, a_u32Param)
#else
# define IEMTLBTRACE_USER0(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) do { } while (0)
# define IEMTLBTRACE_USER1(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) do { } while (0)
# define IEMTLBTRACE_USER2(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) do { } while (0)
# define IEMTLBTRACE_USER3(a_pVCpu, a_u64Param1, a_u64Param2, a_u32Param, a_bParam) do { } while (0)
#endif


/** @name IEM_MC_F_XXX - MC block flags/clues.
 * @note x86 specific
 * @todo Merge with IEM_CIMPL_F_XXX
 * @{ */
#define IEM_MC_F_ONLY_8086          RT_BIT_32(0)
#define IEM_MC_F_MIN_186            RT_BIT_32(1)
#define IEM_MC_F_MIN_286            RT_BIT_32(2)
#define IEM_MC_F_NOT_286_OR_OLDER   IEM_MC_F_MIN_386
#define IEM_MC_F_MIN_386            RT_BIT_32(3)
#define IEM_MC_F_MIN_486            RT_BIT_32(4)
#define IEM_MC_F_MIN_PENTIUM        RT_BIT_32(5)
#define IEM_MC_F_MIN_PENTIUM_II     IEM_MC_F_MIN_PENTIUM
#define IEM_MC_F_MIN_CORE           IEM_MC_F_MIN_PENTIUM
#define IEM_MC_F_64BIT              RT_BIT_32(6)
#define IEM_MC_F_NOT_64BIT          RT_BIT_32(7)
/** This is set by IEMAllN8vePython.py to indicate a variation with the
 * flags-clearing-and-checking. */
#define IEM_MC_F_WITH_FLAGS         RT_BIT_32(8)
/** This is set by IEMAllN8vePython.py to indicate a variation without the
 * flags-clearing-and-checking, when there is also a variation with that.
 * @note Do not set this manully, it's only for python and for testing in
 *       the native recompiler! */
#define IEM_MC_F_WITHOUT_FLAGS      RT_BIT_32(9)
/** @} */

/** @name IEM_CIMPL_F_XXX - State change clues for CIMPL calls.
 *
 * These clues are mainly for the recompiler, so that it can emit correct code.
 *
 * They are processed by the python script and which also automatically
 * calculates flags for MC blocks based on the statements, extending the use of
 * these flags to describe MC block behavior to the recompiler core.  The python
 * script pass the flags to the IEM_MC2_END_EMIT_CALLS macro, but mainly for
 * error checking purposes.  The script emits the necessary fEndTb = true and
 * similar statements as this reduces compile time a tiny bit.
 *
 * @{ */
/** Flag set if direct branch, clear if absolute or indirect. */
#define IEM_CIMPL_F_BRANCH_DIRECT        RT_BIT_32(0)
/** Flag set if indirect branch, clear if direct or relative.
 * This is also used for all system control transfers (SYSCALL, SYSRET, INT, ++)
 * as well as for return instructions (RET, IRET, RETF). */
#define IEM_CIMPL_F_BRANCH_INDIRECT      RT_BIT_32(1)
/** Flag set if relative branch, clear if absolute or indirect. */
#define IEM_CIMPL_F_BRANCH_RELATIVE      RT_BIT_32(2)
/** Flag set if conditional branch, clear if unconditional. */
#define IEM_CIMPL_F_BRANCH_CONDITIONAL   RT_BIT_32(3)
/** Flag set if it's a far branch (changes CS).
 * @note x86 specific */
#define IEM_CIMPL_F_BRANCH_FAR           RT_BIT_32(4)
/** Convenience: Testing any kind of branch. */
#define IEM_CIMPL_F_BRANCH_ANY          (IEM_CIMPL_F_BRANCH_DIRECT | IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_RELATIVE)

/** Execution flags may change (IEMCPU::fExec). */
#define IEM_CIMPL_F_MODE                RT_BIT_32(5)
/** May change significant portions of RFLAGS.
 * @note x86 specific */
#define IEM_CIMPL_F_RFLAGS              RT_BIT_32(6)
/** May change the status bits (X86_EFL_STATUS_BITS) in RFLAGS.
 * @note x86 specific */
#define IEM_CIMPL_F_STATUS_FLAGS        RT_BIT_32(7)
/** May trigger interrupt shadowing.
 * @note x86 specific */
#define IEM_CIMPL_F_INHIBIT_SHADOW      RT_BIT_32(8)
/** May enable interrupts, so recheck IRQ immediately afterwards executing
 *  the instruction. */
#define IEM_CIMPL_F_CHECK_IRQ_AFTER     RT_BIT_32(9)
/** May disable interrupts, so recheck IRQ immediately before executing the
 *  instruction. */
#define IEM_CIMPL_F_CHECK_IRQ_BEFORE    RT_BIT_32(10)
/** Convenience: Check for IRQ both before and after an instruction. */
#define IEM_CIMPL_F_CHECK_IRQ_BEFORE_AND_AFTER (IEM_CIMPL_F_CHECK_IRQ_BEFORE | IEM_CIMPL_F_CHECK_IRQ_AFTER)
/** May trigger a VM exit (treated like IEM_CIMPL_F_MODE atm). */
#define IEM_CIMPL_F_VMEXIT              RT_BIT_32(11)
/** May modify FPU state.
 * @todo Not sure if this is useful yet.  */
#define IEM_CIMPL_F_FPU                 RT_BIT_32(12)
/** REP prefixed instruction which may yield before updating PC.
 * @todo Not sure if this is useful, REP functions now return non-zero
 *       status if they don't update the PC.
 * @note x86 specific */
#define IEM_CIMPL_F_REP                 RT_BIT_32(13)
/** I/O instruction.
 * @todo Not sure if this is useful yet.
 * @note x86 specific */
#define IEM_CIMPL_F_IO                  RT_BIT_32(14)
/** Force end of TB after the instruction. */
#define IEM_CIMPL_F_END_TB              RT_BIT_32(15)
/** Flag set if a branch may also modify the stack (push/pop return address). */
#define IEM_CIMPL_F_BRANCH_STACK        RT_BIT_32(16)
/** Flag set if a branch may also modify the stack (push/pop return address)
 *  and switch it (load/restore SS:RSP).
 * @note x86 specific */
#define IEM_CIMPL_F_BRANCH_STACK_FAR    RT_BIT_32(17)
/** Convenience: Raise exception (technically unnecessary, since it shouldn't return VINF_SUCCESS). */
#define IEM_CIMPL_F_XCPT \
    (IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR \
     | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT)
/** Convenience: Memory access (arm). */
#define IEM_CIMPL_F_MEM                 IEM_CIMPL_F_XCPT

/** The block calls a C-implementation instruction function with two implicit arguments.
 * Mutually exclusive with IEM_CIMPL_F_CALLS_AIMPL and
 * IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE.
 * @note The python scripts will add this if missing.  */
#define IEM_CIMPL_F_CALLS_CIMPL                 RT_BIT_32(18)
/** The block calls an ASM-implementation instruction function.
 * Mutually exclusive with IEM_CIMPL_F_CALLS_CIMPL and
 * IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE.
 * @note The python scripts will add this if missing.  */
#define IEM_CIMPL_F_CALLS_AIMPL                 RT_BIT_32(19)
/** The block calls an ASM-implementation instruction function with an implicit
 * X86FXSTATE pointer argument.
 * Mutually exclusive with IEM_CIMPL_F_CALLS_CIMPL, IEM_CIMPL_F_CALLS_AIMPL and
 * IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE.
 * @note The python scripts will add this if missing.
 * @note x86 specific */
#define IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE    RT_BIT_32(20)
/** The block calls an ASM-implementation instruction function with an implicit
 * X86XSAVEAREA pointer argument.
 * Mutually exclusive with IEM_CIMPL_F_CALLS_CIMPL, IEM_CIMPL_F_CALLS_AIMPL and
 * IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE.
 * @note No different from IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE, so same value.
 * @note The python scripts will add this if missing.
 * @note x86 specific */
#define IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE     IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE
/** @} */


/** @name IEM_F_XXX - Execution mode flags (IEMCPU::fExec, IEMTB::fFlags).
 *
 * These flags are set when entering IEM and adjusted as code is executed, such
 * that they will always contain the current values as instructions are
 * finished.
 *
 * In recompiled execution mode, (most of) these flags are included in the
 * translation block selection key and stored in IEMTB::fFlags alongside the
 * IEMTB_F_XXX flags.  The latter flags uses bits 31 thru 24, which are all zero
 * in IEMCPU::fExec.
 *
 * @{ */
/** Mode: The block target mode mask.
 * X86: CPUMODE plus protected, v86 and pre-386 indicators.
 * ARM: PSTATE.nRW | PSTATE.T | PSTATE.EL.
 *      This doesn't quite overlap with SPSR_ELx when in AARCH32 mode,
 *      but that's life. */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEM_F_MODE_MASK                    UINT32_C(0x0000001f)
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define IEM_F_MODE_MASK                    UINT32_C(0x0000003c)
#endif

#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
/** X86 Mode: The IEMMODE part of the IEMTB_F_MODE_MASK value. */
# define IEM_F_MODE_X86_CPUMODE_MASK        UINT32_C(0x00000003)
/** X86 Mode: Bit used to indicating pre-386 CPU in 16-bit mode (for eliminating
 * conditional in EIP/IP updating), and flat wide open CS, SS, DS, and ES in
 * 32-bit mode (for simplifying most memory accesses). */
# define IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK UINT32_C(0x00000004)
/** X86 Mode: Bit indicating protected mode, real mode (or SMM) when not set. */
# define IEM_F_MODE_X86_PROT_MASK           UINT32_C(0x00000008)
/** X86 Mode: Bit used to indicate virtual 8086 mode (only 16-bit). */
# define IEM_F_MODE_X86_V86_MASK            UINT32_C(0x00000010)

/** X86 Mode: 16-bit on 386 or later. */
# define IEM_F_MODE_X86_16BIT               UINT32_C(0x00000000)
/** X86 Mode: 80286, 80186 and 8086/88 targetting blocks (EIP update opt). */
# define IEM_F_MODE_X86_16BIT_PRE_386       UINT32_C(0x00000004)
/** X86 Mode: 16-bit protected mode on 386 or later. */
# define IEM_F_MODE_X86_16BIT_PROT          UINT32_C(0x00000008)
/** X86 Mode: 16-bit protected mode on 386 or later. */
# define IEM_F_MODE_X86_16BIT_PROT_PRE_386  UINT32_C(0x0000000c)
/** X86 Mode: 16-bit virtual 8086 protected mode (on 386 or later). */
# define IEM_F_MODE_X86_16BIT_PROT_V86      UINT32_C(0x00000018)

/** X86 Mode: 32-bit on 386 or later. */
# define IEM_F_MODE_X86_32BIT               UINT32_C(0x00000001)
/** X86 Mode: 32-bit mode with wide open flat CS, SS, DS and ES. */
# define IEM_F_MODE_X86_32BIT_FLAT          UINT32_C(0x00000005)
/** X86 Mode: 32-bit protected mode. */
# define IEM_F_MODE_X86_32BIT_PROT          UINT32_C(0x00000009)
/** X86 Mode: 32-bit protected mode with wide open flat CS, SS, DS and ES. */
# define IEM_F_MODE_X86_32BIT_PROT_FLAT     UINT32_C(0x0000000d)

/** X86 Mode: 64-bit (includes protected, but not the flat bit). */
# define IEM_F_MODE_X86_64BIT               UINT32_C(0x0000000a)

/** X86 Mode: Checks if @a a_fExec represent a FLAT mode. */
# define IEM_F_MODE_X86_IS_FLAT(a_fExec)    (   ((a_fExec) & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT \
                                             || ((a_fExec) & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT \
                                             || ((a_fExec) & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT)

/** X86: The current protection level (CPL) shift factor.   */
# define IEM_F_X86_CPL_SHIFT                 8
/** X86: The current protection level (CPL) mask. */
# define IEM_F_X86_CPL_MASK                  UINT32_C(0x00000300)
/** X86: The current protection level (CPL) shifted mask. */
# define IEM_F_X86_CPL_SMASK                 UINT32_C(0x00000003)

/** X86: Alignment checks enabled (CR0.AM=1 & EFLAGS.AC=1). */
# define IEM_F_X86_AC                        UINT32_C(0x00080000)

/** X86 execution context.
 * The IEM_F_X86_CTX_XXX values are individual flags that can be combined (with
 * the exception of IEM_F_X86_CTX_NORMAL).  This allows running VMs from SMM
 * mode. */
# define IEM_F_X86_CTX_MASK                  UINT32_C(0x0000f000)
/** X86 context: Plain regular execution context. */
# define IEM_F_X86_CTX_NORMAL                UINT32_C(0x00000000)
/** X86 context: VT-x enabled. */
# define IEM_F_X86_CTX_VMX                   UINT32_C(0x00001000)
/** X86 context: AMD-V enabled. */
# define IEM_F_X86_CTX_SVM                   UINT32_C(0x00002000)
/** X86 context: In AMD-V or VT-x guest mode. */
# define IEM_F_X86_CTX_IN_GUEST              UINT32_C(0x00004000)
/** X86 context: System management mode (SMM). */
# define IEM_F_X86_CTX_SMM                   UINT32_C(0x00008000)

/** @todo Add TF+RF+INHIBIT indicator(s), so we can eliminate the conditional in
 * iemRegFinishClearingRF() most for most situations (CPUMCTX_DBG_HIT_DRX_MASK
 * and CPUMCTX_DBG_DBGF_MASK are covered by the IEM_F_PENDING_BRK_XXX bits
 * alread). */

/** @todo Add TF+RF+INHIBIT indicator(s), so we can eliminate the conditional in
 *        iemRegFinishClearingRF() most for most situations
 *        (CPUMCTX_DBG_HIT_DRX_MASK and CPUMCTX_DBG_DBGF_MASK are covered by
 *        the IEM_F_PENDING_BRK_XXX bits alread). */

#endif /* X86 || doxygen  */

#if defined(VBOX_VMM_TARGET_ARMV8) || defined(DOXYGEN_RUNNING)
/** ARM: The stack pointer index - not part of mode */
# define IEM_F_ARM_SP_IDX                   UINT32_C(0x00000003)
/** ARM: Get the SP register index. */
# define IEM_F_ARM_GET_SP_IDX(a_fExec)      ((a_fExec) & IEM_F_ARM_SP_IDX)

/** ARM Mode: Exception (privilege) level shift count. */
# define IEM_F_MODE_ARM_EL_SHIFT            2
/** ARM Mode: Exception (privilege) level mask. */
# define IEM_F_MODE_ARM_EL_MASK             UINT32_C(0x0000000c)
/** ARM Mode: Exception (privilege) level shifted down mask. */
# define IEM_F_MODE_ARM_EL_SMASK            UINT32_C(0x00000003)
/** ARM Mode: 32-bit (set) or 64-bit (clear) indicator (SPSR_ELx.M[4]). */
# define IEM_F_MODE_ARM_32BIT               UINT32_C(0x00000010)
/** ARM Mode: Thumb mode indicator (SPSR_ELx.T).  */
# define IEM_F_MODE_ARM_T32                 UINT32_C(0x00000020)
/** ARM Mode: Get the exception (privilege) level. */
# define IEM_F_MODE_ARM_GET_EL(a_fExec)     (((a_fExec) >> IEM_F_MODE_ARM_EL_SHIFT) & IEM_F_MODE_ARM_EL_SMASK)

/** ARM: Normal alignment checks enabled (SCTRL_ELx.A). */
# define IEM_F_ARM_A                        UINT32_C(0x00000040)
/** ARM: LSE2 alignment checks enabled (~SCTRL_ELx.nAA). */
# define IEM_F_ARM_AA                       UINT32_C(0x00000080)
/** ARM: SP alignment checks enabled for current EL (SCTRL_ELx.SA/SA0). */
# define IEM_F_ARM_SA                       UINT32_C(0x00000100)

#if 0 /* revisit when needed */
/** ARM: 4K minium page (granule) size (stage 1+2). */
# define IEM_F_ARM_MIN_PAGE_4K              UINT32_C(0x00000000)
/** ARM: 16K minium page (granule) size (stage 1+2). */
# define IEM_F_ARM_MIN_PAGE_16K             UINT32_C(0x00000200)
/** ARM: 64K minium page (granule) size (stage 1+2). */
# define IEM_F_ARM_MIN_PAGE_64K             UINT32_C(0x00000400)
/** ARM: Mask for the mimum page (granule) size encoding (stage 1+2).
 * This is the shift count - 12. */
# define IEM_F_ARM_MIN_PAGE_MASK            UINT32_C(0x00000700)
/** ARM: The shift count for the tage 1 page (granule) size encoding value.
 * @note Don't confuse with IEM_F_ARM_GET_MIN_PAGE_SHIFT! */
# define IEM_F_ARM_MIN_PAGE_SHIFT           8
/** ARM: Get the current minium page (granule) shift count. */
# define IEM_F_ARM_GET_MIN_PAGE_SHIFT(a_fExec)          (12 + (((a_fExec) & IEM_F_ARM_MIN_PAGE_MASK) >> IEM_F_ARM_MIN_PAGE_SHIFT))
/** ARM: Get the current minium page (granule) size. */
# define IEM_F_ARM_GET_MIN_PAGE_SIZE(a_fExec)           (1 << IEM_F_ARM_GET_MIN_PAGE_SHIFT(a_fExec))
/** ARM: Get the current minium page (granule) offset mask. */
# define IEM_F_ARM_GET_MIN_PAGE_OFFSET_MASK(a_fExec)    (IEM_F_ARM_GET_MIN_PAGE_SHIFT(a_fExec) - 1)

/** ARM: Get the current TLB page (granule) shift count.
 * Same as IEM_F_ARM_GET_MIN_PAGE_SHIFT */
# define IEM_F_ARM_GET_TLB_PAGE_SHIFT(a_fExec)          IEM_F_ARM_GET_MIN_PAGE_SHIFT(a_fExec)
/** ARM: Get the current TLB page (granule) size.
 * Same as IEM_F_ARM_GET_MIN_PAGE_SIZE. */
# define IEM_F_ARM_GET_TLB_PAGE_SIZE(a_fExec)           IEM_F_ARM_GET_MIN_PAGE_SIZE(a_fExec)
/** ARM: Get the current TLB page (granule) offset mask.
 * Same as IEM_F_ARM_GET_MIN_PAGE_OFFSET_MASK. */
# define IEM_F_ARM_GET_TLB_PAGE_OFFSET_MASK(a_fExec)    IEM_F_ARM_GET_MIN_PAGE_OFFSET_MASK(a_fExec)
#else
/** ARM: Get the current TLB page (granule) shift count.
 * Same as IEM_F_ARM_GET_MIN_PAGE_SHIFT */
# define IEM_F_ARM_GET_TLB_PAGE_SHIFT(a_fExec)          GUEST_MIN_PAGE_SHIFT
/** ARM: Get the current TLB page (granule) size.
 * Same as IEM_F_ARM_GET_MIN_PAGE_SIZE. */
# define IEM_F_ARM_GET_TLB_PAGE_SIZE(a_fExec)           GUEST_MIN_PAGE_SIZE
/** ARM: Get the current TLB page (granule) offset mask.
 * Same as IEM_F_ARM_GET_MIN_PAGE_OFFSET_MASK. */
# define IEM_F_ARM_GET_TLB_PAGE_OFFSET_MASK(a_fExec)    GUEST_MIN_PAGE_OFFSET_MASK
#endif

/** ARM: The translation regime mask (see IEM_ARM_REGIME_XXX). */
# define IEM_F_ARM_REGIME_MASK              UINT32_C(0x0000f000)
/** ARM: The translation regime mask (see IEM_ARM_REGIME_XXX). */
# define IEM_F_ARM_REGIME_SHIFT             12

#endif /* ARM || doxygen  */

/** Bypass access handlers when set. */
#define IEM_F_BYPASS_HANDLERS               UINT32_C(0x00010000)
/** Have pending hardware instruction breakpoints.   */
#define IEM_F_PENDING_BRK_INSTR             UINT32_C(0x00020000)
/** Have pending hardware data breakpoints.   */
#define IEM_F_PENDING_BRK_DATA              UINT32_C(0x00040000)

/** X86: Have pending hardware I/O breakpoints. */
#define IEM_F_PENDING_BRK_X86_IO            UINT32_C(0x00000400)
/** X86: Disregard the lock prefix (implied or not) when set. */
#define IEM_F_X86_DISREGARD_LOCK            UINT32_C(0x00000800)

/** ARM: Software step (single step).
 * @todo make generic? */
#define IEM_F_ARM_SOFTWARE_STEP             UINT32_C(0x00000800)

/** Pending breakpoint mask (what iemCalcExecDbgFlags works out). */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEM_F_PENDING_BRK_MASK             (IEM_F_PENDING_BRK_INSTR | IEM_F_PENDING_BRK_DATA | IEM_F_PENDING_BRK_X86_IO)
#else
# define IEM_F_PENDING_BRK_MASK             (IEM_F_PENDING_BRK_INSTR | IEM_F_PENDING_BRK_DATA)
#endif

/** Caller configurable options. */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEM_F_USER_OPTS                    (IEM_F_BYPASS_HANDLERS | IEM_F_X86_DISREGARD_LOCK)
#else
# define IEM_F_USER_OPTS                    (IEM_F_BYPASS_HANDLERS)
#endif
/** @} */


/** @name IEMTB_F_XXX - Translation block flags (IEMTB::fFlags).
 *
 * Extends the IEM_F_XXX flags (subject to IEMTB_F_IEM_F_MASK) to make up the
 * translation block flags.  The combined flag mask (subject to
 * IEMTB_F_KEY_MASK) is used as part of the lookup key for translation blocks.
 *
 * @{ */
/** Mask of IEM_F_XXX flags included in IEMTB_F_XXX. */
#define IEMTB_F_IEM_F_MASK              UINT32_C(0x00ffffff)

/** Type: The block type mask. */
#define IEMTB_F_TYPE_MASK               UINT32_C(0x03000000)
/** Type: Purly threaded recompiler (via tables). */
#define IEMTB_F_TYPE_THREADED           UINT32_C(0x01000000)
/** Type: Native recompilation.  */
#define IEMTB_F_TYPE_NATIVE             UINT32_C(0x02000000)

/** Set when we're starting the block in an "interrupt shadow".
 * We don't need to distingish between the two types of this mask, thus the one.
 * @see CPUMCTX_INHIBIT_SHADOW, CPUMIsInInterruptShadow()  */
#define IEMTB_F_X86_INHIBIT_SHADOW      UINT32_C(0x04000000)
/** Set when we're currently inhibiting NMIs
 * @see CPUMCTX_INHIBIT_NMI, CPUMAreInterruptsInhibitedByNmi() */
#define IEMTB_F_X86_INHIBIT_NMI         UINT32_C(0x08000000)

/** Checks that EIP/IP is wihin CS.LIM before each instruction.  Used when
 * we're close the limit before starting a TB, as determined by
 * iemGetTbFlagsForCurrentPc(). */
#define IEMTB_F_X86_CS_LIM_CHECKS       UINT32_C(0x10000000)

/** Mask of the IEMTB_F_XXX flags that are part of the TB lookup key.
 *
 * @note We skip all of IEM_F_X86_CTX_MASK, with the exception of SMM (which we
 *       don't implement), because we don't currently generate any context
 *       specific code - that's all handled in CIMPL functions.
 *
 *       For the threaded recompiler we don't generate any CPL specific code
 *       either, but the native recompiler does for memory access (saves getting
 *       the CPL from fExec and turning it into IEMTLBE_F_PT_NO_USER).
 *       Since most OSes will not share code between rings, this shouldn't
 *       have any real effect on TB/memory/recompiling load.
 */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define IEMTB_F_KEY_MASK               ((UINT32_MAX & ~(IEM_F_X86_CTX_MASK | IEMTB_F_TYPE_MASK)) | IEM_F_X86_CTX_SMM)
#else
# define IEMTB_F_KEY_MASK               (UINT32_MAX)
#endif
/** @} */

#ifdef VBOX_VMM_TARGET_X86
AssertCompile( (IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_PROT_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_V86_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(  IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_PROT_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_V86_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_V86_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_V86_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_V86     & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT_V86     & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_V86     & IEM_F_MODE_X86_V86_MASK);

AssertCompile( (IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(!(IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(!(IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(  IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(!(IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(!(IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_PROT_MASK);
AssertCompile( (IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_PROT_MASK);

AssertCompile( (IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_64BIT);
AssertCompile(  IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
#endif /* VBOX_VMM_TARGET_X86 */

#ifdef VBOX_VMM_TARGET_ARMV8
AssertCompile(IEM_F_MODE_ARM_EL_SHIFT == ARMV8_SPSR_EL2_AARCH64_EL_SHIFT);
AssertCompile(IEM_F_MODE_ARM_EL_MASK  == ARMV8_SPSR_EL2_AARCH64_EL);
AssertCompile(IEM_F_MODE_ARM_32BIT    == ARMV8_SPSR_EL2_AARCH64_M4);
AssertCompile(IEM_F_MODE_ARM_T32      == ARMV8_SPSR_EL2_AARCH64_T);
#endif

/** Native instruction type for use with the native code generator.
 * This is a byte (uint8_t) for x86 and amd64 and uint32_t for the other(s). */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
typedef uint8_t IEMNATIVEINSTR;
#else
typedef uint32_t IEMNATIVEINSTR;
#endif
/** Pointer to a native instruction unit. */
typedef IEMNATIVEINSTR *PIEMNATIVEINSTR;
/** Pointer to a const native instruction unit. */
typedef IEMNATIVEINSTR const *PCIEMNATIVEINSTR;

/**
 * A call for the threaded call table.
 */
typedef struct IEMTHRDEDCALLENTRY
{
    /** The function to call (IEMTHREADEDFUNCS). */
    uint16_t    enmFunction;

    /** Instruction number in the TB (for statistics). */
    uint8_t     idxInstr;
    /** The opcode length. */
    uint8_t     cbOpcode;
    /** Offset into IEMTB::pabOpcodes. */
    uint16_t    offOpcode;

    /** TB lookup table index (7 bits) and large size (1 bits).
     *
     * The default size is 1 entry, but for indirect calls and returns we set the
     * top bit and allocate 4 (IEM_TB_LOOKUP_TAB_LARGE_SIZE) entries.  The large
     * tables uses RIP for selecting the entry to use, as it is assumed a hash table
     * lookup isn't that slow compared to sequentially trying out 4 TBs.
     *
     * By default lookup table entry 0 for a TB is reserved as a fallback for
     * calltable entries w/o explicit entreis, so this member will be non-zero if
     * there is a lookup entry associated with this call.
     *
     * @sa IEM_TB_LOOKUP_TAB_GET_SIZE, IEM_TB_LOOKUP_TAB_GET_IDX
     */
    uint8_t     uTbLookup;

    /** Flags - IEMTHREADEDCALLENTRY_F_XXX. */
    uint8_t     fFlags;

    /** Generic parameters.
     * @todo ARM: Hope we can get away with one param here... */
    uint64_t    auParams[3];
} IEMTHRDEDCALLENTRY;
AssertCompileSize(IEMTHRDEDCALLENTRY, sizeof(uint64_t) * 4);
/** Pointer to a threaded call entry. */
typedef struct IEMTHRDEDCALLENTRY *PIEMTHRDEDCALLENTRY;
/** Pointer to a const threaded call entry. */
typedef IEMTHRDEDCALLENTRY const *PCIEMTHRDEDCALLENTRY;

/** The number of TB lookup table entries for a large allocation
 *  (IEMTHRDEDCALLENTRY::uTbLookup bit 7 set). */
#define IEM_TB_LOOKUP_TAB_LARGE_SIZE                    4
/** Get the lookup table size from IEMTHRDEDCALLENTRY::uTbLookup. */
#define IEM_TB_LOOKUP_TAB_GET_SIZE(a_uTbLookup)         (!((a_uTbLookup) & 0x80) ? 1 : IEM_TB_LOOKUP_TAB_LARGE_SIZE)
/** Get the first lookup table index from IEMTHRDEDCALLENTRY::uTbLookup. */
#define IEM_TB_LOOKUP_TAB_GET_IDX(a_uTbLookup)          ((a_uTbLookup) & 0x7f)
/** Get the lookup table index from IEMTHRDEDCALLENTRY::uTbLookup and PC. */
#define IEM_TB_LOOKUP_TAB_GET_IDX_WITH_PC(a_uTbLookup, a_Pc) \
    (!((a_uTbLookup) & 0x80) ? (a_uTbLookup) & 0x7f : ((a_uTbLookup) & 0x7f) + ((a_Pc) & (IEM_TB_LOOKUP_TAB_LARGE_SIZE - 1)) )

/** Make a IEMTHRDEDCALLENTRY::uTbLookup value. */
#define IEM_TB_LOOKUP_TAB_MAKE(a_idxTable, a_fLarge)    ((a_idxTable) | ((a_fLarge) ? 0x80 : 0))


/** The call entry is a jump target. */
#define IEMTHREADEDCALLENTRY_F_JUMP_TARGET              UINT8_C(0x01)


/**
 * Native IEM TB 'function' typedef.
 *
 * This will throw/longjmp on occation.
 *
 * @note    AMD64 doesn't have that many non-volatile registers and does sport
 *          32-bit address displacments, so we don't need pCtx.
 *
 *          On ARM64 pCtx allows us to directly address the whole register
 *          context without requiring a separate indexing register holding the
 *          offset. This saves an instruction loading the offset for each guest
 *          CPU context access, at the cost of a non-volatile register.
 *          Fortunately, ARM64 has quite a lot more registers.
 */
typedef
#ifdef RT_ARCH_AMD64
int FNIEMTBNATIVE(PVMCPUCC pVCpu)
#else
int FNIEMTBNATIVE(PVMCPUCC pVCpu, PCPUMCTX pCtx)
#endif
#if RT_CPLUSPLUS_PREREQ(201700)
    IEM_NOEXCEPT_MAY_LONGJMP
#endif
    ;
/** Pointer to a native IEM TB entry point function.
 * This will throw/longjmp on occation.  */
typedef FNIEMTBNATIVE *PFNIEMTBNATIVE;


/**
 * Translation block.
 *
 * The current plan is to just keep TBs and associated lookup hash table private
 * to each VCpu as that simplifies TB removal greatly (no races) and generally
 * avoids using expensive atomic primitives for updating lists and stuff.
 */
#pragma pack(2) /* to prevent the Thrd structure from being padded unnecessarily */
typedef struct IEMTB
{
    /** Next block with the same hash table entry. */
    struct IEMTB       *pNext;
    /** Usage counter. */
    uint32_t            cUsed;
    /** The IEMCPU::msRecompilerPollNow last time it was used. */
    uint32_t            msLastUsed;

    /** @name What uniquely identifies the block.
     * @{ */
    RTGCPHYS            GCPhysPc;
    /** IEMTB_F_XXX (i.e. IEM_F_XXX ++). */
    uint32_t            fFlags;
    union
    {
        struct
        {
            /**< Relevant CS X86DESCATTR_XXX bits. */
            uint16_t    fAttr;
        } x86;
    };
    /** @} */

    /** Number of opcode ranges. */
    uint8_t             cRanges;
    /** Statistics: Number of instructions in the block. */
    uint8_t             cInstructions;

    /** Type specific info. */
    union
    {
        struct
        {
            /** The call sequence table. */
            PIEMTHRDEDCALLENTRY paCalls;
            /** Number of calls in paCalls. */
            uint16_t            cCalls;
            /** Number of calls allocated. */
            uint16_t            cAllocated;
        } Thrd;
        struct
        {
            /** The native instructions (PFNIEMTBNATIVE). */
            PIEMNATIVEINSTR     paInstructions;
            /** Number of instructions pointed to by paInstructions. */
            uint32_t            cInstructions;
        } Native;
        /** Generic view for zeroing when freeing. */
        struct
        {
            uintptr_t           uPtr;
            uint32_t            uData;
        } Gen;
    };

    /** The allocation chunk this TB belongs to. */
    uint8_t             idxAllocChunk;
    /** The number of entries in the lookup table.
     * Because we're out of space, the TB lookup table is located before the
     * opcodes pointed to by pabOpcodes. */
    uint8_t             cTbLookupEntries;

    /** Number of bytes of opcodes stored in pabOpcodes.
     * @todo this field isn't really needed, aRanges keeps the actual info. */
    uint16_t            cbOpcodes;
    /** Pointer to the opcode bytes this block was recompiled from.
     * This also points to the TB lookup table, which starts cTbLookupEntries
     * entries before the opcodes (we don't have room atm for another point). */
    uint8_t            *pabOpcodes;

    union
    {
        /** Native recompilation debug info if enabled.
         * This is only generated by the native recompiler. */
        struct IEMTBDBG    *pDbgInfo;
        /** For threaded TBs and natives when debug info is disabled, this is the flat
         * PC corresponding to GCPhysPc. */
        RTGCPTR             FlatPc;
    };

    /* --- 64 byte cache line end --- */

    /** Opcode ranges.
     *
     * The opcode checkers and maybe TLB loading functions will use this to figure
     * out what to do.  The parameter will specify an entry and the opcode offset to
     * start at and the minimum number of bytes to verify (instruction length).
     *
     * When VT-x and AMD-V looks up the opcode bytes for an exitting instruction,
     * they'll first translate RIP (+ cbInstr - 1) to a physical address using the
     * code TLB (must have a valid entry for that address) and scan the ranges to
     * locate the corresponding opcodes. Probably.
     */
    struct IEMTBOPCODERANGE
    {
        /** Offset within pabOpcodes. */
        uint16_t        offOpcodes;
        /** Number of bytes. */
        uint16_t        cbOpcodes;
        /** The page offset. */
        RT_GCC_EXTENSION
        uint16_t        offPhysPage : 12;
        /** Unused bits. */
        RT_GCC_EXTENSION
        uint16_t        u2Unused    :  2;
        /** Index into GCPhysPc + aGCPhysPages for the physical page address. */
        RT_GCC_EXTENSION
        uint16_t        idxPhysPage :  2;
    } aRanges[8];

    /** Physical pages that this TB covers.
     * The GCPhysPc w/o page offset is element zero, so starting here with 1. */
    RTGCPHYS            aGCPhysPages[2];
} IEMTB;
#pragma pack()
AssertCompileMemberAlignment(IEMTB, GCPhysPc, sizeof(RTGCPHYS));
AssertCompileMemberAlignment(IEMTB, Thrd, sizeof(void *));
AssertCompileMemberAlignment(IEMTB, pabOpcodes, sizeof(void *));
AssertCompileMemberAlignment(IEMTB, pDbgInfo, sizeof(void *));
AssertCompileMemberAlignment(IEMTB, aGCPhysPages, sizeof(RTGCPHYS));
AssertCompileMemberOffset(IEMTB, aRanges, 64);
AssertCompileMemberSize(IEMTB, aRanges[0], 6);
#if 1
AssertCompileSize(IEMTB, 128);
# define IEMTB_SIZE_IS_POWER_OF_TWO /**< The IEMTB size is a power of two. */
#else
AssertCompileSize(IEMTB, 168);
# undef  IEMTB_SIZE_IS_POWER_OF_TWO
#endif

/** Pointer to a translation block. */
typedef IEMTB *PIEMTB;
/** Pointer to a const translation block. */
typedef IEMTB const *PCIEMTB;

/** Gets address of the given TB lookup table entry. */
#define IEMTB_GET_TB_LOOKUP_TAB_ENTRY(a_pTb, a_idx) \
    ((PIEMTB *)&(a_pTb)->pabOpcodes[-(int)((a_pTb)->cTbLookupEntries - (a_idx)) * sizeof(PIEMTB)])

/**
 * Gets the physical address for a TB opcode range.
 */
DECL_FORCE_INLINE(RTGCPHYS) iemTbGetRangePhysPageAddr(PCIEMTB pTb, uint8_t idxRange)
{
    Assert(idxRange < RT_MIN(pTb->cRanges, RT_ELEMENTS(pTb->aRanges)));
    uint8_t const idxPage = pTb->aRanges[idxRange].idxPhysPage;
    Assert(idxPage <= RT_ELEMENTS(pTb->aGCPhysPages));
    if (idxPage == 0)
        return pTb->GCPhysPc & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    Assert(!(pTb->aGCPhysPages[idxPage - 1] & GUEST_PAGE_OFFSET_MASK));
    return pTb->aGCPhysPages[idxPage - 1];
}


/**
 * A chunk of memory in the TB allocator.
 */
typedef struct IEMTBCHUNK
{
    /** Pointer to the translation blocks in this chunk. */
    PIEMTB          paTbs;
#ifdef IN_RING0
    /** Allocation handle. */
    RTR0MEMOBJ      hMemObj;
#endif
} IEMTBCHUNK;

/**
 * A per-CPU translation block allocator.
 *
 * Because of how the IEMTBCACHE uses the lower 6 bits of the TB address to keep
 * the length of the collision list, and of course also for cache line alignment
 * reasons, the TBs must be allocated with at least 64-byte alignment.
 * Memory is there therefore allocated using one of the page aligned allocators.
 *
 *
 * To avoid wasting too much memory, it is allocated piecemeal as needed,
 * in chunks (IEMTBCHUNK) of 2 MiB or more.  The TB has an 8-bit chunk index
 * that enables us to quickly calculate the allocation bitmap position when
 * freeing the translation block.
 */
typedef struct IEMTBALLOCATOR
{
    /** Magic value (IEMTBALLOCATOR_MAGIC). */
    uint32_t        uMagic;

#ifdef IEMTB_SIZE_IS_POWER_OF_TWO
    /** Mask corresponding to cTbsPerChunk - 1. */
    uint32_t        fChunkMask;
    /** Shift count corresponding to cTbsPerChunk. */
    uint8_t         cChunkShift;
#else
    uint32_t        uUnused;
    uint8_t         bUnused;
#endif
    /** Number of chunks we're allowed to allocate. */
    uint8_t         cMaxChunks;
    /** Number of chunks currently populated. */
    uint16_t        cAllocatedChunks;
    /** Number of translation blocks per chunk. */
    uint32_t        cTbsPerChunk;
    /** Chunk size. */
    uint32_t        cbPerChunk;

    /** The maximum number of TBs. */
    uint32_t        cMaxTbs;
    /** Total number of TBs in the populated chunks.
     * (cAllocatedChunks * cTbsPerChunk) */
    uint32_t        cTotalTbs;
    /** The current number of TBs in use.
     * The number of free TBs: cAllocatedTbs - cInUseTbs; */
    uint32_t        cInUseTbs;
    /** Statistics: Number of the cInUseTbs that are native ones. */
    uint32_t        cNativeTbs;
    /** Statistics: Number of the cInUseTbs that are threaded ones. */
    uint32_t        cThreadedTbs;

    /** Where to start pruning TBs from when we're out.
     *  See iemTbAllocatorAllocSlow for details. */
    uint32_t        iPruneFrom;
    /** Where to start pruning native TBs from when we're out of executable memory.
     *  See iemTbAllocatorFreeupNativeSpace for details. */
    uint32_t        iPruneNativeFrom;
    uint64_t        u64Padding;

    /** Statistics: Number of TB allocation calls. */
    STAMCOUNTER     StatAllocs;
    /** Statistics: Number of TB free calls. */
    STAMCOUNTER     StatFrees;
    /** Statistics: Time spend pruning. */
    STAMPROFILE     StatPrune;
    /** Statistics: Time spend pruning native TBs. */
    STAMPROFILE     StatPruneNative;

    /** The delayed free list (see iemTbAlloctorScheduleForFree). */
    PIEMTB          pDelayedFreeHead;
    /* Head of the list of free TBs. */
    PIEMTB          pTbsFreeHead;

    /** Allocation chunks. */
    IEMTBCHUNK      aChunks[256];
} IEMTBALLOCATOR;
/** Pointer to a TB allocator. */
typedef struct IEMTBALLOCATOR *PIEMTBALLOCATOR;

/** Magic value for the TB allocator (Emmet Harley Cohen). */
#define IEMTBALLOCATOR_MAGIC        UINT32_C(0x19900525)


/**
 * A per-CPU translation block cache (hash table).
 *
 * The hash table is allocated once during IEM initialization and size double
 * the max TB count, rounded up to the nearest power of two (so we can use and
 * AND mask rather than a rest division when hashing).
 */
typedef struct IEMTBCACHE
{
    /** Magic value (IEMTBCACHE_MAGIC). */
    uint32_t        uMagic;
    /** Size of the hash table.  This is a power of two. */
    uint32_t        cHash;
    /** The mask corresponding to cHash. */
    uint32_t        uHashMask;
    uint32_t        uPadding;

    /** @name Statistics
     * @{ */
    /** Number of collisions ever. */
    STAMCOUNTER     cCollisions;

    /** Statistics: Number of TB lookup misses. */
    STAMCOUNTER     cLookupMisses;
    /** Statistics: Number of TB lookup hits via hash table (debug only). */
    STAMCOUNTER     cLookupHits;
    /** Statistics: Number of TB lookup hits via TB associated lookup table (debug only). */
    STAMCOUNTER     cLookupHitsViaTbLookupTable;
    STAMCOUNTER     auPadding2[2];
    /** Statistics: Collision list length pruning. */
    STAMPROFILE     StatPrune;
    /** @} */

    /** The hash table itself.
     * @note The lower 6 bits of the pointer is used for keeping the collision
     *       list length, so we can take action when it grows too long.
     *       This works because TBs are allocated using a 64 byte (or
     *       higher) alignment from page aligned chunks of memory, so the lower
     *       6 bits of the address will always be zero.
     *       See IEMTBCACHE_PTR_COUNT_MASK, IEMTBCACHE_PTR_MAKE and friends.
     */
    RT_FLEXIBLE_ARRAY_EXTENSION
    PIEMTB          apHash[RT_FLEXIBLE_ARRAY];
} IEMTBCACHE;
/** Pointer to a per-CPU translation block cache. */
typedef IEMTBCACHE *PIEMTBCACHE;

/** Magic value for IEMTBCACHE (Johnny O'Neal). */
#define IEMTBCACHE_MAGIC            UINT32_C(0x19561010)

/** The collision count mask for IEMTBCACHE::apHash entries. */
#define IEMTBCACHE_PTR_COUNT_MASK               ((uintptr_t)0x3f)
/** The max collision count for IEMTBCACHE::apHash entries before pruning. */
#define IEMTBCACHE_PTR_MAX_COUNT                ((uintptr_t)0x30)
/** Combine a TB pointer and a collision list length into a value for an
 *  IEMTBCACHE::apHash entry. */
#define IEMTBCACHE_PTR_MAKE(a_pTb, a_cCount)    (PIEMTB)((uintptr_t)(a_pTb) | (a_cCount))
/** Combine a TB pointer and a collision list length into a value for an
 *  IEMTBCACHE::apHash entry. */
#define IEMTBCACHE_PTR_GET_TB(a_pHashEntry)     (PIEMTB)((uintptr_t)(a_pHashEntry) & ~IEMTBCACHE_PTR_COUNT_MASK)
/** Combine a TB pointer and a collision list length into a value for an
 *  IEMTBCACHE::apHash entry. */
#define IEMTBCACHE_PTR_GET_COUNT(a_pHashEntry)  ((uintptr_t)(a_pHashEntry) & IEMTBCACHE_PTR_COUNT_MASK)

/**
 * Calculates the hash table slot for a TB from physical PC address and TB flags.
 */
#define IEMTBCACHE_HASH(a_paCache, a_fTbFlags, a_GCPhysPc) \
    IEMTBCACHE_HASH_NO_KEY_MASK(a_paCache, (a_fTbFlags) & IEMTB_F_KEY_MASK, a_GCPhysPc)

/**
 * Calculates the hash table slot for a TB from physical PC address and TB
 * flags, ASSUMING the caller has applied IEMTB_F_KEY_MASK to @a a_fTbFlags.
 */
#define IEMTBCACHE_HASH_NO_KEY_MASK(a_paCache, a_fTbFlags, a_GCPhysPc) \
    (((uint32_t)(a_GCPhysPc) ^ (a_fTbFlags)) & (a_paCache)->uHashMask)


/** @name IEMBRANCHED_F_XXX - Branched indicator (IEMCPU::fTbBranched).
 *
 * These flags parallels the main IEM_CIMPL_F_BRANCH_XXX flags.
 *
 * @{ */
/** Value if no branching happened recently. */
#define IEMBRANCHED_F_NO            UINT8_C(0x00)
/** Flag set if direct branch, clear if absolute or indirect. */
#define IEMBRANCHED_F_DIRECT        UINT8_C(0x01)
/** Flag set if indirect branch, clear if direct or relative. */
#define IEMBRANCHED_F_INDIRECT      UINT8_C(0x02)
/** Flag set if relative branch, clear if absolute or indirect. */
#define IEMBRANCHED_F_RELATIVE      UINT8_C(0x04)
/** Flag set if conditional branch, clear if unconditional. */
#define IEMBRANCHED_F_CONDITIONAL   UINT8_C(0x08)
/** Flag set if it's a far branch.
 * @note x86 specific */
#define IEMBRANCHED_F_FAR           UINT8_C(0x10)
/** Flag set if the stack pointer is modified. */
#define IEMBRANCHED_F_STACK         UINT8_C(0x20)
/** Flag set if the stack pointer and (maybe) the stack segment are modified.
 * @note x86 specific */
#define IEMBRANCHED_F_STACK_FAR     UINT8_C(0x40)
/** Flag set (by IEM_MC_REL_JMP_XXX) if it's a zero bytes relative jump. */
#define IEMBRANCHED_F_ZERO          UINT8_C(0x80)
/** @} */


/** @def IEM_MAX_MEM_MAPPINGS
 * Maximum number of concurrent memory needed by the target architecture.
 * @x86     There are a few instructions with two memory operands (push/pop [mem],
 *          string instructions).  We add another entry for safety.
 * @arm     Except for the recently specified memcpy/move instructions,
 *          ARM instruction takes at most one memory operand.  We use 1 and add
 *          another entry for safety, ignoring the memcpy instructions for now. */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING) /* for now: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
# define IEM_MAX_MEM_MAPPINGS       3
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define IEM_MAX_MEM_MAPPINGS       2
#else
# error "port me"
#endif

/** @def IEM_BOUNCE_BUFFER_SIZE
 * The size of the bounce buffers.  This is dictated by the largest memory
 * operand of the target architecture.
 * @x86     fxsave/fxrstor takes a 512 byte operand. Whether we actually need a
 *          512 byte bounce buffer for it is questionable...
 * @arm     Currently we shouldn't need more than 64 bytes here (ld64b, ld4). */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING) /* for now: */ || defined(VBOX_VMM_TARGET_AGNOSTIC)
# define IEM_BOUNCE_BUFFER_SIZE     512
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define IEM_BOUNCE_BUFFER_SIZE     64
#else
# error "port me"
#endif


/**
 * The per-CPU IEM state.
 */
typedef struct IEMCPU
{
    /** Info status code that needs to be propagated to the IEM caller.
     * This cannot be passed internally, as it would complicate all success
     * checks within the interpreter making the code larger and almost impossible
     * to get right.  Instead, we'll store status codes to pass on here.  Each
     * source of these codes will perform appropriate sanity checks. */
    int32_t                 rcPassUp;                                                                       /* 0x00 */
    /** Execution flag, IEM_F_XXX. */
    uint32_t                fExec;                                                                          /* 0x04 */

    /** @name Decoder state.
     * @{ */
#ifdef IEM_WITH_CODE_TLB
    /** The offset of the next instruction byte. */
    uint32_t                offInstrNextByte;                                                               /* 0x08 */
# if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
    /** X86: The number of bytes available at pbInstrBuf for the current
     * instruction. This takes the max opcode length into account so that doesn't
     * need to be checked separately. */
    uint32_t                cbInstrBuf;                                                                /* x86: 0x0c */
# else
    /** The number of bytes available at pbInstrBuf in total (for IEMExecLots).
     * @note Set to zero when the code TLB is flushed to trigger TLB reload. */
    uint32_t                cbInstrBufTotal;                                                          /* !x86: 0x0c */
# endif
    /** Pointer to the page containing PC, user specified buffer or abOpcode.
     * This can be NULL if the page isn't mappable for some reason, in which
     * case we'll do fallback stuff.
     *
     * If we're executing an instruction from a user specified buffer,
     * IEMExecOneWithPrefetchedByPC and friends, this is not necessarily a page
     * aligned pointer but pointer to the user data.
     *
     * @x86 For instructions crossing pages, this will start on the first page and
     * be advanced to the next page by the time we've decoded the instruction.  This
     * therefore precludes stuff like <tt>pbInstrBuf[offInstrNextByte + cbInstrBuf - cbCurInstr]</tt>
     */
    uint8_t const          *pbInstrBuf;                                                                     /* 0x10 */
# if ARCH_BITS == 32
    uint32_t                uInstrBufHigh; /** The high dword of the host context pbInstrBuf member. */
# endif
    /** The program counter corresponding to pbInstrBuf.
     * This is set to a non-canonical address when we need to invalidate it. */
    uint64_t                uInstrBufPc;                                                                    /* 0x18 */
    /** The guest physical address corresponding to pbInstrBuf. */
    RTGCPHYS                GCPhysInstrBuf;                                                                 /* 0x20 */
# if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
    /** X86: The number of bytes available at pbInstrBuf in total (for IEMExecLots).
     * This takes the CS segment limit into account.
     * @note Set to zero when the code TLB is flushed to trigger TLB reload. */
    uint16_t                cbInstrBufTotal;                                                           /* x86: 0x28 */
    /** X86: Offset into pbInstrBuf of the first byte of the current instruction.
     * Can be negative to efficiently handle cross page instructions. */
    int16_t                 offCurInstrStart;                                                          /* x86: 0x2a */
# endif

# if (!defined(IEM_WITH_OPAQUE_DECODER_STATE) && defined(VBOX_VMM_TARGET_X86)) || defined(DOXYGEN_RUNNING)
    /** X86: The prefix mask (IEM_OP_PRF_XXX). */
    uint32_t                fPrefixes;                                                                 /* x86: 0x2c */
    /** X86: The extra REX ModR/M register field bit (REX.R << 3). */
    uint8_t                 uRexReg;                                                                   /* x86: 0x30 */
    /** X86: The extra REX ModR/M r/m field, SIB base and opcode reg bit
     * (REX.B << 3). */
    uint8_t                 uRexB;                                                                     /* x86: 0x31 */
    /** X86: The extra REX SIB index field bit (REX.X << 3). */
    uint8_t                 uRexIndex;                                                                 /* x86: 0x32 */

    /** X86: The effective segment register (X86_SREG_XXX). */
    uint8_t                 iEffSeg;                                                                   /* x86: 0x33 */

    /** X86: The offset of the ModR/M byte relative to the start of the instruction. */
    uint8_t                 offModRm;                                                                  /* x86: 0x34 */

#  ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    /** X86: The current offset into abOpcode. */
    uint8_t                 offOpcode;                                                                 /* x86: 0x35 */
#  else
    uint8_t                 bUnused;                                                                   /* x86: 0x35 */
#  endif
# else  /* IEM_WITH_OPAQUE_DECODER_STATE || !X86 */
#  ifdef VBOX_VMM_TARGET_X86
    uint8_t                 abOpaqueDecoderPart1[0x36 - 0x2c];
#  endif
# endif /* IEM_WITH_OPAQUE_DECODER_STATE || !X86 */

#else  /* !IEM_WITH_CODE_TLB */
#  ifndef IEM_WITH_OPAQUE_DECODER_STATE
    /** The size of what has currently been fetched into abOpcode. */
    uint8_t                 cbOpcode;                                                                       /*       0x08 */
    /** The current offset into abOpcode. */
    uint8_t                 offOpcode;                                                                      /*       0x09 */

#   ifdef VBOX_VMM_TARGET_X86
    /** X86: The offset of the ModR/M byte relative to the start of the
     *  instruction. */
    uint8_t                 offModRm;                                                                  /* x86:       0x0a */

    /** X86: The effective segment register (X86_SREG_XXX). */
    uint8_t                 iEffSeg;                                                                   /* x86:       0x0b */

    /** X86: The prefix mask (IEM_OP_PRF_XXX). */
    uint32_t                fPrefixes;                                                                 /* x86:       0x0c */
    /** X86: The extra REX ModR/M register field bit (REX.R << 3). */
    uint8_t                 uRexReg;                                                                   /* x86:       0x10 */
    /** X86: The extra REX ModR/M r/m field, SIB base and opcode reg bit
     * (REX.B << 3). */
    uint8_t                 uRexB;                                                                     /* x86:       0x11 */
    /** X86: The extra REX SIB index field bit (REX.X << 3). */
    uint8_t                 uRexIndex;                                                                 /* x86:       0x12 */
#   endif
# else  /* IEM_WITH_OPAQUE_DECODER_STATE */
#   ifdef VBOX_VMM_TARGET_X86
    uint8_t                 abOpaqueDecoderPart1[0x13 - 0x08];
#   else
    uint8_t                 abOpaqueDecoderPart1[2];
#   endif
# endif /* IEM_WITH_OPAQUE_DECODER_STATE */
#endif /* !IEM_WITH_CODE_TLB */

#if  (!defined(IEM_WITH_OPAQUE_DECODER_STATE) && (defined(VBOX_VMM_TARGET_X86) || !defined(IEM_WITH_CODE_TLB))) \
  || defined(DOXGYEN_RUNNING)
# ifdef VBOX_VMM_TARGET_X86
    /** X86: The effective operand mode. */
    IEMMODE                 enmEffOpSize;                                                              /* x86: 0x36, 0x13 */
    /** X86: The default addressing mode. */
    IEMMODE                 enmDefAddrMode;                                                            /* x86: 0x37, 0x14 */
    /** X86: The effective addressing mode. */
    IEMMODE                 enmEffAddrMode;                                                            /* x86: 0x38, 0x15 */
    /** X86: The default operand mode. */
    IEMMODE                 enmDefOpSize;                                                              /* x86: 0x39, 0x16 */

    /** X86: Prefix index (VEX.pp) for two byte and three byte tables. */
    uint8_t                 idxPrefix;                                                                 /* x86: 0x3a, 0x17 */
    /** X86: 3rd VEX/EVEX/XOP register.
     * Please use IEM_GET_EFFECTIVE_VVVV to access.  */
    uint8_t                 uVex3rdReg;                                                                /* x86: 0x3b, 0x18 */
    /** X86: The VEX/EVEX/XOP length field. */
    uint8_t                 uVexLength;                                                                /* x86: 0x3c, 0x19 */
    /** X86: Additional EVEX stuff. */
    uint8_t                 fEvexStuff;                                                                /* x86: 0x3d, 0x1a */

#  ifndef IEM_WITH_CODE_TLB
    /** Explicit alignment padding. */
    uint8_t                 abAlignment2a[1];                                                          /* x86:       0x1b */
#  endif
    /** X86: The FPU opcode (FOP). */
    uint16_t                uFpuOpcode;                                                                /* x86: 0x3e, 0x1c */
#  ifndef IEM_WITH_CODE_TLB
    /** Opcode buffer alignment padding. */
    uint8_t                 abAlignment2b[2];                                                          /* x86:       0x1e */
#  endif
# else  /* !VBOX_VMM_TARGET_X86 */
    /** Opcode buffer alignment padding. */
    uint8_t                 abAlignment2b[2];                                                         /* !x86:       0x0a */
# endif /* !VBOX_VMM_TARGET_X86 */

    /** The opcode bytes. */
# ifdef VBOX_VMM_TARGET_X86
    uint8_t                 abOpcode[15];                                                              /* x86: 0x40, 0x20 */
# else
    union
    {
        uint8_t             abOpcode[  28];                                                           /* !x86:       0x0c(..0x28) */
        uint16_t            au16Opcode[14];
        uint32_t            au32Opcode[ 7];
    };
# endif
# ifdef VBOX_VMM_TARGET_X86
    /** X86: Explicit alignment padding. */
#  ifdef IEM_WITH_CODE_TLB
    //uint8_t                 abAlignment2c[0x4f - 0x4f];                                              /* x86: 0x4f */
#  else
    uint8_t                 abAlignment2c[0x4f - 0x2f];                                                /* x86:       0x2f */
#  endif
# elif !defined(VBOX_VMM_TARGET_ARMV8)
    uint8_t                 abAlignment2c[0x4f - 0x28];                                         /* !x86: !arm:       0x28 */
# endif

#else  /* IEM_WITH_OPAQUE_DECODER_STATE || (!x86 && TLB) */
# ifdef IEM_WITH_CODE_TLB
#  ifdef VBOX_VMM_TARGET_X86
    uint8_t                 abOpaqueDecoderPart2[0x4f - 0x36];
#  elif !defined(VBOX_VMM_TARGET_ARMV8)
    uint8_t                 abOpaqueDecoderPart2[0x4f - 0x28];
#  endif
# else
#  ifdef VBOX_VMM_TARGET_X86
    uint8_t                 abOpaqueDecoderPart2[0x4f - 0x13];
#  elif defined(VBOX_VMM_TARGET_ARMV8)
    uint8_t                 abOpaqueDecoderPart2[0x28 - 0x0a];
#  else
    uint8_t                 abOpaqueDecoderPart2[0x4f - 0x09];
#  endif
# endif
#endif /* IEM_WITH_OPAQUE_DECODER_STATE */
    /** @} */

#  ifdef VBOX_VMM_TARGET_ARMV8
    RTGCPTR                 GCPtrEffT0SzAndMask;                                                       /* arm: 0x28 */
    RTGCPTR                 GCPtrEffT1SzOrMask;                                                        /* arm: 0x30 */
    uint8_t                 abPadding3[0x4f - 0x38];                                                   /* arm: 0x38 */
#  endif

    /** The number of active guest memory mappings. */
    uint8_t                 cActiveMappings;                                                                /* 0x4f, 0x4f */

    /** Records for tracking guest memory mappings. */
    struct
    {
        /** The address of the mapped bytes. */
        R3R0PTRTYPE(void *) pv;
        /** The access flags (IEM_ACCESS_XXX).
         * IEM_ACCESS_INVALID if the entry is unused. */
        uint32_t            fAccess;
#if HC_ARCH_BITS == 64
        uint32_t            u32Alignment4; /**< Alignment padding. */
#endif
    } aMemMappings[IEM_MAX_MEM_MAPPINGS];                                           /* arm: 0x50 LB 0x20  x86: 0x50 LB 0x30 */

    /** Locking records for the mapped memory. */
    union
    {
        PGMPAGEMAPLOCK      Lock;
        uint64_t            au64Padding[2];
    } aMemMappingLocks[IEM_MAX_MEM_MAPPINGS];                                       /* arm: 0x70 LB 0x20  x86: 0x80 LB 0x30 */

    /** Bounce buffer info.
     * This runs in parallel to aMemMappings. */
    struct
    {
        /** The physical address of the first byte. */
        RTGCPHYS            GCPhysFirst;
        /** The physical address of the second page. */
        RTGCPHYS            GCPhysSecond;
        /** The number of bytes in the first page. */
        uint16_t            cbFirst;
        /** The number of bytes in the second page. */
        uint16_t            cbSecond;
        /** Whether it's unassigned memory. */
        bool                fUnassigned;
        /** Explicit alignment padding. */
        bool                afAlignment5[3];
    } aMemBbMappings[IEM_MAX_MEM_MAPPINGS];                                         /* arm: 0x90 LB 0x30  x86: 0xb0 LB 0x48 */

    /** The flags of the current exception / interrupt.
     * @note X86 specific? */
    uint32_t                fCurXcpt;                                               /* arm: 0xc0          x86: 0xf8 */
    /** The current exception / interrupt.
     *@note X86 specific? */
    uint8_t                 uCurXcpt;                                               /* arm: 0xc4          x86: 0xfc */
    /** Exception / interrupt recursion depth.
     *@note X86 specific? */
    int8_t                  cXcptRecursions;                                        /* arm: 0xc5          x86: 0xfb */

    /** The next unused mapping index.
     * @todo try find room for this up with cActiveMappings. */
    uint8_t                 iNextMapping;                                           /* arm: 0xc6          x86: 0xfd */
    uint8_t                 abAlignment7[IEM_MAX_MEM_MAPPINGS == 3 ? 1 : 0x39];

    /** Bounce buffer storage.
     * This runs in parallel to aMemMappings and aMemBbMappings. */
    struct
    {
        uint8_t             ab[IEM_BOUNCE_BUFFER_SIZE];
    } aBounceBuffers[IEM_MAX_MEM_MAPPINGS];                                        /* arm: 0x100 LB 0x80  x86: 0x100 LB 0x600 */


    /** Pointer set jump buffer - ring-3 context. */
    R3PTRTYPE(jmp_buf *)    pJmpBufR3;
    /** Pointer set jump buffer - ring-0 context. */
    R0PTRTYPE(jmp_buf *)    pJmpBufR0;

    /** @todo Should move this near @a fCurXcpt later. */
    /** The CR2 for the current exception / interrupt. */
    uint64_t                uCurXcptCr2;
    /** The error code for the current exception / interrupt. */
    uint32_t                uCurXcptErr;

    /** @name Statistics
     * @{  */
    /** The number of instructions we've executed. */
    uint32_t                cInstructions;
    /** The number of potential exits. */
    uint32_t                cPotentialExits;
    /** Counts the VERR_IEM_INSTR_NOT_IMPLEMENTED returns. */
    uint32_t                cRetInstrNotImplemented;
    /** Counts the VERR_IEM_ASPECT_NOT_IMPLEMENTED returns. */
    uint32_t                cRetAspectNotImplemented;
    /** Counts informational statuses returned (other than VINF_SUCCESS). */
    uint32_t                cRetInfStatuses;
    /** Counts other error statuses returned. */
    uint32_t                cRetErrStatuses;
    /** Number of times rcPassUp has been used. */
    uint32_t                cRetPassUpStatus;
    /** Number of times RZ left with instruction commit pending for ring-3. */
    uint32_t                cPendingCommit;
    /** Number of misaligned (host sense) atomic instruction accesses. */
    uint32_t                cMisalignedAtomics;
    /** Number of long jumps. */
    uint32_t                cLongJumps;
    /** @} */

    /** @name Target CPU information.
     * @{ */
#if IEM_CFG_TARGET_CPU == IEMTARGETCPU_DYNAMIC
    /** The target CPU. */
    uint8_t                 uTargetCpu;
#else
    uint8_t                 bTargetCpuPadding;
#endif
    /** For selecting assembly works matching the target CPU EFLAGS behaviour, see
     * IEMTARGETCPU_EFL_BEHAVIOR_XXX for values, with the 1st entry for when no
     * native host support and the 2nd for when there is.
     *
     * The two values are typically indexed by a g_CpumHostFeatures bit.
     *
     * This is for instance used for the BSF & BSR instructions where AMD and
     * Intel CPUs produce different EFLAGS. */
    uint8_t                 aidxTargetCpuEflFlavour[2];

    /** The CPU vendor. */
    CPUMCPUVENDOR           enmCpuVendor;
    /** @} */

    /** Counts RDMSR \#GP(0) LogRel(). */
    uint8_t                 cLogRelRdMsr;
    /** Counts WRMSR \#GP(0) LogRel(). */
    uint8_t                 cLogRelWrMsr;
    /** Alignment padding. */
    uint8_t                 abAlignment9[50];


    /** @name Recompiled Exection
     * @{ */
    /** Pointer to the current translation block.
     * This can either be one being executed or one being compiled. */
    R3PTRTYPE(PIEMTB)       pCurTbR3;
#ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
    /** Frame pointer for the last native TB to execute. */
    R3PTRTYPE(void *)       pvTbFramePointerR3;
#else
    R3PTRTYPE(void *)       pvUnusedR3;
#endif
#ifdef IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
    /** The saved host floating point control register (MXCSR on x86, FPCR on arm64)
     * needing restore when the TB finished, IEMNATIVE_SIMD_FP_CTRL_REG_NOT_MODIFIED indicates the TB
     * didn't modify it so we don't need to restore it. */
# ifdef RT_ARCH_AMD64
    uint32_t                uRegFpCtrl;
    /** Temporary copy of MXCSR for stmxcsr/ldmxcsr (so we don't have to fiddle with stack pointers). */
    uint32_t                uRegMxcsrTmp;
# elif defined(RT_ARCH_ARM64)
    uint64_t                uRegFpCtrl;
# else
#  error "Port me"
# endif
#else
    uint64_t                u64Unused;
#endif
    /** Pointer to the ring-3 TB cache for this EMT. */
    R3PTRTYPE(PIEMTBCACHE)  pTbCacheR3;
    /** Pointer to the ring-3 TB lookup entry.
     * This either points to pTbLookupEntryDummyR3 or an actually lookuptable
     * entry, thus it can always safely be used w/o NULL checking. */
    R3PTRTYPE(PIEMTB *)     ppTbLookupEntryR3;
#if 0 /* unused */
    /** The PC (RIP) at the start of pCurTbR3/pCurTbR0.
     * The TBs are based on physical addresses, so this is needed to correleated
     * RIP to opcode bytes stored in the TB (AMD-V / VT-x). */
    uint64_t                uCurTbStartPc;
#endif

    /** Number of threaded TBs executed. */
    uint64_t                cTbExecThreaded;
    /** Number of native TBs executed. */
    uint64_t                cTbExecNative;

    /** The number of IRQ/FF checks till the next timer poll call. */
    uint32_t                cTbsTillNextTimerPoll;
    /** The virtual sync time at the last timer poll call in milliseconds. */
    uint32_t                msRecompilerPollNow;
    /** The virtual sync time at the last timer poll call in nanoseconds. */
    uint64_t                nsRecompilerPollNow;
    /** The previous cTbsTillNextTimerPoll value. */
    uint32_t                cTbsTillNextTimerPollPrev;

    /** The current instruction number in a native TB.
     * This is set by code that may trigger an unexpected TB exit (throw/longjmp)
     * and will be picked up by the TB execution loop. Only used when
     * IEMNATIVE_WITH_INSTRUCTION_COUNTING is defined. */
    uint8_t                 idxTbCurInstr;
    /** @} */

    /** @name Recompilation
     * @{ */
    /** Whether we need to check the opcode bytes for the current instruction.
     * This is set by a previous instruction if it modified memory or similar.  */
    bool                    fTbCheckOpcodes;
    /** Indicates whether and how we just branched - IEMBRANCHED_F_XXX. */
    uint8_t                 fTbBranched;
    /** Set when GCPhysInstrBuf is updated because of a page crossing. */
    bool                    fTbCrossedPage;
    /** Whether to end the current TB. */
    bool                    fEndTb;
    /** Indicates that the current instruction is an STI.  This is set by the
     * iemCImpl_sti code and subsequently cleared by the recompiler. */
    bool                    fTbCurInstrIsSti;
    /** Spaced reserved for recompiler data / alignment. */
    bool                    afRecompilerStuff1[1];
    /** Number of instructions before we need emit an IRQ check call again.
     * This helps making sure we don't execute too long w/o checking for
     * interrupts and immediately following instructions that may enable
     * interrupts (e.g. POPF, IRET, STI).  With STI an additional hack is
     * required to make sure we check following the next instruction as well, see
     * fTbCurInstrIsSti. */
    uint8_t                 cInstrTillIrqCheck;
    /** The index of the last CheckIrq call during threaded recompilation. */
    uint16_t                idxLastCheckIrqCallNo;
    /** The size of the IEMTB::pabOpcodes allocation in pThrdCompileTbR3. */
    uint16_t                cbOpcodesAllocated;
    /** The IEMTB::cUsed value when to attempt native recompilation of a TB. */
    uint32_t                uTbNativeRecompileAtUsedCount;
    /** The IEM_CIMPL_F_XXX mask for the current instruction. */
    uint32_t                fTbCurInstr;
    /** The IEM_CIMPL_F_XXX mask for the previous instruction. */
    uint32_t                fTbPrevInstr;
    /** Strict: Tracking skipped EFLAGS calculations.  Any bits set here are
     *  currently not up to date in EFLAGS. */
    uint32_t                fSkippingEFlags;

    /** Fixed TB used for threaded recompilation.
     * This is allocated once with maxed-out sizes and re-used afterwards. */
    R3PTRTYPE(PIEMTB)       pThrdCompileTbR3;
    /** Pointer to the ring-3 TB allocator for this EMT. */
    R3PTRTYPE(PIEMTBALLOCATOR) pTbAllocatorR3;
    /** Pointer to the ring-3 executable memory allocator for this EMT. */
    R3PTRTYPE(struct IEMEXECMEMALLOCATOR *) pExecMemAllocatorR3;
    /** Pointer to the native recompiler state for ring-3. */
    R3PTRTYPE(struct IEMRECOMPILERSTATE *)  pNativeRecompilerStateR3;
    /** Dummy entry for ppTbLookupEntryR3. */
    R3PTRTYPE(PIEMTB)       pTbLookupEntryDummyR3;
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    /** The debug code advances this register as if it was CPUMCTX::rip and we
     * didn't do delayed PC updating.  When CPUMCTX::rip is finally updated,
     * the result is compared with this value. */
    uint64_t                uPcUpdatingDebug;
#elif defined(VBOX_WITH_SAVE_THREADED_TBS_FOR_PROFILING)
    /** The SSM handle used for saving threaded TBs for recompiler profiling. */
    R3PTRTYPE(PSSMHANDLE)   pSsmThreadedTbsForProfiling;
#else
    uint64_t                u64Placeholder;
#endif
    /**
     *  Whether we should use the host instruction invalidation APIs of the
     *  host OS or our own version of it (macOS).  */
    uint8_t                 fHostICacheInvalidation;
#define IEMNATIVE_ICACHE_F_USE_HOST_API     UINT8_C(0x01) /**< Use the host API (macOS) instead of our code. */
#define IEMNATIVE_ICACHE_F_END_WITH_ISH     UINT8_C(0x02) /**< Whether to end with a ISH barrier (arm). */
    bool                    afRecompilerStuff2[7];
    /** @} */

    /** Dummy TLB entry used for accesses to pages with databreakpoints. */
    IEMTLBENTRY             DataBreakpointTlbe;

    /** Threaded TB statistics: Times TB execution was broken off before reaching the end. */
    STAMCOUNTER             StatTbThreadedExecBreaks;
    /** Statistics: Times BltIn_CheckIrq breaks out of the TB. */
    STAMCOUNTER             StatCheckIrqBreaks;
    /** Statistics: Times BltIn_CheckTimers breaks direct linking TBs. */
    STAMCOUNTER             StatCheckTimersBreaks;
    /** Statistics: Times BltIn_CheckMode breaks out of the TB. */
    STAMCOUNTER             StatCheckModeBreaks;
    /** Threaded TB statistics: Times execution break on call with lookup entries. */
    STAMCOUNTER             StatTbThreadedExecBreaksWithLookup;
    /** Threaded TB statistics: Times execution break on call without lookup entries. */
    STAMCOUNTER             StatTbThreadedExecBreaksWithoutLookup;
    /** Statistics: Times a post jump target check missed and had to find new TB. */
    STAMCOUNTER             StatCheckBranchMisses;
    /** Statistics: Times a jump or page crossing required a TB with CS.LIM checking. */
    STAMCOUNTER             StatCheckNeedCsLimChecking;
    /** Statistics: Times a loop was detected within a TB. */
    STAMCOUNTER             StatTbLoopInTbDetected;
    /** Statistics: Times a loop back to the start of the TB was detected. */
    STAMCOUNTER             StatTbLoopFullTbDetected;
    /** Statistics: Times a loop back to the start of the TB was detected, var 2. */
    STAMCOUNTER             StatTbLoopFullTbDetected2;
    /** Exec memory allocator statistics: Number of times allocaintg executable memory failed. */
    STAMCOUNTER             StatNativeExecMemInstrBufAllocFailed;
    /** Native TB statistics: Number of fully recompiled TBs. */
    STAMCOUNTER             StatNativeFullyRecompiledTbs;
    /** TB statistics: Number of instructions per TB. */
    STAMPROFILE             StatTbInstr;
    /** TB statistics: Number of TB lookup table entries per TB. */
    STAMPROFILE             StatTbLookupEntries;
    /** Threaded TB statistics: Number of calls per TB. */
    STAMPROFILE             StatTbThreadedCalls;
    /** Native TB statistics: Native code size per TB. */
    STAMPROFILE             StatTbNativeCode;
    /** Native TB statistics: Profiling native recompilation. */
    STAMPROFILE             StatNativeRecompilation;
    /** Native TB statistics: Number of calls per TB that were recompiled properly. */
    STAMPROFILE             StatNativeCallsRecompiled;
    /** Native TB statistics: Number of threaded calls per TB that weren't recompiled. */
    STAMPROFILE             StatNativeCallsThreaded;
    /** Native recompiled execution: TLB hits for data fetches. */
    STAMCOUNTER             StatNativeTlbHitsForFetch;
    /** Native recompiled execution: TLB hits for data stores. */
    STAMCOUNTER             StatNativeTlbHitsForStore;
    /** Native recompiled execution: TLB hits for stack accesses. */
    STAMCOUNTER             StatNativeTlbHitsForStack;
    /** Native recompiled execution: TLB hits for mapped accesses. */
    STAMCOUNTER             StatNativeTlbHitsForMapped;
    /** Native recompiled execution: Code TLB misses for new page. */
    STAMCOUNTER             StatNativeCodeTlbMissesNewPage;
    /** Native recompiled execution: Code TLB hits for new page. */
    STAMCOUNTER             StatNativeCodeTlbHitsForNewPage;
    /** Native recompiled execution: Code TLB misses for new page with offset. */
    STAMCOUNTER             StatNativeCodeTlbMissesNewPageWithOffset;
    /** Native recompiled execution: Code TLB hits for new page with offset. */
    STAMCOUNTER             StatNativeCodeTlbHitsForNewPageWithOffset;

    /** Native recompiler: Number of calls to iemNativeRegAllocFindFree. */
    STAMCOUNTER             StatNativeRegFindFree;
    /** Native recompiler: Number of times iemNativeRegAllocFindFree needed
     *  to free a variable. */
    STAMCOUNTER             StatNativeRegFindFreeVar;
    /** Native recompiler: Number of times iemNativeRegAllocFindFree did
     *  not need to free any variables. */
    STAMCOUNTER             StatNativeRegFindFreeNoVar;
    /** Native recompiler: Liveness info freed shadowed guest registers in
     * iemNativeRegAllocFindFree. */
    STAMCOUNTER             StatNativeRegFindFreeLivenessUnshadowed;
    /** Native recompiler: Liveness info helped with the allocation in
     *  iemNativeRegAllocFindFree. */
    STAMCOUNTER             StatNativeRegFindFreeLivenessHelped;

    /** Native recompiler: Number of times status flags calc has been skipped. */
    STAMCOUNTER             StatNativeEflSkippedArithmetic;
    /** Native recompiler: Number of times status flags calc has been postponed. */
    STAMCOUNTER             StatNativeEflPostponedArithmetic;
    /** Native recompiler: Total number instructions in this category. */
    STAMCOUNTER             StatNativeEflTotalArithmetic;

    /** Native recompiler: Number of times status flags calc has been skipped. */
    STAMCOUNTER             StatNativeEflSkippedLogical;
    /** Native recompiler: Number of times status flags calc has been postponed. */
    STAMCOUNTER             StatNativeEflPostponedLogical;
    /** Native recompiler: Total number instructions in this category. */
    STAMCOUNTER             StatNativeEflTotalLogical;

    /** Native recompiler: Number of times status flags calc has been skipped. */
    STAMCOUNTER             StatNativeEflSkippedShift;
    /** Native recompiler: Number of times status flags calc has been postponed. */
    STAMCOUNTER             StatNativeEflPostponedShift;
    /** Native recompiler: Total number instructions in this category. */
    STAMCOUNTER             StatNativeEflTotalShift;

    /** Native recompiler: Number of emits per postponement. */
    STAMPROFILE             StatNativeEflPostponedEmits;

    /** Native recompiler: Number of opportunities to skip EFLAGS.CF updating. */
    STAMCOUNTER             StatNativeLivenessEflCfSkippable;
    /** Native recompiler: Number of opportunities to skip EFLAGS.PF updating. */
    STAMCOUNTER             StatNativeLivenessEflPfSkippable;
    /** Native recompiler: Number of opportunities to skip EFLAGS.AF updating. */
    STAMCOUNTER             StatNativeLivenessEflAfSkippable;
    /** Native recompiler: Number of opportunities to skip EFLAGS.ZF updating. */
    STAMCOUNTER             StatNativeLivenessEflZfSkippable;
    /** Native recompiler: Number of opportunities to skip EFLAGS.SF updating. */
    STAMCOUNTER             StatNativeLivenessEflSfSkippable;
    /** Native recompiler: Number of opportunities to skip EFLAGS.OF updating. */
    STAMCOUNTER             StatNativeLivenessEflOfSkippable;
    /** Native recompiler: Number of required EFLAGS.CF updates. */
    STAMCOUNTER             StatNativeLivenessEflCfRequired;
    /** Native recompiler: Number of required EFLAGS.PF updates. */
    STAMCOUNTER             StatNativeLivenessEflPfRequired;
    /** Native recompiler: Number of required EFLAGS.AF updates. */
    STAMCOUNTER             StatNativeLivenessEflAfRequired;
    /** Native recompiler: Number of required EFLAGS.ZF updates. */
    STAMCOUNTER             StatNativeLivenessEflZfRequired;
    /** Native recompiler: Number of required EFLAGS.SF updates. */
    STAMCOUNTER             StatNativeLivenessEflSfRequired;
    /** Native recompiler: Number of required EFLAGS.OF updates. */
    STAMCOUNTER             StatNativeLivenessEflOfRequired;
    /** Native recompiler: Number of potentially delayable EFLAGS.CF updates. */
    STAMCOUNTER             StatNativeLivenessEflCfDelayable;
    /** Native recompiler: Number of potentially delayable EFLAGS.PF updates. */
    STAMCOUNTER             StatNativeLivenessEflPfDelayable;
    /** Native recompiler: Number of potentially delayable EFLAGS.AF updates. */
    STAMCOUNTER             StatNativeLivenessEflAfDelayable;
    /** Native recompiler: Number of potentially delayable EFLAGS.ZF updates. */
    STAMCOUNTER             StatNativeLivenessEflZfDelayable;
    /** Native recompiler: Number of potentially delayable EFLAGS.SF updates. */
    STAMCOUNTER             StatNativeLivenessEflSfDelayable;
    /** Native recompiler: Number of potentially delayable EFLAGS.OF updates. */
    STAMCOUNTER             StatNativeLivenessEflOfDelayable;

    /** Native recompiler: Number of potential PC updates in total. */
    STAMCOUNTER             StatNativePcUpdateTotal;
    /** Native recompiler: Number of PC updates which could be delayed. */
    STAMCOUNTER             StatNativePcUpdateDelayed;

    /** Native recompiler: Number of time we had complicated dirty shadow
     *  register situations with the other branch in IEM_MC_ENDIF. */
    STAMCOUNTER             StatNativeEndIfOtherBranchDirty;

    /** Native recompiler: Number of calls to iemNativeSimdRegAllocFindFree. */
    STAMCOUNTER             StatNativeSimdRegFindFree;
    /** Native recompiler: Number of times iemNativeSimdRegAllocFindFree needed
     *  to free a variable. */
    STAMCOUNTER             StatNativeSimdRegFindFreeVar;
    /** Native recompiler: Number of times iemNativeSimdRegAllocFindFree did
     *  not need to free any variables. */
    STAMCOUNTER             StatNativeSimdRegFindFreeNoVar;
    /** Native recompiler: Liveness info freed shadowed guest registers in
     * iemNativeSimdRegAllocFindFree. */
    STAMCOUNTER             StatNativeSimdRegFindFreeLivenessUnshadowed;
    /** Native recompiler: Liveness info helped with the allocation in
     *  iemNativeSimdRegAllocFindFree. */
    STAMCOUNTER             StatNativeSimdRegFindFreeLivenessHelped;

    /** Native recompiler: Number of potential IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() checks. */
    STAMCOUNTER             StatNativeMaybeDeviceNotAvailXcptCheckPotential;
    /** Native recompiler: Number of potential IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE() checks. */
    STAMCOUNTER             StatNativeMaybeWaitDeviceNotAvailXcptCheckPotential;
    /** Native recompiler: Number of potential IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() checks. */
    STAMCOUNTER             StatNativeMaybeSseXcptCheckPotential;
    /** Native recompiler: Number of potential IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() checks. */
    STAMCOUNTER             StatNativeMaybeAvxXcptCheckPotential;

    /** Native recompiler: Number of IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() checks omitted. */
    STAMCOUNTER             StatNativeMaybeDeviceNotAvailXcptCheckOmitted;
    /** Native recompiler: Number of IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE() checks omitted. */
    STAMCOUNTER             StatNativeMaybeWaitDeviceNotAvailXcptCheckOmitted;
    /** Native recompiler: Number of IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() checks omitted. */
    STAMCOUNTER             StatNativeMaybeSseXcptCheckOmitted;
    /** Native recompiler: Number of IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() checks omitted. */
    STAMCOUNTER             StatNativeMaybeAvxXcptCheckOmitted;

    /** Native recompiler: The TB finished executing completely without jumping to a an exit label.
     * Not availabe in release builds. */
    STAMCOUNTER             StatNativeTbFinished;
    /** Native recompiler: The TB finished executing jumping to the ReturnBreak label. */
    STAMCOUNTER             StatNativeTbExitReturnBreak;
    /** Native recompiler: The TB finished executing jumping to the ReturnBreakFF label. */
    STAMCOUNTER             StatNativeTbExitReturnBreakFF;
    /** Native recompiler: The TB finished executing jumping to the ReturnWithFlags label. */
    STAMCOUNTER             StatNativeTbExitReturnWithFlags;
    /** Native recompiler: The TB finished executing with other non-zero status. */
    STAMCOUNTER             StatNativeTbExitReturnOtherStatus;
    /** Native recompiler: The TB finished executing via throw / long jump. */
    STAMCOUNTER             StatNativeTbExitLongJump;
    /** Native recompiler: The TB finished executing jumping to the ReturnBreak
     *  label, but directly jumped to the next TB, scenario \#1 w/o IRQ checks. */
    STAMCOUNTER             StatNativeTbExitDirectLinking1NoIrq;
    /** Native recompiler: The TB finished executing jumping to the ReturnBreak
     *  label, but directly jumped to the next TB, scenario \#1 with IRQ checks. */
    STAMCOUNTER             StatNativeTbExitDirectLinking1Irq;
    /** Native recompiler: The TB finished executing jumping to the ReturnBreak
     *  label, but directly jumped to the next TB, scenario \#1 w/o IRQ checks. */
    STAMCOUNTER             StatNativeTbExitDirectLinking2NoIrq;
    /** Native recompiler: The TB finished executing jumping to the ReturnBreak
     *  label, but directly jumped to the next TB, scenario \#2 with IRQ checks. */
    STAMCOUNTER             StatNativeTbExitDirectLinking2Irq;

    /** Native recompiler: The TB finished executing jumping to the RaiseDe label. */
    STAMCOUNTER             StatNativeTbExitRaiseDe;
    /** Native recompiler: The TB finished executing jumping to the RaiseUd label. */
    STAMCOUNTER             StatNativeTbExitRaiseUd;
    /** Native recompiler: The TB finished executing jumping to the RaiseSseRelated label. */
    STAMCOUNTER             StatNativeTbExitRaiseSseRelated;
    /** Native recompiler: The TB finished executing jumping to the RaiseAvxRelated label. */
    STAMCOUNTER             StatNativeTbExitRaiseAvxRelated;
    /** Native recompiler: The TB finished executing jumping to the RaiseSseAvxFpRelated label. */
    STAMCOUNTER             StatNativeTbExitRaiseSseAvxFpRelated;
    /** Native recompiler: The TB finished executing jumping to the RaiseNm label. */
    STAMCOUNTER             StatNativeTbExitRaiseNm;
    /** Native recompiler: The TB finished executing jumping to the RaiseGp0 label. */
    STAMCOUNTER             StatNativeTbExitRaiseGp0;
    /** Native recompiler: The TB finished executing jumping to the RaiseMf label. */
    STAMCOUNTER             StatNativeTbExitRaiseMf;
    /** Native recompiler: The TB finished executing jumping to the RaiseXf label. */
    STAMCOUNTER             StatNativeTbExitRaiseXf;
    /** Native recompiler: The TB finished executing jumping to the ObsoleteTb label. */
    STAMCOUNTER             StatNativeTbExitObsoleteTb;

    /** Native recompiler: Number of full TB loops (jumps from end to start). */
    STAMCOUNTER             StatNativeTbExitLoopFullTb;

    /** Native recompiler: Failure situations with direct linking scenario \#1.
     * Counter with StatNativeTbExitReturnBreak. Not in release builds.
     * @{  */
    STAMCOUNTER             StatNativeTbExitDirectLinking1NoTb;
    STAMCOUNTER             StatNativeTbExitDirectLinking1MismatchGCPhysPc;
    STAMCOUNTER             StatNativeTbExitDirectLinking1MismatchFlags;
    STAMCOUNTER             StatNativeTbExitDirectLinking1PendingIrq;
    /** @} */

    /** Native recompiler: Failure situations with direct linking scenario \#2.
     * Counter with StatNativeTbExitReturnBreak. Not in release builds.
     * @{  */
    STAMCOUNTER             StatNativeTbExitDirectLinking2NoTb;
    STAMCOUNTER             StatNativeTbExitDirectLinking2MismatchGCPhysPc;
    STAMCOUNTER             StatNativeTbExitDirectLinking2MismatchFlags;
    STAMCOUNTER             StatNativeTbExitDirectLinking2PendingIrq;
    /** @} */

    /** iemMemMap and iemMemMapJmp statistics.
     *  @{ */
    STAMCOUNTER             StatMemMapJmp;
    STAMCOUNTER             StatMemMapNoJmp;
    STAMCOUNTER             StatMemBounceBufferCrossPage;
    STAMCOUNTER             StatMemBounceBufferMapPhys;
    /** @} */

    /** Timer polling statistics (debug only).
     * @{  */
    STAMPROFILE             StatTimerPoll;
    STAMPROFILE             StatTimerPollPoll;
    STAMPROFILE             StatTimerPollRun;
    STAMCOUNTER             StatTimerPollUnchanged;
    STAMCOUNTER             StatTimerPollTiny;
    STAMCOUNTER             StatTimerPollDefaultCalc;
    STAMCOUNTER             StatTimerPollMax;
    STAMPROFILE             StatTimerPollFactorDivision;
    STAMPROFILE             StatTimerPollFactorMultiplication;
    /** @} */


    STAMCOUNTER             aStatAdHoc[8];

#ifdef IEM_WITH_TLB_TRACE
    /*uint64_t                au64Padding[0];*/
#else
    uint64_t                au64Padding[2];
#endif

#ifdef IEM_WITH_TLB_TRACE
    /** The end (next) trace entry. */
    uint32_t                idxTlbTraceEntry;
    /** Number of trace entries allocated expressed as a power of two. */
    uint32_t                cTlbTraceEntriesShift;
    /** The trace entries. */
    PIEMTLBTRACEENTRY       paTlbTraceEntries;
#endif

    /** Data TLB.
     * @remarks Must be 64-byte aligned. */
    IEMTLB                  DataTlb;
    /** Instruction TLB.
     * @remarks Must be 64-byte aligned. */
    IEMTLB                  CodeTlb;

    /** Exception statistics. */
    STAMCOUNTER             aStatXcpts[32];
    /** Interrupt statistics. */
    uint32_t                aStatInts[256];

#if defined(VBOX_WITH_STATISTICS) && !defined(DOXYGEN_RUNNING) && !defined(IEM_WITHOUT_INSTRUCTION_STATS)
    /** Instruction statistics for ring-0/raw-mode. */
    IEMINSTRSTATS           StatsRZ;
    /** Instruction statistics for ring-3. */
    IEMINSTRSTATS           StatsR3;
# ifdef VBOX_WITH_IEM_RECOMPILER
    /** Statistics per threaded function call.
     * Updated by both the threaded and native recompilers. */
    uint32_t                acThreadedFuncStats[0x6000 /*24576*/];
# endif
#endif
} IEMCPU;
#ifdef IEM_WITH_CODE_TLB
AssertCompileMemberOffset(IEMCPU, GCPhysInstrBuf, 0x20);
#endif
#if !defined(IEM_WITH_OPAQUE_DECODER_STATE) && defined(VBOX_VMM_TARGET_X86)
# ifdef IEM_WITH_CODE_TLB
AssertCompileMemberOffset(IEMCPU, enmEffOpSize, 0x36);
AssertCompileMemberOffset(IEMCPU, abOpcode, 0x40);
# else
AssertCompileMemberOffset(IEMCPU, enmEffOpSize, 0x13);
AssertCompileMemberOffset(IEMCPU, abOpcode, 0x20);
# endif
#elif defined(IEM_WITH_OPAQUE_DECODER_STATE) && defined(VBOX_VMM_TARGET_X86)
# ifdef IEM_WITH_CODE_TLB
AssertCompileMemberOffset(IEMCPU, abOpaqueDecoderPart2, 0x36);
# else
AssertCompileMemberOffset(IEMCPU, abOpaqueDecoderPart2, 0x13);
# endif
#endif
AssertCompileMemberOffset(IEMCPU, cActiveMappings, 0x4f);
AssertCompileMemberAlignment(IEMCPU, aMemMappings, 16);
AssertCompileMemberAlignment(IEMCPU, aMemMappingLocks, 16);
AssertCompileMemberAlignment(IEMCPU, aBounceBuffers, 64);
AssertCompileMemberAlignment(IEMCPU, pCurTbR3, 64);
AssertCompileMemberAlignment(IEMCPU, DataTlb, 64);
AssertCompileMemberAlignment(IEMCPU, CodeTlb, 64);

/** Pointer to the per-CPU IEM state. */
typedef IEMCPU *PIEMCPU;
/** Pointer to the const per-CPU IEM state. */
typedef IEMCPU const *PCIEMCPU;

/** @def IEMNATIVE_SIMD_FP_CTRL_REG_NOT_MODIFIED
 * Value indicating the TB didn't modified the floating point control register.
 * @note Neither FPCR nor MXCSR accept this as a valid value (MXCSR is not fully populated,
 *       FPCR has the upper 32-bit reserved), so this is safe. */
#if defined(IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS) || defined(DOXYGEN_RUNNING)
# ifdef RT_ARCH_AMD64
#  define IEMNATIVE_SIMD_FP_CTRL_REG_NOT_MODIFIED UINT32_MAX
# elif defined(RT_ARCH_ARM64)
#  define IEMNATIVE_SIMD_FP_CTRL_REG_NOT_MODIFIED UINT64_MAX
# else
#  error "Port me"
# endif
#endif

/** @def IEM_GET_CTX
 * Gets the guest CPU context for the calling EMT.
 * @returns PCPUMCTX
 * @param   a_pVCpu The cross context virtual CPU structure of the calling thread.
 */
#define IEM_GET_CTX(a_pVCpu)                    (&(a_pVCpu)->cpum.GstCtx)

/** @def IEM_CTX_ASSERT
 * Asserts that the @a a_fExtrnMbz is present in the CPU context.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 * @param   a_fExtrnMbz     The mask of CPUMCTX_EXTRN_XXX flags that must be zero.
 */
#define IEM_CTX_ASSERT(a_pVCpu, a_fExtrnMbz) \
    AssertMsg(!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnMbz)), \
              ("fExtrn=%#RX64 & fExtrnMbz=%#RX64 -> %#RX64\n", \
              (a_pVCpu)->cpum.GstCtx.fExtrn, (a_fExtrnMbz), (a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnMbz) ))

/** @def IEM_CTX_IMPORT_RET
 * Makes sure the CPU context bits given by @a a_fExtrnImport are imported.
 *
 * Will call the keep to import the bits as needed.
 *
 * Returns on import failure.
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 * @param   a_fExtrnImport  The mask of CPUMCTX_EXTRN_XXX flags to import.
 */
#define IEM_CTX_IMPORT_RET(a_pVCpu, a_fExtrnImport) \
    do { \
        if (!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnImport))) \
        { /* likely */ } \
        else \
        { \
            int rcCtxImport = CPUMImportGuestStateOnDemand(a_pVCpu, a_fExtrnImport); \
            AssertRCReturn(rcCtxImport, rcCtxImport); \
        } \
    } while (0)

/** @def IEM_CTX_IMPORT_NORET
 * Makes sure the CPU context bits given by @a a_fExtrnImport are imported.
 *
 * Will call the keep to import the bits as needed.
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 * @param   a_fExtrnImport  The mask of CPUMCTX_EXTRN_XXX flags to import.
 */
#define IEM_CTX_IMPORT_NORET(a_pVCpu, a_fExtrnImport) \
    do { \
        if (!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnImport))) \
        { /* likely */ } \
        else \
        { \
            int rcCtxImport = CPUMImportGuestStateOnDemand(a_pVCpu, a_fExtrnImport); \
            AssertLogRelRC(rcCtxImport); \
        } \
    } while (0)

/** @def IEM_CTX_IMPORT_JMP
 * Makes sure the CPU context bits given by @a a_fExtrnImport are imported.
 *
 * Will call the keep to import the bits as needed.
 *
 * Jumps on import failure.
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 * @param   a_fExtrnImport  The mask of CPUMCTX_EXTRN_XXX flags to import.
 */
#define IEM_CTX_IMPORT_JMP(a_pVCpu, a_fExtrnImport) \
    do { \
        if (!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnImport))) \
        { /* likely */ } \
        else \
        { \
            int rcCtxImport = CPUMImportGuestStateOnDemand(a_pVCpu, a_fExtrnImport); \
            AssertRCStmt(rcCtxImport, IEM_DO_LONGJMP(pVCpu, rcCtxImport)); \
        } \
    } while (0)



/** @def IEM_GET_TARGET_CPU
 * Gets the current IEMTARGETCPU value.
 * @returns IEMTARGETCPU value.
 * @param   a_pVCpu The cross context virtual CPU structure of the calling thread.
 */
#if IEM_CFG_TARGET_CPU != IEMTARGETCPU_DYNAMIC
# define IEM_GET_TARGET_CPU(a_pVCpu)    (IEM_CFG_TARGET_CPU)
#else
# define IEM_GET_TARGET_CPU(a_pVCpu)    ((a_pVCpu)->iem.s.uTargetCpu)
#endif


/** @def IEM_TRY_SETJMP
 * Wrapper around setjmp / try, hiding all the ugly differences.
 *
 * @note Use with extreme care as this is a fragile macro.
 * @param   a_pVCpu     The cross context virtual CPU structure of the calling EMT.
 * @param   a_rcTarget  The variable that should receive the status code in case
 *                      of a longjmp/throw.
 */
/** @def IEM_TRY_SETJMP_AGAIN
 * For when setjmp / try is used again in the same variable scope as a previous
 * IEM_TRY_SETJMP invocation.
 */
/** @def IEM_CATCH_LONGJMP_BEGIN
 * Start wrapper for catch / setjmp-else.
 *
 * This will set up a scope.
 *
 * @note Use with extreme care as this is a fragile macro.
 * @param   a_pVCpu     The cross context virtual CPU structure of the calling EMT.
 * @param   a_rcTarget  The variable that should receive the status code in case
 *                      of a longjmp/throw.
 */
/** @def IEM_CATCH_LONGJMP_END
 * End wrapper for catch / setjmp-else.
 *
 * This will close the scope set up by IEM_CATCH_LONGJMP_BEGIN and clean up the
 * state.
 *
 * @note Use with extreme care as this is a fragile macro.
 * @param   a_pVCpu     The cross context virtual CPU structure of the calling EMT.
 */
#ifdef IEM_WITH_THROW_CATCH
# define IEM_TRY_SETJMP(a_pVCpu, a_rcTarget) \
        a_rcTarget = VINF_SUCCESS; \
        try
# define IEM_TRY_SETJMP_AGAIN(a_pVCpu, a_rcTarget) \
        IEM_TRY_SETJMP(a_pVCpu, a_rcTarget)
# define IEM_CATCH_LONGJMP_BEGIN(a_pVCpu, a_rcTarget) \
        catch (int rcThrown) \
        { \
            a_rcTarget = rcThrown
# define IEM_CATCH_LONGJMP_END(a_pVCpu) \
        } \
        ((void)0)
#else  /* !IEM_WITH_THROW_CATCH */
# define IEM_TRY_SETJMP(a_pVCpu, a_rcTarget) \
        jmp_buf  JmpBuf; \
        jmp_buf * volatile pSavedJmpBuf = (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf); \
        (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf) = &JmpBuf; \
        if ((rcStrict = setjmp(JmpBuf)) == 0)
# define IEM_TRY_SETJMP_AGAIN(a_pVCpu, a_rcTarget) \
        pSavedJmpBuf = (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf); \
        (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf) = &JmpBuf; \
        if ((rcStrict = setjmp(JmpBuf)) == 0)
# define IEM_CATCH_LONGJMP_BEGIN(a_pVCpu, a_rcTarget) \
        else \
        { \
            ((void)0)
# define IEM_CATCH_LONGJMP_END(a_pVCpu) \
        } \
        (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf) = pSavedJmpBuf
#endif /* !IEM_WITH_THROW_CATCH */


/**
 * Shared per-VM IEM data.
 */
typedef struct IEM
{
    /** The VMX APIC-access page handler type. */
    PGMPHYSHANDLERTYPE      hVmxApicAccessPage;
#ifndef VBOX_WITHOUT_CPUID_HOST_CALL
    /** Set if the CPUID host call functionality is enabled.   */
    bool                    fCpuIdHostCall;
#endif
} IEM;



/** @name IEM_ACCESS_XXX - Access details.
 * @{ */
#define IEM_ACCESS_INVALID              UINT32_C(0x000000ff)
#define IEM_ACCESS_TYPE_READ            UINT32_C(0x00000001)
#define IEM_ACCESS_TYPE_WRITE           UINT32_C(0x00000002)
#define IEM_ACCESS_TYPE_EXEC            UINT32_C(0x00000004)
#define IEM_ACCESS_TYPE_MASK            UINT32_C(0x00000007)
#define IEM_ACCESS_WHAT_CODE            UINT32_C(0x00000010)
#define IEM_ACCESS_WHAT_DATA            UINT32_C(0x00000020)
#define IEM_ACCESS_WHAT_STACK           UINT32_C(0x00000030)
#define IEM_ACCESS_WHAT_SYS             UINT32_C(0x00000040)
#define IEM_ACCESS_WHAT_MASK            UINT32_C(0x00000070)
/** The writes are partial, so if initialize the bounce buffer with the
 * orignal RAM content. */
#define IEM_ACCESS_PARTIAL_WRITE        UINT32_C(0x00000100)
/** Used in aMemMappings to indicate that the entry is bounce buffered. */
#define IEM_ACCESS_BOUNCE_BUFFERED      UINT32_C(0x00000200)
/** Bounce buffer with ring-3 write pending, first page. */
#define IEM_ACCESS_PENDING_R3_WRITE_1ST UINT32_C(0x00000400)
/** Bounce buffer with ring-3 write pending, second page. */
#define IEM_ACCESS_PENDING_R3_WRITE_2ND UINT32_C(0x00000800)
/** Not locked, accessed via the TLB. */
#define IEM_ACCESS_NOT_LOCKED           UINT32_C(0x00001000)
/** Atomic access.
 * This enables special alignment checks and the VINF_EM_EMULATE_SPLIT_LOCK
 * fallback for misaligned stuff. See @bugref{10547}.
 * @arm This means FEAT_LSA2 alignment checks according to SCTLR_ELx.nAA. */
#define IEM_ACCESS_ATOMIC               UINT32_C(0x00002000)
/** Valid bit mask. */
#define IEM_ACCESS_VALID_MASK           UINT32_C(0x00003fff)
/** Shift count for the TLB flags (upper word). */
#define IEM_ACCESS_SHIFT_TLB_FLAGS      16

/** Atomic read+write data alias. */
#define IEM_ACCESS_DATA_ATOMIC          (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_WHAT_DATA | IEM_ACCESS_ATOMIC)
/** Read+write data alias. */
#define IEM_ACCESS_DATA_RW              (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_WHAT_DATA)
/** Write data alias. */
#define IEM_ACCESS_DATA_W               (IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_WHAT_DATA)
/** Read data alias. */
#define IEM_ACCESS_DATA_R               (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_WHAT_DATA)
/** Instruction fetch alias. */
#define IEM_ACCESS_INSTRUCTION          (IEM_ACCESS_TYPE_EXEC  | IEM_ACCESS_WHAT_CODE)
/** Stack write alias. */
#define IEM_ACCESS_STACK_W              (IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_WHAT_STACK)
/** Stack read alias. */
#define IEM_ACCESS_STACK_R              (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_WHAT_STACK)
/** Stack read+write alias. */
#define IEM_ACCESS_STACK_RW             (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_WHAT_STACK)
/** Read system table alias. */
#define IEM_ACCESS_SYS_R                (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_WHAT_SYS)
/** Read+write system table alias. */
#define IEM_ACCESS_SYS_RW               (IEM_ACCESS_TYPE_READ  | IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_WHAT_SYS)
/** @} */


/** @def IEM_DECL_MSC_GUARD_IGNORE
 * Disables control flow guards checks inside a method and any function pointers
 * referenced by it. */
#if defined(_MSC_VER) && !defined(IN_RING0)
# define IEM_DECL_MSC_GUARD_IGNORE  __declspec(guard(ignore))
#else
# define IEM_DECL_MSC_GUARD_IGNORE
#endif

/** @def IEM_DECL_MSC_GUARD_NONE
 * Disables control flow guards checks inside a method and but continue track
 * function pointers references by it. */
#if defined(_MSC_VER) && !defined(IN_RING0)
# define IEM_DECL_MSC_GUARD_NONE    __declspec(guard(nocf))
#else
# define IEM_DECL_MSC_GUARD_NONE
#endif


/** @def IEM_DECL_IMPL_TYPE
 * For typedef'ing an instruction implementation function.
 *
 * @param   a_RetType           The return type.
 * @param   a_Name              The name of the type.
 * @param   a_ArgList           The argument list enclosed in parentheses.
 */

/** @def IEM_DECL_IMPL_DEF
 * For defining an instruction implementation function.
 *
 * @param   a_RetType           The return type.
 * @param   a_Name              The name of the type.
 * @param   a_ArgList           The argument list enclosed in parentheses.
 */

#if defined(__GNUC__) && defined(RT_ARCH_X86)
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    __attribute__((__fastcall__)) a_RetType (a_Name) a_ArgList
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    __attribute__((__fastcall__, __nothrow__)) DECL_HIDDEN_ONLY(a_RetType) a_Name a_ArgList
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    __attribute__((__fastcall__, __nothrow__)) DECL_HIDDEN_ONLY(a_RetType) a_Name a_ArgList

#elif defined(_MSC_VER) && defined(RT_ARCH_X86)
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (__fastcall a_Name) a_ArgList
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    IEM_DECL_MSC_GUARD_IGNORE a_RetType __fastcall a_Name a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    IEM_DECL_MSC_GUARD_IGNORE a_RetType __fastcall a_Name a_ArgList RT_NOEXCEPT

#elif __cplusplus >= 201700 /* P0012R1 support */
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL a_Name) a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    IEM_DECL_MSC_GUARD_IGNORE DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    IEM_DECL_MSC_GUARD_IGNORE DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList RT_NOEXCEPT

#else
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL a_Name) a_ArgList
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    IEM_DECL_MSC_GUARD_IGNORE DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    IEM_DECL_MSC_GUARD_IGNORE DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList

#endif


/** @name C instruction implementations for anything slightly complicated.
 * @{ */

#if defined(VBOX_VMM_TARGET_X86)
# define IEM_CIMPL_NEEDS_INSTR_LEN
#endif
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_CIMPL_COMMA_EXTRA_ARGS_CALL    , cbInstr
# define IEM_CIMPL_COMMA_EXTRA_ARGS         , uint8_t cbInstr
#else
# define IEM_CIMPL_COMMA_EXTRA_ARGS_CALL
# define IEM_CIMPL_COMMA_EXTRA_ARGS
#endif

/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * no extra arguments.
 *
 * @param   a_Name              The name of the type.
 */
#define IEM_CIMPL_DECL_TYPE_0(a_Name) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS))

/**
 * For defining a C instruction implementation function taking no extra
 * arguments.
 *
 * @param   a_Name              The name of the function
 */
# define IEM_CIMPL_DEF_0(a_Name) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS))
/**
 * Prototype version of IEM_CIMPL_DEF_0.
 */
#define IEM_CIMPL_PROTO_0(a_Name) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS))
/**
 * For calling a C instruction implementation function taking no extra
 * arguments.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 */
#define IEM_CIMPL_CALL_0(a_fn)              a_fn(pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS_CALL)

/** Type for a C instruction implementation function taking no extra
 *  arguments. */
typedef IEM_CIMPL_DECL_TYPE_0(FNIEMCIMPL0);
/** Function pointer type for a C instruction implementation function taking
 *  no extra arguments. */
typedef FNIEMCIMPL0 *PFNIEMCIMPL0;

/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * one extra argument.
 *
 * @param   a_Name              The name of the type.
 * @param   a_Type0             The argument type.
 * @param   a_Arg0              The argument name.
 */
#define IEM_CIMPL_DECL_TYPE_1(a_Name, a_Type0, a_Arg0) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name,(PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0))
/**
 * For defining a C instruction implementation function taking one extra
 * argument.
 *
 * @param   a_Name              The name of the function
 * @param   a_Type0             The argument type.
 * @param   a_Arg0              The argument name.
 */
#define IEM_CIMPL_DEF_1(a_Name, a_Type0, a_Arg0) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name,(PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0))
/**
 * Prototype version of IEM_CIMPL_DEF_1.
 */
#define IEM_CIMPL_PROTO_1(a_Name, a_Type0, a_Arg0) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name,(PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0))
/**
 * For calling a C instruction implementation function taking one extra
 * argument.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 * @param   a0                  The name of the 1st argument.
 */
#define IEM_CIMPL_CALL_1(a_fn, a0)      a_fn(pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS_CALL, (a0))

/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * two extra arguments.
 *
 * @param   a_Name              The name of the type.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 */
#define IEM_CIMPL_DECL_TYPE_2(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1))
/**
 * For defining a C instruction implementation function taking two extra
 * arguments.
 *
 * @param   a_Name              The name of the function.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 */
#define IEM_CIMPL_DEF_2(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1))
/**
 * Prototype version of IEM_CIMPL_DEF_2.
 */
#define IEM_CIMPL_PROTO_2(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1))
/**
 * For calling a C instruction implementation function taking two extra
 * arguments.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 * @param   a0                  The name of the 1st argument.
 * @param   a1                  The name of the 2nd argument.
 */
#define IEM_CIMPL_CALL_2(a_fn, a0, a1)  a_fn(pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS_CALL, (a0), (a1))

/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * three extra arguments.
 *
 * @param   a_Name              The name of the type.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 * @param   a_Type2             The type of the 3rd argument.
 * @param   a_Arg2              The name of the 3rd argument.
 */
#define IEM_CIMPL_DECL_TYPE_3(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2))
/**
 * For defining a C instruction implementation function taking three extra
 * arguments.
 *
 * @param   a_Name              The name of the function.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 * @param   a_Type2             The type of the 3rd argument.
 * @param   a_Arg2              The name of the 3rd argument.
 */
#define IEM_CIMPL_DEF_3(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2))
/**
 * Prototype version of IEM_CIMPL_DEF_3.
 */
#define IEM_CIMPL_PROTO_3(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2))
/**
 * For calling a C instruction implementation function taking three extra
 * arguments.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 * @param   a0                  The name of the 1st argument.
 * @param   a1                  The name of the 2nd argument.
 * @param   a2                  The name of the 3rd argument.
 */
#define IEM_CIMPL_CALL_3(a_fn, a0, a1, a2) a_fn(pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS_CALL, (a0), (a1), (a2))


/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * four extra arguments.
 *
 * @param   a_Name              The name of the type.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 * @param   a_Type2             The type of the 3rd argument.
 * @param   a_Arg2              The name of the 3rd argument.
 * @param   a_Type3             The type of the 4th argument.
 * @param   a_Arg3              The name of the 4th argument.
 */
#define IEM_CIMPL_DECL_TYPE_4(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2, a_Type3 a_Arg3))
/**
 * For defining a C instruction implementation function taking four extra
 * arguments.
 *
 * @param   a_Name              The name of the function.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 * @param   a_Type2             The type of the 3rd argument.
 * @param   a_Arg2              The name of the 3rd argument.
 * @param   a_Type3             The type of the 4th argument.
 * @param   a_Arg3              The name of the 4th argument.
 */
#define IEM_CIMPL_DEF_4(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, \
                                             a_Type2 a_Arg2, a_Type3 a_Arg3))
/**
 * Prototype version of IEM_CIMPL_DEF_4.
 */
# define IEM_CIMPL_PROTO_4(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, \
                                               a_Type2 a_Arg2, a_Type3 a_Arg3))
/**
 * For calling a C instruction implementation function taking four extra
 * arguments.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 * @param   a0                  The name of the 1st argument.
 * @param   a1                  The name of the 2nd argument.
 * @param   a2                  The name of the 3rd argument.
 * @param   a3                  The name of the 4th argument.
 */
#define IEM_CIMPL_CALL_4(a_fn, a0, a1, a2, a3) a_fn(pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS_CALL, (a0), (a1), (a2), (a3))


/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * five extra arguments.
 *
 * @param   a_Name              The name of the type.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 * @param   a_Type2             The type of the 3rd argument.
 * @param   a_Arg2              The name of the 3rd argument.
 * @param   a_Type3             The type of the 4th argument.
 * @param   a_Arg3              The name of the 4th argument.
 * @param   a_Type4             The type of the 5th argument.
 * @param   a_Arg4              The name of the 5th argument.
 */
#define IEM_CIMPL_DECL_TYPE_5(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3, a_Type4, a_Arg4) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, \
                                               a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2, \
                                               a_Type3 a_Arg3, a_Type4 a_Arg4))
/**
 * For defining a C instruction implementation function taking five extra
 * arguments.
 *
 * @param   a_Name              The name of the function.
 * @param   a_Type0             The type of the 1st argument
 * @param   a_Arg0              The name of the 1st argument.
 * @param   a_Type1             The type of the 2nd argument.
 * @param   a_Arg1              The name of the 2nd argument.
 * @param   a_Type2             The type of the 3rd argument.
 * @param   a_Arg2              The name of the 3rd argument.
 * @param   a_Type3             The type of the 4th argument.
 * @param   a_Arg3              The name of the 4th argument.
 * @param   a_Type4             The type of the 5th argument.
 * @param   a_Arg4              The name of the 5th argument.
 */
#define IEM_CIMPL_DEF_5(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3, a_Type4, a_Arg4) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, \
                                             a_Type2 a_Arg2, a_Type3 a_Arg3, a_Type4 a_Arg4))
/**
 * Prototype version of IEM_CIMPL_DEF_5.
 */
#define IEM_CIMPL_PROTO_5(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3, a_Type4, a_Arg4) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS, a_Type0 a_Arg0, a_Type1 a_Arg1, \
                                               a_Type2 a_Arg2, a_Type3 a_Arg3, a_Type4 a_Arg4))
/**
 * For calling a C instruction implementation function taking five extra
 * arguments.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 * @param   a0                  The name of the 1st argument.
 * @param   a1                  The name of the 2nd argument.
 * @param   a2                  The name of the 3rd argument.
 * @param   a3                  The name of the 4th argument.
 * @param   a4                  The name of the 5th argument.
 */
#define IEM_CIMPL_CALL_5(a_fn, a0, a1, a2, a3, a4) a_fn(pVCpu IEM_CIMPL_COMMA_EXTRA_ARGS_CALL, (a0), (a1), (a2), (a3), (a4))

/** @}  */


/** @name Opcode Decoder Function Types.
 * @{ */

/** @typedef PFNIEMOP
 * Pointer to an opcode decoder function.
 */

/** @def FNIEMOP_DEF
 * Define an opcode decoder function.
 *
 * We're using macors for this so that adding and removing parameters as well as
 * tweaking compiler specific attributes becomes easier.  See FNIEMOP_CALL
 *
 * @param   a_Name      The function name.
 */

/** @typedef PFNIEMOPRM
 * Pointer to an opcode decoder function with RM byte.
 */

/** @def FNIEMOPRM_DEF
 * Define an opcode decoder function with RM byte.
 *
 * We're using macors for this so that adding and removing parameters as well as
 * tweaking compiler specific attributes becomes easier.  See FNIEMOP_CALL_1
 *
 * @param   a_Name      The function name.
 */

#if defined(__GNUC__) && defined(RT_ARCH_X86)
typedef VBOXSTRICTRC (__attribute__((__fastcall__)) * PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (__attribute__((__fastcall__)) * PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__fastcall__, __nothrow__)) a_Name(PVMCPUCC pVCpu)
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__fastcall__, __nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0)
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__fastcall__, __nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1)

# define FNIEMOP_TYPE_1(a_TypeName, a_Type0, a_Name0) \
    typedef  VBOXSTRICTRC (__attribute__((__fastcall__, __nothrow__)) * a_TypeName)(PVMCPUCC pVCpu, a_Type0 a_Name0)

#elif defined(_MSC_VER) && defined(RT_ARCH_X86)
typedef VBOXSTRICTRC (__fastcall * PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (__fastcall * PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1) IEM_NOEXCEPT_MAY_LONGJMP

# define FNIEMOP_TYPE_1(a_TypeName, a_Type0, a_Name0) \
    typedef VBOXSTRICTRC (__fastcall * a_TypeName)(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP_TYPEDEF

#elif defined(__GNUC__) && !defined(IEM_WITH_THROW_CATCH)
typedef VBOXSTRICTRC (* PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (* PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu)
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0)
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1)

# define FNIEMOP_TYPE_1(a_TypeName, a_Type0, a_Name0) \
    typedef VBOXSTRICTRC (* a_TypeName)(PVMCPUCC pVCpu, a_Type0 a_Name0)

#else
typedef VBOXSTRICTRC (* PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (* PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC IEM_DECL_MSC_GUARD_IGNORE VBOXSTRICTRC a_Name(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC IEM_DECL_MSC_GUARD_IGNORE VBOXSTRICTRC a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC IEM_DECL_MSC_GUARD_IGNORE VBOXSTRICTRC a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1) IEM_NOEXCEPT_MAY_LONGJMP

# define FNIEMOP_TYPE_1(a_TypeName, a_Type0, a_Name0) \
    typedef VBOXSTRICTRC (* a_TypeName)(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP_TYPEDEF

#endif

FNIEMOP_TYPE_1(PFIEMOPU32, uint32_t, u32);

#define FNIEMOPRM_DEF(a_Name) FNIEMOP_DEF_1(a_Name, uint8_t, bRm)

/**
 * Call an opcode decoder function.
 *
 * We're using macors for this so that adding and removing parameters can be
 * done as we please.  See FNIEMOP_DEF.
 */
#define FNIEMOP_CALL(a_pfn) (a_pfn)(pVCpu)

/**
 * Call a common opcode decoder function taking one extra argument.
 *
 * We're using macors for this so that adding and removing parameters can be
 * done as we please.  See FNIEMOP_DEF_1.
 */
#define FNIEMOP_CALL_1(a_pfn, a0)           (a_pfn)(pVCpu, a0)

/**
 * Call a common opcode decoder function taking one extra argument.
 *
 * We're using macors for this so that adding and removing parameters can be
 * done as we please.  See FNIEMOP_DEF_1.
 */
#define FNIEMOP_CALL_2(a_pfn, a0, a1)       (a_pfn)(pVCpu, a0, a1)
/** @} */


/** @name Misc Helpers
 * @{  */

/** Used to shut up GCC warnings about variables that 'may be used uninitialized'
 * due to GCC lacking knowledge about the value range of a switch. */
#if RT_CPLUSPLUS_PREREQ(202000)
# define IEM_NOT_REACHED_DEFAULT_CASE_RET() default: [[unlikely]] AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE)
#else
# define IEM_NOT_REACHED_DEFAULT_CASE_RET() default: AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE)
#endif

/** Variant of IEM_NOT_REACHED_DEFAULT_CASE_RET that returns a custom value. */
#if RT_CPLUSPLUS_PREREQ(202000)
# define IEM_NOT_REACHED_DEFAULT_CASE_RET2(a_RetValue) default: [[unlikely]] AssertFailedReturn(a_RetValue)
#else
# define IEM_NOT_REACHED_DEFAULT_CASE_RET2(a_RetValue) default: AssertFailedReturn(a_RetValue)
#endif

/**
 * Returns IEM_RETURN_ASPECT_NOT_IMPLEMENTED, and in debug builds logs the
 * occation.
 */
#ifdef LOG_ENABLED
# define IEM_RETURN_ASPECT_NOT_IMPLEMENTED() \
    do { \
        /*Log*/ LogAlways(("%s: returning IEM_RETURN_ASPECT_NOT_IMPLEMENTED (line %d)\n", __FUNCTION__, __LINE__)); \
        return VERR_IEM_ASPECT_NOT_IMPLEMENTED; \
    } while (0)
#else
# define IEM_RETURN_ASPECT_NOT_IMPLEMENTED() \
    return VERR_IEM_ASPECT_NOT_IMPLEMENTED
#endif

/**
 * Returns IEM_RETURN_ASPECT_NOT_IMPLEMENTED, and in debug builds logs the
 * occation using the supplied logger statement.
 *
 * @param   a_LoggerArgs    What to log on failure.
 */
#ifdef LOG_ENABLED
# define IEM_RETURN_ASPECT_NOT_IMPLEMENTED_LOG(a_LoggerArgs) \
    do { \
        LogAlways((LOG_FN_FMT ": ", __PRETTY_FUNCTION__)); LogAlways(a_LoggerArgs); \
        /*LogFunc(a_LoggerArgs);*/ \
        return VERR_IEM_ASPECT_NOT_IMPLEMENTED; \
    } while (0)
#else
# define IEM_RETURN_ASPECT_NOT_IMPLEMENTED_LOG(a_LoggerArgs) \
    return VERR_IEM_ASPECT_NOT_IMPLEMENTED
#endif

/**
 * Returns a (const) pointer to the CPUMFEATURES for the guest CPU.
 * @returns PCCPUMFEATURES
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_GET_GUEST_CPU_FEATURES(a_pVCpu) (&((a_pVCpu)->CTX_SUFF(pVM)->cpum.ro.GuestFeatures))

/**
 * Returns a (const) pointer to the CPUMFEATURES for the host CPU.
 * @returns PCCPUMFEATURES
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_GET_HOST_CPU_FEATURES(a_pVCpu)  (&g_CpumHostFeatures.s)



/** @} */

uint32_t                iemCalcExecDbgFlagsSlow(PVMCPUCC pVCpu);
VBOXSTRICTRC            iemExecInjectPendingTrap(PVMCPUCC pVCpu);

/** @} */


/** @name   Memory access.
 * @{ */
VBOXSTRICTRC    iemMemBounceBufferMapCrossPage(PVMCPUCC pVCpu, int iMemMap, void **ppvMem, uint8_t *pbUnmapInfo,
                                               size_t cbMem, RTGCPTR GCPtrFirst, uint32_t fAccess) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemBounceBufferMapPhys(PVMCPUCC pVCpu, unsigned iMemMap, void **ppvMem, uint8_t *pbUnmapInfo, size_t cbMem,
                                          RTGCPHYS GCPhysFirst, uint32_t fAccess, VBOXSTRICTRC rcMap) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemCommitAndUnmap(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
#ifndef IN_RING3
VBOXSTRICTRC    iemMemCommitAndUnmapPostponeTroubleToR3(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
#endif
void            iemMemRollbackAndUnmap(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
void            iemMemRollback(PVMCPUCC pVCpu) RT_NOEXCEPT;

void            iemMemCommitAndUnmapJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapRwSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapAtSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapWoSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapRoSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemRollbackAndUnmapWoSafe(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;

VBOXSTRICTRC    iemMemPageTranslateAndCheckAccess(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbAccess, uint32_t fAccess,
                                                  PRTGCPHYS pGCPhysMem) RT_NOEXCEPT;

void            iemTlbInvalidateAllPhysicalSlow(PVMCPUCC pVCpu) RT_NOEXCEPT;
/** @} */

/*
 * Recompiler related stuff.
 */

DECLHIDDEN(int)     iemPollTimers(PVMCC pVM, PVMCPUCC pVCpu) RT_NOEXCEPT;

DECLCALLBACK(int)   iemTbInit(PVMCC pVM, uint32_t cInitialTbs, uint32_t cMaxTbs,
                              uint64_t cbInitialExec, uint64_t cbMaxExec, uint32_t cbChunkExec);
void                iemThreadedTbObsolete(PVMCPUCC pVCpu, PIEMTB pTb, bool fSafeToFree);
DECLHIDDEN(void)    iemTbAllocatorFree(PVMCPUCC pVCpu, PIEMTB pTb);
void                iemTbAllocatorProcessDelayedFrees(PVMCPUCC pVCpu, PIEMTBALLOCATOR pTbAllocator);
void                iemTbAllocatorFreeupNativeSpace(PVMCPUCC pVCpu, uint32_t cNeededInstrs);
DECLHIDDEN(PIEMTBALLOCATOR) iemTbAllocatorFreeBulkStart(PVMCPUCC pVCpu);
DECLHIDDEN(void)    iemTbAllocatorFreeBulk(PVMCPUCC pVCpu, PIEMTBALLOCATOR pTbAllocator, PIEMTB pTb);
DECLHIDDEN(const char *) iemTbFlagsToString(uint32_t fFlags, char *pszBuf, size_t cbBuf) RT_NOEXCEPT;
DECLHIDDEN(void)    iemThreadedDisassembleTb(PCIEMTB pTb, PCDBGFINFOHLP pHlp) RT_NOEXCEPT;
#if defined(VBOX_WITH_IEM_NATIVE_RECOMPILER) && defined(VBOX_WITH_SAVE_THREADED_TBS_FOR_PROFILING)
DECLHIDDEN(void)    iemThreadedSaveTbForProfilingCleanup(PVMCPU pVCpu);
#endif


/** @todo FNIEMTHREADEDFUNC and friends may need more work... */
#if defined(__GNUC__) && !defined(IEM_WITH_THROW_CATCH)
typedef VBOXSTRICTRC /*__attribute__((__nothrow__))*/ FNIEMTHREADEDFUNC(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2);
typedef FNIEMTHREADEDFUNC *PFNIEMTHREADEDFUNC;
# define IEM_DECL_IEMTHREADEDFUNC_DEF(a_Name) \
    VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2)
# define IEM_DECL_IEMTHREADEDFUNC_PROTO(a_Name) \
    VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2)

#else
typedef VBOXSTRICTRC (FNIEMTHREADEDFUNC)(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2);
typedef FNIEMTHREADEDFUNC *PFNIEMTHREADEDFUNC;
# define IEM_DECL_IEMTHREADEDFUNC_DEF(a_Name) \
    IEM_DECL_MSC_GUARD_IGNORE VBOXSTRICTRC a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2) IEM_NOEXCEPT_MAY_LONGJMP
# define IEM_DECL_IEMTHREADEDFUNC_PROTO(a_Name) \
    IEM_DECL_MSC_GUARD_IGNORE VBOXSTRICTRC a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2) IEM_NOEXCEPT_MAY_LONGJMP
#endif


/* Native recompiler public bits: */

DECLHIDDEN(PIEMTB)  iemNativeRecompile(PVMCPUCC pVCpu, PIEMTB pTb) RT_NOEXCEPT;
DECLHIDDEN(void)    iemNativeDisassembleTb(PVMCPU pVCpu, PCIEMTB pTb, PCDBGFINFOHLP pHlp) RT_NOEXCEPT;
int                 iemExecMemAllocatorInit(PVMCPU pVCpu, uint64_t cbMax, uint64_t cbInitial, uint32_t cbChunk) RT_NOEXCEPT;
DECLHIDDEN(PIEMNATIVEINSTR) iemExecMemAllocatorAlloc(PVMCPU pVCpu, uint32_t cbReq, PIEMTB pTb, PIEMNATIVEINSTR *ppaExec,
                                                     struct IEMNATIVEPERCHUNKCTX const **ppChunkCtx) RT_NOEXCEPT;
DECLHIDDEN(PIEMNATIVEINSTR) iemExecMemAllocatorAllocFromChunk(PVMCPU pVCpu, uint32_t idxChunk, uint32_t cbReq,
                                                              PIEMNATIVEINSTR *ppaExec);
DECLHIDDEN(void)    iemExecMemAllocatorReadyForUse(PVMCPUCC pVCpu, void *pv, size_t cb) RT_NOEXCEPT;
void                iemExecMemAllocatorFree(PVMCPU pVCpu, void *pv, size_t cb) RT_NOEXCEPT;
DECLASM(DECL_NO_RETURN(void)) iemNativeTbLongJmp(void *pvFramePointer, int rc) RT_NOEXCEPT;
DECLHIDDEN(struct IEMNATIVEPERCHUNKCTX const *) iemExecMemGetTbChunkCtx(PVMCPU pVCpu, PCIEMTB pTb);
DECLHIDDEN(int) iemNativeRecompileAttachExecMemChunkCtx(PVMCPU pVCpu, uint32_t idxChunk, struct IEMNATIVEPERCHUNKCTX const **ppCtx);

# ifdef VBOX_VMM_TARGET_X86
#  include "VMMAll/target-x86/IEMInternal-x86.h"
# elif defined(VBOX_VMM_TARGET_ARMV8)
#  include "VMMAll/target-armv8/IEMInternal-armv8.h"
# endif

#endif /* !RT_IN_ASSEMBLER - ASM-NOINC-END */


/** @} */

RT_C_DECLS_END

/* ASM-INC: %include "IEMInternalStruct.mac" */

#endif /* !VMM_INCLUDED_SRC_include_IEMInternal_h */

