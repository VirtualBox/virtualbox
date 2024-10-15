/* $Id: IEMInternal-armv8.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Internal header file, ARMv8 variant.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_IEMInternal_armv8_h
#define VMM_INCLUDED_SRC_include_IEMInternal_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/cpum.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/stam.h>
#include <VBox/param.h>

#include <iprt/setjmp-without-sigmask.h>


RT_C_DECLS_BEGIN


/** @defgroup grp_iem_int       Internals
 * @ingroup grp_iem
 * @internal
 * @{
 */

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
#if (defined(IEM_WITH_SETJMP) && defined(IN_RING3) && (defined(__GNUC__) || defined(_MSC_VER))) \
 || defined(DOXYGEN_RUNNING)
# define IEM_WITH_THROW_CATCH
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
#  define IEM_DO_LONGJMP(a_pVCpu, a_rc)  throw int(a_rc)
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


//#define IEM_LOG_MEMORY_WRITES

#if !defined(IN_TSTVMSTRUCT) && !defined(DOXYGEN_RUNNING)
/** Instruction statistics.   */
typedef struct IEMINSTRSTATS
{
# define IEM_DO_INSTR_STAT(a_Name, a_szDesc) uint32_t a_Name;
/** @todo # include "IEMInstructionStatisticsTmpl.h" */
    uint8_t bDummy;
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
#define IEMTARGETCPU_EFL_BEHAVIOR_NATIVE      0     /**< Native result; Intel EFLAGS when on non-x86 hosts. */
#define IEMTARGETCPU_EFL_BEHAVIOR_RESERVED    1     /**< Reserved/dummy entry slot that's the same as 0. */
#define IEMTARGETCPU_EFL_BEHAVIOR_MASK        1     /**< For masking the index before use. */
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


/** The TLB size (power of two).
 * We initially chose 256 because that way we can obtain the result directly
 * from a 8-bit register without an additional AND instruction.
 * See also @bugref{10687}. */
#define IEMTLB_ENTRY_COUNT                      256
#define IEMTLB_ENTRY_COUNT_AS_POWER_OF_TWO      8

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


/**
 * An IEM TLB.
 *
 * We've got two of these, one for data and one for instructions.
 */
typedef struct IEMTLB
{
    /** The TLB entries.
     * We've choosen 256 because that way we can obtain the result directly from a
     * 8-bit register without an additional AND instruction. */
    IEMTLBENTRY         aEntries[IEMTLB_ENTRY_COUNT];
    /** The TLB revision.
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
     * The initial value is choosen to cause an early wraparound. */
    uint64_t volatile   uTlbPhysRev;

    /* Statistics: */

    /** TLB hits (VBOX_WITH_STATISTICS only). */
    uint64_t            cTlbHits;
    /** TLB misses. */
    uint32_t            cTlbMisses;
    /** Slow read path.  */
    uint32_t            cTlbSlowCodeReadPath;
#if 0
    /** TLB misses because of tag mismatch. */
    uint32_t            cTlbMissesTag;
    /** TLB misses because of virtual access violation. */
    uint32_t            cTlbMissesVirtAccess;
    /** TLB misses because of dirty bit. */
    uint32_t            cTlbMissesDirty;
    /** TLB misses because of MMIO */
    uint32_t            cTlbMissesMmio;
    /** TLB misses because of write access handlers. */
    uint32_t            cTlbMissesWriteHandler;
    /** TLB misses because no r3(/r0) mapping. */
    uint32_t            cTlbMissesMapping;
#endif
    /** Alignment padding. */
    uint32_t            au32Padding[3+5];
} IEMTLB;
AssertCompileSizeAlignment(IEMTLB, 64);
/** The width (in bits) of the address portion of the TLB tag.   */
#define IEMTLB_TAG_ADDR_WIDTH   36
/** IEMTLB::uTlbRevision increment.  */
#define IEMTLB_REVISION_INCR    RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH)
/** IEMTLB::uTlbRevision mask.  */
#define IEMTLB_REVISION_MASK    (~(RT_BIT_64(IEMTLB_TAG_ADDR_WIDTH) - 1))

/** IEMTLB::uTlbPhysRev increment.
 * @sa IEMTLBE_F_PHYS_REV */
#define IEMTLB_PHYS_REV_INCR    RT_BIT_64(10)
/**
 * Calculates the TLB tag for a virtual address.
 * @returns Tag value for indexing and comparing with IEMTLB::uTag.
 * @param   a_pTlb      The TLB.
 * @param   a_GCPtr     The virtual address.
 */
#define IEMTLB_CALC_TAG(a_pTlb, a_GCPtr)    ( IEMTLB_CALC_TAG_NO_REV(a_GCPtr) | (a_pTlb)->uTlbRevision )
/**
 * Calculates the TLB tag for a virtual address but without TLB revision.
 * @returns Tag value for indexing and comparing with IEMTLB::uTag.
 * @param   a_GCPtr     The virtual address.
 */
#define IEMTLB_CALC_TAG_NO_REV(a_GCPtr)     ( (((a_GCPtr) << 16) >> (GUEST_PAGE_SHIFT + 16)) )
/**
 * Converts a TLB tag value into a TLB index.
 * @returns Index into IEMTLB::aEntries.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG.
 */
#define IEMTLB_TAG_TO_INDEX(a_uTag)         ( (uint8_t)(a_uTag) )
/**
 * Converts a TLB tag value into a TLB index.
 * @returns Index into IEMTLB::aEntries.
 * @param   a_pTlb      The TLB.
 * @param   a_uTag      Value returned by IEMTLB_CALC_TAG.
 */
#define IEMTLB_TAG_TO_ENTRY(a_pTlb, a_uTag) ( &(a_pTlb)->aEntries[IEMTLB_TAG_TO_INDEX(a_uTag)] )


/**
 * The per-CPU IEM state.
 *
 * @todo This is just a STUB currently!
 */
typedef struct IEMCPU
{
    /** Info status code that needs to be propagated to the IEM caller.
     * This cannot be passed internally, as it would complicate all success
     * checks within the interpreter making the code larger and almost impossible
     * to get right.  Instead, we'll store status codes to pass on here.  Each
     * source of these codes will perform appropriate sanity checks. */
    int32_t                 rcPassUp;                                                                       /* 0x00 */

    /** The current CPU execution mode (CS). */
    IEMMODE                 enmCpuMode;                                                                     /* 0x04 */
    /** The Exception Level (EL). */
    uint8_t                 uEl;                                                                            /* 0x05 */

    /** Whether to bypass access handlers or not. */
    bool                    fBypassHandlers : 1;                                                            /* 0x06.0 */
    /** Whether there are pending hardware instruction breakpoints. */
    bool                    fPendingInstructionBreakpoints : 1;                                             /* 0x06.2 */
    /** Whether there are pending hardware data breakpoints. */
    bool                    fPendingDataBreakpoints : 1;                                                    /* 0x06.3 */

    /* Unused/padding */
    bool                    fUnused;                                                                        /* 0x07 */

    /** @name Decoder state.
     * @{ */
#ifndef IEM_WITH_OPAQUE_DECODER_STATE
    /** The current instruction being executed. */
    uint32_t                u32Insn;
    uint8_t                 abOpaqueDecoder[0x48 - 0x4 - 0x8];
#else  /* IEM_WITH_OPAQUE_DECODER_STATE */
    uint8_t                 abOpaqueDecoder[0x48 - 0x8];
#endif /* IEM_WITH_OPAQUE_DECODER_STATE */
    /** @} */


    /** The flags of the current exception / interrupt. */
    uint32_t                fCurXcpt;                                                                       /* 0x48, 0x48 */
    /** The current exception / interrupt. */
    uint8_t                 uCurXcpt;
    /** Exception / interrupt recursion depth. */
    int8_t                  cXcptRecursions;

    /** The number of active guest memory mappings. */
    uint8_t                 cActiveMappings;
    /** The next unused mapping index. */
    uint8_t                 iNextMapping;
    /** Records for tracking guest memory mappings. */
    struct
    {
        /** The address of the mapped bytes. */
        void               *pv;
        /** The access flags (IEM_ACCESS_XXX).
         * IEM_ACCESS_INVALID if the entry is unused. */
        uint32_t            fAccess;
#if HC_ARCH_BITS == 64
        uint32_t            u32Alignment4; /**< Alignment padding. */
#endif
    } aMemMappings[3];

    /** Locking records for the mapped memory. */
    union
    {
        PGMPAGEMAPLOCK      Lock;
        uint64_t            au64Padding[2];
    } aMemMappingLocks[3];

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
    } aMemBbMappings[3];

    /* Ensure that aBounceBuffers are aligned at a 32 byte boundrary. */
    uint64_t                abAlignment7[1];

    /** Bounce buffer storage.
     * This runs in parallel to aMemMappings and aMemBbMappings. */
    struct
    {
        uint8_t             ab[512];
    } aBounceBuffers[3];


    /** Pointer set jump buffer - ring-3 context. */
    R3PTRTYPE(jmp_buf *)    pJmpBufR3;

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

    uint8_t                 bPadding;

    /** The CPU vendor. */
    CPUMCPUVENDOR           enmCpuVendor;
    /** @} */

    /** @name Host CPU information.
     * @{ */
    /** The CPU vendor. */
    CPUMCPUVENDOR           enmHostCpuVendor;
    /** @} */

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

#if defined(VBOX_WITH_STATISTICS) && !defined(IN_TSTVMSTRUCT) && !defined(DOXYGEN_RUNNING)
    /** Instruction statistics for ring-3. */
    IEMINSTRSTATS           StatsR3;
#endif
} IEMCPU;
AssertCompileMemberOffset(IEMCPU, fCurXcpt, 0x48);
AssertCompileMemberAlignment(IEMCPU, aBounceBuffers, 8);
AssertCompileMemberAlignment(IEMCPU, aBounceBuffers, 16);
AssertCompileMemberAlignment(IEMCPU, aBounceBuffers, 32);
AssertCompileMemberAlignment(IEMCPU, aBounceBuffers, 64);
AssertCompileMemberAlignment(IEMCPU, DataTlb, 64);
AssertCompileMemberAlignment(IEMCPU, CodeTlb, 64);

/** Pointer to the per-CPU IEM state. */
typedef IEMCPU *PIEMCPU;
/** Pointer to the const per-CPU IEM state. */
typedef IEMCPU const *PCIEMCPU;


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
#define IEM_CTX_ASSERT(a_pVCpu, a_fExtrnMbz)    AssertMsg(!((a_pVCpu)->cpum.GstCtx.fExtrn & (a_fExtrnMbz)), \
                                                          ("fExtrn=%#RX64 fExtrnMbz=%#RX64\n", (a_pVCpu)->cpum.GstCtx.fExtrn, \
                                                          (a_fExtrnMbz)))

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
/** @todo Thumb mode. */
#ifdef IEM_WITH_CODE_TLB
# define IEM_GET_INSTR_LEN(a_pVCpu)     (sizeof(uint32_t))
#else
# define IEM_GET_INSTR_LEN(a_pVCpu)     (sizeof(uint32_t))
#endif


/**
 * Shared per-VM IEM data.
 */
typedef struct IEM
{
    uint8_t bDummy;
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
/** Valid bit mask. */
#define IEM_ACCESS_VALID_MASK           UINT32_C(0x00001fff)
/** Shift count for the TLB flags (upper word). */
#define IEM_ACCESS_SHIFT_TLB_FLAGS      16

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

/** Hint to IEMAllInstructionPython.py that this macro should be skipped.  */
#define IEMOPHINT_SKIP_PYTHON       RT_BIT_32(31)

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

#if __cplusplus >= 201700 /* P0012R1 support */
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL a_Name) a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    a_RetType VBOXCALL a_Name a_ArgList RT_NOEXCEPT
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    a_RetType VBOXCALL a_Name a_ArgList RT_NOEXCEPT

#else
# define IEM_DECL_IMPL_TYPE(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL a_Name) a_ArgList
# define IEM_DECL_IMPL_DEF(a_RetType, a_Name, a_ArgList) \
    a_RetType VBOXCALL a_Name a_ArgList
# define IEM_DECL_IMPL_PROTO(a_RetType, a_Name, a_ArgList) \
    a_RetType VBOXCALL a_Name a_ArgList

#endif

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

# if 0 /** @todo r=bird: This upsets doxygen. Generally, these macros and types probably won't change with the target arch.
        * Nor will probably the TLB definitions.  So, we need some better splitting of this code. */
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
#endif

#if defined(__GNUC__) && defined(RT_ARCH_X86)
typedef VBOXSTRICTRC (__attribute__((__fastcall__)) * PFNIEMOP)(PVMCPUCC pVCpu);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__fastcall__, __nothrow__)) a_Name(PVMCPUCC pVCpu)
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__fastcall__, __nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0)
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__fastcall__, __nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1)

#elif defined(_MSC_VER) && defined(RT_ARCH_X86)
typedef VBOXSTRICTRC (__fastcall * PFNIEMOP)(PVMCPUCC pVCpu);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC /*__declspec(naked)*/ VBOXSTRICTRC __fastcall a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1) IEM_NOEXCEPT_MAY_LONGJMP

#elif defined(__GNUC__) && !defined(IEM_WITH_THROW_CATCH)
typedef VBOXSTRICTRC (* PFNIEMOP)(PVMCPUCC pVCpu);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu)
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0)
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC __attribute__((__nothrow__)) a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1)

#else
typedef VBOXSTRICTRC (* PFNIEMOP)(PVMCPUCC pVCpu);
# define FNIEMOP_DEF(a_Name) \
    IEM_STATIC VBOXSTRICTRC a_Name(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_1(a_Name, a_Type0, a_Name0) \
    IEM_STATIC VBOXSTRICTRC a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0) IEM_NOEXCEPT_MAY_LONGJMP
# define FNIEMOP_DEF_2(a_Name, a_Type0, a_Name0, a_Type1, a_Name1) \
    IEM_STATIC VBOXSTRICTRC a_Name(PVMCPUCC pVCpu, a_Type0 a_Name0, a_Type1 a_Name1) IEM_NOEXCEPT_MAY_LONGJMP

#endif

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

/** @} */

void                    iemInitPendingBreakpointsSlow(PVMCPUCC pVCpu);


/** @name  Raising Exceptions.
 * @{ */
VBOXSTRICTRC            iemRaiseXcptOrInt(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector, uint32_t fFlags,
                                          uint16_t uErr, uint64_t uCr2) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseXcptOrIntJmp(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector,
                                             uint32_t fFlags, uint16_t uErr, uint64_t uCr2) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseDivideError(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseDebugException(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseUndefinedOpcode(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaisePageFault(PVMCPUCC pVCpu, RTGCPTR GCPtrWhere, uint32_t cbAccess, uint32_t fAccess, int rc) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaisePageFaultJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrWhere, uint32_t cbAccess, uint32_t fAccess, int rc) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseMathFault(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC            iemRaiseAlignmentCheckException(PVMCPUCC pVCpu) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
DECL_NO_RETURN(void)    iemRaiseAlignmentCheckExceptionJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP;
#endif
VBOXSTRICTRC            iemRaiseSimdFpException(PVMCPUCC pVCpu) RT_NOEXCEPT;

IEM_CIMPL_DEF_0(iemCImplRaiseDivideError);
IEM_CIMPL_DEF_0(iemCImplRaiseInvalidOpcode);

/**
 * Macro for calling iemCImplRaiseDivideError().
 *
 * This enables us to add/remove arguments and force different levels of
 * inlining as we wish.
 *
 * @return  Strict VBox status code.
 */
#define IEMOP_RAISE_DIVIDE_ERROR_RET()      IEM_MC_DEFER_TO_CIMPL_0_RET(iemCImplRaiseDivideError)

/**
 * Macro for calling iemCImplRaiseInvalidOpcode().
 *
 * This enables us to add/remove arguments and force different levels of
 * inlining as we wish.
 *
 * @return  Strict VBox status code.
 */
#define IEMOP_RAISE_INVALID_OPCODE_RET()    IEM_MC_DEFER_TO_CIMPL_0_RET(iemCImplRaiseInvalidOpcode)
/** @} */

/** @name   Memory access.
 * @{ */

VBOXSTRICTRC    iemMemMap(PVMCPUCC pVCpu, void **ppvMem, size_t cbMem, uint8_t iSegReg, RTGCPTR GCPtrMem,
                          uint32_t fAccess, uint32_t uAlignCtl) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemCommitAndUnmap(PVMCPUCC pVCpu, void *pvMem, uint32_t fAccess) RT_NOEXCEPT;
#ifndef IN_RING3
VBOXSTRICTRC    iemMemCommitAndUnmapPostponeTroubleToR3(PVMCPUCC pVCpu, void *pvMem, uint32_t fAccess) RT_NOEXCEPT;
#endif
void            iemMemRollback(PVMCPUCC pVCpu) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemApplySegment(PVMCPUCC pVCpu, uint32_t fAccess, uint8_t iSegReg, size_t cbMem, PRTGCPTR pGCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemMarkSelDescAccessed(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemPageTranslateAndCheckAccess(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbAccess, uint32_t fAccess, PRTGCPHYS pGCPhysMem) RT_NOEXCEPT;

VBOXSTRICTRC    iemMemFetchDataU8(PVMCPUCC pVCpu, uint8_t *pu8Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU32_ZX_U64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataR80(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataD80(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU128(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchDataU256(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
uint8_t         iemMemFetchDataU8Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint16_t        iemMemFetchDataU16Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint32_t        iemMemFetchDataU32Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
uint64_t        iemMemFetchDataU64Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataR80Jmp(PVMCPUCC pVCpu, PRTFLOAT80U pr80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataD80Jmp(PVMCPUCC pVCpu, PRTPBCD80U pd80Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU128Jmp(PVMCPUCC pVCpu, PRTUINT128U pu128Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemFetchDataU256Jmp(PVMCPUCC pVCpu, PRTUINT256U pu256Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP;
#endif

VBOXSTRICTRC    iemMemFetchSysU8(PVMCPUCC pVCpu, uint8_t *pu8Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU16(PVMCPUCC pVCpu, uint16_t *pu16Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU32(PVMCPUCC pVCpu, uint32_t *pu32Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemFetchSysU64(PVMCPUCC pVCpu, uint64_t *pu64Dst, uint8_t iSegReg, RTGCPTR GCPtrMem) RT_NOEXCEPT;

VBOXSTRICTRC    iemMemStoreDataU8(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint8_t u8Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU16(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint16_t u16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU32(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU64(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint64_t u64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU128(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStoreDataU256(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) RT_NOEXCEPT;
#ifdef IEM_WITH_SETJMP
void            iemMemStoreDataU8Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint8_t u8Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU16Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint16_t u16Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU32Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint32_t u32Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU64Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, uint64_t u64Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU128Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, RTUINT128U u128Value) IEM_NOEXCEPT_MAY_LONGJMP;
void            iemMemStoreDataU256Jmp(PVMCPUCC pVCpu, uint8_t iSegReg, RTGCPTR GCPtrMem, PCRTUINT256U pu256Value) IEM_NOEXCEPT_MAY_LONGJMP;
#endif

VBOXSTRICTRC    iemMemStackPushBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                            void **ppvMem, uint64_t *puNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushCommitSpecial(PVMCPUCC pVCpu, void *pvMem, uint64_t uNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU16(PVMCPUCC pVCpu, uint16_t u16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32(PVMCPUCC pVCpu, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU64(PVMCPUCC pVCpu, uint64_t u64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU16Ex(PVMCPUCC pVCpu, uint16_t u16Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32Ex(PVMCPUCC pVCpu, uint32_t u32Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU64Ex(PVMCPUCC pVCpu, uint64_t u64Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPushU32SReg(PVMCPUCC pVCpu, uint32_t u32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopBeginSpecial(PVMCPUCC pVCpu, size_t cbMem, uint32_t cbAlign,
                                           void const **ppvMem, uint64_t *puNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopContinueSpecial(PVMCPUCC pVCpu, size_t off, size_t cbMem,
                                              void const **ppvMem, uint64_t uCurNewRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopDoneSpecial(PVMCPUCC pVCpu, void const *pvMem) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU16(PVMCPUCC pVCpu, uint16_t *pu16Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU32(PVMCPUCC pVCpu, uint32_t *pu32Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU64(PVMCPUCC pVCpu, uint64_t *pu64Value) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU16Ex(PVMCPUCC pVCpu, uint16_t *pu16Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU32Ex(PVMCPUCC pVCpu, uint32_t *pu32Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
VBOXSTRICTRC    iemMemStackPopU64Ex(PVMCPUCC pVCpu, uint64_t *pu64Value, PRTUINT64U pTmpRsp) RT_NOEXCEPT;
/** @} */

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_IEMInternal_armv8_h */

