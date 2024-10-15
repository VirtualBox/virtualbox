/* $Id: IEMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

/** @def IEM_WITH_SETJMP
 * Enables alternative status code handling using setjmps.
 *
 * This adds a bit of expense via the setjmp() call since it saves all the
 * non-volatile registers.  However, it eliminates return code checks and allows
 * for more optimal return value passing (return regs instead of stack buffer).
 */
#if defined(DOXYGEN_RUNNING) || defined(RT_OS_WINDOWS) || 1
# define IEM_WITH_SETJMP
#endif

/** @def IEM_WITH_THROW_CATCH
 * Enables using C++ throw/catch as an alternative to setjmp/longjmp in user
 * mode code when IEM_WITH_SETJMP is in effect.
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
#if defined(__cplusplus) && defined(IEM_WITH_SETJMP) && defined(IN_RING3) && (defined(__GNUC__) || defined(_MSC_VER)) /* ASM-NOINC-START */
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

/** Enables the SIMD register allocator @bugref{10614}.  */
#if defined(DOXYGEN_RUNNING) || 1
# define IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
#endif
/** Enables access to even callee saved registers. */
//# define IEMNATIVE_WITH_SIMD_REG_ACCESS_ALL_REGISTERS

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
#if defined(IEM_WITH_SETJMP) || defined(DOXYGEN_RUNNING)
# ifdef IEM_WITH_THROW_CATCH
#  ifdef VBOX_WITH_IEM_NATIVE_RECOMPILER_LONGJMP
#   define IEM_DO_LONGJMP(a_pVCpu, a_rc) do { \
            if ((a_pVCpu)->iem.s.pvTbFramePointerR3) \
                iemNativeTbLongJmp((a_pVCpu)->iem.s.pvTbFramePointerR3, (a_rc)); \
            throw int(a_rc); \
        } while (0)
#  else
#   define IEM_DO_LONGJMP(a_pVCpu, a_rc) throw int(a_rc)
#  endif
# else
#  define IEM_DO_LONGJMP(a_pVCpu, a_rc)  longjmp(*(a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf), (a_rc))
# endif
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
#if defined(IEM_WITH_SETJMP) && (defined(_MSC_VER) || defined(IEM_WITH_THROW_CATCH))
# define IEM_NOEXCEPT_MAY_LONGJMP   RT_NOEXCEPT_EX(false)
#else
# define IEM_NOEXCEPT_MAY_LONGJMP   RT_NOEXCEPT
#endif
/* ASM-NOINC-END */

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

//#define IEM_WITH_CODE_TLB // - work in progress
//#define IEM_WITH_DATA_TLB // - work in progress


/** @def IEM_USE_UNALIGNED_DATA_ACCESS
 * Use unaligned accesses instead of elaborate byte assembly. */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(DOXYGEN_RUNNING)  /*ASM-NOINC*/
# define IEM_USE_UNALIGNED_DATA_ACCESS
#endif                                                                          /*ASM-NOINC*/

//#define IEM_LOG_MEMORY_WRITES



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
 * Branch types.
 */
typedef enum IEMBRANCH
{
    IEMBRANCH_JUMP = 1,
    IEMBRANCH_CALL,
    IEMBRANCH_TRAP,
    IEMBRANCH_SOFTWARE_INT,
    IEMBRANCH_HARDWARE_INT
} IEMBRANCH;
AssertCompileSize(IEMBRANCH, 4);


/**
 * INT instruction types.
 */
typedef enum IEMINT
{
    /** INT n instruction (opcode 0xcd imm). */
    IEMINT_INTN  = 0,
    /** Single byte INT3 instruction (opcode 0xcc). */
    IEMINT_INT3  = IEM_XCPT_FLAGS_BP_INSTR,
    /** Single byte INTO instruction (opcode 0xce). */
    IEMINT_INTO  = IEM_XCPT_FLAGS_OF_INSTR,
    /** Single byte INT1 (ICEBP) instruction (opcode 0xf1). */
    IEMINT_INT1 = IEM_XCPT_FLAGS_ICEBP_INSTR
} IEMINT;
AssertCompileSize(IEMINT, 4);


/**
 * A FPU result.
 */
typedef struct IEMFPURESULT
{
    /** The output value. */
    RTFLOAT80U      r80Result;
    /** The output status. */
    uint16_t        FSW;
} IEMFPURESULT;
AssertCompileMemberOffset(IEMFPURESULT, FSW, 10);
/** Pointer to a FPU result. */
typedef IEMFPURESULT *PIEMFPURESULT;
/** Pointer to a const FPU result. */
typedef IEMFPURESULT const *PCIEMFPURESULT;


/**
 * A FPU result consisting of two output values and FSW.
 */
typedef struct IEMFPURESULTTWO
{
    /** The first output value. */
    RTFLOAT80U      r80Result1;
    /** The output status. */
    uint16_t        FSW;
    /** The second output value. */
    RTFLOAT80U      r80Result2;
} IEMFPURESULTTWO;
AssertCompileMemberOffset(IEMFPURESULTTWO, FSW, 10);
AssertCompileMemberOffset(IEMFPURESULTTWO, r80Result2, 12);
/** Pointer to a FPU result consisting of two output values and FSW. */
typedef IEMFPURESULTTWO *PIEMFPURESULTTWO;
/** Pointer to a const FPU result consisting of two output values and FSW. */
typedef IEMFPURESULTTWO const *PCIEMFPURESULTTWO;


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
     */
    uint64_t                uTag;
    /** Access flags and physical TLB revision.
     *
     * - Bit  0 - page tables   - not executable (X86_PTE_PAE_NX).
     * - Bit  1 - page tables   - not writable (complemented X86_PTE_RW).
     * - Bit  2 - page tables   - not user (complemented X86_PTE_US).
     * - Bit  3 - pgm phys/virt - not directly writable.
     * - Bit  4 - pgm phys page - not directly readable.
     * - Bit  5 - page tables   - not accessed (complemented X86_PTE_A).
     * - Bit  6 - page tables   - not dirty (complemented X86_PTE_D).
     * - Bit  7 - tlb entry     - pMappingR3 member not valid.
     * - Bits 63 thru 8 are used for the physical TLB revision number.
     *
     * We're using complemented bit meanings here because it makes it easy to check
     * whether special action is required.  For instance a user mode write access
     * would do a "TEST fFlags, (X86_PTE_RW | X86_PTE_US | X86_PTE_D)" and a
     * non-zero result would mean special handling needed because either it wasn't
     * writable, or it wasn't user, or the page wasn't dirty.  A user mode read
     * access would do "TEST fFlags, X86_PTE_US"; and a kernel mode read wouldn't
     * need to check any PTE flag.
     */
    uint64_t                fFlagsAndPhysRev;
    /** The guest physical page address. */
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
#define IEMTLBE_F_PT_NO_EXEC        RT_BIT_64(0)  /**< Page tables: Not executable. */
#define IEMTLBE_F_PT_NO_WRITE       RT_BIT_64(1)  /**< Page tables: Not writable. */
#define IEMTLBE_F_PT_NO_USER        RT_BIT_64(2)  /**< Page tables: Not user accessible (supervisor only). */
#define IEMTLBE_F_PG_NO_WRITE       RT_BIT_64(3)  /**< Phys page:   Not writable (access handler, ROM, whatever). */
#define IEMTLBE_F_PG_NO_READ        RT_BIT_64(4)  /**< Phys page:   Not readable (MMIO / access handler, ROM) */
#define IEMTLBE_F_PT_NO_ACCESSED    RT_BIT_64(5)  /**< Phys tables: Not accessed (need to be marked accessed). */
#define IEMTLBE_F_PT_NO_DIRTY       RT_BIT_64(6)  /**< Page tables: Not dirty (needs to be made dirty on write). */
#define IEMTLBE_F_PT_LARGE_PAGE     RT_BIT_64(7)  /**< Page tables: Large 2 or 4 MiB page (for flushing). */
#define IEMTLBE_F_NO_MAPPINGR3      RT_BIT_64(8)  /**< TLB entry:   The IEMTLBENTRY::pMappingR3 member is invalid. */
#define IEMTLBE_F_PG_UNASSIGNED     RT_BIT_64(9)  /**< Phys page:   Unassigned memory (not RAM, ROM, MMIO2 or MMIO). */
#define IEMTLBE_F_PG_CODE_PAGE      RT_BIT_64(10) /**< Phys page:   Code page. */
#define IEMTLBE_F_PHYS_REV          UINT64_C(0xfffffffffffff800) /**< Physical revision mask. @sa IEMTLB_PHYS_REV_INCR */
/** @} */
AssertCompile(PGMIEMGCPHYS2PTR_F_NO_WRITE     == IEMTLBE_F_PG_NO_WRITE);
AssertCompile(PGMIEMGCPHYS2PTR_F_NO_READ      == IEMTLBE_F_PG_NO_READ);
AssertCompile(PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 == IEMTLBE_F_NO_MAPPINGR3);
AssertCompile(PGMIEMGCPHYS2PTR_F_UNASSIGNED   == IEMTLBE_F_PG_UNASSIGNED);
AssertCompile(PGMIEMGCPHYS2PTR_F_CODE_PAGE    == IEMTLBE_F_PG_CODE_PAGE);
AssertCompile(PGM_WALKINFO_BIG_PAGE           == IEMTLBE_F_PT_LARGE_PAGE);
/** The bits set by PGMPhysIemGCPhys2PtrNoLock. */
#define IEMTLBE_GCPHYS2PTR_MASK     (  PGMIEMGCPHYS2PTR_F_NO_WRITE \
                                     | PGMIEMGCPHYS2PTR_F_NO_READ \
                                     | PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 \
                                     | PGMIEMGCPHYS2PTR_F_UNASSIGNED \
                                     | PGMIEMGCPHYS2PTR_F_CODE_PAGE \
                                     | IEMTLBE_F_PHYS_REV )


/** The TLB size (power of two).
 * We initially chose 256 because that way we can obtain the result directly
 * from a 8-bit register without an additional AND instruction.
 * See also @bugref{10687}. */
#if defined(RT_ARCH_AMD64)
# define IEMTLB_ENTRY_COUNT                      256
# define IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO      8
#else
# define IEMTLB_ENTRY_COUNT                      8192
# define IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO      13
#endif
AssertCompile(RT_BIT_32(IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO) == IEMTLB_ENTRY_COUNT);

/** TLB slot format spec (assumes uint32_t or unsigned value). */
#if IEMTLB_ENTRY_COUNT <= 0x100 / 2
# define IEMTLB_SLOT_FMT    "%02x"
#elif IEMTLB_ENTRY_COUNT <= 0x1000 / 2
# define IEMTLB_SLOT_FMT    "%03x"
#elif IEMTLB_ENTRY_COUNT <= 0x10000 / 2
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
#define IEMTLB_WITH_LARGE_PAGE_BITMAP

/**
 * An IEM TLB.
 *
 * We've got two of these, one for data and one for instructions.
 */
typedef struct IEMTLB
{
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
     * This is actually only 56 bits wide (see IEMTLBENTRY::fFlagsAndPhysRev) and is
     * incremented by adding RT_BIT_64(8).  When it wraps around and becomes zero,
     * a rendezvous is called and each CPU wipe the IEMTLBENTRY::pMappingR3 as well
     * as IEMTLBENTRY::fFlagsAndPhysRev bits 63 thru 8, 4, and 3.
     *
     * The initial value is choosen to cause an early wraparound.
     *
     * @note This is placed between the two TLB revisions because we
     *       load it in pair with one or the other on arm64. */
    uint64_t volatile   uTlbPhysRev;
    /** The global TLB revision.
     * Same as uTlbRevision, but only increased for global flushes. */
    uint64_t            uTlbRevisionGlobal;

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
    /** Large page range for non-global pages. */
                        NonGlobalLargePageRange,
    /** Large page range for global pages. */
                        GlobalLargePageRange;
    /** Number of non-global entries for large pages loaded since last TLB flush. */
    uint32_t            cTlbNonGlobalLargePageCurLoads;
    /** Number of global entries for large pages loaded since last TLB flush. */
    uint32_t            cTlbGlobalLargePageCurLoads;

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
    /** Subset of cTlbCoreMisses that results in PTE.G=1 loads (odd entries). */
    uint64_t            cTlbCoreGlobalLoads;
    /** Safe read/write TLB misses in iemMemMapJmp (so data only). */
    uint64_t            cTlbSafeMisses;
    /** Subset of cTlbSafeMisses that results in PTE.G=1 loads (odd entries). */
    uint64_t            cTlbSafeGlobalLoads;
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

    uint32_t            au32Padding[13];

    /** The TLB entries.
     * Even entries are for PTE.G=0 and uses uTlbRevision.
     * Odd  entries are for PTE.G=1 and uses uTlbRevisionGlobal. */
    IEMTLBENTRY         aEntries[IEMTLB_ENTRY_COUNT * 2];
#ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
    /** Bitmap tracking TLB entries for large pages.
     * This duplicates IEMTLBE_F_PT_LARGE_PAGE for each TLB entry. */
    uint64_t            bmLargePage[IEMTLB_ENTRY_COUNT * 2 / 64];
#endif
} IEMTLB;
AssertCompileSizeAlignment(IEMTLB, 64);
#ifdef IEMTLB_WITH_LARGE_PAGE_BITMAP
AssertCompile(IEMTLB_ENTRY_COUNT >= 32 /* bmLargePage ASSUMPTION */);
#endif
/** The width (in bits) of the address portion of the TLB tag.   */
#define IEMTLB_TAG_ADDR_WIDTH   36
/** IEMTLB::uTlbRevision increment.  */
#define IEMTLB_REVISION_INCR    RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH)
/** IEMTLB::uTlbRevision mask.  */
#define IEMTLB_REVISION_MASK    (~(RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH) - 1))

/** IEMTLB::uTlbPhysRev increment.
 * @sa IEMTLBE_F_PHYS_REV */
#define IEMTLB_PHYS_REV_INCR    RT_BIT_64(11)
AssertCompile(IEMTLBE_F_PHYS_REV == ~(IEMTLB_PHYS_REV_INCR - 1U));

/**
 * Calculates the TLB tag for a virtual address but without TLB revision.
 * @returns Tag value for indexing and comparing with IEMTLB::uTag.
 * @param   a_GCPtr     The virtual address.  Must be RTGCPTR or same size or
 *                      the clearing of the top 16 bits won't work (if 32-bit
 *                      we'll end up with mostly zeros).
 */
#define IEMTLB_CALC_TAG_NO_REV(a_GCPtr)     ( (((a_GCPtr) << 16) >> (GUEST_PAGE_SHIFT + 16)) )
/**
 * Converts a TLB tag value into a even TLB index.
 * @returns Index into IEMTLB::aEntries.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG.
 */
#if IEMTLB_ENTRY_COUNT == 256
# define IEMTLB_TAG_TO_EVEN_INDEX(a_uTag)   ( (uint8_t)(a_uTag) * 2U )
#else
# define IEMTLB_TAG_TO_EVEN_INDEX(a_uTag)   ( ((a_uTag) & (IEMTLB_ENTRY_COUNT - 1U)) * 2U )
AssertCompile(RT_IS_POWER_OF_TWO(IEMTLB_ENTRY_COUNT));
#endif
/**
 * Converts a TLB tag value into an even TLB index.
 * @returns Pointer into IEMTLB::aEntries corresponding to .
 * @param   a_pTlb      The TLB.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG or
 *                      IEMTLB_CALC_TAG_NO_REV.
 */
#define IEMTLB_TAG_TO_EVEN_ENTRY(a_pTlb, a_uTag)    ( &(a_pTlb)->aEntries[IEMTLB_TAG_TO_EVEN_INDEX(a_uTag)] )

/** Converts a GC address to an even TLB index. */
#define IEMTLB_ADDR_TO_EVEN_INDEX(a_GCPtr)  IEMTLB_TAG_TO_EVEN_INDEX(IEMTLB_CALC_TAG_NO_REV(a_GCPtr))


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
    kIemTlbTraceType_FlushGlobal,
    kIemTlbTraceType_Load,
    kIemTlbTraceType_LoadGlobal,
    kIemTlbTraceType_Load_Cr0,
    kIemTlbTraceType_Load_Cr3,
    kIemTlbTraceType_Load_Cr4,
    kIemTlbTraceType_Load_Efer,
    kIemTlbTraceType_Irq,
    kIemTlbTraceType_Xcpt,
    kIemTlbTraceType_IRet,
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
/** Flag set if it's a far branch (changes CS). */
#define IEM_CIMPL_F_BRANCH_FAR           RT_BIT_32(4)
/** Convenience: Testing any kind of branch. */
#define IEM_CIMPL_F_BRANCH_ANY          (IEM_CIMPL_F_BRANCH_DIRECT | IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_RELATIVE)

/** Execution flags may change (IEMCPU::fExec). */
#define IEM_CIMPL_F_MODE                RT_BIT_32(5)
/** May change significant portions of RFLAGS. */
#define IEM_CIMPL_F_RFLAGS              RT_BIT_32(6)
/** May change the status bits (X86_EFL_STATUS_BITS) in RFLAGS. */
#define IEM_CIMPL_F_STATUS_FLAGS        RT_BIT_32(7)
/** May trigger interrupt shadowing. */
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
 *       status if they don't update the PC. */
#define IEM_CIMPL_F_REP                 RT_BIT_32(13)
/** I/O instruction.
 * @todo Not sure if this is useful yet.  */
#define IEM_CIMPL_F_IO                  RT_BIT_32(14)
/** Force end of TB after the instruction. */
#define IEM_CIMPL_F_END_TB              RT_BIT_32(15)
/** Flag set if a branch may also modify the stack (push/pop return address). */
#define IEM_CIMPL_F_BRANCH_STACK        RT_BIT_32(16)
/** Flag set if a branch may also modify the stack (push/pop return address)
 *  and switch it (load/restore SS:RSP). */
#define IEM_CIMPL_F_BRANCH_STACK_FAR    RT_BIT_32(17)
/** Convenience: Raise exception (technically unnecessary, since it shouldn't return VINF_SUCCESS). */
#define IEM_CIMPL_F_XCPT \
    (IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR \
     | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT)

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
 * @note The python scripts will add this if missing.  */
#define IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE    RT_BIT_32(20)
/** The block calls an ASM-implementation instruction function with an implicit
 * X86XSAVEAREA pointer argument.
 * Mutually exclusive with IEM_CIMPL_F_CALLS_CIMPL, IEM_CIMPL_F_CALLS_AIMPL and
 * IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE.
 * @note No different from IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE, so same value.
 * @note The python scripts will add this if missing.  */
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
/** Mode: The block target mode mask. */
#define IEM_F_MODE_MASK                     UINT32_C(0x0000001f)
/** Mode: The IEMMODE part of the IEMTB_F_MODE_MASK value. */
#define IEM_F_MODE_CPUMODE_MASK             UINT32_C(0x00000003)
/** X86 Mode: Bit used to indicating pre-386 CPU in 16-bit mode (for eliminating
 * conditional in EIP/IP updating), and flat wide open CS, SS, DS, and ES in
 * 32-bit mode (for simplifying most memory accesses). */
#define IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK UINT32_C(0x00000004)
/** X86 Mode: Bit indicating protected mode, real mode (or SMM) when not set. */
#define IEM_F_MODE_X86_PROT_MASK            UINT32_C(0x00000008)
/** X86 Mode: Bit used to indicate virtual 8086 mode (only 16-bit). */
#define IEM_F_MODE_X86_V86_MASK             UINT32_C(0x00000010)

/** X86 Mode: 16-bit on 386 or later. */
#define IEM_F_MODE_X86_16BIT                UINT32_C(0x00000000)
/** X86 Mode: 80286, 80186 and 8086/88 targetting blocks (EIP update opt). */
#define IEM_F_MODE_X86_16BIT_PRE_386        UINT32_C(0x00000004)
/** X86 Mode: 16-bit protected mode on 386 or later. */
#define IEM_F_MODE_X86_16BIT_PROT           UINT32_C(0x00000008)
/** X86 Mode: 16-bit protected mode on 386 or later. */
#define IEM_F_MODE_X86_16BIT_PROT_PRE_386   UINT32_C(0x0000000c)
/** X86 Mode: 16-bit virtual 8086 protected mode (on 386 or later). */
#define IEM_F_MODE_X86_16BIT_PROT_V86       UINT32_C(0x00000018)

/** X86 Mode: 32-bit on 386 or later. */
#define IEM_F_MODE_X86_32BIT                UINT32_C(0x00000001)
/** X86 Mode: 32-bit mode with wide open flat CS, SS, DS and ES. */
#define IEM_F_MODE_X86_32BIT_FLAT           UINT32_C(0x00000005)
/** X86 Mode: 32-bit protected mode. */
#define IEM_F_MODE_X86_32BIT_PROT           UINT32_C(0x00000009)
/** X86 Mode: 32-bit protected mode with wide open flat CS, SS, DS and ES. */
#define IEM_F_MODE_X86_32BIT_PROT_FLAT      UINT32_C(0x0000000d)

/** X86 Mode: 64-bit (includes protected, but not the flat bit). */
#define IEM_F_MODE_X86_64BIT                UINT32_C(0x0000000a)

/** X86 Mode: Checks if @a a_fExec represent a FLAT mode. */
#define IEM_F_MODE_X86_IS_FLAT(a_fExec)     (   ((a_fExec) & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT \
                                             || ((a_fExec) & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT \
                                             || ((a_fExec) & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT)

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

/** Pending breakpoint mask (what iemCalcExecDbgFlags works out). */
#define IEM_F_PENDING_BRK_MASK              (IEM_F_PENDING_BRK_INSTR | IEM_F_PENDING_BRK_DATA | IEM_F_PENDING_BRK_X86_IO)

/** Caller configurable options. */
#define IEM_F_USER_OPTS                     (IEM_F_BYPASS_HANDLERS | IEM_F_X86_DISREGARD_LOCK)

/** X86: The current protection level (CPL) shift factor.   */
#define IEM_F_X86_CPL_SHIFT                 8
/** X86: The current protection level (CPL) mask. */
#define IEM_F_X86_CPL_MASK                  UINT32_C(0x00000300)
/** X86: The current protection level (CPL) shifted mask. */
#define IEM_F_X86_CPL_SMASK                 UINT32_C(0x00000003)

/** X86: Alignment checks enabled (CR0.AM=1 & EFLAGS.AC=1). */
#define IEM_F_X86_AC                        UINT32_C(0x00080000)

/** X86 execution context.
 * The IEM_F_X86_CTX_XXX values are individual flags that can be combined (with
 * the exception of IEM_F_X86_CTX_NORMAL).  This allows running VMs from SMM
 * mode. */
#define IEM_F_X86_CTX_MASK                  UINT32_C(0x0000f000)
/** X86 context: Plain regular execution context. */
#define IEM_F_X86_CTX_NORMAL                UINT32_C(0x00000000)
/** X86 context: VT-x enabled. */
#define IEM_F_X86_CTX_VMX                   UINT32_C(0x00001000)
/** X86 context: AMD-V enabled. */
#define IEM_F_X86_CTX_SVM                   UINT32_C(0x00002000)
/** X86 context: In AMD-V or VT-x guest mode. */
#define IEM_F_X86_CTX_IN_GUEST              UINT32_C(0x00004000)
/** X86 context: System management mode (SMM). */
#define IEM_F_X86_CTX_SMM                   UINT32_C(0x00008000)

/** @todo Add TF+RF+INHIBIT indicator(s), so we can eliminate the conditional in
 * iemRegFinishClearingRF() most for most situations (CPUMCTX_DBG_HIT_DRX_MASK
 * and CPUMCTX_DBG_DBGF_MASK are covered by the IEM_F_PENDING_BRK_XXX bits
 * alread). */

/** @todo Add TF+RF+INHIBIT indicator(s), so we can eliminate the conditional in
 *        iemRegFinishClearingRF() most for most situations
 *        (CPUMCTX_DBG_HIT_DRX_MASK and CPUMCTX_DBG_DBGF_MASK are covered by
 *        the IEM_F_PENDING_BRK_XXX bits alread). */

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
 * @see CPUMCTX_INHIBIT_SHADOW, CPUMIsInInterruptShadow() */
#define IEMTB_F_INHIBIT_SHADOW          UINT32_C(0x04000000)
/** Set when we're currently inhibiting NMIs
 * @see CPUMCTX_INHIBIT_NMI, CPUMAreInterruptsInhibitedByNmi() */
#define IEMTB_F_INHIBIT_NMI             UINT32_C(0x08000000)

/** Checks that EIP/IP is wihin CS.LIM before each instruction.  Used when
 * we're close the limit before starting a TB, as determined by
 * iemGetTbFlagsForCurrentPc(). */
#define IEMTB_F_CS_LIM_CHECKS           UINT32_C(0x10000000)

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
#define IEMTB_F_KEY_MASK                ((UINT32_MAX & ~(IEM_F_X86_CTX_MASK | IEMTB_F_TYPE_MASK)) | IEM_F_X86_CTX_SMM)
/** @} */

AssertCompile( (IEM_F_MODE_X86_16BIT              & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_PROT_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT              & IEM_F_MODE_X86_V86_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(  IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_PROT_MASK));
AssertCompile(!(IEM_F_MODE_X86_16BIT_PRE_386      & IEM_F_MODE_X86_V86_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT         & IEM_F_MODE_X86_V86_MASK));
AssertCompile( (IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_16BIT);
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT_PRE_386 & IEM_F_MODE_X86_V86_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_V86     & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_16BIT_PROT_V86     & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_16BIT_PROT_V86     & IEM_F_MODE_X86_V86_MASK);

AssertCompile( (IEM_F_MODE_X86_32BIT              & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(!(IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(!(IEM_F_MODE_X86_32BIT              & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(  IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(!(IEM_F_MODE_X86_32BIT_FLAT         & IEM_F_MODE_X86_PROT_MASK));
AssertCompile( (IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(!(IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT         & IEM_F_MODE_X86_PROT_MASK);
AssertCompile( (IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT);
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK);
AssertCompile(  IEM_F_MODE_X86_32BIT_PROT_FLAT    & IEM_F_MODE_X86_PROT_MASK);

AssertCompile( (IEM_F_MODE_X86_64BIT              & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT);
AssertCompile(  IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_PROT_MASK);
AssertCompile(!(IEM_F_MODE_X86_64BIT              & IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK));

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

    /** Generic parameters. */
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
/** Get the lookup table index from IEMTHRDEDCALLENTRY::uTbLookup and RIP. */
#define IEM_TB_LOOKUP_TAB_GET_IDX_WITH_RIP(a_uTbLookup, a_Rip) \
    (!((a_uTbLookup) & 0x80) ? (a_uTbLookup) & 0x7f : ((a_uTbLookup) & 0x7f) + ((a_Rip) & (IEM_TB_LOOKUP_TAB_LARGE_SIZE - 1)) )

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
/** Pointer to a per-CPU translation block cahce. */
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
/** Flag set if it's a far branch. */
#define IEMBRANCHED_F_FAR           UINT8_C(0x10)
/** Flag set if the stack pointer is modified. */
#define IEMBRANCHED_F_STACK         UINT8_C(0x20)
/** Flag set if the stack pointer and (maybe) the stack segment are modified. */
#define IEMBRANCHED_F_STACK_FAR     UINT8_C(0x40)
/** Flag set (by IEM_MC_REL_JMP_XXX) if it's a zero bytes relative jump. */
#define IEMBRANCHED_F_ZERO          UINT8_C(0x80)
/** @} */


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
    /** The number of bytes available at pbInstrBuf for the current instruction.
     * This takes the max opcode length into account so that doesn't need to be
     * checked separately. */
    uint32_t                cbInstrBuf;                                                                     /* 0x0c */
    /** Pointer to the page containing RIP, user specified buffer or abOpcode.
     * This can be NULL if the page isn't mappable for some reason, in which
     * case we'll do fallback stuff.
     *
     * If we're executing an instruction from a user specified buffer,
     * IEMExecOneWithPrefetchedByPC and friends, this is not necessarily a page
     * aligned pointer but pointer to the user data.
     *
     * For instructions crossing pages, this will start on the first page and be
     * advanced to the next page by the time we've decoded the instruction.  This
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
    /** The number of bytes available at pbInstrBuf in total (for IEMExecLots).
     * This takes the CS segment limit into account.
     * @note Set to zero when the code TLB is flushed to trigger TLB reload. */
    uint16_t                cbInstrBufTotal;                                                                /* 0x28 */
    /** Offset into pbInstrBuf of the first byte of the current instruction.
     * Can be negative to efficiently handle cross page instructions. */
    int16_t                 offCurInstrStart;                                                               /* 0x2a */

# ifndef IEM_WITH_OPAQUE_DECODER_STATE
    /** The prefix mask (IEM_OP_PRF_XXX). */
    uint32_t                fPrefixes;                                                                      /* 0x2c */
    /** The extra REX ModR/M register field bit (REX.R << 3). */
    uint8_t                 uRexReg;                                                                        /* 0x30 */
    /** The extra REX ModR/M r/m field, SIB base and opcode reg bit
     * (REX.B << 3). */
    uint8_t                 uRexB;                                                                          /* 0x31 */
    /** The extra REX SIB index field bit (REX.X << 3). */
    uint8_t                 uRexIndex;                                                                      /* 0x32 */

    /** The effective segment register (X86_SREG_XXX). */
    uint8_t                 iEffSeg;                                                                        /* 0x33 */

    /** The offset of the ModR/M byte relative to the start of the instruction. */
    uint8_t                 offModRm;                                                                       /* 0x34 */

#  ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    /** The current offset into abOpcode. */
    uint8_t                 offOpcode;                                                                      /* 0x35 */
#  else
    uint8_t                 bUnused;                                                                        /* 0x35 */
#  endif
# else  /* IEM_WITH_OPAQUE_DECODER_STATE */
    uint8_t                 abOpaqueDecoderPart1[0x36 - 0x2c];
# endif /* IEM_WITH_OPAQUE_DECODER_STATE */

#else  /* !IEM_WITH_CODE_TLB */
#  ifndef IEM_WITH_OPAQUE_DECODER_STATE
    /** The size of what has currently been fetched into abOpcode. */
    uint8_t                 cbOpcode;                                                                       /*       0x08 */
    /** The current offset into abOpcode. */
    uint8_t                 offOpcode;                                                                      /*       0x09 */
    /** The offset of the ModR/M byte relative to the start of the instruction. */
    uint8_t                 offModRm;                                                                       /*       0x0a */

    /** The effective segment register (X86_SREG_XXX). */
    uint8_t                 iEffSeg;                                                                        /*       0x0b */

    /** The prefix mask (IEM_OP_PRF_XXX). */
    uint32_t                fPrefixes;                                                                      /*       0x0c */
    /** The extra REX ModR/M register field bit (REX.R << 3). */
    uint8_t                 uRexReg;                                                                        /*       0x10 */
    /** The extra REX ModR/M r/m field, SIB base and opcode reg bit
     * (REX.B << 3). */
    uint8_t                 uRexB;                                                                          /*       0x11 */
    /** The extra REX SIB index field bit (REX.X << 3). */
    uint8_t                 uRexIndex;                                                                      /*       0x12 */

# else  /* IEM_WITH_OPAQUE_DECODER_STATE */
    uint8_t                 abOpaqueDecoderPart1[0x13 - 0x08];
# endif /* IEM_WITH_OPAQUE_DECODER_STATE */
#endif /* !IEM_WITH_CODE_TLB */

#ifndef IEM_WITH_OPAQUE_DECODER_STATE
    /** The effective operand mode. */
    IEMMODE                 enmEffOpSize;                                                                   /* 0x36, 0x13 */
    /** The default addressing mode. */
    IEMMODE                 enmDefAddrMode;                                                                 /* 0x37, 0x14 */
    /** The effective addressing mode. */
    IEMMODE                 enmEffAddrMode;                                                                 /* 0x38, 0x15 */
    /** The default operand mode. */
    IEMMODE                 enmDefOpSize;                                                                   /* 0x39, 0x16 */

    /** Prefix index (VEX.pp) for two byte and three byte tables. */
    uint8_t                 idxPrefix;                                                                      /* 0x3a, 0x17 */
    /** 3rd VEX/EVEX/XOP register.
     * Please use IEM_GET_EFFECTIVE_VVVV to access.  */
    uint8_t                 uVex3rdReg;                                                                     /* 0x3b, 0x18 */
    /** The VEX/EVEX/XOP length field. */
    uint8_t                 uVexLength;                                                                     /* 0x3c, 0x19 */
    /** Additional EVEX stuff. */
    uint8_t                 fEvexStuff;                                                                     /* 0x3d, 0x1a */

# ifndef IEM_WITH_CODE_TLB
    /** Explicit alignment padding. */
    uint8_t                 abAlignment2a[1];                                                               /*       0x1b */
# endif
    /** The FPU opcode (FOP). */
    uint16_t                uFpuOpcode;                                                                     /* 0x3e, 0x1c */
# ifndef IEM_WITH_CODE_TLB
    /** Explicit alignment padding. */
    uint8_t                 abAlignment2b[2];                                                               /*       0x1e */
# endif

    /** The opcode bytes. */
    uint8_t                 abOpcode[15];                                                                   /* 0x40, 0x20 */
    /** Explicit alignment padding. */
# ifdef IEM_WITH_CODE_TLB
    //uint8_t                 abAlignment2c[0x4f - 0x4f];                                                     /* 0x4f */
# else
    uint8_t                 abAlignment2c[0x4f - 0x2f];                                                     /*       0x2f */
# endif

#else  /* IEM_WITH_OPAQUE_DECODER_STATE */
# ifdef IEM_WITH_CODE_TLB
    uint8_t                 abOpaqueDecoderPart2[0x4f - 0x36];
# else
    uint8_t                 abOpaqueDecoderPart2[0x4f - 0x13];
# endif
#endif /* IEM_WITH_OPAQUE_DECODER_STATE */
    /** @} */


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
    } aMemMappings[3];                                                                                      /* 0x50 LB 0x30 */

    /** Locking records for the mapped memory. */
    union
    {
        PGMPAGEMAPLOCK      Lock;
        uint64_t            au64Padding[2];
    } aMemMappingLocks[3];                                                                                  /* 0x80 LB 0x30 */

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
    } aMemBbMappings[3];                                                                                    /* 0xb0 LB 0x48 */

    /** The flags of the current exception / interrupt. */
    uint32_t                fCurXcpt;                                                                       /* 0xf8 */
    /** The current exception / interrupt. */
    uint8_t                 uCurXcpt;                                                                       /* 0xfc */
    /** Exception / interrupt recursion depth. */
    int8_t                  cXcptRecursions;                                                                /* 0xfb */

    /** The next unused mapping index.
     * @todo try find room for this up with cActiveMappings. */
    uint8_t                 iNextMapping;                                                                   /* 0xfd */
    uint8_t                 abAlignment7[1];

    /** Bounce buffer storage.
     * This runs in parallel to aMemMappings and aMemBbMappings. */
    struct
    {
        uint8_t             ab[512];
    } aBounceBuffers[3];                                                                                    /* 0x100 LB 0x600 */


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
    /** The number of bytes data or stack written (mostly for IEMExecOneEx).
     * This may contain uncommitted writes.  */
    uint32_t                cbWritten;
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

    /** @name Host CPU information.
     * @{ */
    /** The CPU vendor. */
    CPUMCPUVENDOR           enmHostCpuVendor;
    /** @} */

    /** Counts RDMSR \#GP(0) LogRel(). */
    uint8_t                 cLogRelRdMsr;
    /** Counts WRMSR \#GP(0) LogRel(). */
    uint8_t                 cLogRelWrMsr;
    /** Alignment padding. */
    uint8_t                 abAlignment9[42];


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
#if 0  /* unused */
    /** Previous GCPhysInstrBuf value - only valid if fTbCrossedPage is set.   */
    RTGCPHYS                GCPhysInstrBufPrev;
#endif

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

//#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
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
//#endif

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

/** @def IEM_GET_INSTR_LEN
 * Gets the instruction length. */
#ifdef IEM_WITH_CODE_TLB
# define IEM_GET_INSTR_LEN(a_pVCpu)     ((a_pVCpu)->iem.s.offInstrNextByte - (uint32_t)(int32_t)(a_pVCpu)->iem.s.offCurInstrStart)
#else
# define IEM_GET_INSTR_LEN(a_pVCpu)     ((a_pVCpu)->iem.s.offOpcode)
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
#if defined(IEM_WITH_SETJMP) || defined(DOXYGEN_RUNNING)
# ifdef IEM_WITH_THROW_CATCH
#  define IEM_TRY_SETJMP(a_pVCpu, a_rcTarget) \
        a_rcTarget = VINF_SUCCESS; \
        try
#  define IEM_TRY_SETJMP_AGAIN(a_pVCpu, a_rcTarget) \
        IEM_TRY_SETJMP(a_pVCpu, a_rcTarget)
#  define IEM_CATCH_LONGJMP_BEGIN(a_pVCpu, a_rcTarget) \
        catch (int rcThrown) \
        { \
            a_rcTarget = rcThrown
#  define IEM_CATCH_LONGJMP_END(a_pVCpu) \
        } \
        ((void)0)
# else  /* !IEM_WITH_THROW_CATCH */
#  define IEM_TRY_SETJMP(a_pVCpu, a_rcTarget) \
        jmp_buf  JmpBuf; \
        jmp_buf * volatile pSavedJmpBuf = (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf); \
        (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf) = &JmpBuf; \
        if ((rcStrict = setjmp(JmpBuf)) == 0)
#  define IEM_TRY_SETJMP_AGAIN(a_pVCpu, a_rcTarget) \
        pSavedJmpBuf = (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf); \
        (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf) = &JmpBuf; \
        if ((rcStrict = setjmp(JmpBuf)) == 0)
#  define IEM_CATCH_LONGJMP_BEGIN(a_pVCpu, a_rcTarget) \
        else \
        { \
            ((void)0)
#  define IEM_CATCH_LONGJMP_END(a_pVCpu) \
        } \
        (a_pVCpu)->iem.s.CTX_SUFF(pJmpBuf) = pSavedJmpBuf
# endif /* !IEM_WITH_THROW_CATCH */
#endif  /* IEM_WITH_SETJMP */


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
 * fallback for misaligned stuff. See @bugref{10547}. */
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

/** @name Prefix constants (IEMCPU::fPrefixes)
 * @{ */
#define IEM_OP_PRF_SEG_CS               RT_BIT_32(0)  /**< CS segment prefix (0x2e). */
#define IEM_OP_PRF_SEG_SS               RT_BIT_32(1)  /**< SS segment prefix (0x36). */
#define IEM_OP_PRF_SEG_DS               RT_BIT_32(2)  /**< DS segment prefix (0x3e). */
#define IEM_OP_PRF_SEG_ES               RT_BIT_32(3)  /**< ES segment prefix (0x26). */
#define IEM_OP_PRF_SEG_FS               RT_BIT_32(4)  /**< FS segment prefix (0x64). */
#define IEM_OP_PRF_SEG_GS               RT_BIT_32(5)  /**< GS segment prefix (0x65). */
#define IEM_OP_PRF_SEG_MASK             UINT32_C(0x3f)

#define IEM_OP_PRF_SIZE_OP              RT_BIT_32(8)  /**< Operand size prefix (0x66). */
#define IEM_OP_PRF_SIZE_REX_W           RT_BIT_32(9)  /**< REX.W prefix (0x48-0x4f). */
#define IEM_OP_PRF_SIZE_ADDR            RT_BIT_32(10) /**< Address size prefix (0x67). */

#define IEM_OP_PRF_LOCK                 RT_BIT_32(16) /**< Lock prefix (0xf0). */
#define IEM_OP_PRF_REPNZ                RT_BIT_32(17) /**< Repeat-not-zero prefix (0xf2). */
#define IEM_OP_PRF_REPZ                 RT_BIT_32(18) /**< Repeat-if-zero prefix (0xf3). */

#define IEM_OP_PRF_REX                  RT_BIT_32(24) /**< Any REX prefix (0x40-0x4f). */
#define IEM_OP_PRF_REX_B                RT_BIT_32(25) /**< REX.B prefix (0x41,0x43,0x45,0x47,0x49,0x4b,0x4d,0x4f). */
#define IEM_OP_PRF_REX_X                RT_BIT_32(26) /**< REX.X prefix (0x42,0x43,0x46,0x47,0x4a,0x4b,0x4e,0x4f). */
#define IEM_OP_PRF_REX_R                RT_BIT_32(27) /**< REX.R prefix (0x44,0x45,0x46,0x47,0x4c,0x4d,0x4e,0x4f). */
/** Mask with all the REX prefix flags.
 * This is generally for use when needing to undo the REX prefixes when they
 * are followed legacy prefixes and therefore does not immediately preceed
 * the first opcode byte.
 * For testing whether any REX prefix is present, use  IEM_OP_PRF_REX instead. */
#define IEM_OP_PRF_REX_MASK  (IEM_OP_PRF_REX | IEM_OP_PRF_REX_R | IEM_OP_PRF_REX_B | IEM_OP_PRF_REX_X | IEM_OP_PRF_SIZE_REX_W )

#define IEM_OP_PRF_VEX                  RT_BIT_32(28) /**< Indiciates VEX prefix. */
#define IEM_OP_PRF_EVEX                 RT_BIT_32(29) /**< Indiciates EVEX prefix. */
#define IEM_OP_PRF_XOP                  RT_BIT_32(30) /**< Indiciates XOP prefix. */
/** @} */

/** @name IEMOPFORM_XXX - Opcode forms
 * @note These are ORed together with IEMOPHINT_XXX.
 * @{ */
/** ModR/M: reg, r/m */
#define IEMOPFORM_RM            0
/** ModR/M: reg, r/m (register) */
#define IEMOPFORM_RM_REG        (IEMOPFORM_RM | IEMOPFORM_MOD3)
/** ModR/M: reg, r/m (memory)   */
#define IEMOPFORM_RM_MEM        (IEMOPFORM_RM | IEMOPFORM_NOT_MOD3)
/** ModR/M: reg, r/m, imm */
#define IEMOPFORM_RMI           1
/** ModR/M: reg, r/m (register), imm */
#define IEMOPFORM_RMI_REG       (IEMOPFORM_RMI | IEMOPFORM_MOD3)
/** ModR/M: reg, r/m (memory), imm   */
#define IEMOPFORM_RMI_MEM       (IEMOPFORM_RMI | IEMOPFORM_NOT_MOD3)
/** ModR/M: reg, r/m, xmm0 */
#define IEMOPFORM_RM0           2
/** ModR/M: reg, r/m (register), xmm0 */
#define IEMOPFORM_RM0_REG       (IEMOPFORM_RM0 | IEMOPFORM_MOD3)
/** ModR/M: reg, r/m (memory), xmm0   */
#define IEMOPFORM_RM0_MEM       (IEMOPFORM_RM0 | IEMOPFORM_NOT_MOD3)
/** ModR/M: r/m, reg */
#define IEMOPFORM_MR            3
/** ModR/M: r/m (register), reg */
#define IEMOPFORM_MR_REG        (IEMOPFORM_MR | IEMOPFORM_MOD3)
/** ModR/M: r/m (memory), reg */
#define IEMOPFORM_MR_MEM        (IEMOPFORM_MR | IEMOPFORM_NOT_MOD3)
/** ModR/M: r/m, reg, imm */
#define IEMOPFORM_MRI           4
/** ModR/M: r/m (register), reg, imm */
#define IEMOPFORM_MRI_REG       (IEMOPFORM_MRI | IEMOPFORM_MOD3)
/** ModR/M: r/m (memory), reg, imm */
#define IEMOPFORM_MRI_MEM       (IEMOPFORM_MRI | IEMOPFORM_NOT_MOD3)
/** ModR/M: r/m only */
#define IEMOPFORM_M             5
/** ModR/M: r/m only (register). */
#define IEMOPFORM_M_REG         (IEMOPFORM_M | IEMOPFORM_MOD3)
/** ModR/M: r/m only (memory). */
#define IEMOPFORM_M_MEM         (IEMOPFORM_M | IEMOPFORM_NOT_MOD3)
/** ModR/M: r/m, imm */
#define IEMOPFORM_MI            6
/** ModR/M: r/m (register), imm */
#define IEMOPFORM_MI_REG        (IEMOPFORM_MI | IEMOPFORM_MOD3)
/** ModR/M: r/m (memory), imm */
#define IEMOPFORM_MI_MEM        (IEMOPFORM_MI | IEMOPFORM_NOT_MOD3)
/** ModR/M: r/m, 1  (shift and rotate instructions) */
#define IEMOPFORM_M1            7
/** ModR/M: r/m (register), 1. */
#define IEMOPFORM_M1_REG        (IEMOPFORM_M1 | IEMOPFORM_MOD3)
/** ModR/M: r/m (memory), 1. */
#define IEMOPFORM_M1_MEM        (IEMOPFORM_M1 | IEMOPFORM_NOT_MOD3)
/** ModR/M: r/m, CL  (shift and rotate instructions)
 * @todo This should just've been a generic fixed register. But the python
 *       code doesn't needs more convincing. */
#define IEMOPFORM_M_CL          8
/** ModR/M: r/m (register), CL. */
#define IEMOPFORM_M_CL_REG      (IEMOPFORM_M_CL | IEMOPFORM_MOD3)
/** ModR/M: r/m (memory), CL. */
#define IEMOPFORM_M_CL_MEM      (IEMOPFORM_M_CL | IEMOPFORM_NOT_MOD3)
/** ModR/M: reg only */
#define IEMOPFORM_R             9

/** VEX+ModR/M: reg, r/m */
#define IEMOPFORM_VEX_RM        16
/** VEX+ModR/M: reg, r/m (register) */
#define IEMOPFORM_VEX_RM_REG    (IEMOPFORM_VEX_RM | IEMOPFORM_MOD3)
/** VEX+ModR/M: reg, r/m (memory)   */
#define IEMOPFORM_VEX_RM_MEM    (IEMOPFORM_VEX_RM | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: r/m, reg */
#define IEMOPFORM_VEX_MR        17
/** VEX+ModR/M: r/m (register), reg */
#define IEMOPFORM_VEX_MR_REG    (IEMOPFORM_VEX_MR | IEMOPFORM_MOD3)
/** VEX+ModR/M: r/m (memory), reg */
#define IEMOPFORM_VEX_MR_MEM    (IEMOPFORM_VEX_MR | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: r/m, reg, imm8 */
#define IEMOPFORM_VEX_MRI       18
/** VEX+ModR/M: r/m (register), reg, imm8 */
#define IEMOPFORM_VEX_MRI_REG   (IEMOPFORM_VEX_MRI | IEMOPFORM_MOD3)
/** VEX+ModR/M: r/m (memory), reg, imm8 */
#define IEMOPFORM_VEX_MRI_MEM   (IEMOPFORM_VEX_MRI | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: r/m only */
#define IEMOPFORM_VEX_M         19
/** VEX+ModR/M: r/m only (register). */
#define IEMOPFORM_VEX_M_REG     (IEMOPFORM_VEX_M | IEMOPFORM_MOD3)
/** VEX+ModR/M: r/m only (memory). */
#define IEMOPFORM_VEX_M_MEM     (IEMOPFORM_VEX_M | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: reg only */
#define IEMOPFORM_VEX_R         20
/** VEX+ModR/M: reg, vvvv, r/m */
#define IEMOPFORM_VEX_RVM       21
/** VEX+ModR/M: reg, vvvv, r/m (register). */
#define IEMOPFORM_VEX_RVM_REG   (IEMOPFORM_VEX_RVM | IEMOPFORM_MOD3)
/** VEX+ModR/M: reg, vvvv, r/m (memory). */
#define IEMOPFORM_VEX_RVM_MEM   (IEMOPFORM_VEX_RVM | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: reg, vvvv, r/m, imm */
#define IEMOPFORM_VEX_RVMI      22
/** VEX+ModR/M: reg, vvvv, r/m (register), imm. */
#define IEMOPFORM_VEX_RVMI_REG  (IEMOPFORM_VEX_RVMI | IEMOPFORM_MOD3)
/** VEX+ModR/M: reg, vvvv, r/m (memory), imm. */
#define IEMOPFORM_VEX_RVMI_MEM  (IEMOPFORM_VEX_RVMI | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: reg, vvvv, r/m, imm(reg) */
#define IEMOPFORM_VEX_RVMR      23
/** VEX+ModR/M: reg, vvvv, r/m (register), imm(reg). */
#define IEMOPFORM_VEX_RVMR_REG  (IEMOPFORM_VEX_RVMI | IEMOPFORM_MOD3)
/** VEX+ModR/M: reg, vvvv, r/m (memory), imm(reg). */
#define IEMOPFORM_VEX_RVMR_MEM  (IEMOPFORM_VEX_RVMI | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: reg, r/m, vvvv */
#define IEMOPFORM_VEX_RMV       24
/** VEX+ModR/M: reg, r/m, vvvv (register). */
#define IEMOPFORM_VEX_RMV_REG   (IEMOPFORM_VEX_RMV | IEMOPFORM_MOD3)
/** VEX+ModR/M: reg, r/m, vvvv (memory). */
#define IEMOPFORM_VEX_RMV_MEM   (IEMOPFORM_VEX_RMV | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: reg, r/m, imm8 */
#define IEMOPFORM_VEX_RMI       25
/** VEX+ModR/M: reg, r/m, imm8 (register). */
#define IEMOPFORM_VEX_RMI_REG   (IEMOPFORM_VEX_RMI | IEMOPFORM_MOD3)
/** VEX+ModR/M: reg, r/m, imm8 (memory). */
#define IEMOPFORM_VEX_RMI_MEM   (IEMOPFORM_VEX_RMI | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M: r/m, vvvv, reg */
#define IEMOPFORM_VEX_MVR       26
/** VEX+ModR/M: r/m, vvvv, reg (register) */
#define IEMOPFORM_VEX_MVR_REG   (IEMOPFORM_VEX_MVR | IEMOPFORM_MOD3)
/** VEX+ModR/M: r/m, vvvv, reg (memory) */
#define IEMOPFORM_VEX_MVR_MEM   (IEMOPFORM_VEX_MVR | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M+/n: vvvv, r/m */
#define IEMOPFORM_VEX_VM        27
/** VEX+ModR/M+/n: vvvv, r/m (register) */
#define IEMOPFORM_VEX_VM_REG    (IEMOPFORM_VEX_VM | IEMOPFORM_MOD3)
/** VEX+ModR/M+/n: vvvv, r/m (memory) */
#define IEMOPFORM_VEX_VM_MEM    (IEMOPFORM_VEX_VM | IEMOPFORM_NOT_MOD3)
/** VEX+ModR/M+/n: vvvv, r/m, imm8 */
#define IEMOPFORM_VEX_VMI       28
/** VEX+ModR/M+/n: vvvv, r/m, imm8 (register) */
#define IEMOPFORM_VEX_VMI_REG   (IEMOPFORM_VEX_VMI | IEMOPFORM_MOD3)
/** VEX+ModR/M+/n: vvvv, r/m, imm8 (memory) */
#define IEMOPFORM_VEX_VMI_MEM   (IEMOPFORM_VEX_VMI | IEMOPFORM_NOT_MOD3)

/** Fixed register instruction, no R/M. */
#define IEMOPFORM_FIXED         32

/** The r/m is a register. */
#define IEMOPFORM_MOD3          RT_BIT_32(8)
/** The r/m is a memory access. */
#define IEMOPFORM_NOT_MOD3      RT_BIT_32(9)
/** @} */

/** @name IEMOPHINT_XXX - Additional Opcode Hints
 * @note These are ORed together with IEMOPFORM_XXX.
 * @{ */
/** Ignores the operand size prefix (66h). */
#define IEMOPHINT_IGNORES_OZ_PFX    RT_BIT_32(10)
/** Ignores REX.W (aka WIG). */
#define IEMOPHINT_IGNORES_REXW      RT_BIT_32(11)
/** Both the operand size prefixes (66h + REX.W) are ignored. */
#define IEMOPHINT_IGNORES_OP_SIZES  (IEMOPHINT_IGNORES_OZ_PFX | IEMOPHINT_IGNORES_REXW)
/** Allowed with the lock prefix. */
#define IEMOPHINT_LOCK_ALLOWED      RT_BIT_32(11)
/** The VEX.L value is ignored (aka LIG). */
#define IEMOPHINT_VEX_L_IGNORED     RT_BIT_32(12)
/** The VEX.L value must be zero (i.e. 128-bit width only). */
#define IEMOPHINT_VEX_L_ZERO        RT_BIT_32(13)
/** The VEX.L value must be one (i.e. 256-bit width only). */
#define IEMOPHINT_VEX_L_ONE         RT_BIT_32(14)
/** The VEX.V value must be zero. */
#define IEMOPHINT_VEX_V_ZERO        RT_BIT_32(15)
/** The REX.W/VEX.V value must be zero. */
#define IEMOPHINT_REX_W_ZERO        RT_BIT_32(16)
#define IEMOPHINT_VEX_W_ZERO        IEMOPHINT_REX_W_ZERO
/** The REX.W/VEX.V value must be one. */
#define IEMOPHINT_REX_W_ONE         RT_BIT_32(17)
#define IEMOPHINT_VEX_W_ONE         IEMOPHINT_REX_W_ONE

/** Hint to IEMAllInstructionPython.py that this macro should be skipped.  */
#define IEMOPHINT_SKIP_PYTHON       RT_BIT_32(31)
/** @} */

/**
 * Possible hardware task switch sources.
 */
typedef enum IEMTASKSWITCH
{
    /** Task switch caused by an interrupt/exception. */
    IEMTASKSWITCH_INT_XCPT = 1,
    /** Task switch caused by a far CALL. */
    IEMTASKSWITCH_CALL,
    /** Task switch caused by a far JMP. */
    IEMTASKSWITCH_JUMP,
    /** Task switch caused by an IRET. */
    IEMTASKSWITCH_IRET
} IEMTASKSWITCH;
AssertCompileSize(IEMTASKSWITCH, 4);

/**
 * Possible CrX load (write) sources.
 */
typedef enum IEMACCESSCRX
{
    /** CrX access caused by 'mov crX' instruction. */
    IEMACCESSCRX_MOV_CRX,
    /** CrX (CR0) write caused by 'lmsw' instruction. */
    IEMACCESSCRX_LMSW,
    /** CrX (CR0) write caused by 'clts' instruction. */
    IEMACCESSCRX_CLTS,
    /** CrX (CR0) read caused by 'smsw' instruction. */
    IEMACCESSCRX_SMSW
} IEMACCESSCRX;

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/** @name IEM_SLAT_FAIL_XXX - Second-level address translation failure information.
 *
 * These flags provide further context to SLAT page-walk failures that could not be
 * determined by PGM (e.g, PGM is not privy to memory access permissions).
 *
 * @{
 */
/** Translating a nested-guest linear address failed accessing a nested-guest
 *  physical address. */
# define IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR          RT_BIT_32(0)
/** Translating a nested-guest linear address failed accessing a
 *  paging-structure entry or updating accessed/dirty bits. */
# define IEM_SLAT_FAIL_LINEAR_TO_PAGE_TABLE         RT_BIT_32(1)
/** @} */

DECLCALLBACK(FNPGMPHYSHANDLER)      iemVmxApicAccessPageHandler;
# ifndef IN_RING3
DECLCALLBACK(FNPGMRZPHYSPFHANDLER)  iemVmxApicAccessPagePfHandler;
# endif
#endif

/**
 * Indicates to the verifier that the given flag set is undefined.
 *
 * Can be invoked again to add more flags.
 *
 * This is a NOOP if the verifier isn't compiled in.
 *
 * @note We're temporarily keeping this until code is converted to new
 *       disassembler style opcode handling.
 */
#define IEMOP_VERIFICATION_UNDEFINED_EFLAGS(a_fEfl) do { } while (0)


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
    a_RetType __fastcall a_Name a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    a_RetType __fastcall a_Name a_ArgList RT_NOEXCEPT

#elif __cplusplus >= 201700 /* P0012R1 support */
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL a_Name) a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList RT_NOEXCEPT

#else
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL a_Name) a_ArgList
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    DECL_HIDDEN_ONLY(a_RetType) VBOXCALL a_Name a_ArgList

#endif

/** Defined in IEMAllAImplC.cpp but also used by IEMAllAImplA.asm. */
RT_C_DECLS_BEGIN
extern uint8_t const g_afParity[256];
RT_C_DECLS_END


/** @name Arithmetic assignment operations on bytes (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINU8, (uint32_t fEFlagsIn, uint8_t  *pu8Dst,  uint8_t  u8Src));
typedef FNIEMAIMPLBINU8  *PFNIEMAIMPLBINU8;
FNIEMAIMPLBINU8 iemAImpl_add_u8, iemAImpl_add_u8_locked;
FNIEMAIMPLBINU8 iemAImpl_adc_u8, iemAImpl_adc_u8_locked;
FNIEMAIMPLBINU8 iemAImpl_sub_u8, iemAImpl_sub_u8_locked;
FNIEMAIMPLBINU8 iemAImpl_sbb_u8, iemAImpl_sbb_u8_locked;
FNIEMAIMPLBINU8  iemAImpl_or_u8,  iemAImpl_or_u8_locked;
FNIEMAIMPLBINU8 iemAImpl_xor_u8, iemAImpl_xor_u8_locked;
FNIEMAIMPLBINU8 iemAImpl_and_u8, iemAImpl_and_u8_locked;
/** @} */

/** @name Arithmetic assignment operations on words (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINU16, (uint32_t fEFlagsIn, uint16_t *pu16Dst, uint16_t u16Src));
typedef FNIEMAIMPLBINU16  *PFNIEMAIMPLBINU16;
FNIEMAIMPLBINU16 iemAImpl_add_u16, iemAImpl_add_u16_locked;
FNIEMAIMPLBINU16 iemAImpl_adc_u16, iemAImpl_adc_u16_locked;
FNIEMAIMPLBINU16 iemAImpl_sub_u16, iemAImpl_sub_u16_locked;
FNIEMAIMPLBINU16 iemAImpl_sbb_u16, iemAImpl_sbb_u16_locked;
FNIEMAIMPLBINU16  iemAImpl_or_u16,  iemAImpl_or_u16_locked;
FNIEMAIMPLBINU16 iemAImpl_xor_u16, iemAImpl_xor_u16_locked;
FNIEMAIMPLBINU16 iemAImpl_and_u16, iemAImpl_and_u16_locked;
/** @}  */


/** @name Arithmetic assignment operations on double words (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINU32, (uint32_t fEFlagsIn, uint32_t *pu32Dst, uint32_t u32Src));
typedef FNIEMAIMPLBINU32 *PFNIEMAIMPLBINU32;
FNIEMAIMPLBINU32 iemAImpl_add_u32, iemAImpl_add_u32_locked;
FNIEMAIMPLBINU32 iemAImpl_adc_u32, iemAImpl_adc_u32_locked;
FNIEMAIMPLBINU32 iemAImpl_sub_u32, iemAImpl_sub_u32_locked;
FNIEMAIMPLBINU32 iemAImpl_sbb_u32, iemAImpl_sbb_u32_locked;
FNIEMAIMPLBINU32  iemAImpl_or_u32,  iemAImpl_or_u32_locked;
FNIEMAIMPLBINU32 iemAImpl_xor_u32, iemAImpl_xor_u32_locked;
FNIEMAIMPLBINU32 iemAImpl_and_u32, iemAImpl_and_u32_locked;
FNIEMAIMPLBINU32 iemAImpl_blsi_u32, iemAImpl_blsi_u32_fallback;
FNIEMAIMPLBINU32 iemAImpl_blsr_u32, iemAImpl_blsr_u32_fallback;
FNIEMAIMPLBINU32 iemAImpl_blsmsk_u32, iemAImpl_blsmsk_u32_fallback;
/** @}  */

/** @name Arithmetic assignment operations on quad words (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINU64, (uint32_t fEFlagsIn, uint64_t *pu64Dst, uint64_t u64Src));
typedef FNIEMAIMPLBINU64 *PFNIEMAIMPLBINU64;
FNIEMAIMPLBINU64 iemAImpl_add_u64, iemAImpl_add_u64_locked;
FNIEMAIMPLBINU64 iemAImpl_adc_u64, iemAImpl_adc_u64_locked;
FNIEMAIMPLBINU64 iemAImpl_sub_u64, iemAImpl_sub_u64_locked;
FNIEMAIMPLBINU64 iemAImpl_sbb_u64, iemAImpl_sbb_u64_locked;
FNIEMAIMPLBINU64  iemAImpl_or_u64,  iemAImpl_or_u64_locked;
FNIEMAIMPLBINU64 iemAImpl_xor_u64, iemAImpl_xor_u64_locked;
FNIEMAIMPLBINU64 iemAImpl_and_u64, iemAImpl_and_u64_locked;
FNIEMAIMPLBINU64 iemAImpl_blsi_u64, iemAImpl_blsi_u64_fallback;
FNIEMAIMPLBINU64 iemAImpl_blsr_u64, iemAImpl_blsr_u64_fallback;
FNIEMAIMPLBINU64 iemAImpl_blsmsk_u64, iemAImpl_blsmsk_u64_fallback;
/** @}  */

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINROU8, (uint32_t fEFlagsIn, uint8_t const *pu8Dst, uint8_t u8Src));
typedef FNIEMAIMPLBINROU8 *PFNIEMAIMPLBINROU8;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINROU16,(uint32_t fEFlagsIn, uint16_t const *pu16Dst, uint16_t u16Src));
typedef FNIEMAIMPLBINROU16 *PFNIEMAIMPLBINROU16;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINROU32,(uint32_t fEFlagsIn, uint32_t const *pu32Dst, uint32_t u32Src));
typedef FNIEMAIMPLBINROU32 *PFNIEMAIMPLBINROU32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLBINROU64,(uint32_t fEFlagsIn, uint64_t const *pu64Dst, uint64_t u64Src));
typedef FNIEMAIMPLBINROU64 *PFNIEMAIMPLBINROU64;

/** @name Compare operations (thrown in with the binary ops).
 * @{ */
FNIEMAIMPLBINROU8  iemAImpl_cmp_u8;
FNIEMAIMPLBINROU16 iemAImpl_cmp_u16;
FNIEMAIMPLBINROU32 iemAImpl_cmp_u32;
FNIEMAIMPLBINROU64 iemAImpl_cmp_u64;
/** @}  */

/** @name Test operations (thrown in with the binary ops).
 * @{ */
FNIEMAIMPLBINROU8  iemAImpl_test_u8;
FNIEMAIMPLBINROU16 iemAImpl_test_u16;
FNIEMAIMPLBINROU32 iemAImpl_test_u32;
FNIEMAIMPLBINROU64 iemAImpl_test_u64;
/** @}  */

/** @name Bit operations operations (thrown in with the binary ops).
 * @{ */
FNIEMAIMPLBINROU16 iemAImpl_bt_u16;
FNIEMAIMPLBINROU32 iemAImpl_bt_u32;
FNIEMAIMPLBINROU64 iemAImpl_bt_u64;
FNIEMAIMPLBINU16 iemAImpl_btc_u16, iemAImpl_btc_u16_locked;
FNIEMAIMPLBINU32 iemAImpl_btc_u32, iemAImpl_btc_u32_locked;
FNIEMAIMPLBINU64 iemAImpl_btc_u64, iemAImpl_btc_u64_locked;
FNIEMAIMPLBINU16 iemAImpl_btr_u16, iemAImpl_btr_u16_locked;
FNIEMAIMPLBINU32 iemAImpl_btr_u32, iemAImpl_btr_u32_locked;
FNIEMAIMPLBINU64 iemAImpl_btr_u64, iemAImpl_btr_u64_locked;
FNIEMAIMPLBINU16 iemAImpl_bts_u16, iemAImpl_bts_u16_locked;
FNIEMAIMPLBINU32 iemAImpl_bts_u32, iemAImpl_bts_u32_locked;
FNIEMAIMPLBINU64 iemAImpl_bts_u64, iemAImpl_bts_u64_locked;
/** @}  */

/** @name Arithmetic three operand operations on double words (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLBINVEXU32, (uint32_t *pu32Dst, uint32_t u32Src1, uint32_t u32Src2, uint32_t *pEFlags));
typedef FNIEMAIMPLBINVEXU32 *PFNIEMAIMPLBINVEXU32;
FNIEMAIMPLBINVEXU32 iemAImpl_andn_u32, iemAImpl_andn_u32_fallback;
FNIEMAIMPLBINVEXU32 iemAImpl_bextr_u32, iemAImpl_bextr_u32_fallback;
FNIEMAIMPLBINVEXU32 iemAImpl_bzhi_u32, iemAImpl_bzhi_u32_fallback;
/** @}  */

/** @name Arithmetic three operand operations on quad words (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLBINVEXU64, (uint64_t *pu64Dst, uint64_t u64Src1, uint64_t u64Src2, uint32_t *pEFlags));
typedef FNIEMAIMPLBINVEXU64 *PFNIEMAIMPLBINVEXU64;
FNIEMAIMPLBINVEXU64 iemAImpl_andn_u64, iemAImpl_andn_u64_fallback;
FNIEMAIMPLBINVEXU64 iemAImpl_bextr_u64, iemAImpl_bextr_u64_fallback;
FNIEMAIMPLBINVEXU64 iemAImpl_bzhi_u64, iemAImpl_bzhi_u64_fallback;
/** @}  */

/** @name Arithmetic three operand operations on double words w/o EFLAGS (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLBINVEXU32NOEFL, (uint32_t *pu32Dst, uint32_t u32Src1, uint32_t u32Src2));
typedef FNIEMAIMPLBINVEXU32NOEFL *PFNIEMAIMPLBINVEXU32NOEFL;
FNIEMAIMPLBINVEXU32NOEFL iemAImpl_pdep_u32, iemAImpl_pdep_u32_fallback;
FNIEMAIMPLBINVEXU32NOEFL iemAImpl_pext_u32, iemAImpl_pext_u32_fallback;
FNIEMAIMPLBINVEXU32NOEFL iemAImpl_sarx_u32, iemAImpl_sarx_u32_fallback;
FNIEMAIMPLBINVEXU32NOEFL iemAImpl_shlx_u32, iemAImpl_shlx_u32_fallback;
FNIEMAIMPLBINVEXU32NOEFL iemAImpl_shrx_u32, iemAImpl_shrx_u32_fallback;
FNIEMAIMPLBINVEXU32NOEFL iemAImpl_rorx_u32;
/** @}  */

/** @name Arithmetic three operand operations on quad words w/o EFLAGS (binary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLBINVEXU64NOEFL, (uint64_t *pu64Dst, uint64_t u64Src1, uint64_t u64Src2));
typedef FNIEMAIMPLBINVEXU64NOEFL *PFNIEMAIMPLBINVEXU64NOEFL;
FNIEMAIMPLBINVEXU64NOEFL iemAImpl_pdep_u64, iemAImpl_pdep_u64_fallback;
FNIEMAIMPLBINVEXU64NOEFL iemAImpl_pext_u64, iemAImpl_pext_u64_fallback;
FNIEMAIMPLBINVEXU64NOEFL iemAImpl_sarx_u64, iemAImpl_sarx_u64_fallback;
FNIEMAIMPLBINVEXU64NOEFL iemAImpl_shlx_u64, iemAImpl_shlx_u64_fallback;
FNIEMAIMPLBINVEXU64NOEFL iemAImpl_shrx_u64, iemAImpl_shrx_u64_fallback;
FNIEMAIMPLBINVEXU64NOEFL iemAImpl_rorx_u64;
/** @}  */

/** @name MULX 32-bit and 64-bit.
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMULXVEXU32, (uint32_t *puDst1, uint32_t *puDst2, uint32_t uSrc1, uint32_t uSrc2));
typedef FNIEMAIMPLMULXVEXU32 *PFNIEMAIMPLMULXVEXU32;
FNIEMAIMPLMULXVEXU32 iemAImpl_mulx_u32, iemAImpl_mulx_u32_fallback;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMULXVEXU64, (uint64_t *puDst1, uint64_t *puDst2, uint64_t uSrc1, uint64_t uSrc2));
typedef FNIEMAIMPLMULXVEXU64 *PFNIEMAIMPLMULXVEXU64;
FNIEMAIMPLMULXVEXU64 iemAImpl_mulx_u64, iemAImpl_mulx_u64_fallback;
/** @}  */


/** @name Exchange memory with register operations.
 * @{ */
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u8_locked, (uint8_t  *pu8Mem,  uint8_t  *pu8Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u16_locked,(uint16_t *pu16Mem, uint16_t *pu16Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u32_locked,(uint32_t *pu32Mem, uint32_t *pu32Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u64_locked,(uint64_t *pu64Mem, uint64_t *pu64Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u8_unlocked, (uint8_t  *pu8Mem,  uint8_t  *pu8Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u16_unlocked,(uint16_t *pu16Mem, uint16_t *pu16Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u32_unlocked,(uint32_t *pu32Mem, uint32_t *pu32Reg));
IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u64_unlocked,(uint64_t *pu64Mem, uint64_t *pu64Reg));
/** @}  */

/** @name Exchange and add operations.
 * @{ */
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u8, (uint8_t  *pu8Dst,  uint8_t  *pu8Reg,  uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u16,(uint16_t *pu16Dst, uint16_t *pu16Reg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u32,(uint32_t *pu32Dst, uint32_t *pu32Reg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u64,(uint64_t *pu64Dst, uint64_t *pu64Reg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u8_locked, (uint8_t  *pu8Dst,  uint8_t  *pu8Reg,  uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u16_locked,(uint16_t *pu16Dst, uint16_t *pu16Reg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u32_locked,(uint32_t *pu32Dst, uint32_t *pu32Reg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u64_locked,(uint64_t *pu64Dst, uint64_t *pu64Reg, uint32_t *pEFlags));
/** @}  */

/** @name Compare and exchange.
 * @{ */
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u8,        (uint8_t  *pu8Dst,  uint8_t  *puAl,  uint8_t  uSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u8_locked, (uint8_t  *pu8Dst,  uint8_t  *puAl,  uint8_t  uSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u16,       (uint16_t *pu16Dst, uint16_t *puAx,  uint16_t uSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u16_locked,(uint16_t *pu16Dst, uint16_t *puAx,  uint16_t uSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u32,       (uint32_t *pu32Dst, uint32_t *puEax, uint32_t uSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u32_locked,(uint32_t *pu32Dst, uint32_t *puEax, uint32_t uSrcReg, uint32_t *pEFlags));
#if ARCH_BITS == 32
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64,       (uint64_t *pu64Dst, uint64_t *puRax, uint64_t *puSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64_locked,(uint64_t *pu64Dst, uint64_t *puRax, uint64_t *puSrcReg, uint32_t *pEFlags));
#else
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64,       (uint64_t *pu64Dst, uint64_t *puRax, uint64_t uSrcReg, uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64_locked,(uint64_t *pu64Dst, uint64_t *puRax, uint64_t uSrcReg, uint32_t *pEFlags));
#endif
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg8b,(uint64_t *pu64Dst, PRTUINT64U pu64EaxEdx, PRTUINT64U pu64EbxEcx,
                                            uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg8b_locked,(uint64_t *pu64Dst, PRTUINT64U pu64EaxEdx, PRTUINT64U pu64EbxEcx,
                                                   uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b,(PRTUINT128U pu128Dst, PRTUINT128U pu128RaxRdx, PRTUINT128U pu128RbxRcx,
                                             uint32_t *pEFlags));
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b_locked,(PRTUINT128U pu128Dst, PRTUINT128U pu128RaxRdx, PRTUINT128U pu128RbxRcx,
                                                    uint32_t *pEFlags));
#ifndef RT_ARCH_ARM64
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b_fallback,(PRTUINT128U pu128Dst, PRTUINT128U pu128RaxRdx,
                                                      PRTUINT128U pu128RbxRcx, uint32_t *pEFlags));
#endif
/** @} */

/** @name Memory ordering
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEMFENCE,(void));
typedef FNIEMAIMPLMEMFENCE *PFNIEMAIMPLMEMFENCE;
IEM_DECL_IMPL_DEF(void, iemAImpl_mfence,(void));
IEM_DECL_IMPL_DEF(void, iemAImpl_sfence,(void));
IEM_DECL_IMPL_DEF(void, iemAImpl_lfence,(void));
#ifndef RT_ARCH_ARM64
IEM_DECL_IMPL_DEF(void, iemAImpl_alt_mem_fence,(void));
#endif
/** @} */

/** @name Double precision shifts
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLSHIFTDBLU16,(uint16_t *pu16Dst, uint16_t u16Src, uint8_t cShift, uint32_t *pEFlags));
typedef FNIEMAIMPLSHIFTDBLU16  *PFNIEMAIMPLSHIFTDBLU16;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLSHIFTDBLU32,(uint32_t *pu32Dst, uint32_t u32Src, uint8_t cShift, uint32_t *pEFlags));
typedef FNIEMAIMPLSHIFTDBLU32  *PFNIEMAIMPLSHIFTDBLU32;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLSHIFTDBLU64,(uint64_t *pu64Dst, uint64_t u64Src, uint8_t cShift, uint32_t *pEFlags));
typedef FNIEMAIMPLSHIFTDBLU64  *PFNIEMAIMPLSHIFTDBLU64;
FNIEMAIMPLSHIFTDBLU16 iemAImpl_shld_u16, iemAImpl_shld_u16_amd, iemAImpl_shld_u16_intel;
FNIEMAIMPLSHIFTDBLU32 iemAImpl_shld_u32, iemAImpl_shld_u32_amd, iemAImpl_shld_u32_intel;
FNIEMAIMPLSHIFTDBLU64 iemAImpl_shld_u64, iemAImpl_shld_u64_amd, iemAImpl_shld_u64_intel;
FNIEMAIMPLSHIFTDBLU16 iemAImpl_shrd_u16, iemAImpl_shrd_u16_amd, iemAImpl_shrd_u16_intel;
FNIEMAIMPLSHIFTDBLU32 iemAImpl_shrd_u32, iemAImpl_shrd_u32_amd, iemAImpl_shrd_u32_intel;
FNIEMAIMPLSHIFTDBLU64 iemAImpl_shrd_u64, iemAImpl_shrd_u64_amd, iemAImpl_shrd_u64_intel;
/** @}  */


/** @name Bit search operations (thrown in with the binary ops).
 * @{ */
FNIEMAIMPLBINU16 iemAImpl_bsf_u16, iemAImpl_bsf_u16_amd, iemAImpl_bsf_u16_intel;
FNIEMAIMPLBINU32 iemAImpl_bsf_u32, iemAImpl_bsf_u32_amd, iemAImpl_bsf_u32_intel;
FNIEMAIMPLBINU64 iemAImpl_bsf_u64, iemAImpl_bsf_u64_amd, iemAImpl_bsf_u64_intel;
FNIEMAIMPLBINU16 iemAImpl_bsr_u16, iemAImpl_bsr_u16_amd, iemAImpl_bsr_u16_intel;
FNIEMAIMPLBINU32 iemAImpl_bsr_u32, iemAImpl_bsr_u32_amd, iemAImpl_bsr_u32_intel;
FNIEMAIMPLBINU64 iemAImpl_bsr_u64, iemAImpl_bsr_u64_amd, iemAImpl_bsr_u64_intel;
FNIEMAIMPLBINU16 iemAImpl_lzcnt_u16, iemAImpl_lzcnt_u16_amd, iemAImpl_lzcnt_u16_intel;
FNIEMAIMPLBINU32 iemAImpl_lzcnt_u32, iemAImpl_lzcnt_u32_amd, iemAImpl_lzcnt_u32_intel;
FNIEMAIMPLBINU64 iemAImpl_lzcnt_u64, iemAImpl_lzcnt_u64_amd, iemAImpl_lzcnt_u64_intel;
FNIEMAIMPLBINU16 iemAImpl_tzcnt_u16, iemAImpl_tzcnt_u16_amd, iemAImpl_tzcnt_u16_intel;
FNIEMAIMPLBINU32 iemAImpl_tzcnt_u32, iemAImpl_tzcnt_u32_amd, iemAImpl_tzcnt_u32_intel;
FNIEMAIMPLBINU64 iemAImpl_tzcnt_u64, iemAImpl_tzcnt_u64_amd, iemAImpl_tzcnt_u64_intel;
FNIEMAIMPLBINU16 iemAImpl_popcnt_u16, iemAImpl_popcnt_u16_fallback;
FNIEMAIMPLBINU32 iemAImpl_popcnt_u32, iemAImpl_popcnt_u32_fallback;
FNIEMAIMPLBINU64 iemAImpl_popcnt_u64, iemAImpl_popcnt_u64_fallback;
/** @}  */

/** @name Signed multiplication operations (thrown in with the binary ops).
 * @{ */
FNIEMAIMPLBINU16 iemAImpl_imul_two_u16, iemAImpl_imul_two_u16_amd, iemAImpl_imul_two_u16_intel;
FNIEMAIMPLBINU32 iemAImpl_imul_two_u32, iemAImpl_imul_two_u32_amd, iemAImpl_imul_two_u32_intel;
FNIEMAIMPLBINU64 iemAImpl_imul_two_u64, iemAImpl_imul_two_u64_amd, iemAImpl_imul_two_u64_intel;
/** @}  */

/** @name Arithmetic assignment operations on bytes (unary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLUNARYU8,  (uint8_t  *pu8Dst,  uint32_t *pEFlags));
typedef FNIEMAIMPLUNARYU8  *PFNIEMAIMPLUNARYU8;
FNIEMAIMPLUNARYU8 iemAImpl_inc_u8, iemAImpl_inc_u8_locked;
FNIEMAIMPLUNARYU8 iemAImpl_dec_u8, iemAImpl_dec_u8_locked;
FNIEMAIMPLUNARYU8 iemAImpl_not_u8, iemAImpl_not_u8_locked;
FNIEMAIMPLUNARYU8 iemAImpl_neg_u8, iemAImpl_neg_u8_locked;
/** @} */

/** @name Arithmetic assignment operations on words (unary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLUNARYU16,  (uint16_t  *pu16Dst,  uint32_t *pEFlags));
typedef FNIEMAIMPLUNARYU16  *PFNIEMAIMPLUNARYU16;
FNIEMAIMPLUNARYU16 iemAImpl_inc_u16, iemAImpl_inc_u16_locked;
FNIEMAIMPLUNARYU16 iemAImpl_dec_u16, iemAImpl_dec_u16_locked;
FNIEMAIMPLUNARYU16 iemAImpl_not_u16, iemAImpl_not_u16_locked;
FNIEMAIMPLUNARYU16 iemAImpl_neg_u16, iemAImpl_neg_u16_locked;
/** @} */

/** @name Arithmetic assignment operations on double words (unary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLUNARYU32,  (uint32_t  *pu32Dst,  uint32_t *pEFlags));
typedef FNIEMAIMPLUNARYU32  *PFNIEMAIMPLUNARYU32;
FNIEMAIMPLUNARYU32 iemAImpl_inc_u32, iemAImpl_inc_u32_locked;
FNIEMAIMPLUNARYU32 iemAImpl_dec_u32, iemAImpl_dec_u32_locked;
FNIEMAIMPLUNARYU32 iemAImpl_not_u32, iemAImpl_not_u32_locked;
FNIEMAIMPLUNARYU32 iemAImpl_neg_u32, iemAImpl_neg_u32_locked;
/** @} */

/** @name Arithmetic assignment operations on quad words (unary).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLUNARYU64,  (uint64_t  *pu64Dst,  uint32_t *pEFlags));
typedef FNIEMAIMPLUNARYU64  *PFNIEMAIMPLUNARYU64;
FNIEMAIMPLUNARYU64 iemAImpl_inc_u64, iemAImpl_inc_u64_locked;
FNIEMAIMPLUNARYU64 iemAImpl_dec_u64, iemAImpl_dec_u64_locked;
FNIEMAIMPLUNARYU64 iemAImpl_not_u64, iemAImpl_not_u64_locked;
FNIEMAIMPLUNARYU64 iemAImpl_neg_u64, iemAImpl_neg_u64_locked;
/** @} */


/** @name Shift operations on bytes (Group 2).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSHIFTU8,(uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift));
typedef FNIEMAIMPLSHIFTU8  *PFNIEMAIMPLSHIFTU8;
FNIEMAIMPLSHIFTU8 iemAImpl_rol_u8, iemAImpl_rol_u8_amd, iemAImpl_rol_u8_intel;
FNIEMAIMPLSHIFTU8 iemAImpl_ror_u8, iemAImpl_ror_u8_amd, iemAImpl_ror_u8_intel;
FNIEMAIMPLSHIFTU8 iemAImpl_rcl_u8, iemAImpl_rcl_u8_amd, iemAImpl_rcl_u8_intel;
FNIEMAIMPLSHIFTU8 iemAImpl_rcr_u8, iemAImpl_rcr_u8_amd, iemAImpl_rcr_u8_intel;
FNIEMAIMPLSHIFTU8 iemAImpl_shl_u8, iemAImpl_shl_u8_amd, iemAImpl_shl_u8_intel;
FNIEMAIMPLSHIFTU8 iemAImpl_shr_u8, iemAImpl_shr_u8_amd, iemAImpl_shr_u8_intel;
FNIEMAIMPLSHIFTU8 iemAImpl_sar_u8, iemAImpl_sar_u8_amd, iemAImpl_sar_u8_intel;
/** @} */

/** @name Shift operations on words (Group 2).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSHIFTU16,(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift));
typedef FNIEMAIMPLSHIFTU16  *PFNIEMAIMPLSHIFTU16;
FNIEMAIMPLSHIFTU16 iemAImpl_rol_u16, iemAImpl_rol_u16_amd, iemAImpl_rol_u16_intel;
FNIEMAIMPLSHIFTU16 iemAImpl_ror_u16, iemAImpl_ror_u16_amd, iemAImpl_ror_u16_intel;
FNIEMAIMPLSHIFTU16 iemAImpl_rcl_u16, iemAImpl_rcl_u16_amd, iemAImpl_rcl_u16_intel;
FNIEMAIMPLSHIFTU16 iemAImpl_rcr_u16, iemAImpl_rcr_u16_amd, iemAImpl_rcr_u16_intel;
FNIEMAIMPLSHIFTU16 iemAImpl_shl_u16, iemAImpl_shl_u16_amd, iemAImpl_shl_u16_intel;
FNIEMAIMPLSHIFTU16 iemAImpl_shr_u16, iemAImpl_shr_u16_amd, iemAImpl_shr_u16_intel;
FNIEMAIMPLSHIFTU16 iemAImpl_sar_u16, iemAImpl_sar_u16_amd, iemAImpl_sar_u16_intel;
/** @} */

/** @name Shift operations on double words (Group 2).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSHIFTU32,(uint32_t fEFlagsIn, uint32_t *pu32Dst, uint8_t cShift));
typedef FNIEMAIMPLSHIFTU32  *PFNIEMAIMPLSHIFTU32;
FNIEMAIMPLSHIFTU32 iemAImpl_rol_u32, iemAImpl_rol_u32_amd, iemAImpl_rol_u32_intel;
FNIEMAIMPLSHIFTU32 iemAImpl_ror_u32, iemAImpl_ror_u32_amd, iemAImpl_ror_u32_intel;
FNIEMAIMPLSHIFTU32 iemAImpl_rcl_u32, iemAImpl_rcl_u32_amd, iemAImpl_rcl_u32_intel;
FNIEMAIMPLSHIFTU32 iemAImpl_rcr_u32, iemAImpl_rcr_u32_amd, iemAImpl_rcr_u32_intel;
FNIEMAIMPLSHIFTU32 iemAImpl_shl_u32, iemAImpl_shl_u32_amd, iemAImpl_shl_u32_intel;
FNIEMAIMPLSHIFTU32 iemAImpl_shr_u32, iemAImpl_shr_u32_amd, iemAImpl_shr_u32_intel;
FNIEMAIMPLSHIFTU32 iemAImpl_sar_u32, iemAImpl_sar_u32_amd, iemAImpl_sar_u32_intel;
/** @} */

/** @name Shift operations on words (Group 2).
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSHIFTU64,(uint32_t fEFlagsIn, uint64_t *pu64Dst, uint8_t cShift));
typedef FNIEMAIMPLSHIFTU64  *PFNIEMAIMPLSHIFTU64;
FNIEMAIMPLSHIFTU64 iemAImpl_rol_u64, iemAImpl_rol_u64_amd, iemAImpl_rol_u64_intel;
FNIEMAIMPLSHIFTU64 iemAImpl_ror_u64, iemAImpl_ror_u64_amd, iemAImpl_ror_u64_intel;
FNIEMAIMPLSHIFTU64 iemAImpl_rcl_u64, iemAImpl_rcl_u64_amd, iemAImpl_rcl_u64_intel;
FNIEMAIMPLSHIFTU64 iemAImpl_rcr_u64, iemAImpl_rcr_u64_amd, iemAImpl_rcr_u64_intel;
FNIEMAIMPLSHIFTU64 iemAImpl_shl_u64, iemAImpl_shl_u64_amd, iemAImpl_shl_u64_intel;
FNIEMAIMPLSHIFTU64 iemAImpl_shr_u64, iemAImpl_shr_u64_amd, iemAImpl_shr_u64_intel;
FNIEMAIMPLSHIFTU64 iemAImpl_sar_u64, iemAImpl_sar_u64_amd, iemAImpl_sar_u64_intel;
/** @} */

/** @name Multiplication and division operations.
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMULDIVU8,(uint16_t *pu16AX, uint8_t u8FactorDivisor, uint32_t fEFlags));
typedef FNIEMAIMPLMULDIVU8  *PFNIEMAIMPLMULDIVU8;
FNIEMAIMPLMULDIVU8 iemAImpl_mul_u8,  iemAImpl_mul_u8_amd,  iemAImpl_mul_u8_intel;
FNIEMAIMPLMULDIVU8 iemAImpl_imul_u8, iemAImpl_imul_u8_amd, iemAImpl_imul_u8_intel;
FNIEMAIMPLMULDIVU8 iemAImpl_div_u8,  iemAImpl_div_u8_amd,  iemAImpl_div_u8_intel;
FNIEMAIMPLMULDIVU8 iemAImpl_idiv_u8, iemAImpl_idiv_u8_amd, iemAImpl_idiv_u8_intel;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMULDIVU16,(uint16_t *pu16AX, uint16_t *pu16DX, uint16_t u16FactorDivisor, uint32_t fEFlags));
typedef FNIEMAIMPLMULDIVU16  *PFNIEMAIMPLMULDIVU16;
FNIEMAIMPLMULDIVU16 iemAImpl_mul_u16,  iemAImpl_mul_u16_amd,  iemAImpl_mul_u16_intel;
FNIEMAIMPLMULDIVU16 iemAImpl_imul_u16, iemAImpl_imul_u16_amd, iemAImpl_imul_u16_intel;
FNIEMAIMPLMULDIVU16 iemAImpl_div_u16,  iemAImpl_div_u16_amd,  iemAImpl_div_u16_intel;
FNIEMAIMPLMULDIVU16 iemAImpl_idiv_u16, iemAImpl_idiv_u16_amd, iemAImpl_idiv_u16_intel;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMULDIVU32,(uint32_t *pu32EAX, uint32_t *pu32EDX, uint32_t u32FactorDivisor, uint32_t fEFlags));
typedef FNIEMAIMPLMULDIVU32  *PFNIEMAIMPLMULDIVU32;
FNIEMAIMPLMULDIVU32 iemAImpl_mul_u32,  iemAImpl_mul_u32_amd,  iemAImpl_mul_u32_intel;
FNIEMAIMPLMULDIVU32 iemAImpl_imul_u32, iemAImpl_imul_u32_amd, iemAImpl_imul_u32_intel;
FNIEMAIMPLMULDIVU32 iemAImpl_div_u32,  iemAImpl_div_u32_amd,  iemAImpl_div_u32_intel;
FNIEMAIMPLMULDIVU32 iemAImpl_idiv_u32, iemAImpl_idiv_u32_amd, iemAImpl_idiv_u32_intel;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMULDIVU64,(uint64_t *pu64RAX, uint64_t *pu64RDX, uint64_t u64FactorDivisor, uint32_t fEFlags));
typedef FNIEMAIMPLMULDIVU64  *PFNIEMAIMPLMULDIVU64;
FNIEMAIMPLMULDIVU64 iemAImpl_mul_u64,  iemAImpl_mul_u64_amd,  iemAImpl_mul_u64_intel;
FNIEMAIMPLMULDIVU64 iemAImpl_imul_u64, iemAImpl_imul_u64_amd, iemAImpl_imul_u64_intel;
FNIEMAIMPLMULDIVU64 iemAImpl_div_u64,  iemAImpl_div_u64_amd,  iemAImpl_div_u64_intel;
FNIEMAIMPLMULDIVU64 iemAImpl_idiv_u64, iemAImpl_idiv_u64_amd, iemAImpl_idiv_u64_intel;
/** @} */

/** @name Byte Swap.
 * @{  */
IEM_DECL_IMPL_TYPE(void, iemAImpl_bswap_u16,(uint32_t *pu32Dst)); /* Yes, 32-bit register access. */
IEM_DECL_IMPL_TYPE(void, iemAImpl_bswap_u32,(uint32_t *pu32Dst));
IEM_DECL_IMPL_TYPE(void, iemAImpl_bswap_u64,(uint64_t *pu64Dst));
/** @}  */

/** @name Misc.
 * @{ */
FNIEMAIMPLBINU16 iemAImpl_arpl;
/** @} */

/** @name RDRAND and RDSEED
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLRDRANDSEEDU16,(uint16_t *puDst, uint32_t *pEFlags));
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLRDRANDSEEDU32,(uint32_t *puDst, uint32_t *pEFlags));
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLRDRANDSEEDU64,(uint64_t *puDst, uint32_t *pEFlags));
typedef FNIEMAIMPLRDRANDSEEDU16  *PFNIEMAIMPLRDRANDSEEDU16;
typedef FNIEMAIMPLRDRANDSEEDU32  *PFNIEMAIMPLRDRANDSEEDU32;
typedef FNIEMAIMPLRDRANDSEEDU64  *PFNIEMAIMPLRDRANDSEEDU64;

FNIEMAIMPLRDRANDSEEDU16 iemAImpl_rdrand_u16, iemAImpl_rdrand_u16_fallback;
FNIEMAIMPLRDRANDSEEDU32 iemAImpl_rdrand_u32, iemAImpl_rdrand_u32_fallback;
FNIEMAIMPLRDRANDSEEDU64 iemAImpl_rdrand_u64, iemAImpl_rdrand_u64_fallback;
FNIEMAIMPLRDRANDSEEDU16 iemAImpl_rdseed_u16, iemAImpl_rdseed_u16_fallback;
FNIEMAIMPLRDRANDSEEDU32 iemAImpl_rdseed_u32, iemAImpl_rdseed_u32_fallback;
FNIEMAIMPLRDRANDSEEDU64 iemAImpl_rdseed_u64, iemAImpl_rdseed_u64_fallback;
/** @} */

/** @name ADOX and ADCX
 * @{ */
FNIEMAIMPLBINU32 iemAImpl_adcx_u32, iemAImpl_adcx_u32_fallback;
FNIEMAIMPLBINU64 iemAImpl_adcx_u64, iemAImpl_adcx_u64_fallback;
FNIEMAIMPLBINU32 iemAImpl_adox_u32, iemAImpl_adox_u32_fallback;
FNIEMAIMPLBINU64 iemAImpl_adox_u64, iemAImpl_adox_u64_fallback;
/** @} */

/** @name FPU operations taking a 32-bit float argument
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR32FSW,(PCX86FXSTATE pFpuState, uint16_t *pFSW,
                                                      PCRTFLOAT80U pr80Val1, PCRTFLOAT32U pr32Val2));
typedef FNIEMAIMPLFPUR32FSW *PFNIEMAIMPLFPUR32FSW;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR32,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT32U pr32Val2));
typedef FNIEMAIMPLFPUR32    *PFNIEMAIMPLFPUR32;

FNIEMAIMPLFPUR32FSW iemAImpl_fcom_r80_by_r32;
FNIEMAIMPLFPUR32    iemAImpl_fadd_r80_by_r32;
FNIEMAIMPLFPUR32    iemAImpl_fmul_r80_by_r32;
FNIEMAIMPLFPUR32    iemAImpl_fsub_r80_by_r32;
FNIEMAIMPLFPUR32    iemAImpl_fsubr_r80_by_r32;
FNIEMAIMPLFPUR32    iemAImpl_fdiv_r80_by_r32;
FNIEMAIMPLFPUR32    iemAImpl_fdivr_r80_by_r32;

IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_r32,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT32U pr32Val));
IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_r32,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTFLOAT32U pr32Val, PCRTFLOAT80U pr80Val));
/** @} */

/** @name FPU operations taking a 64-bit float argument
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR64FSW,(PCX86FXSTATE pFpuState, uint16_t *pFSW,
                                                      PCRTFLOAT80U pr80Val1, PCRTFLOAT64U pr64Val2));
typedef FNIEMAIMPLFPUR64FSW *PFNIEMAIMPLFPUR64FSW;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR64,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT64U pr64Val2));
typedef FNIEMAIMPLFPUR64   *PFNIEMAIMPLFPUR64;

FNIEMAIMPLFPUR64FSW iemAImpl_fcom_r80_by_r64;
FNIEMAIMPLFPUR64    iemAImpl_fadd_r80_by_r64;
FNIEMAIMPLFPUR64    iemAImpl_fmul_r80_by_r64;
FNIEMAIMPLFPUR64    iemAImpl_fsub_r80_by_r64;
FNIEMAIMPLFPUR64    iemAImpl_fsubr_r80_by_r64;
FNIEMAIMPLFPUR64    iemAImpl_fdiv_r80_by_r64;
FNIEMAIMPLFPUR64    iemAImpl_fdivr_r80_by_r64;

IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_r64,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT64U pr64Val));
IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_r64,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTFLOAT64U pr32Val, PCRTFLOAT80U pr80Val));
/** @} */

/** @name FPU operations taking a 80-bit float argument
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2));
typedef FNIEMAIMPLFPUR80    *PFNIEMAIMPLFPUR80;
FNIEMAIMPLFPUR80            iemAImpl_fadd_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fmul_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fsub_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fsubr_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fdiv_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fdivr_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fprem_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fprem1_r80_by_r80;
FNIEMAIMPLFPUR80            iemAImpl_fscale_r80_by_r80;

FNIEMAIMPLFPUR80            iemAImpl_fpatan_r80_by_r80,  iemAImpl_fpatan_r80_by_r80_amd,  iemAImpl_fpatan_r80_by_r80_intel;
FNIEMAIMPLFPUR80            iemAImpl_fyl2x_r80_by_r80,   iemAImpl_fyl2x_r80_by_r80_amd,   iemAImpl_fyl2x_r80_by_r80_intel;
FNIEMAIMPLFPUR80            iemAImpl_fyl2xp1_r80_by_r80, iemAImpl_fyl2xp1_r80_by_r80_amd, iemAImpl_fyl2xp1_r80_by_r80_intel;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR80FSW,(PCX86FXSTATE pFpuState, uint16_t *pFSW,
                                                      PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2));
typedef FNIEMAIMPLFPUR80FSW *PFNIEMAIMPLFPUR80FSW;
FNIEMAIMPLFPUR80FSW         iemAImpl_fcom_r80_by_r80;
FNIEMAIMPLFPUR80FSW         iemAImpl_fucom_r80_by_r80;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPUR80EFL,(PCX86FXSTATE pFpuState, uint16_t *pu16Fsw,
                                                          PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2));
typedef FNIEMAIMPLFPUR80EFL *PFNIEMAIMPLFPUR80EFL;
FNIEMAIMPLFPUR80EFL         iemAImpl_fcomi_r80_by_r80;
FNIEMAIMPLFPUR80EFL         iemAImpl_fucomi_r80_by_r80;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR80UNARY,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val));
typedef FNIEMAIMPLFPUR80UNARY *PFNIEMAIMPLFPUR80UNARY;
FNIEMAIMPLFPUR80UNARY       iemAImpl_fabs_r80;
FNIEMAIMPLFPUR80UNARY       iemAImpl_fchs_r80;
FNIEMAIMPLFPUR80UNARY       iemAImpl_f2xm1_r80, iemAImpl_f2xm1_r80_amd, iemAImpl_f2xm1_r80_intel;
FNIEMAIMPLFPUR80UNARY       iemAImpl_fsqrt_r80;
FNIEMAIMPLFPUR80UNARY       iemAImpl_frndint_r80;
FNIEMAIMPLFPUR80UNARY       iemAImpl_fsin_r80, iemAImpl_fsin_r80_amd, iemAImpl_fsin_r80_intel;
FNIEMAIMPLFPUR80UNARY       iemAImpl_fcos_r80, iemAImpl_fcos_r80_amd, iemAImpl_fcos_r80_intel;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR80UNARYFSW,(PCX86FXSTATE pFpuState, uint16_t *pu16Fsw, PCRTFLOAT80U pr80Val));
typedef FNIEMAIMPLFPUR80UNARYFSW *PFNIEMAIMPLFPUR80UNARYFSW;
FNIEMAIMPLFPUR80UNARYFSW    iemAImpl_ftst_r80;
FNIEMAIMPLFPUR80UNARYFSW    iemAImpl_fxam_r80;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR80LDCONST,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes));
typedef FNIEMAIMPLFPUR80LDCONST *PFNIEMAIMPLFPUR80LDCONST;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fld1;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fldl2t;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fldl2e;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fldpi;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fldlg2;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fldln2;
FNIEMAIMPLFPUR80LDCONST     iemAImpl_fldz;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUR80UNARYTWO,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo,
                                                           PCRTFLOAT80U pr80Val));
typedef FNIEMAIMPLFPUR80UNARYTWO *PFNIEMAIMPLFPUR80UNARYTWO;
FNIEMAIMPLFPUR80UNARYTWO    iemAImpl_fptan_r80_r80, iemAImpl_fptan_r80_r80_amd, iemAImpl_fptan_r80_r80_intel;
FNIEMAIMPLFPUR80UNARYTWO    iemAImpl_fxtract_r80_r80;
FNIEMAIMPLFPUR80UNARYTWO    iemAImpl_fsincos_r80_r80, iemAImpl_fsincos_r80_r80_amd, iemAImpl_fsincos_r80_r80_intel;

IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val));
IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_r80,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTFLOAT80U pr80Dst, PCRTFLOAT80U pr80Src));

IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_d80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTPBCD80U pd80Val));
IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_d80,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTPBCD80U pd80Dst, PCRTFLOAT80U pr80Src));

/** @} */

/** @name FPU operations taking a 16-bit signed integer argument
 * @{  */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUI16,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, int16_t const *pi16Val2));
typedef FNIEMAIMPLFPUI16 *PFNIEMAIMPLFPUI16;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUSTR80TOI16,(PCX86FXSTATE pFpuState, uint16_t *pFpuRes,
                                                          int16_t *pi16Dst, PCRTFLOAT80U pr80Src));
typedef FNIEMAIMPLFPUSTR80TOI16 *PFNIEMAIMPLFPUSTR80TOI16;

FNIEMAIMPLFPUI16    iemAImpl_fiadd_r80_by_i16;
FNIEMAIMPLFPUI16    iemAImpl_fimul_r80_by_i16;
FNIEMAIMPLFPUI16    iemAImpl_fisub_r80_by_i16;
FNIEMAIMPLFPUI16    iemAImpl_fisubr_r80_by_i16;
FNIEMAIMPLFPUI16    iemAImpl_fidiv_r80_by_i16;
FNIEMAIMPLFPUI16    iemAImpl_fidivr_r80_by_i16;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUI16FSW,(PCX86FXSTATE pFpuState, uint16_t *pFSW,
                                                      PCRTFLOAT80U pr80Val1, int16_t const *pi16Val2));
typedef FNIEMAIMPLFPUI16FSW *PFNIEMAIMPLFPUI16FSW;
FNIEMAIMPLFPUI16FSW     iemAImpl_ficom_r80_by_i16;

IEM_DECL_IMPL_DEF(void, iemAImpl_fild_r80_from_i16,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, int16_t const *pi16Val));
FNIEMAIMPLFPUSTR80TOI16 iemAImpl_fist_r80_to_i16;
FNIEMAIMPLFPUSTR80TOI16 iemAImpl_fistt_r80_to_i16, iemAImpl_fistt_r80_to_i16_amd, iemAImpl_fistt_r80_to_i16_intel;
/** @}  */

/** @name FPU operations taking a 32-bit signed integer argument
 * @{  */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUI32,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, int32_t const *pi32Val2));
typedef FNIEMAIMPLFPUI32 *PFNIEMAIMPLFPUI32;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUSTR80TOI32,(PCX86FXSTATE pFpuState, uint16_t *pFpuRes,
                                                          int32_t *pi32Dst, PCRTFLOAT80U pr80Src));
typedef FNIEMAIMPLFPUSTR80TOI32 *PFNIEMAIMPLFPUSTR80TOI32;

FNIEMAIMPLFPUI32    iemAImpl_fiadd_r80_by_i32;
FNIEMAIMPLFPUI32    iemAImpl_fimul_r80_by_i32;
FNIEMAIMPLFPUI32    iemAImpl_fisub_r80_by_i32;
FNIEMAIMPLFPUI32    iemAImpl_fisubr_r80_by_i32;
FNIEMAIMPLFPUI32    iemAImpl_fidiv_r80_by_i32;
FNIEMAIMPLFPUI32    iemAImpl_fidivr_r80_by_i32;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUI32FSW,(PCX86FXSTATE pFpuState, uint16_t *pFSW,
                                                      PCRTFLOAT80U pr80Val1, int32_t const *pi32Val2));
typedef FNIEMAIMPLFPUI32FSW *PFNIEMAIMPLFPUI32FSW;
FNIEMAIMPLFPUI32FSW     iemAImpl_ficom_r80_by_i32;

IEM_DECL_IMPL_DEF(void, iemAImpl_fild_r80_from_i32,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, int32_t const *pi32Val));
FNIEMAIMPLFPUSTR80TOI32 iemAImpl_fist_r80_to_i32;
FNIEMAIMPLFPUSTR80TOI32 iemAImpl_fistt_r80_to_i32;
/** @}  */

/** @name FPU operations taking a 64-bit signed integer argument
 * @{  */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLFPUSTR80TOI64,(PCX86FXSTATE pFpuState, uint16_t *pFpuRes,
                                                          int64_t *pi64Dst, PCRTFLOAT80U pr80Src));
typedef FNIEMAIMPLFPUSTR80TOI64 *PFNIEMAIMPLFPUSTR80TOI64;

IEM_DECL_IMPL_DEF(void, iemAImpl_fild_r80_from_i64,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, int64_t const *pi64Val));
FNIEMAIMPLFPUSTR80TOI64 iemAImpl_fist_r80_to_i64;
FNIEMAIMPLFPUSTR80TOI64 iemAImpl_fistt_r80_to_i64;
/** @} */


/** Temporary type representing a 256-bit vector register. */
typedef struct { uint64_t au64[4]; } IEMVMM256;
/** Temporary type pointing to a 256-bit vector register. */
typedef IEMVMM256 *PIEMVMM256;
/** Temporary type pointing to a const 256-bit vector register. */
typedef IEMVMM256 *PCIEMVMM256;


/** @name Media (SSE/MMX/AVX) operations: full1 + full2 -> full1.
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAF2U64,(PCX86FXSTATE pFpuState, uint64_t *puDst, uint64_t const *puSrc));
typedef FNIEMAIMPLMEDIAF2U64   *PFNIEMAIMPLMEDIAF2U64;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF2U128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));
typedef FNIEMAIMPLMEDIAF2U128  *PFNIEMAIMPLMEDIAF2U128;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF2U256,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc));
typedef FNIEMAIMPLMEDIAF2U256  *PFNIEMAIMPLMEDIAF2U256;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF3U128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2));
typedef FNIEMAIMPLMEDIAF3U128  *PFNIEMAIMPLMEDIAF3U128;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF3U256,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2));
typedef FNIEMAIMPLMEDIAF3U256  *PFNIEMAIMPLMEDIAF3U256;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF2U64,(uint64_t *puDst, uint64_t const *puSrc));
typedef FNIEMAIMPLMEDIAOPTF2U64   *PFNIEMAIMPLMEDIAOPTF2U64;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF2U128,(PRTUINT128U puDst, PCRTUINT128U puSrc));
typedef FNIEMAIMPLMEDIAOPTF2U128  *PFNIEMAIMPLMEDIAOPTF2U128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF3U128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2));
typedef FNIEMAIMPLMEDIAOPTF3U128  *PFNIEMAIMPLMEDIAOPTF3U128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF3U256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2));
typedef FNIEMAIMPLMEDIAOPTF3U256  *PFNIEMAIMPLMEDIAOPTF3U256;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF2U256,(PRTUINT256U puDst, PCRTUINT256U puSrc));
typedef FNIEMAIMPLMEDIAOPTF2U256  *PFNIEMAIMPLMEDIAOPTF2U256;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pshufb_u64, iemAImpl_pshufb_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pand_u64, iemAImpl_pandn_u64, iemAImpl_por_u64, iemAImpl_pxor_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pcmpeqb_u64,  iemAImpl_pcmpeqw_u64,  iemAImpl_pcmpeqd_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pcmpgtb_u64,  iemAImpl_pcmpgtw_u64,  iemAImpl_pcmpgtd_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_paddb_u64, iemAImpl_paddsb_u64, iemAImpl_paddusb_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_paddw_u64, iemAImpl_paddsw_u64, iemAImpl_paddusw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_paddd_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_paddq_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psubb_u64, iemAImpl_psubsb_u64, iemAImpl_psubusb_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psubw_u64, iemAImpl_psubsw_u64, iemAImpl_psubusw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psubd_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psubq_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pmaddwd_u64, iemAImpl_pmaddwd_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pmullw_u64, iemAImpl_pmulhw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pminub_u64, iemAImpl_pmaxub_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pminsw_u64, iemAImpl_pmaxsw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pabsb_u64, iemAImpl_pabsb_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pabsw_u64, iemAImpl_pabsw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pabsd_u64, iemAImpl_pabsd_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psignb_u64, iemAImpl_psignb_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psignw_u64, iemAImpl_psignw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psignd_u64, iemAImpl_psignd_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_phaddw_u64, iemAImpl_phaddw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_phaddd_u64, iemAImpl_phaddd_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_phsubw_u64, iemAImpl_phsubw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_phsubd_u64, iemAImpl_phsubd_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_phaddsw_u64, iemAImpl_phaddsw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_phsubsw_u64, iemAImpl_phsubsw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pmaddubsw_u64, iemAImpl_pmaddubsw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pmulhrsw_u64, iemAImpl_pmulhrsw_u64_fallback;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pmuludq_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psllw_u64, iemAImpl_psrlw_u64, iemAImpl_psraw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pslld_u64, iemAImpl_psrld_u64, iemAImpl_psrad_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psllq_u64, iemAImpl_psrlq_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_packsswb_u64, iemAImpl_packuswb_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_packssdw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pmulhuw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_pavgb_u64, iemAImpl_pavgw_u64;
FNIEMAIMPLMEDIAOPTF2U64  iemAImpl_psadbw_u64;

FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pshufb_u128, iemAImpl_pshufb_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pand_u128, iemAImpl_pandn_u128, iemAImpl_por_u128, iemAImpl_pxor_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pcmpeqb_u128, iemAImpl_pcmpeqw_u128, iemAImpl_pcmpeqd_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pcmpeqq_u128, iemAImpl_pcmpeqq_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pcmpgtb_u128, iemAImpl_pcmpgtw_u128, iemAImpl_pcmpgtd_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pcmpgtq_u128, iemAImpl_pcmpgtq_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_paddb_u128, iemAImpl_paddsb_u128, iemAImpl_paddusb_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_paddw_u128, iemAImpl_paddsw_u128, iemAImpl_paddusw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_paddd_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_paddq_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psubb_u128, iemAImpl_psubsb_u128, iemAImpl_psubusb_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psubw_u128, iemAImpl_psubsw_u128, iemAImpl_psubusw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psubd_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psubq_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmullw_u128, iemAImpl_pmullw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmulhw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmulld_u128, iemAImpl_pmulld_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaddwd_u128, iemAImpl_pmaddwd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pminub_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pminud_u128, iemAImpl_pminud_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pminuw_u128, iemAImpl_pminuw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pminsb_u128, iemAImpl_pminsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pminsd_u128, iemAImpl_pminsd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pminsw_u128, iemAImpl_pminsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaxub_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaxud_u128, iemAImpl_pmaxud_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaxuw_u128, iemAImpl_pmaxuw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaxsb_u128, iemAImpl_pmaxsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaxsw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaxsd_u128, iemAImpl_pmaxsd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pabsb_u128, iemAImpl_pabsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pabsw_u128, iemAImpl_pabsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pabsd_u128, iemAImpl_pabsd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psignb_u128, iemAImpl_psignb_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psignw_u128, iemAImpl_psignw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psignd_u128, iemAImpl_psignd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phaddw_u128, iemAImpl_phaddw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phaddd_u128, iemAImpl_phaddd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phsubw_u128, iemAImpl_phsubw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phsubd_u128, iemAImpl_phsubd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phaddsw_u128, iemAImpl_phaddsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phsubsw_u128, iemAImpl_phsubsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaddubsw_u128, iemAImpl_pmaddubsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmulhrsw_u128, iemAImpl_pmulhrsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmuludq_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmaddwd_u128, iemAImpl_pmaddwd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_packsswb_u128, iemAImpl_packuswb_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_packssdw_u128, iemAImpl_packusdw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psllw_u128, iemAImpl_psrlw_u128, iemAImpl_psraw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pslld_u128, iemAImpl_psrld_u128, iemAImpl_psrad_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psllq_u128, iemAImpl_psrlq_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmulhuw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pavgb_u128, iemAImpl_pavgw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_psadbw_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_pmuldq_u128, iemAImpl_pmuldq_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_unpcklps_u128, iemAImpl_unpcklpd_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_unpckhps_u128, iemAImpl_unpckhpd_u128;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_phminposuw_u128, iemAImpl_phminposuw_u128_fallback;

FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpshufb_u128,    iemAImpl_vpshufb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpand_u128,      iemAImpl_vpand_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpandn_u128,     iemAImpl_vpandn_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpor_u128,       iemAImpl_vpor_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpxor_u128,      iemAImpl_vpxor_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpeqb_u128,   iemAImpl_vpcmpeqb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpeqw_u128,   iemAImpl_vpcmpeqw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpeqd_u128,   iemAImpl_vpcmpeqd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpeqq_u128,   iemAImpl_vpcmpeqq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpgtb_u128,   iemAImpl_vpcmpgtb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpgtw_u128,   iemAImpl_vpcmpgtw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpgtd_u128,   iemAImpl_vpcmpgtd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpcmpgtq_u128,   iemAImpl_vpcmpgtq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddb_u128,     iemAImpl_vpaddb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddw_u128,     iemAImpl_vpaddw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddd_u128,     iemAImpl_vpaddd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddq_u128,     iemAImpl_vpaddq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubb_u128,     iemAImpl_vpsubb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubw_u128,     iemAImpl_vpsubw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubd_u128,     iemAImpl_vpsubd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubq_u128,     iemAImpl_vpsubq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpminub_u128,    iemAImpl_vpminub_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpminuw_u128,    iemAImpl_vpminuw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpminud_u128,    iemAImpl_vpminud_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpminsb_u128,    iemAImpl_vpminsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpminsw_u128,    iemAImpl_vpminsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpminsd_u128,    iemAImpl_vpminsd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaxub_u128,    iemAImpl_vpmaxub_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaxuw_u128,    iemAImpl_vpmaxuw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaxud_u128,    iemAImpl_vpmaxud_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaxsb_u128,    iemAImpl_vpmaxsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaxsw_u128,    iemAImpl_vpmaxsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaxsd_u128,    iemAImpl_vpmaxsd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpacksswb_u128,  iemAImpl_vpacksswb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpackssdw_u128,  iemAImpl_vpackssdw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpackuswb_u128,  iemAImpl_vpackuswb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpackusdw_u128,  iemAImpl_vpackusdw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmullw_u128,    iemAImpl_vpmullw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmulld_u128,    iemAImpl_vpmulld_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmulhw_u128,    iemAImpl_vpmulhw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmulhuw_u128,   iemAImpl_vpmulhuw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpavgb_u128,     iemAImpl_vpavgb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpavgw_u128,     iemAImpl_vpavgw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsignb_u128,    iemAImpl_vpsignb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsignw_u128,    iemAImpl_vpsignw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsignd_u128,    iemAImpl_vpsignd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vphaddw_u128,    iemAImpl_vphaddw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vphaddd_u128,    iemAImpl_vphaddd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vphsubw_u128,    iemAImpl_vphsubw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vphsubd_u128,    iemAImpl_vphsubd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vphaddsw_u128,   iemAImpl_vphaddsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vphsubsw_u128,   iemAImpl_vphsubsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaddubsw_u128, iemAImpl_vpmaddubsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmulhrsw_u128,  iemAImpl_vpmulhrsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsadbw_u128,    iemAImpl_vpsadbw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmuldq_u128,    iemAImpl_vpmuldq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmuludq_u128,   iemAImpl_vpmuludq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubsb_u128,    iemAImpl_vpsubsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubsw_u128,    iemAImpl_vpsubsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubusb_u128,   iemAImpl_vpsubusb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsubusw_u128,   iemAImpl_vpsubusw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddusb_u128,   iemAImpl_vpaddusb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddusw_u128,   iemAImpl_vpaddusw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddsb_u128,    iemAImpl_vpaddsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpaddsw_u128,    iemAImpl_vpaddsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsllw_u128,     iemAImpl_vpsllw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpslld_u128,     iemAImpl_vpslld_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsllq_u128,     iemAImpl_vpsllq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsraw_u128,     iemAImpl_vpsraw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsrad_u128,     iemAImpl_vpsrad_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsrlw_u128,     iemAImpl_vpsrlw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsrld_u128,     iemAImpl_vpsrld_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpsrlq_u128,     iemAImpl_vpsrlq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vpmaddwd_u128, iemAImpl_vpmaddwd_u128_fallback;

FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_vpabsb_u128,     iemAImpl_vpabsb_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_vpabsw_u128,     iemAImpl_vpabsd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_vpabsd_u128,     iemAImpl_vpabsw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_vphminposuw_u128, iemAImpl_vphminposuw_u128_fallback;

FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpshufb_u256,    iemAImpl_vpshufb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpand_u256,      iemAImpl_vpand_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpandn_u256,     iemAImpl_vpandn_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpor_u256,       iemAImpl_vpor_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpxor_u256,      iemAImpl_vpxor_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpeqb_u256,   iemAImpl_vpcmpeqb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpeqw_u256,   iemAImpl_vpcmpeqw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpeqd_u256,   iemAImpl_vpcmpeqd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpeqq_u256,   iemAImpl_vpcmpeqq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpgtb_u256,   iemAImpl_vpcmpgtb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpgtw_u256,   iemAImpl_vpcmpgtw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpgtd_u256,   iemAImpl_vpcmpgtd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpcmpgtq_u256,   iemAImpl_vpcmpgtq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddb_u256,     iemAImpl_vpaddb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddw_u256,     iemAImpl_vpaddw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddd_u256,     iemAImpl_vpaddd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddq_u256,     iemAImpl_vpaddq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubb_u256,     iemAImpl_vpsubb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubw_u256,     iemAImpl_vpsubw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubd_u256,     iemAImpl_vpsubd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubq_u256,     iemAImpl_vpsubq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpminub_u256,    iemAImpl_vpminub_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpminuw_u256,    iemAImpl_vpminuw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpminud_u256,    iemAImpl_vpminud_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpminsb_u256,    iemAImpl_vpminsb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpminsw_u256,    iemAImpl_vpminsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpminsd_u256,    iemAImpl_vpminsd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaxub_u256,    iemAImpl_vpmaxub_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaxuw_u256,    iemAImpl_vpmaxuw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaxud_u256,    iemAImpl_vpmaxud_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaxsb_u256,    iemAImpl_vpmaxsb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaxsw_u256,    iemAImpl_vpmaxsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaxsd_u256,    iemAImpl_vpmaxsd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpacksswb_u256,  iemAImpl_vpacksswb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpackssdw_u256,  iemAImpl_vpackssdw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpackuswb_u256,  iemAImpl_vpackuswb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpackusdw_u256,  iemAImpl_vpackusdw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmullw_u256,    iemAImpl_vpmullw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmulld_u256,    iemAImpl_vpmulld_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmulhw_u256,    iemAImpl_vpmulhw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmulhuw_u256,   iemAImpl_vpmulhuw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpavgb_u256,     iemAImpl_vpavgb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpavgw_u256,     iemAImpl_vpavgw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsignb_u256,    iemAImpl_vpsignb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsignw_u256,    iemAImpl_vpsignw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsignd_u256,    iemAImpl_vpsignd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vphaddw_u256,    iemAImpl_vphaddw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vphaddd_u256,    iemAImpl_vphaddd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vphsubw_u256,    iemAImpl_vphsubw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vphsubd_u256,    iemAImpl_vphsubd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vphaddsw_u256,   iemAImpl_vphaddsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vphsubsw_u256,   iemAImpl_vphsubsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaddubsw_u256, iemAImpl_vpmaddubsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmulhrsw_u256,  iemAImpl_vpmulhrsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsadbw_u256,    iemAImpl_vpsadbw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmuldq_u256,    iemAImpl_vpmuldq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmuludq_u256,   iemAImpl_vpmuludq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubsb_u256,    iemAImpl_vpsubsb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubsw_u256,    iemAImpl_vpsubsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubusb_u256,   iemAImpl_vpsubusb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsubusw_u256,   iemAImpl_vpsubusw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddusb_u256,   iemAImpl_vpaddusb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddusw_u256,   iemAImpl_vpaddusw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddsb_u256,    iemAImpl_vpaddsb_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpaddsw_u256,    iemAImpl_vpaddsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsllw_u256,     iemAImpl_vpsllw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpslld_u256,     iemAImpl_vpslld_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsllq_u256,     iemAImpl_vpsllq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsraw_u256,     iemAImpl_vpsraw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsrad_u256,     iemAImpl_vpsrad_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsrlw_u256,     iemAImpl_vpsrlw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsrld_u256,     iemAImpl_vpsrld_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpsrlq_u256,     iemAImpl_vpsrlq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpmaddwd_u256, iemAImpl_vpmaddwd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpermps_u256,    iemAImpl_vpermps_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256 iemAImpl_vpermd_u256,     iemAImpl_vpermd_u256_fallback;

FNIEMAIMPLMEDIAOPTF2U256 iemAImpl_vpabsb_u256,     iemAImpl_vpabsb_u256_fallback;
FNIEMAIMPLMEDIAOPTF2U256 iemAImpl_vpabsw_u256,     iemAImpl_vpabsw_u256_fallback;
FNIEMAIMPLMEDIAOPTF2U256 iemAImpl_vpabsd_u256,     iemAImpl_vpabsd_u256_fallback;
/** @} */

/** @name Media (SSE/MMX/AVX) operations: lowhalf1 + lowhalf1 -> full1.
 * @{ */
FNIEMAIMPLMEDIAOPTF2U64   iemAImpl_punpcklbw_u64,  iemAImpl_punpcklwd_u64,  iemAImpl_punpckldq_u64;
FNIEMAIMPLMEDIAOPTF2U128  iemAImpl_punpcklbw_u128, iemAImpl_punpcklwd_u128, iemAImpl_punpckldq_u128, iemAImpl_punpcklqdq_u128;
FNIEMAIMPLMEDIAOPTF3U128  iemAImpl_vpunpcklbw_u128,  iemAImpl_vpunpcklbw_u128_fallback,
                          iemAImpl_vpunpcklwd_u128,  iemAImpl_vpunpcklwd_u128_fallback,
                          iemAImpl_vpunpckldq_u128,  iemAImpl_vpunpckldq_u128_fallback,
                          iemAImpl_vpunpcklqdq_u128, iemAImpl_vpunpcklqdq_u128_fallback,
                          iemAImpl_vunpcklps_u128, iemAImpl_vunpcklps_u128_fallback,
                          iemAImpl_vunpcklpd_u128, iemAImpl_vunpcklpd_u128_fallback,
                          iemAImpl_vunpckhps_u128, iemAImpl_vunpckhps_u128_fallback,
                          iemAImpl_vunpckhpd_u128, iemAImpl_vunpckhpd_u128_fallback;

FNIEMAIMPLMEDIAOPTF3U256  iemAImpl_vpunpcklbw_u256,  iemAImpl_vpunpcklbw_u256_fallback,
                          iemAImpl_vpunpcklwd_u256,  iemAImpl_vpunpcklwd_u256_fallback,
                          iemAImpl_vpunpckldq_u256,  iemAImpl_vpunpckldq_u256_fallback,
                          iemAImpl_vpunpcklqdq_u256, iemAImpl_vpunpcklqdq_u256_fallback,
                          iemAImpl_vunpcklps_u256, iemAImpl_vunpcklps_u256_fallback,
                          iemAImpl_vunpcklpd_u256, iemAImpl_vunpcklpd_u256_fallback,
                          iemAImpl_vunpckhps_u256, iemAImpl_vunpckhps_u256_fallback,
                          iemAImpl_vunpckhpd_u256, iemAImpl_vunpckhpd_u256_fallback;
/** @} */

/** @name Media (SSE/MMX/AVX) operations: hihalf1 + hihalf2 -> full1.
 * @{ */
FNIEMAIMPLMEDIAOPTF2U64   iemAImpl_punpckhbw_u64,  iemAImpl_punpckhwd_u64,  iemAImpl_punpckhdq_u64;
FNIEMAIMPLMEDIAOPTF2U128  iemAImpl_punpckhbw_u128, iemAImpl_punpckhwd_u128, iemAImpl_punpckhdq_u128, iemAImpl_punpckhqdq_u128;
FNIEMAIMPLMEDIAOPTF3U128  iemAImpl_vpunpckhbw_u128,  iemAImpl_vpunpckhbw_u128_fallback,
                          iemAImpl_vpunpckhwd_u128,  iemAImpl_vpunpckhwd_u128_fallback,
                          iemAImpl_vpunpckhdq_u128,  iemAImpl_vpunpckhdq_u128_fallback,
                          iemAImpl_vpunpckhqdq_u128, iemAImpl_vpunpckhqdq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256  iemAImpl_vpunpckhbw_u256,  iemAImpl_vpunpckhbw_u256_fallback,
                          iemAImpl_vpunpckhwd_u256,  iemAImpl_vpunpckhwd_u256_fallback,
                          iemAImpl_vpunpckhdq_u256,  iemAImpl_vpunpckhdq_u256_fallback,
                          iemAImpl_vpunpckhqdq_u256, iemAImpl_vpunpckhqdq_u256_fallback;
/** @} */

/** @name Media (SSE/MMX/AVX) operation: Packed Shuffle Stuff (evil)
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAPSHUFU128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAPSHUFU128 *PFNIEMAIMPLMEDIAPSHUFU128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAPSHUFU256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAPSHUFU256 *PFNIEMAIMPLMEDIAPSHUFU256;
IEM_DECL_IMPL_DEF(void, iemAImpl_pshufw_u64,(uint64_t *puDst, uint64_t const *puSrc, uint8_t bEvil));
FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_pshufhw_u128, iemAImpl_pshuflw_u128, iemAImpl_pshufd_u128;
#ifndef IEM_WITHOUT_ASSEMBLY
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpshufhw_u256, iemAImpl_vpshuflw_u256, iemAImpl_vpshufd_u256;
#endif
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpshufhw_u256_fallback, iemAImpl_vpshuflw_u256_fallback, iemAImpl_vpshufd_u256_fallback;
/** @} */

/** @name Media (SSE/MMX/AVX) operation: Shift Immediate Stuff (evil)
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAPSHIFTU64,(uint64_t *puDst, uint8_t bShift));
typedef FNIEMAIMPLMEDIAPSHIFTU64 *PFNIEMAIMPLMEDIAPSHIFTU64;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAPSHIFTU128,(PRTUINT128U puDst, uint8_t bShift));
typedef FNIEMAIMPLMEDIAPSHIFTU128 *PFNIEMAIMPLMEDIAPSHIFTU128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAPSHIFTU256,(PRTUINT256U puDst, uint8_t bShift));
typedef FNIEMAIMPLMEDIAPSHIFTU256 *PFNIEMAIMPLMEDIAPSHIFTU256;
FNIEMAIMPLMEDIAPSHIFTU64  iemAImpl_psllw_imm_u64,  iemAImpl_pslld_imm_u64,  iemAImpl_psllq_imm_u64;
FNIEMAIMPLMEDIAPSHIFTU64  iemAImpl_psrlw_imm_u64,  iemAImpl_psrld_imm_u64,  iemAImpl_psrlq_imm_u64;
FNIEMAIMPLMEDIAPSHIFTU64  iemAImpl_psraw_imm_u64,  iemAImpl_psrad_imm_u64;
FNIEMAIMPLMEDIAPSHIFTU128 iemAImpl_psllw_imm_u128, iemAImpl_pslld_imm_u128, iemAImpl_psllq_imm_u128;
FNIEMAIMPLMEDIAPSHIFTU128 iemAImpl_psrlw_imm_u128, iemAImpl_psrld_imm_u128, iemAImpl_psrlq_imm_u128;
FNIEMAIMPLMEDIAPSHIFTU128 iemAImpl_psraw_imm_u128, iemAImpl_psrad_imm_u128;
FNIEMAIMPLMEDIAPSHIFTU128 iemAImpl_pslldq_imm_u128, iemAImpl_psrldq_imm_u128;
/** @} */

/** @name Media (SSE/MMX/AVX) operation: Move Byte Mask
 * @{ */
IEM_DECL_IMPL_DEF(void, iemAImpl_maskmovq_u64,(uint64_t *puMem, uint64_t const *puSrc, uint64_t const *puMsk));
IEM_DECL_IMPL_DEF(void, iemAImpl_maskmovdqu_u128,(PRTUINT128U puMem, PCRTUINT128U puSrc, PCRTUINT128U puMsk));
IEM_DECL_IMPL_DEF(void, iemAImpl_pmovmskb_u64,(uint64_t *pu64Dst, uint64_t const *puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_pmovmskb_u128,(uint64_t *pu64Dst, PCRTUINT128U puSrc));
#ifndef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovmskb_u256,(uint64_t *pu64Dst, PCRTUINT256U puSrc));
#endif
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovmskb_u256_fallback,(uint64_t *pu64Dst, PCRTUINT256U puSrc));
/** @} */

/** @name Media (SSE/MMX/AVX) operations: Variable Blend Packed Bytes/R32/R64.
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLBLENDU128,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puMask));
typedef FNIEMAIMPLBLENDU128  *PFNIEMAIMPLBLENDU128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLAVXBLENDU128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, PCRTUINT128U puMask));
typedef FNIEMAIMPLAVXBLENDU128  *PFNIEMAIMPLAVXBLENDU128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLAVXBLENDU256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, PCRTUINT256U puMask));
typedef FNIEMAIMPLAVXBLENDU256  *PFNIEMAIMPLAVXBLENDU256;

FNIEMAIMPLBLENDU128 iemAImpl_pblendvb_u128;
FNIEMAIMPLBLENDU128 iemAImpl_pblendvb_u128_fallback;
FNIEMAIMPLAVXBLENDU128 iemAImpl_vpblendvb_u128;
FNIEMAIMPLAVXBLENDU128 iemAImpl_vpblendvb_u128_fallback;
FNIEMAIMPLAVXBLENDU256 iemAImpl_vpblendvb_u256;
FNIEMAIMPLAVXBLENDU256 iemAImpl_vpblendvb_u256_fallback;

FNIEMAIMPLBLENDU128 iemAImpl_blendvps_u128;
FNIEMAIMPLBLENDU128 iemAImpl_blendvps_u128_fallback;
FNIEMAIMPLAVXBLENDU128 iemAImpl_vblendvps_u128;
FNIEMAIMPLAVXBLENDU128 iemAImpl_vblendvps_u128_fallback;
FNIEMAIMPLAVXBLENDU256 iemAImpl_vblendvps_u256;
FNIEMAIMPLAVXBLENDU256 iemAImpl_vblendvps_u256_fallback;

FNIEMAIMPLBLENDU128 iemAImpl_blendvpd_u128;
FNIEMAIMPLBLENDU128 iemAImpl_blendvpd_u128_fallback;
FNIEMAIMPLAVXBLENDU128 iemAImpl_vblendvpd_u128;
FNIEMAIMPLAVXBLENDU128 iemAImpl_vblendvpd_u128_fallback;
FNIEMAIMPLAVXBLENDU256 iemAImpl_vblendvpd_u256;
FNIEMAIMPLAVXBLENDU256 iemAImpl_vblendvpd_u256_fallback;
/** @} */


/** @name Media (SSE/MMX/AVX) operation: Sort this later
 * @{ */
IEM_DECL_IMPL_DEF(void, iemAImpl_pmovsxbw_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbw_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbw_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbw_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovsxbd_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbd_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbd_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbd_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovsxbq_u128,(PRTUINT128U puDst, uint16_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbq_u128,(PRTUINT128U puDst, uint16_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbq_u128_fallback,(PRTUINT128U puDst, uint16_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbq_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovsxwd_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwd_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwd_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwd_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovsxwq_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwq_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwq_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwq_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovsxdq_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxdq_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxdq_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxdq_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovzxbw_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbw_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbw_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbw_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovzxbd_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbd_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbd_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbd_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovzxbq_u128,(PRTUINT128U puDst, uint16_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbq_u128,(PRTUINT128U puDst, uint16_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbq_u128_fallback,(PRTUINT128U puDst, uint16_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbq_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovzxwd_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwd_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwd_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwd_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovzxwq_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwq_u128,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwq_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwq_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovzxdq_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxdq_u128,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxdq_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxdq_u256,(PRTUINT256U puDst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_shufpd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufpd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufpd_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil));

IEM_DECL_IMPL_DEF(void, iemAImpl_shufps_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufps_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufps_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_vshufps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil));

IEM_DECL_IMPL_DEF(void, iemAImpl_palignr_u64,(uint64_t *pu64Dst, uint64_t u64Src, uint8_t bEvil));
IEM_DECL_IMPL_DEF(void, iemAImpl_palignr_u64_fallback,(uint64_t *pu64Dst, uint64_t u64Src, uint8_t bEvil));

IEM_DECL_IMPL_DEF(void, iemAImpl_movmskps_u128,(uint8_t *pu8Dst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskps_u128,(uint8_t *pu8Dst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskps_u128_fallback,(uint8_t *pu8Dst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskps_u256,(uint8_t *pu8Dst, PCRTUINT256U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskps_u256_fallback,(uint8_t *pu8Dst, PCRTUINT256U puSrc));

IEM_DECL_IMPL_DEF(void, iemAImpl_movmskpd_u128,(uint8_t *pu8Dst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskpd_u128,(uint8_t *pu8Dst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskpd_u128_fallback,(uint8_t *pu8Dst, PCRTUINT128U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskpd_u256,(uint8_t *pu8Dst, PCRTUINT256U puSrc));
IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskpd_u256_fallback,(uint8_t *pu8Dst, PCRTUINT256U puSrc));


typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF2U128IMM8,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAOPTF2U128IMM8 *PFNIEMAIMPLMEDIAOPTF2U128IMM8;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF2U256IMM8,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAOPTF2U256IMM8 *PFNIEMAIMPLMEDIAOPTF2U256IMM8;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF3U128IMM8,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAOPTF3U128IMM8 *PFNIEMAIMPLMEDIAOPTF3U128IMM8;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLMEDIAOPTF3U256IMM8,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAOPTF3U256IMM8 *PFNIEMAIMPLMEDIAOPTF3U256IMM8;

FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_palignr_u128, iemAImpl_palignr_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_pblendw_u128, iemAImpl_pblendw_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_blendps_u128, iemAImpl_blendps_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_blendpd_u128, iemAImpl_blendpd_u128_fallback;

FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vpalignr_u128, iemAImpl_vpalignr_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vpblendw_u128, iemAImpl_vpblendw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vpblendd_u128, iemAImpl_vpblendd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vblendps_u128, iemAImpl_vblendps_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vblendpd_u128, iemAImpl_vblendpd_u128_fallback;

FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vpalignr_u256, iemAImpl_vpalignr_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vpblendw_u256, iemAImpl_vpblendw_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vpblendd_u256, iemAImpl_vpblendd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vblendps_u256, iemAImpl_vblendps_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vblendpd_u256, iemAImpl_vblendpd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vperm2i128_u256, iemAImpl_vperm2i128_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vperm2f128_u256, iemAImpl_vperm2f128_u256_fallback;

FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_aesimc_u128,     iemAImpl_aesimc_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_aesenc_u128,     iemAImpl_aesenc_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_aesenclast_u128, iemAImpl_aesenclast_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_aesdec_u128,     iemAImpl_aesdec_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_aesdeclast_u128, iemAImpl_aesdeclast_u128_fallback;

FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_vaesimc_u128,     iemAImpl_vaesimc_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vaesenc_u128,     iemAImpl_vaesenc_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vaesenclast_u128, iemAImpl_vaesenclast_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vaesdec_u128,     iemAImpl_vaesdec_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128 iemAImpl_vaesdeclast_u128, iemAImpl_vaesdeclast_u128_fallback;

FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_aeskeygenassist_u128, iemAImpl_aeskeygenassist_u128_fallback;

FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_vaeskeygenassist_u128, iemAImpl_vaeskeygenassist_u128_fallback;

FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_sha1nexte_u128,       iemAImpl_sha1nexte_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_sha1msg1_u128,        iemAImpl_sha1msg1_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_sha1msg2_u128,        iemAImpl_sha1msg2_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_sha256msg1_u128,      iemAImpl_sha256msg1_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128 iemAImpl_sha256msg2_u128,      iemAImpl_sha256msg2_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_sha1rnds4_u128,   iemAImpl_sha1rnds4_u128_fallback;
IEM_DECL_IMPL_DEF(void, iemAImpl_sha256rnds2_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puXmm0Constants));
IEM_DECL_IMPL_DEF(void, iemAImpl_sha256rnds2_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puXmm0Constants));

FNIEMAIMPLMEDIAOPTF2U256IMM8 iemAImpl_vpermq_u256,      iemAImpl_vpermq_u256_fallback;
FNIEMAIMPLMEDIAOPTF2U256IMM8 iemAImpl_vpermpd_u256,     iemAImpl_vpermpd_u256_fallback;

typedef struct IEMPCMPISTRXSRC
{
    RTUINT128U              uSrc1;
    RTUINT128U              uSrc2;
} IEMPCMPISTRXSRC;
typedef IEMPCMPISTRXSRC *PIEMPCMPISTRXSRC;
typedef const IEMPCMPISTRXSRC *PCIEMPCMPISTRXSRC;

typedef struct IEMPCMPESTRXSRC
{
    RTUINT128U              uSrc1;
    RTUINT128U              uSrc2;
    uint64_t                u64Rax;
    uint64_t                u64Rdx;
} IEMPCMPESTRXSRC;
typedef IEMPCMPESTRXSRC *PIEMPCMPESTRXSRC;
typedef const IEMPCMPESTRXSRC *PCIEMPCMPESTRXSRC;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLPCMPISTRIU128IMM8,(uint32_t *pEFlags, PCRTUINT128U pSrc1, PCRTUINT128U pSrc2, uint8_t bEvil));
typedef FNIEMAIMPLPCMPISTRIU128IMM8 *PFNIEMAIMPLPCMPISTRIU128IMM8;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLPCMPESTRIU128IMM8,(uint32_t *pu32Ecx, uint32_t *pEFlags, PCIEMPCMPESTRXSRC pSrc, uint8_t bEvil));
typedef FNIEMAIMPLPCMPESTRIU128IMM8 *PFNIEMAIMPLPCMPESTRIU128IMM8;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLPCMPISTRMU128IMM8,(PRTUINT128U puDst, uint32_t *pEFlags, PCIEMPCMPISTRXSRC pSrc, uint8_t bEvil));
typedef FNIEMAIMPLPCMPISTRMU128IMM8 *PFNIEMAIMPLPCMPISTRMU128IMM8;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLPCMPESTRMU128IMM8,(PRTUINT128U puDst, uint32_t *pEFlags, PCIEMPCMPESTRXSRC pSrc, uint8_t bEvil));
typedef FNIEMAIMPLPCMPESTRMU128IMM8 *PFNIEMAIMPLPCMPESTRMU128IMM8;

FNIEMAIMPLPCMPISTRIU128IMM8 iemAImpl_pcmpistri_u128,  iemAImpl_pcmpistri_u128_fallback;
FNIEMAIMPLPCMPESTRIU128IMM8 iemAImpl_pcmpestri_u128,  iemAImpl_pcmpestri_u128_fallback;
FNIEMAIMPLPCMPISTRMU128IMM8 iemAImpl_pcmpistrm_u128,  iemAImpl_pcmpistrm_u128_fallback;
FNIEMAIMPLPCMPESTRMU128IMM8 iemAImpl_pcmpestrm_u128,  iemAImpl_pcmpestrm_u128_fallback;
FNIEMAIMPLPCMPISTRIU128IMM8 iemAImpl_vpcmpistri_u128, iemAImpl_vpcmpistri_u128_fallback;
FNIEMAIMPLPCMPESTRIU128IMM8 iemAImpl_vpcmpestri_u128, iemAImpl_vpcmpestri_u128_fallback;
FNIEMAIMPLPCMPISTRMU128IMM8 iemAImpl_vpcmpistrm_u128, iemAImpl_vpcmpistrm_u128_fallback;
FNIEMAIMPLPCMPESTRMU128IMM8 iemAImpl_vpcmpestrm_u128, iemAImpl_vpcmpestrm_u128_fallback;


FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_pclmulqdq_u128, iemAImpl_pclmulqdq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vpclmulqdq_u128, iemAImpl_vpclmulqdq_u128_fallback;

FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_mpsadbw_u128, iemAImpl_mpsadbw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U128IMM8 iemAImpl_vmpsadbw_u128, iemAImpl_vmpsadbw_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256IMM8 iemAImpl_vmpsadbw_u256, iemAImpl_vmpsadbw_u256_fallback;

FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsllw_imm_u128, iemAImpl_vpsllw_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsllw_imm_u256, iemAImpl_vpsllw_imm_u256_fallback;
FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpslld_imm_u128, iemAImpl_vpslld_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpslld_imm_u256, iemAImpl_vpslld_imm_u256_fallback;
FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsllq_imm_u128, iemAImpl_vpsllq_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsllq_imm_u256, iemAImpl_vpsllq_imm_u256_fallback;
IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift));

FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsraw_imm_u128, iemAImpl_vpsraw_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsraw_imm_u256, iemAImpl_vpsraw_imm_u256_fallback;
FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsrad_imm_u128, iemAImpl_vpsrad_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsrad_imm_u256, iemAImpl_vpsrad_imm_u256_fallback;

FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsrlw_imm_u128, iemAImpl_vpsrlw_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsrlw_imm_u256, iemAImpl_vpsrlw_imm_u256_fallback;
FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsrld_imm_u128, iemAImpl_vpsrld_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsrld_imm_u256, iemAImpl_vpsrld_imm_u256_fallback;
FNIEMAIMPLMEDIAPSHUFU128 iemAImpl_vpsrlq_imm_u128, iemAImpl_vpsrlq_imm_u128_fallback;
FNIEMAIMPLMEDIAPSHUFU256 iemAImpl_vpsrlq_imm_u256, iemAImpl_vpsrlq_imm_u256_fallback;
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift));
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift));

FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpermilps_u128,     iemAImpl_vpermilps_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_vpermilps_imm_u128, iemAImpl_vpermilps_imm_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpermilps_u256,     iemAImpl_vpermilps_u256_fallback;
FNIEMAIMPLMEDIAOPTF2U256IMM8 iemAImpl_vpermilps_imm_u256, iemAImpl_vpermilps_imm_u256_fallback;

FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpermilpd_u128,     iemAImpl_vpermilpd_u128_fallback;
FNIEMAIMPLMEDIAOPTF2U128IMM8 iemAImpl_vpermilpd_imm_u128, iemAImpl_vpermilpd_imm_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpermilpd_u256,     iemAImpl_vpermilpd_u256_fallback;
FNIEMAIMPLMEDIAOPTF2U256IMM8 iemAImpl_vpermilpd_imm_u256, iemAImpl_vpermilpd_imm_u256_fallback;

FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpsllvd_u128, iemAImpl_vpsllvd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpsllvd_u256, iemAImpl_vpsllvd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpsllvq_u128, iemAImpl_vpsllvq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpsllvq_u256, iemAImpl_vpsllvq_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpsravd_u128, iemAImpl_vpsravd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpsravd_u256, iemAImpl_vpsravd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpsrlvd_u128, iemAImpl_vpsrlvd_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpsrlvd_u256, iemAImpl_vpsrlvd_u256_fallback;
FNIEMAIMPLMEDIAOPTF3U128     iemAImpl_vpsrlvq_u128, iemAImpl_vpsrlvq_u128_fallback;
FNIEMAIMPLMEDIAOPTF3U256     iemAImpl_vpsrlvq_u256, iemAImpl_vpsrlvq_u256_fallback;
/** @} */

/** @name Media Odds and Ends
 * @{ */
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCR32U8,(uint32_t *puDst, uint8_t uSrc));
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCR32U16,(uint32_t *puDst, uint16_t uSrc));
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCR32U32,(uint32_t *puDst, uint32_t uSrc));
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLCR32U64,(uint32_t *puDst, uint64_t uSrc));
FNIEMAIMPLCR32U8  iemAImpl_crc32_u8,  iemAImpl_crc32_u8_fallback;
FNIEMAIMPLCR32U16 iemAImpl_crc32_u16, iemAImpl_crc32_u16_fallback;
FNIEMAIMPLCR32U32 iemAImpl_crc32_u32, iemAImpl_crc32_u32_fallback;
FNIEMAIMPLCR32U64 iemAImpl_crc32_u64, iemAImpl_crc32_u64_fallback;

typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLF2EFL128,(PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint32_t *pEFlags));
typedef FNIEMAIMPLF2EFL128 *PFNIEMAIMPLF2EFL128;
typedef IEM_DECL_IMPL_TYPE(void, FNIEMAIMPLF2EFL256,(PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint32_t *pEFlags));
typedef FNIEMAIMPLF2EFL256 *PFNIEMAIMPLF2EFL256;
FNIEMAIMPLF2EFL128 iemAImpl_ptest_u128;
FNIEMAIMPLF2EFL256 iemAImpl_vptest_u256, iemAImpl_vptest_u256_fallback;
FNIEMAIMPLF2EFL128 iemAImpl_vtestps_u128, iemAImpl_vtestps_u128_fallback;
FNIEMAIMPLF2EFL256 iemAImpl_vtestps_u256, iemAImpl_vtestps_u256_fallback;
FNIEMAIMPLF2EFL128 iemAImpl_vtestpd_u128, iemAImpl_vtestpd_u128_fallback;
FNIEMAIMPLF2EFL256 iemAImpl_vtestpd_u256, iemAImpl_vtestpd_u256_fallback;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I32U64,(uint32_t uMxCsrIn, int32_t *pi32Dst, const uint64_t *pu64Src)); /* pu64Src is a double precision floating point. */
typedef FNIEMAIMPLSSEF2I32U64 *PFNIEMAIMPLSSEF2I32U64;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I64U64,(uint32_t uMxCsrIn, int64_t *pi64Dst, const uint64_t *pu64Src)); /* pu64Src is a double precision floating point. */
typedef FNIEMAIMPLSSEF2I64U64 *PFNIEMAIMPLSSEF2I64U64;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I32U32,(uint32_t uMxCsrIn, int32_t *pi32Dst, const uint32_t *pu32Src)); /* pu32Src is a single precision floating point. */
typedef FNIEMAIMPLSSEF2I32U32 *PFNIEMAIMPLSSEF2I32U32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I64U32,(uint32_t uMxCsrIn, int64_t *pi64Dst, const uint32_t *pu32Src)); /* pu32Src is a single precision floating point. */
typedef FNIEMAIMPLSSEF2I64U32 *PFNIEMAIMPLSSEF2I64U32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I32R32,(uint32_t uMxCsrIn, int32_t *pi32Dst, PCRTFLOAT32U pr32Src));
typedef FNIEMAIMPLSSEF2I32R32 *PFNIEMAIMPLSSEF2I32R32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I64R32,(uint32_t uMxCsrIn, int64_t *pi64Dst, PCRTFLOAT32U pr32Src));
typedef FNIEMAIMPLSSEF2I64R32 *PFNIEMAIMPLSSEF2I64R32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I32R64,(uint32_t uMxCsrIn, int32_t *pi32Dst, PCRTFLOAT64U pr64Src));
typedef FNIEMAIMPLSSEF2I32R64 *PFNIEMAIMPLSSEF2I32R64;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2I64R64,(uint32_t uMxCsrIn, int64_t *pi64Dst, PCRTFLOAT64U pr64Src));
typedef FNIEMAIMPLSSEF2I64R64 *PFNIEMAIMPLSSEF2I64R64;

FNIEMAIMPLSSEF2I32U64 iemAImpl_cvttsd2si_i32_r64;
FNIEMAIMPLSSEF2I32U64 iemAImpl_cvtsd2si_i32_r64;

FNIEMAIMPLSSEF2I64U64 iemAImpl_cvttsd2si_i64_r64;
FNIEMAIMPLSSEF2I64U64 iemAImpl_cvtsd2si_i64_r64;

FNIEMAIMPLSSEF2I32U32 iemAImpl_cvttss2si_i32_r32;
FNIEMAIMPLSSEF2I32U32 iemAImpl_cvtss2si_i32_r32;

FNIEMAIMPLSSEF2I64U32 iemAImpl_cvttss2si_i64_r32;
FNIEMAIMPLSSEF2I64U32 iemAImpl_cvtss2si_i64_r32;

FNIEMAIMPLSSEF2I32R32 iemAImpl_vcvttss2si_i32_r32, iemAImpl_vcvttss2si_i32_r32_fallback;
FNIEMAIMPLSSEF2I64R32 iemAImpl_vcvttss2si_i64_r32, iemAImpl_vcvttss2si_i64_r32_fallback;
FNIEMAIMPLSSEF2I32R32 iemAImpl_vcvtss2si_i32_r32,  iemAImpl_vcvtss2si_i32_r32_fallback;
FNIEMAIMPLSSEF2I64R32 iemAImpl_vcvtss2si_i64_r32,  iemAImpl_vcvtss2si_i64_r32_fallback;

FNIEMAIMPLSSEF2I32R64 iemAImpl_vcvttss2si_i32_r64, iemAImpl_vcvttss2si_i32_r64_fallback;
FNIEMAIMPLSSEF2I64R64 iemAImpl_vcvttss2si_i64_r64, iemAImpl_vcvttss2si_i64_r64_fallback;
FNIEMAIMPLSSEF2I32R64 iemAImpl_vcvtss2si_i32_r64,  iemAImpl_vcvtss2si_i32_r64_fallback;
FNIEMAIMPLSSEF2I64R64 iemAImpl_vcvtss2si_i64_r64,  iemAImpl_vcvtss2si_i64_r64_fallback;

FNIEMAIMPLSSEF2I32R32 iemAImpl_vcvttsd2si_i32_r32, iemAImpl_vcvttsd2si_i32_r32_fallback;
FNIEMAIMPLSSEF2I64R32 iemAImpl_vcvttsd2si_i64_r32, iemAImpl_vcvttsd2si_i64_r32_fallback;
FNIEMAIMPLSSEF2I32R32 iemAImpl_vcvtsd2si_i32_r32,  iemAImpl_vcvtsd2si_i32_r32_fallback;
FNIEMAIMPLSSEF2I64R32 iemAImpl_vcvtsd2si_i64_r32,  iemAImpl_vcvtsd2si_i64_r32_fallback;

FNIEMAIMPLSSEF2I32R64 iemAImpl_vcvttsd2si_i32_r64, iemAImpl_vcvttsd2si_i32_r64_fallback;
FNIEMAIMPLSSEF2I64R64 iemAImpl_vcvttsd2si_i64_r64, iemAImpl_vcvttsd2si_i64_r64_fallback;
FNIEMAIMPLSSEF2I32R64 iemAImpl_vcvtsd2si_i32_r64,  iemAImpl_vcvtsd2si_i32_r64_fallback;
FNIEMAIMPLSSEF2I64R64 iemAImpl_vcvtsd2si_i64_r64,  iemAImpl_vcvtsd2si_i64_r64_fallback;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2R32I32,(uint32_t uMxCsrIn, PRTFLOAT32U pr32Dst, const int32_t *pi32Src));
typedef FNIEMAIMPLSSEF2R32I32 *PFNIEMAIMPLSSEF2R32I32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2R32I64,(uint32_t uMxCsrIn, PRTFLOAT32U pr32Dst, const int64_t *pi64Src));
typedef FNIEMAIMPLSSEF2R32I64 *PFNIEMAIMPLSSEF2R32I64;

FNIEMAIMPLSSEF2R32I32 iemAImpl_cvtsi2ss_r32_i32;
FNIEMAIMPLSSEF2R32I64 iemAImpl_cvtsi2ss_r32_i64;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLAVXF3XMMI32,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, const int32_t *pi32Src));
typedef FNIEMAIMPLAVXF3XMMI32 *PFNIEMAIMPLAVXF3XMMI32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLAVXF3XMMI64,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, const int64_t *pi64Src));
typedef FNIEMAIMPLAVXF3XMMI64 *PFNIEMAIMPLAVXF3XMMI64;

FNIEMAIMPLAVXF3XMMI32 iemAImpl_vcvtsi2ss_u128_i32, iemAImpl_vcvtsi2ss_u128_i32_fallback;
FNIEMAIMPLAVXF3XMMI64 iemAImpl_vcvtsi2ss_u128_i64, iemAImpl_vcvtsi2ss_u128_i64_fallback;


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2R64I32,(uint32_t uMxCsrIn, PRTFLOAT64U pr64Dst, const int32_t *pi32Src));
typedef FNIEMAIMPLSSEF2R64I32 *PFNIEMAIMPLSSEF2R64I32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSSEF2R64I64,(uint32_t uMxCsrIn, PRTFLOAT64U pr64Dst, const int64_t *pi64Src));
typedef FNIEMAIMPLSSEF2R64I64 *PFNIEMAIMPLSSEF2R64I64;

FNIEMAIMPLSSEF2R64I32 iemAImpl_cvtsi2sd_r64_i32;
FNIEMAIMPLSSEF2R64I64 iemAImpl_cvtsi2sd_r64_i64;

FNIEMAIMPLAVXF3XMMI32 iemAImpl_vcvtsi2sd_u128_i32, iemAImpl_vcvtsi2sd_u128_i32_fallback;
FNIEMAIMPLAVXF3XMMI64 iemAImpl_vcvtsi2sd_u128_i64, iemAImpl_vcvtsi2sd_u128_i64_fallback;

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2pd_u128_u64,(uint32_t uMxCsrIn, PX86XMMREG puDst, const uint64_t *pu64Src)); /* Actually two single precision floating point values. */
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2pd_u128_u64_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst,  const uint64_t *pu64Src)); /* Actually two single precision floating point values. */
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2pd_u256_u128,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2pd_u256_u128_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86XMMREG puSrc));


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2pd_u128_u64,(uint32_t uMxCsrIn, PX86XMMREG puDst, const uint64_t *pu64Src)); /* Actually two single precision floating point values. */
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2pd_u128_u64_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst,  const uint64_t *pu64Src)); /* Actually two single precision floating point values. */
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2pd_u256_u128,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2pd_u256_u128_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86XMMREG puSrc));


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLF2EFLMXCSRR32R32,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT32U uSrc1, RTFLOAT32U uSrc2));
typedef FNIEMAIMPLF2EFLMXCSRR32R32 *PFNIEMAIMPLF2EFLMXCSRR32R32;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLF2EFLMXCSRR64R64,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT64U uSrc1, RTFLOAT64U uSrc2));
typedef FNIEMAIMPLF2EFLMXCSRR64R64 *PFNIEMAIMPLF2EFLMXCSRR64R64;

FNIEMAIMPLF2EFLMXCSRR32R32 iemAImpl_ucomiss_u128;
FNIEMAIMPLF2EFLMXCSRR32R32 iemAImpl_vucomiss_u128, iemAImpl_vucomiss_u128_fallback;

FNIEMAIMPLF2EFLMXCSRR64R64 iemAImpl_ucomisd_u128;
FNIEMAIMPLF2EFLMXCSRR64R64 iemAImpl_vucomisd_u128, iemAImpl_vucomisd_u128_fallback;

FNIEMAIMPLF2EFLMXCSRR32R32 iemAImpl_comiss_u128;
FNIEMAIMPLF2EFLMXCSRR32R32 iemAImpl_vcomiss_u128, iemAImpl_vcomiss_u128_fallback;

FNIEMAIMPLF2EFLMXCSRR64R64 iemAImpl_comisd_u128;
FNIEMAIMPLF2EFLMXCSRR64R64 iemAImpl_vcomisd_u128, iemAImpl_vcomisd_u128_fallback;


typedef struct IEMMEDIAF2XMMSRC
{
    X86XMMREG               uSrc1;
    X86XMMREG               uSrc2;
} IEMMEDIAF2XMMSRC;
typedef IEMMEDIAF2XMMSRC *PIEMMEDIAF2XMMSRC;
typedef const IEMMEDIAF2XMMSRC *PCIEMMEDIAF2XMMSRC;


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF3XMMIMM8,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAF3XMMIMM8 *PFNIEMAIMPLMEDIAF3XMMIMM8;


typedef struct IEMMEDIAF2YMMSRC
{
    X86YMMREG               uSrc1;
    X86YMMREG               uSrc2;
} IEMMEDIAF2YMMSRC;
typedef IEMMEDIAF2YMMSRC *PIEMMEDIAF2YMMSRC;
typedef const IEMMEDIAF2YMMSRC *PCIEMMEDIAF2YMMSRC;


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF3YMMIMM8,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCIEMMEDIAF2YMMSRC puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAF3YMMIMM8 *PFNIEMAIMPLMEDIAF3YMMIMM8;


FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_cmpps_u128;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_cmppd_u128;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_cmpss_u128;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_cmpsd_u128;

FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_vcmpps_u128, iemAImpl_vcmpps_u128_fallback;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_vcmppd_u128, iemAImpl_vcmppd_u128_fallback;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_vcmpss_u128, iemAImpl_vcmpss_u128_fallback;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_vcmpsd_u128, iemAImpl_vcmpsd_u128_fallback;

FNIEMAIMPLMEDIAF3YMMIMM8 iemAImpl_vcmpps_u256, iemAImpl_vcmpps_u256_fallback;
FNIEMAIMPLMEDIAF3YMMIMM8 iemAImpl_vcmppd_u256, iemAImpl_vcmppd_u256_fallback;

FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_roundss_u128;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_roundsd_u128;

FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_dpps_u128,     iemAImpl_dpps_u128_fallback;
FNIEMAIMPLMEDIAF3XMMIMM8 iemAImpl_dppd_u128,     iemAImpl_dppd_u128_fallback;


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF2U128IMM8,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAF2U128IMM8 *PFNIEMAIMPLMEDIAF2U128IMM8;


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMEDIAF2U256IMM8,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc, uint8_t bEvil));
typedef FNIEMAIMPLMEDIAF2U256IMM8 *PFNIEMAIMPLMEDIAF2U256IMM8;


FNIEMAIMPLMEDIAF2U128IMM8 iemAImpl_roundps_u128,  iemAImpl_roundps_u128_fallback;
FNIEMAIMPLMEDIAF2U128IMM8 iemAImpl_roundpd_u128,  iemAImpl_roundpd_u128_fallback;

FNIEMAIMPLMEDIAF2U128IMM8 iemAImpl_vroundps_u128, iemAImpl_vroundps_u128_fallback;
FNIEMAIMPLMEDIAF2U128IMM8 iemAImpl_vroundpd_u128, iemAImpl_vroundpd_u128_fallback;

FNIEMAIMPLMEDIAF2U256IMM8 iemAImpl_vroundps_u256, iemAImpl_vroundps_u256_fallback;
FNIEMAIMPLMEDIAF2U256IMM8 iemAImpl_vroundpd_u256, iemAImpl_vroundpd_u256_fallback;

FNIEMAIMPLMEDIAF3XMMIMM8  iemAImpl_vroundss_u128, iemAImpl_vroundss_u128_fallback;
FNIEMAIMPLMEDIAF3XMMIMM8  iemAImpl_vroundsd_u128, iemAImpl_vroundsd_u128_fallback;

FNIEMAIMPLMEDIAF3XMMIMM8  iemAImpl_vdpps_u128,     iemAImpl_vdpps_u128_fallback;
FNIEMAIMPLMEDIAF3XMMIMM8  iemAImpl_vdppd_u128,     iemAImpl_vdppd_u128_fallback;

FNIEMAIMPLMEDIAF3YMMIMM8  iemAImpl_vdpps_u256,     iemAImpl_vdpps_u256_fallback;


typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMXCSRU64U128,(uint32_t fMxCsrIn, uint64_t *pu64Dst, PCX86XMMREG pSrc));
typedef FNIEMAIMPLMXCSRU64U128 *PFNIEMAIMPLMXCSRU64U128;

FNIEMAIMPLMXCSRU64U128 iemAImpl_cvtpd2pi_u128;
FNIEMAIMPLMXCSRU64U128 iemAImpl_cvttpd2pi_u128;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMXCSRU128U64,(uint32_t fMxCsrIn, PX86XMMREG pDst, uint64_t u64Src));
typedef FNIEMAIMPLMXCSRU128U64 *PFNIEMAIMPLMXCSRU128U64;

FNIEMAIMPLMXCSRU128U64 iemAImpl_cvtpi2ps_u128;
FNIEMAIMPLMXCSRU128U64 iemAImpl_cvtpi2pd_u128;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLMXCSRU64U64,(uint32_t fMxCsrIn, uint64_t *pu64Dst, uint64_t u64Src));
typedef FNIEMAIMPLMXCSRU64U64 *PFNIEMAIMPLMXCSRU64U64;

FNIEMAIMPLMXCSRU64U64 iemAImpl_cvtps2pi_u128;
FNIEMAIMPLMXCSRU64U64 iemAImpl_cvttps2pi_u128;

/** @} */


/** @name Function tables.
 * @{
 */

/**
 * Function table for a binary operator providing implementation based on
 * operand size.
 */
typedef struct IEMOPBINSIZES
{
    PFNIEMAIMPLBINU8  pfnNormalU8,    pfnLockedU8;
    PFNIEMAIMPLBINU16 pfnNormalU16,   pfnLockedU16;
    PFNIEMAIMPLBINU32 pfnNormalU32,   pfnLockedU32;
    PFNIEMAIMPLBINU64 pfnNormalU64,   pfnLockedU64;
} IEMOPBINSIZES;
/** Pointer to a binary operator function table. */
typedef IEMOPBINSIZES const *PCIEMOPBINSIZES;


/**
 * Function table for a unary operator providing implementation based on
 * operand size.
 */
typedef struct IEMOPUNARYSIZES
{
    PFNIEMAIMPLUNARYU8  pfnNormalU8,    pfnLockedU8;
    PFNIEMAIMPLUNARYU16 pfnNormalU16,   pfnLockedU16;
    PFNIEMAIMPLUNARYU32 pfnNormalU32,   pfnLockedU32;
    PFNIEMAIMPLUNARYU64 pfnNormalU64,   pfnLockedU64;
} IEMOPUNARYSIZES;
/** Pointer to a unary operator function table. */
typedef IEMOPUNARYSIZES const *PCIEMOPUNARYSIZES;


/**
 * Function table for a shift operator providing implementation based on
 * operand size.
 */
typedef struct IEMOPSHIFTSIZES
{
    PFNIEMAIMPLSHIFTU8  pfnNormalU8;
    PFNIEMAIMPLSHIFTU16 pfnNormalU16;
    PFNIEMAIMPLSHIFTU32 pfnNormalU32;
    PFNIEMAIMPLSHIFTU64 pfnNormalU64;
} IEMOPSHIFTSIZES;
/** Pointer to a shift operator function table. */
typedef IEMOPSHIFTSIZES const *PCIEMOPSHIFTSIZES;


/**
 * Function table for a multiplication or division operation.
 */
typedef struct IEMOPMULDIVSIZES
{
    PFNIEMAIMPLMULDIVU8  pfnU8;
    PFNIEMAIMPLMULDIVU16 pfnU16;
    PFNIEMAIMPLMULDIVU32 pfnU32;
    PFNIEMAIMPLMULDIVU64 pfnU64;
} IEMOPMULDIVSIZES;
/** Pointer to a multiplication or division operation function table. */
typedef IEMOPMULDIVSIZES const *PCIEMOPMULDIVSIZES;


/**
 * Function table for a double precision shift operator providing implementation
 * based on operand size.
 */
typedef struct IEMOPSHIFTDBLSIZES
{
    PFNIEMAIMPLSHIFTDBLU16 pfnNormalU16;
    PFNIEMAIMPLSHIFTDBLU32 pfnNormalU32;
    PFNIEMAIMPLSHIFTDBLU64 pfnNormalU64;
} IEMOPSHIFTDBLSIZES;
/** Pointer to a double precision shift function table. */
typedef IEMOPSHIFTDBLSIZES const *PCIEMOPSHIFTDBLSIZES;


/**
 * Function table for media instruction taking two full sized media source
 * registers and one full sized destination register (AVX).
 */
typedef struct IEMOPMEDIAF3
{
    PFNIEMAIMPLMEDIAF3U128 pfnU128;
    PFNIEMAIMPLMEDIAF3U256 pfnU256;
} IEMOPMEDIAF3;
/** Pointer to a media operation function table for 3 full sized ops (AVX). */
typedef IEMOPMEDIAF3 const *PCIEMOPMEDIAF3;

/** @def IEMOPMEDIAF3_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAF3_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAF3 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAF3 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAF3_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAF3 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAF3_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAF3_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAF3_INIT_VARS_EX */
#define IEMOPMEDIAF3_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAF3_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                              RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))


/**
 * Function table for media instruction taking one full sized media source
 * registers and one full sized destination register (AVX).
 */
typedef struct IEMOPMEDIAF2
{
    PFNIEMAIMPLMEDIAF2U128 pfnU128;
    PFNIEMAIMPLMEDIAF2U256 pfnU256;
} IEMOPMEDIAF2;
/** Pointer to a media operation function table for 2 full sized ops (AVX). */
typedef IEMOPMEDIAF2 const *PCIEMOPMEDIAF2;

/** @def IEMOPMEDIAF2_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAF2_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAF2 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAF2 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAF2_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAF2 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAF2_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAF2_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAF2_INIT_VARS_EX */
#define IEMOPMEDIAF2_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAF2_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                              RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))


/**
 * Function table for media instruction taking two full sized media source
 * registers and one full sized destination register, but no additional state
 * (AVX).
 */
typedef struct IEMOPMEDIAOPTF3
{
    PFNIEMAIMPLMEDIAOPTF3U128 pfnU128;
    PFNIEMAIMPLMEDIAOPTF3U256 pfnU256;
} IEMOPMEDIAOPTF3;
/** Pointer to a media operation function table for 3 full sized ops (AVX). */
typedef IEMOPMEDIAOPTF3 const *PCIEMOPMEDIAOPTF3;

/** @def IEMOPMEDIAOPTF3_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAOPTF3_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF3 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAOPTF3 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAOPTF3_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF3 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAOPTF3_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAOPTF3_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAOPTF3_INIT_VARS_EX */
#define IEMOPMEDIAOPTF3_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAOPTF3_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                                 RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))

/**
 * Function table for media instruction taking one full sized media source
 * registers and one full sized destination register, but no additional state
 * (AVX).
 */
typedef struct IEMOPMEDIAOPTF2
{
    PFNIEMAIMPLMEDIAOPTF2U128 pfnU128;
    PFNIEMAIMPLMEDIAOPTF2U256 pfnU256;
} IEMOPMEDIAOPTF2;
/** Pointer to a media operation function table for 2 full sized ops (AVX). */
typedef IEMOPMEDIAOPTF2 const *PCIEMOPMEDIAOPTF2;

/** @def IEMOPMEDIAOPTF2_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAOPTF2_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF2 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAOPTF2 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAOPTF2_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF2 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAOPTF2_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAOPTF2_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAOPTF2_INIT_VARS_EX */
#define IEMOPMEDIAOPTF2_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAOPTF2_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                                 RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))


/**
 * Function table for media instruction taking one full sized media source
 * register and one full sized destination register and an 8-bit immediate (AVX).
 */
typedef struct IEMOPMEDIAF2IMM8
{
    PFNIEMAIMPLMEDIAF2U128IMM8 pfnU128;
    PFNIEMAIMPLMEDIAF2U256IMM8 pfnU256;
} IEMOPMEDIAF2IMM8;
/** Pointer to a media operation function table for 2 full sized ops (AVX). */
typedef IEMOPMEDIAF2IMM8 const *PCIEMOPMEDIAF2IMM8;

/** @def IEMOPMEDIAF2IMM8_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAF2IMM8_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAF2IMM8 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAF2IMM8 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAF2IMM8_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAF2IMM8 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAF2IMM8_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAF2IMM8_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAF2IMM8_INIT_VARS_EX */
#define IEMOPMEDIAF2IMM8_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAF2IMM8_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                                  RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))


/**
 * Function table for media instruction taking one full sized media source
 * register and one full sized destination register and an 8-bit immediate, but no additional state
 * (AVX).
 */
typedef struct IEMOPMEDIAOPTF2IMM8
{
    PFNIEMAIMPLMEDIAOPTF2U128IMM8 pfnU128;
    PFNIEMAIMPLMEDIAOPTF2U256IMM8 pfnU256;
} IEMOPMEDIAOPTF2IMM8;
/** Pointer to a media operation function table for 2 full sized ops (AVX). */
typedef IEMOPMEDIAOPTF2IMM8 const *PCIEMOPMEDIAOPTF2IMM8;

/** @def IEMOPMEDIAOPTF2IMM8_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAOPTF2IMM8_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF2IMM8 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAOPTF2IMM8 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAOPTF2IMM8_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF2IMM8 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAOPTF2IMM8_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAOPTF2IMM8_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAOPTF2IMM8_INIT_VARS_EX */
#define IEMOPMEDIAOPTF2IMM8_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAOPTF2IMM8_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_imm_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_imm_u256),\
                                     RT_CONCAT3(iemAImpl_,a_InstrNm,_imm_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_imm_u256_fallback))

/**
 * Function table for media instruction taking two full sized media source
 * registers and one full sized destination register and an 8-bit immediate, but no additional state
 * (AVX).
 */
typedef struct IEMOPMEDIAOPTF3IMM8
{
    PFNIEMAIMPLMEDIAOPTF3U128IMM8 pfnU128;
    PFNIEMAIMPLMEDIAOPTF3U256IMM8 pfnU256;
} IEMOPMEDIAOPTF3IMM8;
/** Pointer to a media operation function table for 3 full sized ops (AVX). */
typedef IEMOPMEDIAOPTF3IMM8 const *PCIEMOPMEDIAOPTF3IMM8;

/** @def IEMOPMEDIAOPTF3IMM8_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPMEDIAOPTF3IMM8_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF3IMM8 const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPMEDIAOPTF3IMM8 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPMEDIAOPTF3IMM8_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPMEDIAOPTF3IMM8 const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPMEDIAOPTF3IMM8_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPMEDIAOPTF3IMM8_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPMEDIAOPTF3IMM8_INIT_VARS_EX */
#define IEMOPMEDIAOPTF3IMM8_INIT_VARS(a_InstrNm) \
    IEMOPMEDIAOPTF3IMM8_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                                     RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))
/** @} */


/**
 * Function table for blend type instruction taking three full sized media source
 * registers and one full sized destination register, but no additional state
 * (AVX).
 */
typedef struct IEMOPBLENDOP
{
    PFNIEMAIMPLAVXBLENDU128 pfnU128;
    PFNIEMAIMPLAVXBLENDU256 pfnU256;
} IEMOPBLENDOP;
/** Pointer to a media operation function table for 4 full sized ops (AVX). */
typedef IEMOPBLENDOP const *PCIEMOPBLENDOP;

/** @def IEMOPBLENDOP_INIT_VARS_EX
 * Declares a s_Host (x86 & amd64 only) and a s_Fallback variable with the
 * given functions as initializers.  For use in AVX functions where a pair of
 * functions are only used once and the function table need not be public. */
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
#  define IEMOPBLENDOP_INIT_VARS_EX(a_pfnHostU128, a_pfnHostU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPBLENDOP const s_Host     = { a_pfnHostU128,     a_pfnHostU256 }; \
    static IEMOPBLENDOP const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# else
#  define IEMOPBLENDOP_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) \
    static IEMOPBLENDOP const s_Fallback = { a_pfnFallbackU128, a_pfnFallbackU256 }
# endif
#else
# define IEMOPBLENDOP_INIT_VARS_EX(a_pfnU128, a_pfnU256, a_pfnFallbackU128, a_pfnFallbackU256) (void)0
#endif
/** @def IEMOPBLENDOP_INIT_VARS
 * Generate AVX function tables for the @a a_InstrNm instruction.
 * @sa IEMOPBLENDOP_INIT_VARS_EX */
#define IEMOPBLENDOP_INIT_VARS(a_InstrNm) \
    IEMOPBLENDOP_INIT_VARS_EX(RT_CONCAT3(iemAImpl_,a_InstrNm,_u128),           RT_CONCAT3(iemAImpl_,a_InstrNm,_u256),\
                              RT_CONCAT3(iemAImpl_,a_InstrNm,_u128_fallback),  RT_CONCAT3(iemAImpl_,a_InstrNm,_u256_fallback))


/** @name SSE/AVX single/double precision floating point operations.
 * @{ */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPSSEF2U128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2));
typedef FNIEMAIMPLFPSSEF2U128  *PFNIEMAIMPLFPSSEF2U128;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPSSEF2U128R32,(uint32_t uMxCsrIn, PX86XMMREG Result, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2));
typedef FNIEMAIMPLFPSSEF2U128R32  *PFNIEMAIMPLFPSSEF2U128R32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPSSEF2U128R64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2));
typedef FNIEMAIMPLFPSSEF2U128R64  *PFNIEMAIMPLFPSSEF2U128R64;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPAVXF3U128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2));
typedef FNIEMAIMPLFPAVXF3U128  *PFNIEMAIMPLFPAVXF3U128;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPAVXF3U128R32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2));
typedef FNIEMAIMPLFPAVXF3U128R32  *PFNIEMAIMPLFPAVXF3U128R32;
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPAVXF3U128R64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2));
typedef FNIEMAIMPLFPAVXF3U128R64  *PFNIEMAIMPLFPAVXF3U128R64;

typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLFPAVXF3U256,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2));
typedef FNIEMAIMPLFPAVXF3U256  *PFNIEMAIMPLFPAVXF3U256;

FNIEMAIMPLFPSSEF2U128 iemAImpl_addps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_addpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_mulps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_mulpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_subps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_subpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_minps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_minpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_divps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_divpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_maxps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_maxpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_haddps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_haddpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_hsubps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_hsubpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_sqrtps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_rsqrtps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_sqrtpd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_rcpps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_addsubps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_addsubpd_u128;

FNIEMAIMPLFPSSEF2U128 iemAImpl_cvtpd2ps_u128;
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_cvtps2pd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, uint64_t const *pu64Src));

FNIEMAIMPLFPSSEF2U128 iemAImpl_cvtdq2ps_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_cvtps2dq_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_cvttps2dq_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_cvttpd2dq_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_cvtdq2pd_u128;
FNIEMAIMPLFPSSEF2U128 iemAImpl_cvtpd2dq_u128;

FNIEMAIMPLFPSSEF2U128R32 iemAImpl_addss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_addsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_mulss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_mulsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_subss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_subsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_minss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_minsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_divss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_divsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_maxss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_maxsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_cvtss2sd_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_cvtsd2ss_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_sqrtss_u128_r32;
FNIEMAIMPLFPSSEF2U128R64 iemAImpl_sqrtsd_u128_r64;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_rsqrtss_u128_r32;
FNIEMAIMPLFPSSEF2U128R32 iemAImpl_rcpss_u128_r32;

FNIEMAIMPLMEDIAF3U128 iemAImpl_vaddps_u128, iemAImpl_vaddps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vaddpd_u128, iemAImpl_vaddpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vmulps_u128, iemAImpl_vmulps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vmulpd_u128, iemAImpl_vmulpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vsubps_u128, iemAImpl_vsubps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vsubpd_u128, iemAImpl_vsubpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vminps_u128, iemAImpl_vminps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vminpd_u128, iemAImpl_vminpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vdivps_u128, iemAImpl_vdivps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vdivpd_u128, iemAImpl_vdivpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vmaxps_u128, iemAImpl_vmaxps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vmaxpd_u128, iemAImpl_vmaxpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vhaddps_u128, iemAImpl_vhaddps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vhaddpd_u128, iemAImpl_vhaddpd_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vhsubps_u128, iemAImpl_vhsubps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vhsubpd_u128, iemAImpl_vhsubpd_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vsqrtps_u128, iemAImpl_vsqrtps_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vsqrtpd_u128, iemAImpl_vsqrtpd_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vrsqrtps_u128,  iemAImpl_vrsqrtps_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vrcpps_u128,    iemAImpl_vrcpps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vaddsubps_u128, iemAImpl_vaddsubps_u128_fallback;
FNIEMAIMPLMEDIAF3U128 iemAImpl_vaddsubpd_u128, iemAImpl_vaddsubpd_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vcvtdq2ps_u128, iemAImpl_vcvtdq2ps_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vcvtps2dq_u128, iemAImpl_vcvtps2dq_u128_fallback;
FNIEMAIMPLMEDIAF2U128 iemAImpl_vcvttps2dq_u128, iemAImpl_vcvttps2dq_u128_fallback;
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2ps_u128_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2ps_u128_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvttpd2dq_u128_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvttpd2dq_u128_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2dq_u128_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2dq_u128_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc));


FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vaddss_u128_r32, iemAImpl_vaddss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vaddsd_u128_r64, iemAImpl_vaddsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vmulss_u128_r32, iemAImpl_vmulss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vmulsd_u128_r64, iemAImpl_vmulsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vsubss_u128_r32, iemAImpl_vsubss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vsubsd_u128_r64, iemAImpl_vsubsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vminss_u128_r32, iemAImpl_vminss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vminsd_u128_r64, iemAImpl_vminsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vdivss_u128_r32, iemAImpl_vdivss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vdivsd_u128_r64, iemAImpl_vdivsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vmaxss_u128_r32, iemAImpl_vmaxss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vmaxsd_u128_r64, iemAImpl_vmaxsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vsqrtss_u128_r32, iemAImpl_vsqrtss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vsqrtsd_u128_r64, iemAImpl_vsqrtsd_u128_r64_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vrsqrtss_u128_r32, iemAImpl_vrsqrtss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vrcpss_u128_r32,   iemAImpl_vrcpss_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R32 iemAImpl_vcvtss2sd_u128_r32, iemAImpl_vcvtss2sd_u128_r32_fallback;
FNIEMAIMPLFPAVXF3U128R64 iemAImpl_vcvtsd2ss_u128_r64, iemAImpl_vcvtsd2ss_u128_r64_fallback;


FNIEMAIMPLFPAVXF3U256 iemAImpl_vaddps_u256, iemAImpl_vaddps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vaddpd_u256, iemAImpl_vaddpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vmulps_u256, iemAImpl_vmulps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vmulpd_u256, iemAImpl_vmulpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vsubps_u256, iemAImpl_vsubps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vsubpd_u256, iemAImpl_vsubpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vminps_u256, iemAImpl_vminps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vminpd_u256, iemAImpl_vminpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vdivps_u256, iemAImpl_vdivps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vdivpd_u256, iemAImpl_vdivpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vmaxps_u256, iemAImpl_vmaxps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vmaxpd_u256, iemAImpl_vmaxpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vhaddps_u256, iemAImpl_vhaddps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vhaddpd_u256, iemAImpl_vhaddpd_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vhsubps_u256, iemAImpl_vhsubps_u256_fallback;
FNIEMAIMPLFPAVXF3U256 iemAImpl_vhsubpd_u256, iemAImpl_vhsubpd_u256_fallback;
FNIEMAIMPLMEDIAF3U256 iemAImpl_vaddsubps_u256, iemAImpl_vaddsubps_u256_fallback;
FNIEMAIMPLMEDIAF3U256 iemAImpl_vaddsubpd_u256, iemAImpl_vaddsubpd_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vsqrtps_u256, iemAImpl_vsqrtps_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vsqrtpd_u256, iemAImpl_vsqrtpd_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vrsqrtps_u256,  iemAImpl_vrsqrtps_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vrcpps_u256,    iemAImpl_vrcpps_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vcvtdq2ps_u256,  iemAImpl_vcvtdq2ps_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vcvtps2dq_u256,  iemAImpl_vcvtps2dq_u256_fallback;
FNIEMAIMPLMEDIAF2U256 iemAImpl_vcvttps2dq_u256, iemAImpl_vcvttps2dq_u256_fallback;
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2ps_u128_u256,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2ps_u128_u256_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvttpd2dq_u128_u256,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvttpd2dq_u128_u256_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2dq_u128_u256,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc));
IEM_DECL_IMPL_PROTO(uint32_t, iemAImpl_vcvtpd2dq_u128_u256_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc));
/** @} */

/** @name C instruction implementations for anything slightly complicated.
 * @{ */

/**
 * For typedef'ing or declaring a C instruction implementation function taking
 * no extra arguments.
 *
 * @param   a_Name              The name of the type.
 */
# define IEM_CIMPL_DECL_TYPE_0(a_Name) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr))
/**
 * For defining a C instruction implementation function taking no extra
 * arguments.
 *
 * @param   a_Name              The name of the function
 */
# define IEM_CIMPL_DEF_0(a_Name) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr))
/**
 * Prototype version of IEM_CIMPL_DEF_0.
 */
# define IEM_CIMPL_PROTO_0(a_Name) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr))
/**
 * For calling a C instruction implementation function taking no extra
 * arguments.
 *
 * This special call macro adds default arguments to the call and allow us to
 * change these later.
 *
 * @param   a_fn                The name of the function.
 */
# define IEM_CIMPL_CALL_0(a_fn)            a_fn(pVCpu, cbInstr)

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
# define IEM_CIMPL_DECL_TYPE_1(a_Name, a_Type0, a_Arg0) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0))
/**
 * For defining a C instruction implementation function taking one extra
 * argument.
 *
 * @param   a_Name              The name of the function
 * @param   a_Type0             The argument type.
 * @param   a_Arg0              The argument name.
 */
# define IEM_CIMPL_DEF_1(a_Name, a_Type0, a_Arg0) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0))
/**
 * Prototype version of IEM_CIMPL_DEF_1.
 */
# define IEM_CIMPL_PROTO_1(a_Name, a_Type0, a_Arg0) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0))
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
# define IEM_CIMPL_CALL_1(a_fn, a0)        a_fn(pVCpu, cbInstr, (a0))

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
# define IEM_CIMPL_DECL_TYPE_2(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1))
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
# define IEM_CIMPL_DEF_2(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1))
/**
 * Prototype version of IEM_CIMPL_DEF_2.
 */
# define IEM_CIMPL_PROTO_2(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1))
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
# define IEM_CIMPL_CALL_2(a_fn, a0, a1)    a_fn(pVCpu, cbInstr, (a0), (a1))

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
# define IEM_CIMPL_DECL_TYPE_3(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2))
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
# define IEM_CIMPL_DEF_3(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2))
/**
 * Prototype version of IEM_CIMPL_DEF_3.
 */
# define IEM_CIMPL_PROTO_3(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2))
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
# define IEM_CIMPL_CALL_3(a_fn, a0, a1, a2) a_fn(pVCpu, cbInstr, (a0), (a1), (a2))


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
# define IEM_CIMPL_DECL_TYPE_4(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, a_Type2 a_Arg2, a_Type3 a_Arg3))
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
# define IEM_CIMPL_DEF_4(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, \
                                             a_Type2 a_Arg2, a_Type3 a_Arg3))
/**
 * Prototype version of IEM_CIMPL_DEF_4.
 */
# define IEM_CIMPL_PROTO_4(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, \
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
# define IEM_CIMPL_CALL_4(a_fn, a0, a1, a2, a3) a_fn(pVCpu, cbInstr, (a0), (a1), (a2), (a3))


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
# define IEM_CIMPL_DECL_TYPE_5(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3, a_Type4, a_Arg4) \
    IEM_DECL_IMPL_TYPE(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, \
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
# define IEM_CIMPL_DEF_5(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3, a_Type4, a_Arg4) \
    IEM_DECL_IMPL_DEF(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, \
                                             a_Type2 a_Arg2, a_Type3 a_Arg3, a_Type4 a_Arg4))
/**
 * Prototype version of IEM_CIMPL_DEF_5.
 */
# define IEM_CIMPL_PROTO_5(a_Name, a_Type0, a_Arg0, a_Type1, a_Arg1, a_Type2, a_Arg2, a_Type3, a_Arg3, a_Type4, a_Arg4) \
    IEM_DECL_IMPL_PROTO(VBOXSTRICTRC, a_Name, (PVMCPUCC pVCpu, uint8_t cbInstr, a_Type0 a_Arg0, a_Type1 a_Arg1, \
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
# define IEM_CIMPL_CALL_5(a_fn, a0, a1, a2, a3, a4) a_fn(pVCpu, cbInstr, (a0), (a1), (a2), (a3), (a4))

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

#elif defined(_MSC_VER) && defined(RT_ARCH_X86)
typedef VBOXSTRICTRC (__fastcall * PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (__fastcall * PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1) IEM_NOEXCEPT_MAY_LONGJMP

#elif defined(__GNUC__) && !defined(IEM_WITH_THROW_CATCH)
typedef VBOXSTRICTRC (* PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (* PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu)
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0)
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1)

#else
typedef VBOXSTRICTRC (* PFNIEMOP)(PVMCPUCC pVCpu);
typedef VBOXSTRICTRC (* PFNIEMOPRM)(PVMCPUCC pVCpu, uint8_t bRm);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC a_Name(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1) IEM_NOEXCEPT_MAY_LONGJMP

#endif
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
 * Gets the CPU mode (from fExec) as a IEMMODE value.
 *
 * @returns IEMMODE
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_GET_CPU_MODE(a_pVCpu)           ((a_pVCpu)->iem.s.fExec & IEM_F_MODE_CPUMODE_MASK)

/**
 * Check if we're currently executing in real or virtual 8086 mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_REAL_OR_V86_MODE(a_pVCpu)    ((  ((a_pVCpu)->iem.s.fExec  ^ IEM_F_MODE_X86_PROT_MASK) \
                                              & (IEM_F_MODE_X86_V86_MASK | IEM_F_MODE_X86_PROT_MASK)) != 0)

/**
 * Check if we're currently executing in virtual 8086 mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_V86_MODE(a_pVCpu)            (((a_pVCpu)->iem.s.fExec & IEM_F_MODE_X86_V86_MASK) != 0)

/**
 * Check if we're currently executing in long mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_LONG_MODE(a_pVCpu)           (CPUMIsGuestInLongModeEx(IEM_GET_CTX(a_pVCpu)))

/**
 * Check if we're currently executing in a 16-bit code segment.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_16BIT_CODE(a_pVCpu)          (IEM_GET_CPU_MODE(a_pVCpu) == IEMMODE_16BIT)

/**
 * Check if we're currently executing in a 32-bit code segment.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_32BIT_CODE(a_pVCpu)          (IEM_GET_CPU_MODE(a_pVCpu) == IEMMODE_32BIT)

/**
 * Check if we're currently executing in a 64-bit code segment.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_64BIT_CODE(a_pVCpu)          (IEM_GET_CPU_MODE(a_pVCpu) == IEMMODE_64BIT)

/**
 * Check if we're currently executing in real mode.
 *
 * @returns @c true if it is, @c false if not.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_IS_REAL_MODE(a_pVCpu)           (!((a_pVCpu)->iem.s.fExec & IEM_F_MODE_X86_PROT_MASK))

/**
 * Gets the current protection level (CPL).
 *
 * @returns 0..3
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_GET_CPL(a_pVCpu)                (((a_pVCpu)->iem.s.fExec >> IEM_F_X86_CPL_SHIFT) & IEM_F_X86_CPL_SMASK)

/**
 * Sets the current protection level (CPL).
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_SET_CPL(a_pVCpu, a_uCpl) \
    do { (a_pVCpu)->iem.s.fExec = ((a_pVCpu)->iem.s.fExec & ~IEM_F_X86_CPL_MASK) | ((a_uCpl) << IEM_F_X86_CPL_SHIFT); } while (0)

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

/**
 * Evaluates to true if we're presenting an Intel CPU to the guest.
 */
#define IEM_IS_GUEST_CPU_INTEL(a_pVCpu)     ( (a_pVCpu)->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL )

/**
 * Evaluates to true if we're presenting an AMD CPU to the guest.
 */
#define IEM_IS_GUEST_CPU_AMD(a_pVCpu)       ( (a_pVCpu)->iem.s.enmCpuVendor == CPUMCPUVENDOR_AMD || (a_pVCpu)->iem.s.enmCpuVendor == CPUMCPUVENDOR_HYGON )

/**
 * Check if the address is canonical.
 */
#define IEM_IS_CANONICAL(a_u64Addr)         X86_IS_CANONICAL(a_u64Addr)

/** Checks if the ModR/M byte is in register mode or not.  */
#define IEM_IS_MODRM_REG_MODE(a_bRm)        ( ((a_bRm) & X86_MODRM_MOD_MASK) == (3 << X86_MODRM_MOD_SHIFT) )
/** Checks if the ModR/M byte is in memory mode or not.  */
#define IEM_IS_MODRM_MEM_MODE(a_bRm)        ( ((a_bRm) & X86_MODRM_MOD_MASK) != (3 << X86_MODRM_MOD_SHIFT) )

/**
 * Gets the register (reg) part of a ModR/M encoding, with REX.R added in.
 *
 * For use during decoding.
 */
#define IEM_GET_MODRM_REG(a_pVCpu, a_bRm)   ( (((a_bRm) >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) | (a_pVCpu)->iem.s.uRexReg )
/**
 * Gets the r/m part of a ModR/M encoding as a register index, with REX.B added in.
 *
 * For use during decoding.
 */
#define IEM_GET_MODRM_RM(a_pVCpu, a_bRm)    ( ((a_bRm) & X86_MODRM_RM_MASK) | (a_pVCpu)->iem.s.uRexB )

/**
 * Gets the register (reg) part of a ModR/M encoding, without REX.R.
 *
 * For use during decoding.
 */
#define IEM_GET_MODRM_REG_8(a_bRm)          ( (((a_bRm) >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) )
/**
 * Gets the r/m part of a ModR/M encoding as a register index, without REX.B.
 *
 * For use during decoding.
 */
#define IEM_GET_MODRM_RM_8(a_bRm)           ( ((a_bRm) & X86_MODRM_RM_MASK) )

/**
 * Gets the register (reg) part of a ModR/M encoding as an extended 8-bit
 * register index, with REX.R added in.
 *
 * For use during decoding.
 *
 * @see iemGRegRefU8Ex, iemGRegFetchU8Ex, iemGRegStoreU8Ex
 */
#define IEM_GET_MODRM_REG_EX8(a_pVCpu, a_bRm) \
    (   (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_REX) \
     || !((a_bRm) & (4 << X86_MODRM_REG_SHIFT)) /* IEM_GET_MODRM_REG(pVCpu, a_bRm) < 4 */ \
     ? IEM_GET_MODRM_REG(pVCpu, a_bRm) : (((a_bRm) >> X86_MODRM_REG_SHIFT) & 3) | 16)
/**
 * Gets the r/m part of a ModR/M encoding as an extended 8-bit register index,
 * with REX.B added in.
 *
 * For use during decoding.
 *
 * @see iemGRegRefU8Ex, iemGRegFetchU8Ex, iemGRegStoreU8Ex
 */
#define IEM_GET_MODRM_RM_EX8(a_pVCpu, a_bRm) \
    (   (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_REX) \
     || !((a_bRm) & 4) /* IEM_GET_MODRM_RM(pVCpu, a_bRm) < 4 */ \
     ? IEM_GET_MODRM_RM(pVCpu, a_bRm) : ((a_bRm) & 3) | 16)

/**
 * Combines the prefix REX and ModR/M byte for passing to
 * iemOpHlpCalcRmEffAddrThreadedAddr64().
 *
 * @returns The ModRM byte but with bit 3 set to REX.B and bit 4 to REX.X.
 *          The two bits are part of the REG sub-field, which isn't needed in
 *          iemOpHlpCalcRmEffAddrThreadedAddr64().
 *
 * For use during decoding/recompiling.
 */
#define IEM_GET_MODRM_EX(a_pVCpu, a_bRm) \
    (  ((a_bRm) & ~X86_MODRM_REG_MASK) \
     | (uint8_t)( (pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_REX_B | IEM_OP_PRF_REX_X)) >> (25 - 3) ) )
AssertCompile(IEM_OP_PRF_REX_B == RT_BIT_32(25));
AssertCompile(IEM_OP_PRF_REX_X == RT_BIT_32(26));

/**
 * Gets the effective VEX.VVVV value.
 *
 * The 4th bit is ignored if not 64-bit code.
 * @returns effective V-register value.
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling thread.
 */
#define IEM_GET_EFFECTIVE_VVVV(a_pVCpu) \
    (IEM_IS_64BIT_CODE(a_pVCpu) ? (a_pVCpu)->iem.s.uVex3rdReg : (a_pVCpu)->iem.s.uVex3rdReg & 7)


/**
 * Gets the register (reg) part of a the special 4th register byte used by
 * vblendvps and vblendvpd.
 *
 * For use during decoding.
 */
#define IEM_GET_IMM8_REG(a_pVCpu, a_bRegImm8) \
    (IEM_IS_64BIT_CODE(a_pVCpu) ? (a_bRegImm8) >> 4 : ((a_bRegImm8) >> 4) & 7)


/**
 * Checks if we're executing inside an AMD-V or VT-x guest.
 */
#if defined(VBOX_WITH_NESTED_HWVIRT_VMX) || defined(VBOX_WITH_NESTED_HWVIRT_SVM)
# define IEM_IS_IN_GUEST(a_pVCpu)       RT_BOOL((a_pVCpu)->iem.s.fExec & IEM_F_X86_CTX_IN_GUEST)
#else
# define IEM_IS_IN_GUEST(a_pVCpu)       false
#endif


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX

/**
 * Check if the guest has entered VMX root operation.
 */
# define IEM_VMX_IS_ROOT_MODE(a_pVCpu)      (CPUMIsGuestInVmxRootMode(IEM_GET_CTX(a_pVCpu)))

/**
 * Check if the guest has entered VMX non-root operation.
 */
# define IEM_VMX_IS_NON_ROOT_MODE(a_pVCpu)  (   ((a_pVCpu)->iem.s.fExec & (IEM_F_X86_CTX_VMX | IEM_F_X86_CTX_IN_GUEST)) \
                                             ==                           (IEM_F_X86_CTX_VMX | IEM_F_X86_CTX_IN_GUEST) )

/**
 * Check if the nested-guest has the given Pin-based VM-execution control set.
 */
# define IEM_VMX_IS_PINCTLS_SET(a_pVCpu, a_PinCtl)  (CPUMIsGuestVmxPinCtlsSet(IEM_GET_CTX(a_pVCpu), (a_PinCtl)))

/**
 * Check if the nested-guest has the given Processor-based VM-execution control set.
 */
# define IEM_VMX_IS_PROCCTLS_SET(a_pVCpu, a_ProcCtl) (CPUMIsGuestVmxProcCtlsSet(IEM_GET_CTX(a_pVCpu), (a_ProcCtl)))

/**
 * Check if the nested-guest has the given Secondary Processor-based VM-execution
 * control set.
 */
# define IEM_VMX_IS_PROCCTLS2_SET(a_pVCpu, a_ProcCtl2) (CPUMIsGuestVmxProcCtls2Set(IEM_GET_CTX(a_pVCpu), (a_ProcCtl2)))

/** Gets the guest-physical address of the shadows VMCS for the given VCPU. */
# define IEM_VMX_GET_SHADOW_VMCS(a_pVCpu)           ((a_pVCpu)->cpum.GstCtx.hwvirt.vmx.GCPhysShadowVmcs)

/** Whether a shadow VMCS is present for the given VCPU. */
# define IEM_VMX_HAS_SHADOW_VMCS(a_pVCpu)           RT_BOOL(IEM_VMX_GET_SHADOW_VMCS(a_pVCpu) != NIL_RTGCPHYS)

/** Gets the VMXON region pointer. */
# define IEM_VMX_GET_VMXON_PTR(a_pVCpu)             ((a_pVCpu)->cpum.GstCtx.hwvirt.vmx.GCPhysVmxon)

/** Gets the guest-physical address of the current VMCS for the given VCPU. */
# define IEM_VMX_GET_CURRENT_VMCS(a_pVCpu)          ((a_pVCpu)->cpum.GstCtx.hwvirt.vmx.GCPhysVmcs)

/** Whether a current VMCS is present for the given VCPU. */
# define IEM_VMX_HAS_CURRENT_VMCS(a_pVCpu)          RT_BOOL(IEM_VMX_GET_CURRENT_VMCS(a_pVCpu) != NIL_RTGCPHYS)

/** Assigns the guest-physical address of the current VMCS for the given VCPU. */
# define IEM_VMX_SET_CURRENT_VMCS(a_pVCpu, a_GCPhysVmcs) \
    do \
    { \
        Assert((a_GCPhysVmcs) != NIL_RTGCPHYS); \
        (a_pVCpu)->cpum.GstCtx.hwvirt.vmx.GCPhysVmcs = (a_GCPhysVmcs); \
    } while (0)

/** Clears any current VMCS for the given VCPU. */
# define IEM_VMX_CLEAR_CURRENT_VMCS(a_pVCpu) \
    do \
    { \
        (a_pVCpu)->cpum.GstCtx.hwvirt.vmx.GCPhysVmcs = NIL_RTGCPHYS; \
    } while (0)

/**
 * Invokes the VMX VM-exit handler for an instruction intercept.
 */
# define IEM_VMX_VMEXIT_INSTR_RET(a_pVCpu, a_uExitReason, a_cbInstr) \
    do { return iemVmxVmexitInstr((a_pVCpu), (a_uExitReason), (a_cbInstr)); } while (0)

/**
 * Invokes the VMX VM-exit handler for an instruction intercept where the
 * instruction provides additional VM-exit information.
 */
# define IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(a_pVCpu, a_uExitReason, a_uInstrId, a_cbInstr) \
    do { return iemVmxVmexitInstrNeedsInfo((a_pVCpu), (a_uExitReason), (a_uInstrId), (a_cbInstr)); } while (0)

/**
 * Invokes the VMX VM-exit handler for a task switch.
 */
# define IEM_VMX_VMEXIT_TASK_SWITCH_RET(a_pVCpu, a_enmTaskSwitch, a_SelNewTss, a_cbInstr) \
    do { return iemVmxVmexitTaskSwitch((a_pVCpu), (a_enmTaskSwitch), (a_SelNewTss), (a_cbInstr)); } while (0)

/**
 * Invokes the VMX VM-exit handler for MWAIT.
 */
# define IEM_VMX_VMEXIT_MWAIT_RET(a_pVCpu, a_fMonitorArmed, a_cbInstr) \
    do { return iemVmxVmexitInstrMwait((a_pVCpu), (a_fMonitorArmed), (a_cbInstr)); } while (0)

/**
 * Invokes the VMX VM-exit handler for EPT faults.
 */
# define IEM_VMX_VMEXIT_EPT_RET(a_pVCpu, a_pPtWalk, a_fAccess, a_fSlatFail, a_cbInstr) \
    do { return iemVmxVmexitEpt(a_pVCpu, a_pPtWalk, a_fAccess, a_fSlatFail, a_cbInstr); } while (0)

/**
 * Invokes the VMX VM-exit handler.
 */
# define IEM_VMX_VMEXIT_TRIPLE_FAULT_RET(a_pVCpu, a_uExitReason, a_uExitQual) \
    do { return iemVmxVmexit((a_pVCpu), (a_uExitReason), (a_uExitQual)); } while (0)

#else
# define IEM_VMX_IS_ROOT_MODE(a_pVCpu)                                          (false)
# define IEM_VMX_IS_NON_ROOT_MODE(a_pVCpu)                                      (false)
# define IEM_VMX_IS_PINCTLS_SET(a_pVCpu, a_cbInstr)                             (false)
# define IEM_VMX_IS_PROCCTLS_SET(a_pVCpu, a_cbInstr)                            (false)
# define IEM_VMX_IS_PROCCTLS2_SET(a_pVCpu, a_cbInstr)                           (false)
# define IEM_VMX_VMEXIT_INSTR_RET(a_pVCpu, a_uExitReason, a_cbInstr)            do { return VERR_VMX_IPE_1; } while (0)
# define IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(a_pVCpu, a_uExitReason, a_uInstrId, a_cbInstr)  do { return VERR_VMX_IPE_1; } while (0)
# define IEM_VMX_VMEXIT_TASK_SWITCH_RET(a_pVCpu, a_enmTaskSwitch, a_SelNewTss, a_cbInstr)    do { return VERR_VMX_IPE_1; } while (0)
# define IEM_VMX_VMEXIT_MWAIT_RET(a_pVCpu, a_fMonitorArmed, a_cbInstr)          do { return VERR_VMX_IPE_1; } while (0)
# define IEM_VMX_VMEXIT_EPT_RET(a_pVCpu, a_pPtWalk, a_fAccess, a_fSlatFail, a_cbInstr)       do { return VERR_VMX_IPE_1; } while (0)
# define IEM_VMX_VMEXIT_TRIPLE_FAULT_RET(a_pVCpu, a_uExitReason, a_uExitQual)   do { return VERR_VMX_IPE_1; } while (0)

#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/**
 * Checks if we're executing a guest using AMD-V.
 */
# define IEM_SVM_IS_IN_GUEST(a_pVCpu) (   (a_pVCpu->iem.s.fExec & (IEM_F_X86_CTX_SVM | IEM_F_X86_CTX_IN_GUEST)) \
                                       ==                         (IEM_F_X86_CTX_SVM | IEM_F_X86_CTX_IN_GUEST))
/**
 * Check if an SVM control/instruction intercept is set.
 */
# define IEM_SVM_IS_CTRL_INTERCEPT_SET(a_pVCpu, a_Intercept) \
    (IEM_SVM_IS_IN_GUEST(a_pVCpu) && CPUMIsGuestSvmCtrlInterceptSet(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_Intercept)))

/**
 * Check if an SVM read CRx intercept is set.
 */
# define IEM_SVM_IS_READ_CR_INTERCEPT_SET(a_pVCpu, a_uCr) \
    (IEM_SVM_IS_IN_GUEST(a_pVCpu) && CPUMIsGuestSvmReadCRxInterceptSet(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_uCr)))

/**
 * Check if an SVM write CRx intercept is set.
 */
# define IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(a_pVCpu, a_uCr) \
    (IEM_SVM_IS_IN_GUEST(a_pVCpu) && CPUMIsGuestSvmWriteCRxInterceptSet(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_uCr)))

/**
 * Check if an SVM read DRx intercept is set.
 */
# define IEM_SVM_IS_READ_DR_INTERCEPT_SET(a_pVCpu, a_uDr) \
    (IEM_SVM_IS_IN_GUEST(a_pVCpu) && CPUMIsGuestSvmReadDRxInterceptSet(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_uDr)))

/**
 * Check if an SVM write DRx intercept is set.
 */
# define IEM_SVM_IS_WRITE_DR_INTERCEPT_SET(a_pVCpu, a_uDr) \
    (IEM_SVM_IS_IN_GUEST(a_pVCpu) && CPUMIsGuestSvmWriteDRxInterceptSet(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_uDr)))

/**
 * Check if an SVM exception intercept is set.
 */
# define IEM_SVM_IS_XCPT_INTERCEPT_SET(a_pVCpu, a_uVector) \
    (IEM_SVM_IS_IN_GUEST(a_pVCpu) && CPUMIsGuestSvmXcptInterceptSet(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_uVector)))

/**
 * Invokes the SVM \#VMEXIT handler for the nested-guest.
 */
# define IEM_SVM_VMEXIT_RET(a_pVCpu, a_uExitCode, a_uExitInfo1, a_uExitInfo2) \
    do { return iemSvmVmexit((a_pVCpu), (a_uExitCode), (a_uExitInfo1), (a_uExitInfo2)); } while (0)

/**
 * Invokes the 'MOV CRx' SVM \#VMEXIT handler after constructing the
 * corresponding decode assist information.
 */
# define IEM_SVM_CRX_VMEXIT_RET(a_pVCpu, a_uExitCode, a_enmAccessCrX, a_iGReg) \
    do \
    { \
        uint64_t uExitInfo1; \
        if (   IEM_GET_GUEST_CPU_FEATURES(a_pVCpu)->fSvmDecodeAssists \
            && (a_enmAccessCrX) == IEMACCESSCRX_MOV_CRX) \
            uExitInfo1 = SVM_EXIT1_MOV_CRX_MASK | ((a_iGReg) & 7); \
        else \
            uExitInfo1 = 0; \
        IEM_SVM_VMEXIT_RET(a_pVCpu, a_uExitCode, uExitInfo1, 0); \
    } while (0)

/** Check and handles SVM nested-guest instruction intercept and updates
 *  NRIP if needed.
 */
# define IEM_SVM_CHECK_INSTR_INTERCEPT(a_pVCpu, a_Intercept, a_uExitCode, a_uExitInfo1, a_uExitInfo2, a_cbInstr) \
    do \
    { \
        if (IEM_SVM_IS_CTRL_INTERCEPT_SET(a_pVCpu, a_Intercept)) \
        { \
            IEM_SVM_UPDATE_NRIP(a_pVCpu, a_cbInstr); \
            IEM_SVM_VMEXIT_RET(a_pVCpu, a_uExitCode, a_uExitInfo1, a_uExitInfo2); \
        } \
    } while (0)

/** Checks and handles SVM nested-guest CR0 read intercept. */
# define IEM_SVM_CHECK_READ_CR0_INTERCEPT(a_pVCpu, a_uExitInfo1, a_uExitInfo2, a_cbInstr) \
    do \
    { \
        if (!IEM_SVM_IS_READ_CR_INTERCEPT_SET(a_pVCpu, 0)) \
        { /* probably likely */ } \
        else \
        { \
            IEM_SVM_UPDATE_NRIP(a_pVCpu, a_cbInstr); \
            IEM_SVM_VMEXIT_RET(a_pVCpu, SVM_EXIT_READ_CR0, a_uExitInfo1, a_uExitInfo2); \
        } \
    } while (0)

/**
 * Updates the NextRIP (NRI) field in the nested-guest VMCB.
 */
# define IEM_SVM_UPDATE_NRIP(a_pVCpu, a_cbInstr) \
    do { \
        if (IEM_GET_GUEST_CPU_FEATURES(a_pVCpu)->fSvmNextRipSave) \
            CPUMGuestSvmUpdateNRip(a_pVCpu, IEM_GET_CTX(a_pVCpu), (a_cbInstr)); \
    } while (0)

#else
# define IEM_SVM_IS_CTRL_INTERCEPT_SET(a_pVCpu, a_Intercept)                                (false)
# define IEM_SVM_IS_READ_CR_INTERCEPT_SET(a_pVCpu, a_uCr)                                   (false)
# define IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(a_pVCpu, a_uCr)                                  (false)
# define IEM_SVM_IS_READ_DR_INTERCEPT_SET(a_pVCpu, a_uDr)                                   (false)
# define IEM_SVM_IS_WRITE_DR_INTERCEPT_SET(a_pVCpu, a_uDr)                                  (false)
# define IEM_SVM_IS_XCPT_INTERCEPT_SET(a_pVCpu, a_uVector)                                  (false)
# define IEM_SVM_VMEXIT_RET(a_pVCpu, a_uExitCode, a_uExitInfo1, a_uExitInfo2)               do { return VERR_SVM_IPE_1; } while (0)
# define IEM_SVM_CRX_VMEXIT_RET(a_pVCpu, a_uExitCode, a_enmAccessCrX, a_iGReg)              do { return VERR_SVM_IPE_1; } while (0)
# define IEM_SVM_CHECK_INSTR_INTERCEPT(a_pVCpu, a_Intercept, a_uExitCode, \
                                       a_uExitInfo1, a_uExitInfo2, a_cbInstr)               do { } while (0)
# define IEM_SVM_CHECK_READ_CR0_INTERCEPT(a_pVCpu, a_uExitInfo1, a_uExitInfo2, a_cbInstr)   do { } while (0)
# define IEM_SVM_UPDATE_NRIP(a_pVCpu, a_cbInstr)                                            do { } while (0)

#endif

/** @} */

uint32_t                iemCalcExecDbgFlagsSlow(PVMCPUCC pVCpu);
VBOXSTRICTRC            iemExecInjectPendingTrap(PVMCPUCC pVCpu);


/**
 * Selector descriptor table entry as fetched by iemMemFetchSelDesc.
 */
typedef union IEMSELDESC
{
    /** The legacy view. */
    X86DESC     Legacy;
    /** The long mode view. */
    X86DESC64   Long;
} IEMSELDESC;
/** Pointer to a selector descriptor table entry. */
typedef IEMSELDESC *PIEMSELDESC;

/** @name  Raising Exceptions.
 * @{ */
VBOXSTRICTRC            iemTaskSwitch(PVMCPUCC pVCpu, IEMTASKSWITCH enmTaskSwitch, uint32_t uNextEip, uint32_t fFlags,
                                      uint16_t uErr, uint64_t uCr2, RTSEL SelTSS, PIEMSELDESC pNewDescTSS) RT_NOEXCEPT;

VBOXSTRICTRC            iemRaiseXcptOrInt(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector, uint32_t fFlags,
                                          uint16_t uErr, uint64_t uCr2) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseXcptOrIntJmp(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector,
                                             uint32_t fFlags, uint16_t uErr, uint64_t uCr2) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseDivideError(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseDivideErrorJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseDebugException(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseBoundRangeExceeded(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseUndefinedOpcode(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseUndefinedOpcodeJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseDeviceNotAvailable(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseDeviceNotAvailableJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseTaskSwitchFaultWithErr(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseTaskSwitchFaultCurrentTSS(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseTaskSwitchFault0(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseTaskSwitchFaultBySelector(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT;
/*VBOXSTRICTRC            iemRaiseSelectorNotPresent(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) RT_NOEXCEPT;*/
VBOXSTRICTRC            iemRaiseSelectorNotPresentWithErr(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseSelectorNotPresentBySelector(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseStackSelectorNotPresentBySelector(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseStackSelectorNotPresentWithErr(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseGeneralProtectionFault(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseGeneralProtectionFault0(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseGeneralProtectionFault0Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseGeneralProtectionFaultBySelector(PVMCPUCC pVCpu, RTSEL Sel) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseNotCanonical(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseSelectorBounds(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseSelectorBoundsJmp(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseSelectorBoundsBySelector(PVMCPUCC pVCpu, RTSEL Sel) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseSelectorBoundsBySelectorJmp(PVMCPUCC pVCpu, RTSEL Sel) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseSelectorInvalidAccess(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseSelectorInvalidAccessJmp(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaisePageFault(PVMCPUCC pVCpu, RTGCPTR GCPtrWhere, uint32_t cbAccess, uint32_t fAccess, int rc) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaisePageFaultJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrWhere, uint32_t cbAccess, uint32_t fAccess, int rc) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseMathFault(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseMathFaultJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseAlignmentCheckException(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseAlignmentCheckExceptionJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseSimdFpException(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseSimdFpExceptionJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif

void                    iemLogSyscallRealModeInt(PVMCPUCC pVCpu, uint8_t u8Vector, uint8_t cbInstr);
void                    iemLogSyscallProtModeInt(PVMCPUCC pVCpu, uint8_t u8Vector, uint8_t cbInstr);

IEM_CIMPL_DEF_0(iemCImplRaiseDivideError);
IEM_CIMPL_DEF_0(iemCImplRaiseInvalidLockPrefix);
IEM_CIMPL_DEF_0(iemCImplRaiseInvalidOpcode);

/**
 * Macro for calling iemCImplRaiseDivideError().
 *
 * This is for things that will _always_ decode to an \#DE, taking the
 * recompiler into consideration and everything.
 *
 * @return  Strict VBox status code.
 */
#define IEMOP_RAISE_DIVIDE_ERROR_RET()          IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_XCPT, 0, iemCImplRaiseDivideError)

/**
 * Macro for calling iemCImplRaiseInvalidLockPrefix().
 *
 * This is for things that will _always_ decode to an \#UD, taking the
 * recompiler into consideration and everything.
 *
 * @return  Strict VBox status code.
 */
#define IEMOP_RAISE_INVALID_LOCK_PREFIX_RET()   IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_XCPT, 0, iemCImplRaiseInvalidLockPrefix)

/**
 * Macro for calling iemCImplRaiseInvalidOpcode() for decode/static \#UDs.
 *
 * This is for things that will _always_ decode to an \#UD, taking the
 * recompiler into consideration and everything.
 *
 * @return  Strict VBox status code.
 */
#define IEMOP_RAISE_INVALID_OPCODE_RET()        IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_XCPT, 0, iemCImplRaiseInvalidOpcode)

/**
 * Macro for calling iemCImplRaiseInvalidOpcode() for runtime-style \#UDs.
 *
 * Using this macro means you've got _buggy_ _code_ and are doing things that
 * belongs exclusively in IEMAllCImpl.cpp during decoding.
 *
 * @return  Strict VBox status code.
 * @see     IEMOP_RAISE_INVALID_OPCODE_RET
 */
#define IEMOP_RAISE_INVALID_OPCODE_RUNTIME_RET() IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_XCPT, 0, iemCImplRaiseInvalidOpcode)

/** @} */

/** @name Register Access.
 * @{ */
VBOXSTRICTRC    iemRegRipRelativeJumpS8AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                           IEMMODE enmEffOpSize) RT_NOEXCEPT;
VBOXSTRICTRC    iemRegRipRelativeJumpS16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int16_t offNextInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemRegRipRelativeJumpS32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int32_t offNextInstr,
                                                            IEMMODE enmEffOpSize) RT_NOEXCEPT;
/** @} */

/** @name FPU access and helpers.
 * @{ */
void            iemFpuPushResult(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuPushResultWithMemOp(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuPushResultTwo(PVMCPUCC pVCpu, PIEMFPURESULTTWO pResult, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStoreResult(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStoreResultThenPop(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStoreResultWithMemOp(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg,
                                           uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStoreResultWithMemOpThenPop(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg,
                                                  uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuUpdateOpcodeAndIp(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuUpdateFSW(PVMCPUCC pVCpu, uint16_t u16FSW, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuUpdateFSWThenPop(PVMCPUCC pVCpu, uint16_t u16FSW, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuUpdateFSWWithMemOp(PVMCPUCC pVCpu, uint16_t u16FSW, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuUpdateFSWThenPopPop(PVMCPUCC pVCpu, uint16_t u16FSW, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuUpdateFSWWithMemOpThenPop(PVMCPUCC pVCpu, uint16_t u16FSW, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackUnderflow(PVMCPUCC pVCpu, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackUnderflowWithMemOp(PVMCPUCC pVCpu, uint8_t iStReg, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackUnderflowThenPop(PVMCPUCC pVCpu, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackUnderflowWithMemOpThenPop(PVMCPUCC pVCpu, uint8_t iStReg, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackUnderflowThenPopPop(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackPushUnderflow(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackPushUnderflowTwo(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackPushOverflow(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT;
void            iemFpuStackPushOverflowWithMemOp(PVMCPUCC pVCpu, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT;
/** @} */

/** @name SSE+AVX SIMD access and helpers.
 * @{ */
void            iemSseUpdateMxcsr(PVMCPUCC pVCpu, uint32_t fMxcsr) RT_NOEXCEPT;
/** @} */

/** @name   Memory access.
 * @{ */

/** Report a \#GP instead of \#AC and do not restrict to ring-3 */
#define IEM_MEMMAP_F_ALIGN_GP       RT_BIT_32(16)
/** SSE access that should report a \#GP instead of \#AC, unless MXCSR.MM=1
 *  when it works like normal \#AC. Always used with IEM_MEMMAP_F_ALIGN_GP. */
#define IEM_MEMMAP_F_ALIGN_SSE      RT_BIT_32(17)
/** If \#AC is applicable, raise it. Always used with IEM_MEMMAP_F_ALIGN_GP.
 * Users include FXSAVE & FXRSTOR. */
#define IEM_MEMMAP_F_ALIGN_GP_OR_AC RT_BIT_32(18)

VBOXSTRICTRC    iemMemMap(PVMCPUCC pVCpu, void **ppvMem, uint8_t *pbUnmapInfo, size_t cbMem, uint8_t iSegReg, RTGCPTR GCPtrMem,
                          uint32_t fAccess, uint32_t uAlignCtl) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemCommitAndUnmap(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
#ifndef IN_RING3
VBOXSTRICTRC    iemMemCommitAndUnmapPostponeTroubleToR3(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
#endif
void            iemMemRollbackAndUnmap(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
void            iemMemRollback(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemApplySegment(PVMCPUCC pVCpu, uint32_t fAccess, uint8_t iSegReg, size_t cbMem, PRTGCPTR pGCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemMarkSelDescAccessed(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemPageTranslateAndCheckAccess(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbAccess, uint32_t fAccess, PRTGCPHYS pGCPhysMem) RT_NOEXCEPT;

void            iemOpcodeFlushLight(PVMCPUCC pVCpu, uint8_t cbInstr);
void            iemOpcodeFlushHeavy(PVMCPUCC pVCpu, uint8_t cbInstr);
#ifdef IEM_WITH_CODE_TLB
void            iemOpcodeFetchBytesJmp(PVMCPUCC pVCpu, size_t cbDst, void *pvDst) IEM_NOEXCEPT_MAY_LONGJMP;
#else
VBOXSTRICTRC    iemOpcodeFetchMoreBytes(PVMCPUCC pVCpu, size_t cbMin) RT_NOEXCEPT;
#endif
#ifdef IEM_WITH_SETJMP
uint8_t         iemOpcodeGetNextU8SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t        iemOpcodeGetNextU16SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemOpcodeGetNextU32SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemOpcodeGetNextU64SlowJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#else
VBOXSTRICTRC    iemOpcodeGetNextU8Slow(PVMCPUCC pVCpu, uint8_t *pb) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextS8SxU16Slow(PVMCPUCC pVCpu, uint16_t *pu16) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextS8SxU32Slow(PVMCPUCC pVCpu, uint32_t *pu32) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextS8SxU64Slow(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextU16Slow(PVMCPUCC pVCpu, uint16_t *pu16) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextU16ZxU32Slow(PVMCPUCC pVCpu, uint32_t *pu32) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextU16ZxU64Slow(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextU32Slow(PVMCPUCC pVCpu, uint32_t *pu32) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextU32ZxU64Slow(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextS32SxU64Slow(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT;
VBOXSTRICTRC    iemOpcodeGetNextU64Slow(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT;
#endif

VBOXSTRICTRC    iemMemFetchDataU8(PVMCPUCC pVCpu, uint8_t *pu8Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32NoAc(PVMCPUCC pVCpu, uint32_t *pu32Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32_ZX_U64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64NoAc(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64AlignedU128(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataR80(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataD80(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU128(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU128NoAc(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU128AlignedSse(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU256(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU256NoAc(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU256AlignedAvx(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataXdtr(PVMCPUCC pVCpu, uint16_t *pcbLimit, PRTGCPTR pGCPtrBase, uint8_t iSegReg,
                                    RTGCPTR GCPtrMem, IEMMODE enmOpSize) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
uint8_t         iemMemFetchDataU8SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t        iemMemFetchDataU16SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchDataU32SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchDataU32NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFlatFetchDataU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64AlignedU128SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataR80SafeJmp(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataD80SafeJmp(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128SafeJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128NoAcSafeJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128AlignedSseSafeJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256SafeJmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256NoAcSafeJmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256AlignedAvxSafeJmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
# if 0 /* these are inlined now */
uint8_t         iemMemFetchDataU8Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t        iemMemFetchDataU16Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchDataU32Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFlatFetchDataU32Jmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64AlignedU128Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataR80Jmp(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataD80Jmp(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128Jmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128NoAcJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128AlignedSseJmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256NoAcJmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256AlignedAvxJmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
# endif
void            iemMemFetchDataU256Jmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
#endif

VBOXSTRICTRC    iemMemFetchSysU8(PVMCPUCC pVCpu, uint8_t *pu8Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSelDesc(PVMCPUCC pVCpu, PIEMSELDESC pDesc, uint16_t uSel, uint8_t uXcpt) RT_NOEXCEPT;

VBOXSTRICTRC    iemMemStoreDataU8(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint8_t u8Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU16(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint16_t u16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU32(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU64(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint64_t u64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128NoAc(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128AlignedSse(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256NoAc(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256AlignedAvx(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataXdtr(PVMCPUCC pVCpu, uint16_t cbLimit, RTGCPTR GCPtrBase, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
void            iemMemStoreDataU8SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint8_t u8Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU16SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint16_t u16Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU32SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint32_t u32Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU64SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint64_t u64Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT128U pu128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128AlignedSseSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT128U pu128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256NoAcSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256AlignedAvxSafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataR80SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTFLOAT80U pr80Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataD80SafeJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTPBCD80U pd80Value) IEM_NOEXCEPT_MAY_LONGJMP;
#if 0
void            iemMemStoreDataU8Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint8_t u8Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU16Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint16_t u16Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU32Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint32_t u32Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU64Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint64_t u64Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataNoAcU128Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256NoAcJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256AlignedAvxJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
void            iemMemStoreDataU128AlignedSseJmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
#endif

#ifdef IEM_WITH_SETJMP
uint8_t        *iemMemMapDataU8RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint8_t        *iemMemMapDataU8AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint8_t        *iemMemMapDataU8WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint8_t const  *iemMemMapDataU8RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t       *iemMemMapDataU16RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t       *iemMemMapDataU16AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t       *iemMemMapDataU16WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t const *iemMemMapDataU16RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t       *iemMemMapDataU32RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t       *iemMemMapDataU32AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t       *iemMemMapDataU32WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t const *iemMemMapDataU32RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t       *iemMemMapDataU64RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t       *iemMemMapDataU64AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t       *iemMemMapDataU64WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t const *iemMemMapDataU64RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTFLOAT80U     iemMemMapDataR80RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTFLOAT80U     iemMemMapDataR80WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PCRTFLOAT80U    iemMemMapDataR80RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTPBCD80U      iemMemMapDataD80RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTPBCD80U      iemMemMapDataD80WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PCRTPBCD80U     iemMemMapDataD80RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTUINT128U     iemMemMapDataU128RwSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTUINT128U     iemMemMapDataU128AtSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PRTUINT128U     iemMemMapDataU128WoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
PCRTUINT128U    iemMemMapDataU128RoSafeJmp(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemCommitAndUnmapJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapRwSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapAtSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapWoSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemCommitAndUnmapRoSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemRollbackAndUnmapWoSafe(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
#endif

VBOXSTRICTRC    iemMemStackPushBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                            void **ppvMem, uint8_t *pbUnmapInfo, uint64_t *puNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushCommitSpecial(PVMCPUCC pVCpu, uint8_t bUnmapInfo, uint64_t uNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU16(PVMCPUCC pVCpu, uint16_t u16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32(PVMCPUCC pVCpu, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU64(PVMCPUCC pVCpu, uint64_t u64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU16Ex(PVMCPUCC pVCpu, uint16_t u16Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32Ex(PVMCPUCC pVCpu, uint32_t u32Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU64Ex(PVMCPUCC pVCpu, uint64_t u64Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32SReg(PVMCPUCC pVCpu, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                           void const **ppvMem, uint8_t *pbUnmapInfo, uint64_t *puNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopContinueSpecial(PVMCPUCC pVCpu, size_t off, size_t cbMem,
                                              void const **ppvMem, uint8_t *pbUnmapInfo, uint64_t uCurNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopDoneSpecial(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU16(PVMCPUCC pVCpu, uint16_t *pu16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU32(PVMCPUCC pVCpu, uint32_t *pu32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU64(PVMCPUCC pVCpu, uint64_t *pu64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU16Ex(PVMCPUCC pVCpu, uint16_t *pu16Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU32Ex(PVMCPUCC pVCpu, uint32_t *pu32Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU64Ex(PVMCPUCC pVCpu, uint64_t *pu64Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;

#ifdef IEM_WITH_SETJMP
void            iemMemStackPushU16SafeJmp(PVMCPUCC pVCpu, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPushU32SafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPushU32SRegSafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPushU64SafeJmp(PVMCPUCC pVCpu, uint64_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPopGRegU16SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPopGRegU32SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStackPopGRegU64SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemFlat32StackPushU16SafeJmp(PVMCPUCC pVCpu, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPushU32SafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPushU32SRegSafeJmp(PVMCPUCC pVCpu, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPopGRegU16SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat32StackPopGRegU32SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemFlat64StackPushU16SafeJmp(PVMCPUCC pVCpu, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat64StackPushU64SafeJmp(PVMCPUCC pVCpu, uint64_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat64StackPopGRegU16SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFlat64StackPopGRegU64SafeJmp(PVMCPUCC pVCpu, uint8_t iGReg) IEM_NOEXCEPT_MAY_LONGJMP;

void            iemMemStoreStackU16SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint16_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreStackU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreStackU32SRegSafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreStackU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t uValue) IEM_NOEXCEPT_MAY_LONGJMP;

uint16_t        iemMemFetchStackU16SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchStackU32SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchStackU64SafeJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;

#endif

/** @} */

/** @name IEMAllCImpl.cpp
 * @note sed -e '/IEM_CIMPL_DEF_/!d' -e 's/IEM_CIMPL_DEF_/IEM_CIMPL_PROTO_/' -e 's/$/;/'
 * @{ */
IEM_CIMPL_PROTO_2(iemCImpl_pop_mem16, uint16_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_2(iemCImpl_pop_mem32, uint16_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_2(iemCImpl_pop_mem64, uint16_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_0(iemCImpl_popa_16);
IEM_CIMPL_PROTO_0(iemCImpl_popa_32);
IEM_CIMPL_PROTO_0(iemCImpl_pusha_16);
IEM_CIMPL_PROTO_0(iemCImpl_pusha_32);
IEM_CIMPL_PROTO_1(iemCImpl_pushf, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_1(iemCImpl_popf, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_3(iemCImpl_FarJmp, uint16_t, uSel, uint64_t, offSeg, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_3(iemCImpl_callf, uint16_t, uSel, uint64_t, offSeg, IEMMODE, enmEffOpSize);
typedef IEM_CIMPL_DECL_TYPE_3(FNIEMCIMPLFARBRANCH, uint16_t, uSel, uint64_t, offSeg, IEMMODE, enmEffOpSize);
typedef FNIEMCIMPLFARBRANCH *PFNIEMCIMPLFARBRANCH;
IEM_CIMPL_PROTO_2(iemCImpl_retf, IEMMODE, enmEffOpSize, uint16_t, cbPop);
IEM_CIMPL_PROTO_3(iemCImpl_enter, IEMMODE, enmEffOpSize, uint16_t, cbFrame, uint8_t, cParameters);
IEM_CIMPL_PROTO_1(iemCImpl_leave, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_int, uint8_t, u8Int, IEMINT, enmInt);
IEM_CIMPL_PROTO_1(iemCImpl_iret_real_v8086, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_4(iemCImpl_iret_prot_v8086, uint32_t, uNewEip, uint16_t, uNewCs, uint32_t, uNewFlags, uint64_t, uNewRsp);
IEM_CIMPL_PROTO_1(iemCImpl_iret_prot_NestedTask, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_1(iemCImpl_iret_prot, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_1(iemCImpl_iret_64bit, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_1(iemCImpl_iret, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_0(iemCImpl_loadall286);
IEM_CIMPL_PROTO_0(iemCImpl_syscall);
IEM_CIMPL_PROTO_1(iemCImpl_sysret, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_0(iemCImpl_sysenter);
IEM_CIMPL_PROTO_1(iemCImpl_sysexit, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_LoadSReg, uint8_t, iSegReg, uint16_t, uSel);
IEM_CIMPL_PROTO_2(iemCImpl_load_SReg, uint8_t, iSegReg, uint16_t, uSel);
IEM_CIMPL_PROTO_2(iemCImpl_pop_Sreg, uint8_t, iSegReg, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_5(iemCImpl_load_SReg_Greg, uint16_t, uSel, uint64_t, offSeg, uint8_t, iSegReg, uint8_t, iGReg, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_VerX, uint16_t, uSel, bool, fWrite);
IEM_CIMPL_PROTO_3(iemCImpl_LarLsl_u64, uint64_t *, pu64Dst, uint16_t, uSel, bool, fIsLar);
IEM_CIMPL_PROTO_3(iemCImpl_LarLsl_u16, uint16_t *, pu16Dst, uint16_t, uSel, bool, fIsLar);
IEM_CIMPL_PROTO_3(iemCImpl_lgdt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_sgdt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_3(iemCImpl_lidt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_sidt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_1(iemCImpl_lldt, uint16_t, uNewLdt);
IEM_CIMPL_PROTO_2(iemCImpl_sldt_reg, uint8_t, iGReg, uint8_t, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_sldt_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_1(iemCImpl_ltr, uint16_t, uNewTr);
IEM_CIMPL_PROTO_2(iemCImpl_str_reg, uint8_t, iGReg, uint8_t, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_str_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_2(iemCImpl_mov_Rd_Cd, uint8_t, iGReg, uint8_t, iCrReg);
IEM_CIMPL_PROTO_2(iemCImpl_smsw_reg, uint8_t, iGReg, uint8_t, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_smsw_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_4(iemCImpl_load_CrX, uint8_t, iCrReg, uint64_t, uNewCrX, IEMACCESSCRX, enmAccessCrX, uint8_t, iGReg);
IEM_CIMPL_PROTO_2(iemCImpl_mov_Cd_Rd, uint8_t, iCrReg, uint8_t, iGReg);
IEM_CIMPL_PROTO_2(iemCImpl_lmsw, uint16_t, u16NewMsw, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_0(iemCImpl_clts);
IEM_CIMPL_PROTO_2(iemCImpl_mov_Rd_Dd, uint8_t, iGReg, uint8_t, iDrReg);
IEM_CIMPL_PROTO_2(iemCImpl_mov_Dd_Rd, uint8_t, iDrReg, uint8_t, iGReg);
IEM_CIMPL_PROTO_2(iemCImpl_mov_Rd_Td, uint8_t, iGReg, uint8_t, iTrReg);
IEM_CIMPL_PROTO_2(iemCImpl_mov_Td_Rd, uint8_t, iTrReg, uint8_t, iGReg);
IEM_CIMPL_PROTO_1(iemCImpl_invlpg, RTGCPTR, GCPtrPage);
IEM_CIMPL_PROTO_3(iemCImpl_invpcid, uint8_t, iEffSeg, RTGCPTR, GCPtrInvpcidDesc, uint64_t, uInvpcidType);
IEM_CIMPL_PROTO_0(iemCImpl_invd);
IEM_CIMPL_PROTO_0(iemCImpl_wbinvd);
IEM_CIMPL_PROTO_0(iemCImpl_rsm);
IEM_CIMPL_PROTO_0(iemCImpl_rdtsc);
IEM_CIMPL_PROTO_0(iemCImpl_rdtscp);
IEM_CIMPL_PROTO_0(iemCImpl_rdpmc);
IEM_CIMPL_PROTO_0(iemCImpl_rdmsr);
IEM_CIMPL_PROTO_0(iemCImpl_wrmsr);
IEM_CIMPL_PROTO_3(iemCImpl_in, uint16_t, u16Port, uint8_t, cbReg, uint8_t, bImmAndEffAddrMode);
IEM_CIMPL_PROTO_2(iemCImpl_in_eAX_DX, uint8_t, cbReg, IEMMODE, enmEffAddrMode);
IEM_CIMPL_PROTO_3(iemCImpl_out, uint16_t, u16Port, uint8_t, cbReg, uint8_t, bImmAndEffAddrMode);
IEM_CIMPL_PROTO_2(iemCImpl_out_DX_eAX, uint8_t, cbReg, IEMMODE, enmEffAddrMode);
IEM_CIMPL_PROTO_0(iemCImpl_cli);
IEM_CIMPL_PROTO_0(iemCImpl_sti);
IEM_CIMPL_PROTO_0(iemCImpl_hlt);
IEM_CIMPL_PROTO_1(iemCImpl_monitor, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_mwait);
IEM_CIMPL_PROTO_0(iemCImpl_swapgs);
IEM_CIMPL_PROTO_0(iemCImpl_cpuid);
IEM_CIMPL_PROTO_1(iemCImpl_aad, uint8_t, bImm);
IEM_CIMPL_PROTO_1(iemCImpl_aam, uint8_t, bImm);
IEM_CIMPL_PROTO_0(iemCImpl_daa);
IEM_CIMPL_PROTO_0(iemCImpl_das);
IEM_CIMPL_PROTO_0(iemCImpl_aaa);
IEM_CIMPL_PROTO_0(iemCImpl_aas);
IEM_CIMPL_PROTO_3(iemCImpl_bound_16, int16_t, idxArray, int16_t, idxLowerBound, int16_t, idxUpperBound);
IEM_CIMPL_PROTO_3(iemCImpl_bound_32, int32_t, idxArray, int32_t, idxLowerBound, int32_t, idxUpperBound);
IEM_CIMPL_PROTO_0(iemCImpl_xgetbv);
IEM_CIMPL_PROTO_0(iemCImpl_xsetbv);
IEM_CIMPL_PROTO_5(iemCImpl_cmpxchg16b_fallback_rendezvous, PRTUINT128U, pu128Dst, PRTUINT128U, pu128RaxRdx,
                  PRTUINT128U, pu128RbxRcx, uint32_t *, pEFlags, uint8_t, bUnmapInfo);
IEM_CIMPL_PROTO_2(iemCImpl_clflush_clflushopt, uint8_t, iEffSeg, RTGCPTR, GCPtrEff);
IEM_CIMPL_PROTO_1(iemCImpl_finit, bool, fCheckXcpts);
IEM_CIMPL_PROTO_3(iemCImpl_fxsave, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_3(iemCImpl_fxrstor, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_3(iemCImpl_xsave, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_3(iemCImpl_xrstor, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_stmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff);
IEM_CIMPL_PROTO_2(iemCImpl_vstmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff);
IEM_CIMPL_PROTO_2(iemCImpl_ldmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff);
IEM_CIMPL_PROTO_2(iemCImpl_vldmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff);
IEM_CIMPL_PROTO_3(iemCImpl_fnstenv, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_3(iemCImpl_fnsave, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst);
IEM_CIMPL_PROTO_3(iemCImpl_fldenv, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_3(iemCImpl_frstor, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_1(iemCImpl_fldcw, uint16_t, u16Fcw);
IEM_CIMPL_PROTO_2(iemCImpl_fxch_underflow, uint8_t, iStReg, uint16_t, uFpuOpcode);
IEM_CIMPL_PROTO_3(iemCImpl_fcomi_fucomi, uint8_t, iStReg, bool, fUCmp, uint32_t, uPopAndFpuOpcode);
IEM_CIMPL_PROTO_2(iemCImpl_rdseed, uint8_t, iReg, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_2(iemCImpl_rdrand, uint8_t, iReg, IEMMODE, enmEffOpSize);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovps_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovps_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovps_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovps_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovd_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovd_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovd_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovd_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovpd_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovpd_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovpd_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vmaskmovpd_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovq_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovq_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovq_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc);
IEM_CIMPL_PROTO_4(iemCImpl_vpmaskmovq_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc);
IEM_CIMPL_PROTO_2(iemCImpl_vpgather_worker_xx, uint32_t, u32PackedArgs, uint32_t, u32Disp);

/** @} */

/** @name IEMAllCImplStrInstr.cpp.h
 * @note sed -e '/IEM_CIMPL_DEF_/!d' -e 's/IEM_CIMPL_DEF_/IEM_CIMPL_PROTO_/' -e 's/$/;/' -e 's/RT_CONCAT4(//' \
 *           -e 's/,ADDR_SIZE)/64/g' -e 's/,OP_SIZE,/64/g' -e 's/,OP_rAX,/rax/g' IEMAllCImplStrInstr.cpp.h
 * @{ */
IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op8_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op8_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_al_m16);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_al_m16);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op8_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_al_m16);
IEM_CIMPL_PROTO_1(iemCImpl_lods_al_m16, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op8_addr16, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op8_addr16, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op8_addr16, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op8_addr16, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op16_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op16_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_ax_m16);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_ax_m16);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op16_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_ax_m16);
IEM_CIMPL_PROTO_1(iemCImpl_lods_ax_m16, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op16_addr16, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op16_addr16, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op16_addr16, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op16_addr16, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op32_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op32_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_eax_m16);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_eax_m16);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op32_addr16, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_eax_m16);
IEM_CIMPL_PROTO_1(iemCImpl_lods_eax_m16, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op32_addr16, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op32_addr16, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op32_addr16, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op32_addr16, uint8_t, iEffSeg, bool, fIoChecked);


IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op8_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op8_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_al_m32);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_al_m32);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op8_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_al_m32);
IEM_CIMPL_PROTO_1(iemCImpl_lods_al_m32, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op8_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op8_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op8_addr32, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op8_addr32, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op16_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op16_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_ax_m32);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_ax_m32);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op16_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_ax_m32);
IEM_CIMPL_PROTO_1(iemCImpl_lods_ax_m32, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op16_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op16_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op16_addr32, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op16_addr32, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op32_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op32_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_eax_m32);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_eax_m32);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op32_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_eax_m32);
IEM_CIMPL_PROTO_1(iemCImpl_lods_eax_m32, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op32_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op32_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op32_addr32, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op32_addr32, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op64_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op64_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_rax_m32);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_rax_m32);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op64_addr32, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_rax_m32);
IEM_CIMPL_PROTO_1(iemCImpl_lods_rax_m32, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op64_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op64_addr32, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op64_addr32, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op64_addr32, uint8_t, iEffSeg, bool, fIoChecked);


IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op8_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op8_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_al_m64);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_al_m64);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op8_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_al_m64);
IEM_CIMPL_PROTO_1(iemCImpl_lods_al_m64, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op8_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op8_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op8_addr64, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op8_addr64, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op16_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op16_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_ax_m64);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_ax_m64);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op16_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_ax_m64);
IEM_CIMPL_PROTO_1(iemCImpl_lods_ax_m64, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op16_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op16_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op16_addr64, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op16_addr64, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op32_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op32_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_eax_m64);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_eax_m64);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op32_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_eax_m64);
IEM_CIMPL_PROTO_1(iemCImpl_lods_eax_m64, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op32_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op32_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op32_addr64, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op32_addr64, uint8_t, iEffSeg, bool, fIoChecked);

IEM_CIMPL_PROTO_1(iemCImpl_repe_cmps_op64_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_repne_cmps_op64_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_repe_scas_rax_m64);
IEM_CIMPL_PROTO_0(iemCImpl_repne_scas_rax_m64);
IEM_CIMPL_PROTO_1(iemCImpl_rep_movs_op64_addr64, uint8_t, iEffSeg);
IEM_CIMPL_PROTO_0(iemCImpl_stos_rax_m64);
IEM_CIMPL_PROTO_1(iemCImpl_lods_rax_m64, int8_t, iEffSeg);
IEM_CIMPL_PROTO_1(iemCImpl_ins_op64_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_1(iemCImpl_rep_ins_op64_addr64, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_outs_op64_addr64, uint8_t, iEffSeg, bool, fIoChecked);
IEM_CIMPL_PROTO_2(iemCImpl_rep_outs_op64_addr64, uint8_t, iEffSeg, bool, fIoChecked);
/** @} */

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
VBOXSTRICTRC    iemVmxVmexit(PVMCPUCC pVCpu, uint32_t uExitReason, uint64_t u64ExitQual) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstr(PVMCPUCC pVCpu, uint32_t uExitReason, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrNeedsInfo(PVMCPUCC pVCpu, uint32_t uExitReason, VMXINSTRID uInstrId, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitTaskSwitch(PVMCPUCC pVCpu, IEMTASKSWITCH enmTaskSwitch, RTSEL SelNewTss, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitEvent(PVMCPUCC pVCpu, uint8_t uVector, uint32_t fFlags, uint32_t uErrCode, uint64_t uCr2, uint8_t cbInstr)  RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitEventDoubleFault(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitEpt(PVMCPUCC pVCpu, PPGMPTWALKFAST pWalk, uint32_t fAccess, uint32_t fSlatFail, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitPreemptTimer(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMwait(PVMCPUCC pVCpu, bool fMonitorHwArmed, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrIo(PVMCPUCC pVCpu, VMXINSTRID uInstrId, uint16_t u16Port,
                                    bool fImm, uint8_t cbAccess, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrStrIo(PVMCPUCC pVCpu, VMXINSTRID uInstrId, uint16_t u16Port, uint8_t cbAccess,
                                       bool fRep, VMXEXITINSTRINFO ExitInstrInfo, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMovDrX(PVMCPUCC pVCpu, VMXINSTRID uInstrId, uint8_t iDrReg, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMovToCr8(PVMCPUCC pVCpu, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMovFromCr8(PVMCPUCC pVCpu, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMovToCr3(PVMCPUCC pVCpu, uint64_t uNewCr3, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMovFromCr3(PVMCPUCC pVCpu, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrMovToCr0Cr4(PVMCPUCC pVCpu, uint8_t iCrReg, uint64_t *puNewCrX, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrClts(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrLmsw(PVMCPUCC pVCpu, uint32_t uGuestCr0, uint16_t *pu16NewMsw,
                                      RTGCPTR GCPtrEffDst, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVmexitInstrInvlpg(PVMCPUCC pVCpu, RTGCPTR GCPtrPage, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxApicWriteEmulation(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxVirtApicAccessUnused(PVMCPUCC pVCpu, PRTGCPHYS pGCPhysAccess, size_t cbAccess, uint32_t fAccess) RT_NOEXCEPT;
uint32_t        iemVmxVirtApicReadRaw32(PVMCPUCC pVCpu, uint16_t offReg) RT_NOEXCEPT;
void            iemVmxVirtApicWriteRaw32(PVMCPUCC pVCpu, uint16_t offReg, uint32_t uReg) RT_NOEXCEPT;
VBOXSTRICTRC    iemVmxInvvpid(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrInvvpidDesc,
                              uint64_t u64InvvpidType, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT;
bool            iemVmxIsRdmsrWrmsrInterceptSet(PCVMCPU pVCpu, uint32_t uExitReason, uint32_t idMsr) RT_NOEXCEPT;
IEM_CIMPL_PROTO_0(iemCImpl_vmxoff);
IEM_CIMPL_PROTO_2(iemCImpl_vmxon, uint8_t, iEffSeg, RTGCPTR, GCPtrVmxon);
IEM_CIMPL_PROTO_0(iemCImpl_vmlaunch);
IEM_CIMPL_PROTO_0(iemCImpl_vmresume);
IEM_CIMPL_PROTO_2(iemCImpl_vmptrld, uint8_t, iEffSeg, RTGCPTR, GCPtrVmcs);
IEM_CIMPL_PROTO_2(iemCImpl_vmptrst, uint8_t, iEffSeg, RTGCPTR, GCPtrVmcs);
IEM_CIMPL_PROTO_2(iemCImpl_vmclear, uint8_t, iEffSeg, RTGCPTR, GCPtrVmcs);
IEM_CIMPL_PROTO_2(iemCImpl_vmwrite_reg, uint64_t, u64Val, uint64_t, u64VmcsField);
IEM_CIMPL_PROTO_3(iemCImpl_vmwrite_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrVal, uint32_t, u64VmcsField);
IEM_CIMPL_PROTO_2(iemCImpl_vmread_reg64, uint64_t *, pu64Dst, uint64_t, u64VmcsField);
IEM_CIMPL_PROTO_2(iemCImpl_vmread_reg32, uint64_t *, pu32Dst, uint32_t, u32VmcsField);
IEM_CIMPL_PROTO_3(iemCImpl_vmread_mem_reg64, uint8_t, iEffSeg, RTGCPTR, GCPtrDst, uint32_t, u64VmcsField);
IEM_CIMPL_PROTO_3(iemCImpl_vmread_mem_reg32, uint8_t, iEffSeg, RTGCPTR, GCPtrDst, uint32_t, u32VmcsField);
IEM_CIMPL_PROTO_3(iemCImpl_invvpid, uint8_t, iEffSeg, RTGCPTR, GCPtrInvvpidDesc, uint64_t, uInvvpidType);
IEM_CIMPL_PROTO_3(iemCImpl_invept, uint8_t, iEffSeg, RTGCPTR, GCPtrInveptDesc, uint64_t, uInveptType);
IEM_CIMPL_PROTO_0(iemCImpl_vmx_pause);
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
VBOXSTRICTRC    iemSvmVmexit(PVMCPUCC pVCpu, uint64_t uExitCode, uint64_t uExitInfo1, uint64_t uExitInfo2) RT_NOEXCEPT;
VBOXSTRICTRC    iemHandleSvmEventIntercept(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector, uint32_t fFlags, uint32_t uErr, uint64_t uCr2) RT_NOEXCEPT;
VBOXSTRICTRC    iemSvmHandleIOIntercept(PVMCPUCC pVCpu, uint16_t u16Port, SVMIOIOTYPE enmIoType, uint8_t cbReg,
                                        uint8_t cAddrSizeBits, uint8_t iEffSeg, bool fRep, bool fStrIo, uint8_t cbInstr) RT_NOEXCEPT;
VBOXSTRICTRC    iemSvmHandleMsrIntercept(PVMCPUCC pVCpu, uint32_t idMsr, bool fWrite, uint8_t cbInstr) RT_NOEXCEPT;
IEM_CIMPL_PROTO_0(iemCImpl_vmrun);
IEM_CIMPL_PROTO_0(iemCImpl_vmload);
IEM_CIMPL_PROTO_0(iemCImpl_vmsave);
IEM_CIMPL_PROTO_0(iemCImpl_clgi);
IEM_CIMPL_PROTO_0(iemCImpl_stgi);
IEM_CIMPL_PROTO_0(iemCImpl_invlpga);
IEM_CIMPL_PROTO_0(iemCImpl_skinit);
IEM_CIMPL_PROTO_0(iemCImpl_svm_pause);
#endif

IEM_CIMPL_PROTO_0(iemCImpl_vmcall);  /* vmx */
IEM_CIMPL_PROTO_0(iemCImpl_vmmcall); /* svm */
IEM_CIMPL_PROTO_1(iemCImpl_Hypercall, uint16_t, uDisOpcode); /* both */

extern const PFNIEMOP g_apfnIemInterpretOnlyOneByteMap[256];
extern const PFNIEMOP g_apfnIemInterpretOnlyTwoByteMap[1024];
extern const PFNIEMOP g_apfnIemInterpretOnlyThreeByte0f3a[1024];
extern const PFNIEMOP g_apfnIemInterpretOnlyThreeByte0f38[1024];
extern const PFNIEMOP g_apfnIemInterpretOnlyVecMap1[1024];
extern const PFNIEMOP g_apfnIemInterpretOnlyVecMap2[1024];
extern const PFNIEMOP g_apfnIemInterpretOnlyVecMap3[1024];

/*
 * Recompiler related stuff.
 */
extern const PFNIEMOP g_apfnIemThreadedRecompilerOneByteMap[256];
extern const PFNIEMOP g_apfnIemThreadedRecompilerTwoByteMap[1024];
extern const PFNIEMOP g_apfnIemThreadedRecompilerThreeByte0f3a[1024];
extern const PFNIEMOP g_apfnIemThreadedRecompilerThreeByte0f38[1024];
extern const PFNIEMOP g_apfnIemThreadedRecompilerVecMap1[1024];
extern const PFNIEMOP g_apfnIemThreadedRecompilerVecMap2[1024];
extern const PFNIEMOP g_apfnIemThreadedRecompilerVecMap3[1024];

DECLHIDDEN(int)     iemPollTimers(PVMCC pVM, PVMCPUCC pVCpu) RT_NOEXCEPT;

DECLCALLBACK(int)   iemTbInit(PVMCC pVM, uint32_t cInitialTbs, uint32_t cMaxTbs,
                              uint64_t cbInitialExec, uint64_t cbMaxExec, uint32_t cbChunkExec);
void                iemThreadedTbObsolete(PVMCPUCC pVCpu, PIEMTB pTb, bool fSafeToFree);
DECLHIDDEN(void)    iemTbAllocatorFree(PVMCPUCC pVCpu, PIEMTB pTb);
void                iemTbAllocatorProcessDelayedFrees(PVMCPUCC pVCpu, PIEMTBALLOCATOR pTbAllocator);
void                iemTbAllocatorFreeupNativeSpace(PVMCPUCC pVCpu, uint32_t cNeededInstrs);
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
    VBOXSTRICTRC a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2) IEM_NOEXCEPT_MAY_LONGJMP
# define IEM_DECL_IEMTHREADEDFUNC_PROTO(a_Name) \
    VBOXSTRICTRC a_Name(PVMCPU pVCpu, uint64_t uParam0, uint64_t uParam1, uint64_t uParam2) IEM_NOEXCEPT_MAY_LONGJMP
#endif


IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_Nop);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_LogCpuState);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_DeferToCImpl0);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckIrq);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckTimers);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckTimersAndIrq);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckMode);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckHwInstrBps);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodes);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodes);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesConsiderCsLim);

/* Branching: */
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndPcAndOpcodes);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckPcAndOpcodes);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckPcAndOpcodesConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesLoadingTlbConsiderCsLim);

/* Natural page crossing: */
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesAcrossPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlbConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNextPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlbConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNewPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlb);
IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlbConsiderCsLim);

IEM_DECL_IEMTHREADEDFUNC_PROTO(iemThreadedFunc_BltIn_Jump);

bool iemThreadedCompileEmitIrqCheckBefore(PVMCPUCC pVCpu, PIEMTB pTb);
bool iemThreadedCompileBeginEmitCallsComplications(PVMCPUCC pVCpu, PIEMTB pTb);
#ifdef IEM_WITH_INTRA_TB_JUMPS
DECLHIDDEN(int)     iemThreadedCompileBackAtFirstInstruction(PVMCPU pVCpu, PIEMTB pTb) RT_NOEXCEPT;
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

/** Packed 32-bit argument for iemCImpl_vpgather_worker_xx. */
typedef union IEMGATHERARGS
{
    /** Integer view. */
    uint32_t u;
    /** Bitfield view. */
    struct
    {
        uint32_t iYRegDst       : 4; /**<  0 - XMM or YMM register number (destination) */
        uint32_t iYRegIdc       : 4; /**<  4 - XMM or YMM register number (indices)     */
        uint32_t iYRegMsk       : 4; /**<  8 - XMM or YMM register number (mask)        */
        uint32_t iGRegBase      : 4; /**< 12 - general register number    (base ptr)    */
        uint32_t iScale         : 2; /**< 16 - scale factor               (1/2/4/8)     */
        uint32_t enmEffOpSize   : 2; /**< 18 - operand size               (16/32/64/--) */
        uint32_t enmEffAddrMode : 2; /**< 20 - addressing  mode           (16/32/64/--) */
        uint32_t iEffSeg        : 3; /**< 22 - effective segment (ES/CS/SS/DS/FS/GS)    */
        uint32_t fVex256        : 1; /**< 25 - overall instruction width (128/256 bits) */
        uint32_t fIdxQword      : 1; /**< 26 - individual index width     (4/8 bytes)   */
        uint32_t fValQword      : 1; /**< 27 - individual value width     (4/8 bytes)   */
    } s;
} IEMGATHERARGS;
AssertCompileSize(IEMGATHERARGS, sizeof(uint32_t));

#endif /* !RT_IN_ASSEMBLER - ASM-NOINC-END */


/** @} */

RT_C_DECLS_END

/* ASM-INC: %include "IEMInternalStruct.mac" */

#endif /* !VMM_INCLUDED_SRC_include_IEMInternal_h */

