/* $Id: IEMN8veRecompiler.h 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Native Recompiler Internals.
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

#ifndef VMM_INCLUDED_SRC_include_IEMN8veRecompiler_h
#define VMM_INCLUDED_SRC_include_IEMN8veRecompiler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @defgroup grp_iem_n8ve_re   Native Recompiler Internals.
 * @ingroup grp_iem_int
 * @{
 */

#include <iprt/assertcompile.h> /* for RT_IN_ASSEMBLER mode */
#include <VBox/cdefs.h>         /* for VBOXSTRICTRC_STRICT_ENABLED */

/** @def IEMNATIVE_WITH_TB_DEBUG_INFO
 * Enables generating internal debug info for better TB disassembly dumping. */
#if defined(DEBUG) || defined(DOXYGEN_RUNNING) || 0
# define IEMNATIVE_WITH_TB_DEBUG_INFO
#endif

/** @def IEMNATIVE_WITH_LIVENESS_ANALYSIS
 * Enables liveness analysis.  */
#if 1 || defined(DOXYGEN_RUNNING)
# define IEMNATIVE_WITH_LIVENESS_ANALYSIS
#endif

/** @def IEMNATIVE_WITH_EFLAGS_SKIPPING
 * Enables skipping EFLAGS calculations/updating based on liveness info.  */
#if defined(IEMNATIVE_WITH_LIVENESS_ANALYSIS) || defined(DOXYGEN_RUNNING)
# define IEMNATIVE_WITH_EFLAGS_SKIPPING
#endif

/** @def IEMNATIVE_STRICT_EFLAGS_SKIPPING
 * Enables strict consistency checks around EFLAGS skipping.
 * @note Only defined when IEMNATIVE_WITH_EFLAGS_SKIPPING is also defined. */
#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
# ifdef VBOX_STRICT
#  define IEMNATIVE_STRICT_EFLAGS_SKIPPING
# endif
#elif defined(DOXYGEN_RUNNING)
# define IEMNATIVE_STRICT_EFLAGS_SKIPPING
#endif

/** @def IEMNATIVE_WITH_EFLAGS_POSTPONING
 * Enables delaying EFLAGS calculations/updating to conditional code paths
 * that are (hopefully) not taken so frequently.
 *
 * This can only help with case where there is an conditional
 * call/exception/tbexit that needs the flag, but in the default code stream the
 * flag will be clobbered.  Useful for TlbMiss scenarios and sequences of memory
 * based instructions clobbering status flags. */
#if defined(IEMNATIVE_WITH_LIVENESS_ANALYSIS) || defined(DOXYGEN_RUNNING)
# if 1 || defined(DOXYGEN_RUNNING)
#  define IEMNATIVE_WITH_EFLAGS_POSTPONING
# endif
#endif
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
# ifndef IEMNATIVE_WITH_EFLAGS_SKIPPING
#  error "IEMNATIVE_WITH_EFLAGS_POSTPONING requires IEMNATIVE_WITH_EFLAGS_SKIPPING at present"
# endif
#endif

/** @def IEMLIVENESS_EXTENDED_LAYOUT
 * Enables the extended liveness data layout.  */
#if defined(IEMNATIVE_WITH_EFLAGS_POSTPONING) || defined(DOXYGEN_RUNNING) || 0
# define IEMLIVENESS_EXTENDED_LAYOUT
#endif


#ifdef VBOX_WITH_STATISTICS
/** Always count instructions for now. */
# define IEMNATIVE_WITH_INSTRUCTION_COUNTING
#endif


/** @name Stack Frame Layout
 *
 * @{  */
/** The size of the area for stack variables and spills and stuff.
 * @note This limit is duplicated in the python script(s).  We add 0x40 for
 *       alignment padding. */
#define IEMNATIVE_FRAME_VAR_SIZE            (0xc0 + 0x40)
/** Number of 64-bit variable slots (0x100 / 8 = 32. */
#define IEMNATIVE_FRAME_VAR_SLOTS           (IEMNATIVE_FRAME_VAR_SIZE / 8)
AssertCompile(IEMNATIVE_FRAME_VAR_SLOTS == 32);

#ifdef RT_ARCH_AMD64
/** An stack alignment adjustment (between non-volatile register pushes and
 *  the stack variable area, so the latter better aligned). */
# define IEMNATIVE_FRAME_ALIGN_SIZE         8

/** Number of stack arguments slots for calls made from the frame. */
# ifdef RT_OS_WINDOWS
#  define IEMNATIVE_FRAME_STACK_ARG_COUNT   4
# else
#  define IEMNATIVE_FRAME_STACK_ARG_COUNT   2
# endif
/** Number of any shadow arguments (spill area) for calls we make. */
# ifdef RT_OS_WINDOWS
#  define IEMNATIVE_FRAME_SHADOW_ARG_COUNT  4
# else
#  define IEMNATIVE_FRAME_SHADOW_ARG_COUNT  0
# endif

/** Frame pointer (RBP) relative offset of the last push. */
# ifdef RT_OS_WINDOWS
#  define IEMNATIVE_FP_OFF_LAST_PUSH        (7 * -8)
# else
#  define IEMNATIVE_FP_OFF_LAST_PUSH        (5 * -8)
# endif
/** Frame pointer (RBP) relative offset of the stack variable area (the lowest
 * address for it). */
# define IEMNATIVE_FP_OFF_STACK_VARS        (IEMNATIVE_FP_OFF_LAST_PUSH - IEMNATIVE_FRAME_ALIGN_SIZE - IEMNATIVE_FRAME_VAR_SIZE)
/** Frame pointer (RBP) relative offset of the first stack argument for calls. */
# define IEMNATIVE_FP_OFF_STACK_ARG0        (IEMNATIVE_FP_OFF_STACK_VARS - IEMNATIVE_FRAME_STACK_ARG_COUNT * 8)
/** Frame pointer (RBP) relative offset of the second stack argument for calls. */
# define IEMNATIVE_FP_OFF_STACK_ARG1        (IEMNATIVE_FP_OFF_STACK_ARG0 + 8)
# ifdef RT_OS_WINDOWS
/** Frame pointer (RBP) relative offset of the third stack argument for calls. */
#  define IEMNATIVE_FP_OFF_STACK_ARG2       (IEMNATIVE_FP_OFF_STACK_ARG0 + 16)
/** Frame pointer (RBP) relative offset of the fourth stack argument for calls. */
#  define IEMNATIVE_FP_OFF_STACK_ARG3       (IEMNATIVE_FP_OFF_STACK_ARG0 + 24)
# endif

# ifdef RT_OS_WINDOWS
/** Frame pointer (RBP) relative offset of the first incoming shadow argument. */
#  define IEMNATIVE_FP_OFF_IN_SHADOW_ARG0   (16)
/** Frame pointer (RBP) relative offset of the second incoming shadow argument. */
#  define IEMNATIVE_FP_OFF_IN_SHADOW_ARG1   (24)
/** Frame pointer (RBP) relative offset of the third incoming shadow argument. */
#  define IEMNATIVE_FP_OFF_IN_SHADOW_ARG2   (32)
/** Frame pointer (RBP) relative offset of the fourth incoming shadow argument. */
#  define IEMNATIVE_FP_OFF_IN_SHADOW_ARG3   (40)
/** The offset to VBOXSTRICTRC on the stack. */
#  define IEMNATIVE_FP_OFF_VBOXSTRICRC      IEMNATIVE_FP_OFF_IN_SHADOW_ARG0
# endif

#elif RT_ARCH_ARM64
/** No alignment padding needed for arm64.
 * @note HACK ALERT! We abuse this for keeping VBOXSTRICTRC on windows, since
 *       it isn't allowed to be returned by register. */
# define IEMNATIVE_FRAME_ALIGN_SIZE        0
# ifdef VBOXSTRICTRC_STRICT_ENABLED
#  ifdef RT_OS_WINDOWS
#   undef  IEMNATIVE_FRAME_ALIGN_SIZE
#   define IEMNATIVE_FRAME_ALIGN_SIZE       16
/** The offset to VBOXSTRICTRC on the stack. */
#   define IEMNATIVE_FP_OFF_VBOXSTRICRC     (IEMNATIVE_FP_OFF_LAST_PUSH - IEMNATIVE_FRAME_ALIGN_SIZE)
#  endif
# endif
/** No stack argument slots, got 8 registers for arguments will suffice. */
# define IEMNATIVE_FRAME_STACK_ARG_COUNT    0
/** There are no argument spill area. */
# define IEMNATIVE_FRAME_SHADOW_ARG_COUNT   0

/** Number of saved registers at the top of our stack frame.
 * This includes the return address and old frame pointer, so x19 thru x30. */
# define IEMNATIVE_FRAME_SAVE_REG_COUNT     (12)
/** The size of the save registered (IEMNATIVE_FRAME_SAVE_REG_COUNT). */
# define IEMNATIVE_FRAME_SAVE_REG_SIZE      (IEMNATIVE_FRAME_SAVE_REG_COUNT * 8)

/** Frame pointer (BP) relative offset of the last push. */
# define IEMNATIVE_FP_OFF_LAST_PUSH         (10 * -8)

/** Frame pointer (BP) relative offset of the stack variable area (the lowest
 * address for it). */
# define IEMNATIVE_FP_OFF_STACK_VARS        (IEMNATIVE_FP_OFF_LAST_PUSH - IEMNATIVE_FRAME_ALIGN_SIZE - IEMNATIVE_FRAME_VAR_SIZE)

#else
# error "port me"
#endif
/** @} */


/** @name Fixed Register Allocation(s)
 * @{ */
/** @def IEMNATIVE_REG_FIXED_PVMCPU
 * The number of the register holding the pVCpu pointer.  */
/** @def IEMNATIVE_REG_FIXED_PCPUMCTX
 * The number of the register holding the &pVCpu->cpum.GstCtx pointer.
 * @note This not available on AMD64, only ARM64. */
/** @def IEMNATIVE_REG_FIXED_TMP0
 * Dedicated temporary register.
 * @note This has extremely short lifetime, must be used with great care to make
 *       sure any calling code or code being called is making use of it.
 *       It will definitely not survive a call or anything of that nature.
 * @todo replace this by a register allocator and content tracker.  */
/** @def IEMNATIVE_REG_FIXED_MASK
 * Mask GPRs with fixes assignments, either by us or dictated by the CPU/OS
 * architecture. */
/** @def IEMNATIVE_SIMD_REG_FIXED_TMP0
 * Mask SIMD registers with fixes assignments, either by us or dictated by the CPU/OS
 * architecture. */
/** @def IEMNATIVE_SIMD_REG_FIXED_TMP0
 * Dedicated temporary SIMD register. */
#if defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING) /* arm64 goes first because of doxygen */
# define IEMNATIVE_REG_FIXED_PVMCPU         ARMV8_A64_REG_X28
# define IEMNATIVE_REG_FIXED_PVMCPU_ASM     RT_CONCAT(x,IEMNATIVE_REG_FIXED_PVMCPU)
# define IEMNATIVE_REG_FIXED_PCPUMCTX       ARMV8_A64_REG_X27
# define IEMNATIVE_REG_FIXED_PCPUMCTX_ASM   RT_CONCAT(x,IEMNATIVE_REG_FIXED_PCPUMCTX)
# define IEMNATIVE_REG_FIXED_TMP0           ARMV8_A64_REG_X15
# if defined(IEMNATIVE_WITH_DELAYED_PC_UPDATING) && 0 /* debug the updating with a shadow RIP. */
#   define IEMNATIVE_REG_FIXED_TMP1         ARMV8_A64_REG_X16
#   define IEMNATIVE_REG_FIXED_PC_DBG       ARMV8_A64_REG_X26
#   define IEMNATIVE_REG_FIXED_MASK_ADD     (  RT_BIT_32(IEMNATIVE_REG_FIXED_TMP1) \
                                             | RT_BIT_32(IEMNATIVE_REG_FIXED_PC_DBG))
# else
#   define IEMNATIVE_REG_FIXED_MASK_ADD     0
# endif
# define IEMNATIVE_REG_FIXED_MASK           (  RT_BIT_32(ARMV8_A64_REG_SP) \
                                             | RT_BIT_32(ARMV8_A64_REG_LR) \
                                             | RT_BIT_32(ARMV8_A64_REG_BP) \
                                             | RT_BIT_32(IEMNATIVE_REG_FIXED_PVMCPU) \
                                             | RT_BIT_32(IEMNATIVE_REG_FIXED_PCPUMCTX) \
                                             | RT_BIT_32(ARMV8_A64_REG_X18) \
                                             | RT_BIT_32(IEMNATIVE_REG_FIXED_TMP0) \
                                             | IEMNATIVE_REG_FIXED_MASK_ADD)

# define IEMNATIVE_SIMD_REG_FIXED_TMP0      ARMV8_A64_REG_Q30
# if defined(IEMNATIVE_WITH_SIMD_REG_ACCESS_ALL_REGISTERS)
#  define IEMNATIVE_SIMD_REG_FIXED_MASK     RT_BIT_32(ARMV8_A64_REG_Q30)
# else
/** @note
 * ARM64 has 32 registers, but they are only 128-bit wide.  So, in order to
 * support emulating 256-bit registers we pair two real registers statically to
 * one virtual for now, leaving us with only 16 256-bit registers. We always
 * pair v0 with v1, v2 with v3, etc. so we mark the higher register as fixed and
 * the register allocator assumes that it will be always free when the lower is
 * picked.
 *
 * Also ARM64 declares the low 64-bit of v8-v15 as callee saved, so we don't
 * touch them in order to avoid having to save and restore them in the
 * prologue/epilogue.
 */
#  define IEMNATIVE_SIMD_REG_FIXED_MASK     (  UINT32_C(0xff00) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q31) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q30) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q29) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q27) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q25) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q23) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q21) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q19) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q17) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q15) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q13) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q11) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q9) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q7) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q5) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q3) \
                                             | RT_BIT_32(ARMV8_A64_REG_Q1))
# endif

#elif defined(RT_ARCH_AMD64)
# define IEMNATIVE_REG_FIXED_PVMCPU         X86_GREG_xBX
# define IEMNATIVE_REG_FIXED_PVMCPU_ASM     xBX
# define IEMNATIVE_REG_FIXED_TMP0           X86_GREG_x11
# define IEMNATIVE_REG_FIXED_MASK           (  RT_BIT_32(IEMNATIVE_REG_FIXED_PVMCPU) \
                                             | RT_BIT_32(IEMNATIVE_REG_FIXED_TMP0) \
                                             | RT_BIT_32(X86_GREG_xSP) \
                                             | RT_BIT_32(X86_GREG_xBP) )

# define IEMNATIVE_SIMD_REG_FIXED_TMP0      5 /* xmm5/ymm5 */
# ifndef IEMNATIVE_WITH_SIMD_REG_ACCESS_ALL_REGISTERS
#  ifndef _MSC_VER
#   define IEMNATIVE_WITH_SIMD_REG_ACCESS_ALL_REGISTERS
#  endif
# endif
# ifdef IEMNATIVE_WITH_SIMD_REG_ACCESS_ALL_REGISTERS
#  define IEMNATIVE_SIMD_REG_FIXED_MASK     (RT_BIT_32(IEMNATIVE_SIMD_REG_FIXED_TMP0))
# else
/** @note On Windows/AMD64 xmm6 through xmm15 are marked as callee saved. */
#  define IEMNATIVE_SIMD_REG_FIXED_MASK     (  UINT32_C(0xffc0) \
                                             | RT_BIT_32(IEMNATIVE_SIMD_REG_FIXED_TMP0))
# endif

#else
# error "port me"
#endif
/** @} */

/** @name Call related registers.
 * @{ */
/** @def IEMNATIVE_CALL_RET_GREG
 * The return value register. */
/** @def IEMNATIVE_CALL_ARG_GREG_COUNT
 * Number of arguments in registers. */
/** @def IEMNATIVE_CALL_ARG0_GREG
 * The general purpose register carrying argument \#0. */
/** @def IEMNATIVE_CALL_ARG1_GREG
 * The general purpose register carrying argument \#1. */
/** @def IEMNATIVE_CALL_ARG2_GREG
 * The general purpose register carrying argument \#2. */
/** @def IEMNATIVE_CALL_ARG3_GREG
 * The general purpose register carrying argument \#3. */
/** @def IEMNATIVE_CALL_VOLATILE_GREG_MASK
 * Mask of registers the callee will not save and may trash. */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_CALL_RET_GREG            X86_GREG_xAX

# ifdef RT_OS_WINDOWS
#  define IEMNATIVE_CALL_ARG_GREG_COUNT     4
#  define IEMNATIVE_CALL_ARG0_GREG          X86_GREG_xCX
#  define IEMNATIVE_CALL_ARG1_GREG          X86_GREG_xDX
#  define IEMNATIVE_CALL_ARG2_GREG          X86_GREG_x8
#  define IEMNATIVE_CALL_ARG3_GREG          X86_GREG_x9
#  define IEMNATIVE_CALL_ARGS_GREG_MASK     (  RT_BIT_32(IEMNATIVE_CALL_ARG0_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG3_GREG) )
#  define IEMNATIVE_CALL_VOLATILE_GREG_MASK (  RT_BIT_32(X86_GREG_xAX) \
                                             | RT_BIT_32(X86_GREG_xCX) \
                                             | RT_BIT_32(X86_GREG_xDX) \
                                             | RT_BIT_32(X86_GREG_x8) \
                                             | RT_BIT_32(X86_GREG_x9) \
                                             | RT_BIT_32(X86_GREG_x10) \
                                             | RT_BIT_32(X86_GREG_x11) )
/* xmm0 - xmm5 are marked as volatile. */
#  define IEMNATIVE_CALL_VOLATILE_SIMD_REG_MASK (UINT32_C(0x3f))

# else  /* !RT_OS_WINDOWS */
#  define IEMNATIVE_CALL_ARG_GREG_COUNT     6
#  define IEMNATIVE_CALL_ARG0_GREG          X86_GREG_xDI
#  define IEMNATIVE_CALL_ARG1_GREG          X86_GREG_xSI
#  define IEMNATIVE_CALL_ARG2_GREG          X86_GREG_xDX
#  define IEMNATIVE_CALL_ARG3_GREG          X86_GREG_xCX
#  define IEMNATIVE_CALL_ARG4_GREG          X86_GREG_x8
#  define IEMNATIVE_CALL_ARG5_GREG          X86_GREG_x9
#  define IEMNATIVE_CALL_ARGS_GREG_MASK     (  RT_BIT_32(IEMNATIVE_CALL_ARG0_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG3_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG4_GREG) \
                                             | RT_BIT_32(IEMNATIVE_CALL_ARG5_GREG) )
#  define IEMNATIVE_CALL_VOLATILE_GREG_MASK (  RT_BIT_32(X86_GREG_xAX) \
                                             | RT_BIT_32(X86_GREG_xCX) \
                                             | RT_BIT_32(X86_GREG_xDX) \
                                             | RT_BIT_32(X86_GREG_xDI) \
                                             | RT_BIT_32(X86_GREG_xSI) \
                                             | RT_BIT_32(X86_GREG_x8) \
                                             | RT_BIT_32(X86_GREG_x9) \
                                             | RT_BIT_32(X86_GREG_x10) \
                                             | RT_BIT_32(X86_GREG_x11) )
/* xmm0 - xmm15 are marked as volatile. */
#  define IEMNATIVE_CALL_VOLATILE_SIMD_REG_MASK (UINT32_C(0xffff))
# endif /* !RT_OS_WINDOWS */

#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_CALL_RET_GREG            ARMV8_A64_REG_X0
# define IEMNATIVE_CALL_ARG_GREG_COUNT      8
# define IEMNATIVE_CALL_ARG0_GREG           ARMV8_A64_REG_X0
# define IEMNATIVE_CALL_ARG1_GREG           ARMV8_A64_REG_X1
# define IEMNATIVE_CALL_ARG2_GREG           ARMV8_A64_REG_X2
# define IEMNATIVE_CALL_ARG3_GREG           ARMV8_A64_REG_X3
# define IEMNATIVE_CALL_ARG4_GREG           ARMV8_A64_REG_X4
# define IEMNATIVE_CALL_ARG5_GREG           ARMV8_A64_REG_X5
# define IEMNATIVE_CALL_ARG6_GREG           ARMV8_A64_REG_X6
# define IEMNATIVE_CALL_ARG7_GREG           ARMV8_A64_REG_X7
# define IEMNATIVE_CALL_ARGS_GREG_MASK      (  RT_BIT_32(ARMV8_A64_REG_X0) \
                                             | RT_BIT_32(ARMV8_A64_REG_X1) \
                                             | RT_BIT_32(ARMV8_A64_REG_X2) \
                                             | RT_BIT_32(ARMV8_A64_REG_X3) \
                                             | RT_BIT_32(ARMV8_A64_REG_X4) \
                                             | RT_BIT_32(ARMV8_A64_REG_X5) \
                                             | RT_BIT_32(ARMV8_A64_REG_X6) \
                                             | RT_BIT_32(ARMV8_A64_REG_X7) )
# define IEMNATIVE_CALL_VOLATILE_GREG_MASK  (  RT_BIT_32(ARMV8_A64_REG_X0) \
                                             | RT_BIT_32(ARMV8_A64_REG_X1) \
                                             | RT_BIT_32(ARMV8_A64_REG_X2) \
                                             | RT_BIT_32(ARMV8_A64_REG_X3) \
                                             | RT_BIT_32(ARMV8_A64_REG_X4) \
                                             | RT_BIT_32(ARMV8_A64_REG_X5) \
                                             | RT_BIT_32(ARMV8_A64_REG_X6) \
                                             | RT_BIT_32(ARMV8_A64_REG_X7) \
                                             | RT_BIT_32(ARMV8_A64_REG_X8) \
                                             | RT_BIT_32(ARMV8_A64_REG_X9) \
                                             | RT_BIT_32(ARMV8_A64_REG_X10) \
                                             | RT_BIT_32(ARMV8_A64_REG_X11) \
                                             | RT_BIT_32(ARMV8_A64_REG_X12) \
                                             | RT_BIT_32(ARMV8_A64_REG_X13) \
                                             | RT_BIT_32(ARMV8_A64_REG_X14) \
                                             | RT_BIT_32(ARMV8_A64_REG_X15) \
                                             | RT_BIT_32(ARMV8_A64_REG_X16) \
                                             | RT_BIT_32(ARMV8_A64_REG_X17) )
/* The low 64 bits of v8 - v15 marked as callee saved but the rest is volatile,
 * so to simplify our life a bit we just mark everything as volatile. */
# define IEMNATIVE_CALL_VOLATILE_SIMD_REG_MASK UINT32_C(0xffffffff)

#endif

/** This is the maximum argument count we'll ever be needing. */
#define IEMNATIVE_CALL_MAX_ARG_COUNT        7
#ifdef RT_OS_WINDOWS
# ifdef VBOXSTRICTRC_STRICT_ENABLED
#  undef IEMNATIVE_CALL_MAX_ARG_COUNT
#  define IEMNATIVE_CALL_MAX_ARG_COUNT      8
# endif
#endif

/** @def IEMNATIVE_CALL_VOLATILE_NOTMP_GREG_MASK
 * Variant of IEMNATIVE_CALL_VOLATILE_GREG_MASK that excludes
 * IEMNATIVE_REG_FIXED_TMP0 on hosts that uses it. */
#ifdef IEMNATIVE_REG_FIXED_TMP0
# ifdef IEMNATIVE_REG_FIXED_TMP1
#  define IEMNATIVE_CALL_VOLATILE_NOTMP_GREG_MASK   (  IEMNATIVE_CALL_VOLATILE_GREG_MASK \
                                                     & ~(  RT_BIT_32(IEMNATIVE_REG_FIXED_TMP0) \
                                                         | RT_BIT_32(IEMNATIVE_REG_FIXED_TMP1)))
# else
#  define IEMNATIVE_CALL_VOLATILE_NOTMP_GREG_MASK   (IEMNATIVE_CALL_VOLATILE_GREG_MASK & ~RT_BIT_32(IEMNATIVE_REG_FIXED_TMP0))
# endif
#else
# define IEMNATIVE_CALL_VOLATILE_NOTMP_GREG_MASK    IEMNATIVE_CALL_VOLATILE_GREG_MASK
#endif

/** @def IEMNATIVE_CALL_NONVOLATILE_GREG_MASK
 * The allocatable non-volatile general purpose register set.  */
#define IEMNATIVE_CALL_NONVOLATILE_GREG_MASK \
    (~IEMNATIVE_CALL_VOLATILE_GREG_MASK & ~IEMNATIVE_REG_FIXED_MASK & IEMNATIVE_HST_GREG_MASK)
/** @} */


/** @def IEMNATIVE_HST_GREG_COUNT
 * Number of host general purpose registers we tracker. */
/** @def IEMNATIVE_HST_GREG_MASK
 * Mask corresponding to IEMNATIVE_HST_GREG_COUNT that can be applied to
 * inverted register masks and such to get down to a correct set of regs. */
/** @def IEMNATIVE_HST_SIMD_REG_COUNT
 * Number of host SIMD registers we track. */
/** @def IEMNATIVE_HST_SIMD_REG_MASK
 * Mask corresponding to IEMNATIVE_HST_SIMD_REG_COUNT that can be applied to
 * inverted register masks and such to get down to a correct set of regs. */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_HST_GREG_COUNT           16
# define IEMNATIVE_HST_GREG_MASK            UINT32_C(0xffff)

# define IEMNATIVE_HST_SIMD_REG_COUNT       16
# define IEMNATIVE_HST_SIMD_REG_MASK        UINT32_C(0xffff)

#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_HST_GREG_COUNT           32
# define IEMNATIVE_HST_GREG_MASK            UINT32_MAX

# define IEMNATIVE_HST_SIMD_REG_COUNT       32
# define IEMNATIVE_HST_SIMD_REG_MASK        UINT32_MAX

#else
# error "Port me!"
#endif


#ifndef RT_IN_ASSEMBLER /* ASM-NOINC-START - the rest of the file */


/** Native code generator label types. */
typedef enum
{
    kIemNativeLabelType_Invalid = 0,
    /** @name Exit reasons - Labels w/o data, only once instance per TB.
     *
     * The labels requiring register inputs are documented.
     *
     * @note Jumps to these requires instructions that are capable of spanning the
     *       max TB length.
     * @{
     */
    /* Simple labels comes first for indexing reasons. RaiseXx is order by the exception's numerical value(s). */
    kIemNativeLabelType_RaiseDe,                /**< Raise (throw) X86_XCPT_DE (00h). */
    kIemNativeLabelType_RaiseUd,                /**< Raise (throw) X86_XCPT_UD (06h). */
    kIemNativeLabelType_RaiseSseRelated,        /**< Raise (throw) X86_XCPT_UD or X86_XCPT_NM according to cr0 & cr4. */
    kIemNativeLabelType_RaiseAvxRelated,        /**< Raise (throw) X86_XCPT_UD or X86_XCPT_NM according to xcr0, cr0 & cr4. */
    kIemNativeLabelType_RaiseSseAvxFpRelated,   /**< Raise (throw) X86_XCPT_UD or X86_XCPT_XF according to c4. */
    kIemNativeLabelType_RaiseNm,                /**< Raise (throw) X86_XCPT_NM (07h). */
    kIemNativeLabelType_RaiseGp0,               /**< Raise (throw) X86_XCPT_GP (0dh) w/ errcd=0. */
    kIemNativeLabelType_RaiseMf,                /**< Raise (throw) X86_XCPT_MF (10h). */
    kIemNativeLabelType_RaiseXf,                /**< Raise (throw) X86_XCPT_XF (13h). */
    kIemNativeLabelType_ObsoleteTb,             /**< Calls iemNativeHlpObsoleteTb (no inputs). */
    kIemNativeLabelType_NeedCsLimChecking,      /**< Calls iemNativeHlpNeedCsLimChecking (no inputs). */
    kIemNativeLabelType_CheckBranchMiss,        /**< Calls iemNativeHlpCheckBranchMiss (no inputs). */
    kIemNativeLabelType_LastSimple = kIemNativeLabelType_CheckBranchMiss,

    /* Manually defined labels: */
    /**< Returns with VINF_SUCCESS, no inputs. */
    kIemNativeLabelType_ReturnSuccess,
    /** Returns with VINF_IEM_REEXEC_FINISH_WITH_FLAGS, no inputs. */
    kIemNativeLabelType_ReturnWithFlags,
    /** Returns with VINF_IEM_REEXEC_BREAK, no inputs. */
    kIemNativeLabelType_ReturnBreak,
    /** Returns with VINF_IEM_REEXEC_BREAK_FF, no inputs. */
    kIemNativeLabelType_ReturnBreakFF,
    /** The last TB exit label that doesn't have any input registers. */
    kIemNativeLabelType_LastTbExitWithoutInputs = kIemNativeLabelType_ReturnBreakFF,

    /** Argument registers 1, 2 & 3 are set up.  */
    kIemNativeLabelType_ReturnBreakViaLookup,
    /** Argument registers 1, 2 & 3 are set up.  */
    kIemNativeLabelType_ReturnBreakViaLookupWithIrq,
    /** Argument registers 1 & 2 are set up.  */
    kIemNativeLabelType_ReturnBreakViaLookupWithTlb,
    /** Argument registers 1 & 2 are set up.  */
    kIemNativeLabelType_ReturnBreakViaLookupWithTlbAndIrq,
    /** Return register holds the RC and the instruction number is in CL/RCX
     * on amd64 and the 2rd argument register elsewhere. */
    kIemNativeLabelType_NonZeroRetOrPassUp,

    /** The last fixup for branches that can span almost the whole TB length.
     * @note Whether kIemNativeLabelType_Return needs to be one of these is
     *       a bit questionable, since nobody jumps to it except other tail code. */
    kIemNativeLabelType_LastWholeTbBranch = kIemNativeLabelType_NonZeroRetOrPassUp,
    /** The last fixup for branches that exits the TB. */
    kIemNativeLabelType_LastTbExit        = kIemNativeLabelType_NonZeroRetOrPassUp,
    /** @} */

    /** Loop-jump target. */
    kIemNativeLabelType_LoopJumpTarget,

    /*
     * Labels with data, potentially multiple instances per TB:
     *
     * These are localized labels, so no fixed jump type restrictions here.
     */
    kIemNativeLabelType_FirstWithMultipleInstances,
    kIemNativeLabelType_If = kIemNativeLabelType_FirstWithMultipleInstances,
    kIemNativeLabelType_Else,
    kIemNativeLabelType_Endif,
    kIemNativeLabelType_CheckIrq,
    kIemNativeLabelType_TlbLookup,
    kIemNativeLabelType_TlbMiss,
    kIemNativeLabelType_TlbDone,
    kIemNativeLabelType_End
} IEMNATIVELABELTYPE;

#define IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmLabel) \
    ((a_enmLabel) <= kIemNativeLabelType_LastTbExit && (a_enmLabel) > kIemNativeLabelType_Invalid)

#define IEMNATIVELABELTYPE_IS_EXIT_WITHOUT_INPUTS(a_enmLabel) \
    ((a_enmLabel) <= kIemNativeLabelType_LastTbExitWithoutInputs && (a_enmLabel) > kIemNativeLabelType_Invalid)

/**
 * Get the mask of input registers for an TB exit label.
 * This will return zero for any non-exit lable.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVELABELTYPE_GET_INPUT_REG_MASK(a_enmLabel) \
    (     (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookup \
       || (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookupWithIrq \
     ? RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG) | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) | RT_BIT_32(IEMNATIVE_CALL_ARG3_GREG) \
     :    (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookupWithTlb \
       || (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookupWithTlbAndIrq \
     ? RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG) | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) \
     : (a_enmLabel) == kIemNativeLabelType_NonZeroRetOrPassUp \
     ? RT_BIT_32(IEMNATIVE_CALL_RET_GREG)  | RT_BIT_32(X86_GREG_xCX) /* <-- the difference */ \
     : 0)
# else
# define IEMNATIVELABELTYPE_GET_INPUT_REG_MASK(a_enmLabel) \
    (     (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookup \
       || (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookupWithIrq \
     ? RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG) | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) | RT_BIT_32(IEMNATIVE_CALL_ARG3_GREG) \
     :    (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookupWithTlb \
       || (a_enmLabel) == kIemNativeLabelType_ReturnBreakViaLookupWithTlbAndIrq \
     ? RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG) | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) \
     : (a_enmLabel) == kIemNativeLabelType_NonZeroRetOrPassUp \
     ? RT_BIT_32(IEMNATIVE_CALL_RET_GREG)  | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG) \
     : 0)
#endif


/** Native code generator label definition. */
typedef struct IEMNATIVELABEL
{
    /** Code offset if defined, UINT32_MAX if it needs to be generated after/in
     * the epilog. */
    uint32_t    off;
    /** The type of label (IEMNATIVELABELTYPE). */
    uint16_t    enmType;
    /** Additional label data, type specific. */
    uint16_t    uData;
} IEMNATIVELABEL;
/** Pointer to a label. */
typedef IEMNATIVELABEL *PIEMNATIVELABEL;



/** Native code generator fixup types.  */
typedef enum
{
    kIemNativeFixupType_Invalid = 0,
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /** AMD64 fixup: PC relative 32-bit with addend in bData. */
    kIemNativeFixupType_Rel32,
#elif defined(RT_ARCH_ARM64)
    /** ARM64 fixup: PC relative offset at bits 25:0 (B, BL).  */
    kIemNativeFixupType_RelImm26At0,
    /** ARM64 fixup: PC relative offset at bits 23:5 (CBZ, CBNZ, B.CC).  */
    kIemNativeFixupType_RelImm19At5,
    /** ARM64 fixup: PC relative offset at bits 18:5 (TBZ, TBNZ).  */
    kIemNativeFixupType_RelImm14At5,
#endif
    kIemNativeFixupType_End
} IEMNATIVEFIXUPTYPE;

/** Native code generator fixup. */
typedef struct IEMNATIVEFIXUP
{
    /** Code offset of the fixup location. */
    uint32_t    off;
    /** The IEMNATIVELABEL this is a fixup for. */
    uint16_t    idxLabel;
    /** The fixup type (IEMNATIVEFIXUPTYPE). */
    uint8_t     enmType;
    /** Addend or other data. */
    int8_t      offAddend;
} IEMNATIVEFIXUP;
/** Pointer to a native code generator fixup. */
typedef IEMNATIVEFIXUP *PIEMNATIVEFIXUP;



/** Native code generator fixup to per chunk TB tail code. */
typedef struct IEMNATIVEEXITFIXUP
{
    /** Code offset of the fixup location. */
    uint32_t            off;
    /** The exit reason. */
    IEMNATIVELABELTYPE  enmExitReason;
} IEMNATIVEEXITFIXUP;
/** Pointer to a native code generator TB exit fixup. */
typedef IEMNATIVEEXITFIXUP *PIEMNATIVEEXITFIXUP;

/**
 * Per executable memory chunk context with addresses for common code.
 */
typedef struct IEMNATIVEPERCHUNKCTX
{
    /** Pointers to the exit labels */
    PIEMNATIVEINSTR apExitLabels[kIemNativeLabelType_LastTbExit + 1];
} IEMNATIVEPERCHUNKCTX;
/** Pointer to per-chunk recompiler context. */
typedef IEMNATIVEPERCHUNKCTX *PIEMNATIVEPERCHUNKCTX;
/** Pointer to const per-chunk recompiler context. */
typedef const IEMNATIVEPERCHUNKCTX *PCIEMNATIVEPERCHUNKCTX;



/**
 * One bit of the state.
 *
 * Each register state takes up two bits.  We keep the two bits in two separate
 * 64-bit words to simplify applying them to the guest shadow register mask in
 * the register allocator.
 */
typedef union IEMLIVENESSBIT
{
    uint64_t        bm64;
    RT_GCC_EXTENSION struct
    {                                     /*   bit no */
        uint64_t    bmGprs      : 16;   /**< 0x00 /  0: The 16 general purpose registers. */
        uint64_t    fCr0        :  1;   /**< 0x10 / 16: */
        uint64_t    fCr4        :  1;   /**< 0x11 / 17: */
        uint64_t    fFcw        :  1;   /**< 0x12 / 18: */
        uint64_t    fFsw        :  1;   /**< 0x13 / 19: */
        uint64_t    bmSegBase   :  6;   /**< 0x14 / 20: */
        uint64_t    bmSegAttrib :  6;   /**< 0x1a / 26: */
        uint64_t    bmSegLimit  :  6;   /**< 0x20 / 32: */
        uint64_t    bmSegSel    :  6;   /**< 0x26 / 38: */
        uint64_t    fXcr0       :  1;   /**< 0x2c / 44: */
        uint64_t    fMxCsr      :  1;   /**< 0x2d / 45: */
        uint64_t    fEflOther   :  1;   /**< 0x2e / 46: Other EFLAGS bits   (~X86_EFL_STATUS_BITS & X86_EFL_LIVE_MASK). First! */
        uint64_t    fEflCf      :  1;   /**< 0x2f / 47: Carry flag          (X86_EFL_CF / 0). */
        uint64_t    fEflPf      :  1;   /**< 0x30 / 48: Parity flag         (X86_EFL_PF / 2). */
        uint64_t    fEflAf      :  1;   /**< 0x31 / 59: Auxilary carry flag (X86_EFL_AF / 4). */
        uint64_t    fEflZf      :  1;   /**< 0x32 / 50: Zero flag           (X86_EFL_ZF / 6). */
        uint64_t    fEflSf      :  1;   /**< 0x33 / 51: Signed flag         (X86_EFL_SF / 7). */
        uint64_t    fEflOf      :  1;   /**< 0x34 / 52: Overflow flag       (X86_EFL_OF / 12). */
        uint64_t    fUnusedPc   :  1;   /**< 0x35 / 53: (PC in ) */
        uint64_t    uUnused     : 10;     /* 0x36 / 54 -> 0x40/64 */
    };
} IEMLIVENESSBIT;
AssertCompileSize(IEMLIVENESSBIT, 8);

#define IEMLIVENESSBIT_IDX_EFL_OTHER    ((unsigned)kIemNativeGstReg_EFlags + 0)
#define IEMLIVENESSBIT_IDX_EFL_CF       ((unsigned)kIemNativeGstReg_EFlags + 1)
#define IEMLIVENESSBIT_IDX_EFL_PF       ((unsigned)kIemNativeGstReg_EFlags + 2)
#define IEMLIVENESSBIT_IDX_EFL_AF       ((unsigned)kIemNativeGstReg_EFlags + 3)
#define IEMLIVENESSBIT_IDX_EFL_ZF       ((unsigned)kIemNativeGstReg_EFlags + 4)
#define IEMLIVENESSBIT_IDX_EFL_SF       ((unsigned)kIemNativeGstReg_EFlags + 5)
#define IEMLIVENESSBIT_IDX_EFL_OF       ((unsigned)kIemNativeGstReg_EFlags + 6)
#define IEMLIVENESSBIT_IDX_EFL_COUNT    7


/**
 * A liveness state entry.
 *
 * The first 128 bits runs parallel to kIemNativeGstReg_xxx for the most part.
 * Once we add a SSE register shadowing, we'll add another 64-bit element for
 * that.
 */
typedef union IEMLIVENESSENTRY
{
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
    uint64_t        bm64[16 / 8];
    uint16_t        bm32[16 / 4];
    uint16_t        bm16[16 / 2];
    uint8_t         bm8[ 16 / 1];
    IEMLIVENESSBIT  aBits[2];
#else
    uint64_t        bm64[32 / 8];
    uint16_t        bm32[32 / 4];
    uint16_t        bm16[32 / 2];
    uint8_t         bm8[ 32 / 1];
    IEMLIVENESSBIT  aBits[4];
#endif
    RT_GCC_EXTENSION struct
    {
        /** Bit \#0 of the register states. */
        IEMLIVENESSBIT Bit0;
        /** Bit \#1 of the register states. */
        IEMLIVENESSBIT Bit1;
#ifdef IEMLIVENESS_EXTENDED_LAYOUT
        /** Bit \#2 of the register states. */
        IEMLIVENESSBIT Bit2;
        /** Bit \#3 of the register states. */
        IEMLIVENESSBIT Bit3;
#endif
    };
} IEMLIVENESSENTRY;
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
AssertCompileSize(IEMLIVENESSENTRY, 16);
#else
AssertCompileSize(IEMLIVENESSENTRY, 32);
#endif
/** Pointer to a liveness state entry. */
typedef IEMLIVENESSENTRY *PIEMLIVENESSENTRY;
/** Pointer to a const liveness state entry. */
typedef IEMLIVENESSENTRY const *PCIEMLIVENESSENTRY;

/** @name 64-bit value masks for IEMLIVENESSENTRY.
 * @{ */                                      /*         0xzzzzyyyyxxxxwwww */
#define IEMLIVENESSBIT_MASK                     UINT64_C(0x001fffffffffffff)

#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEMLIVENESSBIT0_XCPT_OR_CALL           UINT64_C(0x0000000000000000)
# define IEMLIVENESSBIT1_XCPT_OR_CALL           IEMLIVENESSBIT_MASK

# define IEMLIVENESSBIT0_ALL_UNUSED             IEMLIVENESSBIT_MASK
# define IEMLIVENESSBIT1_ALL_UNUSED             UINT64_C(0x0000000000000000)
#endif

#define IEMLIVENESSBIT_ALL_EFL_MASK             UINT64_C(0x001fc00000000000)
#define IEMLIVENESSBIT_STATUS_EFL_MASK          UINT64_C(0x001f800000000000)

#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEMLIVENESSBIT0_ALL_EFL_INPUT          IEMLIVENESSBIT_ALL_EFL_MASK
# define IEMLIVENESSBIT1_ALL_EFL_INPUT          IEMLIVENESSBIT_ALL_EFL_MASK
#endif
/** @} */


/** @name The liveness state for a register.
 *
 * The state values have been picked to with state accumulation in mind (what
 * the iemNativeLivenessFunc_xxxx functions does), as that is the most
 * performance critical work done with the values.
 *
 * This is a compressed state that only requires 2 bits per register.
 * When accumulating state, we'll be using three IEMLIVENESSENTRY copies:
 *      1. the incoming state from the following call,
 *      2. the outgoing state for this call,
 *      3. mask of the entries set in the 2nd.
 *
 * The mask entry (3rd one above) will be used both when updating the outgoing
 * state and when merging in incoming state for registers not touched by the
 * current call.
 *
 *
 * Extended Layout:
 *
 * The extended layout variation differs from the above as it records the
 * different register accesses as individual bits, and it is currently used for
 * the delayed EFLAGS calculation experiments.   The latter means that
 * calls/tb-exits and potential calls/exceptions/tb-exits are recorded
 * separately so the latter can be checked for in combination with clobbering.
 *
 * @{ */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
/** The register will be clobbered and the current value thrown away.
 *
 * When this is applied to the state (2) we'll simply be AND'ing it with the
 * (old) mask (3) and adding the register to the mask.   This way we'll
 * preserve the high priority IEMLIVENESS_STATE_XCPT_OR_CALL and
 * IEMLIVENESS_STATE_INPUT states. */
# define IEMLIVENESS_STATE_CLOBBERED    0
/** The register is unused in the remainder of the TB.
 *
 * This is an initial state and can not be set by any of the
 * iemNativeLivenessFunc_xxxx callbacks. */
# define IEMLIVENESS_STATE_UNUSED       1
/** The register value is required in a potential call or exception.
 *
 * This means that the register value must be calculated and is best written to
 * the state, but that any shadowing registers can be flushed thereafter as it's
 * not used again.  This state has lower priority than IEMLIVENESS_STATE_INPUT.
 *
 * It is typically applied across the board, but we preserve incoming
 * IEMLIVENESS_STATE_INPUT values.  This latter means we have to do some extra
 * trickery to filter out IEMLIVENESS_STATE_UNUSED:
 *      1. r0 = old & ~mask;
 *      2. r0 = t1 & (t1 >> 1);
 *      3. state |= r0 | 0b10;
 *      4. mask = ~0;
 */
# define IEMLIVENESS_STATE_XCPT_OR_CALL 2
/** The register value is used as input.
 *
 * This means that the register value must be calculated and it is best to keep
 * it in a register.  It does not need to be writtent out as such.  This is the
 * highest priority state.
 *
 * Whether the call modifies the register or not isn't relevant to earlier
 * calls, so that's not recorded.
 *
 * When applying this state we just or in the value in the outgoing state and
 * mask. */
# define IEMLIVENESS_STATE_INPUT        3
/** Mask of the state bits.   */
# define IEMLIVENESS_STATE_MASK         3
/** The number of bits per state.   */
# define IEMLIVENESS_STATE_BIT_COUNT    2

/** Check if we're expecting read & write accesses to a register with the given (previous) liveness state.
 * @note only used in assertions. */
# define IEMLIVENESS_STATE_IS_MODIFY_EXPECTED(a_uState)  ((uint32_t)((a_uState) - 1U) >= (uint32_t)(IEMLIVENESS_STATE_INPUT - 1U))
/** Check if we're expecting read accesses to a register with the given (previous) liveness state.
 * @note only used in assertions. */
# define IEMLIVENESS_STATE_IS_INPUT_EXPECTED(a_uState)   IEMLIVENESS_STATE_IS_MODIFY_EXPECTED(a_uState)
/** Check if a register clobbering is expected given the (previous) liveness state.
 * The state must be either CLOBBERED or XCPT_OR_CALL, but it may also
 * include INPUT if the register is used in more than one place.
 * @note only used in assertions. */
# define IEMLIVENESS_STATE_IS_CLOBBER_EXPECTED(a_uState) ((uint32_t)(a_uState) != IEMLIVENESS_STATE_UNUSED)

/** Check if all status flags are going to be clobbered and doesn't need
 *  calculating in the current step.
 * @param a_pCurEntry  The current liveness entry.
 * @note  Used by actual code. */
# define IEMLIVENESS_STATE_ARE_STATUS_EFL_TO_BE_CLOBBERED(a_pCurEntry) \
    ( (((a_pCurEntry)->Bit0.bm64 | (a_pCurEntry)->Bit1.bm64) & IEMLIVENESSBIT_STATUS_EFL_MASK) == 0 )

/***
 * Construct a mask of what will be clobbered and never used.
 *
 * This is mainly used with IEMLIVENESSBIT_STATUS_EFL_MASK to avoid
 * unnecessary EFLAGS calculations.
 *
 * @param a_pCurEntry  The current liveness entry.
 * @note  Used by actual code.
 */
# define IEMLIVENESS_STATE_GET_WILL_BE_CLOBBERED_SET(a_pCurEntry) \
    ( ~((a_pCurEntry)->Bit0.bm64 | (a_pCurEntry)->Bit1.bm64) & IEMLIVENESSBIT_MASK )

/** Construct a mask of the guest registers in the UNUSED and XCPT_OR_CALL
 *  states, as these are no longer needed.
 * @param a_pCurEntry  The current liveness entry.
 * @note  Used by actual code. */
AssertCompile(IEMLIVENESS_STATE_UNUSED == 1 && IEMLIVENESS_STATE_XCPT_OR_CALL == 2);
# define IEMLIVENESS_STATE_GET_CAN_BE_FREED_SET(a_pCurEntry) \
    ( (a_pCurEntry)->Bit0.bm64 ^ (a_pCurEntry)->Bit1.bm64 )


#else  /* IEMLIVENESS_EXTENDED_LAYOUT */
/** The register is not used any more. */
# define IEMLIVENESS_STATE_UNUSED           0
/** Flag: The register is required in a potential call or/and exception. */
# define IEMLIVENESS_STATE_POTENTIAL_CALL   1
# define IEMLIVENESS_BIT_POTENTIAL_CALL     0
/** Flag: The register is read. */
# define IEMLIVENESS_STATE_READ             2
# define IEMLIVENESS_BIT_READ               1
/** Flag: The register is written. */
# define IEMLIVENESS_STATE_WRITE            4
# define IEMLIVENESS_BIT_WRITE              2
/** Flag: Unconditional call. */
# define IEMLIVENESS_STATE_CALL             8
# define IEMLIVENESS_BIT_CALL               3

# define IEMLIVENESS_STATE_IS_MODIFY_EXPECTED(a_uState) \
    ( ((a_uState) & (IEMLIVENESS_STATE_WRITE | IEMLIVENESS_STATE_READ)) == (IEMLIVENESS_STATE_WRITE | IEMLIVENESS_STATE_READ) )
# define IEMLIVENESS_STATE_IS_INPUT_EXPECTED(a_uState)   RT_BOOL((a_uState) & IEMLIVENESS_STATE_READ)
# define IEMLIVENESS_STATE_IS_CLOBBER_EXPECTED(a_uState) RT_BOOL((a_uState) & IEMLIVENESS_STATE_WRITE)

# define IEMLIVENESS_STATE_ARE_STATUS_EFL_TO_BE_CLOBBERED(a_pCurEntry)  \
    (   ((a_pCurEntry)->aBits[IEMLIVENESS_BIT_WRITE].bm64 & IEMLIVENESSBIT_STATUS_EFL_MASK) == IEMLIVENESSBIT_STATUS_EFL_MASK \
     && !(  (  (a_pCurEntry)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 \
             | (a_pCurEntry)->aBits[IEMLIVENESS_BIT_READ].bm64 \
             | (a_pCurEntry)->aBits[IEMLIVENESS_BIT_CALL].bm64) \
          & IEMLIVENESSBIT_STATUS_EFL_MASK) )

/** Construct a mask of the registers not in the read or write state.
 * @note  We could skips writes, if they aren't from us, as this is just a hack
 *        to prevent trashing registers that have just been written or will be
 *        written when we retire the current instruction.
 * @param a_pCurEntry  The current liveness entry.
 * @note  Used by actual code. */
# define IEMLIVENESS_STATE_GET_CAN_BE_FREED_SET(a_pCurEntry) \
    (  ~(a_pCurEntry)->aBits[IEMLIVENESS_BIT_READ].bm64 \
     & ~(a_pCurEntry)->aBits[IEMLIVENESS_BIT_WRITE].bm64 \
     & IEMLIVENESSBIT_MASK )

/***
 * Construct a mask of what will be clobbered and never used.
 *
 * This is mainly used with IEMLIVENESSBIT_STATUS_EFL_MASK to avoid
 * unnecessary EFLAGS calculations.
 *
 * @param a_pCurEntry  The current liveness entry.
 * @note  Used by actual code.
 */
# define IEMLIVENESS_STATE_GET_WILL_BE_CLOBBERED_SET(a_pCurEntry) \
    (  (a_pCurEntry)->aBits[IEMLIVENESS_BIT_WRITE].bm64 \
     & ~(  (a_pCurEntry)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 \
         | (a_pCurEntry)->aBits[IEMLIVENESS_BIT_READ].bm64 \
         | (a_pCurEntry)->aBits[IEMLIVENESS_BIT_CALL].bm64) )

/**
 * Construct a mask of what (EFLAGS) which can be postponed.
 *
 * The postponement is for the avoiding EFLAGS status bits calculations in the
 * primary code stream whenever possible, and instead only do these in the TLB
 * load and TB exit code paths which shouldn't be traveled quite as often.
 * A requirement, though, is that the status bits will be clobbered later in the
 * TB.
 *
 * User need to apply IEMLIVENESSBIT_STATUS_EFL_MASK if appropriate/necessary.
 *
 * @param a_pCurEntry  The current liveness entry.
 * @note  Used by actual code.
 */
# define IEMLIVENESS_STATE_GET_CAN_BE_POSTPONED_SET(a_pCurEntry) \
    (  (a_pCurEntry)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 \
     & (a_pCurEntry)->aBits[IEMLIVENESS_BIT_WRITE].bm64 \
     & ~(  (a_pCurEntry)->aBits[IEMLIVENESS_BIT_READ].bm64 \
         | (a_pCurEntry)->aBits[IEMLIVENESS_BIT_CALL].bm64) )

#endif /* IEMLIVENESS_EXTENDED_LAYOUT */
/** @} */

/** @name Liveness helpers for builtin functions and similar.
 *
 * These are not used by IEM_MC_BEGIN/END blocks, IEMAllN8veLiveness.cpp has its
 * own set of manipulator macros for those.
 *
 * @{ */
/** Initializing the state as all unused. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_INIT_AS_UNUSED(a_pOutgoing) \
    do { \
        (a_pOutgoing)->Bit0.bm64 = IEMLIVENESSBIT0_ALL_UNUSED; \
        (a_pOutgoing)->Bit1.bm64 = IEMLIVENESSBIT1_ALL_UNUSED; \
    } while (0)
#else
# define IEM_LIVENESS_RAW_INIT_AS_UNUSED(a_pOutgoing) \
    do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ          ].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_WRITE         ].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_CALL          ].bm64 = 0; \
    } while (0)
#endif

/** Initializing the outgoing state with a potential xcpt or call state.
 * This only works when all later changes will be IEMLIVENESS_STATE_INPUT.
 *
 * @note Must invoke IEM_LIVENESS_RAW_FINISH_WITH_POTENTIAL_CALL when done!
 */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_INIT_WITH_POTENTIAL_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        (a_pOutgoing)->Bit0.bm64 = (a_pIncoming)->Bit0.bm64 & (a_pIncoming)->Bit1.bm64; \
        (a_pOutgoing)->Bit1.bm64 = IEMLIVENESSBIT1_XCPT_OR_CALL; \
    } while (0)
#else
# define IEM_LIVENESS_RAW_INIT_WITH_POTENTIAL_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 = IEMLIVENESSBIT_MASK; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ          ].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_WRITE         ].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_CALL          ].bm64 = 0; \
    } while (0)
#endif

/** Completes IEM_LIVENESS_RAW_INIT_WITH_POTENTIAL_CALL after applying any
 * other state modifications.
 */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_FINISH_WITH_POTENTIAL_CALL(a_pOutgoing, a_pIncoming) ((void)0)
#else
# define IEM_LIVENESS_RAW_FINISH_WITH_POTENTIAL_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        uint64_t const fInhMask = ~(  (a_pOutgoing)->aBits[IEMLIVENESS_BIT_CALL].bm64 \
                                    | (a_pOutgoing)->aBits[IEMLIVENESS_BIT_WRITE].bm64); \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 |= (a_pIncoming)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 & fInhMask; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ          ].bm64 |= (a_pIncoming)->aBits[IEMLIVENESS_BIT_READ].bm64  & fInhMask; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_WRITE         ].bm64 |= (a_pIncoming)->aBits[IEMLIVENESS_BIT_WRITE].bm64 & fInhMask; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_CALL          ].bm64 |= (a_pIncoming)->aBits[IEMLIVENESS_BIT_CALL].bm64  & fInhMask; \
    } while (0)
#endif

/** Initializing the outgoing state with an unconditional call state.
 * This should only really be used alone. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_INIT_WITH_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        (a_pOutgoing)->Bit0.bm64 = (a_pIncoming)->Bit0.bm64 & (a_pIncoming)->Bit1.bm64; \
        (a_pOutgoing)->Bit1.bm64 = IEMLIVENESSBIT1_XCPT_OR_CALL; \
    } while (0)
#else
# define IEM_LIVENESS_RAW_INIT_WITH_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_CALL          ].bm64 = IEMLIVENESSBIT_MASK; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ          ].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_WRITE         ].bm64 = 0; \
        RT_NOREF(a_pIncoming); \
    } while (0)
#endif

#if 0 /* unused */
/** Initializing the outgoing state with an unconditional call state as well as
 *  an potential call/exception preceding it.
 * This should only really be used alone. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_INIT_WITH_CALL_AND_POTENTIAL_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        (a_pOutgoing)->Bit0.bm64 = (a_pIncoming)->Bit0.bm64 & (a_pIncoming)->Bit1.bm64; \
        (a_pOutgoing)->Bit1.bm64 = IEMLIVENESSBIT1_XCPT_OR_CALL; \
    } while (0)
#else
# define IEM_LIVENESS_RAW_INIT_WITH_CALL_AND_POTENTIAL_CALL(a_pOutgoing, a_pIncoming) \
    do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_POTENTIAL_CALL].bm64 = IEMLIVENESSBIT_MASK; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_CALL          ].bm64 = IEMLIVENESSBIT_MASK; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ          ].bm64 = 0; \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_WRITE         ].bm64 = 0; \
    } while (0)
#endif
#endif

/** Adds a segment base register as input to the outgoing state. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_SEG_BASE_INPUT(a_pOutgoing, a_iSReg) do { \
        (a_pOutgoing)->Bit0.bmSegBase   |= RT_BIT_64(a_iSReg); \
        (a_pOutgoing)->Bit1.bmSegBase   |= RT_BIT_64(a_iSReg); \
    } while (0)
#else
# define IEM_LIVENESS_RAW_SEG_BASE_INPUT(a_pOutgoing, a_iSReg) do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ].bmSegBase |= RT_BIT_64(a_iSReg); \
    } while (0)
#endif

/** Adds a segment attribute register as input to the outgoing state. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_SEG_ATTRIB_INPUT(a_pOutgoing, a_iSReg) do { \
        (a_pOutgoing)->Bit0.bmSegAttrib |= RT_BIT_64(a_iSReg); \
        (a_pOutgoing)->Bit1.bmSegAttrib |= RT_BIT_64(a_iSReg); \
    } while (0)
#else
# define IEM_LIVENESS_RAW_SEG_ATTRIB_INPUT(a_pOutgoing, a_iSReg) do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ].bmSegAttrib |= RT_BIT_64(a_iSReg); \
    } while (0)
#endif

/** Adds a segment limit register as input to the outgoing state. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_SEG_LIMIT_INPUT(a_pOutgoing, a_iSReg) do { \
        (a_pOutgoing)->Bit0.bmSegLimit  |= RT_BIT_64(a_iSReg); \
        (a_pOutgoing)->Bit1.bmSegLimit  |= RT_BIT_64(a_iSReg); \
    } while (0)
#else
# define IEM_LIVENESS_RAW_SEG_LIMIT_INPUT(a_pOutgoing, a_iSReg) do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ].bmSegLimit |= RT_BIT_64(a_iSReg); \
    } while (0)
#endif

/** Adds a segment limit register as input to the outgoing state. */
#ifndef IEMLIVENESS_EXTENDED_LAYOUT
# define IEM_LIVENESS_RAW_EFLAGS_ONE_INPUT(a_pOutgoing, a_fEflMember) do { \
        (a_pOutgoing)->Bit0.a_fEflMember  |= 1; \
        (a_pOutgoing)->Bit1.a_fEflMember  |= 1; \
    } while (0)
#else
# define IEM_LIVENESS_RAW_EFLAGS_ONE_INPUT(a_pOutgoing, a_fEflMember) do { \
        (a_pOutgoing)->aBits[IEMLIVENESS_BIT_READ].a_fEflMember |= 1; \
    } while (0)
#endif
/** @} */

/** @def IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY
 * Debug assertion that the required flags are available and not incorrectly skipped.
 */
#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
# define IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(a_pReNative, a_fEflNeeded) \
    AssertMsg(!((a_pReNative)->fSkippingEFlags & (a_fEflNeeded)), \
              ("%#x & %#x -> %#x\n", (a_pReNative)->fSkippingEFlags, \
               a_fEflNeeded, (a_pReNative)->fSkippingEFlags & (a_fEflNeeded) ))
#else
# define IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(a_pReNative, a_fEflNeeded) ((void)0)
#endif

/** @def IEMNATIVE_ASSERT_EFLAGS_POSTPONING_ONLY
 * Debug assertion that the required flags are available and not incorrectly postponed.
 */
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
# define IEMNATIVE_ASSERT_EFLAGS_POSTPONING_ONLY(a_pReNative, a_fEflNeeded) \
    AssertMsg(!((a_pReNative)->PostponedEfl.fEFlags & (a_fEflNeeded)), \
              ("%#x & %#x -> %#x\n", (a_pReNative)->PostponedEfl.fEFlags, \
               a_fEflNeeded, (a_pReNative)->PostponedEfl.fEFlags & (a_fEflNeeded) ))
#else
# define IEMNATIVE_ASSERT_EFLAGS_POSTPONING_ONLY(a_pReNative, a_fEflNeeded) ((void)0)
#endif

/** @def IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING
 * Debug assertion that the required flags are available and not incorrectly
 * skipped or postponed.
 */
#if defined(IEMNATIVE_WITH_EFLAGS_SKIPPING) && defined(IEMNATIVE_WITH_EFLAGS_POSTPONING)
# define IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(a_pReNative, a_fEflNeeded) \
    AssertMsg(!(((a_pReNative)->fSkippingEFlags | (a_pReNative)->PostponedEfl.fEFlags) & (a_fEflNeeded)), \
              ("(%#x | %#x) & %#x -> %#x\n", (a_pReNative)->fSkippingEFlags, (a_pReNative)->PostponedEfl.fEFlags, \
               a_fEflNeeded, ((a_pReNative)->fSkippingEFlags | (a_pReNative)->PostponedEfl.fEFlags) & (a_fEflNeeded) ))
#elif defined(IEMNATIVE_WITH_EFLAGS_SKIPPING)
# define IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(a_pReNative, a_fEflNeeded) \
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(a_pReNative, a_fEflNeeded)
#elif defined(IEMNATIVE_WITH_EFLAGS_POSTPONING) \
# define IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(a_pReNative, a_fEflNeeded) \
    IEMNATIVE_ASSERT_EFLAGS_POSTPONING_ONLY(a_pReNative, a_fEflNeeded)
#else
# define IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(a_pReNative, a_fEflNeeded) ((void)0)
#endif

/** @def IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK
 * Checks that the EFLAGS bits specified by @a a_fEflNeeded are actually
 * calculated and up to date.  This is to double check that we haven't skipped
 * EFLAGS calculations when we actually need them.  NOP in non-strict builds.
 * @note has to be placed in
 */
#ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
# define IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(a_pReNative, a_off, a_fEflNeeded) do { \
        (a_off) = iemNativeEmitEFlagsSkippingCheck(a_pReNative, a_off, a_fEflNeeded); \
    } while (0)
#else
# define IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(a_pReNative, a_off, a_fEflNeeded) do { } while (0)
#endif


/** @def IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS
 * Number of extra instructions to allocate for each TB exit to account for
 * postponed EFLAGS calculations.
 */
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
# ifdef RT_ARCH_AMD64
#  ifdef VBOX_STRICT
#   define IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS   64
#  else
#   define IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS   32
#  endif
# elif defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING)
#  ifdef VBOX_STRICT
#   define IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS   48
#  else
#   define IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS   32
#  endif
# else
#  error "port me"
# endif
#else
# define IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS    0
#endif

/** @def IEMNATIVE_CLEAR_POSTPONED_EFLAGS
 * Helper macro function for calling iemNativeClearPostponedEFlags() when
 * IEMNATIVE_WITH_EFLAGS_POSTPONING is enabled.
 */
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
# define IEMNATIVE_CLEAR_POSTPONED_EFLAGS(a_pReNative, a_fEflClobbered) iemNativeClearPostponedEFlags<a_fEflClobbered>(a_pReNative)
#else
# define IEMNATIVE_CLEAR_POSTPONED_EFLAGS(a_pReNative, a_fEflClobbered) ((void)0)
#endif

/** @def IEMNATIVE_HAS_POSTPONED_EFLAGS_CALCS
 * Macro for testing whether there are currently any postponed EFLAGS calcs w/o
 * needing to \#ifdef the check.
 */
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
# define IEMNATIVE_HAS_POSTPONED_EFLAGS_CALCS(a_pReNative) ((a_pReNative)->PostponedEfl.fEFlags != 0)
#else
# define IEMNATIVE_HAS_POSTPONED_EFLAGS_CALCS(a_pReNative) false
#endif


/**
 * Translation block debug info entry type.
 */
typedef enum IEMTBDBGENTRYTYPE
{
    kIemTbDbgEntryType_Invalid = 0,
    /** The entry is for marking a native code position.
     * Entries following this all apply to this position. */
    kIemTbDbgEntryType_NativeOffset,
    /** The entry is for a new guest instruction. */
    kIemTbDbgEntryType_GuestInstruction,
    /** Marks the start of a threaded call. */
    kIemTbDbgEntryType_ThreadedCall,
    /** Marks the location of a label. */
    kIemTbDbgEntryType_Label,
    /** Info about a host register shadowing a guest register. */
    kIemTbDbgEntryType_GuestRegShadowing,
    /** Info about a host SIMD register shadowing a guest SIMD register. */
    kIemTbDbgEntryType_GuestSimdRegShadowing,
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    /** Info about a delayed RIP update. */
    kIemTbDbgEntryType_DelayedPcUpdate,
#endif
    /** Info about a shadowed guest register becoming dirty. */
    kIemTbDbgEntryType_GuestRegDirty,
    /** Info about register writeback/flush oepration. */
    kIemTbDbgEntryType_GuestRegWriteback,
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /** Info about a delayed EFLAGS calculation. */
    kIemTbDbgEntryType_PostponedEFlagsCalc,
#endif
    kIemTbDbgEntryType_End
} IEMTBDBGENTRYTYPE;

/**
 * Translation block debug info entry.
 */
typedef union IEMTBDBGENTRY
{
    /** Plain 32-bit view. */
    uint32_t u;

    /** Generic view for getting at the type field. */
    struct
    {
        /** IEMTBDBGENTRYTYPE */
        uint32_t    uType : 4;
        uint32_t    uTypeSpecific : 28;
    } Gen;

    struct
    {
        /** kIemTbDbgEntryType_ThreadedCall1. */
        uint32_t    uType      : 4;
        /** Native code offset. */
        uint32_t    offNative  : 28;
    } NativeOffset;

    struct
    {
        /** kIemTbDbgEntryType_GuestInstruction. */
        uint32_t    uType      : 4;
        uint32_t    uUnused    : 4;
        /** The IEM_F_XXX flags. */
        uint32_t    fExec      : 24;
    } GuestInstruction;

    struct
    {
        /* kIemTbDbgEntryType_ThreadedCall. */
        uint32_t    uType       : 4;
        /** Set if the call was recompiled to native code, clear if just calling
         *  threaded function. */
        uint32_t    fRecompiled : 1;
        uint32_t    uUnused     : 11;
        /** The threaded call number (IEMTHREADEDFUNCS). */
        uint32_t    enmCall     : 16;
    } ThreadedCall;

    struct
    {
        /* kIemTbDbgEntryType_Label. */
        uint32_t    uType      : 4;
        uint32_t    uUnused    : 4;
        /** The label type (IEMNATIVELABELTYPE).   */
        uint32_t    enmLabel   : 8;
        /** The label data. */
        uint32_t    uData      : 16;
    } Label;

    struct
    {
        /* kIemTbDbgEntryType_GuestRegShadowing. */
        uint32_t    uType         : 4;
        uint32_t    uUnused       : 4;
        /** The guest register being shadowed (IEMNATIVEGSTREG). */
        uint32_t    idxGstReg     : 8;
        /** The host new register number, UINT8_MAX if dropped. */
        uint32_t    idxHstReg     : 8;
        /** The previous host register number, UINT8_MAX if new.   */
        uint32_t    idxHstRegPrev : 8;
    } GuestRegShadowing;

    struct
    {
        /* kIemTbDbgEntryType_GuestSimdRegShadowing. */
        uint32_t    uType             : 4;
        uint32_t    uUnused           : 4;
        /** The guest register being shadowed (IEMNATIVEGSTSIMDREG). */
        uint32_t    idxGstSimdReg     : 8;
        /** The host new register number, UINT8_MAX if dropped. */
        uint32_t    idxHstSimdReg     : 8;
        /** The previous host register number, UINT8_MAX if new.   */
        uint32_t    idxHstSimdRegPrev : 8;
    } GuestSimdRegShadowing;

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    struct
    {
        /* kIemTbDbgEntryType_DelayedPcUpdate. */
        uint32_t    uType         : 4;
        /** Number of instructions skipped. */
        uint32_t    cInstrSkipped : 8;
        /* The instruction offset added to the program counter. */
        int32_t     offPc         : 20;
    } DelayedPcUpdate;
#endif

    struct
    {
        /* kIemTbDbgEntryType_GuestRegDirty. */
        uint32_t    uType         : 4;
        uint32_t    uUnused       : 11;
        /** Flag whether this is about a SIMD (true) or general (false) register. */
        uint32_t    fSimdReg      : 1;
        /** The guest register index being marked as dirty. */
        uint32_t    idxGstReg     : 8;
        /** The host register number this register is shadowed in .*/
        uint32_t    idxHstReg     : 8;
    } GuestRegDirty;

    struct
    {
        /* kIemTbDbgEntryType_GuestRegWriteback. */
        uint32_t    uType         : 4;
        /** Flag whether this is about a SIMD (true) or general (false) register flush. */
        uint32_t    fSimdReg      : 1;
        /** The mask shift. */
        uint32_t    cShift        : 2;
        /** The guest register mask being written back. */
        uint32_t    fGstReg       : 25;
    } GuestRegWriteback;

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    struct
    {
        /* kIemTbDbgEntryType_PostponedEFlagsCalc. */
        uint32_t    uType         : 4;
        /** The EFLAGS operation (IEMNATIVE_POSTPONED_EFL_OP_T). */
        uint32_t    enmOp         : 4;
        /** The mask shift. */
        uint32_t    cOpBits       : 8;
        /** The emit instance number (0-based). */
        uint32_t    idxEmit       : 8;
        /** Unused. */
        uint32_t    uUnused       : 8;
    } PostponedEflCalc;
#endif
} IEMTBDBGENTRY;
AssertCompileSize(IEMTBDBGENTRY, sizeof(uint32_t));
/** Pointer to a debug info entry. */
typedef IEMTBDBGENTRY *PIEMTBDBGENTRY;
/** Pointer to a const debug info entry. */
typedef IEMTBDBGENTRY const *PCIEMTBDBGENTRY;

/**
 * Translation block debug info.
 */
typedef struct IEMTBDBG
{
    /** This is the flat PC corresponding to IEMTB::GCPhysPc. */
    RTGCPTR         FlatPc;
    /** Number of entries in aEntries. */
    uint32_t        cEntries;
    /** The offset of the last kIemTbDbgEntryType_NativeOffset record. */
    uint32_t        offNativeLast;
    /** Debug info entries. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    IEMTBDBGENTRY   aEntries[RT_FLEXIBLE_ARRAY];
} IEMTBDBG;
/** Pointer to TB debug info. */
typedef IEMTBDBG *PIEMTBDBG;
/** Pointer to const TB debug info. */
typedef IEMTBDBG const *PCIEMTBDBG;

/**
 * Guest registers that can be shadowed in GPRs.
 *
 * This runs parallel to the liveness state (IEMLIVENESSBIT, ++). The EFlags
 * must be placed last, as the liveness state tracks it as 7 subcomponents and
 * we don't want to waste space here.
 *
 * @note Make sure to update IEMLIVENESSBIT, IEMLIVENESSBIT_ALL_EFL_MASK and
 *       friends as well as IEMAllN8veLiveness.cpp.
 */
typedef enum IEMNATIVEGSTREG : uint8_t
{
    kIemNativeGstReg_GprFirst      = 0,
    kIemNativeGstReg_Rax           = kIemNativeGstReg_GprFirst + 0,
    kIemNativeGstReg_Rcx           = kIemNativeGstReg_GprFirst + 1,
    kIemNativeGstReg_Rdx           = kIemNativeGstReg_GprFirst + 2,
    kIemNativeGstReg_Rbx           = kIemNativeGstReg_GprFirst + 3,
    kIemNativeGstReg_Rsp           = kIemNativeGstReg_GprFirst + 4,
    kIemNativeGstReg_Rbp           = kIemNativeGstReg_GprFirst + 5,
    kIemNativeGstReg_Rsi           = kIemNativeGstReg_GprFirst + 6,
    kIemNativeGstReg_Rdi           = kIemNativeGstReg_GprFirst + 7,
    kIemNativeGstReg_GprLast       = kIemNativeGstReg_GprFirst + 15,
    kIemNativeGstReg_Cr0,
    kIemNativeGstReg_Cr4,
    kIemNativeGstReg_FpuFcw,
    kIemNativeGstReg_FpuFsw,
    kIemNativeGstReg_SegBaseFirst,
    kIemNativeGstReg_CsBase        = kIemNativeGstReg_SegBaseFirst + X86_SREG_CS,
    kIemNativeGstReg_SegBaseLast   = kIemNativeGstReg_SegBaseFirst + 5,
    kIemNativeGstReg_SegAttribFirst,
    kIemNativeGstReg_SegAttribLast = kIemNativeGstReg_SegAttribFirst + 5,
    kIemNativeGstReg_SegLimitFirst,
    kIemNativeGstReg_SegLimitLast  = kIemNativeGstReg_SegLimitFirst + 5,
    kIemNativeGstReg_SegSelFirst,
    kIemNativeGstReg_SegSelLast    = kIemNativeGstReg_SegSelFirst + 5,
    kIemNativeGstReg_Xcr0,
    kIemNativeGstReg_MxCsr,
    kIemNativeGstReg_EFlags,            /**< 32-bit, includes internal flags. */
    /* 6 entry gap for liveness EFlags subdivisions. */
    kIemNativeGstReg_Pc            = kIemNativeGstReg_EFlags + 7,
    kIemNativeGstReg_End
} IEMNATIVEGSTREG;
AssertCompile((int)kIemNativeGstReg_SegLimitFirst == 32);
AssertCompile((UINT64_C(0x7f) << kIemNativeGstReg_EFlags) == IEMLIVENESSBIT_ALL_EFL_MASK);
AssertCompile(RT_BIT_64(kIemNativeGstReg_Pc) - UINT64_C(1) == IEMLIVENESSBIT_MASK);

/** @name Helpers for converting register numbers to IEMNATIVEGSTREG values.
 * @{  */
#define IEMNATIVEGSTREG_GPR(a_iGpr)             ((IEMNATIVEGSTREG)(kIemNativeGstReg_GprFirst       + (a_iGpr)    ))
#define IEMNATIVEGSTREG_SEG_SEL(a_iSegReg)      ((IEMNATIVEGSTREG)(kIemNativeGstReg_SegSelFirst    + (a_iSegReg) ))
#define IEMNATIVEGSTREG_SEG_BASE(a_iSegReg)     ((IEMNATIVEGSTREG)(kIemNativeGstReg_SegBaseFirst   + (a_iSegReg) ))
#define IEMNATIVEGSTREG_SEG_LIMIT(a_iSegReg)    ((IEMNATIVEGSTREG)(kIemNativeGstReg_SegLimitFirst  + (a_iSegReg) ))
#define IEMNATIVEGSTREG_SEG_ATTRIB(a_iSegReg)   ((IEMNATIVEGSTREG)(kIemNativeGstReg_SegAttribFirst + (a_iSegReg) ))
/** @} */


/**
 * Guest registers that can be shadowed in host SIMD registers.
 *
 * @todo r=aeichner Liveness tracking
 * @todo r=aeichner Given that we can only track xmm/ymm here does this actually make sense?
 */
typedef enum IEMNATIVEGSTSIMDREG : uint8_t
{
    kIemNativeGstSimdReg_SimdRegFirst  = 0,
    kIemNativeGstSimdReg_SimdRegLast   = kIemNativeGstSimdReg_SimdRegFirst + 15,
    kIemNativeGstSimdReg_End
} IEMNATIVEGSTSIMDREG;

/** @name Helpers for converting register numbers to IEMNATIVEGSTSIMDREG values.
 * @{  */
#define IEMNATIVEGSTSIMDREG_SIMD(a_iSimdReg)   ((IEMNATIVEGSTSIMDREG)(kIemNativeGstSimdReg_SimdRegFirst + (a_iSimdReg)))
/** @} */

/**
 * The Load/store size for a SIMD guest register.
 */
typedef enum IEMNATIVEGSTSIMDREGLDSTSZ : uint8_t
{
    /** Invalid size. */
    kIemNativeGstSimdRegLdStSz_Invalid = 0,
    /** Loads the low 128-bit of a guest SIMD register. */
    kIemNativeGstSimdRegLdStSz_Low128,
    /** Loads the high 128-bit of a guest SIMD register. */
    kIemNativeGstSimdRegLdStSz_High128,
    /** Loads the whole 256-bits of a guest SIMD register. */
    kIemNativeGstSimdRegLdStSz_256,
    /** End value. */
    kIemNativeGstSimdRegLdStSz_End
} IEMNATIVEGSTSIMDREGLDSTSZ;


/**
 * Intended use statement for iemNativeRegAllocTmpForGuestReg().
 */
typedef enum IEMNATIVEGSTREGUSE
{
    /** The usage is read-only, the register holding the guest register
     * shadow copy will not be modified by the caller. */
    kIemNativeGstRegUse_ReadOnly = 0,
    /** The caller will update the guest register (think: PC += cbInstr).
     * The guest shadow copy will follow the returned register. */
    kIemNativeGstRegUse_ForUpdate,
    /** The call will put an entirely new value in the guest register, so
     * if new register is allocate it will be returned uninitialized. */
    kIemNativeGstRegUse_ForFullWrite,
    /** The caller will use the guest register value as input in a calculation
     * and the host register will be modified.
     * This means that the returned host register will not be marked as a shadow
     * copy of the guest register. */
    kIemNativeGstRegUse_Calculation
} IEMNATIVEGSTREGUSE;

/**
 * Guest registers (classes) that can be referenced.
 */
typedef enum IEMNATIVEGSTREGREF : uint8_t
{
    kIemNativeGstRegRef_Invalid = 0,
    kIemNativeGstRegRef_Gpr,
    kIemNativeGstRegRef_GprHighByte,    /**< AH, CH, DH, BH*/
    kIemNativeGstRegRef_EFlags,
    kIemNativeGstRegRef_MxCsr,
    kIemNativeGstRegRef_FpuReg,
    kIemNativeGstRegRef_MReg,
    kIemNativeGstRegRef_XReg,
    kIemNativeGstRegRef_X87,
    kIemNativeGstRegRef_XState,
    //kIemNativeGstRegRef_YReg, - doesn't work.
    kIemNativeGstRegRef_End
} IEMNATIVEGSTREGREF;


/** Variable kinds. */
typedef enum IEMNATIVEVARKIND : uint8_t
{
    /** Customary invalid zero value. */
    kIemNativeVarKind_Invalid = 0,
    /** This is either in a register or on the stack. */
    kIemNativeVarKind_Stack,
    /** Immediate value - loaded into register when needed, or can live on the
     *  stack if referenced (in theory). */
    kIemNativeVarKind_Immediate,
    /** Variable reference - loaded into register when needed, never stack. */
    kIemNativeVarKind_VarRef,
    /** Guest register reference - loaded into register when needed, never stack. */
    kIemNativeVarKind_GstRegRef,
    /** End of valid values. */
    kIemNativeVarKind_End
} IEMNATIVEVARKIND;


/** Variable or argument. */
typedef struct IEMNATIVEVAR
{
    RT_GCC_EXTENSION
    union
    {
        struct
        {
            /** The kind of variable. */
            IEMNATIVEVARKIND    enmKind;
            /** The variable size in bytes. */
            uint8_t             cbVar;
            /** Set if the registered is currently used exclusively, false if the
             *  variable is idle and the register can be grabbed. */
            bool                fRegAcquired;
            /** Flag whether this variable is held in a SIMD register (only supported for
             * 128-bit and 256-bit variables), only valid when idxReg is not UINT8_MAX. */
            bool                fSimdReg;
        };
        uint32_t        u32Init0;   /**< Init optimzation - cbVar is set, the other are initialized with zeros. */
    };

    RT_GCC_EXTENSION
    union
    {
        struct
        {
            /** The host register allocated for the variable, UINT8_MAX if not. */
            uint8_t             idxReg;
            /** The argument number if argument, UINT8_MAX if regular variable. */
            uint8_t             uArgNo;
            /** The first stack slot (uint64_t), except for immediate and references
             *  where it usually is UINT8_MAX. This is allocated lazily, so if a variable
             *  has a stack slot it has been initialized and has a value.  Unused variables
             *  has neither a stack slot nor a host register assignment. */
            uint8_t             idxStackSlot;
            /** If referenced, the index (unpacked) of the variable referencing this one,
             * otherwise UINT8_MAX.  A referenced variable must only be placed on the stack
             * and must be either kIemNativeVarKind_Stack or kIemNativeVarKind_Immediate. */
            uint8_t             idxReferrerVar;
        };
        uint32_t        u32Init1;   /**< Init optimization; all these are initialized to 0xff. */
    };

    union
    {
        /** kIemNativeVarKind_Immediate: The immediate value. */
        uint64_t            uValue;
        /** kIemNativeVarKind_VarRef: The index (unpacked) of the variable being referenced. */
        uint8_t             idxRefVar;
        /** kIemNativeVarKind_GstRegRef: The guest register being referrenced. */
        struct
        {
            /** The class of register. */
            IEMNATIVEGSTREGREF  enmClass;
            /** Index within the class. */
            uint8_t             idx;
        } GstRegRef;
    } u;
} IEMNATIVEVAR;
AssertCompileSize(IEMNATIVEVAR, 16);
/** Pointer to a variable or argument. */
typedef IEMNATIVEVAR *PIEMNATIVEVAR;
/** Pointer to a const variable or argument. */
typedef IEMNATIVEVAR const *PCIEMNATIVEVAR;

/** What is being kept in a host register. */
typedef enum IEMNATIVEWHAT : uint8_t
{
    /** The traditional invalid zero value. */
    kIemNativeWhat_Invalid = 0,
    /** Mapping a variable (IEMNATIVEHSTREG::idxVar). */
    kIemNativeWhat_Var,
    /** Temporary register, this is typically freed when a MC completes. */
    kIemNativeWhat_Tmp,
    /** Call argument w/o a variable mapping.  This is free (via
     * IEMNATIVE_CALL_VOLATILE_GREG_MASK) after the call is emitted. */
    kIemNativeWhat_Arg,
    /** Return status code.
     * @todo not sure if we need this... */
    kIemNativeWhat_rc,
    /** The fixed pVCpu (PVMCPUCC) register.
     * @todo consider offsetting this on amd64 to use negative offsets to access
     *       more members using 8-byte disp. */
    kIemNativeWhat_pVCpuFixed,
    /** The fixed pCtx (PCPUMCTX) register.
     * @todo consider offsetting this on amd64 to use negative offsets to access
     *       more members using 8-byte disp. */
    kIemNativeWhat_pCtxFixed,
    /** Fixed temporary register. */
    kIemNativeWhat_FixedTmp,
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    /** Shadow RIP for the delayed RIP updating debugging. */
    kIemNativeWhat_PcShadow,
#endif
    /** Register reserved by the CPU or OS architecture. */
    kIemNativeWhat_FixedReserved,
    /** End of valid values. */
    kIemNativeWhat_End
} IEMNATIVEWHAT;

/**
 * Host general register entry.
 *
 * The actual allocation status is kept in IEMRECOMPILERSTATE::bmHstRegs.
 *
 * @todo Track immediate values in host registers similarlly to how we track the
 *       guest register shadow copies. For it to be real helpful, though,
 *       we probably need to know which will be reused and put them into
 *       non-volatile registers, otherwise it's going to be more or less
 *       restricted to an instruction or two.
 */
typedef struct IEMNATIVEHSTREG
{
    /** Set of guest registers this one shadows.
     *
     * Using a bitmap here so we can designate the same host register as a copy
     * for more than one guest register.  This is expected to be useful in
     * situations where one value is copied to several registers in a sequence.
     * If the mapping is 1:1, then we'd have to pick which side of a 'MOV SRC,DST'
     * sequence we'd want to let this register follow to be a copy of and there
     * will always be places where we'd be picking the wrong one.
     */
    uint64_t        fGstRegShadows;
    /** What is being kept in this register. */
    IEMNATIVEWHAT   enmWhat;
    /** Variable index (packed) if holding a variable, otherwise UINT8_MAX. */
    uint8_t         idxVar;
    /** Stack slot assigned by iemNativeVarSaveVolatileRegsPreHlpCall and freed
     * by iemNativeVarRestoreVolatileRegsPostHlpCall.  This is not valid outside
     * that scope. */
    uint8_t         idxStackSlot;
    /** Alignment padding. */
    uint8_t         abAlign[5];
} IEMNATIVEHSTREG;


/**
 * Host SIMD register entry - this tracks a virtual 256-bit register split into two 128-bit
 * halves, on architectures where there is no 256-bit register available this entry will track
 * two adjacent 128-bit host registers.
 *
 * The actual allocation status is kept in IEMRECOMPILERSTATE::bmHstSimdRegs.
 */
typedef struct IEMNATIVEHSTSIMDREG
{
    /** Set of guest registers this one shadows.
     *
     * Using a bitmap here so we can designate the same host register as a copy
     * for more than one guest register.  This is expected to be useful in
     * situations where one value is copied to several registers in a sequence.
     * If the mapping is 1:1, then we'd have to pick which side of a 'MOV SRC,DST'
     * sequence we'd want to let this register follow to be a copy of and there
     * will always be places where we'd be picking the wrong one.
     */
    uint64_t                  fGstRegShadows;
    /** What is being kept in this register. */
    IEMNATIVEWHAT             enmWhat;
    /** Variable index (packed) if holding a variable, otherwise UINT8_MAX. */
    uint8_t                   idxVar;
    /** Flag what is currently loaded, low 128-bits, high 128-bits or complete 256-bits. */
    IEMNATIVEGSTSIMDREGLDSTSZ enmLoaded;
    /** Alignment padding. */
    uint8_t                   abAlign[5];
} IEMNATIVEHSTSIMDREG;


/**
 * Core state for the native recompiler, that is, things that needs careful
 * handling when dealing with branches.
 */
typedef struct IEMNATIVECORESTATE
{
    /** Allocation bitmap for aHstRegs. */
    uint32_t                    bmHstRegs;

    /** Bitmap marking which host register contains guest register shadow copies.
     * This is used during register allocation to try preserve copies.  */
    uint32_t                    bmHstRegsWithGstShadow;
    /** Bitmap marking valid entries in aidxGstRegShadows. */
    uint64_t                    bmGstRegShadows;
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    /** Bitmap marking the shadowed guest register as dirty and needing writeback when flushing. */
    uint64_t                    bmGstRegShadowDirty;
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    /** The current instruction offset in bytes from when the guest program counter
     * was updated last. Used for delaying the write to the guest context program counter
     * as long as possible. */
    int64_t                     offPc;
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    /** Set after we've loaded PC into uPcUpdatingDebug at the first update. */
    bool                        fDebugPcInitialized;
# endif
#endif

    /** Allocation bitmap for aHstSimdRegs. */
    uint32_t                    bmHstSimdRegs;

    /** Bitmap marking which host SIMD register contains guest SIMD register shadow copies.
     * This is used during register allocation to try preserve copies.  */
    uint32_t                    bmHstSimdRegsWithGstShadow;
    /** Bitmap marking valid entries in aidxSimdGstRegShadows. */
    uint64_t                    bmGstSimdRegShadows;
    /** Bitmap marking whether the low 128-bit of the shadowed guest register are dirty and need writeback. */
    uint64_t                    bmGstSimdRegShadowDirtyLo128;
    /** Bitmap marking whether the high 128-bit of the shadowed guest register are dirty and need writeback. */
    uint64_t                    bmGstSimdRegShadowDirtyHi128;

    union
    {
        /** Index of variable (unpacked) arguments, UINT8_MAX if not valid. */
        uint8_t                 aidxArgVars[8];
        /** For more efficient resetting. */
        uint64_t                u64ArgVars;
    };

    /** Allocation bitmap for the stack. */
    uint32_t                    bmStack;
    /** Allocation bitmap for aVars. */
    uint32_t                    bmVars;

    /** Maps a guest register to a host GPR (index by IEMNATIVEGSTREG).
     * Entries are only valid if the corresponding bit in bmGstRegShadows is set.
     * (A shadow copy of a guest register can only be held in a one host register,
     * there are no duplicate copies or ambiguities like that). */
    uint8_t                     aidxGstRegShadows[kIemNativeGstReg_End];
    /** Maps a guest SIMD register to a host SIMD register (index by IEMNATIVEGSTSIMDREG).
     * Entries are only valid if the corresponding bit in bmGstSimdRegShadows is set.
     * (A shadow copy of a guest register can only be held in a one host register,
     * there are no duplicate copies or ambiguities like that). */
    uint8_t                     aidxGstSimdRegShadows[kIemNativeGstSimdReg_End];

    /** Host register allocation tracking. */
    IEMNATIVEHSTREG             aHstRegs[IEMNATIVE_HST_GREG_COUNT];
    /** Host SIMD register allocation tracking. */
    IEMNATIVEHSTSIMDREG         aHstSimdRegs[IEMNATIVE_HST_SIMD_REG_COUNT];

    /** Variables and arguments. */
    IEMNATIVEVAR                aVars[9];
} IEMNATIVECORESTATE;
/** Pointer to core state. */
typedef IEMNATIVECORESTATE *PIEMNATIVECORESTATE;
/** Pointer to const core state. */
typedef IEMNATIVECORESTATE const *PCIEMNATIVECORESTATE;

/** @def IEMNATIVE_VAR_IDX_UNPACK
 * @returns Index into IEMNATIVECORESTATE::aVars.
 * @param   a_idxVar    Variable index w/ magic (in strict builds).
 */
/** @def IEMNATIVE_VAR_IDX_PACK
 * @returns Variable index w/ magic (in strict builds).
 * @param   a_idxVar    Index into IEMNATIVECORESTATE::aVars.
 */
#ifdef VBOX_STRICT
# define IEMNATIVE_VAR_IDX_UNPACK(a_idxVar) ((a_idxVar) & IEMNATIVE_VAR_IDX_MASK)
# define IEMNATIVE_VAR_IDX_PACK(a_idxVar)   ((a_idxVar) | IEMNATIVE_VAR_IDX_MAGIC)
# define IEMNATIVE_VAR_IDX_MAGIC            UINT8_C(0xd0)
# define IEMNATIVE_VAR_IDX_MAGIC_MASK       UINT8_C(0xf0)
# define IEMNATIVE_VAR_IDX_MASK             UINT8_C(0x0f)
#else
# define IEMNATIVE_VAR_IDX_UNPACK(a_idxVar) (a_idxVar)
# define IEMNATIVE_VAR_IDX_PACK(a_idxVar)   (a_idxVar)
#endif


/** Clear the dirty state of the given guest SIMD register. */
#define IEMNATIVE_SIMD_REG_STATE_CLR_DIRTY(a_pReNative, a_iSimdReg) \
    do { \
        (a_pReNative)->Core.bmGstSimdRegShadowDirtyLo128 &= ~RT_BIT_64(a_iSimdReg); \
        (a_pReNative)->Core.bmGstSimdRegShadowDirtyHi128 &= ~RT_BIT_64(a_iSimdReg); \
    } while (0)

/** Returns whether the low 128-bits of the given guest SIMD register are dirty. */
#define IEMNATIVE_SIMD_REG_STATE_IS_DIRTY_LO_U128(a_pReNative, a_iSimdReg) \
    RT_BOOL((a_pReNative)->Core.bmGstSimdRegShadowDirtyLo128 & RT_BIT_64(a_iSimdReg))
/** Returns whether the high 128-bits of the given guest SIMD register are dirty. */
#define IEMNATIVE_SIMD_REG_STATE_IS_DIRTY_HI_U128(a_pReNative, a_iSimdReg) \
    RT_BOOL((a_pReNative)->Core.bmGstSimdRegShadowDirtyHi128 & RT_BIT_64(a_iSimdReg))
/** Returns whether the given guest SIMD register is dirty. */
#define IEMNATIVE_SIMD_REG_STATE_IS_DIRTY_U256(a_pReNative, a_iSimdReg) \
    RT_BOOL(((a_pReNative)->Core.bmGstSimdRegShadowDirtyLo128 | (a_pReNative)->Core.bmGstSimdRegShadowDirtyHi128) & RT_BIT_64(a_iSimdReg))

/** Set the low 128-bits of the given guest SIMD register to the dirty state. */
#define IEMNATIVE_SIMD_REG_STATE_SET_DIRTY_LO_U128(a_pReNative, a_iSimdReg) \
    ((a_pReNative)->Core.bmGstSimdRegShadowDirtyLo128 |= RT_BIT_64(a_iSimdReg))
/** Set the high 128-bits of the given guest SIMD register to the dirty state. */
#define IEMNATIVE_SIMD_REG_STATE_SET_DIRTY_HI_U128(a_pReNative, a_iSimdReg) \
    ((a_pReNative)->Core.bmGstSimdRegShadowDirtyHi128 |= RT_BIT_64(a_iSimdReg))

/** Flag for indicating that IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() has emitted code in the current TB. */
#define IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_DEVICE_NOT_AVAILABLE         RT_BIT_32(0)
    /** Flag for indicating that IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() has emitted code in the current TB. */
#define IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_WAIT_DEVICE_NOT_AVAILABLE    RT_BIT_32(1)
/** Flag for indicating that IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() has emitted code in the current TB. */
#define IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_SSE                          RT_BIT_32(2)
/** Flag for indicating that IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() has emitted code in the current TB. */
#define IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX                          RT_BIT_32(3)
# ifdef IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
/** Flag indicating that the guest MXCSR was synced to the host floating point control register. */
# define IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SYNCED                                     RT_BIT_32(4)
/** Flag indicating whether the host floating point control register was saved before overwriting it. */
# define IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SAVED                                      RT_BIT_32(5)
#endif


#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
typedef enum IEMNATIVE_POSTPONED_EFL_OP_T : uint8_t
{
    kIemNativePostponedEflOp_Invalid = 0,
    /** Logical operation.
     * Operands: result register.
     * @note This clears OF, CF and (undefined) AF, thus no need for inputs. */
    kIemNativePostponedEflOp_Logical,
    kIemNativePostponedEflOp_End
} IEMNATIVE_POSTPONED_EFL_OP_T;
#endif /* IEMNATIVE_WITH_EFLAGS_POSTPONING */

/**
 * Conditional stack entry.
 */
typedef struct IEMNATIVECOND
{
    /** Set if we're in the "else" part, clear if we're in the "if" before it. */
    bool                        fInElse;
    union
    {
        RT_GCC_EXTENSION struct
        {
            /** Set if the if-block unconditionally exited the TB. */
            bool                fIfExitTb;
            /** Set if the else-block unconditionally exited the TB. */
            bool                fElseExitTb;
        };
        /** Indexed by fInElse. */
        bool                    afExitTb[2];
    };
    bool                        afPadding[5];
    /** The label for the IEM_MC_ELSE. */
    uint32_t                    idxLabelElse;
    /** The label for the IEM_MC_ENDIF. */
    uint32_t                    idxLabelEndIf;
    /** The initial state snapshot as the if-block starts executing. */
    IEMNATIVECORESTATE          InitialState;
    /** The state snapshot at the end of the if-block. */
    IEMNATIVECORESTATE          IfFinalState;
} IEMNATIVECOND;
/** Pointer to a condition stack entry. */
typedef IEMNATIVECOND *PIEMNATIVECOND;


/**
 * Native recompiler state.
 */
typedef struct IEMRECOMPILERSTATE
{
    /** Size of the buffer that pbNativeRecompileBufR3 points to in
     * IEMNATIVEINSTR units. */
    uint32_t                    cInstrBufAlloc;
#ifdef VBOX_STRICT
    /** Strict: How far the last iemNativeInstrBufEnsure() checked. */
    uint32_t                    offInstrBufChecked;
#else
    uint32_t                    uPadding1; /* We don't keep track of the size here... */
#endif
    /** Fixed temporary code buffer for native recompilation. */
    PIEMNATIVEINSTR             pInstrBuf;

    /** Bitmaps with the label types used. */
    uint64_t                    bmLabelTypes;
    /** Actual number of labels in paLabels. */
    uint32_t                    cLabels;
    /** Max number of entries allowed in paLabels before reallocating it. */
    uint32_t                    cLabelsAlloc;
    /** Labels defined while recompiling (referenced by fixups). */
    PIEMNATIVELABEL             paLabels;
    /** Array with indexes of unique labels (uData always 0). */
    uint32_t                    aidxUniqueLabels[kIemNativeLabelType_FirstWithMultipleInstances];

    /** Actual number of fixups paFixups. */
    uint32_t                    cFixups;
    /** Max number of entries allowed in paFixups before reallocating it. */
    uint32_t                    cFixupsAlloc;
    /** Buffer used by the recompiler for recording fixups when generating code. */
    PIEMNATIVEFIXUP             paFixups;

    /** Actual number of fixups in paTbExitFixups. */
    uint32_t                    cTbExitFixups;
    /** Max number of entries allowed in paTbExitFixups before reallocating it. */
    uint32_t                    cTbExitFixupsAlloc;
    /** Buffer used by the recompiler for recording fixups when generating code. */
    PIEMNATIVEEXITFIXUP         paTbExitFixups;

#if defined(IEMNATIVE_WITH_TB_DEBUG_INFO) || defined(VBOX_WITH_STATISTICS)
    /** Statistics: The idxInstr+1 value at the last PC update. */
    uint8_t                     idxInstrPlusOneOfLastPcUpdate;
#endif

#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    /** Number of debug info entries allocated for pDbgInfo. */
    uint32_t                    cDbgInfoAlloc;
    /** Debug info. */
    PIEMTBDBG                   pDbgInfo;
#endif

#ifdef IEMNATIVE_WITH_LIVENESS_ANALYSIS
    /** The current call index (liveness array and threaded calls in TB). */
    uint32_t                    idxCurCall;
    /** Number of liveness entries allocated. */
    uint32_t                    cLivenessEntriesAlloc;
    /** Liveness entries for all the calls in the TB begin recompiled.
     * The entry for idxCurCall contains the info for what the next call will
     * require wrt registers.  (Which means the last entry is the initial liveness
     * state.) */
    PIEMLIVENESSENTRY           paLivenessEntries;
#endif

    /** The translation block being recompiled. */
    PCIEMTB                     pTbOrg;
    /** The VMCPU structure of the EMT. */
    PVMCPUCC                    pVCpu;

    /** Condition sequence number (for generating unique labels). */
    uint16_t                    uCondSeqNo;
    /** Check IRQ sequence number (for generating unique labels). */
    uint16_t                    uCheckIrqSeqNo;
    /** TLB load sequence number (for generating unique labels). */
    uint16_t                    uTlbSeqNo;
    /** The current condition stack depth (aCondStack). */
    uint8_t                     cCondDepth;

    /** The argument count + hidden regs from the IEM_MC_BEGIN_EX statement. */
    uint8_t                     cArgsX;
    /** The IEM_CIMPL_F_XXX flags from the IEM_MC_BEGIN statement. */
    uint32_t                    fCImpl;
    /** The IEM_MC_F_XXX flags from the IEM_MC_BEGIN statement. */
    uint32_t                    fMc;
    /** The expected IEMCPU::fExec value for the current call/instruction. */
    uint32_t                    fExec;
    /** IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_XXX flags for exception flags
     * we only emit once per TB (or when the cr0/cr4/xcr0 register changes).
     *
     * This is an optimization because these control registers can only be changed from
     * by calling a C helper we can catch. Should reduce the number of instructions in a TB
     * consisting of multiple SIMD instructions.
     */
    uint32_t                    fSimdRaiseXcptChecksEmitted;
    /** The call number of the last CheckIrq, UINT32_MAX if not seen. */
    uint32_t                    idxLastCheckIrqCallNo;
#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    uint32_t                    fSkippingEFlags;
#endif
#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    struct
    {
        /** EFLAGS status bits that we're currently postponing the calculcation of. */
        uint32_t                        fEFlags;
        /** The postponed EFLAGS status bits calculation operation. */
        IEMNATIVE_POSTPONED_EFL_OP_T    enmOp;
        /** The bit-width of the postponed EFLAGS calculation. */
        uint8_t                         cOpBits;
        /** Host register holding result or first source for the delayed operation,
         *  UINT8_MAX if not in use. */
        uint8_t                         idxReg1;
        /** Host register holding second source for the delayed operation,
         *  UINT8_MAX if not in use. */
        uint8_t                         idxReg2;
# if defined(VBOX_WITH_STATISTICS) || defined(IEMNATIVE_WITH_TB_DEBUG_INFO)
        /** Number of times the delayed calculation was emitted. */
        uint8_t                         cEmits;
# endif
    } PostponedEfl;
#endif

    /** Core state requiring care with branches. */
    IEMNATIVECORESTATE          Core;

    /** The condition nesting stack. */
    IEMNATIVECOND               aCondStack[2];

#ifndef IEM_WITH_THROW_CATCH
    /** Pointer to the setjmp/longjmp buffer if we're not using C++ exceptions
     *  for recompilation error handling. */
    jmp_buf                     JmpBuf;
#endif
} IEMRECOMPILERSTATE;
/** Pointer to a native recompiler state. */
typedef IEMRECOMPILERSTATE *PIEMRECOMPILERSTATE;


/** @def IEMNATIVE_TRY_SETJMP
 * Wrapper around setjmp / try, hiding all the ugly differences.
 *
 * @note Use with extreme care as this is a fragile macro.
 * @param   a_pReNative The native recompile state.
 * @param   a_rcTarget  The variable that should receive the status code in case
 *                      of a longjmp/throw.
 */
/** @def IEMNATIVE_CATCH_LONGJMP_BEGIN
 * Start wrapper for catch / setjmp-else.
 *
 * This will set up a scope.
 *
 * @note Use with extreme care as this is a fragile macro.
 * @param   a_pReNative The native recompile state.
 * @param   a_rcTarget  The variable that should receive the status code in case
 *                      of a longjmp/throw.
 */
/** @def IEMNATIVE_CATCH_LONGJMP_END
 * End wrapper for catch / setjmp-else.
 *
 * This will close the scope set up by IEMNATIVE_CATCH_LONGJMP_BEGIN and clean
 * up the state.
 *
 * @note Use with extreme care as this is a fragile macro.
 * @param   a_pReNative The native recompile state.
 */
/** @def IEMNATIVE_DO_LONGJMP
 *
 * Wrapper around longjmp / throw.
 *
 * @param   a_pReNative The native recompile state.
 * @param   a_rc        The status code jump back with / throw.
 */
#ifdef IEM_WITH_THROW_CATCH
# define IEMNATIVE_TRY_SETJMP(a_pReNative, a_rcTarget) \
       a_rcTarget = VINF_SUCCESS; \
       try
# define IEMNATIVE_CATCH_LONGJMP_BEGIN(a_pReNative, a_rcTarget) \
       catch (int rcThrown) \
       { \
           a_rcTarget = rcThrown
# define IEMNATIVE_CATCH_LONGJMP_END(a_pReNative) \
       } \
       ((void)0)
# define IEMNATIVE_DO_LONGJMP(a_pReNative, a_rc)  throw int(a_rc)
#else  /* !IEM_WITH_THROW_CATCH */
# define IEMNATIVE_TRY_SETJMP(a_pReNative, a_rcTarget) \
       if ((a_rcTarget = setjmp((a_pReNative)->JmpBuf)) == 0)
# define IEMNATIVE_CATCH_LONGJMP_BEGIN(a_pReNative, a_rcTarget) \
       else \
       { \
           ((void)0)
# define IEMNATIVE_CATCH_LONGJMP_END(a_pReNative) \
       }
# define IEMNATIVE_DO_LONGJMP(a_pReNative, a_rc)  longjmp((a_pReNative)->JmpBuf, (a_rc))
#endif /* !IEM_WITH_THROW_CATCH */


/**
 * Native recompiler worker for a threaded function.
 *
 * @returns New code buffer offset; throws VBox status code in case of a failure.
 * @param   pReNative   The native recompiler state.
 * @param   off         The current code buffer offset.
 * @param   pCallEntry  The threaded call entry.
 *
 * @note    This may throw/longjmp VBox status codes (int) to abort compilation, so no RT_NOEXCEPT!
 */
typedef uint32_t (VBOXCALL FNIEMNATIVERECOMPFUNC)(PIEMRECOMPILERSTATE pReNative, uint32_t off, PCIEMTHRDEDCALLENTRY pCallEntry);
/** Pointer to a native recompiler worker for a threaded function. */
typedef FNIEMNATIVERECOMPFUNC *PFNIEMNATIVERECOMPFUNC;

/** Defines a native recompiler worker for a threaded function.
 * @see FNIEMNATIVERECOMPFUNC  */
#define IEM_DECL_IEMNATIVERECOMPFUNC_DEF(a_Name) \
    IEM_DECL_MSC_GUARD_IGNORE uint32_t VBOXCALL \
    a_Name(PIEMRECOMPILERSTATE pReNative, uint32_t off, PCIEMTHRDEDCALLENTRY pCallEntry)

/** Prototypes a native recompiler function for a threaded function.
 * @see FNIEMNATIVERECOMPFUNC  */
#define IEM_DECL_IEMNATIVERECOMPFUNC_PROTO(a_Name) FNIEMNATIVERECOMPFUNC a_Name


/**
 * Native recompiler liveness analysis worker for a threaded function.
 *
 * @param   pCallEntry  The threaded call entry.
 * @param   pIncoming   The incoming liveness state entry.
 * @param   pOutgoing   The outgoing liveness state entry.
 */
typedef DECLCALLBACKTYPE(void, FNIEMNATIVELIVENESSFUNC, (PCIEMTHRDEDCALLENTRY pCallEntry,
                                                         PCIEMLIVENESSENTRY pIncoming, PIEMLIVENESSENTRY pOutgoing));
/** Pointer to a native recompiler liveness analysis worker for a threaded function. */
typedef FNIEMNATIVELIVENESSFUNC *PFNIEMNATIVELIVENESSFUNC;

/** Defines a native recompiler liveness analysis worker for a threaded function.
 * @see FNIEMNATIVELIVENESSFUNC  */
#define IEM_DECL_IEMNATIVELIVENESSFUNC_DEF(a_Name) \
    IEM_DECL_MSC_GUARD_IGNORE DECLCALLBACK(void) \
    a_Name(PCIEMTHRDEDCALLENTRY pCallEntry, PCIEMLIVENESSENTRY pIncoming, PIEMLIVENESSENTRY pOutgoing)

/** Prototypes a native recompiler liveness analysis function for a threaded function.
 * @see FNIEMNATIVELIVENESSFUNC  */
#define IEM_DECL_IEMNATIVELIVENESSFUNC_PROTO(a_Name) FNIEMNATIVELIVENESSFUNC a_Name


/** Define a native recompiler helper function, safe to call from the TB code. */
#define IEM_DECL_NATIVE_HLP_DEF(a_RetType, a_Name, a_ArgList) \
    DECL_HIDDEN_THROW(a_RetType) VBOXCALL a_Name a_ArgList
/** Prototype a native recompiler helper function, safe to call from the TB code. */
#define IEM_DECL_NATIVE_HLP_PROTO(a_RetType, a_Name, a_ArgList) \
    DECL_HIDDEN_THROW(a_RetType) VBOXCALL a_Name a_ArgList
/** Pointer typedef a native recompiler helper function, safe to call from the TB code. */
#define IEM_DECL_NATIVE_HLP_PTR(a_RetType, a_Name, a_ArgList) \
    a_RetType (VBOXCALL *a_Name) a_ArgList


#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddNativeOffset(PIEMRECOMPILERSTATE pReNative, uint32_t off);
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddGuestRegShadowing(PIEMRECOMPILERSTATE pReNative, IEMNATIVEGSTREG enmGstReg,
                                                                 uint8_t idxHstReg = UINT8_MAX, uint8_t idxHstRegPrev = UINT8_MAX);
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddGuestSimdRegShadowing(PIEMRECOMPILERSTATE pReNative,
                                                                     IEMNATIVEGSTSIMDREG enmGstSimdReg,
                                                                     uint8_t idxHstSimdReg = UINT8_MAX,
                                                                     uint8_t idxHstSimdRegPrev = UINT8_MAX);
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddGuestRegDirty(PIEMRECOMPILERSTATE pReNative, bool fSimdReg,
                                                             uint8_t idxGstReg, uint8_t idxHstReg);
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddGuestRegWriteback(PIEMRECOMPILERSTATE pReNative, bool fSimdReg,
                                                                 uint64_t fGstReg);
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddDelayedPcUpdate(PIEMRECOMPILERSTATE pReNative,
                                                               uint64_t offPc, uint32_t cInstrSkipped);
# ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
DECL_HIDDEN_THROW(void)     iemNativeDbgInfoAddPostponedEFlagsCalc(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                   IEMNATIVE_POSTPONED_EFL_OP_T enmOp, uint8_t cOpBits,
                                                                   uint8_t idxInstance);
# endif
#endif /* IEMNATIVE_WITH_TB_DEBUG_INFO */

DECL_HIDDEN_THROW(uint32_t) iemNativeLabelCreate(PIEMRECOMPILERSTATE pReNative, IEMNATIVELABELTYPE enmType,
                                                 uint32_t offWhere = UINT32_MAX, uint16_t uData = 0);
DECL_HIDDEN_THROW(void)     iemNativeLabelDefine(PIEMRECOMPILERSTATE pReNative, uint32_t idxLabel, uint32_t offWhere);
DECLHIDDEN(uint32_t)        iemNativeLabelFind(PIEMRECOMPILERSTATE pReNative, IEMNATIVELABELTYPE enmType,
                                               uint32_t offWhere = UINT32_MAX, uint16_t uData = 0) RT_NOEXCEPT;
DECL_HIDDEN_THROW(void)     iemNativeAddFixup(PIEMRECOMPILERSTATE pReNative, uint32_t offWhere, uint32_t idxLabel,
                                              IEMNATIVEFIXUPTYPE enmType, int8_t offAddend = 0);
DECL_HIDDEN_THROW(void)     iemNativeAddTbExitFixup(PIEMRECOMPILERSTATE pReNative, uint32_t offWhere,
                                                    IEMNATIVELABELTYPE enmExitReason);
DECL_HIDDEN_THROW(PIEMNATIVEINSTR) iemNativeInstrBufEnsureSlow(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t cInstrReq);

DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmp(PIEMRECOMPILERSTATE pReNative, uint32_t *poff);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpPreferNonVolatile(PIEMRECOMPILERSTATE pReNative, uint32_t *poff);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpEx(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint32_t fRegMask);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpExPreferNonVolatile(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint32_t fRegMask);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpImm(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint64_t uImm);

DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegReadOnly(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegUpdate(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegFullWrite(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegCalculation(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegReadOnlyNoVolatile(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegUpdateNoVolatile(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegFullWriteNoVolatile(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegCalculationNoVolatile(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg);

#if defined(IEMNATIVE_WITH_LIVENESS_ANALYSIS) && defined(VBOX_STRICT)
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestEFlagsReadOnly(PIEMRECOMPILERSTATE pReNative, uint32_t *poff,
                                                                       uint64_t fRead, uint64_t fWrite = 0, uint64_t fPotentialCall = 0);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestEFlagsForUpdate(PIEMRECOMPILERSTATE pReNative, uint32_t *poff,
                                                                        uint64_t fRead, uint64_t fWrite = 0, uint64_t fPotentialCall = 0);
#endif

DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestRegIfAlreadyPresent(PIEMRECOMPILERSTATE pReNative, uint32_t *poff,
                                                                            IEMNATIVEGSTREG enmGstReg);
#if defined(IEMNATIVE_WITH_LIVENESS_ANALYSIS) && defined(VBOX_STRICT)
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAllocTmpForGuestEFlagsIfAlreadyPresent(PIEMRECOMPILERSTATE pReNative, uint32_t *poff,
                                                                               uint64_t fRead, uint64_t fWrite = 0);
#else
DECL_FORCE_INLINE_THROW(uint8_t)
iemNativeRegAllocTmpForGuestEFlagsIfAlreadyPresent(PIEMRECOMPILERSTATE pReNative, uint32_t *poff,
                                                   uint64_t fRead, uint64_t fWrite = 0)
{
    RT_NOREF(fRead, fWrite);
    return iemNativeRegAllocTmpForGuestRegIfAlreadyPresent(pReNative, poff, kIemNativeGstReg_EFlags);
}
#endif

DECL_HIDDEN_THROW(uint32_t) iemNativeRegAllocArgs(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cArgs);
DECL_HIDDEN_THROW(uint8_t)  iemNativeRegAssignRc(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg);
#if (defined(IPRT_INCLUDED_x86_h) && defined(RT_ARCH_AMD64)) || (defined(IPRT_INCLUDED_armv8_h) && defined(RT_ARCH_ARM64))
DECL_HIDDEN_THROW(uint32_t) iemNativeRegMoveOrSpillStackVar(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar,
                                                            uint32_t fForbiddenRegs = IEMNATIVE_CALL_VOLATILE_GREG_MASK);
DECL_HIDDEN_THROW(uint32_t) iemNativeSimdRegMoveOrSpillStackVar(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar,
                                                                uint32_t fForbiddenRegs = IEMNATIVE_CALL_VOLATILE_SIMD_REG_MASK);
#endif
DECLHIDDEN(void)            iemNativeRegFree(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg) RT_NOEXCEPT;
DECLHIDDEN(void)            iemNativeRegFreeTmp(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg) RT_NOEXCEPT;
DECLHIDDEN(void)            iemNativeRegFreeTmpImm(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg) RT_NOEXCEPT;
DECLHIDDEN(void)            iemNativeRegFreeVar(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg, bool fFlushShadows) RT_NOEXCEPT;
DECLHIDDEN(void)            iemNativeSimdRegFreeVar(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstSimdReg, bool fFlushShadows) RT_NOEXCEPT;
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
DECL_HIDDEN_THROW(uint32_t) iemNativeSimdRegFlushDirtyGuestByHostSimdRegShadow(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxHstReg);
#endif
DECLHIDDEN(void)            iemNativeRegFreeAndFlushMask(PIEMRECOMPILERSTATE pReNative, uint32_t fHstRegMask) RT_NOEXCEPT;
DECL_HIDDEN_THROW(uint32_t) iemNativeRegMoveAndFreeAndFlushAtCall(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cArgs,
                                                                  uint32_t fKeepVars = 0);
DECLHIDDEN(void)            iemNativeRegFlushGuestShadows(PIEMRECOMPILERSTATE pReNative, uint64_t fGstRegs) RT_NOEXCEPT;
DECLHIDDEN(void)            iemNativeRegFlushGuestShadowsByHostMask(PIEMRECOMPILERSTATE pReNative, uint32_t fHstRegs) RT_NOEXCEPT;
DECL_HIDDEN_THROW(uint32_t) iemNativeRegRestoreGuestShadowsInVolatileRegs(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                          uint32_t fHstRegsActiveShadows);
#ifdef VBOX_STRICT
DECLHIDDEN(void)            iemNativeRegAssertSanity(PIEMRECOMPILERSTATE pReNative);
#endif
DECL_HIDDEN_THROW(uint32_t) iemNativeRegFlushPendingWritesSlow(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint64_t fGstShwExcept,
                                                               uint64_t fGstSimdShwExcept);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitPcDebugCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitPcDebugCheckWithReg(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxPcReg);
# endif
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitPcWritebackSlow(PIEMRECOMPILERSTATE pReNative, uint32_t off);
#endif
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
DECL_HIDDEN_THROW(uint32_t) iemNativeRegFlushPendingWrite(PIEMRECOMPILERSTATE pReNative, uint32_t off, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeRegFlushPendingWriteEx(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                            PIEMNATIVECORESTATE pCore, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeRegFlushDirtyGuest(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                        uint64_t fFlushGstReg = UINT64_MAX);
DECL_HIDDEN_THROW(uint32_t) iemNativeRegFlushDirtyGuestByHostRegShadow(PIEMRECOMPILERSTATE pReNative,
                                                                       uint32_t off, uint8_t idxHstReg);
#endif


DECL_HIDDEN_THROW(uint8_t)  iemNativeSimdRegAllocTmp(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, bool fPreferVolatile = true);
DECL_HIDDEN_THROW(uint8_t)  iemNativeSimdRegAllocTmpEx(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint32_t fRegMask,
                                                       bool fPreferVolatile = true);
DECL_HIDDEN_THROW(uint8_t)  iemNativeSimdRegAllocTmpForGuestSimdReg(PIEMRECOMPILERSTATE pReNative, uint32_t *poff,
                                                                    IEMNATIVEGSTSIMDREG enmGstSimdReg,
                                                                    IEMNATIVEGSTSIMDREGLDSTSZ enmLoadSz,
                                                                    IEMNATIVEGSTREGUSE enmIntendedUse = kIemNativeGstRegUse_ReadOnly,
                                                                    bool fNoVolatileRegs = false);
DECLHIDDEN(void)            iemNativeSimdRegFreeTmp(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstSimdReg) RT_NOEXCEPT;
DECLHIDDEN(void)            iemNativeSimdRegFlushGuestShadows(PIEMRECOMPILERSTATE pReNative, uint64_t fGstSimdRegs) RT_NOEXCEPT;
DECL_HIDDEN_THROW(uint32_t) iemNativeSimdRegFlushPendingWrite(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                              IEMNATIVEGSTSIMDREG enmGstSimdReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitLoadSimdRegWithGstShadowSimdReg(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                         uint8_t idxHstSimdReg, IEMNATIVEGSTSIMDREG enmGstSimdReg,
                                                                         IEMNATIVEGSTSIMDREGLDSTSZ enmLoadSz);

DECL_HIDDEN_THROW(uint8_t)  iemNativeArgAlloc(PIEMRECOMPILERSTATE pReNative, uint8_t iArgNo, uint8_t cbType);
DECL_HIDDEN_THROW(uint8_t)  iemNativeArgAllocConst(PIEMRECOMPILERSTATE pReNative, uint8_t iArgNo, uint8_t cbType, uint64_t uValue);
DECL_HIDDEN_THROW(uint8_t)  iemNativeArgAllocLocalRef(PIEMRECOMPILERSTATE pReNative, uint8_t iArgNo, uint8_t idxOtherVar);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarAlloc(PIEMRECOMPILERSTATE pReNative, uint8_t cbType);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarAllocConst(PIEMRECOMPILERSTATE pReNative, uint8_t cbType, uint64_t uValue);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarAllocAssign(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint8_t cbType, uint8_t idxVarOther);
DECL_HIDDEN_THROW(void)     iemNativeVarSetKindToStack(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar);
DECL_HIDDEN_THROW(void)     iemNativeVarSetKindToConst(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint64_t uValue);
DECL_HIDDEN_THROW(void)     iemNativeVarSetKindToGstRegRef(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar,
                                                           IEMNATIVEGSTREGREF enmRegClass, uint8_t idxReg);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarGetStackSlot(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarRegisterAcquireSlow(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarRegisterAcquireWithPrefSlow(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar,
                                                                    uint32_t *poff, uint8_t idxRegPref);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarRegisterAcquireInitedSlow(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarRegisterAcquireInitedWithPrefSlow(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar,
                                                                          uint32_t *poff, uint8_t idxRegPref);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarSimdRegisterAcquire(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff,
                                                            bool fInitialized = false, uint8_t idxRegPref = UINT8_MAX);
DECL_HIDDEN_THROW(uint8_t)  iemNativeVarRegisterAcquireForGuestReg(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar,
                                                                   IEMNATIVEGSTREG enmGstReg, uint32_t *poff);
DECL_HIDDEN_THROW(uint32_t) iemNativeVarSaveVolatileRegsPreHlpCall(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                   uint32_t fHstGprNotToSave);
DECL_HIDDEN_THROW(uint32_t) iemNativeVarRestoreVolatileRegsPostHlpCall(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                       uint32_t fHstGprNotToSave);
DECLHIDDEN(void)            iemNativeVarFreeOneWorker(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar);
DECLHIDDEN(void)            iemNativeVarFreeAllSlow(PIEMRECOMPILERSTATE pReNative, uint32_t bmVars);

DECL_HIDDEN_THROW(uint32_t) iemNativeEmitLoadGprWithGstShadowReg(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                 uint8_t idxHstReg, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitLoadGprWithGstShadowRegEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                                                   uint8_t idxHstReg, IEMNATIVEGSTREG enmGstReg);
#ifdef VBOX_STRICT
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitTop32BitsClearCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitGuestRegValueCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxReg,
                                                            IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitGuestRegValueCheckEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf,
                                                              uint32_t off, uint8_t idxReg, IEMNATIVEGSTREG enmGstReg);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitGuestSimdRegValueCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxSimdReg,
                                                                IEMNATIVEGSTSIMDREG enmGstSimdReg,
                                                                IEMNATIVEGSTSIMDREGLDSTSZ enmLoadSz);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitExecFlagsCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t fExec);
#endif
#ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitEFlagsSkippingCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t fEflNeeded);
#endif
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitCheckCallRetAndPassUp(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitCallCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cArgs, uint8_t cHiddenArgs, bool fFlushPendingWrites = true);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitCImplCall(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr,
                                                   uint64_t fGstShwFlush, uintptr_t pfnCImpl, uint8_t cbInstr, uint8_t cAddParams,
                                                   uint64_t uParam0, uint64_t uParam1, uint64_t uParam2);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitThreadedCall(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                      PCIEMTHRDEDCALLENTRY pCallEntry);
IEM_DECL_IEMNATIVELIVENESSFUNC_PROTO(iemNativeLivenessFunc_ThreadedCall);
DECL_HIDDEN_THROW(uint32_t) iemNativeEmitLeaGprByGstRegRef(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxGprDst,
                                                           IEMNATIVEGSTREGREF enmClass, uint8_t idxRegInClass);


IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecStatusCodeFiddling,(PVMCPUCC pVCpu, int rc, uint8_t idxInstr));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseGp0,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseNm,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseUd,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseMf,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseXf,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseDe,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpObsoleteTb,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpNeedCsLimChecking,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpCheckBranchMiss,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseAvxRelated,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseSseRelated,(PVMCPUCC pVCpu));
IEM_DECL_NATIVE_HLP_PROTO(int,      iemNativeHlpExecRaiseSseAvxFpRelated,(PVMCPUCC pVCpu));

IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU8,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU8_Sx_U16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU8_Sx_U32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU8_Sx_U64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU16_Sx_U32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU16_Sx_U64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU32_Sx_U64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFetchDataU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFetchDataU128,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PRTUINT128U pu128Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFetchDataU128AlignedSse,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PRTUINT128U pu128Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFetchDataU128NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PRTUINT128U pu128Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFetchDataU256NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PRTUINT256U pu256Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFetchDataU256AlignedAvx,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PRTUINT256U pu256Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU8,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, uint8_t u8Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, uint16_t u16Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, uint32_t u32Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, uint64_t u64Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU128AlignedSse,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PCRTUINT128U pu128Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU128NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PCRTUINT128U pu128Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU256NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PCRTUINT256U pu256Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemStoreDataU256AlignedAvx,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t iSegReg, PCRTUINT256U pu256Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackStoreU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint16_t u16Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackStoreU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackStoreU32SReg,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackStoreU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t u64Value));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t, iemNativeHlpStackFetchU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t, iemNativeHlpStackFetchU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpStackFetchU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));

IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU8,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU8_Sx_U16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU8_Sx_U32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU8_Sx_U64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU16_Sx_U32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU16_Sx_U64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU32_Sx_U64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpMemFlatFetchDataU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatFetchDataU128,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PRTUINT128U pu128Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatFetchDataU128AlignedSse,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PRTUINT128U pu128Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatFetchDataU128NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PRTUINT128U pu128Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatFetchDataU256NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PRTUINT256U pu256Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatFetchDataU256AlignedAvx,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PRTUINT256U pu256Dst));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU8,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t u8Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint16_t u16Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t u64Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU128AlignedSse,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT128U pu128Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU128NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT128U pu128Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU256NoAc,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT256U pu256Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpMemFlatStoreDataU256AlignedAvx,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, PCRTUINT256U pu256Src));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackFlatStoreU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint16_t u16Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackFlatStoreU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackFlatStoreU32SReg,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t u32Value));
IEM_DECL_NATIVE_HLP_PROTO(void,     iemNativeHlpStackFlatStoreU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint64_t u64Value));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t, iemNativeHlpStackFlatFetchU16,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t, iemNativeHlpStackFlatFetchU32,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t, iemNativeHlpStackFlatFetchU64,(PVMCPUCC pVCpu, RTGCPTR GCPtrMem));

IEM_DECL_NATIVE_HLP_PROTO(uint8_t *,        iemNativeHlpMemMapDataU8Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint8_t *,        iemNativeHlpMemMapDataU8Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint8_t *,        iemNativeHlpMemMapDataU8Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint8_t const *,  iemNativeHlpMemMapDataU8Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t *,       iemNativeHlpMemMapDataU16Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t *,       iemNativeHlpMemMapDataU16Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t *,       iemNativeHlpMemMapDataU16Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t const *, iemNativeHlpMemMapDataU16Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t *,       iemNativeHlpMemMapDataU32Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t *,       iemNativeHlpMemMapDataU32Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t *,       iemNativeHlpMemMapDataU32Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t const *, iemNativeHlpMemMapDataU32Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t *,       iemNativeHlpMemMapDataU64Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t *,       iemNativeHlpMemMapDataU64Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t *,       iemNativeHlpMemMapDataU64Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t const *, iemNativeHlpMemMapDataU64Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(RTFLOAT80U *,     iemNativeHlpMemMapDataR80Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(RTPBCD80U *,      iemNativeHlpMemMapDataD80Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U *,     iemNativeHlpMemMapDataU128Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U *,     iemNativeHlpMemMapDataU128Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U *,     iemNativeHlpMemMapDataU128Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U const *, iemNativeHlpMemMapDataU128Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem, uint8_t iSegReg));

IEM_DECL_NATIVE_HLP_PROTO(uint8_t *,        iemNativeHlpMemFlatMapDataU8Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint8_t *,        iemNativeHlpMemFlatMapDataU8Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint8_t *,        iemNativeHlpMemFlatMapDataU8Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint8_t const *,  iemNativeHlpMemFlatMapDataU8Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t *,       iemNativeHlpMemFlatMapDataU16Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t *,       iemNativeHlpMemFlatMapDataU16Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t *,       iemNativeHlpMemFlatMapDataU16Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint16_t const *, iemNativeHlpMemFlatMapDataU16Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t *,       iemNativeHlpMemFlatMapDataU32Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t *,       iemNativeHlpMemFlatMapDataU32Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t *,       iemNativeHlpMemFlatMapDataU32Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint32_t const *, iemNativeHlpMemFlatMapDataU32Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t *,       iemNativeHlpMemFlatMapDataU64Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t *,       iemNativeHlpMemFlatMapDataU64Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t *,       iemNativeHlpMemFlatMapDataU64Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(uint64_t const *, iemNativeHlpMemFlatMapDataU64Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(RTFLOAT80U *,     iemNativeHlpMemFlatMapDataR80Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(RTPBCD80U *,      iemNativeHlpMemFlatMapDataD80Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U *,     iemNativeHlpMemFlatMapDataU128Atomic,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U *,     iemNativeHlpMemFlatMapDataU128Rw,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U *,     iemNativeHlpMemFlatMapDataU128Wo,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));
IEM_DECL_NATIVE_HLP_PROTO(RTUINT128U const *, iemNativeHlpMemFlatMapDataU128Ro,(PVMCPUCC pVCpu, uint8_t *pbUnmapInfo, RTGCPTR GCPtrMem));

IEM_DECL_NATIVE_HLP_PROTO(void, iemNativeHlpMemCommitAndUnmapAtomic,(PVMCPUCC pVCpu, uint8_t bUnmapInfo));
IEM_DECL_NATIVE_HLP_PROTO(void, iemNativeHlpMemCommitAndUnmapRw,(PVMCPUCC pVCpu, uint8_t bUnmapInfo));
IEM_DECL_NATIVE_HLP_PROTO(void, iemNativeHlpMemCommitAndUnmapWo,(PVMCPUCC pVCpu, uint8_t bUnmapInfo));
IEM_DECL_NATIVE_HLP_PROTO(void, iemNativeHlpMemCommitAndUnmapRo,(PVMCPUCC pVCpu, uint8_t bUnmapInfo));


/**
 * Info about shadowed guest register values.
 * @see IEMNATIVEGSTREG
 */
typedef struct IEMANTIVEGSTREGINFO
{
    /** Offset in VMCPU. */
    uint32_t    off;
    /** The field size. */
    uint8_t     cb;
    /** Name (for logging). */
    const char *pszName;
} IEMANTIVEGSTREGINFO;
extern DECL_HIDDEN_DATA(IEMANTIVEGSTREGINFO const)  g_aGstShadowInfo[];
extern DECL_HIDDEN_DATA(const char * const)         g_apszIemNativeHstRegNames[];
extern DECL_HIDDEN_DATA(int32_t const)              g_aoffIemNativeCallStackArgBpDisp[];
extern DECL_HIDDEN_DATA(uint32_t const)             g_afIemNativeCallRegs[];
extern DECL_HIDDEN_DATA(uint8_t const)              g_aidxIemNativeCallRegs[];



/**
 * Ensures that there is sufficient space in the instruction output buffer.
 *
 * This will reallocate the buffer if needed and allowed.
 *
 * @note    Always use IEMNATIVE_ASSERT_INSTR_BUF_ENSURE when done to check the
 *          allocation size.
 *
 * @returns Pointer to the instruction output buffer on success; throws VBox
 *          status code on failure, so no need to check it.
 * @param   pReNative   The native recompile state.
 * @param   off         Current instruction offset.  Works safely for UINT32_MAX
 *                      as well.
 * @param   cInstrReq   Number of instruction about to be added.  It's okay to
 *                      overestimate this a bit.
 */
DECL_FORCE_INLINE_THROW(PIEMNATIVEINSTR)
iemNativeInstrBufEnsure(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t cInstrReq)
{
    uint64_t const offChecked = off + (uint64_t)cInstrReq; /** @todo may reconsider the need for UINT32_MAX safety... */
    if (RT_LIKELY(offChecked <= pReNative->cInstrBufAlloc))
    {
#ifdef VBOX_STRICT
        pReNative->offInstrBufChecked = offChecked;
#endif
        return pReNative->pInstrBuf;
    }
    return iemNativeInstrBufEnsureSlow(pReNative, off, cInstrReq);
}

/**
 * Checks that we didn't exceed the space requested in the last
 * iemNativeInstrBufEnsure() call.
 */
#define IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(a_pReNative, a_off) \
    AssertMsg((a_off) <= (a_pReNative)->offInstrBufChecked, \
              ("off=%#x offInstrBufChecked=%#x\n", (a_off), (a_pReNative)->offInstrBufChecked))

/**
 * Checks that a variable index is valid.
 */
#ifdef IEMNATIVE_VAR_IDX_MAGIC
# define IEMNATIVE_ASSERT_VAR_IDX(a_pReNative, a_idxVar) \
    AssertMsg(   ((a_idxVar) & IEMNATIVE_VAR_IDX_MAGIC_MASK) == IEMNATIVE_VAR_IDX_MAGIC \
              && (unsigned)IEMNATIVE_VAR_IDX_UNPACK(a_idxVar) < RT_ELEMENTS((a_pReNative)->Core.aVars) \
              && ((a_pReNative)->Core.bmVars & RT_BIT_32(IEMNATIVE_VAR_IDX_UNPACK(a_idxVar))), \
              ("%s=%#x\n", #a_idxVar, a_idxVar))
#else
# define IEMNATIVE_ASSERT_VAR_IDX(a_pReNative, a_idxVar) \
    AssertMsg(   (unsigned)(a_idxVar) < RT_ELEMENTS((a_pReNative)->Core.aVars) \
              && ((a_pReNative)->Core.bmVars & RT_BIT_32(a_idxVar)), ("%s=%d\n", #a_idxVar, a_idxVar))
#endif

/**
 * Checks that a variable index is valid and that the variable is assigned the
 * correct argument number.
 * This also adds a RT_NOREF of a_idxVar.
 */
#ifdef IEMNATIVE_VAR_IDX_MAGIC
# define IEMNATIVE_ASSERT_ARG_VAR_IDX(a_pReNative, a_idxVar, a_uArgNo) do { \
        RT_NOREF_PV(a_idxVar); \
        AssertMsg(   ((a_idxVar) & IEMNATIVE_VAR_IDX_MAGIC_MASK) == IEMNATIVE_VAR_IDX_MAGIC \
                  && (unsigned)IEMNATIVE_VAR_IDX_UNPACK(a_idxVar) < RT_ELEMENTS((a_pReNative)->Core.aVars) \
                  && ((a_pReNative)->Core.bmVars & RT_BIT_32(IEMNATIVE_VAR_IDX_UNPACK(a_idxVar))) \
                  && (a_pReNative)->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVar)].uArgNo == (a_uArgNo), \
                  ("%s=%d; uArgNo=%d, expected %u\n", #a_idxVar, a_idxVar, \
                   (a_pReNative)->Core.aVars[RT_MIN(IEMNATIVE_VAR_IDX_UNPACK(a_idxVar), \
                                                    RT_ELEMENTS((a_pReNative)->Core.aVars)) - 1].uArgNo, \
                   a_uArgNo)); \
    } while (0)
#else
# define IEMNATIVE_ASSERT_ARG_VAR_IDX(a_pReNative, a_idxVar, a_uArgNo) do { \
        RT_NOREF_PV(a_idxVar); \
        AssertMsg(   (unsigned)(a_idxVar) < RT_ELEMENTS((a_pReNative)->Core.aVars) \
                  && ((a_pReNative)->Core.bmVars & RT_BIT_32(a_idxVar))\
                  && (a_pReNative)->Core.aVars[a_idxVar].uArgNo == (a_uArgNo) \
                  , ("%s=%d; uArgNo=%d, expected %u\n", #a_idxVar, a_idxVar, \
                     (a_pReNative)->Core.aVars[RT_MIN(a_idxVar, RT_ELEMENTS((a_pReNative)->Core.aVars)) - 1].uArgNo, a_uArgNo)); \
    } while (0)
#endif


/**
 * Checks that a variable has the expected size.
 */
#define IEMNATIVE_ASSERT_VAR_SIZE(a_pReNative, a_idxVar, a_cbVar) \
    AssertMsg((a_pReNative)->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVar)].cbVar == (a_cbVar), \
              ("%s=%#x: cbVar=%#x, expected %#x!\n", #a_idxVar, a_idxVar, \
              (a_pReNative)->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(a_idxVar)].cbVar, (a_cbVar)))


/**
 * Calculates the stack address of a variable as a [r]BP displacement value.
 */
DECL_FORCE_INLINE(int32_t)
iemNativeStackCalcBpDisp(uint8_t idxStackSlot)
{
    Assert(idxStackSlot < IEMNATIVE_FRAME_VAR_SLOTS);
    return idxStackSlot * sizeof(uint64_t) + IEMNATIVE_FP_OFF_STACK_VARS;
}


/**
 * Releases the variable's register.
 *
 * The register must have been previously acquired calling
 * iemNativeVarRegisterAcquire(), iemNativeVarRegisterAcquireForGuestReg() or
 * iemNativeVarRegisterSetAndAcquire().
 */
DECL_INLINE_THROW(void) iemNativeVarRegisterRelease(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    Assert(pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)].fRegAcquired);
    pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)].fRegAcquired = false;
}


DECL_INLINE_THROW(void) iemNativeVarSimdRegisterRelease(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar)
{
    Assert(pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)].fSimdReg);
    iemNativeVarRegisterRelease(pReNative, idxVar);
}


/**
 * Makes sure variable @a idxVar has a register assigned to it and that it stays
 * fixed till we call iemNativeVarRegisterRelease.
 *
 * @returns The host register number.
 * @param   pReNative       The recompiler state.
 * @param   idxVar          The variable.
 * @param   poff            Pointer to the instruction buffer offset.
 *                          In case a register needs to be freed up or the value
 *                          loaded off the stack.
 * @note    Must not modify the host status flags!
 */
DECL_INLINE_THROW(uint8_t) iemNativeVarRegisterAcquire(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    Assert(pVar->cbVar <= 8);
    Assert(!pVar->fRegAcquired);
    uint8_t const idxReg = pVar->idxReg;
    if (idxReg < RT_ELEMENTS(pReNative->Core.aHstRegs))
    {
        Assert(   pVar->enmKind > kIemNativeVarKind_Invalid
               && pVar->enmKind < kIemNativeVarKind_End);
        pVar->fRegAcquired = true;
        return idxReg;
    }
    return iemNativeVarRegisterAcquireSlow(pReNative, idxVar, poff);
}


/**
 * Makes sure variable @a idxVar has a register assigned to it and that it stays
 * fixed till we call iemNativeVarRegisterRelease.
 *
 * @returns The host register number.
 * @param   pReNative       The recompiler state.
 * @param   idxVar          The variable.
 * @param   poff            Pointer to the instruction buffer offset.
 *                          In case a register needs to be freed up or the value
 *                          loaded off the stack.
 * @param   idxRegPref      Preferred register number.
 * @note    Must not modify the host status flags!
 */
DECL_INLINE_THROW(uint8_t)
iemNativeVarRegisterAcquireWithPref(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff, uint8_t idxRegPref)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    Assert(pVar->cbVar <= 8);
    Assert(!pVar->fRegAcquired);
    Assert(idxRegPref < RT_ELEMENTS(pReNative->Core.aHstRegs));
    uint8_t const idxReg = pVar->idxReg;
    if (idxReg < RT_ELEMENTS(pReNative->Core.aHstRegs))
    {
        Assert(   pVar->enmKind > kIemNativeVarKind_Invalid
               && pVar->enmKind < kIemNativeVarKind_End);
        pVar->fRegAcquired = true;
        return idxReg;
    }
    return iemNativeVarRegisterAcquireWithPrefSlow(pReNative, idxVar, poff, idxRegPref);
}


/**
 * Makes sure variable @a idxVar has a register assigned to it and that it stays
 * fixed till we call iemNativeVarRegisterRelease.
 *
 * The variable must be initialized or VERR_IEM_VAR_NOT_INITIALIZED will be
 * thrown.
 *
 * @returns The host register number.
 * @param   pReNative       The recompiler state.
 * @param   idxVar          The variable.
 * @param   poff            Pointer to the instruction buffer offset.
 *                          In case a register needs to be freed up or the value
 *                          loaded off the stack.
 * @note    Must not modify the host status flags!
 */
DECL_INLINE_THROW(uint8_t) iemNativeVarRegisterAcquireInited(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    Assert(pVar->cbVar <= 8);
    Assert(!pVar->fRegAcquired);
    uint8_t const idxReg = pVar->idxReg;
    if (idxReg < RT_ELEMENTS(pReNative->Core.aHstRegs))
    {
        Assert(   pVar->enmKind > kIemNativeVarKind_Invalid
               && pVar->enmKind < kIemNativeVarKind_End);
        pVar->fRegAcquired = true;
        return idxReg;
    }
    return iemNativeVarRegisterAcquireInitedSlow(pReNative, idxVar, poff);
}


/**
 * Makes sure variable @a idxVar has a register assigned to it and that it stays
 * fixed till we call iemNativeVarRegisterRelease.
 *
 * The variable must be initialized or VERR_IEM_VAR_NOT_INITIALIZED will be
 * thrown.
 *
 * @returns The host register number.
 * @param   pReNative       The recompiler state.
 * @param   idxVar          The variable.
 * @param   poff            Pointer to the instruction buffer offset.
 *                          In case a register needs to be freed up or the value
 *                          loaded off the stack.
 * @param   idxRegPref      Preferred register number.
 * @note    Must not modify the host status flags!
 */
DECL_INLINE_THROW(uint8_t)
iemNativeVarRegisterAcquireInitedWithPref(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint32_t *poff, uint8_t idxRegPref)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    Assert(pVar->cbVar <= 8);
    Assert(!pVar->fRegAcquired);
    Assert(idxRegPref < RT_ELEMENTS(pReNative->Core.aHstRegs));
    uint8_t const idxReg = pVar->idxReg;
    if (idxReg < RT_ELEMENTS(pReNative->Core.aHstRegs))
    {
        Assert(   pVar->enmKind > kIemNativeVarKind_Invalid
               && pVar->enmKind < kIemNativeVarKind_End);
        pVar->fRegAcquired = true;
        return idxReg;
    }
    return iemNativeVarRegisterAcquireInitedWithPrefSlow(pReNative, idxVar, poff, idxRegPref);
}


/**
 * Converts IEM_CIMPL_F_XXX flags into a guest register shadow copy flush mask.
 *
 * @returns The flush mask.
 * @param   fCImpl          The IEM_CIMPL_F_XXX flags.
 * @param   fGstShwFlush    The starting flush mask.
 */
DECL_FORCE_INLINE(uint64_t) iemNativeCImplFlagsToGuestShadowFlushMask(uint32_t fCImpl, uint64_t fGstShwFlush)
{
    if (fCImpl & IEM_CIMPL_F_BRANCH_FAR)
        fGstShwFlush |= RT_BIT_64(kIemNativeGstReg_SegSelFirst   + X86_SREG_CS)
                     |  RT_BIT_64(kIemNativeGstReg_SegBaseFirst  + X86_SREG_CS)
                     |  RT_BIT_64(kIemNativeGstReg_SegLimitFirst + X86_SREG_CS);
    if (fCImpl & IEM_CIMPL_F_BRANCH_STACK_FAR)
        fGstShwFlush |= RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xSP)
                     |  RT_BIT_64(kIemNativeGstReg_SegSelFirst   + X86_SREG_SS)
                     |  RT_BIT_64(kIemNativeGstReg_SegBaseFirst  + X86_SREG_SS)
                     |  RT_BIT_64(kIemNativeGstReg_SegLimitFirst + X86_SREG_SS);
    else if (fCImpl & IEM_CIMPL_F_BRANCH_STACK)
        fGstShwFlush |= RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xSP);
    if (fCImpl & (IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_STATUS_FLAGS | IEM_CIMPL_F_INHIBIT_SHADOW))
        fGstShwFlush |= RT_BIT_64(kIemNativeGstReg_EFlags);
    return fGstShwFlush;
}


/** Number of hidden arguments for CIMPL calls.
 * @note We're sufferning from the usual VBOXSTRICTRC fun on Windows. */
#if defined(VBOXSTRICTRC_STRICT_ENABLED) && defined(RT_OS_WINDOWS) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM64))
# define IEM_CIMPL_HIDDEN_ARGS 3
#else
# define IEM_CIMPL_HIDDEN_ARGS 2
#endif


/** Number of hidden arguments for SSE_AIMPL calls. */
#define IEM_SSE_AIMPL_HIDDEN_ARGS   1
/** Number of hidden arguments for AVX_AIMPL calls. */
#define IEM_AVX_AIMPL_HIDDEN_ARGS   1


#ifdef IEMNATIVE_WITH_LIVENESS_ANALYSIS

# ifndef IEMLIVENESS_EXTENDED_LAYOUT
/**
 * Helper for iemNativeLivenessGetStateByGstReg.
 *
 * @returns IEMLIVENESS_STATE_XXX
 * @param   fMergedStateExp2    This is the RT_BIT_32() of each sub-state
 *                              ORed together.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeLivenessMergeExpandedEFlagsState(uint32_t fMergedStateExp2)
{
    /* INPUT trumps anything else. */
    if (fMergedStateExp2 & RT_BIT_32(IEMLIVENESS_STATE_INPUT))
        return IEMLIVENESS_STATE_INPUT;

    /* CLOBBERED trumps XCPT_OR_CALL and UNUSED. */
    if (fMergedStateExp2 & RT_BIT_32(IEMLIVENESS_STATE_CLOBBERED))
    {
        /* If not all sub-fields are clobbered they must be considered INPUT. */
        if (fMergedStateExp2 & (RT_BIT_32(IEMLIVENESS_STATE_UNUSED) | RT_BIT_32(IEMLIVENESS_STATE_XCPT_OR_CALL)))
            return IEMLIVENESS_STATE_INPUT;
        return IEMLIVENESS_STATE_CLOBBERED;
    }

    /* XCPT_OR_CALL trumps UNUSED. */
    if (fMergedStateExp2 & RT_BIT_32(IEMLIVENESS_STATE_XCPT_OR_CALL))
        return IEMLIVENESS_STATE_XCPT_OR_CALL;

    return IEMLIVENESS_STATE_UNUSED;
}
# endif /* !IEMLIVENESS_EXTENDED_LAYOUT */


DECL_FORCE_INLINE(uint32_t)
iemNativeLivenessGetStateByGstRegEx(PCIEMLIVENESSENTRY pLivenessEntry, unsigned enmGstRegEx)
{
# ifndef IEMLIVENESS_EXTENDED_LAYOUT
    return ((pLivenessEntry->Bit0.bm64 >> enmGstRegEx) & 1)
         | (((pLivenessEntry->Bit1.bm64 >> enmGstRegEx) << 1) & 2);
# else
    return ( (pLivenessEntry->Bit0.bm64 >> enmGstRegEx)       & 1)
         | (((pLivenessEntry->Bit1.bm64 >> enmGstRegEx) << 1) & 2)
         | (((pLivenessEntry->Bit2.bm64 >> enmGstRegEx) << 2) & 4)
         | (((pLivenessEntry->Bit3.bm64 >> enmGstRegEx) << 3) & 8);
# endif
}


DECL_FORCE_INLINE(uint32_t)
iemNativeLivenessGetStateByGstReg(PCIEMLIVENESSENTRY pLivenessEntry, IEMNATIVEGSTREG enmGstReg)
{
    uint32_t uRet = iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, (unsigned)enmGstReg);
    if (enmGstReg == kIemNativeGstReg_EFlags)
    {
        /* Merge the eflags states to one. */
# ifndef IEMLIVENESS_EXTENDED_LAYOUT
        uRet  = RT_BIT_32(uRet);
        uRet |= RT_BIT_32(pLivenessEntry->Bit0.fEflCf | (pLivenessEntry->Bit1.fEflCf << 1));
        uRet |= RT_BIT_32(pLivenessEntry->Bit0.fEflPf | (pLivenessEntry->Bit1.fEflPf << 1));
        uRet |= RT_BIT_32(pLivenessEntry->Bit0.fEflAf | (pLivenessEntry->Bit1.fEflAf << 1));
        uRet |= RT_BIT_32(pLivenessEntry->Bit0.fEflZf | (pLivenessEntry->Bit1.fEflZf << 1));
        uRet |= RT_BIT_32(pLivenessEntry->Bit0.fEflSf | (pLivenessEntry->Bit1.fEflSf << 1));
        uRet |= RT_BIT_32(pLivenessEntry->Bit0.fEflOf | (pLivenessEntry->Bit1.fEflOf << 1));
        uRet  = iemNativeLivenessMergeExpandedEFlagsState(uRet);
# else
        AssertCompile(IEMLIVENESSBIT_IDX_EFL_OTHER == (unsigned)kIemNativeGstReg_EFlags);
        uRet |= iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, IEMLIVENESSBIT_IDX_EFL_CF);
        uRet |= iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, IEMLIVENESSBIT_IDX_EFL_PF);
        uRet |= iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, IEMLIVENESSBIT_IDX_EFL_AF);
        uRet |= iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, IEMLIVENESSBIT_IDX_EFL_ZF);
        uRet |= iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, IEMLIVENESSBIT_IDX_EFL_SF);
        uRet |= iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, IEMLIVENESSBIT_IDX_EFL_OF);
# endif
    }
    return uRet;
}

# ifdef VBOX_STRICT

/** For assertions only - caller checks that idxCurCall isn't zero. */
DECL_FORCE_INLINE(uint32_t)
iemNativeLivenessGetPrevStateByGstReg(PIEMRECOMPILERSTATE pReNative, IEMNATIVEGSTREG enmGstReg)
{
    return iemNativeLivenessGetStateByGstReg(&pReNative->paLivenessEntries[pReNative->idxCurCall - 1], enmGstReg);
}


/** For assertions only - caller checks that idxCurCall isn't zero. */
DECL_FORCE_INLINE(uint32_t)
iemNativeLivenessGetPrevStateByGstRegEx(PIEMRECOMPILERSTATE pReNative, IEMNATIVEGSTREG enmGstReg)
{
    return iemNativeLivenessGetStateByGstRegEx(&pReNative->paLivenessEntries[pReNative->idxCurCall - 1], enmGstReg);
}

# endif /* VBOX_STRICT */
#endif /* IEMNATIVE_WITH_LIVENESS_ANALYSIS */


/**
 * Gets the number of hidden arguments for an expected IEM_MC_CALL statement.
 */
DECL_FORCE_INLINE(uint8_t) iemNativeArgGetHiddenArgCount(PIEMRECOMPILERSTATE pReNative)
{
    if (pReNative->fCImpl & IEM_CIMPL_F_CALLS_CIMPL)
        return IEM_CIMPL_HIDDEN_ARGS;
    if (pReNative->fCImpl & (IEM_CIMPL_F_CALLS_AIMPL_WITH_FXSTATE | IEM_CIMPL_F_CALLS_AIMPL_WITH_XSTATE))
        return 1;
    return 0;
}


DECL_FORCE_INLINE(uint8_t) iemNativeRegMarkAllocated(PIEMRECOMPILERSTATE pReNative, unsigned idxReg,
                                                     IEMNATIVEWHAT enmWhat, uint8_t idxVar = UINT8_MAX) RT_NOEXCEPT
{
    pReNative->Core.bmHstRegs |= RT_BIT_32(idxReg);

    pReNative->Core.aHstRegs[idxReg].enmWhat        = enmWhat;
    pReNative->Core.aHstRegs[idxReg].fGstRegShadows = 0;
    pReNative->Core.aHstRegs[idxReg].idxVar         = idxVar;
    return (uint8_t)idxReg;
}



/*********************************************************************************************************************************
*   Register Allocator (GPR)                                                                                                     *
*********************************************************************************************************************************/

#ifdef RT_ARCH_ARM64
# include <iprt/armv8.h>
#endif


/**
 * Marks host register @a idxHstReg as containing a shadow copy of guest
 * register @a enmGstReg.
 *
 * ASSUMES that caller has made sure @a enmGstReg is not associated with any
 * host register before calling.
 */
DECL_FORCE_INLINE(void)
iemNativeRegMarkAsGstRegShadow(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg, IEMNATIVEGSTREG enmGstReg, uint32_t off)
{
    Assert(!(pReNative->Core.bmGstRegShadows & RT_BIT_64(enmGstReg)));
    Assert(!pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows);
    Assert((unsigned)enmGstReg < (unsigned)kIemNativeGstReg_End);

    pReNative->Core.aidxGstRegShadows[enmGstReg]       = idxHstReg;
    pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows = RT_BIT_64(enmGstReg); /** @todo why? not OR? */
    pReNative->Core.bmGstRegShadows                   |= RT_BIT_64(enmGstReg);
    pReNative->Core.bmHstRegsWithGstShadow            |= RT_BIT_32(idxHstReg);
#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    iemNativeDbgInfoAddNativeOffset(pReNative, off);
    iemNativeDbgInfoAddGuestRegShadowing(pReNative, enmGstReg, idxHstReg);
#else
    RT_NOREF(off);
#endif
}


/**
 * Clear any guest register shadow claims from @a idxHstReg.
 *
 * The register does not need to be shadowing any guest registers.
 */
DECL_FORCE_INLINE(void)
iemNativeRegClearGstRegShadowing(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg, uint32_t off)
{
    Assert(      (pReNative->Core.bmGstRegShadows & pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows)
              == pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows
           && pReNative->Core.bmGstRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    Assert(   RT_BOOL(pReNative->Core.bmHstRegsWithGstShadow & RT_BIT_32(idxHstReg))
           == RT_BOOL(pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows));
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    Assert(!(pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows & pReNative->Core.bmGstRegShadowDirty));
#endif

#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    uint64_t fGstRegs = pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows;
    if (fGstRegs)
    {
        Assert(fGstRegs < RT_BIT_64(kIemNativeGstReg_End));
        iemNativeDbgInfoAddNativeOffset(pReNative, off);
        while (fGstRegs)
        {
            unsigned const iGstReg = ASMBitFirstSetU64(fGstRegs) - 1;
            fGstRegs &= ~RT_BIT_64(iGstReg);
            iemNativeDbgInfoAddGuestRegShadowing(pReNative, (IEMNATIVEGSTREG)iGstReg, UINT8_MAX, idxHstReg);
        }
    }
#else
    RT_NOREF(off);
#endif

    pReNative->Core.bmHstRegsWithGstShadow            &= ~RT_BIT_32(idxHstReg);
    pReNative->Core.bmGstRegShadows                   &= ~pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows;
    pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows = 0;
}


/**
 * Clear guest register shadow claim regarding @a enmGstReg from @a idxHstReg
 * and global overview flags.
 */
DECL_FORCE_INLINE(void)
iemNativeRegClearGstRegShadowingOne(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstReg, IEMNATIVEGSTREG enmGstReg, uint32_t off)
{
    Assert(pReNative->Core.bmGstRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    Assert(      (pReNative->Core.bmGstRegShadows & pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows)
              == pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows
           && pReNative->Core.bmGstRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    Assert(pReNative->Core.bmGstRegShadows                    & RT_BIT_64(enmGstReg));
    Assert(pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows & RT_BIT_64(enmGstReg));
    Assert(pReNative->Core.bmHstRegsWithGstShadow             & RT_BIT_32(idxHstReg));
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    Assert(!(pReNative->Core.bmGstRegShadowDirty              & RT_BIT_64(enmGstReg)));
#endif

#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    iemNativeDbgInfoAddNativeOffset(pReNative, off);
    iemNativeDbgInfoAddGuestRegShadowing(pReNative, enmGstReg, UINT8_MAX, idxHstReg);
#else
    RT_NOREF(off);
#endif

    uint64_t const fGstRegShadowsNew = pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows & ~RT_BIT_64(enmGstReg);
    pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows = fGstRegShadowsNew;
    if (!fGstRegShadowsNew)
        pReNative->Core.bmHstRegsWithGstShadow        &= ~RT_BIT_32(idxHstReg);
    pReNative->Core.bmGstRegShadows                   &= ~RT_BIT_64(enmGstReg);
}


#if 0 /* unused */
/**
 * Clear any guest register shadow claim for @a enmGstReg.
 */
DECL_FORCE_INLINE(void)
iemNativeRegClearGstRegShadowingByGstReg(PIEMRECOMPILERSTATE pReNative, IEMNATIVEGSTREG enmGstReg, uint32_t off)
{
    Assert(pReNative->Core.bmGstRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    if (pReNative->Core.bmGstRegShadows & RT_BIT_64(enmGstReg))
    {
        Assert(pReNative->Core.aidxGstRegShadows[enmGstReg] < RT_ELEMENTS(pReNative->Core.aHstRegs));
        iemNativeRegClearGstRegShadowingOne(pReNative, pReNative->Core.aidxGstRegShadows[enmGstReg], enmGstReg, off);
    }
}
#endif


/**
 * Clear any guest register shadow claim for @a enmGstReg and mark @a idxHstRegNew
 * as the new shadow of it.
 *
 * Unlike the other guest reg shadow helpers, this does the logging for you.
 * However, it is the liveness state is not asserted here, the caller must do
 * that.
 */
DECL_FORCE_INLINE(void)
iemNativeRegClearAndMarkAsGstRegShadow(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstRegNew,
                                       IEMNATIVEGSTREG enmGstReg, uint32_t off)
{
    Assert(pReNative->Core.bmGstRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    if (pReNative->Core.bmGstRegShadows & RT_BIT_64(enmGstReg))
    {
        uint8_t const idxHstRegOld = pReNative->Core.aidxGstRegShadows[enmGstReg];
        Assert(idxHstRegOld < RT_ELEMENTS(pReNative->Core.aHstRegs));
        if (idxHstRegOld == idxHstRegNew)
            return;
        Log12(("iemNativeRegClearAndMarkAsGstRegShadow: %s for guest %s (from %s)\n", g_apszIemNativeHstRegNames[idxHstRegNew],
               g_aGstShadowInfo[enmGstReg].pszName, g_apszIemNativeHstRegNames[idxHstRegOld]));
        iemNativeRegClearGstRegShadowingOne(pReNative, pReNative->Core.aidxGstRegShadows[enmGstReg], enmGstReg, off);
    }
    else
        Log12(("iemNativeRegClearAndMarkAsGstRegShadow: %s for guest %s\n", g_apszIemNativeHstRegNames[idxHstRegNew],
               g_aGstShadowInfo[enmGstReg].pszName));
    iemNativeRegMarkAsGstRegShadow(pReNative, idxHstRegNew, enmGstReg, off);
}


/**
 * Transfers the guest register shadow claims of @a enmGstReg from @a idxRegFrom
 * to @a idxRegTo.
 */
DECL_FORCE_INLINE(void)
iemNativeRegTransferGstRegShadowing(PIEMRECOMPILERSTATE pReNative, uint8_t idxRegFrom, uint8_t idxRegTo,
                                    IEMNATIVEGSTREG enmGstReg, uint32_t off)
{
    Assert(pReNative->Core.aHstRegs[idxRegFrom].fGstRegShadows & RT_BIT_64(enmGstReg));
    Assert(pReNative->Core.aidxGstRegShadows[enmGstReg] == idxRegFrom);
    Assert(      (pReNative->Core.bmGstRegShadows & pReNative->Core.aHstRegs[idxRegFrom].fGstRegShadows)
              == pReNative->Core.aHstRegs[idxRegFrom].fGstRegShadows
           && pReNative->Core.bmGstRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    Assert(   (pReNative->Core.bmGstRegShadows & pReNative->Core.aHstRegs[idxRegTo].fGstRegShadows)
           == pReNative->Core.aHstRegs[idxRegTo].fGstRegShadows);
    Assert(   RT_BOOL(pReNative->Core.bmHstRegsWithGstShadow & RT_BIT_32(idxRegFrom))
           == RT_BOOL(pReNative->Core.aHstRegs[idxRegFrom].fGstRegShadows));

    uint64_t const fGstRegShadowsFrom = pReNative->Core.aHstRegs[idxRegFrom].fGstRegShadows & ~RT_BIT_64(enmGstReg);
    pReNative->Core.aHstRegs[idxRegFrom].fGstRegShadows  = fGstRegShadowsFrom;
    if (!fGstRegShadowsFrom)
        pReNative->Core.bmHstRegsWithGstShadow          &= ~RT_BIT_32(idxRegFrom);
    pReNative->Core.bmHstRegsWithGstShadow              |= RT_BIT_32(idxRegTo);
    pReNative->Core.aHstRegs[idxRegTo].fGstRegShadows   |= RT_BIT_64(enmGstReg);
    pReNative->Core.aidxGstRegShadows[enmGstReg]         = idxRegTo;
#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    iemNativeDbgInfoAddNativeOffset(pReNative, off);
    iemNativeDbgInfoAddGuestRegShadowing(pReNative, enmGstReg, idxRegTo, idxRegFrom);
#else
    RT_NOREF(off);
#endif
}


/**
 * Flushes any delayed guest register writes.
 *
 * This must be called prior to calling CImpl functions and any helpers that use
 * the guest state (like raising exceptions) and such.
 *
 * This optimization has not yet been implemented.  The first target would be
 * RIP updates, since these are the most common ones.
 *
 * @note This function does not flush any shadowing information for guest
 *       registers. This needs to be done by the caller if it wishes to do so.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeRegFlushPendingWrites(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint64_t fGstShwExcept = 0,
                               uint64_t fGstSimdShwExcept = 0)
{
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    uint64_t const fWritebackPc            = ~fGstShwExcept & RT_BIT_64(kIemNativeGstReg_Pc);
#else
    uint64_t const fWritebackPc            = 0;
#endif
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    uint64_t const bmGstRegShadowDirty     = pReNative->Core.bmGstRegShadowDirty & ~fGstShwExcept;
#else
    uint64_t const bmGstRegShadowDirty     = 0;
#endif
    uint64_t const bmGstSimdRegShadowDirty = (  pReNative->Core.bmGstSimdRegShadowDirtyLo128
                                              | pReNative->Core.bmGstSimdRegShadowDirtyHi128)
                                           & ~fGstSimdShwExcept;
    if (bmGstRegShadowDirty | bmGstSimdRegShadowDirty | fWritebackPc)
        return iemNativeRegFlushPendingWritesSlow(pReNative, off, fGstShwExcept, fGstSimdShwExcept);

    return off;
}


/**
 * Allocates a temporary host general purpose register for keeping a guest
 * register value.
 *
 * Since we may already have a register holding the guest register value,
 * code will be emitted to do the loading if that's not the case. Code may also
 * be emitted if we have to free up a register to satify the request.
 *
 * @returns The host register number; throws VBox status code on failure, so no
 *          need to check the return value.
 * @param   pReNative       The native recompile state.
 * @param   poff            Pointer to the variable with the code buffer
 *                          position. This will be update if we need to move a
 *                          variable from register to stack in order to satisfy
 *                          the request.
 * @param   enmGstReg       The guest register that will is to be updated.
 * @param   enmIntendedUse  How the caller will be using the host register.
 * @param   fNoVolatileRegs Set if no volatile register allowed, clear if any
 *                          register is okay (default).  The ASSUMPTION here is
 *                          that the caller has already flushed all volatile
 *                          registers, so this is only applied if we allocate a
 *                          new register.
 * @sa      iemNativeRegAllocTmpForGuestEFlags
 *          iemNativeRegAllocTmpForGuestRegIfAlreadyPresent
 *          iemNativeRegAllocTmpForGuestRegInt
 */
DECL_FORCE_INLINE_THROW(uint8_t)
iemNativeRegAllocTmpForGuestReg(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, IEMNATIVEGSTREG enmGstReg,
                                IEMNATIVEGSTREGUSE const enmIntendedUse = kIemNativeGstRegUse_ReadOnly,
                                bool const fNoVolatileRegs = false)
{
    if (enmIntendedUse == kIemNativeGstRegUse_ReadOnly)
        return !fNoVolatileRegs
             ? iemNativeRegAllocTmpForGuestRegReadOnly(pReNative, poff, enmGstReg)
             : iemNativeRegAllocTmpForGuestRegReadOnlyNoVolatile(pReNative, poff, enmGstReg);
    if (enmIntendedUse == kIemNativeGstRegUse_ForUpdate)
        return !fNoVolatileRegs
             ? iemNativeRegAllocTmpForGuestRegUpdate(pReNative, poff, enmGstReg)
             : iemNativeRegAllocTmpForGuestRegUpdateNoVolatile(pReNative, poff, enmGstReg);
    if (enmIntendedUse == kIemNativeGstRegUse_ForFullWrite)
        return !fNoVolatileRegs
             ? iemNativeRegAllocTmpForGuestRegFullWrite(pReNative, poff, enmGstReg)
             : iemNativeRegAllocTmpForGuestRegFullWriteNoVolatile(pReNative, poff, enmGstReg);
    Assert(enmIntendedUse == kIemNativeGstRegUse_Calculation);
    return !fNoVolatileRegs
         ? iemNativeRegAllocTmpForGuestRegCalculation(pReNative, poff, enmGstReg)
         : iemNativeRegAllocTmpForGuestRegCalculationNoVolatile(pReNative, poff, enmGstReg);
}

#if !defined(IEMNATIVE_WITH_LIVENESS_ANALYSIS) || !defined(VBOX_STRICT)

DECL_FORCE_INLINE_THROW(uint8_t)
iemNativeRegAllocTmpForGuestEFlagsReadOnly(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint64_t fRead,
                                           uint64_t fWrite = 0, uint64_t fPotentialCall = 0)
{
    RT_NOREF(fRead, fWrite, fPotentialCall);
    return iemNativeRegAllocTmpForGuestRegReadOnly(pReNative, poff, kIemNativeGstReg_EFlags);
}

DECL_FORCE_INLINE_THROW(uint8_t)
iemNativeRegAllocTmpForGuestEFlagsForUpdate(PIEMRECOMPILERSTATE pReNative, uint32_t *poff, uint64_t fRead,
                                            uint64_t fWrite = 0, uint64_t fPotentialCall = 0)
{
    RT_NOREF(fRead, fWrite, fPotentialCall);
    return iemNativeRegAllocTmpForGuestRegUpdate(pReNative, poff, kIemNativeGstReg_EFlags);
}

#endif



/*********************************************************************************************************************************
*   SIMD register allocator (largely code duplication of the GPR allocator for now but might diverge)                            *
*********************************************************************************************************************************/

DECL_FORCE_INLINE(uint8_t)
iemNativeSimdRegMarkAllocated(PIEMRECOMPILERSTATE pReNative, uint8_t idxSimdReg,
                              IEMNATIVEWHAT enmWhat, uint8_t idxVar = UINT8_MAX) RT_NOEXCEPT
{
    pReNative->Core.bmHstSimdRegs |= RT_BIT_32(idxSimdReg);

    pReNative->Core.aHstSimdRegs[idxSimdReg].enmWhat        = enmWhat;
    pReNative->Core.aHstSimdRegs[idxSimdReg].idxVar         = idxVar;
    pReNative->Core.aHstSimdRegs[idxSimdReg].fGstRegShadows = 0;
    return idxSimdReg;
}


/**
 * Marks host SIMD register @a idxHstSimdReg as containing a shadow copy of guest
 * SIMD register @a enmGstSimdReg.
 *
 * ASSUMES that caller has made sure @a enmGstSimdReg is not associated with any
 * host register before calling.
 */
DECL_FORCE_INLINE(void)
iemNativeSimdRegMarkAsGstSimdRegShadow(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstSimdReg,
                                       IEMNATIVEGSTSIMDREG enmGstSimdReg, uint32_t off)
{
    Assert(!(pReNative->Core.bmGstSimdRegShadows & RT_BIT_64(enmGstSimdReg)));
    Assert(!pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows);
    Assert((unsigned)enmGstSimdReg < (unsigned)kIemNativeGstSimdReg_End);

    pReNative->Core.aidxGstSimdRegShadows[enmGstSimdReg]       = idxHstSimdReg;
    pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows |= RT_BIT_64(enmGstSimdReg);
    pReNative->Core.bmGstSimdRegShadows                        |= RT_BIT_64(enmGstSimdReg);
    pReNative->Core.bmHstSimdRegsWithGstShadow                 |= RT_BIT_32(idxHstSimdReg);
#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    iemNativeDbgInfoAddNativeOffset(pReNative, off);
    iemNativeDbgInfoAddGuestSimdRegShadowing(pReNative, enmGstSimdReg, idxHstSimdReg);
#else
    RT_NOREF(off);
#endif
}


/**
 * Transfers the guest SIMD register shadow claims of @a enmGstSimdReg from @a idxSimdRegFrom
 * to @a idxSimdRegTo.
 */
DECL_FORCE_INLINE(void)
iemNativeSimdRegTransferGstSimdRegShadowing(PIEMRECOMPILERSTATE pReNative, uint8_t idxSimdRegFrom, uint8_t idxSimdRegTo,
                                            IEMNATIVEGSTSIMDREG enmGstSimdReg, uint32_t off)
{
    Assert(pReNative->Core.aHstSimdRegs[idxSimdRegFrom].fGstRegShadows & RT_BIT_64(enmGstSimdReg));
    Assert(pReNative->Core.aidxGstSimdRegShadows[enmGstSimdReg] == idxSimdRegFrom);
    Assert(      (pReNative->Core.bmGstSimdRegShadows & pReNative->Core.aHstSimdRegs[idxSimdRegFrom].fGstRegShadows)
              == pReNative->Core.aHstSimdRegs[idxSimdRegFrom].fGstRegShadows
           && pReNative->Core.bmGstSimdRegShadows < RT_BIT_64(kIemNativeGstReg_End));
    Assert(   (pReNative->Core.bmGstSimdRegShadows & pReNative->Core.aHstSimdRegs[idxSimdRegTo].fGstRegShadows)
           == pReNative->Core.aHstSimdRegs[idxSimdRegTo].fGstRegShadows);
    Assert(   RT_BOOL(pReNative->Core.bmHstSimdRegsWithGstShadow & RT_BIT_32(idxSimdRegFrom))
           == RT_BOOL(pReNative->Core.aHstSimdRegs[idxSimdRegFrom].fGstRegShadows));
    Assert(   pReNative->Core.aHstSimdRegs[idxSimdRegFrom].enmLoaded
           == pReNative->Core.aHstSimdRegs[idxSimdRegTo].enmLoaded);

    uint64_t const fGstRegShadowsFrom = pReNative->Core.aHstSimdRegs[idxSimdRegFrom].fGstRegShadows & ~RT_BIT_64(enmGstSimdReg);
    pReNative->Core.aHstSimdRegs[idxSimdRegFrom].fGstRegShadows  = fGstRegShadowsFrom;
    if (!fGstRegShadowsFrom)
    {
        pReNative->Core.bmHstSimdRegsWithGstShadow               &= ~RT_BIT_32(idxSimdRegFrom);
        pReNative->Core.aHstSimdRegs[idxSimdRegFrom].enmLoaded    = kIemNativeGstSimdRegLdStSz_Invalid;
    }
    pReNative->Core.bmHstSimdRegsWithGstShadow                |= RT_BIT_32(idxSimdRegTo);
    pReNative->Core.aHstSimdRegs[idxSimdRegTo].fGstRegShadows |= RT_BIT_64(enmGstSimdReg);
    pReNative->Core.aidxGstSimdRegShadows[enmGstSimdReg]       = idxSimdRegTo;
#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    iemNativeDbgInfoAddNativeOffset(pReNative, off);
    iemNativeDbgInfoAddGuestSimdRegShadowing(pReNative, enmGstSimdReg, idxSimdRegTo, idxSimdRegFrom);
#else
    RT_NOREF(off);
#endif
}


/**
 * Clear any guest register shadow claims from @a idxHstSimdReg.
 *
 * The register does not need to be shadowing any guest registers.
 */
DECL_FORCE_INLINE(void)
iemNativeSimdRegClearGstSimdRegShadowing(PIEMRECOMPILERSTATE pReNative, uint8_t idxHstSimdReg, uint32_t off)
{
    Assert(      (pReNative->Core.bmGstSimdRegShadows & pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows)
              == pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows
           && pReNative->Core.bmGstSimdRegShadows < RT_BIT_64(kIemNativeGstSimdReg_End));
    Assert(   RT_BOOL(pReNative->Core.bmHstSimdRegsWithGstShadow & RT_BIT_32(idxHstSimdReg))
           == RT_BOOL(pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows));
    Assert(   !(pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows & pReNative->Core.bmGstSimdRegShadowDirtyLo128)
           && !(pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows & pReNative->Core.bmGstSimdRegShadowDirtyHi128));

#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    uint64_t fGstRegs = pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows;
    if (fGstRegs)
    {
        Assert(fGstRegs < RT_BIT_64(kIemNativeGstSimdReg_End));
        iemNativeDbgInfoAddNativeOffset(pReNative, off);
        while (fGstRegs)
        {
            unsigned const iGstReg = ASMBitFirstSetU64(fGstRegs) - 1;
            fGstRegs &= ~RT_BIT_64(iGstReg);
            iemNativeDbgInfoAddGuestSimdRegShadowing(pReNative, (IEMNATIVEGSTSIMDREG)iGstReg, UINT8_MAX, idxHstSimdReg);
        }
    }
#else
    RT_NOREF(off);
#endif

    pReNative->Core.bmHstSimdRegsWithGstShadow        &= ~RT_BIT_32(idxHstSimdReg);
    pReNative->Core.bmGstSimdRegShadows               &= ~pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows;
    pReNative->Core.aHstSimdRegs[idxHstSimdReg].fGstRegShadows = 0;
    pReNative->Core.aHstSimdRegs[idxHstSimdReg].enmLoaded      = kIemNativeGstSimdRegLdStSz_Invalid;
}



#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
/**
 * Emits code to update the guest RIP value by adding the current offset since the start of the last RIP update.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitPcWriteback(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    if (pReNative->Core.offPc)
        return iemNativeEmitPcWritebackSlow(pReNative, off);
    return off;
}
#endif /* IEMNATIVE_WITH_DELAYED_PC_UPDATING  */


/** @note iemNativeTbEntry returns VBOXSTRICTRC, but we don't declare it as
 *        it saves us the trouble of a hidden parameter on MSC/amd64. */
#ifdef RT_ARCH_AMD64
extern "C" IEM_DECL_NATIVE_HLP_DEF(int, iemNativeTbEntry, (PVMCPUCC pVCpu, uintptr_t pfnTbBody));
#elif defined(RT_ARCH_ARM64)
extern "C" IEM_DECL_NATIVE_HLP_DEF(int, iemNativeTbEntry, (PVMCPUCC pVCpu, PCPUMCTX pCpumCtx, uintptr_t pfnTbBody));
#endif

#ifdef IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
extern "C" IEM_DECL_NATIVE_HLP_DEF(int, iemNativeFpCtrlRegRestore, (uint64_t u64RegFpCtrl));
#endif

#endif /* !RT_IN_ASSEMBLER - ASM-NOINC-END */

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_IEMN8veRecompiler_h */

