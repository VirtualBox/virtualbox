/* $Id: bs3kit.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - structures, symbols, macros and stuff.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef BS3KIT_INCLUDED_bs3kit_h
#define BS3KIT_INCLUDED_bs3kit_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef DOXYGEN_RUNNING
# undef  IN_RING0
# define IN_RING0
#endif

#define RT_NO_STRICT            /* Don't drag in IPRT assertion code in inline code we may use (asm.h). */
#include <iprt/cdefs.h>
#include <iprt/types.h>

#ifndef DOXYGEN_RUNNING
# undef  IN_RING0
#endif

/*
 * Make asm.h and friend compatible with our 64-bit assembly config (ASM_CALL64_MSC).
 */
#if defined(__GNUC__) && ARCH_BITS == 64
# undef DECLASM
# ifdef __cplusplus
#  define DECLASM(type)             extern "C" type BS3_CALL
# else
#  define DECLASM(type)             type BS3_CALL
# endif
#endif


/*
 * Work around ms_abi trouble in the gcc camp (gcc bugzilla #50818).
 * ASSUMES all va_lists are in functions with
 */
#if defined(__GNUC__) && ARCH_BITS == 64
# undef  va_list
# undef  va_start
# undef  va_end
# undef  va_copy
# define va_list                    __builtin_ms_va_list
# define va_start(a_Va, a_Arg)      __builtin_ms_va_start(a_Va, a_Arg)
# define va_end(a_Va)               __builtin_ms_va_end(a_Va)
# define va_copy(a_DstVa, a_SrcVa)  __builtin_ms_va_copy(a_DstVa, a_SrcVa)
#endif


/** @def BS3_USE_ALT_16BIT_TEXT_SEG
 * @ingroup grp_bs3kit
 * Combines the BS3_USE_RM_TEXT_SEG,  BS3_USE_X0_TEXT_SEG, and
 * BS3_USE_X1_TEXT_SEG indicators into a single one.
 */
#if defined(BS3_USE_RM_TEXT_SEG) || defined(BS3_USE_X0_TEXT_SEG) || defined(BS3_USE_X1_TEXT_SEG) || defined(DOXYGEN_RUNNING)
# define BS3_USE_ALT_16BIT_TEXT_SEG
#else
# undef  BS3_USE_ALT_16BIT_TEXT_SEG
#endif

/** @def BS3_USE_X0_TEXT_SEG
 * @ingroup grp_bs3kit
 * Emit 16-bit code to the BS3X0TEXT16 segment - ignored for 32-bit and 64-bit.
 *
 * Calling directly into the BS3X0TEXT16 segment is only possible in real-mode
 * and v8086 mode.  In protected mode the real far pointer have to be converted
 * to a protected mode pointer that uses BS3_SEL_X0TEXT16_CS, Bs3TestDoModes and
 * associates does this automatically.
 */
#ifdef DOXYGEN_RUNNING
# define BS3_USE_X0_TEXT_SEG
#endif

/** @def BS3_USE_X1_TEXT_SEG
 * @ingroup grp_bs3kit
 * Emit 16-bit code to the BS3X1TEXT16 segment - ignored for 32-bit and 64-bit.
 *
 * Calling directly into the BS3X1TEXT16 segment is only possible in real-mode
 * and v8086 mode.  In protected mode the real far pointer have to be converted
 * to a protected mode pointer that uses BS3_SEL_X1TEXT16_CS, Bs3TestDoModes and
 * associates does this automatically.
 */
#ifdef DOXYGEN_RUNNING
# define BS3_USE_X1_TEXT_SEG
#endif

/** @def BS3_USE_RM_TEXT_SEG
 * @ingroup grp_bs3kit
 * Emit 16-bit code to the BS3RMTEXT16 segment - ignored for 32-bit and 64-bit.
 *
 * This segment is normally used for real-mode only code, though
 * BS3_SEL_RMTEXT16_CS can be used to call it from protected mode.  Unlike the
 * BS3X0TEXT16 and BS3X1TEXT16 segments which are empty by default, this segment
 * is used by common BS3Kit code.
 */
#ifdef DOXYGEN_RUNNING
# define BS3_USE_X0_TEXT_SEG
#endif

/** @def BS3_MODEL_FAR_CODE
 * @ingroup grp_bs3kit
 * Default compiler model indicates far code.
 */
#ifdef DOXYGEN_RUNNING
# define BS3_MODEL_FAR_CODE
#elif !defined(BS3_MODEL_FAR_CODE) && (defined(__LARGE__) || defined(__MEDIUM__) || defined(__HUGE__)) && ARCH_BITS == 16
# define BS3_MODEL_FAR_CODE
#endif


/*
 * We normally don't want the noreturn / aborts attributes as they mess up stack traces.
 *
 * Note! pragma aux <fnname> aborts can only be used with functions
 *       implemented in C and functions that does not have parameters.
 */
#define BS3_KIT_WITH_NO_RETURN
#ifndef BS3_KIT_WITH_NO_RETURN
# undef  DECL_NO_RETURN
# define DECL_NO_RETURN(type) type
#endif


/*
 * We may want to reuse some IPRT code in the common name space, so we
 * redefine the RT_MANGLER to work like BS3_CMN_NM.  (We cannot use
 * BS3_CMN_NM yet, as we need to include IPRT headers with function
 * declarations before we can define it. Thus the duplciate effort.)
 */
#if ARCH_BITS == 16
# undef RTCALL
# if defined(BS3_USE_ALT_16BIT_TEXT_SEG)
#  define RTCALL __cdecl __far
#  define RT_MANGLER(a_Name) RT_CONCAT(a_Name,_f16)
# else
#  define RTCALL __cdecl __near
#  define RT_MANGLER(a_Name) RT_CONCAT(a_Name,_c16)
# endif
#else
# define RT_MANGLER(a_Name)  RT_CONCAT3(a_Name,_c,ARCH_BITS)
#endif
#include <iprt/mangling.h>
#include <iprt/x86.h>
#include <iprt/err.h>

/*
 * Include data symbol mangling (function mangling/mapping must be done
 * after the protypes).
 */
#include "bs3kit-mangling-data.h"



RT_C_DECLS_BEGIN

/** @defgroup grp_bs3kit     BS3Kit - Boot Sector Kit \#3
 *
 * The BS3Kit is a framework for bare metal floppy/usb image tests,
 * see the @ref pg_bs3kit "doc page" for more.
 *
 * @{ */


/** @name Execution modes.
 * @{ */
#define BS3_MODE_INVALID    UINT8_C(0x00)
#define BS3_MODE_RM         UINT8_C(0x01)  /**< real mode. */
#define BS3_MODE_PE16       UINT8_C(0x11)  /**< 16-bit protected mode kernel+tss, running 16-bit code, unpaged. */
#define BS3_MODE_PE16_32    UINT8_C(0x12)  /**< 16-bit protected mode kernel+tss, running 32-bit code, unpaged. */
#define BS3_MODE_PE16_V86   UINT8_C(0x18)  /**< 16-bit protected mode kernel+tss, running virtual 8086 mode code, unpaged. */
#define BS3_MODE_PE32       UINT8_C(0x22)  /**< 32-bit protected mode kernel+tss, running 32-bit code, unpaged. */
#define BS3_MODE_PE32_16    UINT8_C(0x21)  /**< 32-bit protected mode kernel+tss, running 16-bit code, unpaged. */
#define BS3_MODE_PEV86      UINT8_C(0x28)  /**< 32-bit protected mode kernel+tss, running virtual 8086 mode code, unpaged. */
#define BS3_MODE_PP16       UINT8_C(0x31)  /**< 16-bit protected mode kernel+tss, running 16-bit code, paged. */
#define BS3_MODE_PP16_32    UINT8_C(0x32)  /**< 16-bit protected mode kernel+tss, running 32-bit code, paged. */
#define BS3_MODE_PP16_V86   UINT8_C(0x38)  /**< 16-bit protected mode kernel+tss, running virtual 8086 mode code, paged. */
#define BS3_MODE_PP32       UINT8_C(0x42)  /**< 32-bit protected mode kernel+tss, running 32-bit code, paged. */
#define BS3_MODE_PP32_16    UINT8_C(0x41)  /**< 32-bit protected mode kernel+tss, running 16-bit code, paged. */
#define BS3_MODE_PPV86      UINT8_C(0x48)  /**< 32-bit protected mode kernel+tss, running virtual 8086 mode code, paged. */
#define BS3_MODE_PAE16      UINT8_C(0x51)  /**< 16-bit protected mode kernel+tss, running 16-bit code, PAE paging. */
#define BS3_MODE_PAE16_32   UINT8_C(0x52)  /**< 16-bit protected mode kernel+tss, running 32-bit code, PAE paging. */
#define BS3_MODE_PAE16_V86  UINT8_C(0x58)  /**< 16-bit protected mode kernel+tss, running virtual 8086 mode, PAE paging. */
#define BS3_MODE_PAE32      UINT8_C(0x62)  /**< 32-bit protected mode kernel+tss, running 32-bit code, PAE paging. */
#define BS3_MODE_PAE32_16   UINT8_C(0x61)  /**< 32-bit protected mode kernel+tss, running 16-bit code, PAE paging. */
#define BS3_MODE_PAEV86     UINT8_C(0x68)  /**< 32-bit protected mode kernel+tss, running virtual 8086 mode, PAE paging. */
#define BS3_MODE_LM16       UINT8_C(0x71)  /**< 16-bit long mode (paged), kernel+tss always 64-bit. */
#define BS3_MODE_LM32       UINT8_C(0x72)  /**< 32-bit long mode (paged), kernel+tss always 64-bit. */
#define BS3_MODE_LM64       UINT8_C(0x74)  /**< 64-bit long mode (paged), kernel+tss always 64-bit. */

#define BS3_MODE_CODE_MASK  UINT8_C(0x0f)  /**< Running code mask. */
#define BS3_MODE_CODE_16    UINT8_C(0x01)  /**< Running 16-bit code. */
#define BS3_MODE_CODE_32    UINT8_C(0x02)  /**< Running 32-bit code. */
#define BS3_MODE_CODE_64    UINT8_C(0x04)  /**< Running 64-bit code. */
#define BS3_MODE_CODE_V86   UINT8_C(0x08)  /**< Running 16-bit virtual 8086 code. */

#define BS3_MODE_SYS_MASK   UINT8_C(0xf0)  /**< kernel+tss mask. */
#define BS3_MODE_SYS_RM     UINT8_C(0x00)  /**< Real mode kernel+tss. */
#define BS3_MODE_SYS_PE16   UINT8_C(0x10)  /**< 16-bit protected mode kernel+tss. */
#define BS3_MODE_SYS_PE32   UINT8_C(0x20)  /**< 32-bit protected mode kernel+tss. */
#define BS3_MODE_SYS_PP16   UINT8_C(0x30)  /**< 16-bit paged protected mode kernel+tss. */
#define BS3_MODE_SYS_PP32   UINT8_C(0x40)  /**< 32-bit paged protected mode kernel+tss. */
#define BS3_MODE_SYS_PAE16  UINT8_C(0x50)  /**< 16-bit PAE paged protected mode kernel+tss. */
#define BS3_MODE_SYS_PAE32  UINT8_C(0x60)  /**< 32-bit PAE paged protected mode kernel+tss. */
#define BS3_MODE_SYS_LM     UINT8_C(0x70)  /**< 64-bit (paged) long mode protected mode kernel+tss. */

/** Whether the mode has paging enabled. */
#define BS3_MODE_IS_PAGED(a_fMode)              ((a_fMode) >= BS3_MODE_PP16)
/** Whether the mode has legacy paging enabled (legacy as opposed to PAE or
 * long mode). */
#define BS3_MODE_IS_LEGACY_PAGING(a_fMode)      ((a_fMode) >= BS3_MODE_PP16 && (a_fMode) < BS3_MODE_PAE16)

/** Whether the mode is running v8086 code. */
#define BS3_MODE_IS_V86(a_fMode)                (((a_fMode) & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_V86)
/** Whether the we're executing in real mode or v8086 mode. */
#define BS3_MODE_IS_RM_OR_V86(a_fMode)          ((a_fMode) == BS3_MODE_RM || BS3_MODE_IS_V86(a_fMode))
/** Whether the mode is running 16-bit code, except v8086. */
#define BS3_MODE_IS_16BIT_CODE_NO_V86(a_fMode)  (((a_fMode) & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_16)
/** Whether the mode is running 16-bit code (includes v8086). */
#define BS3_MODE_IS_16BIT_CODE(a_fMode)         (BS3_MODE_IS_16BIT_CODE_NO_V86(a_fMode) || BS3_MODE_IS_V86(a_fMode))
/** Whether the mode is running 32-bit code. */
#define BS3_MODE_IS_32BIT_CODE(a_fMode)         (((a_fMode) & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_32)
/** Whether the mode is running 64-bit code. */
#define BS3_MODE_IS_64BIT_CODE(a_fMode)         (((a_fMode) & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_64)

/** Whether the system is in real mode. */
#define BS3_MODE_IS_RM_SYS(a_fMode)             (((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_RM)
/** Whether the system is some 16-bit mode that isn't real mode. */
#define BS3_MODE_IS_16BIT_SYS_NO_RM(a_fMode)    (   ((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PE16 \
                                                 || ((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PP16 \
                                                 || ((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PAE16)
/** Whether the system is some 16-bit mode (includes real mode). */
#define BS3_MODE_IS_16BIT_SYS(a_fMode)          (BS3_MODE_IS_16BIT_SYS_NO_RM(a_fMode) || BS3_MODE_IS_RM_SYS(a_fMode))
/** Whether the system is some 32-bit mode. */
#define BS3_MODE_IS_32BIT_SYS(a_fMode)          (   ((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PE32 \
                                                 || ((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PP32 \
                                                 || ((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PAE32)
/** Whether the system is long mode. */
#define BS3_MODE_IS_64BIT_SYS(a_fMode)          (((a_fMode) & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_LM)

/** Whether the system is in protected mode (with or without paging).
 * @note Long mode is not included. */
#define BS3_MODE_IS_PM_SYS(a_fMode)             ((a_fMode) >= BS3_MODE_SYS_PE16 && (a_fMode) < BS3_MODE_SYS_LM)

/** @todo testcase: How would long-mode handle a 16-bit TSS loaded prior to the switch? (mainly stack switching wise) Hopefully, it will tripple fault, right? */
/** @} */


/** @name BS3_ADDR_XXX - Static Memory Allocation
 * @{ */
/** The flat load address for the code after the bootsector. */
#define BS3_ADDR_LOAD           0x10000
/** Where we save the boot registers during init.
 * Located right before the code. */
#define BS3_ADDR_REG_SAVE       (BS3_ADDR_LOAD - sizeof(BS3REGCTX) - 8)
/** Where the stack starts (initial RSP value).
 * Located 16 bytes (assumed by boot sector) before the saved registers.
 * SS.BASE=0. The size is a little short of 32KB  */
#define BS3_ADDR_STACK          (BS3_ADDR_REG_SAVE - 16)
/** The ring-0 stack (8KB) for ring transitions. */
#define BS3_ADDR_STACK_R0       0x06000
/** The ring-1 stack (8KB) for ring transitions. */
#define BS3_ADDR_STACK_R1       0x04000
/** The ring-2 stack (8KB) for ring transitions. */
#define BS3_ADDR_STACK_R2       0x02000
/** IST1 ring-0 stack for long mode (4KB), used for double faults elsewhere. */
#define BS3_ADDR_STACK_R0_IST1  0x09000
/** IST2 ring-0 stack for long mode (3KB), used for spare 0 stack elsewhere. */
#define BS3_ADDR_STACK_R0_IST2  0x08000
/** IST3 ring-0 stack for long mode (1KB). */
#define BS3_ADDR_STACK_R0_IST3  0x07400
/** IST4 ring-0 stack for long mode (1KB), used for spare 1 stack elsewhere. */
#define BS3_ADDR_STACK_R0_IST4  0x07000
/** IST5 ring-0 stack for long mode (1KB). */
#define BS3_ADDR_STACK_R0_IST5  0x06c00
/** IST6 ring-0 stack for long mode (1KB). */
#define BS3_ADDR_STACK_R0_IST6  0x06800
/** IST7 ring-0 stack for long mode (1KB). */
#define BS3_ADDR_STACK_R0_IST7  0x06400

/** The base address of the BS3TEXT16 segment (same as BS3_LOAD_ADDR).
 * @sa BS3_SEL_TEXT16 */
#define BS3_ADDR_BS3TEXT16      0x10000
/** The base address of the BS3SYSTEM16 segment.
 * @sa BS3_SEL_SYSTEM16 */
#define BS3_ADDR_BS3SYSTEM16    0x20000
/** The base address of the BS3DATA16/BS3KIT_GRPNM_DATA16 segment.
 * @sa BS3_SEL_DATA16 */
#define BS3_ADDR_BS3DATA16      0x29000
/** @} */

/** @name BS3_SEL_XXX - GDT selector assignments.
 *
 * The real mode segment numbers for BS16TEXT, BS16DATA and BS16SYSTEM are
 * present in the GDT, this allows the 16-bit C/C++ and assembly code to
 * continue using the real mode segment values in ring-0 protected mode.
 *
 * The three segments have fixed locations:
 * | segment     | flat address | real mode segment |
 * | ----------- | ------------ | ----------------- |
 * | BS3TEXT16   |   0x00010000 |             1000h |
 * | BS3SYSTEM16 |   0x00020000 |             2000h |
 * | BS3DATA16   |   0x00029000 |             2900h |
 *
 * This means that we've got a lot of GDT space to play around with.
 *
 * @{ */
#define BS3_SEL_LDT                 0x0010 /**< The LDT selector for Bs3Ldt. */
#define BS3_SEL_TSS16               0x0020 /**< The 16-bit TSS selector. */
#define BS3_SEL_TSS16_DF            0x0028 /**< The 16-bit TSS selector for double faults. */
#define BS3_SEL_TSS16_SPARE0        0x0030 /**< The 16-bit TSS selector for testing. */
#define BS3_SEL_TSS16_SPARE1        0x0038 /**< The 16-bit TSS selector for testing. */
#define BS3_SEL_TSS32               0x0040 /**< The 32-bit TSS selector. */
#define BS3_SEL_TSS32_DF            0x0048 /**< The 32-bit TSS selector for double faults. */
#define BS3_SEL_TSS32_SPARE0        0x0050 /**< The 32-bit TSS selector for testing. */
#define BS3_SEL_TSS32_SPARE1        0x0058 /**< The 32-bit TSS selector for testing. */
#define BS3_SEL_TSS32_IOBP_IRB      0x0060 /**< The 32-bit TSS selector with I/O permission and interrupt redirection bitmaps. */
#define BS3_SEL_TSS32_IRB           0x0068 /**< The 32-bit TSS selector with only interrupt redirection bitmap (IOPB stripped by limit). */
#define BS3_SEL_TSS64               0x0070 /**< The 64-bit TSS selector. */
#define BS3_SEL_TSS64_SPARE0        0x0080 /**< The 64-bit TSS selector. */
#define BS3_SEL_TSS64_SPARE1        0x0090 /**< The 64-bit TSS selector. */
#define BS3_SEL_TSS64_IOBP          0x00a0 /**< The 64-bit TSS selector. */

#define BS3_SEL_RMTEXT16_CS         0x00e0 /**< Conforming code selector for accessing the BS3RMTEXT16 segment. Runtime config. */
#define BS3_SEL_X0TEXT16_CS         0x00e8 /**< Conforming code selector for accessing the BS3X0TEXT16 segment. Runtime config. */
#define BS3_SEL_X1TEXT16_CS         0x00f0 /**< Conforming code selector for accessing the BS3X1TEXT16 segment. Runtime config. */
#define BS3_SEL_VMMDEV_MMIO16       0x00f8 /**< Selector for accessing the VMMDev MMIO segment at 00df000h from 16-bit code. */

/** Checks if @a uSel is in the BS3_SEL_RX_XXX range. */
#define BS3_SEL_IS_IN_RING_RANGE(uSel) ( (unsigned)(uSel -  BS3_SEL_R0_FIRST) < (unsigned)(4 << BS3_SEL_RING_SHIFT) )
#define BS3_SEL_RING_SHIFT          8      /**< For the formula: BS3_SEL_R0_XXX + ((cs & 3) << BS3_SEL_RING_SHIFT) */
#define BS3_SEL_RING_SUB_MASK       0x00f8 /**< Mask for getting the sub-selector. For use with BS3_SEL_R*_FIRST. */

/** Checks if @a uSel is in the BS3_SEL_R0_XXX range. */
#define BS3_SEL_IS_IN_R0_RANGE(uSel) ( (unsigned)(uSel -  BS3_SEL_R0_FIRST) < (unsigned)(1 << BS3_SEL_RING_SHIFT) )
#define BS3_SEL_R0_FIRST            0x0100 /**< The first selector in the ring-0 block. */
#define BS3_SEL_R0_CS16             0x0100 /**< ring-0: 16-bit code selector,  base 0x10000. */
#define BS3_SEL_R0_DS16             0x0108 /**< ring-0: 16-bit data selector,  base 0x23000. */
#define BS3_SEL_R0_SS16             0x0110 /**< ring-0: 16-bit stack selector, base 0x00000. */
#define BS3_SEL_R0_CS32             0x0118 /**< ring-0: 32-bit flat code selector. */
#define BS3_SEL_R0_DS32             0x0120 /**< ring-0: 32-bit flat data selector. */
#define BS3_SEL_R0_SS32             0x0128 /**< ring-0: 32-bit flat stack selector. */
#define BS3_SEL_R0_CS64             0x0130 /**< ring-0: 64-bit flat code selector. */
#define BS3_SEL_R0_DS64             0x0138 /**< ring-0: 64-bit flat data & stack selector. */
#define BS3_SEL_R0_CS16_EO          0x0140 /**< ring-0: 16-bit execute-only code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R0_CS16_CNF         0x0148 /**< ring-0: 16-bit conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R0_CS16_CNF_EO      0x0150 /**< ring-0: 16-bit execute-only conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R0_CS32_EO          0x0158 /**< ring-0: 32-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R0_CS32_CNF         0x0160 /**< ring-0: 32-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R0_CS32_CNF_EO      0x0168 /**< ring-0: 32-bit execute-only conforming code selector, not accessed, flat. */
#define BS3_SEL_R0_CS64_EO          0x0170 /**< ring-0: 64-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R0_CS64_CNF         0x0178 /**< ring-0: 64-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R0_CS64_CNF_EO      0x0180 /**< ring-0: 64-bit execute-only conforming code selector, not accessed, flat. */

#define BS3_SEL_R1_FIRST            0x0200 /**< The first selector in the ring-1 block. */
#define BS3_SEL_R1_CS16             0x0200 /**< ring-1: 16-bit code selector,  base 0x10000. */
#define BS3_SEL_R1_DS16             0x0208 /**< ring-1: 16-bit data selector,  base 0x23000. */
#define BS3_SEL_R1_SS16             0x0210 /**< ring-1: 16-bit stack selector, base 0x00000. */
#define BS3_SEL_R1_CS32             0x0218 /**< ring-1: 32-bit flat code selector. */
#define BS3_SEL_R1_DS32             0x0220 /**< ring-1: 32-bit flat data selector. */
#define BS3_SEL_R1_SS32             0x0228 /**< ring-1: 32-bit flat stack selector. */
#define BS3_SEL_R1_CS64             0x0230 /**< ring-1: 64-bit flat code selector. */
#define BS3_SEL_R1_DS64             0x0238 /**< ring-1: 64-bit flat data & stack selector. */
#define BS3_SEL_R1_CS16_EO          0x0240 /**< ring-1: 16-bit execute-only code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R1_CS16_CNF         0x0248 /**< ring-1: 16-bit conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R1_CS16_CNF_EO      0x0250 /**< ring-1: 16-bit execute-only conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R1_CS32_EO          0x0258 /**< ring-1: 32-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R1_CS32_CNF         0x0260 /**< ring-1: 32-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R1_CS32_CNF_EO      0x0268 /**< ring-1: 32-bit execute-only conforming code selector, not accessed, flat. */
#define BS3_SEL_R1_CS64_EO          0x0270 /**< ring-1: 64-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R1_CS64_CNF         0x0278 /**< ring-1: 64-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R1_CS64_CNF_EO      0x0280 /**< ring-1: 64-bit execute-only conforming code selector, not accessed, flat. */

#define BS3_SEL_R2_FIRST            0x0300 /**< The first selector in the ring-2 block. */
#define BS3_SEL_R2_CS16             0x0300 /**< ring-2: 16-bit code selector,  base 0x10000. */
#define BS3_SEL_R2_DS16             0x0308 /**< ring-2: 16-bit data selector,  base 0x23000. */
#define BS3_SEL_R2_SS16             0x0310 /**< ring-2: 16-bit stack selector, base 0x00000. */
#define BS3_SEL_R2_CS32             0x0318 /**< ring-2: 32-bit flat code selector. */
#define BS3_SEL_R2_DS32             0x0320 /**< ring-2: 32-bit flat data selector. */
#define BS3_SEL_R2_SS32             0x0328 /**< ring-2: 32-bit flat stack selector. */
#define BS3_SEL_R2_CS64             0x0330 /**< ring-2: 64-bit flat code selector. */
#define BS3_SEL_R2_DS64             0x0338 /**< ring-2: 64-bit flat data & stack selector. */
#define BS3_SEL_R2_CS16_EO          0x0340 /**< ring-2: 16-bit execute-only code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R2_CS16_CNF         0x0348 /**< ring-2: 16-bit conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R2_CS16_CNF_EO      0x0350 /**< ring-2: 16-bit execute-only conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R2_CS32_EO          0x0358 /**< ring-2: 32-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R2_CS32_CNF         0x0360 /**< ring-2: 32-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R2_CS32_CNF_EO      0x0368 /**< ring-2: 32-bit execute-only conforming code selector, not accessed, flat. */
#define BS3_SEL_R2_CS64_EO          0x0370 /**< ring-2: 64-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R2_CS64_CNF         0x0378 /**< ring-2: 64-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R2_CS64_CNF_EO      0x0380 /**< ring-2: 64-bit execute-only conforming code selector, not accessed, flat. */

#define BS3_SEL_R3_FIRST            0x0400 /**< The first selector in the ring-3 block. */
#define BS3_SEL_R3_CS16             0x0400 /**< ring-3: 16-bit code selector,  base 0x10000. */
#define BS3_SEL_R3_DS16             0x0408 /**< ring-3: 16-bit data selector,  base 0x23000. */
#define BS3_SEL_R3_SS16             0x0410 /**< ring-3: 16-bit stack selector, base 0x00000. */
#define BS3_SEL_R3_CS32             0x0418 /**< ring-3: 32-bit flat code selector. */
#define BS3_SEL_R3_DS32             0x0420 /**< ring-3: 32-bit flat data selector. */
#define BS3_SEL_R3_SS32             0x0428 /**< ring-3: 32-bit flat stack selector. */
#define BS3_SEL_R3_CS64             0x0430 /**< ring-3: 64-bit flat code selector. */
#define BS3_SEL_R3_DS64             0x0438 /**< ring-3: 64-bit flat data & stack selector. */
#define BS3_SEL_R3_CS16_EO          0x0440 /**< ring-3: 16-bit execute-only code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R3_CS16_CNF         0x0448 /**< ring-3: 16-bit conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R3_CS16_CNF_EO      0x0450 /**< ring-3: 16-bit execute-only conforming code selector, not accessed, 0xfffe limit, CS16 base. */
#define BS3_SEL_R3_CS32_EO          0x0458 /**< ring-3: 32-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R3_CS32_CNF         0x0460 /**< ring-3: 32-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R3_CS32_CNF_EO      0x0468 /**< ring-3: 32-bit execute-only conforming code selector, not accessed, flat. */
#define BS3_SEL_R3_CS64_EO          0x0470 /**< ring-3: 64-bit execute-only code selector, not accessed, flat. */
#define BS3_SEL_R3_CS64_CNF         0x0478 /**< ring-3: 64-bit conforming code selector, not accessed, flat. */
#define BS3_SEL_R3_CS64_CNF_EO      0x0480 /**< ring-3: 64-bit execute-only conforming code selector, not accessed, flat. */

#define BS3_SEL_R3_LAST             0x04f8 /**< ring-3: Last of the BS3_SEL_RX_XXX range. */

#define BS3_SEL_SPARE_FIRST         0x0500 /**< The first selector in the spare block */
#define BS3_SEL_SPARE_00            0x0500 /**< Spare selector number 00h. */
#define BS3_SEL_SPARE_01            0x0508 /**< Spare selector number 01h. */
#define BS3_SEL_SPARE_02            0x0510 /**< Spare selector number 02h. */
#define BS3_SEL_SPARE_03            0x0518 /**< Spare selector number 03h. */
#define BS3_SEL_SPARE_04            0x0520 /**< Spare selector number 04h. */
#define BS3_SEL_SPARE_05            0x0528 /**< Spare selector number 05h. */
#define BS3_SEL_SPARE_06            0x0530 /**< Spare selector number 06h. */
#define BS3_SEL_SPARE_07            0x0538 /**< Spare selector number 07h. */
#define BS3_SEL_SPARE_08            0x0540 /**< Spare selector number 08h. */
#define BS3_SEL_SPARE_09            0x0548 /**< Spare selector number 09h. */
#define BS3_SEL_SPARE_0a            0x0550 /**< Spare selector number 0ah. */
#define BS3_SEL_SPARE_0b            0x0558 /**< Spare selector number 0bh. */
#define BS3_SEL_SPARE_0c            0x0560 /**< Spare selector number 0ch. */
#define BS3_SEL_SPARE_0d            0x0568 /**< Spare selector number 0dh. */
#define BS3_SEL_SPARE_0e            0x0570 /**< Spare selector number 0eh. */
#define BS3_SEL_SPARE_0f            0x0578 /**< Spare selector number 0fh. */
#define BS3_SEL_SPARE_10            0x0580 /**< Spare selector number 10h. */
#define BS3_SEL_SPARE_11            0x0588 /**< Spare selector number 11h. */
#define BS3_SEL_SPARE_12            0x0590 /**< Spare selector number 12h. */
#define BS3_SEL_SPARE_13            0x0598 /**< Spare selector number 13h. */
#define BS3_SEL_SPARE_14            0x05a0 /**< Spare selector number 14h. */
#define BS3_SEL_SPARE_15            0x05a8 /**< Spare selector number 15h. */
#define BS3_SEL_SPARE_16            0x05b0 /**< Spare selector number 16h. */
#define BS3_SEL_SPARE_17            0x05b8 /**< Spare selector number 17h. */
#define BS3_SEL_SPARE_18            0x05c0 /**< Spare selector number 18h. */
#define BS3_SEL_SPARE_19            0x05c8 /**< Spare selector number 19h. */
#define BS3_SEL_SPARE_1a            0x05d0 /**< Spare selector number 1ah. */
#define BS3_SEL_SPARE_1b            0x05d8 /**< Spare selector number 1bh. */
#define BS3_SEL_SPARE_1c            0x05e0 /**< Spare selector number 1ch. */
#define BS3_SEL_SPARE_1d            0x05e8 /**< Spare selector number 1dh. */
#define BS3_SEL_SPARE_1e            0x05f0 /**< Spare selector number 1eh. */
#define BS3_SEL_SPARE_1f            0x05f8 /**< Spare selector number 1fh. */

#define BS3_SEL_TILED               0x0600 /**< 16-bit data tiling: First - base=0x00000000, limit=64KB, DPL=3. */
#define BS3_SEL_TILED_LAST          0x0df8 /**< 16-bit data tiling: Last  - base=0x00ff0000, limit=64KB, DPL=3. */
#define BS3_SEL_TILED_AREA_SIZE     0x001000000 /**< 16-bit data tiling: Size of addressable area, in bytes. (16 MB) */

#define BS3_SEL_FREE_PART1          0x0e00 /**< Free selector space - part \#1. */
#define BS3_SEL_FREE_PART1_LAST     0x0ff8 /**< Free selector space - part \#1, last entry. */

#define BS3_SEL_TEXT16              0x1000 /**< The BS3TEXT16 selector. */

#define BS3_SEL_FREE_PART2          0x1008 /**< Free selector space - part \#2. */
#define BS3_SEL_FREE_PART2_LAST     0x17f8 /**< Free selector space - part \#2, last entry. */

#define BS3_SEL_TILED_R0            0x1800 /**< 16-bit data/stack tiling: First - base=0x00000000, limit=64KB, DPL=0. */
#define BS3_SEL_TILED_R0_LAST       0x1ff8 /**< 16-bit data/stack tiling: Last  - base=0x00ff0000, limit=64KB, DPL=0. */

#define BS3_SEL_SYSTEM16            0x2000 /**< The BS3SYSTEM16 selector. */

#define BS3_SEL_FREE_PART3          0x2008 /**< Free selector space - part \#3. */
#define BS3_SEL_FREE_PART3_LAST     0x28f8 /**< Free selector space - part \#3, last entry. */

#define BS3_SEL_DATA16              0x2900 /**< The BS3DATA16/BS3KIT_GRPNM_DATA16 selector. */

#define BS3_SEL_HIGH16_CS_00        0x2908 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_01        0x2910 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_02        0x2918 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_03        0x2920 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_04        0x2928 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_05        0x2930 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_06        0x2938 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_07        0x2940 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_08        0x2948 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_09        0x2950 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_0a        0x2958 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_0b        0x2960 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_0c        0x2968 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_0d        0x2970 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_0e        0x2978 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_0f        0x2980 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_10        0x2988 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_11        0x2990 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_12        0x2998 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_13        0x29a0 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_14        0x29a8 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_15        0x29b0 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_16        0x29b8 /**< 16-bit CS for high DLLs. */
#define BS3_SEL_HIGH16_CS_17        0x29c0 /**< 16-bit CS for high DLLs. */

#define BS3_SEL_HIGH16_DS_00        0x29c8 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_01        0x29d0 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_02        0x29d8 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_03        0x29e0 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_04        0x29e8 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_05        0x29f0 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_06        0x29f8 /**< 16-bit DS for high DLLs. */
#define BS3_SEL_HIGH16_DS_07        0x2a00 /**< 16-bit DS for high DLLs. */

#define BS3_SEL_FREE_PART4          0x2a08 /**< Free selector space - part \#4. */
#define BS3_SEL_FREE_PART4_LAST     0x2f98 /**< Free selector space - part \#4, last entry. */

#define BS3_SEL_PRE_TEST_PAGE_08    0x2fa0 /**< Selector located 8 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_07    0x2fa8 /**< Selector located 7 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_06    0x2fb0 /**< Selector located 6 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_05    0x2fb8 /**< Selector located 5 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_04    0x2fc0 /**< Selector located 4 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_03    0x2fc8 /**< Selector located 3 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_02    0x2fd0 /**< Selector located 2 selectors before the test page. */
#define BS3_SEL_PRE_TEST_PAGE_01    0x2fd8 /**< Selector located 1 selector  before the test page. */
#define BS3_SEL_TEST_PAGE           0x2fe0 /**< Start of the test page intended for playing around with paging and GDT. */
#define BS3_SEL_TEST_PAGE_00        0x2fe0 /**< Test page selector number 00h (convenience). */
#define BS3_SEL_TEST_PAGE_01        0x2fe8 /**< Test page selector number 01h (convenience). */
#define BS3_SEL_TEST_PAGE_02        0x2ff0 /**< Test page selector number 02h (convenience). */
#define BS3_SEL_TEST_PAGE_03        0x2ff8 /**< Test page selector number 03h (convenience). */
#define BS3_SEL_TEST_PAGE_04        0x3000 /**< Test page selector number 04h (convenience). */
#define BS3_SEL_TEST_PAGE_05        0x3008 /**< Test page selector number 05h (convenience). */
#define BS3_SEL_TEST_PAGE_06        0x3010 /**< Test page selector number 06h (convenience). */
#define BS3_SEL_TEST_PAGE_07        0x3018 /**< Test page selector number 07h (convenience). */
#define BS3_SEL_TEST_PAGE_LAST      0x3fd0 /**< The last selector in the spare page. */

#define BS3_SEL_GDT_LIMIT           0x3fd8 /**< The GDT limit. */
/** @} */

/** @name BS3_SEL_IS_XXX - Predicates for standard selectors.
 *
 * Standard selectors are in the range BS3_SEL_R0_FIRST thru BS3_SEL_R3_LAST.
 *
 * @{ */
#define BS3_SEL_IS_CS16(a_uSel)     (((a_uSel) & 0xf8) == 0x00)
#define BS3_SEL_IS_CS32(a_uSel)     (((a_uSel) & 0xf8) == 0x18)
#define BS3_SEL_IS_CS64(a_uSel)     (((a_uSel) & 0xf8) == 0x30)

#define BS3_SEL_IS_ANY_CS16(a_uSel) (   ((a_uSel) & 0xf8) == 0x00 \
                                     || ((a_uSel) & 0xf8) == 0x40 \
                                     || ((a_uSel) & 0xf8) == 0x48 \
                                     || ((a_uSel) & 0xf8) == 0x50 )
#define BS3_SEL_IS_ANY_CS32(a_uSel) (   ((a_uSel) & 0xf8) == 0x18 \
                                     || ((a_uSel) & 0xf8) == 0x58 \
                                     || ((a_uSel) & 0xf8) == 0x60 \
                                     || ((a_uSel) & 0xf8) == 0x68 )
#define BS3_SEL_IS_ANY_CS64(a_uSel) (   ((a_uSel) & 0xf8) == 0x18 \
                                     || ((a_uSel) & 0xf8) == 0x58 \
                                     || ((a_uSel) & 0xf8) == 0x60 \
                                     || ((a_uSel) & 0xf8) == 0x68 )

#define BS3_SEL_IS_DS16(a_uSel)     (((a_uSel) & 0xf8) == 0x08)
#define BS3_SEL_IS_DS32(a_uSel)     (((a_uSel) & 0xf8) == 0x20)
#define BS3_SEL_IS_DS64(a_uSel)     (((a_uSel) & 0xf8) == 0x38)

#define BS3_SEL_IS_SS16(a_uSel)     (((a_uSel) & 0xf8) == 0x10)
#define BS3_SEL_IS_SS32(a_uSel)     (((a_uSel) & 0xf8) == 0x28)
/** @} */


/** @def BS3_FAR
 * For indicating far pointers in 16-bit code.
 * Does nothing in 32-bit and 64-bit code. */
/** @def BS3_NEAR
 * For indicating near pointers in 16-bit code.
 * Does nothing in 32-bit and 64-bit code. */
/** @def BS3_FAR_CODE
 * For indicating far 16-bit functions.
 * Does nothing in 32-bit and 64-bit code. */
/** @def BS3_NEAR_CODE
 * For indicating near 16-bit functions.
 * Does nothing in 32-bit and 64-bit code. */
/** @def BS3_FAR_DATA
 * For indicating far 16-bit external data, i.e. in a segment other than DATA16.
 * Does nothing in 32-bit and 64-bit code. */
#ifdef M_I86
# define BS3_FAR            __far
# define BS3_NEAR           __near
# define BS3_FAR_CODE       __far
# define BS3_NEAR_CODE      __near
# define BS3_FAR_DATA       __far
#else
# define BS3_FAR
# define BS3_NEAR
# define BS3_FAR_CODE
# define BS3_NEAR_CODE
# define BS3_FAR_DATA
#endif

#if ARCH_BITS == 16 || defined(DOXYGEN_RUNNING)
/** @def BS3_FP_SEG
 * Get the selector (segment) part of a far pointer.
 *
 * @returns selector.
 * @param   a_pv        Far pointer.
 */
# define BS3_FP_SEG(a_pv)            ((uint16_t)(__segment)(void BS3_FAR *)(a_pv))
/** @def BS3_FP_OFF
 * Get the segment offset part of a far pointer.
 *
 * For sake of convenience, this works like a uintptr_t cast in 32-bit and
 * 64-bit code.
 *
 * @returns offset.
 * @param   a_pv        Far pointer.
 */
# define BS3_FP_OFF(a_pv)            ((uint16_t)(void __near *)(a_pv))
/** @def BS3_FP_MAKE
 * Create a far pointer.
 *
 * @returns Far pointer.
 * @param   a_uSeg      The selector/segment.
 * @param   a_off       The offset into the segment.
 */
# define BS3_FP_MAKE(a_uSeg, a_off)  (((__segment)(a_uSeg)) :> ((void __near *)(a_off)))
#else
# define BS3_FP_OFF(a_pv)            ((uintptr_t)(a_pv))
#endif

/**
 * Converts a far real mode address to a 32-bit flat address.
 *
 * @returns Flat address.
 * @param   a_pv        The _real_ _mode_ far pointer to convert.
 */
#define BS3_FP_REAL_TO_FLAT(a_pv)   (((uint32_t)BS3_FP_SEG(a_pv) << 4) + BS3_FP_OFF(a_pv))

/** @def BS3_MAKE_PROT_R0PTR_FROM_FLAT
 * Creates a protected mode pointer from a flat address.
 *
 * For sake of convenience, this macro also works in 32-bit and 64-bit mode,
 * only there it doesn't return a far pointer but a flat point.
 *
 * @returns far void pointer if 16-bit code, near/flat void pointer in 32-bit
 *          and 64-bit.
 * @param   a_uFlat     Flat address in the first 16MB. */
#if ARCH_BITS == 16
# define BS3_MAKE_PROT_R0PTR_FROM_FLAT(a_uFlat)  \
    BS3_FP_MAKE(((uint16_t)(a_uFlat >> 16) << 3) + BS3_SEL_TILED, (uint16_t)(a_uFlat))
#else
# define BS3_MAKE_PROT_R0PTR_FROM_FLAT(a_uFlat)  ((void *)(uintptr_t)(a_uFlat))
#endif

/** @def BS3_MAKE_PROT_R0PTR_FROM_REAL
 * Creates a protected mode pointer from a far real mode address.
 *
 * For sake of convenience, this macro also works in 32-bit and 64-bit mode,
 * only there it doesn't return a far pointer but a flat point.
 *
 * @returns far void pointer if 16-bit code, near/flat void pointer in 32-bit
 *          and 64-bit.
 * @param   a_uSeg      The selector/segment.
 * @param   a_off       The offset into the segment.
 */
#if ARCH_BITS == 16
# define BS3_MAKE_PROT_R0PTR_FROM_REAL(a_uSeg, a_off) BS3_MAKE_PROT_R0PTR_FROM_FLAT(((uint32_t)(a_uSeg) << 4) + (uint16_t)(a_off))
#else
# define BS3_MAKE_PROT_R0PTR_FROM_REAL(a_uSeg, a_off) ( (void *)(uintptr_t)(((uint32_t)(a_uSeg) << 4) + (uint16_t)(a_off)) )
#endif


/** @def BS3_CALL
 * The calling convension used by BS3 functions.  */
#if ARCH_BITS != 64
# define BS3_CALL           __cdecl
#elif !defined(_MSC_VER)
# define BS3_CALL           __attribute__((__ms_abi__))
#else
# define BS3_CALL
#endif

/** @def IN_BS3KIT
 * Indicates that we're in the same link job as the BS3Kit code. */
#ifdef DOXYGEN_RUNNING
# define IN_BS3KIT
#endif

/** @def BS3_DECL
 * Declares a BS3Kit function with default far/near.
 *
 * Until we outgrow BS3TEXT16, we use all near functions in 16-bit.
 *
 * @param a_Type        The return type. */
#if ARCH_BITS != 16 || !defined(BS3_USE_ALT_16BIT_TEXT_SEG)
# define BS3_DECL(a_Type)  BS3_DECL_NEAR(a_Type)
#else
# define BS3_DECL(a_Type)  BS3_DECL_FAR(a_Type)
#endif

/** @def BS3_DECL_NEAR
 * Declares a BS3Kit function, always near everywhere.
 *
 * Until we outgrow BS3TEXT16, we use all near functions in 16-bit.
 *
 * @param a_Type        The return type. */
#ifdef IN_BS3KIT
# define BS3_DECL_NEAR(a_Type)  DECLEXPORT(a_Type) BS3_NEAR_CODE BS3_CALL
#else
# define BS3_DECL_NEAR(a_Type)  DECLIMPORT(a_Type) BS3_NEAR_CODE BS3_CALL
#endif

/** @def BS3_DECL_FAR
 * Declares a BS3Kit function, far 16-bit, otherwise near.
 *
 * Until we outgrow BS3TEXT16, we use all near functions in 16-bit.
 *
 * @param a_Type        The return type. */
#ifdef IN_BS3KIT
# define BS3_DECL_FAR(a_Type)   DECLEXPORT(a_Type) BS3_FAR_CODE BS3_CALL
#else
# define BS3_DECL_FAR(a_Type)   DECLIMPORT(a_Type) BS3_FAR_CODE BS3_CALL
#endif

/** @def BS3_DECL_CALLBACK
 * Declares a BS3Kit callback function (typically static).
 *
 * @param a_Type        The return type. */
#ifdef IN_BS3KIT
# define BS3_DECL_CALLBACK(a_Type)   a_Type BS3_FAR_CODE BS3_CALL
#else
# define BS3_DECL_CALLBACK(a_Type)   a_Type BS3_FAR_CODE BS3_CALL
#endif

/** @def BS3_DECL_NEAR_CALLBACK
 * Declares a near BS3Kit callback function (typically static).
 *
 * 16-bit users must be in CGROUP16!
 *
 * @param a_Type        The return type. */
#ifdef IN_BS3KIT
# define BS3_DECL_NEAR_CALLBACK(a_Type) a_Type BS3_NEAR_CODE BS3_CALL
#else
# define BS3_DECL_NEAR_CALLBACK(a_Type) a_Type BS3_NEAR_CODE BS3_CALL
#endif

/**
 * Constructs a common name.
 *
 * Example: BS3_CMN_NM(Bs3Shutdown)
 *
 * @param   a_Name      The name of the function or global variable.
 */
#define BS3_CMN_NM(a_Name)      RT_CONCAT3(a_Name,_c,ARCH_BITS)

/**
 * Constructs a common function name, far in 16-bit code.
 *
 * Example: BS3_CMN_FAR_NM(Bs3Shutdown)
 *
 * @param   a_Name      The name of the function.
 */
#if ARCH_BITS == 16
# define BS3_CMN_FAR_NM(a_Name) RT_CONCAT(a_Name,_f16)
#else
# define BS3_CMN_FAR_NM(a_Name) RT_CONCAT3(a_Name,_c,ARCH_BITS)
#endif

/**
 * Constructs a common function name, far or near as defined by the source.
 *
 * Which to use in 16-bit mode is defined by BS3_USE_ALT_16BIT_TEXT_SEG.  In
 * 32-bit and 64-bit mode there are no far symbols, only near ones.
 *
 * Example: BS3_CMN_FN_NM(Bs3Shutdown)
 *
 * @param   a_Name      The name of the function.
 */
#if ARCH_BITS != 16 || !defined(BS3_USE_ALT_16BIT_TEXT_SEG)
# define BS3_CMN_FN_NM(a_Name)  BS3_CMN_NM(a_Name)
#else
# define BS3_CMN_FN_NM(a_Name)  BS3_CMN_FAR_NM(a_Name)
#endif


/**
 * Constructs a data name.
 *
 * This glosses over the underscore prefix usage of our 16-bit, 32-bit and
 * 64-bit compilers.
 *
 * Example: @code{.c}
 *  \#define Bs3Gdt BS3_DATA_NM(Bs3Gdt)
 *  extern X86DESC BS3_FAR_DATA Bs3Gdt
 * @endcode
 *
 * @param   a_Name      The name of the global variable.
 * @remarks Mainly used in bs3kit-mangling.h, internal headers and templates.
 */
//converter does this now//#if ARCH_BITS == 64
//converter does this now//# define BS3_DATA_NM(a_Name)  RT_CONCAT(_,a_Name)
//converter does this now//#else
# define BS3_DATA_NM(a_Name)  a_Name
//converter does this now//#endif

/**
 * Template for creating a pointer union type.
 * @param   a_BaseName      The base type name.
 * @param   a_Modifiers     The type modifier.
 */
#define BS3_PTR_UNION_TEMPLATE(a_BaseName, a_Modifiers) \
    typedef union a_BaseName \
    { \
        /** Pointer into the void. */ \
        a_Modifiers void BS3_FAR                  *pv; \
        /** As a signed integer. */ \
        intptr_t                                   i; \
        /** As an unsigned integer. */ \
        uintptr_t                                  u; \
        /** Pointer to char value. */ \
        a_Modifiers char BS3_FAR                   *pch; \
        /** Pointer to char value. */ \
        a_Modifiers unsigned char BS3_FAR          *puch; \
        /** Pointer to a int value. */ \
        a_Modifiers int BS3_FAR                    *pi; \
        /** Pointer to a unsigned int value. */ \
        a_Modifiers unsigned int BS3_FAR           *pu; \
        /** Pointer to a long value. */ \
        a_Modifiers long BS3_FAR                   *pl; \
        /** Pointer to a long value. */ \
        a_Modifiers unsigned long BS3_FAR          *pul; \
        /** Pointer to a memory size value. */ \
        a_Modifiers size_t BS3_FAR                 *pcb; \
        /** Pointer to a byte value. */ \
        a_Modifiers uint8_t BS3_FAR                *pb; \
        /** Pointer to a 8-bit unsigned value. */ \
        a_Modifiers uint8_t BS3_FAR                *pu8; \
        /** Pointer to a 16-bit unsigned value. */ \
        a_Modifiers uint16_t BS3_FAR               *pu16; \
        /** Pointer to a 32-bit unsigned value. */ \
        a_Modifiers uint32_t BS3_FAR               *pu32; \
        /** Pointer to a 64-bit unsigned value. */ \
        a_Modifiers uint64_t BS3_FAR               *pu64; \
        /** Pointer to a UTF-16 character. */ \
        a_Modifiers RTUTF16 BS3_FAR                *pwc; \
        /** Pointer to a UUID character. */ \
        a_Modifiers RTUUID BS3_FAR                 *pUuid; \
    } a_BaseName; \
    /** Pointer to a pointer union. */ \
    typedef a_BaseName *RT_CONCAT(P,a_BaseName)
BS3_PTR_UNION_TEMPLATE(BS3PTRUNION, RT_NOTHING);
BS3_PTR_UNION_TEMPLATE(BS3CPTRUNION, const);
BS3_PTR_UNION_TEMPLATE(BS3VPTRUNION, volatile);
BS3_PTR_UNION_TEMPLATE(BS3CVPTRUNION, const volatile);

/** Generic far function type. */
typedef BS3_DECL_FAR(void)  FNBS3FAR(void);
/** Generic far function pointer type. */
typedef FNBS3FAR           *FPFNBS3FAR;

/** Generic near function type. */
typedef BS3_DECL_NEAR(void) FNBS3NEAR(void);
/** Generic near function pointer type. */
typedef FNBS3NEAR          *PFNBS3NEAR;

/** Generic far 16:16 function pointer type for address conversion functions. */
#if ARCH_BITS == 16
typedef FPFNBS3FAR          PFNBS3FARADDRCONV;
#else
typedef uint32_t            PFNBS3FARADDRCONV;
#endif

/** The system call vector. */
#define BS3_TRAP_SYSCALL        UINT8_C(0x20)

/** @name System call numbers (ax).
 * Paramenters are generally passed in registers specific to each system call,
 * however cx:xSI is used for passing a pointer parameter.
 * @{ */
/** Print char (cl). */
#define BS3_SYSCALL_PRINT_CHR   UINT16_C(0x0001)
/** Print string (pointer in cx:xSI, length in dx). */
#define BS3_SYSCALL_PRINT_STR   UINT16_C(0x0002)
/** Switch to ring-0. */
#define BS3_SYSCALL_TO_RING0    UINT16_C(0x0003)
/** Switch to ring-1. */
#define BS3_SYSCALL_TO_RING1    UINT16_C(0x0004)
/** Switch to ring-2. */
#define BS3_SYSCALL_TO_RING2    UINT16_C(0x0005)
/** Switch to ring-3. */
#define BS3_SYSCALL_TO_RING3    UINT16_C(0x0006)
/** Restore context (pointer in cx:xSI, flags in dx). */
#define BS3_SYSCALL_RESTORE_CTX UINT16_C(0x0007)
/** Set DRx register (value in ESI, register number in dl). */
#define BS3_SYSCALL_SET_DRX     UINT16_C(0x0008)
/** Get DRx register (register number in dl, value returned in ax:dx). */
#define BS3_SYSCALL_GET_DRX     UINT16_C(0x0009)
/** Set CRx register (value in ESI, register number in dl). */
#define BS3_SYSCALL_SET_CRX     UINT16_C(0x000a)
/** Get CRx register (register number in dl, value returned in ax:dx). */
#define BS3_SYSCALL_GET_CRX     UINT16_C(0x000b)
/** Set the task register (value in ESI). */
#define BS3_SYSCALL_SET_TR      UINT16_C(0x000c)
/** Get the task register (value returned in ax). */
#define BS3_SYSCALL_GET_TR      UINT16_C(0x000d)
/** Set the LDT register (value in ESI). */
#define BS3_SYSCALL_SET_LDTR    UINT16_C(0x000e)
/** Get the LDT register (value returned in ax). */
#define BS3_SYSCALL_GET_LDTR    UINT16_C(0x000f)
/** Set XCR0 register (value in edx:esi). */
#define BS3_SYSCALL_SET_XCR0    UINT16_C(0x0010)
/** Get XCR0 register (value returned in edx:eax). */
#define BS3_SYSCALL_GET_XCR0    UINT16_C(0x0011)
/** The last system call value. */
#define BS3_SYSCALL_LAST        BS3_SYSCALL_GET_XCR0
/** @} */



/** @defgroup grp_bs3kit_system System Structures
 * @{ */
/** The GDT, indexed by BS3_SEL_XXX shifted by 3. */
extern X86DESC BS3_FAR_DATA Bs3Gdt[(BS3_SEL_GDT_LIMIT + 1) / 8];

extern X86DESC64 BS3_FAR_DATA Bs3Gdt_Ldt;                   /**< @see BS3_SEL_LDT */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss16;                  /**< @see BS3_SEL_TSS16  */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss16DoubleFault;       /**< @see BS3_SEL_TSS16_DF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss16Spare0;            /**< @see BS3_SEL_TSS16_SPARE0 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss16Spare1;            /**< @see BS3_SEL_TSS16_SPARE1 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss32;                  /**< @see BS3_SEL_TSS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss32DoubleFault;       /**< @see BS3_SEL_TSS32_DF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss32Spare0;            /**< @see BS3_SEL_TSS32_SPARE0 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss32Spare1;            /**< @see BS3_SEL_TSS32_SPARE1 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss32IobpIntRedirBm;    /**< @see BS3_SEL_TSS32_IOBP_IRB */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss32IntRedirBm;        /**< @see BS3_SEL_TSS32_IRB */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss64;                  /**< @see BS3_SEL_TSS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss64Spare0;            /**< @see BS3_SEL_TSS64_SPARE0 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss64Spare1;            /**< @see BS3_SEL_TSS64_SPARE1 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_Tss64Iobp;              /**< @see BS3_SEL_TSS64_IOBP */
extern X86DESC BS3_FAR_DATA Bs3Gdte_RMTEXT16_CS;            /**< @see BS3_SEL_RMTEXT16_CS */
extern X86DESC BS3_FAR_DATA Bs3Gdte_X0TEXT16_CS;            /**< @see BS3_SEL_X0TEXT16_CS */
extern X86DESC BS3_FAR_DATA Bs3Gdte_X1TEXT16_CS;            /**< @see BS3_SEL_X1TEXT16_CS */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_MMIO16;              /**< @see BS3_SEL_VMMDEV_MMIO16 */

extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_First;               /**< @see BS3_SEL_R0_FIRST */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS16;                /**< @see BS3_SEL_R0_CS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_DS16;                /**< @see BS3_SEL_R0_DS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_SS16;                /**< @see BS3_SEL_R0_SS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS32;                /**< @see BS3_SEL_R0_CS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_DS32;                /**< @see BS3_SEL_R0_DS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_SS32;                /**< @see BS3_SEL_R0_SS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS64;                /**< @see BS3_SEL_R0_CS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_DS64;                /**< @see BS3_SEL_R0_DS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS16_EO;             /**< @see BS3_SEL_R0_CS16_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS16_CNF;            /**< @see BS3_SEL_R0_CS16_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS16_CND_EO;         /**< @see BS3_SEL_R0_CS16_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS32_EO;             /**< @see BS3_SEL_R0_CS32_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS32_CNF;            /**< @see BS3_SEL_R0_CS32_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS32_CNF_EO;         /**< @see BS3_SEL_R0_CS32_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS64_EO;             /**< @see BS3_SEL_R0_CS64_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS64_CNF;            /**< @see BS3_SEL_R0_CS64_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R0_CS64_CNF_EO;         /**< @see BS3_SEL_R0_CS64_CNF_EO */

extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_First;               /**< @see BS3_SEL_R1_FIRST */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS16;                /**< @see BS3_SEL_R1_CS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_DS16;                /**< @see BS3_SEL_R1_DS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_SS16;                /**< @see BS3_SEL_R1_SS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS32;                /**< @see BS3_SEL_R1_CS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_DS32;                /**< @see BS3_SEL_R1_DS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_SS32;                /**< @see BS3_SEL_R1_SS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS64;                /**< @see BS3_SEL_R1_CS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_DS64;                /**< @see BS3_SEL_R1_DS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS16_EO;             /**< @see BS3_SEL_R1_CS16_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS16_CNF;            /**< @see BS3_SEL_R1_CS16_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS16_CND_EO;         /**< @see BS3_SEL_R1_CS16_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS32_EO;             /**< @see BS3_SEL_R1_CS32_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS32_CNF;            /**< @see BS3_SEL_R1_CS32_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS32_CNF_EO;         /**< @see BS3_SEL_R1_CS32_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS64_EO;             /**< @see BS3_SEL_R1_CS64_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS64_CNF;            /**< @see BS3_SEL_R1_CS64_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R1_CS64_CNF_EO;         /**< @see BS3_SEL_R1_CS64_CNF_EO */

extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_First;               /**< @see BS3_SEL_R2_FIRST */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS16;                /**< @see BS3_SEL_R2_CS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_DS16;                /**< @see BS3_SEL_R2_DS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_SS16;                /**< @see BS3_SEL_R2_SS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS32;                /**< @see BS3_SEL_R2_CS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_DS32;                /**< @see BS3_SEL_R2_DS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_SS32;                /**< @see BS3_SEL_R2_SS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS64;                /**< @see BS3_SEL_R2_CS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_DS64;                /**< @see BS3_SEL_R2_DS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS16_EO;             /**< @see BS3_SEL_R2_CS16_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS16_CNF;            /**< @see BS3_SEL_R2_CS16_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS16_CND_EO;         /**< @see BS3_SEL_R2_CS16_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS32_EO;             /**< @see BS3_SEL_R2_CS32_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS32_CNF;            /**< @see BS3_SEL_R2_CS32_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS32_CNF_EO;         /**< @see BS3_SEL_R2_CS32_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS64_EO;             /**< @see BS3_SEL_R2_CS64_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS64_CNF;            /**< @see BS3_SEL_R2_CS64_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R2_CS64_CNF_EO;         /**< @see BS3_SEL_R2_CS64_CNF_EO */

extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_First;               /**< @see BS3_SEL_R3_FIRST */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS16;                /**< @see BS3_SEL_R3_CS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_DS16;                /**< @see BS3_SEL_R3_DS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_SS16;                /**< @see BS3_SEL_R3_SS16 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS32;                /**< @see BS3_SEL_R3_CS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_DS32;                /**< @see BS3_SEL_R3_DS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_SS32;                /**< @see BS3_SEL_R3_SS32 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS64;                /**< @see BS3_SEL_R3_CS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_DS64;                /**< @see BS3_SEL_R3_DS64 */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS16_EO;             /**< @see BS3_SEL_R3_CS16_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS16_CNF;            /**< @see BS3_SEL_R3_CS16_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS16_CND_EO;         /**< @see BS3_SEL_R3_CS16_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS32_EO;             /**< @see BS3_SEL_R3_CS32_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS32_CNF;            /**< @see BS3_SEL_R3_CS32_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS32_CNF_EO;         /**< @see BS3_SEL_R3_CS32_CNF_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS64_EO;             /**< @see BS3_SEL_R3_CS64_EO */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS64_CNF;            /**< @see BS3_SEL_R3_CS64_CNF */
extern X86DESC BS3_FAR_DATA Bs3Gdte_R3_CS64_CNF_EO;         /**< @see BS3_SEL_R3_CS64_CNF_EO */

extern X86DESC BS3_FAR_DATA Bs3GdteSpare00; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_00 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare01; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_01 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare02; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_02 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare03; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_03 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare04; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_04 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare05; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_05 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare06; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_06 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare07; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_07 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare08; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_08 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare09; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_09 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare0a; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_0a */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare0b; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_0b */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare0c; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_0c */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare0d; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_0d */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare0e; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_0e */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare0f; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_0f */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare10; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_10 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare11; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_11 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare12; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_12 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare13; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_13 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare14; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_14 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare15; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_15 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare16; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_16 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare17; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_17 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare18; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_18 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare19; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_19 */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare1a; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_1a */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare1b; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_1b */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare1c; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_1c */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare1d; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_1d */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare1e; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_1e */
extern X86DESC BS3_FAR_DATA Bs3GdteSpare1f; /**< GDT entry for playing with in testcases. @see BS3_SEL_SPARE_1f */

/** GDTs setting up the tiled 16-bit access to the first 16 MBs of memory.
 * @see BS3_SEL_TILED, BS3_SEL_TILED_LAST, BS3_SEL_TILED_AREA_SIZE */
extern X86DESC BS3_FAR_DATA Bs3GdteTiled[256];
/** Free GDTes, part \#1. */
extern X86DESC BS3_FAR_DATA Bs3GdteFreePart1[64];
/** The BS3TEXT16/BS3CLASS16CODE GDT entry. @see BS3_SEL_TEXT16   */
extern X86DESC BS3_FAR_DATA Bs3Gdte_CODE16;
/** Free GDTes, part \#2. */
extern X86DESC BS3_FAR_DATA Bs3GdteFreePart2[511];
/** The BS3SYSTEM16 GDT entry. */
extern X86DESC BS3_FAR_DATA Bs3Gdte_SYSTEM16;
/** Free GDTes, part \#3. */
extern X86DESC BS3_FAR_DATA Bs3GdteFreePart3[223];
/** The BS3DATA16/BS3KIT_GRPNM_DATA16 GDT entry. */
extern X86DESC BS3_FAR_DATA Bs3Gdte_DATA16;

/** 16-bit CSes for high DLLs. */
extern X86DESC BS3_FAR_DATA Bs3GdteHighDllCSes[24];
/** 16-bit DSes for high DLLs. */
extern X86DESC BS3_FAR_DATA Bs3GdteHighDllDSes[8];

/** Free GDTes, part \#4. */
extern X86DESC BS3_FAR_DATA Bs3GdteFreePart4[179];

extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage08; /**< GDT entry 8 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_08 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage07; /**< GDT entry 7 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_07 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage06; /**< GDT entry 6 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_06 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage05; /**< GDT entry 5 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_05 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage04; /**< GDT entry 4 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_04 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage03; /**< GDT entry 3 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_03 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage02; /**< GDT entry 2 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_02 */
extern X86DESC BS3_FAR_DATA Bs3GdtePreTestPage01; /**< GDT entry 1 selectors prior to the test page, testcase resource. @see BS3_SEL_PRE_TEST_PAGE_01 */
/** Array of GDT entries starting on a page boundrary and filling (almost) the
 * whole page.   This is for playing with paging and GDT usage.
 * @see BS3_SEL_TEST_PAGE */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage[2043];
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage00; /**< GDT entry 0 on the test page (convenience). @see BS3_SEL_TEST_PAGE_00 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage01; /**< GDT entry 1 on the test page (convenience). @see BS3_SEL_TEST_PAGE_01 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage02; /**< GDT entry 2 on the test page (convenience). @see BS3_SEL_TEST_PAGE_02 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage03; /**< GDT entry 3 on the test page (convenience). @see BS3_SEL_TEST_PAGE_03 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage04; /**< GDT entry 4 on the test page (convenience). @see BS3_SEL_TEST_PAGE_04 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage05; /**< GDT entry 5 on the test page (convenience). @see BS3_SEL_TEST_PAGE_05 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage06; /**< GDT entry 6 on the test page (convenience). @see BS3_SEL_TEST_PAGE_06 */
extern X86DESC BS3_FAR_DATA Bs3GdteTestPage07; /**< GDT entry 7 on the test page (convenience). @see BS3_SEL_TEST_PAGE_07 */

/** The end of the GDT (exclusive - contains eye-catcher string). */
extern X86DESC BS3_FAR_DATA Bs3GdtEnd;

/** The default 16-bit TSS. */
extern X86TSS16  BS3_FAR_DATA Bs3Tss16;
extern X86TSS16  BS3_FAR_DATA Bs3Tss16DoubleFault;
extern X86TSS16  BS3_FAR_DATA Bs3Tss16Spare0;
extern X86TSS16  BS3_FAR_DATA Bs3Tss16Spare1;
/** The default 32-bit TSS. */
extern X86TSS32  BS3_FAR_DATA Bs3Tss32;
extern X86TSS32  BS3_FAR_DATA Bs3Tss32DoubleFault;
extern X86TSS32  BS3_FAR_DATA Bs3Tss32Spare0;
extern X86TSS32  BS3_FAR_DATA Bs3Tss32Spare1;
/** The default 64-bit TSS. */
extern X86TSS64  BS3_FAR_DATA Bs3Tss64;
extern X86TSS64  BS3_FAR_DATA Bs3Tss64Spare0;
extern X86TSS64  BS3_FAR_DATA Bs3Tss64Spare1;
extern X86TSS64  BS3_FAR_DATA Bs3Tss64WithIopb;
extern X86TSS32  BS3_FAR_DATA Bs3Tss32WithIopb;
/** Interrupt redirection bitmap used by Bs3Tss32WithIopb. */
extern uint8_t   BS3_FAR_DATA Bs3SharedIntRedirBm[32];
/** I/O permission bitmap used by Bs3Tss32WithIopb and Bs3Tss64WithIopb. */
extern uint8_t   BS3_FAR_DATA Bs3SharedIobp[8192+2];
/** End of the I/O permission bitmap (exclusive). */
extern uint8_t   BS3_FAR_DATA Bs3SharedIobpEnd;
/** 16-bit IDT. */
extern X86DESC   BS3_FAR_DATA Bs3Idt16[256];
/** 32-bit IDT. */
extern X86DESC   BS3_FAR_DATA Bs3Idt32[256];
/** 64-bit IDT. */
extern X86DESC64 BS3_FAR_DATA Bs3Idt64[256];
/** Structure for the LIDT instruction for loading the 16-bit IDT. */
extern X86XDTR64 BS3_FAR_DATA Bs3Lidt_Idt16;
/** Structure for the LIDT instruction for loading the 32-bit IDT. */
extern X86XDTR64 BS3_FAR_DATA Bs3Lidt_Idt32;
/** Structure for the LIDT instruction for loading the 64-bit IDT. */
extern X86XDTR64 BS3_FAR_DATA Bs3Lidt_Idt64;
/** Structure for the LIDT instruction for loading the real mode interrupt
 *  vector table. */
extern X86XDTR64 BS3_FAR_DATA Bs3Lidt_Ivt;
/** Structure for the LGDT instruction for loading the current GDT. */
extern X86XDTR64 BS3_FAR_DATA Bs3Lgdt_Gdt;
/** Structure for the LGDT instruction for loading the default GDT. */
extern X86XDTR64 BS3_FAR_DATA Bs3LgdtDef_Gdt;
/** The LDT (all entries are empty, fill in for testing). */
extern X86DESC   BS3_FAR_DATA Bs3Ldt[116];
/** The end of the LDT (exclusive).   */
extern X86DESC   BS3_FAR_DATA Bs3LdtEnd;

/** @} */


/** @name Segment start and end markers, sizes.
 * @{ */
/** Start of the BS3TEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Text16_StartOfSegment;
/** End of the BS3TEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Text16_EndOfSegment;
/** The size of the BS3TEXT16 segment.   */
extern uint16_t BS3_FAR_DATA Bs3Text16_Size;

/** Start of the BS3SYSTEM16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3System16_StartOfSegment;
/** End of the BS3SYSTEM16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3System16_EndOfSegment;

/** Start of the BS3DATA16/BS3KIT_GRPNM_DATA16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Data16_StartOfSegment;
/** End of the BS3DATA16/BS3KIT_GRPNM_DATA16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Data16_EndOfSegment;

/** Start of the BS3RMTEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3RmText16_StartOfSegment;
/** End of the BS3RMTEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3RmText16_EndOfSegment;
/** The size of the BS3RMTEXT16 segment.   */
extern uint16_t BS3_FAR_DATA Bs3RmText16_Size;
/** The flat start address of the BS3X0TEXT16 segment.   */
extern uint32_t BS3_FAR_DATA Bs3RmText16_FlatAddr;

/** Start of the BS3X0TEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3X0Text16_StartOfSegment;
/** End of the BS3X0TEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3X0Text16_EndOfSegment;
/** The size of the BS3X0TEXT16 segment.   */
extern uint16_t BS3_FAR_DATA Bs3X0Text16_Size;
/** The flat start address of the BS3X0TEXT16 segment.   */
extern uint32_t BS3_FAR_DATA Bs3X0Text16_FlatAddr;

/** Start of the BS3X1TEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3X1Text16_StartOfSegment;
/** End of the BS3X1TEXT16 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3X1Text16_EndOfSegment;
/** The size of the BS3X1TEXT16 segment.   */
extern uint16_t BS3_FAR_DATA Bs3X1Text16_Size;
/** The flat start address of the BS3X1TEXT16 segment.   */
extern uint32_t BS3_FAR_DATA Bs3X1Text16_FlatAddr;

/** Start of the BS3TEXT32 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Text32_StartOfSegment;
/** Start of the BS3TEXT32 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Text32_EndOfSegment;

/** Start of the BS3DATA32 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Data32_StartOfSegment;
/** Start of the BS3DATA32 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Data32_EndOfSegment;

/** Start of the BS3TEXT64 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Text64_StartOfSegment;
/** Start of the BS3TEXT64 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Text64_EndOfSegment;

/** Start of the BS3DATA64 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Data64_StartOfSegment;
/** Start of the BS3DATA64 segment.   */
extern uint8_t  BS3_FAR_DATA Bs3Data64_EndOfSegment;

/** The size of the Data16, Text32, Text64, Data32 and Data64 blob. */
extern uint32_t BS3_FAR_DATA Bs3Data16Thru64Text32And64_TotalSize;
/** The total image size (from Text16 thu Data64). */
extern uint32_t BS3_FAR_DATA Bs3TotalImageSize;
/** @} */


/** Lower case hex digits. */
extern char const g_achBs3HexDigits[16+1];
/** Upper case hex digits. */
extern char const g_achBs3HexDigitsUpper[16+1];


/** The current mode (BS3_MODE_XXX) of CPU \#0. */
extern uint8_t    g_bBs3CurrentMode;

/** Hint for 16-bit trap handlers regarding the high word of EIP. */
extern uint32_t   g_uBs3TrapEipHint;

/** Set to disable special V8086 \#GP and \#UD handling in Bs3TrapDefaultHandler.
 * This is useful for getting   */
extern bool volatile g_fBs3TrapNoV86Assist;

/** Copy of the original real-mode interrupt vector table. */
extern RTFAR16 g_aBs3RmIvtOriginal[256];


#ifdef __WATCOMC__
/**
 * Executes the SMSW instruction and returns the value.
 *
 * @returns Machine status word.
 */
uint16_t Bs3AsmSmsw(void);
# pragma aux Bs3AsmSmsw = \
        ".286" \
        "smsw ax" \
        value [ax] modify exact [ax] nomemory;
#endif


/** @defgroup bs3kit_cross_ptr  Cross Context Pointer Type
 *
 * The cross context pointer type is
 *
 * @{ */

/**
 * Cross context pointer base type.
 */
typedef union BS3XPTR
{
    /** The flat pointer.   */
    uint32_t        uFlat;
    /** 16-bit view. */
    struct
    {
        uint16_t    uLow;
        uint16_t    uHigh;
    } u;
#if ARCH_BITS == 16
    /** 16-bit near pointer. */
    void __near    *pvNear;
#elif ARCH_BITS == 32
    /** 32-bit pointer. */
    void           *pvRaw;
#endif
} BS3XPTR;
AssertCompileSize(BS3XPTR, 4);


/** @def BS3_XPTR_DEF_INTERNAL
 * Internal worker.
 *
 * @param   a_Scope     RT_NOTHING if structure or global, static or extern
 *                      otherwise.
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 * @internal
 */
#if ARCH_BITS == 16
# define BS3_XPTR_DEF_INTERNAL(a_Scope, a_Type, a_Name) \
    a_Scope union \
    { \
        BS3XPTR         XPtr; \
        a_Type __near  *pNearTyped; \
    } a_Name
#elif ARCH_BITS == 32
# define BS3_XPTR_DEF_INTERNAL(a_Scope, a_Type, a_Name) \
    a_Scope union \
    { \
        BS3XPTR         XPtr; \
        a_Type         *pTyped; \
    } a_Name
#elif ARCH_BITS == 64
# define BS3_XPTR_DEF_INTERNAL(a_Scope, a_Type, a_Name) \
    a_Scope union \
    { \
        BS3XPTR         XPtr; \
    } a_Name
#else
# error "ARCH_BITS"
#endif

/** @def BS3_XPTR_MEMBER
 * Defines a pointer member that can be shared by all CPU modes.
 *
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 */
#define BS3_XPTR_MEMBER(a_Type, a_Name) BS3_XPTR_DEF_INTERNAL(RT_NOTHING, a_Type, a_Name)

/** @def BS3_XPTR_AUTO
 * Defines a pointer static variable for working with an XPTR.
 *
 * This is typically used to convert flat pointers into context specific
 * pointers.
 *
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 */
#define BS3_XPTR_AUTO(a_Type, a_Name) BS3_XPTR_DEF_INTERNAL(RT_NOTHING, a_Type, a_Name)

/** @def BS3_XPTR_SET_FLAT
 * Sets a cross context pointer.
 *
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 * @param   a_uFlatPtr  The flat pointer value to assign.  If the x-pointer is
 *                      used in real mode, this must be less than 1MB.
 *                      Otherwise the limit is 16MB (due to selector tiling).
 */
#define BS3_XPTR_SET_FLAT(a_Type, a_Name, a_uFlatPtr) \
    do { a_Name.XPtr.uFlat = (a_uFlatPtr); } while (0)

/** @def BS3_XPTR_GET_FLAT
 * Gets the flat address of a cross context pointer.
 *
 * @returns 32-bit flat pointer.
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 */
#define BS3_XPTR_GET_FLAT(a_Type, a_Name) (a_Name.XPtr.uFlat)

/** @def BS3_XPTR_GET_FLAT_LOW
 * Gets the low 16 bits of the flat address.
 *
 * @returns Low 16 bits of the flat pointer.
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 */
#define BS3_XPTR_GET_FLAT_LOW(a_Type, a_Name) (a_Name.XPtr.u.uLow)


#if ARCH_BITS == 16

/**
 * Gets the current ring number.
 * @returns Ring number.
 */
DECLINLINE(uint16_t) Bs3Sel16GetCurRing(void);
# pragma aux Bs3Sel16GetCurRing = \
            "mov ax, ss" \
            "and ax, 3" \
            value [ax] modify exact [ax] nomemory;

/**
 * Converts the high word of a flat pointer into a 16-bit selector.
 *
 * This makes use of the tiled area.  It also handles real mode.
 *
 * @returns Segment selector value.
 * @param   uHigh       The high part of flat pointer.
 * @sa BS3_XPTR_GET, BS3_XPTR_SET
 */
DECLINLINE(__segment) Bs3Sel16HighFlatPtrToSelector(uint16_t uHigh)
{
    if (!BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode))
        return (__segment)(((uHigh << 3) + BS3_SEL_TILED) | Bs3Sel16GetCurRing());
    return (__segment)(uHigh << 12);
}

#endif /* ARCH_BITS == 16 */

/** @def BS3_XPTR_GET
 * Gets the current context pointer value.
 *
 * @returns Usable pointer.
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 */
#if ARCH_BITS == 16
# define BS3_XPTR_GET(a_Type, a_Name) \
    ((a_Type BS3_FAR *)BS3_FP_MAKE(Bs3Sel16HighFlatPtrToSelector((a_Name).XPtr.u.uHigh), (a_Name).pNearTyped))
#elif ARCH_BITS == 32
# define BS3_XPTR_GET(a_Type, a_Name)       ((a_Name).pTyped)
#elif ARCH_BITS == 64
# define BS3_XPTR_GET(a_Type, a_Name)       ((a_Type *)(uintptr_t)(a_Name).XPtr.uFlat)
#else
# error "ARCH_BITS"
#endif

/** @def BS3_XPTR_SET
 * Gets the current context pointer value.
 *
 * @returns Usable pointer.
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 * @param   a_pValue    The new pointer value, current context pointer.
 */
#if ARCH_BITS == 16
# define BS3_XPTR_SET(a_Type, a_Name, a_pValue) \
    do { \
        a_Type BS3_FAR *pTypeCheck = (a_pValue); \
        if (BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode)) \
            (a_Name).XPtr.uFlat = BS3_FP_OFF(pTypeCheck) + ((uint32_t)BS3_FP_SEG(pTypeCheck) << 4); \
        else \
        { \
            (a_Name).XPtr.u.uLow  = BS3_FP_OFF(pTypeCheck); \
            (a_Name).XPtr.u.uHigh = ((BS3_FP_SEG(pTypeCheck) & UINT16_C(0xfff8)) - BS3_SEL_TILED) >> 3; \
        } \
    } while (0)
#elif ARCH_BITS == 32
# define BS3_XPTR_SET(a_Type, a_Name, a_pValue) \
    do { (a_Name).pTyped = (a_pValue); } while (0)
#elif ARCH_BITS == 64
# define BS3_XPTR_SET(a_Type, a_Name, a_pValue) \
    do { \
        a_Type *pTypeCheck  = (a_pValue);  \
        (a_Name).XPtr.uFlat = (uint32_t)(uintptr_t)pTypeCheck; \
    } while (0)
#else
# error "ARCH_BITS"
#endif


/** @def BS3_XPTR_IS_NULL
 * Checks if the cross context pointer is NULL.
 *
 * @returns true if NULL, false if not.
 * @param   a_Type      The type we're pointing to.
 * @param   a_Name      The member or variable name.
 */
#define BS3_XPTR_IS_NULL(a_Type, a_Name)    ((a_Name).XPtr.uFlat == 0)

/**
 * Gets a working pointer from a flat address.
 *
 * @returns Current context pointer.
 * @param   uFlatPtr    The flat address to convert (32-bit or 64-bit).
 */
DECLINLINE(void BS3_FAR *) Bs3XptrFlatToCurrent(RTCCUINTXREG uFlatPtr)
{
    BS3_XPTR_AUTO(void, pTmp);
    BS3_XPTR_SET_FLAT(void, pTmp, uFlatPtr);
    return BS3_XPTR_GET(void, pTmp);
}

/** @} */



/** @defgroup grp_bs3kit_cmn    Common Functions and Data
 *
 * The common functions comes in three variations: 16-bit, 32-bit and 64-bit.
 * Templated code uses the #BS3_CMN_NM macro to mangle the name according to the
 * desired
 *
 * @{
 */

/** @def BS3_CMN_PROTO_INT
 * Internal macro for prototyping all the variations of a common function.
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_CMN_PROTO_STUB, BS3_CMN_PROTO_NOSB
 */
#if ARCH_BITS == 16
# ifndef BS3_USE_ALT_16BIT_TEXT_SEG
#  define BS3_CMN_PROTO_INT(a_RetType, a_Name, a_Params) \
    BS3_DECL_NEAR(a_RetType) BS3_CMN_NM(a_Name) a_Params;  \
    BS3_DECL_FAR(a_RetType)  BS3_CMN_FAR_NM(a_Name) a_Params
# else
#  define BS3_CMN_PROTO_INT(a_RetType, a_Name, a_Params) \
    BS3_DECL_FAR(a_RetType)  BS3_CMN_FAR_NM(a_Name) a_Params
# endif
#else
# define BS3_CMN_PROTO_INT(a_RetType, a_Name, a_Params) \
    BS3_DECL_NEAR(a_RetType) BS3_CMN_NM(a_Name) a_Params
#endif

/** @def BS3_CMN_PROTO_STUB
 * Macro for prototyping all the variations of a common function with automatic
 * near -> far stub.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_CMN_PROTO_NOSB
 */
#define BS3_CMN_PROTO_STUB(a_RetType, a_Name, a_Params) BS3_CMN_PROTO_INT(a_RetType, a_Name, a_Params)

/** @def BS3_CMN_PROTO_NOSB
 * Macro for prototyping all the variations of a common function without any
 * near > far stub.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_CMN_PROTO_STUB
 */
#define BS3_CMN_PROTO_NOSB(a_RetType, a_Name, a_Params) BS3_CMN_PROTO_INT(a_RetType, a_Name, a_Params)

/** @def BS3_CMN_PROTO_FARSTUB
 * Macro for prototyping all the variations of a common function with automatic
 * far -> near stub.
 *
 * @param   a_cbParam16 The size of the 16-bit parameter list in bytes.
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_CMN_PROTO_STUB
 */
#define BS3_CMN_PROTO_FARSTUB(a_cbParam16, a_RetType, a_Name, a_Params) BS3_CMN_PROTO_INT(a_RetType, a_Name, a_Params)


/** @def BS3_CMN_DEF
 * Macro for defining a common function.
 *
 * This makes 16-bit common function far, while 32-bit and 64-bit are near.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 */
#if ARCH_BITS == 16
# define BS3_CMN_DEF(a_RetType, a_Name, a_Params) \
    BS3_DECL_FAR(a_RetType)  BS3_CMN_FAR_NM(a_Name) a_Params
#else
# define BS3_CMN_DEF(a_RetType, a_Name, a_Params) \
    BS3_DECL_NEAR(a_RetType) BS3_CMN_NM(a_Name) a_Params
#endif

/** @def BS3_ASSERT
 * Assert that an expression is true.
 *
 * Calls Bs3Panic if false and it's a strict build.  Does nothing in
 * non-strict builds.  */
#ifdef BS3_STRICT
# define BS3_ASSERT(a_Expr) do { if (!!(a_Expr)) { /* likely */ } else { Bs3Panic(); } } while (0) /**< @todo later */
#else
# define BS3_ASSERT(a_Expr) do { } while (0)
#endif

/**
 * Panic, never return.
 *
 * The current implementation will only halt the CPU.
 */
BS3_CMN_PROTO_NOSB(DECL_NO_RETURN(void), Bs3Panic,(void));
#if !defined(BS3_KIT_WITH_NO_RETURN) && defined(__WATCOMC__)
# pragma aux Bs3Panic_c16 __aborts
# pragma aux Bs3Panic_f16 __aborts
# pragma aux Bs3Panic_c32 __aborts
#endif


/**
 * Translate a mode into a string.
 *
 * @returns Pointer to read-only mode name string.
 * @param   bMode       The mode value (BS3_MODE_XXX).
 */
BS3_CMN_PROTO_STUB(const char BS3_FAR *, Bs3GetModeName,(uint8_t bMode));

/**
 * Translate a mode into a short lower case string.
 *
 * @returns Pointer to read-only short mode name string.
 * @param   bMode       The mode value (BS3_MODE_XXX).
 */
BS3_CMN_PROTO_STUB(const char BS3_FAR *, Bs3GetModeNameShortLower,(uint8_t bMode));

/** CPU vendors. */
typedef enum BS3CPUVENDOR
{
    BS3CPUVENDOR_INVALID = 0,
    BS3CPUVENDOR_INTEL,
    BS3CPUVENDOR_AMD,
    BS3CPUVENDOR_VIA,
    BS3CPUVENDOR_CYRIX,
    BS3CPUVENDOR_SHANGHAI,
    BS3CPUVENDOR_HYGON,
    BS3CPUVENDOR_UNKNOWN,
    BS3CPUVENDOR_END
} BS3CPUVENDOR;

/**
 * Tries to detect the CPU vendor.
 *
 * @returns CPU vendor.
 */
BS3_CMN_PROTO_STUB(BS3CPUVENDOR, Bs3GetCpuVendor,(void));

/**
 * Shutdown the system, never returns.
 *
 * This currently only works for VMs.  When running on real systems it will
 * just halt the CPU.
 */
BS3_CMN_PROTO_NOSB(void, Bs3Shutdown,(void));

/**
 * Prints a 32-bit unsigned value as decimal to the screen.
 *
 * @param   uValue      The 32-bit value.
 */
BS3_CMN_PROTO_NOSB(void, Bs3PrintU32,(uint32_t uValue));

/**
 * Prints a 32-bit unsigned value as hex to the screen.
 *
 * @param   uValue      The 32-bit value.
 */
BS3_CMN_PROTO_NOSB(void, Bs3PrintX32,(uint32_t uValue));

/**
 * Formats and prints a string to the screen.
 *
 * See #Bs3StrFormatV for supported format types.
 *
 * @param   pszFormat       The format string.
 * @param   ...             Format arguments.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3Printf,(const char BS3_FAR *pszFormat, ...));

/**
 * Formats and prints a string to the screen, va_list version.
 *
 * See #Bs3StrFormatV for supported format types.
 *
 * @param   pszFormat       The format string.
 * @param   va              Format arguments.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3PrintfV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Prints a string to the screen.
 *
 * @param   pszString       The string to print.
 */
BS3_CMN_PROTO_STUB(void, Bs3PrintStr,(const char BS3_FAR *pszString));

/**
 * Prints a string to the screen.
 *
 * @param   pszString       The string to print.  Any terminator charss will be printed.
 * @param   cchString       The exact number of characters to print.
 */
BS3_CMN_PROTO_NOSB(void, Bs3PrintStrN,(const char BS3_FAR *pszString, size_t cchString));

/**
 * Prints a char to the screen.
 *
 * @param   ch              The character to print.
 */
BS3_CMN_PROTO_NOSB(void, Bs3PrintChr,(char ch));


/**
 * An output function for #Bs3StrFormatV.
 *
 * @returns Number of characters written.
 * @param   ch      The character to write. Zero in the final call.
 * @param   pvUser  User argument supplied to #Bs3StrFormatV.
 */
typedef BS3_DECL_CALLBACK(size_t) FNBS3STRFORMATOUTPUT(char ch, void BS3_FAR *pvUser);
/** Pointer to an output function for #Bs3StrFormatV. */
typedef FNBS3STRFORMATOUTPUT *PFNBS3STRFORMATOUTPUT;

/**
 * Formats a string, sending the output to @a pfnOutput.
 *
 * Supported types:
 *      - %RI8, %RI16, %RI32, %RI64
 *      - %RU8, %RU16, %RU32, %RU64
 *      - %RX8, %RX16, %RX32, %RX64
 *      - %i, %d
 *      - %u
 *      - %x
 *      - %c
 *      - %p (far pointer)
 *      - %s (far pointer)
 *
 * @returns Sum of @a pfnOutput return values.
 * @param   pszFormat   The format string.
 * @param   va          Format arguments.
 * @param   pfnOutput   The output function.
 * @param   pvUser      The user argument for the output function.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3StrFormatV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va,
                                          PFNBS3STRFORMATOUTPUT pfnOutput, void BS3_FAR *pvUser));

/**
 * Formats a string into a buffer.
 *
 * See #Bs3StrFormatV for supported format types.
 *
 * @returns The length of the formatted string (excluding terminator).
 *          This will be higher or equal to @c cbBuf in case of an overflow.
 * @param   pszBuf      The output buffer.
 * @param   cbBuf       The size of the output buffer.
 * @param   pszFormat   The format string.
 * @param   va          Format arguments.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3StrPrintfV,(char BS3_FAR *pszBuf, size_t cbBuf, const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Formats a string into a buffer.
 *
 * See #Bs3StrFormatV for supported format types.
 *
 * @returns The length of the formatted string (excluding terminator).
 *          This will be higher or equal to @c cbBuf in case of an overflow.
 * @param   pszBuf      The output buffer.
 * @param   cbBuf       The size of the output buffer.
 * @param   pszFormat   The format string.
 * @param   ...         Format arguments.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3StrPrintf,(char BS3_FAR *pszBuf, size_t cbBuf, const char BS3_FAR *pszFormat, ...));


/**
 * Finds the length of a zero terminated string.
 *
 * @returns String length in chars/bytes.
 * @param   pszString       The string to examine.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3StrLen,(const char BS3_FAR *pszString));

/**
 * Finds the length of a zero terminated string, but with a max length.
 *
 * @returns String length in chars/bytes, or @a cchMax if no zero-terminator
 *           was found before we reached the limit.
 * @param   pszString       The string to examine.
 * @param   cchMax          The max length to examine.
 */
BS3_CMN_PROTO_STUB(size_t, Bs3StrNLen,(const char BS3_FAR *pszString, size_t cchMax));

/**
 * CRT style unsafe strcpy.
 *
 * @returns pszDst.
 * @param   pszDst          The destination buffer.  Must be large enough to
 *                          hold the source string.
 * @param   pszSrc          The source string.
 */
BS3_CMN_PROTO_STUB(char BS3_FAR *, Bs3StrCpy,(char BS3_FAR *pszDst, const char BS3_FAR *pszSrc));

/**
 * CRT style memcpy.
 *
 * @returns pvDst
 * @param   pvDst           The destination buffer.
 * @param   pvSrc           The source buffer.
 * @param   cbToCopy        The number of bytes to copy.
 * @sa      Bs3MemCopyFlat
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemCpy,(void BS3_FAR *pvDst, const void BS3_FAR *pvSrc, size_t cbToCopy));

/**
 * GNU (?) style mempcpy.
 *
 * @returns pvDst + cbCopy
 * @param   pvDst           The destination buffer.
 * @param   pvSrc           The source buffer.
 * @param   cbToCopy        The number of bytes to copy.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemPCpy,(void BS3_FAR *pvDst, const void BS3_FAR *pvSrc, size_t cbToCopy));

/**
 * CRT style memmove (overlapping buffers is fine).
 *
 * @returns pvDst
 * @param   pvDst           The destination buffer.
 * @param   pvSrc           The source buffer.
 * @param   cbToCopy        The number of bytes to copy.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemMove,(void BS3_FAR *pvDst, const void BS3_FAR *pvSrc, size_t cbToCopy));

/**
 * BSD style bzero.
 *
 * @param   pvDst           The buffer to be zeroed.
 * @param   cbDst           The number of bytes to zero.
 */
BS3_CMN_PROTO_NOSB(void, Bs3MemZero,(void BS3_FAR *pvDst, size_t cbDst));

/**
 * CRT style memset.
 *
 * @param   pvDst           The buffer to be fill.
 * @param   bFiller         The filler byte.
 * @param   cbDst           The number of bytes to fill.
 */
BS3_CMN_PROTO_NOSB(void, Bs3MemSet,(void BS3_FAR *pvDst, uint8_t bFiller, size_t cbDst));

/**
 * CRT style memchr.
 *
 * @param   pvHaystack      The memory to scan for @a bNeedle.
 * @param   bNeedle         The byte to search for.
 * @param   cbHaystack      The amount of memory to search.
 */
BS3_CMN_PROTO_NOSB(void BS3_FAR *, Bs3MemChr,(void const BS3_FAR *pvHaystack, uint8_t bNeedle, size_t cbHaystack));

/**
 * CRT style memcmp.
 *
 * @returns 0 if equal. Negative if the left side is 'smaller' than the right
 *          side, and positive in the other case.
 * @param   pv1             The left hand memory.
 * @param   pv2             The right hand memory.
 * @param   cb              The number of bytes to compare.
 */
BS3_CMN_PROTO_NOSB(int, Bs3MemCmp,(void const BS3_FAR *pv1, void const BS3_FAR *pv2, size_t cb));

BS3_CMN_PROTO_STUB(void, Bs3UInt64Div,(RTUINT64U uDividend, RTUINT64U uDivisor, RTUINT64U BS3_FAR *paQuotientReminder));
BS3_CMN_PROTO_STUB(void, Bs3UInt32Div,(RTUINT32U uDividend, RTUINT32U uDivisor, RTUINT32U BS3_FAR *paQuotientReminder));


/**
 * Converts a protected mode 32-bit far pointer to a 32-bit flat address.
 *
 * @returns 32-bit flat address.
 * @param   off             The segment offset.
 * @param   uSel            The protected mode segment selector.
 */
BS3_CMN_PROTO_STUB(uint32_t, Bs3SelProtFar32ToFlat32,(uint32_t off, uint16_t uSel));

/**
 * Converts a current mode 32-bit far pointer to a 32-bit flat address.
 *
 * @returns 32-bit flat address.
 * @param   off             The segment offset.
 * @param   uSel            The current mode segment selector.
 */
BS3_CMN_PROTO_STUB(uint32_t, Bs3SelFar32ToFlat32,(uint32_t off, uint16_t uSel));

/**
 * Wrapper around Bs3SelFar32ToFlat32 that makes it easier to use in tight
 * assembly spots.
 *
 * @returns 32-bit flat address.
 * @param   off             The segment offset.
 * @param   uSel            The current mode segment selector.
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(6, uint32_t, Bs3SelFar32ToFlat32NoClobber,(uint32_t off, uint16_t uSel));

/**
 * Converts a real mode code segment to a protected mode code segment selector.
 *
 * @returns protected mode segment selector.
 * @param   uRealSeg        Real mode code segment.
 * @remarks All register are preserved, except return and parameter.
 */
BS3_CMN_PROTO_NOSB(uint16_t, Bs3SelRealModeCodeToProtMode,(uint16_t uRealSeg));

/**
 * Converts a real mode code segment to a protected mode code segment selector.
 *
 * @returns protected mode segment selector.
 * @param   uProtSel        Real mode code segment.
 * @remarks All register are preserved, except return and parameter.
 */
BS3_CMN_PROTO_NOSB(uint16_t, Bs3SelProtModeCodeToRealMode,(uint16_t uProtSel));

/**
 * Converts a flat code address to a real mode segment and offset.
 *
 * @returns Far real mode address (high 16-bit is segment, low is offset).
 * @param   uFlatAddr       Flat code address.
 * @remarks All register are preserved, except return and parameter.
 */
BS3_CMN_PROTO_NOSB(uint32_t, Bs3SelFlatCodeToRealMode,(uint32_t uFlatAddr));

/**
 * Converts a flat code address to a protected mode 16-bit far pointer (ring-0).
 *
 * @returns Far 16-bit protected mode address (high 16-bit is segment selector,
 *          low is segment offset).
 * @param   uFlatAddr       Flat code address.
 * @remarks All register are preserved, except return and parameter.
 */
BS3_CMN_PROTO_NOSB(uint32_t, Bs3SelFlatCodeToProtFar16,(uint32_t uFlatAddr));

/**
 * Converts a far 16:16 real mode (code) address to a flat address.
 *
 * @returns 32-bit flat address.
 * @param   uFar1616        Far real mode address (high 16-bit is segment, low
 *                          is offset).
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 * @remarks Exactly the same as Bs3SelRealModeDataToFlat, except for param.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelRealModeCodeToFlat,(PFNBS3FARADDRCONV uFar1616));

/**
 * Converts a far 16:16 real mode (code) address to a 16-bit protected mode
 * address.
 *
 * This is mainly for converting X0TEXT16 and X1TEXT16 linker pointers to
 * protected mode ones.
 *
 * @returns Protected mode function pointer.
 * @param   uFar1616        Far real mode address (high 16-bit is segment, low
 *                          is offset).  In 16-bit code, this is also the same
 *                          as a linker address.
 */
BS3_CMN_PROTO_FARSTUB(4, PFNBS3FARADDRCONV, Bs3SelRealModeCodeToProtFar16,(PFNBS3FARADDRCONV uFar1616));

/**
 * Converts a flat data address to a real mode segment and offset.
 *
 * @returns Far real mode address (high 16-bit is segment, low is offset)
 * @param   uFlatAddr       Flat code address.
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelFlatDataToRealMode,(uint32_t uFlatAddr));

/**
 * Converts a flat data address to a real mode segment and offset.
 *
 * @returns Far 16-bit protected mode address (high 16-bit is segment selector,
 *          low is segment offset).
 * @param   uFlatAddr       Flat code address.
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelFlatDataToProtFar16,(uint32_t uFlatAddr));

/**
 * Converts a far 16:16 data address to a real mode segment and offset.
 *
 * @returns Far real mode address (high 16-bit is segment, low is offset)
 * @param   uFar1616        Far 16-bit protected mode address (high 16-bit is
 *                          segment selector, low is segment offset).
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelProtFar16DataToRealMode,(uint32_t uFar1616));

/**
 * Converts a far 16:16 real mode address to a 16-bit protected mode address.
 *
 * @returns Far real mode address (high 16-bit is segment, low is offset)
 * @param   uFar1616        Far real mode address (high 16-bit is segment, low
 *                          is offset).
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelRealModeDataToProtFar16,(uint32_t uFar1616));

/**
 * Converts a far 16:16 data address to a flat 32-bit address.
 *
 * @returns 32-bit flat address.
 * @param   uFar1616        Far 16-bit protected mode address (high 16-bit is
 *                          segment selector, low is segment offset).
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelProtFar16DataToFlat,(uint32_t uFar1616));

/**
 * Converts a far 16:16 real mode address to a flat address.
 *
 * @returns 32-bit flat address.
 * @param   uFar1616        Far real mode address (high 16-bit is segment, low
 *                          is offset).
 * @remarks All register are preserved, except return.
 * @remarks No 20h scratch space required in 64-bit mode.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelRealModeDataToFlat,(uint32_t uFar1616));

/**
 * Converts a link-time data pointer to a current context pointer.
 *
 * @returns Converted pointer.
 * @param   pvLnkPtr    The pointer the linker produced.
 */
BS3_CMN_PROTO_FARSTUB(4, void BS3_FAR *, Bs3SelLnkPtrToCurPtr,(void BS3_FAR *pvLnkPtr));

/**
 * Converts a link-time data pointer to a flat address.
 *
 * @returns 32-bit flag address.
 * @param   pvLnkPtr    The pointer the linker produced.
 */
BS3_CMN_PROTO_FARSTUB(4, uint32_t, Bs3SelLnkPtrToFlat,(void BS3_FAR *pvLnkPtr));

/**
 * Converts a link-time code pointer to a current context pointer.
 *
 * @returns Converted function pointer.
 * @param   pfnLnkPtr   The function pointer the linker produced.
 */
BS3_CMN_PROTO_FARSTUB(4, FPFNBS3FAR, Bs3SelLnkCodePtrToCurPtr,(FPFNBS3FAR pfnLnkPtr));

/**
 * Gets a flat address from a working poitner.
 *
 * @returns flat address (32-bit or 64-bit).
 * @param   pv          Current context pointer.
 */
DECLINLINE(RTCCUINTXREG) Bs3SelPtrToFlat(void BS3_FAR *pv)
{
#if ARCH_BITS == 16
    return BS3_CMN_FN_NM(Bs3SelFar32ToFlat32)(BS3_FP_OFF(pv), BS3_FP_SEG(pv));
#else
    return (uintptr_t)pv;
#endif
}

/**
 * Sets up a 16-bit read-write data selector with ring-3 access and 64KB limit.
 *
 * @param   pDesc       Pointer to the descriptor table entry.
 * @param   uBaseAddr   The base address of the descriptor.
 */
BS3_CMN_PROTO_STUB(void, Bs3SelSetup16BitData,(X86DESC BS3_FAR *pDesc, uint32_t uBaseAddr));

/**
 * Sets up a 16-bit execute-read selector with a 64KB limit.
 *
 * @param   pDesc       Pointer to the descriptor table entry.
 * @param   uBaseAddr   The base address of the descriptor.
 * @param   bDpl        The descriptor privilege level.
 */
BS3_CMN_PROTO_STUB(void, Bs3SelSetup16BitCode,(X86DESC BS3_FAR *pDesc, uint32_t uBaseAddr, uint8_t bDpl));

/**
 * Sets up a 32-bit execute-read selector with a user specified limit.
 *
 * @param   pDesc       Pointer to the descriptor table entry.
 * @param   uBaseAddr   The base address of the descriptor.
 * @param   uLimit      The limit. (This is included here and not in the 16-bit
 *                      functions because we're more likely to want to set it
 *                      than for 16-bit selectors.)
 * @param   bDpl        The descriptor privilege level.
 */
BS3_CMN_PROTO_STUB(void, Bs3SelSetup32BitCode,(X86DESC BS3_FAR *pDesc, uint32_t uBaseAddr, uint32_t uLimit, uint8_t bDpl));

/**
 * Sets up a 16-bit or 32-bit gate descriptor.
 *
 * This can be used both for GDT/LDT and IDT.
 *
 * @param   pDesc       Pointer to the descriptor table entry.
 * @param   bType       The gate type.
 * @param   bDpl        The gate DPL.
 * @param   uSel        The gate selector value.
 * @param   off         The gate IP/EIP value.
 * @param   cParams     Number of parameters to copy if call-gate.
 */
BS3_CMN_PROTO_STUB(void, Bs3SelSetupGate,(X86DESC BS3_FAR *pDesc, uint8_t bType, uint8_t bDpl,
                                          uint16_t uSel, uint32_t off, uint8_t cParams));

/**
 * Sets up a 64-bit gate descriptor.
 *
 * This can be used both for GDT/LDT and IDT.
 *
 * @param   pDescPair   Pointer to the _two_ descriptor table entries.
 * @param   bType       The gate type.
 * @param   bDpl        The gate DPL.
 * @param   uSel        The gate selector value.
 * @param   off         The gate IP/EIP value.
 */
BS3_CMN_PROTO_STUB(void, Bs3SelSetupGate64,(X86DESC BS3_FAR *pDescPair, uint8_t bType, uint8_t bDpl, uint16_t uSel, uint64_t off));


/**
 * Slab control structure list head.
 *
 * The slabs on the list must all have the same chunk size.
 */
typedef struct BS3SLABHEAD
{
    /** Pointer to the first slab. */
    BS3_XPTR_MEMBER(struct BS3SLABCTL, pFirst);
    /** The allocation chunk size. */
    uint16_t                        cbChunk;
    /** Number of slabs in the list. */
    uint16_t                        cSlabs;
    /** Number of chunks in the list. */
    uint32_t                        cChunks;
    /** Number of free chunks. */
    uint32_t                        cFreeChunks;
} BS3SLABHEAD;
AssertCompileSize(BS3SLABHEAD, 16);
/** Pointer to a slab list head. */
typedef BS3SLABHEAD BS3_FAR *PBS3SLABHEAD;

/**
 * Allocation slab control structure.
 *
 * This may live at the start of the slab for 4KB slabs, while in a separate
 * static location for the larger ones.
 */
typedef struct BS3SLABCTL
{
    /** Pointer to the next slab control structure in this list. */
    BS3_XPTR_MEMBER(struct BS3SLABCTL, pNext);
    /** Pointer to the slab list head. */
    BS3_XPTR_MEMBER(BS3SLABHEAD,    pHead);
    /** The base address of the slab. */
    BS3_XPTR_MEMBER(uint8_t,        pbStart);
    /** Number of chunks in this slab. */
    uint16_t                        cChunks;
    /** Number of currently free chunks. */
    uint16_t                        cFreeChunks;
    /** The chunk size. */
    uint16_t                        cbChunk;
    /** The shift count corresponding to cbChunk.
     * This is for turning a chunk number into a byte offset and vice versa. */
    uint16_t                        cChunkShift;
    /** Bitmap where set bits indicates allocated blocks (variable size,
     * multiple of 4). */
    uint8_t                         bmAllocated[4];
} BS3SLABCTL;
/** Pointer to a bs3kit slab control structure. */
typedef BS3SLABCTL BS3_FAR *PBS3SLABCTL;

/** The chunks must all be in the same 16-bit segment tile. */
#define BS3_SLAB_ALLOC_F_SAME_TILE      UINT16_C(0x0001)

/**
 * Initializes a slab.
 *
 * @param   pSlabCtl        The slab control structure to initialize.
 * @param   cbSlabCtl       The size of the slab control structure.
 * @param   uFlatSlabPtr    The base address of the slab.
 * @param   cbSlab          The size of the slab.
 * @param   cbChunk         The chunk size.
 */
BS3_CMN_PROTO_STUB(void, Bs3SlabInit,(PBS3SLABCTL pSlabCtl, size_t cbSlabCtl, uint32_t uFlatSlabPtr,
                                      uint32_t cbSlab, uint16_t cbChunk));

/**
 * Allocates one chunk from a slab.
 *
 * @returns Pointer to a chunk on success, NULL if we're out of chunks.
 * @param   pSlabCtl        The slab control structure to allocate from.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3SlabAlloc,(PBS3SLABCTL pSlabCtl));

/**
 * Allocates one or more chunks from a slab.
 *
 * @returns Pointer to the request number of chunks on success, NULL if we're
 *          out of chunks.
 * @param   pSlabCtl        The slab control structure to allocate from.
 * @param   cChunks         The number of contiguous chunks we want.
 * @param   fFlags          Flags, see BS3_SLAB_ALLOC_F_XXX
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3SlabAllocEx,(PBS3SLABCTL pSlabCtl, uint16_t cChunks, uint16_t fFlags));

/**
 * Allocates a specific range of chunks from a slab.
 *
 * @returns Number of chunks it was possible to allocate in the slab.
 * @retval  0 if the given address isn't in the slab.
 * @retval  UINT16_MAX if one or more of the requested chunks are already in
 *          use, so the request cannot be fulfilled.
 * @param   pSlabCtl        The slab control structure to allocate from.
 * @param   uFlatAddr       The flat address of the range to allocate.
 * @param   cChunks         The number of contiguous chunks we want.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3SlabAllocFixed,(PBS3SLABCTL pSlabCtl, uint32_t uFlatAddr, uint16_t cChunks));

/**
 * Frees one or more chunks from a slab.
 *
 * @returns Number of chunks actually freed.  When correctly used, this will
 *          match the @a cChunks parameter, of course.
 * @param   pSlabCtl        The slab control structure to free from.
 * @param   uFlatChunkPtr   The flat address of the chunks to free.
 * @param   cChunks         The number of contiguous chunks to free.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3SlabFree,(PBS3SLABCTL pSlabCtl, uint32_t uFlatChunkPtr, uint16_t cChunks));


/**
 * Initializes the given slab list head.
 *
 * @param   pHead       The slab list head.
 * @param   cbChunk     The chunk size.
 */
BS3_CMN_PROTO_STUB(void, Bs3SlabListInit,(PBS3SLABHEAD pHead, uint16_t cbChunk));

/**
 * Adds an initialized slab control structure to the list.
 *
 * @param   pHead           The slab list head to add it to.
 * @param   pSlabCtl        The slab control structure to add.
 */
BS3_CMN_PROTO_STUB(void, Bs3SlabListAdd,(PBS3SLABHEAD pHead, PBS3SLABCTL pSlabCtl));

/**
 * Allocates one chunk.
 *
 * @returns Pointer to a chunk on success, NULL if we're out of chunks.
 * @param   pHead           The slab list to allocate from.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3SlabListAlloc,(PBS3SLABHEAD pHead));

/**
 * Allocates one or more chunks.
 *
 * @returns Pointer to the request number of chunks on success, NULL if we're
 *          out of chunks.
 * @param   pHead           The slab list to allocate from.
 * @param   cChunks         The number of contiguous chunks we want.
 * @param   fFlags          Flags, see BS3_SLAB_ALLOC_F_XXX
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3SlabListAllocEx,(PBS3SLABHEAD pHead, uint16_t cChunks, uint16_t fFlags));

/**
 * Frees one or more chunks from a slab list.
 *
 * @param   pHead           The slab list to allocate from.
 * @param   pvChunks        Pointer to the first chunk to free.
 * @param   cChunks         The number of contiguous chunks to free.
 */
BS3_CMN_PROTO_STUB(void, Bs3SlabListFree,(PBS3SLABHEAD pHead, void BS3_FAR *pvChunks, uint16_t cChunks));

/**
 * Allocation addressing constraints.
 */
typedef enum BS3MEMKIND
{
    /** Invalid zero type. */
    BS3MEMKIND_INVALID = 0,
    /** Real mode addressable memory. */
    BS3MEMKIND_REAL,
    /** Memory addressable using the 16-bit protected mode tiling. */
    BS3MEMKIND_TILED,
    /** Memory addressable using 32-bit flat addressing. */
    BS3MEMKIND_FLAT32,
    /** Memory addressable using 64-bit flat addressing. */
    BS3MEMKIND_FLAT64,
    /** End of valid types. */
    BS3MEMKIND_END,
} BS3MEMKIND;

/**
 * Allocates low memory.
 *
 * @returns Pointer to a chunk on success, NULL if we're out of chunks.
 * @param   enmKind     The kind of addressing constraints imposed on the
 *                      allocation.
 * @param   cb          How much to allocate.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemAlloc,(BS3MEMKIND enmKind, size_t cb));

/**
 * Allocates zero'ed memory.
 *
 * @param   enmKind     The kind of addressing constraints imposed on the
 *                      allocation.
 * @param   cb          How much to allocate.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemAllocZ,(BS3MEMKIND enmKind, size_t cb));

/**
 * Frees memory.
 *
 * @returns Pointer to a chunk on success, NULL if we're out of chunks.
 * @param   pv          The memory to free (returned by #Bs3MemAlloc).
 * @param   cb          The size of the allocation.
 */
BS3_CMN_PROTO_STUB(void, Bs3MemFree,(void BS3_FAR *pv, size_t cb));

/**
 * Allocates a page with non-present pages on each side.
 *
 * @returns Pointer to the usable page.  NULL on failure.  Use
 *          Bs3MemGuardedTestPageFree to free the allocation.
 * @param   enmKind     The kind of addressing constraints imposed on the
 *                      allocation.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemGuardedTestPageAlloc,(BS3MEMKIND enmKind));

/**
 * Allocates a page with pages on each side to the @a fPte specification.
 *
 * @returns Pointer to the usable page.  NULL on failure.  Use
 *          Bs3MemGuardedTestPageFree to free the allocation.
 * @param   enmKind     The kind of addressing constraints imposed on the
 *                      allocation.
 * @param   fPte        The page table entry specification for the guard pages.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemGuardedTestPageAllocEx,(BS3MEMKIND enmKind, uint64_t fPte));

/**
 * Frees guarded page allocated by Bs3MemGuardedTestPageAlloc or
 * Bs3MemGuardedTestPageAllocEx.
 *
 * @param   pvGuardedPage   Pointer returned by Bs3MemGuardedTestPageAlloc or
 *                          Bs3MemGuardedTestPageAllocEx.  NULL is ignored.
 */
BS3_CMN_PROTO_STUB(void, Bs3MemGuardedTestPageFree,(void BS3_FAR *pvGuardedPage));

/**
 * Print all heap info.
 */
BS3_CMN_PROTO_STUB(void, Bs3MemPrintInfo, (void));

/** The end RAM address below 4GB (approximately). */
extern uint32_t  g_uBs3EndOfRamBelow4G;
/** The end RAM address above 4GB, zero if no memory above 4GB. */
extern uint64_t  g_uBs3EndOfRamAbove4G;


/**
 * Enables the A20 gate.
 */
BS3_CMN_PROTO_NOSB(void, Bs3A20Enable,(void));

/**
 * Enables the A20 gate via the keyboard controller
 */
BS3_CMN_PROTO_NOSB(void, Bs3A20EnableViaKbd,(void));

/**
 * Enables the A20 gate via the PS/2 control port A.
 */
BS3_CMN_PROTO_NOSB(void, Bs3A20EnableViaPortA,(void));

/**
 * Disables the A20 gate.
 */
BS3_CMN_PROTO_NOSB(void, Bs3A20Disable,(void));

/**
 * Disables the A20 gate via the keyboard controller
 */
BS3_CMN_PROTO_NOSB(void, Bs3A20DisableViaKbd,(void));

/**
 * Disables the A20 gate via the PS/2 control port A.
 */
BS3_CMN_PROTO_NOSB(void, Bs3A20DisableViaPortA,(void));


/**
 * Initializes root page tables for page protected mode (PP16, PP32).
 *
 * @returns IPRT status code.
 * @remarks Must not be called in real-mode!
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingInitRootForPP,(void));

/**
 * Initializes root page tables for PAE page protected mode (PAE16, PAE32).
 *
 * @returns IPRT status code.
 * @remarks The default long mode page tables depends on the PAE ones.
 * @remarks Must not be called in real-mode!
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingInitRootForPAE,(void));

/**
 * Initializes root page tables for long mode (LM16, LM32, LM64).
 *
 * @returns IPRT status code.
 * @remarks The default long mode page tables depends on the PAE ones.
 * @remarks Must not be called in real-mode!
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingInitRootForLM,(void));

/**
 * Maps all RAM above 4GB into the long mode page tables.
 *
 * This requires Bs3PagingInitRootForLM to have been called first.
 *
 * @returns IPRT status code.
 * @retval  VERR_WRONG_ORDER if Bs3PagingInitRootForLM wasn't called.
 * @retval  VINF_ALREADY_INITIALIZED if already called or someone mapped
 *          something else above 4GiB already.
 * @retval  VERR_OUT_OF_RANGE if too much RAM (more than 2^47 bytes).
 * @retval  VERR_NO_MEMORY if no more memory for paging structures.
 * @retval  VERR_UNSUPPORTED_ALIGNMENT if the bs3kit allocator malfunctioned and
 *          didn't give us page aligned memory as it should.
 *
 * @param   puFailurePoint      Where to return the address where we encountered
 *                              a failure.  Optional.
 *
 * @remarks Must be called in 32-bit or 64-bit mode as paging structures will be
 *          allocated using BS3MEMKIND_FLAT32, as there might not be sufficient
 *          BS3MEMKIND_TILED memory around.  (Also, too it's simply too much of
 *          a bother to deal with 16-bit for something that's long-mode only.)
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingMapRamAbove4GForLM,(uint64_t *puFailurePoint));

/**
 * Modifies the page table protection of an address range.
 *
 * This only works on the lowest level of the page tables in the current mode.
 *
 * Since we generally use the largest pages available when setting up the
 * initial page tables, this function will usually have to allocate and create
 * more tables.  This may fail if we're low on memory.
 *
 * @returns IPRT status code.
 * @param   uFlat       The flat address of the first page in the range (rounded
 *                      down nearest page boundrary).
 * @param   cb          The range size from @a pv (rounded up to nearest page boundrary).
 * @param   fSet        Mask of zero or more X86_PTE_XXX values to set for the range.
 * @param   fClear      Mask of zero or more X86_PTE_XXX values to clear for the range.
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingProtect,(uint64_t uFlat, uint64_t cb, uint64_t fSet, uint64_t fClear));

/**
 * Modifies the page table protection of an address range.
 *
 * This only works on the lowest level of the page tables in the current mode.
 *
 * Since we generally use the largest pages available when setting up the
 * initial page tables, this function will usually have to allocate and create
 * more tables.  This may fail if we're low on memory.
 *
 * @returns IPRT status code.
 * @param   pv          The address of the first page in the range (rounded
 *                      down nearest page boundrary).
 * @param   cb          The range size from @a pv (rounded up to nearest page boundrary).
 * @param   fSet        Mask of zero or more X86_PTE_XXX values to set for the range.
 * @param   fClear      Mask of zero or more X86_PTE_XXX values to clear for the range.
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingProtectPtr,(void BS3_FAR *pv, size_t cb, uint64_t fSet, uint64_t fClear));

/**
 * Aliases (maps) one or more contiguous physical pages to a virtual range.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_PARAMETER if we're in legacy paging mode and @a uDst or
 *          @a uPhysToAlias are not compatible with legacy paging.
 * @retval  VERR_OUT_OF_RANGE if we cannot traverse the page tables in this mode
 *          (typically real mode or v86, maybe 16-bit PE).
 * @retval  VERR_NO_MEMORY if we cannot allocate page tables for splitting up
 *          the necessary large pages.  No aliasing was performed.
 *
 * @param   uDst                The virtual address to map it at. Rounded down
 *                              to the nearest page (@a cbHowMuch is adjusted
 *                              up).
 * @param   uPhysToAlias        The physical address of the first page in the
 *                              (contiguous) range to map.  Chopped down to
 *                              nearest page boundrary (@a cbHowMuch is not
 *                              adjusted).
 * @param   cbHowMuch           How much to map. Rounded up to nearest page.
 * @param   fPte                The PTE flags.
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingAlias,(uint64_t uDst, uint64_t uPhysToAlias, uint32_t cbHowMuch, uint64_t fPte));

/**
 * Unaliases memory, i.e. restores the 1:1 mapping.
 *
 * @returns VBox status code.  Cannot fail if @a uDst and @a cbHowMuch specify
 *          the range of a successful Bs3PagingAlias call, however it may run
 *          out of memory if it's breaking new ground.
 *
 * @param   uDst                The virtual address to restore to 1:1 mapping.
 *                              Rounded down to the nearest page (@a cbHowMuch
 *                              is adjusted up).
 * @param   cbHowMuch           How much to restore. Rounded up to nearest page.
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingUnalias,(uint64_t uDst, uint32_t cbHowMuch));

/**
 * Get the pointer to the PTE for the given address.
 *
 * @returns Pointer to the PTE.
 * @param   uFlat               The flat address of the page which PTE we want.
 * @param   prc                 Where to return additional error info. Optional.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3PagingGetPte,(uint64_t uFlat, int *prc));

/**
 * Paging information for an address.
 */
typedef struct BS3PAGINGINFO4ADDR
{
    /** The depth of the system's paging mode.
     * This is always 2 for legacy, 3 for PAE and 4 for long mode. */
    uint8_t             cEntries;
    /** The size of the page structures (the entires). */
    uint8_t             cbEntry;
    /** Flags defined for future fun, currently zero. */
    uint16_t            fFlags;
    /** Union display different view on the entry pointers. */
    union
    {
        /** Pointer to the page structure entries, starting with the PTE as 0.
         * If large pages are involved, the first entry will be NULL (first two if 1GB
         * page).  Same if the address is invalid on a higher level. */
        uint8_t BS3_FAR    *apbEntries[4];
        /** Alternative view for legacy mode. */
        struct
        {
            X86PTE BS3_FAR *pPte;
            X86PDE BS3_FAR *pPde;
            void           *pvUnused2;
            void           *pvUnused3;
        } Legacy;
        /** Alternative view for PAE and Long mode. */
        struct
        {
            X86PTEPAE BS3_FAR *pPte;
            X86PDEPAE BS3_FAR *pPde;
            X86PDPE   BS3_FAR *pPdpe;
            X86PML4E  BS3_FAR *pPml4e;
        } Pae;
    } u;
} BS3PAGINGINFO4ADDR;
/** Pointer to paging information for and address.   */
typedef BS3PAGINGINFO4ADDR BS3_FAR *PBS3PAGINGINFO4ADDR;

/**
 * Queries paging information about the given virtual address.
 *
 * @returns VBox status code.
 * @param   uFlat               The flat address to query information about.
 * @param   pPgInfo             Where to return the information.
 */
BS3_CMN_PROTO_STUB(int, Bs3PagingQueryAddressInfo,(uint64_t uFlat, PBS3PAGINGINFO4ADDR pPgInfo));


/** The physical / flat address of the buffer backing the canonical traps.
 * This buffer is spread equally on each side of the 64-bit non-canonical
 * address divide.  Non-64-bit code can use this to setup trick shots and
 * inspect their results. */
extern uint32_t g_uBs3PagingCanonicalTrapsAddr;
/** The size of the buffer at g_uPagingCanonicalTraps (both sides). */
extern uint16_t g_cbBs3PagingCanonicalTraps;
/** The size of one trap buffer (low or high).
 * This is g_cbBs3PagingCanonicalTraps divided by two. */
extern uint16_t g_cbBs3PagingOneCanonicalTrap;

/**
 * Sets up the 64-bit canonical address space trap buffers, if neceessary.
 *
 * @returns Pointer to the buffers (i.e. the first page of the low one) on
 *          success.  NULL on failure.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3PagingSetupCanonicalTraps,(void));


/**
 * Call 16-bit prot mode function from v8086 mode.
 *
 * This switches from v8086 mode to 16-bit protected mode (code) and executes
 * @a fpfnCall with @a cbParams bytes of parameters pushed on the stack.
 * Afterwards it switches back to v8086 mode and returns a 16-bit status code.
 *
 * @returns 16-bit status code if the function returned anything.
 * @param   fpfnCall        Far real mode pointer to the function to call.
 * @param   cbParams        The size of the parameter list, in bytes.
 * @param   ...             The parameters.
 * @sa Bs3SwitchTo32BitAndCallC
 */
BS3_CMN_PROTO_STUB(int, Bs3SwitchFromV86To16BitAndCallC,(FPFNBS3FAR fpfnCall, unsigned cbParams, ...));


/**
 * BS3 integer register.
 */
typedef union BS3REG
{
    /** 8-bit unsigned integer. */
    uint8_t     u8;
    /** 16-bit unsigned integer. */
    uint16_t    u16;
    /** 32-bit unsigned integer. */
    uint32_t    u32;
    /** 64-bit unsigned integer. */
    uint64_t    u64;
    /** Full unsigned integer. */
    uint64_t    u;
    /** High/low byte view. */
    struct
    {
        uint8_t bLo;
        uint8_t bHi;
    } b;
    /** 8-bit view. */
    uint8_t     au8[8];
    /** 16-bit view. */
    uint16_t    au16[4];
    /** 32-bit view. */
    uint32_t    au32[2];
    /** 64-bit view. */
    uint64_t    au64[1];
    /** Unsigned integer, depending on compiler context.
     * This generally follows ARCH_BITS. */
    RTCCUINTREG  uCcReg;
    /** Extended unsigned integer, depending on compiler context.
     * This is 32-bit in 16-bit and 32-bit compiler contexts, and 64-bit in
     * 64-bit. */
    RTCCUINTXREG uCcXReg;
} BS3REG;
/** Pointer to an integer register. */
typedef BS3REG BS3_FAR *PBS3REG;
/** Pointer to a const integer register. */
typedef BS3REG const BS3_FAR *PCBS3REG;

/**
 * Register context (without FPU).
 */
typedef struct BS3REGCTX
{
    BS3REG      rax;                    /**< 0x00  */
    BS3REG      rcx;                    /**< 0x08  */
    BS3REG      rdx;                    /**< 0x10  */
    BS3REG      rbx;                    /**< 0x18  */
    BS3REG      rsp;                    /**< 0x20  */
    BS3REG      rbp;                    /**< 0x28  */
    BS3REG      rsi;                    /**< 0x30  */
    BS3REG      rdi;                    /**< 0x38  */
    BS3REG      r8;                     /**< 0x40  */
    BS3REG      r9;                     /**< 0x48  */
    BS3REG      r10;                    /**< 0x50  */
    BS3REG      r11;                    /**< 0x58  */
    BS3REG      r12;                    /**< 0x60  */
    BS3REG      r13;                    /**< 0x68  */
    BS3REG      r14;                    /**< 0x70  */
    BS3REG      r15;                    /**< 0x78  */
    BS3REG      rflags;                 /**< 0x80  */
    BS3REG      rip;                    /**< 0x88  */
    uint16_t    cs;                     /**< 0x90  */
    uint16_t    ds;                     /**< 0x92  */
    uint16_t    es;                     /**< 0x94  */
    uint16_t    fs;                     /**< 0x96  */
    uint16_t    gs;                     /**< 0x98  */
    uint16_t    ss;                     /**< 0x9a  */
    uint16_t    tr;                     /**< 0x9c  */
    uint16_t    ldtr;                   /**< 0x9e  */
    uint8_t     bMode;                  /**< 0xa0:  BS3_MODE_XXX. */
    uint8_t     bCpl;                   /**< 0xa1: 0-3, 0 is used for real mode. */
    uint8_t     fbFlags;                /**< 0xa2: BS3REG_CTX_F_XXX  */
    uint8_t     abPadding[3];           /**< 0xa3  */
    uint16_t    cr2Range;               /**< 0xa6  */
    BS3REG      cr0;                    /**< 0xa8  */
    BS3REG      cr2;                    /**< 0xb0  */
    BS3REG      cr3;                    /**< 0xb8  */
    BS3REG      cr4;                    /**< 0xc0  */
    uint64_t    uUnused;                /**< 0xc8  */
} BS3REGCTX;
AssertCompileSize(BS3REGCTX, 0xd0);
/** Pointer to a register context. */
typedef BS3REGCTX BS3_FAR *PBS3REGCTX;
/** Pointer to a const register context. */
typedef BS3REGCTX const BS3_FAR *PCBS3REGCTX;

/** @name BS3REG_CTX_F_XXX - BS3REGCTX::fbFlags masks.
 * @{ */
/** The CR0 is MSW (only low 16-bit). */
#define BS3REG_CTX_F_NO_CR0_IS_MSW      UINT8_C(0x01)
/** No CR2 and CR3 values.  Not in CPL 0 or CPU too old for CR2 & CR3. */
#define BS3REG_CTX_F_NO_CR2_CR3         UINT8_C(0x02)
/** No CR4 value. The CPU is too old for CR4. */
#define BS3REG_CTX_F_NO_CR4             UINT8_C(0x04)
/** No TR and LDTR values.  Context gathered in real mode or v8086 mode. */
#define BS3REG_CTX_F_NO_TR_LDTR         UINT8_C(0x08)
/** The context doesn't have valid values for AMD64 GPR extensions. */
#define BS3REG_CTX_F_NO_AMD64           UINT8_C(0x10)
/** @} */

/**
 * Saves the current register context.
 *
 * @param   pRegCtx     Where to store the register context.
 */
BS3_CMN_PROTO_NOSB(void, Bs3RegCtxSave,(PBS3REGCTX pRegCtx));

/**
 * Switch to the specified CPU bitcount, reserve additional stack and save the
 * CPU context.
 *
 * This is for writing more flexible test drivers that can test more than the
 * CPU bitcount (16-bit, 32-bit, 64-bit, and virtual 8086) of the driver itself.
 * For instance a 32-bit driver can do V86 and 16-bit testing, thus saving space
 * by avoiding duplicate 16-bit driver code.
 *
 * @param   pRegCtx         Where to store the register context.
 * @param   bBitMode        Bit mode to switch to, BS3_MODE_CODE_XXX.  Only
 *                          BS3_MODE_CODE_MASK is used, other bits are ignored
 *                          to make it possible to pass a full mode value.
 * @param   cbExtraStack    Number of bytes of additional stack to allocate.
 */
BS3_CMN_PROTO_FARSTUB(8, void, Bs3RegCtxSaveEx,(PBS3REGCTX pRegCtx, uint8_t bBitMode, uint16_t cbExtraStack));

/**
 * This is Bs3RegCtxSaveEx with automatic Bs3RegCtxConvertV86ToRm thrown in.
 *
 * This is for simplifying writing 32-bit test drivers that covers real-mode as
 * well as virtual 8086, 16-bit, 32-bit, and 64-bit modes.
 *
 * @param   pRegCtx         Where to store the register context.
 * @param   bMode           The mode to get a context for.  If this isn't
 *                          BS3_MODE_RM, the BS3_MODE_SYS_MASK has to match the
 *                          one of the current mode.
 * @param   cbExtraStack    Number of bytes of additional stack to allocate.
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSaveForMode,(PBS3REGCTX pRegCtx, uint8_t bMode, uint16_t cbExtraStack));

/**
 * Transforms a register context to a different ring.
 *
 * @param   pRegCtx     The register context.
 * @param   bRing       The target ring (0..3).
 *
 * @note    Do _NOT_ call this for creating real mode or v8086 contexts, because
 *          it will always output a protected mode context!
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxConvertToRingX,(PBS3REGCTX pRegCtx, uint8_t bRing));

/**
 * Transforms a V8086 register context to a real mode one.
 *
 * @param   pRegCtx     The register context.
 *
 * @note    Will assert if called on a non-V8086 context.
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxConvertV86ToRm,(PBS3REGCTX pRegCtx));

/**
 * Restores a register context.
 *
 * @param   pRegCtx     The register context to be restored and resumed.
 * @param   fFlags      BS3REGCTXRESTORE_F_XXX.
 *
 * @remarks Will switch to ring-0.
 * @remarks Does not return.
 */
BS3_CMN_PROTO_NOSB(DECL_NO_RETURN(void), Bs3RegCtxRestore,(PCBS3REGCTX pRegCtx, uint16_t fFlags));
#if !defined(BS3_KIT_WITH_NO_RETURN) && defined(__WATCOMC__)
# pragma aux Bs3RegCtxRestore_c16 "_Bs3RegCtxRestore_aborts_c16" __aborts
# pragma aux Bs3RegCtxRestore_f16 "_Bs3RegCtxRestore_aborts_f16" __aborts
# pragma aux Bs3RegCtxRestore_c32 "_Bs3RegCtxRestore_aborts_c32" __aborts
#endif

/** @name Flags for Bs3RegCtxRestore
 * @{ */
/** Skip restoring the CRx registers. */
#define BS3REGCTXRESTORE_F_SKIP_CRX         UINT16_C(0x0001)
/** Sets g_fBs3TrapNoV86Assist. */
#define BS3REGCTXRESTORE_F_NO_V86_ASSIST    UINT16_C(0x0002)
/** @} */

/**
 * Prints the register context.
 *
 * @param   pRegCtx     The register context to be printed.
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxPrint,(PCBS3REGCTX pRegCtx));

/**
 * Sets a GPR and segment register to point at the same location as @a uFlat.
 *
 * @param   pRegCtx     The register context.
 * @param   pGpr        The general purpose register to set (points within
 *                      @a pRegCtx).
 * @param   pSel        The selector register (points within @a pRegCtx).
 * @param   uFlat       Flat location address.
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSetGrpSegFromFlat,(PBS3REGCTX pRegCtx, PBS3REG pGpr, PRTSEL pSel, RTCCUINTXREG uFlat));

/**
 * Sets a GPR and segment register to point at the same location as @a ovPtr.
 *
 * @param   pRegCtx     The register context.
 * @param   pGpr        The general purpose register to set (points within
 *                      @a pRegCtx).
 * @param   pSel        The selector register (points within @a pRegCtx).
 * @param   pvPtr       Current context pointer.
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSetGrpSegFromCurPtr,(PBS3REGCTX pRegCtx, PBS3REG pGpr, PRTSEL pSel, void BS3_FAR *pvPtr));

/**
 * Sets a GPR and DS to point at the same location as @a pvPtr.
 *
 * @param   pRegCtx     The register context.
 * @param   pGpr        The general purpose register to set (points within
 *                      @a pRegCtx).
 * @param   pvPtr       Current context pointer.
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSetGrpDsFromCurPtr,(PBS3REGCTX pRegCtx, PBS3REG pGpr, void BS3_FAR *pvPtr));

/**
 * Sets CS:RIP to point at the same piece of code as @a uFlatCode.
 *
 * @param   pRegCtx     The register context.
 * @param   uFlatCode   Flat code pointer
 * @sa      Bs3RegCtxSetRipCsFromLnkPtr, Bs3RegCtxSetRipCsFromCurPtr
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSetRipCsFromFlat,(PBS3REGCTX pRegCtx, RTCCUINTXREG uFlatCode));

/**
 * Sets CS:RIP to point at the same piece of code as @a pfnCode.
 *
 * The 16-bit edition of this function expects a far 16:16 address as written by
 * the linker (i.e. real mode).
 *
 * @param   pRegCtx     The register context.
 * @param   pfnCode     Pointer to the code. In 32-bit and 64-bit mode this is a
 *                      flat address, while in 16-bit it's a far 16:16 address
 *                      as fixed up by the linker (real mode selector).  This
 *                      address is converted to match the mode of the context.
 * @sa      Bs3RegCtxSetRipCsFromCurPtr, Bs3RegCtxSetRipCsFromFlat
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSetRipCsFromLnkPtr,(PBS3REGCTX pRegCtx, FPFNBS3FAR pfnCode));

/**
 * Sets CS:RIP to point at the same piece of code as @a pfnCode.
 *
 * @param   pRegCtx     The register context.
 * @param   pfnCode     Pointer to the code.  Current mode pointer.
 * @sa      Bs3RegCtxSetRipCsFromLnkPtr, Bs3RegCtxSetRipCsFromFlat
 */
BS3_CMN_PROTO_STUB(void, Bs3RegCtxSetRipCsFromCurPtr,(PBS3REGCTX pRegCtx, FPFNBS3FAR pfnCode));

/**
 * Sets a GPR by number.
 *
 * @return  true if @a iGpr is valid, false if not.
 * @param   pRegCtx     The register context.
 * @param   iGpr        The GPR number.
 * @param   uValue      The new value.
 * @param   cbValue     The size of the value: 1, 2, 4 or 8.
 */
BS3_CMN_PROTO_STUB(bool, Bs3RegCtxSetGpr,(PBS3REGCTX pRegCtx, uint8_t iGpr, uint64_t uValue, uint8_t cb));

/**
 * Gets the stack pointer as a current context pointer.
 *
 * @return  Pointer to the top of the stack. NULL on failure.
 * @param   pRegCtx     The register context.
 */
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3RegCtxGetRspSsAsCurPtr,(PBS3REGCTX pRegCtx));


/**
 * The method to be used to save and restore the extended context.
 */
typedef enum BS3EXTCTXMETHOD
{
    BS3EXTCTXMETHOD_INVALID = 0,
    BS3EXTCTXMETHOD_ANCIENT,    /**< Ancient fnsave/frstor format. */
    BS3EXTCTXMETHOD_FXSAVE,     /**< fxsave/fxrstor format. */
    BS3EXTCTXMETHOD_XSAVE,      /**< xsave/xrstor format. */
    BS3EXTCTXMETHOD_END,
} BS3EXTCTXMETHOD;


/**
 * Extended CPU context (FPU, SSE, AVX, ++).
 *
 * @remarks Also in bs3kit.inc
 */
typedef struct BS3EXTCTX
{
    /** Dummy/magic value. */
    uint16_t            u16Magic;
    /** The size of the structure. */
    uint16_t            cb;
    /** The method used to save and restore the context (BS3EXTCTXMETHOD). */
    uint8_t             enmMethod;
    uint8_t             abPadding0[3];
    /** Nominal XSAVE_C_XXX. */
    uint64_t            fXcr0Nominal;
    /** The saved XCR0 mask (restored after xrstor).  */
    uint64_t            fXcr0Saved;

    /** Explicit alignment padding. */
    uint8_t             abPadding[64 - 2 - 2 - 1 - 3 - 8 - 8];

    /** The context, variable size (see above).
     * This must be aligned on a 64 byte boundrary. */
    union
    {
        /** fnsave/frstor. */
        X86FPUSTATE     Ancient;
        /** fxsave/fxrstor   */
        X86FXSTATE      x87;
        /** xsave/xrstor   */
        X86XSAVEAREA    x;
        /** Byte array view. */
        uint8_t         ab[sizeof(X86XSAVEAREA)];
    } Ctx;
} BS3EXTCTX;
AssertCompileMemberAlignment(BS3EXTCTX, Ctx, 64);
/** Pointer to an extended CPU context. */
typedef BS3EXTCTX BS3_FAR *PBS3EXTCTX;
/** Pointer to a const extended CPU context. */
typedef BS3EXTCTX const BS3_FAR *PCBS3EXTCTX;

/** Magic value for BS3EXTCTX. */
#define BS3EXTCTX_MAGIC     UINT16_C(0x1980)

/**
 * Allocates and initializes the extended CPU context structure.
 *
 * @returns The new extended CPU context structure.
 * @param   enmKind         The kind of allocation to make.
 */
BS3_CMN_PROTO_STUB(PBS3EXTCTX, Bs3ExtCtxAlloc,(BS3MEMKIND enmKind));

/**
 * Frees an extended CPU context structure.
 *
 * @param   pExtCtx         The extended CPU context (returned by
 *                          Bs3ExtCtxAlloc).
 */
BS3_CMN_PROTO_STUB(void,       Bs3ExtCtxFree,(PBS3EXTCTX pExtCtx));

/**
 * Get the size required for a BS3EXTCTX structure.
 *
 * @returns size in bytes of the whole structure.
 * @param   pfFlags         Where to return flags for Bs3ExtCtxInit.
 * @note    Use Bs3ExtCtxAlloc when possible.
 */
BS3_CMN_PROTO_STUB(uint16_t,   Bs3ExtCtxGetSize,(uint64_t *pfFlags));

/**
 * Initializes the extended CPU context structure.
 * @returns pExtCtx
 * @param   pExtCtx         The extended CPU context.
 * @param   cbExtCtx        The size of the @a pExtCtx allocation.
 * @param   fFlags          XSAVE_C_XXX flags.
 */
BS3_CMN_PROTO_STUB(PBS3EXTCTX, Bs3ExtCtxInit,(PBS3EXTCTX pExtCtx, uint16_t cbExtCtx, uint64_t fFlags));

/**
 * Saves the extended CPU state to the given structure.
 *
 * @param   pExtCtx         The extended CPU context.
 * @remarks All GPRs preserved.
 */
BS3_CMN_PROTO_FARSTUB(4, void, Bs3ExtCtxSave,(PBS3EXTCTX pExtCtx));

/**
 * Saves the extended CPU state to the given structure, when in long mode this
 * is done from 64-bit mode to capture YMM8 thru YMM15.
 *
 * This is for testing 64-bit code from a 32-bit test driver.
 *
 * @param   pExtCtx         The extended CPU context.
 * @note    Only safe to call from ring-0 at present.
 * @remarks All GPRs preserved.
 * @sa      Bs3ExtCtxRestoreEx
 */
BS3_CMN_PROTO_FARSTUB(4, void, Bs3ExtCtxSaveEx,(PBS3EXTCTX pExtCtx));

/**
 * Restores the extended CPU state from the given structure.
 *
 * @param   pExtCtx         The extended CPU context.
 * @remarks All GPRs preserved.
 */
BS3_CMN_PROTO_FARSTUB(4, void, Bs3ExtCtxRestore,(PCBS3EXTCTX pExtCtx));

/**
 * Restores the extended CPU state from the given structure and in long mode
 * switch to 64-bit mode to do this so YMM8-YMM15 are also loaded.
 *
 * This is for testing 64-bit code from a 32-bit test driver.
 *
 * @param   pExtCtx         The extended CPU context.
 * @note    Only safe to call from ring-0 at present.
 * @remarks All GPRs preserved.
 * @sa      Bs3ExtCtxSaveEx
 */
BS3_CMN_PROTO_FARSTUB(4, void, Bs3ExtCtxRestoreEx,(PCBS3EXTCTX pExtCtx));

/**
 * Copies the state from one context to another.
 *
 * @returns pDst
 * @param   pDst            The destination extended CPU context.
 * @param   pSrc            The source extended CPU context.
 */
BS3_CMN_PROTO_STUB(PBS3EXTCTX, Bs3ExtCtxCopy,(PBS3EXTCTX pDst, PCBS3EXTCTX pSrc));

/**
 * Gets the FCW register value from @a pExtCtx.
 *
 * @returns FCW value.
 * @param   pExtCtx         The extended CPU context.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3ExtCtxGetFcw,(PCBS3EXTCTX pExtCtx));

/**
 * Sets the FCW register value in @a pExtCtx.
 *
 * @param   pExtCtx         The extended CPU context.
 * @param   uValue          The new FCW value.
 */
BS3_CMN_PROTO_STUB(void, Bs3ExtCtxSetFcw,(PBS3EXTCTX pExtCtx, uint16_t uValue));

/**
 * Gets the FSW register value from @a pExtCtx.
 *
 * @returns FSW value.
 * @param   pExtCtx         The extended CPU context.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3ExtCtxGetFsw,(PCBS3EXTCTX pExtCtx));

/**
 * Sets the FSW register value in @a pExtCtx.
 *
 * @param   pExtCtx         The extended CPU context.
 * @param   uValue          The new FSW value.
 */
BS3_CMN_PROTO_STUB(void, Bs3ExtCtxSetFsw,(PBS3EXTCTX pExtCtx, uint16_t uValue));

/**
 * Gets the abridged FTW register value from @a pExtCtx.
 *
 * @returns FTW value.
 * @param   pExtCtx         The extended CPU context.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3ExtCtxGetAbridgedFtw,(PCBS3EXTCTX pExtCtx));

/**
 * Sets the abridged FTW register value in @a pExtCtx.
 *
 * Currently this requires that the state stores teh abridged FTW, no conversion
 * to the two-bit variant will be attempted.
 *
 * @returns true if set successfully, false if not.
 * @param   pExtCtx         The extended CPU context.
 * @param   uValue          The new FTW value.
 */
BS3_CMN_PROTO_STUB(bool, Bs3ExtCtxSetAbridgedFtw,(PBS3EXTCTX pExtCtx, uint16_t uValue));

/**
 * Gets the MXCSR register value from @a pExtCtx.
 *
 * @returns MXCSR value, 0 if not part of context.
 * @param   pExtCtx         The extended CPU context.
 */
BS3_CMN_PROTO_STUB(uint32_t, Bs3ExtCtxGetMxCsr,(PCBS3EXTCTX pExtCtx));

/**
 * Sets the MXCSR register value in @a pExtCtx.
 *
 * @returns true if set, false if not supported by the format.
 * @param   pExtCtx         The extended CPU context.
 * @param   uValue          The new MXCSR value.
 */
BS3_CMN_PROTO_STUB(bool, Bs3ExtCtxSetMxCsr,(PBS3EXTCTX pExtCtx, uint32_t uValue));

/**
 * Gets the MXCSR MASK value from @a pExtCtx.
 *
 * @returns MXCSR MASK value, 0 if not part of context.
 * @param   pExtCtx         The extended CPU context.
 */
BS3_CMN_PROTO_STUB(uint32_t, Bs3ExtCtxGetMxCsrMask,(PCBS3EXTCTX pExtCtx));

/**
 * Sets the MXCSR MASK value in @a pExtCtx.
 *
 * @returns true if set, false if not supported by the format.
 * @param   pExtCtx         The extended CPU context.
 * @param   uValue          The new MXCSR MASK value.
 */
BS3_CMN_PROTO_STUB(bool, Bs3ExtCtxSetMxCsrMask,(PBS3EXTCTX pExtCtx, uint32_t uValue));

/**
 * Gets the value of MM register number @a iReg from @a pExtCtx.
 *
 * @returns The MM register value.
 * @param   pExtCtx         The extended CPU context.
 * @param   iReg            The register to get (0 thru 7).
 */
BS3_CMN_PROTO_STUB(uint64_t, Bs3ExtCtxGetMm,(PCBS3EXTCTX pExtCtx, uint8_t iReg));

/** What to do about the 16-bit above the MM QWORD. */
typedef enum BS3EXTCTXTOPMM
{
    /** Invalid zero value. */
    BS3EXTCTXTOPMM_INVALID = 0,
    /** Set to 0FFFFh like real CPUs typically does when updating an MM register. */
    BS3EXTCTXTOPMM_SET,
    /** Set to zero. */
    BS3EXTCTXTOPMM_ZERO,
    /** Don't change the value, leaving it as-is. */
    BS3EXTCTXTOPMM_AS_IS,
    /** End of valid values. */
    BS3EXTCTXTOPMM_END
} BS3EXTCTXTOPMM;

/**
 * Sets the value of YMM register number @a iReg in @a pExtCtx to @a pValue.
 *
 * @returns True if set, false if not.
 * @param   pExtCtx         The extended CPU context.
 * @param   iReg            The register to set.
 * @param   uValue          The new register value.
 * @param   enmTop          What to do about the 16-bit value above the MM
 *                          QWord.
 */
BS3_CMN_PROTO_STUB(bool, Bs3ExtCtxSetMm,(PBS3EXTCTX pExtCtx, uint8_t iReg, uint64_t uValue, BS3EXTCTXTOPMM enmTop));

/**
 * Gets the value of XMM register number @a iReg from @a pExtCtx.
 *
 * @returns pValue
 * @param   pExtCtx         The extended CPU context.
 * @param   iReg            The register to get.
 * @param   pValue          Where to return the value. Zeroed if the state
 *                          doesn't support SSE or if @a iReg is invalid.
 */
BS3_CMN_PROTO_STUB(PRTUINT128U, Bs3ExtCtxGetXmm,(PCBS3EXTCTX pExtCtx, uint8_t iReg, PRTUINT128U pValue));

/**
 * Sets the value of XMM register number @a iReg in @a pExtCtx to @a pValue.
 *
 * @returns True if set, false if not set (not supported by state format or
 *          invalid iReg).
 * @param   pExtCtx         The extended CPU context.
 * @param   iReg            The register to set.
 * @param   pValue          The new register value.
 */
BS3_CMN_PROTO_STUB(bool, Bs3ExtCtxSetXmm,(PBS3EXTCTX pExtCtx, uint8_t iReg, PCRTUINT128U pValue));

/**
 * Gets the value of YMM register number @a iReg from @a pExtCtx.
 *
 * @returns pValue
 * @param   pExtCtx         The extended CPU context.
 * @param   iReg            The register to get.
 * @param   pValue          Where to return the value.  Parts not in the
 *                          extended state are zeroed.  For absent or invalid
 *                          @a iReg values this is set to zero.
 */
BS3_CMN_PROTO_STUB(PRTUINT256U, Bs3ExtCtxGetYmm,(PCBS3EXTCTX pExtCtx, uint8_t iReg, PRTUINT256U pValue));

/**
 * Sets the value of YMM register number @a iReg in @a pExtCtx to @a pValue.
 *
 * @returns true if set (even if only partially). False if not set (not
 *          supported by state format, unsupported/invalid iReg).
 * @param   pExtCtx         The extended CPU context.
 * @param   iReg            The register to set.
 * @param   pValue          The new register value.
 * @param   cbValue         Number of bytes to take from @a pValue, either 16 or
 *                          32. If 16, the high part will be zeroed when present
 *                          in the state.
 */
BS3_CMN_PROTO_STUB(bool, Bs3ExtCtxSetYmm,(PBS3EXTCTX pExtCtx, uint8_t iReg, PCRTUINT256U pValue, uint8_t cbValue));


/** @name Debug register accessors for V8086 mode (works everwhere).
 * @{  */
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDr0,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDr1,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDr2,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDr3,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDr6,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDr7,(void));

BS3_CMN_PROTO_NOSB(void, Bs3RegSetDr0,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetDr1,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetDr2,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetDr3,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetDr6,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetDr7,(RTCCUINTXREG uValue));

BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetDrX,(uint8_t iReg));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetDrX,(uint8_t iReg, RTCCUINTXREG uValue));
/** @} */


/** @name Control register accessors for V8086 mode (works everwhere).
 * @{  */
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetCr0,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetCr2,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetCr3,(void));
BS3_CMN_PROTO_NOSB(RTCCUINTXREG, Bs3RegGetCr4,(void));
BS3_CMN_PROTO_NOSB(uint16_t, Bs3RegGetTr,(void));
BS3_CMN_PROTO_NOSB(uint16_t, Bs3RegGetLdtr,(void));
BS3_CMN_PROTO_NOSB(uint64_t, Bs3RegGetXcr0,(void));

BS3_CMN_PROTO_NOSB(void, Bs3RegSetCr0,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetCr2,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetCr3,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetCr4,(RTCCUINTXREG uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetTr,(uint16_t uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetLdtr,(uint16_t uValue));
BS3_CMN_PROTO_NOSB(void, Bs3RegSetXcr0,(uint64_t uValue));
/** @} */


/**
 * Trap frame.
 */
typedef struct BS3TRAPFRAME
{
    /** 0x00: Exception/interrupt number. */
    uint8_t     bXcpt;
    /** 0x01: The size of the IRET frame. */
    uint8_t     cbIretFrame;
    /** 0x02: The handler CS. */
    uint16_t    uHandlerCs;
    /** 0x04: The handler SS. */
    uint16_t    uHandlerSs;
    /** 0x06: Explicit alignment. */
    uint16_t    usAlignment;
    /** 0x08: The handler RSP (pointer to the iret frame, skipping ErrCd). */
    uint64_t    uHandlerRsp;
    /** 0x10: The handler RFLAGS value. */
    uint64_t    fHandlerRfl;
    /** 0x18: The error code (if applicable). */
    uint64_t    uErrCd;
    /** 0x20: The register context. */
    BS3REGCTX   Ctx;
} BS3TRAPFRAME;
AssertCompileSize(BS3TRAPFRAME, 0x20 + 0xd0);
/** Pointer to a trap frame. */
typedef BS3TRAPFRAME BS3_FAR *PBS3TRAPFRAME;
/** Pointer to a const trap frame.   */
typedef BS3TRAPFRAME const BS3_FAR *PCBS3TRAPFRAME;


/**
 * Re-initializes the trap handling for the current mode.
 *
 * Useful after a test that messes with the IDT/IVT.
 *
 * @sa      Bs3TrapInit
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapReInit,(void));

/**
 * Initializes real mode and v8086 trap handling.
 *
 * @remarks Does not install RM/V86 trap handling, just initializes the
 *          structures.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapRmV86Init,(void));

/**
 * Initializes real mode and v8086 trap handling, extended version.
 *
 * @param   f386Plus    Set if the CPU is 80386 or later and
 *                      extended registers should be saved.  Once initialized
 *                      with this parameter set to @a true, the effect cannot be
 *                      reversed.
 *
 * @remarks Does not install RM/V86 trap handling, just initializes the
 *          structures.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapRmV86InitEx,(bool f386Plus));

/**
 * Initializes 16-bit (protected mode) trap handling.
 *
 * @remarks Does not install 16-bit trap handling, just initializes the
 *          structures.
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap16Init,(void));

/**
 * Initializes 16-bit (protected mode) trap handling, extended version.
 *
 * @param   f386Plus    Set if the CPU is 80386 or later and
 *                      extended registers should be saved.  Once initialized
 *                      with this parameter set to @a true, the effect cannot be
 *                      reversed.
 *
 * @remarks Does not install 16-bit trap handling, just initializes the
 *          structures.
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap16InitEx,(bool f386Plus));

/**
 * Initializes 32-bit trap handling.
 *
 * @remarks Does not install 32-bit trap handling, just initializes the
 *          structures.
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap32Init,(void));

/**
 * Initializes 64-bit trap handling
 *
 * @remarks Does not install 64-bit trap handling, just initializes the
 *          structures.
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap64Init,(void));

/**
 * Initializes 64-bit trap handling, extended version.
 *
 * @remarks Does not install 64-bit trap handling, just initializes the
 *          structures.
 * @param   fMoreIstUsage   Use the interrupt stacks for more CPU exceptions.
 *                          Default (false) is to only IST1 for the double fault
 *                          handler and the rest uses IST0.
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap64InitEx,(bool fMoreIstUsage));

/**
 * Modifies the real-mode / V86 IVT entry specified by @a iIvt.
 *
 * @param   iIvt        The index of the IDT entry to set.
 * @param   uSeg        The handler real-mode segment.
 * @param   off         The handler offset.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapRmV86SetGate,(uint8_t iIvt, uint16_t uSeg, uint16_t off));

/**
 * Modifies the 16-bit IDT entry (protected mode) specified by @a iIdt.
 *
 * @param   iIdt        The index of the IDT entry to set.
 * @param   bType       The gate type (X86_SEL_TYPE_SYS_XXX).
 * @param   bDpl        The DPL.
 * @param   uSel        The handler selector.
 * @param   off         The handler offset (if applicable).
 * @param   cParams     The parameter count (for call gates).
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap16SetGate,(uint8_t iIdt, uint8_t bType, uint8_t bDpl,
                                           uint16_t uSel, uint16_t off, uint8_t cParams));

/** The address of Bs3Trap16GenericEntries.
 * Bs3Trap16GenericEntries is an array of interrupt/trap/whatever entry
 * points, 8 bytes each, that will create a register frame and call the generic
 * C compatible trap handlers. */
extern uint32_t g_Bs3Trap16GenericEntriesFlatAddr;

/**
 * Modifies the 32-bit IDT entry specified by @a iIdt.
 *
 * @param   iIdt        The index of the IDT entry to set.
 * @param   bType       The gate type (X86_SEL_TYPE_SYS_XXX).
 * @param   bDpl        The DPL.
 * @param   uSel        The handler selector.
 * @param   off         The handler offset (if applicable).
 * @param   cParams     The parameter count (for call gates).
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap32SetGate,(uint8_t iIdt, uint8_t bType, uint8_t bDpl,
                                           uint16_t uSel, uint32_t off, uint8_t cParams));

/** The address of Bs3Trap32GenericEntries.
 * Bs3Trap32GenericEntries is an array of interrupt/trap/whatever entry
 * points, 10 bytes each, that will create a register frame and call the generic
 * C compatible trap handlers. */
extern uint32_t g_Bs3Trap32GenericEntriesFlatAddr;

/**
 * Modifies the 64-bit IDT entry specified by @a iIdt.
 *
 * @param   iIdt        The index of the IDT entry to set.
 * @param   bType       The gate type (X86_SEL_TYPE_SYS_XXX).
 * @param   bDpl        The DPL.
 * @param   uSel        The handler selector.
 * @param   off         The handler offset (if applicable).
 * @param   bIst        The interrupt stack to use.
 */
BS3_CMN_PROTO_STUB(void, Bs3Trap64SetGate,(uint8_t iIdt, uint8_t bType, uint8_t bDpl, uint16_t uSel, uint64_t off, uint8_t bIst));

/** The address of Bs3Trap64GenericEntries.
 * Bs3Trap64GenericEntries is an array of interrupt/trap/whatever entry
 * points, 8 bytes each, that will create a register frame and call the generic
 * C compatible trap handlers. */
extern uint32_t g_Bs3Trap64GenericEntriesFlatAddr;

/**
 * Adjusts the DPL the IDT entry specified by @a iIdt.
 *
 * The change is applied to the 16-bit, 32-bit and 64-bit IDTs.
 *
 * @returns Old DPL (from 64-bit IDT).
 * @param   iIdt        The index of the IDT and IVT entry to set.
 * @param   bDpl        The DPL.
 */
BS3_CMN_PROTO_STUB(uint8_t, Bs3TrapSetDpl,(uint8_t iIdt, uint8_t bDpl));

/**
 * C-style trap handler.
 *
 * The caller will resume the context in @a pTrapFrame upon return.
 *
 * @param   pTrapFrame  The trap frame.  Registers can be modified.
 * @note    The 16-bit versions must be in CGROUP16!
 */
typedef BS3_DECL_NEAR_CALLBACK(void) FNBS3TRAPHANDLER(PBS3TRAPFRAME pTrapFrame);
/** Pointer to a trap handler (current template context). */
typedef FNBS3TRAPHANDLER *PFNBS3TRAPHANDLER;

#if ARCH_BITS == 16
/** @copydoc FNBS3TRAPHANDLER */
typedef FNBS3FAR            FNBS3TRAPHANDLER32;
/** @copydoc FNBS3TRAPHANDLER */
typedef FNBS3FAR            FNBS3TRAPHANDLER64;
#else
/** @copydoc FNBS3TRAPHANDLER */
typedef FNBS3TRAPHANDLER    FNBS3TRAPHANDLER32;
/** @copydoc FNBS3TRAPHANDLER */
typedef FNBS3TRAPHANDLER    FNBS3TRAPHANDLER64;
#endif
/** @copydoc PFNBS3TRAPHANDLER */
typedef FNBS3TRAPHANDLER32 *PFNBS3TRAPHANDLER32;
/** @copydoc PFNBS3TRAPHANDLER */
typedef FNBS3TRAPHANDLER64 *PFNBS3TRAPHANDLER64;


/**
 * C-style trap handler, near 16-bit (CGROUP16).
 *
 * The caller will resume the context in @a pTrapFrame upon return.
 *
 * @param   pTrapFrame  The trap frame.  Registers can be modified.
 */
typedef BS3_DECL_NEAR_CALLBACK(void) FNBS3TRAPHANDLER16(PBS3TRAPFRAME pTrapFrame);
/** Pointer to a trap handler (current template context). */
typedef FNBS3TRAPHANDLER16 *PFNBS3TRAPHANDLER16;

/**
 * C-style trap handler, near 16-bit (CGROUP16).
 *
 * The caller will resume the context in @a pTrapFrame upon return.
 *
 * @param   pTrapFrame  The trap frame.  Registers can be modified.
 */
typedef BS3_DECL_CALLBACK(void) FNBS3TRAPHANDLER3264(PBS3TRAPFRAME pTrapFrame);
/** Pointer to a trap handler (current template context). */
typedef FNBS3TRAPHANDLER3264 *FPFNBS3TRAPHANDLER3264;


/**
 * Sets a trap handler (C/C++/assembly) for the current bitcount.
 *
 * @returns Previous handler.
 * @param   iIdt        The index of the IDT entry to set.
 * @param   pfnHandler  Pointer to the handler.
 * @sa      Bs3TrapSetHandlerEx
 */
BS3_CMN_PROTO_STUB(PFNBS3TRAPHANDLER, Bs3TrapSetHandler,(uint8_t iIdt, PFNBS3TRAPHANDLER pfnHandler));

/**
 * Sets a trap handler (C/C++/assembly) for all the bitcounts.
 *
 * @param   iIdt            The index of the IDT and IVT entry to set.
 * @param   pfnHandler16    Pointer to the 16-bit handler. (Assumes linker addresses.)
 * @param   pfnHandler32    Pointer to the 32-bit handler. (Assumes linker addresses.)
 * @param   pfnHandler64    Pointer to the 64-bit handler. (Assumes linker addresses.)
 * @sa      Bs3TrapSetHandler
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapSetHandlerEx,(uint8_t iIdt, PFNBS3TRAPHANDLER16 pfnHandler16,
                                              PFNBS3TRAPHANDLER32 pfnHandler32, PFNBS3TRAPHANDLER64 pfnHandler64));

/**
 * Default C/C++ trap handler.
 *
 * This will check trap record and panic if no match was found.
 *
 * @param   pTrapFrame      Trap frame of the trap to handle.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapDefaultHandler,(PBS3TRAPFRAME pTrapFrame));

/**
 * Prints the trap frame (to screen).
 * @param   pTrapFrame      Trap frame to print.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapPrintFrame,(PCBS3TRAPFRAME pTrapFrame));

/**
 * Sets up a long jump from a trap handler.
 *
 * The long jump will only be performed once, but will catch any kind of trap,
 * fault, interrupt or irq.
 *
 * @retval  true on the initial call.
 * @retval  false on trap return.
 * @param   pTrapFrame      Where to store the trap information when
 *                          returning @c false.
 * @sa      #Bs3TrapUnsetJmp
 */
BS3_CMN_PROTO_NOSB(DECL_RETURNS_TWICE(bool),Bs3TrapSetJmp,(PBS3TRAPFRAME pTrapFrame));

/**
 * Combination of #Bs3TrapSetJmp and #Bs3RegCtxRestore.
 *
 * @param   pCtxRestore     The context to restore.
 * @param   pTrapFrame      Where to store the trap information.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapSetJmpAndRestore,(PCBS3REGCTX pCtxRestore, PBS3TRAPFRAME pTrapFrame));

/**
 * Variation of Bs3TrapSetJmpAndRestore that includes
 * #Bs3TrapSetJmpAndRestoreInRm and calls is if pCtxRestore is a real mode
 * context and we're not in real mode.
 *
 * This is useful for 32-bit test drivers running via #Bs3TestDoModesByOne using
 * BS3TESTMODEBYONEENTRY_F_REAL_MODE_READY to allow them to test real-mode too.
 *
 * @param   pCtxRestore     The context to restore.
 * @param   pTrapFrame      Where to store the trap information.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapSetJmpAndRestoreWithRm,(PCBS3REGCTX pCtxRestore, PBS3TRAPFRAME pTrapFrame));

/**
 * Combination of #Bs3ExtCtxRestoreEx, #Bs3TrapSetJmp, #Bs3RegCtxRestore and
 * #Bs3ExtCtxSaveEx.
 *
 * @param   pCtxRestore     The context to restore.
 * @param   pExtCtxRestore  The extended context to restore.
 * @param   pTrapFrame      Where to store the trap information.
 * @param   pExtCtxTrap     Where to store the extended context after the trap.
 *                          Note, the saving isn't done from the trap handler,
 *                          but after #Bs3TrapSetJmp returns zero (i.e. for the
 *                          2nd time).
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapSetJmpAndRestoreWithExtCtx,(PCBS3REGCTX pCtxRestore, PCBS3EXTCTX pExtCtxRestore,
                                                            PBS3TRAPFRAME pTrapFrame, PBS3EXTCTX pExtCtxTrap));

/**
 * Variation of Bs3TrapSetJmpAndRestoreWithExtCtx that includes
 * #Bs3TrapSetJmpAndRestoreInRm and calls is if pCtxRestore is a real mode
 * context and we're not in real mode.
 *
 * This is useful for 32-bit test drivers running via #Bs3TestDoModesByOne using
 * BS3TESTMODEBYONEENTRY_F_REAL_MODE_READY to allow them to test real-mode too.
 *
 * @param   pCtxRestore     The context to restore.
 * @param   pExtCtxRestore  The extended context to restore.
 * @param   pTrapFrame      Where to store the trap information.
 * @param   pExtCtxTrap     Where to store the extended context after the trap.
 *                          Note, the saving isn't done from the trap handler,
 *                          but after #Bs3TrapSetJmp returns zero (i.e. for the
 *                          2nd time).
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapSetJmpAndRestoreWithExtCtxAndRm,(PCBS3REGCTX pCtxRestore, PCBS3EXTCTX pExtCtxRestore,
                                                                 PBS3TRAPFRAME pTrapFrame, PBS3EXTCTX pExtCtxTrap));

/**
 * Combination of Bs3SwitchToRM, #Bs3TrapSetJmp and #Bs3RegCtxRestore.
 *
 * @param   pCtxRestore     The context to restore.  Must be real-mode
 *                          addressable.
 * @param   pTrapFrame      Where to store the trap information.  Must be
 *                          real-mode addressable.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapSetJmpAndRestoreInRm,(PCBS3REGCTX pCtxRestore, PBS3TRAPFRAME pTrapFrame));

/**
 * Disables a previous #Bs3TrapSetJmp call.
 */
BS3_CMN_PROTO_STUB(void, Bs3TrapUnsetJmp,(void));

/** Entry point for MSR_K8_LSTAR (64-bit).
 * This hooks into the default Bs3TrapSetJmp logic. */
BS3_CMN_PROTO_NOSB(void, Bs3Syscall64Generic,(void));
/** The 32-bit FLAT address of Bs3Syscall64Generic (for 16-bit code). */
extern uint32_t g_pfnBs3Syscall64GenericFlat;

/** Entry point for MSR_K8_CSTAR (64-bit).
 * This hooks into the default Bs3TrapSetJmp logic. */
BS3_CMN_PROTO_NOSB(void, Bs3Syscall64GenericCompatibility,(void));
/** The 32-bit FLAT address of Bs3Syscall64Generic (for 16-bit code). */
extern uint32_t g_pfnBs3Syscall64GenericCompatibilityFlat;



/**
 * The current test step.
 */
extern uint16_t g_usBs3TestStep;

/** Test repetitions necessary to make sure of engaging native recompilation.
 * @note See iemTbCacheLookup() for the actual constant. */
#define BS3_THRESHOLD_NATIVE_RECOMPILER     18

/** The host configurable BS3_THRESHOLD_NATIVE_RECOMPILER value.
 * The host configuration is read by Bs3TestInit, before that it has the same
 * value as the compiletime constant. */
extern uint16_t g_cBs3ThresholdNativeRecompiler;

/**
 * Equivalent to RTTestCreate + RTTestBanner.
 *
 * @param   pszTest         The test name.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestInit,(const char BS3_FAR *pszTest));


/**
 * Equivalent to RTTestSummaryAndDestroy.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestTerm,(void));

/**
 * Equivalent to RTTestISub.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSub,(const char BS3_FAR *pszSubTest));

/**
 * Equivalent to RTTestISubF.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubF,(const char BS3_FAR *pszFormat, ...));

/**
 * Equivalent to RTTestISubV.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Equivalent to RTTestISubDone.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubDone,(void));

/**
 * Equivalent to RTTestISubSub.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubSub,(const char BS3_FAR *pszSubTest));

/**
 * Equivalent to RTTestISubSubF.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubSubF,(const char BS3_FAR *pszFormat, ...));

/**
 * Equivalent to RTTestISubSubV.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubSubV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Equivalent to RTTestISubSubDone.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSubSubDone,(void));

/**
 * Equivalent to RTTestIValue.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestValue,(const char BS3_FAR *pszName, uint64_t u64Value, uint8_t bUnit));

/**
 * Equivalent to RTTestSubErrorCount.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3TestSubErrorCount,(void));

/**
 * Get nanosecond host timestamp.
 *
 * This only works when testing is enabled and will not work in VMs configured
 * with a 286, 186 or 8086/8088 CPU profile.
 */
BS3_CMN_PROTO_STUB(uint64_t, Bs3TestNow,(void));


/**
 * Queries an unsigned 8-bit configuration value.
 *
 * @returns Value.
 * @param   uCfg        A VMMDEV_TESTING_CFG_XXX value.
 * @param   bDefault    The default value to return if the VMMDev isn't
 *                      available or the query failed.
 */
BS3_CMN_PROTO_STUB(uint8_t, Bs3TestQueryCfgU8,(uint16_t uCfg, uint8_t bDefault));

/**
 * Queries a boolean configuration value.
 *
 * @returns Value.
 * @param   uCfg        A VMMDEV_TESTING_CFG_XXX value.
 * @param   fDefault    The default value to return if the VMMDev isn't
 *                      available or the query failed.
 */
BS3_CMN_PROTO_STUB(bool, Bs3TestQueryCfgBool,(uint16_t uCfg, bool fDefault));

/**
 * Queries an unsigned 16-bit configuration value.
 *
 * @returns Value.
 * @param   uCfg        A VMMDEV_TESTING_CFG_XXX value.
 * @param   uDefault    The default value to return if the VMMDev isn't
 *                      available or the query failed.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3TestQueryCfgU16,(uint16_t uCfg, uint16_t uDefault));

/**
 * Queries an unsigned 32-bit configuration value.
 *
 * @returns Value.
 * @param   uCfg        A VMMDEV_TESTING_CFG_XXX value.
 * @param   uDefault    The default value to return if the VMMDev isn't
 *                      available or the query failed.
 */
BS3_CMN_PROTO_STUB(uint32_t, Bs3TestQueryCfgU32,(uint16_t uCfg, uint32_t uDefault));

/**
 * Equivalent to RTTestIPrintf with RTTESTLVL_ALWAYS.
 *
 * @param   pszFormat   What to print, format string.  Explicit newline char.
 * @param   ...         String format arguments.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestPrintf,(const char BS3_FAR *pszFormat, ...));

/**
 * Equivalent to RTTestIPrintfV with RTTESTLVL_ALWAYS.
 *
 * @param   pszFormat   What to print, format string.  Explicit newline char.
 * @param   va          String format arguments.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestPrintfV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Same as Bs3TestPrintf, except no guest screen echo.
 *
 * @param   pszFormat   What to print, format string.  Explicit newline char.
 * @param   ...         String format arguments.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestHostPrintf,(const char BS3_FAR *pszFormat, ...));

/**
 * Same as Bs3TestPrintfV, except no guest screen echo.
 *
 * @param   pszFormat   What to print, format string.  Explicit newline char.
 * @param   va          String format arguments.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestHostPrintfV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Equivalent to RTTestIFailed.
 * @returns false.
 */
BS3_CMN_PROTO_STUB(bool, Bs3TestFailed,(const char BS3_FAR *pszMessage));

/**
 * Equivalent to RTTestIFailedF.
 * @returns false.
 */
BS3_CMN_PROTO_STUB(bool, Bs3TestFailedF,(const char BS3_FAR *pszFormat, ...));

/**
 * Equivalent to RTTestIFailedV.
 * @returns false.
 */
BS3_CMN_PROTO_STUB(bool, Bs3TestFailedV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Equivalent to RTTestISkipped.
 *
 * @param   pszWhy          Optional reason why it's being skipped.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSkipped,(const char BS3_FAR *pszWhy));

/**
 * Equivalent to RTTestISkippedF.
 *
 * @param   pszFormat       Optional reason why it's being skipped.
 * @param   ...             Format arguments.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSkippedF,(const char BS3_FAR *pszFormat, ...));

/**
 * Equivalent to RTTestISkippedV.
 *
 * @param   pszFormat       Optional reason why it's being skipped.
 * @param   va              Format arguments.
 */
BS3_CMN_PROTO_STUB(void, Bs3TestSkippedV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va));

/**
 * Compares two register contexts, with PC and SP adjustments.
 *
 * Differences will be reported as test failures.
 *
 * @returns true if equal, false if not.
 * @param   pActualCtx      The actual register context.
 * @param   pExpectedCtx    Expected register context.
 * @param   cbPcAdjust      Program counter adjustment (applied to @a pExpectedCtx).
 * @param   cbSpAdjust      Stack pointer adjustment (applied to @a pExpectedCtx).
 * @param   fExtraEfl       Extra EFLAGS to OR into @a pExepctedCtx.
 * @param   pszMode         CPU mode or some other helpful text.
 * @param   idTestStep      Test step identifier.
 */
BS3_CMN_PROTO_STUB(bool, Bs3TestCheckRegCtxEx,(PCBS3REGCTX pActualCtx, PCBS3REGCTX pExpectedCtx, uint16_t cbPcAdjust,
                                               int16_t cbSpAdjust, uint32_t fExtraEfl,
                                               const char BS3_FAR *pszMode, uint16_t idTestStep));

/**
 * Compares two extended register contexts.
 *
 * Differences will be reported as test failures.
 *
 * @returns true if equal, false if not.
 * @param   pActualExtCtx   The actual register context.
 * @param   pExpectedExtCtx Expected register context.
 * @param   fFlags          Reserved, pass 0.
 * @param   pszMode         CPU mode or some other helpful text.
 * @param   idTestStep      Test step identifier.
 */
BS3_CMN_PROTO_STUB(bool, Bs3TestCheckExtCtx,(PCBS3EXTCTX pActualExtCtx, PCBS3EXTCTX pExpectedExtCtx, uint16_t fFlags,
                                             const char BS3_FAR *pszMode, uint16_t idTestStep));

/**
 * Performs the testing for the given mode.
 *
 * This is called with the CPU already switch to that mode.
 *
 * @returns 0 on success or directly Bs3TestFailed calls, non-zero to indicate
 *          where the test when wrong. Special value BS3TESTDOMODE_SKIPPED
 *          should be returned to indicate that the test has been skipped.
 * @param   bMode       The current CPU mode.
 */
typedef BS3_DECL_CALLBACK(uint8_t)  FNBS3TESTDOMODE(uint8_t bMode);
/** Pointer (far) to a test (for 32-bit and 64-bit code, will be flatten). */
typedef FNBS3TESTDOMODE            *PFNBS3TESTDOMODE;

/** Special FNBS3TESTDOMODE return code for indicating a skipped mode test.  */
#define BS3TESTDOMODE_SKIPPED       UINT8_MAX

/**
 * Mode sub-test entry.
 *
 * This can only be passed around to functions with the same bit count, as it
 * contains function pointers.  In 16-bit mode, the 16-bit pointers are near and
 * implies BS3TEXT16, whereas the 32-bit and 64-bit pointers are far real mode
 * addresses that will be converted to flat address prior to calling them.
 * Similarly, in 32-bit and 64-bit the addresses are all flat and the 16-bit
 * ones will be converted to BS3TEXT16 based addresses prior to calling.
 */
typedef struct BS3TESTMODEENTRY
{
    /** The sub-test name to be passed to Bs3TestSub if not NULL. */
    const char * BS3_FAR    pszSubTest;

    PFNBS3TESTDOMODE        pfnDoRM;

    PFNBS3TESTDOMODE        pfnDoPE16;
    PFNBS3TESTDOMODE        pfnDoPE16_32;
    PFNBS3TESTDOMODE        pfnDoPE16_V86;
    PFNBS3TESTDOMODE        pfnDoPE32;
    PFNBS3TESTDOMODE        pfnDoPE32_16;
    PFNBS3TESTDOMODE        pfnDoPEV86;

    PFNBS3TESTDOMODE        pfnDoPP16;
    PFNBS3TESTDOMODE        pfnDoPP16_32;
    PFNBS3TESTDOMODE        pfnDoPP16_V86;
    PFNBS3TESTDOMODE        pfnDoPP32;
    PFNBS3TESTDOMODE        pfnDoPP32_16;
    PFNBS3TESTDOMODE        pfnDoPPV86;

    PFNBS3TESTDOMODE        pfnDoPAE16;
    PFNBS3TESTDOMODE        pfnDoPAE16_32;
    PFNBS3TESTDOMODE        pfnDoPAE16_V86;
    PFNBS3TESTDOMODE        pfnDoPAE32;
    PFNBS3TESTDOMODE        pfnDoPAE32_16;
    PFNBS3TESTDOMODE        pfnDoPAEV86;

    PFNBS3TESTDOMODE        pfnDoLM16;
    PFNBS3TESTDOMODE        pfnDoLM32;
    PFNBS3TESTDOMODE        pfnDoLM64;

} BS3TESTMODEENTRY;
/** Pointer to a mode sub-test entry. */
typedef BS3TESTMODEENTRY const *PCBS3TESTMODEENTRY;

/** @def BS3TESTMODEENTRY_CMN
 * Produces a BS3TESTMODEENTRY initializer for common (c16,c32,c64) test
 * functions. */
#define BS3TESTMODEENTRY_CMN(a_szTest, a_BaseNm) \
    {   /*pszSubTest =*/ a_szTest, \
        /*RM*/        RT_CONCAT(a_BaseNm, _c16), \
        /*PE16*/      RT_CONCAT(a_BaseNm, _c16), \
        /*PE16_32*/   RT_CONCAT(a_BaseNm, _c32), \
        /*PE16_V86*/  RT_CONCAT(a_BaseNm, _c16), \
        /*PE32*/      RT_CONCAT(a_BaseNm, _c32), \
        /*PE32_16*/   RT_CONCAT(a_BaseNm, _c16), \
        /*PEV86*/     RT_CONCAT(a_BaseNm, _c16), \
        /*PP16*/      RT_CONCAT(a_BaseNm, _c16), \
        /*PP16_32*/   RT_CONCAT(a_BaseNm, _c32), \
        /*PP16_V86*/  RT_CONCAT(a_BaseNm, _c16), \
        /*PP32*/      RT_CONCAT(a_BaseNm, _c32), \
        /*PP32_16*/   RT_CONCAT(a_BaseNm, _c16), \
        /*PPV86*/     RT_CONCAT(a_BaseNm, _c16), \
        /*PAE16*/     RT_CONCAT(a_BaseNm, _c16), \
        /*PAE16_32*/  RT_CONCAT(a_BaseNm, _c32), \
        /*PAE16_V86*/ RT_CONCAT(a_BaseNm, _c16), \
        /*PAE32*/     RT_CONCAT(a_BaseNm, _c32), \
        /*PAE32_16*/  RT_CONCAT(a_BaseNm, _c16), \
        /*PAEV86*/    RT_CONCAT(a_BaseNm, _c16), \
        /*LM16*/      RT_CONCAT(a_BaseNm, _c16), \
        /*LM32*/      RT_CONCAT(a_BaseNm, _c32), \
        /*LM64*/      RT_CONCAT(a_BaseNm, _c64), \
    }

/** @def BS3TESTMODE_PROTOTYPES_CMN
 * A set of standard protypes to go with #BS3TESTMODEENTRY_CMN. */
#define BS3TESTMODE_PROTOTYPES_CMN(a_BaseNm) \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c16); \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c32); \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c64)

/** @def BS3TESTMODEENTRY_CMN_64
 * Produces a BS3TESTMODEENTRY initializer for common 64-bit test functions. */
#define BS3TESTMODEENTRY_CMN_64(a_szTest, a_BaseNm) \
    {   /*pszSubTest =*/ a_szTest, \
        /*RM*/        NULL, \
        /*PE16*/      NULL, \
        /*PE16_32*/   NULL, \
        /*PE16_V86*/  NULL, \
        /*PE32*/      NULL, \
        /*PE32_16*/   NULL, \
        /*PEV86*/     NULL, \
        /*PP16*/      NULL, \
        /*PP16_32*/   NULL, \
        /*PP16_V86*/  NULL, \
        /*PP32*/      NULL, \
        /*PP32_16*/   NULL, \
        /*PPV86*/     NULL, \
        /*PAE16*/     NULL, \
        /*PAE16_32*/  NULL, \
        /*PAE16_V86*/ NULL, \
        /*PAE32*/     NULL, \
        /*PAE32_16*/  NULL, \
        /*PAEV86*/    NULL, \
        /*LM16*/      NULL, \
        /*LM32*/      NULL, \
        /*LM64*/      RT_CONCAT(a_BaseNm, _c64), \
    }

/** @def BS3TESTMODE_PROTOTYPES_CMN
 * Standard protype to go with #BS3TESTMODEENTRY_CMN_64. */
#define BS3TESTMODE_PROTOTYPES_CMN_64(a_BaseNm) \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c64)

/** @def BS3TESTMODEENTRY_MODE
 * Produces a BS3TESTMODEENTRY initializer for a full set of mode test
 * functions. */
#define BS3TESTMODEENTRY_MODE(a_szTest, a_BaseNm) \
    {   /*pszSubTest =*/ a_szTest, \
        /*RM*/        RT_CONCAT(a_BaseNm, _rm), \
        /*PE16*/      RT_CONCAT(a_BaseNm, _pe16), \
        /*PE16_32*/   RT_CONCAT(a_BaseNm, _pe16_32), \
        /*PE16_V86*/  RT_CONCAT(a_BaseNm, _pe16_v86), \
        /*PE32*/      RT_CONCAT(a_BaseNm, _pe32), \
        /*PE32_16*/   RT_CONCAT(a_BaseNm, _pe32_16), \
        /*PEV86*/     RT_CONCAT(a_BaseNm, _pev86), \
        /*PP16*/      RT_CONCAT(a_BaseNm, _pp16), \
        /*PP16_32*/   RT_CONCAT(a_BaseNm, _pp16_32), \
        /*PP16_V86*/  RT_CONCAT(a_BaseNm, _pp16_v86), \
        /*PP32*/      RT_CONCAT(a_BaseNm, _pp32), \
        /*PP32_16*/   RT_CONCAT(a_BaseNm, _pp32_16), \
        /*PPV86*/     RT_CONCAT(a_BaseNm, _ppv86), \
        /*PAE16*/     RT_CONCAT(a_BaseNm, _pae16), \
        /*PAE16_32*/  RT_CONCAT(a_BaseNm, _pae16_32), \
        /*PAE16_V86*/ RT_CONCAT(a_BaseNm, _pae16_v86), \
        /*PAE32*/     RT_CONCAT(a_BaseNm, _pae32), \
        /*PAE32_16*/  RT_CONCAT(a_BaseNm, _pae32_16), \
        /*PAEV86*/    RT_CONCAT(a_BaseNm, _paev86), \
        /*LM16*/      RT_CONCAT(a_BaseNm, _lm16), \
        /*LM32*/      RT_CONCAT(a_BaseNm, _lm32), \
        /*LM64*/      RT_CONCAT(a_BaseNm, _lm64), \
    }

/** @def BS3TESTMODE_PROTOTYPES_MODE
 * A set of standard protypes to go with #BS3TESTMODEENTRY_MODE. */
#define BS3TESTMODE_PROTOTYPES_MODE(a_BaseNm) \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _rm); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe16_32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe16_v86); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe32_16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pev86); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp16_32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp16_v86); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp32_16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _ppv86); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae16_32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae16_v86); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae32_16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _paev86); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _lm16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _lm32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _lm64)


/**
 * Mode sub-test entry, max bit-count driven
 *
 * This is an alternative to BS3TESTMODEENTRY where a few workers (test drivers)
 * does all the work, using faster 32-bit and 64-bit code where possible.  This
 * avoids executing workers in V8086 mode.  It allows for modifying and checking
 * 64-bit register content when testing LM16 and LM32.
 *
 * The 16-bit workers are only used for real mode and 16-bit protected mode.
 * So, the 16-bit version of the code template can be stripped of anything
 * related to paging and/or v8086, saving code space.
 */
typedef struct BS3TESTMODEBYMAXENTRY
{
    /** The sub-test name to be passed to Bs3TestSub if not NULL. */
    const char * BS3_FAR    pszSubTest;

    PFNBS3TESTDOMODE        pfnDoRM;
    PFNBS3TESTDOMODE        pfnDoPE16;
    PFNBS3TESTDOMODE        pfnDoPE16_32;
    PFNBS3TESTDOMODE        pfnDoPE32;
    PFNBS3TESTDOMODE        pfnDoPP16_32;
    PFNBS3TESTDOMODE        pfnDoPP32;
    PFNBS3TESTDOMODE        pfnDoPAE16_32;
    PFNBS3TESTDOMODE        pfnDoPAE32;
    PFNBS3TESTDOMODE        pfnDoLM64;

    bool                    fDoRM : 1;

    bool                    fDoPE16 : 1;
    bool                    fDoPE16_32 : 1;
    bool                    fDoPE16_V86 : 1;
    bool                    fDoPE32 : 1;
    bool                    fDoPE32_16 : 1;
    bool                    fDoPEV86 : 1;

    bool                    fDoPP16 : 1;
    bool                    fDoPP16_32 : 1;
    bool                    fDoPP16_V86 : 1;
    bool                    fDoPP32 : 1;
    bool                    fDoPP32_16 : 1;
    bool                    fDoPPV86 : 1;

    bool                    fDoPAE16 : 1;
    bool                    fDoPAE16_32 : 1;
    bool                    fDoPAE16_V86 : 1;
    bool                    fDoPAE32 : 1;
    bool                    fDoPAE32_16 : 1;
    bool                    fDoPAEV86 : 1;

    bool                    fDoLM16 : 1;
    bool                    fDoLM32 : 1;
    bool                    fDoLM64 : 1;

} BS3TESTMODEBYMAXENTRY;
/** Pointer to a mode-by-max sub-test entry. */
typedef BS3TESTMODEBYMAXENTRY const *PCBS3TESTMODEBYMAXENTRY;

/** @def BS3TESTMODEBYMAXENTRY_CMN
 * Produces a BS3TESTMODEBYMAXENTRY initializer for common (c16,c32,c64) test
 * functions. */
#define BS3TESTMODEBYMAXENTRY_CMN(a_szTest, a_BaseNm) \
    {   /*pszSubTest =*/    a_szTest, \
        /*RM*/              RT_CONCAT(a_BaseNm, _c16), \
        /*PE16*/            RT_CONCAT(a_BaseNm, _c16), \
        /*PE16_32*/         RT_CONCAT(a_BaseNm, _c32), \
        /*PE32*/            RT_CONCAT(a_BaseNm, _c32), \
        /*PP16_32*/         RT_CONCAT(a_BaseNm, _c32), \
        /*PP32*/            RT_CONCAT(a_BaseNm, _c32), \
        /*PAE16_32*/        RT_CONCAT(a_BaseNm, _c32), \
        /*PAE32*/           RT_CONCAT(a_BaseNm, _c32), \
        /*LM64*/            RT_CONCAT(a_BaseNm, _c64), \
        /*fDoRM*/           true, \
        /*fDoPE16*/         true, \
        /*fDoPE16_32*/      true, \
        /*fDoPE16_V86*/     true, \
        /*fDoPE32*/         true, \
        /*fDoPE32_16*/      true, \
        /*fDoPEV86*/        true, \
        /*fDoPP16*/         true, \
        /*fDoPP16_32*/      true, \
        /*fDoPP16_V86*/     true, \
        /*fDoPP32*/         true, \
        /*fDoPP32_16*/      true, \
        /*fDoPPV86*/        true, \
        /*fDoPAE16*/        true, \
        /*fDoPAE16_32*/     true, \
        /*fDoPAE16_V86*/    true, \
        /*fDoPAE32*/        true, \
        /*fDoPAE32_16*/     true, \
        /*fDoPAEV86*/       true, \
        /*fDoLM16*/         true, \
        /*fDoLM32*/         true, \
        /*fDoLM64*/         true, \
    }

/** @def BS3TESTMODEBYMAX_PROTOTYPES_CMN
 * A set of standard protypes to go with #BS3TESTMODEBYMAXENTRY_CMN. */
#define BS3TESTMODEBYMAX_PROTOTYPES_CMN(a_BaseNm) \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c16); \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c32); \
    FNBS3TESTDOMODE /*BS3_FAR_CODE*/    RT_CONCAT(a_BaseNm, _c64)


/** @def BS3TESTMODEBYMAXENTRY_MODE
 * Produces a BS3TESTMODEBYMAXENTRY initializer for a full set of mode test
 * functions. */
#define BS3TESTMODEBYMAXENTRY_MODE(a_szTest, a_BaseNm) \
    {   /*pszSubTest =*/ a_szTest, \
        /*RM*/              RT_CONCAT(a_BaseNm, _rm), \
        /*PE16*/            RT_CONCAT(a_BaseNm, _pe16), \
        /*PE16_32*/         RT_CONCAT(a_BaseNm, _pe16_32), \
        /*PE32*/            RT_CONCAT(a_BaseNm, _pe32), \
        /*PP16_32*/         RT_CONCAT(a_BaseNm, _pp16_32), \
        /*PP32*/            RT_CONCAT(a_BaseNm, _pp32), \
        /*PAE16_32*/        RT_CONCAT(a_BaseNm, _pae16_32), \
        /*PAE32*/           RT_CONCAT(a_BaseNm, _pae32), \
        /*LM64*/            RT_CONCAT(a_BaseNm, _lm64), \
        /*fDoRM*/           true, \
        /*fDoPE16*/         true, \
        /*fDoPE16_32*/      true, \
        /*fDoPE16_V86*/     true, \
        /*fDoPE32*/         true, \
        /*fDoPE32_16*/      true, \
        /*fDoPEV86*/        true, \
        /*fDoPP16*/         true, \
        /*fDoPP16_32*/      true, \
        /*fDoPP16_V86*/     true, \
        /*fDoPP32*/         true, \
        /*fDoPP32_16*/      true, \
        /*fDoPPV86*/        true, \
        /*fDoPAE16*/        true, \
        /*fDoPAE16_32*/     true, \
        /*fDoPAE16_V86*/    true, \
        /*fDoPAE32*/        true, \
        /*fDoPAE32_16*/     true, \
        /*fDoPAEV86*/       true, \
        /*fDoLM16*/         true, \
        /*fDoLM32*/         true, \
        /*fDoLM64*/         true, \
    }

/** @def BS3TESTMODEBYMAX_PROTOTYPES_MODE
 * A set of standard protypes to go with #BS3TESTMODEBYMAXENTRY_MODE. */
#define BS3TESTMODEBYMAX_PROTOTYPES_MODE(a_BaseNm) \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _rm); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe16); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe16_32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pe32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp16_32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pp32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae16_32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _pae32); \
    FNBS3TESTDOMODE   RT_CONCAT(a_BaseNm, _lm64)


/**
 * One worker drives all modes.
 *
 * This is an alternative to BS3TESTMODEENTRY where one worker, typically
 * 16-bit, does all the test driver work.  It's called repeatedly from all
 * the modes being tested.
 */
typedef struct BS3TESTMODEBYONEENTRY
{
    const char * BS3_FAR    pszSubTest;
    PFNBS3TESTDOMODE        pfnWorker;
    /** BS3TESTMODEBYONEENTRY_F_XXX. */
    uint32_t                fFlags;
} BS3TESTMODEBYONEENTRY;
/** Pointer to a mode-by-one sub-test entry. */
typedef BS3TESTMODEBYONEENTRY const *PCBS3TESTMODEBYONEENTRY;

/** @name BS3TESTMODEBYONEENTRY_F_XXX - flags.
 * @{ */
/** Only test modes that has paging enabled. */
#define BS3TESTMODEBYONEENTRY_F_ONLY_PAGING     RT_BIT_32(0)
/** Minimal mode selection. */
#define BS3TESTMODEBYONEENTRY_F_MINIMAL         RT_BIT_32(1)
/** The 32-bit worker is ready to handle real-mode by mode switching. */
#define BS3TESTMODEBYONEENTRY_F_REAL_MODE_READY RT_BIT_32(2)
/** Skip all v8086 mode tests. */
#define BS3TESTMODEBYONEENTRY_F_SKIP_V8086      RT_BIT_32(3)
/** @} */


/**
 * Sets the full GDTR register.
 *
 * @param   cbLimit     The limit.
 * @param   uBase       The base address - 24, 32 or 64 bit depending on the
 *                      CPU mode.
 */
BS3_CMN_PROTO_NOSB(void, Bs3UtilSetFullGdtr,(uint16_t cbLimit, uint64_t uBase));

/**
 * Sets the full IDTR register.
 *
 * @param   cbLimit     The limit.
 * @param   uBase       The base address - 24, 32 or 64 bit depending on the
 *                      CPU mode.
 */
BS3_CMN_PROTO_NOSB(void, Bs3UtilSetFullIdtr,(uint16_t cbLimit, uint64_t uBase));


/** @} */


/**
 * Initializes all of boot sector kit \#3.
 */
BS3_DECL(void) Bs3InitAll_rm(void);

/**
 * Initializes all of boot sector kit \#3 as well as the high DLLs.
 */
BS3_DECL(void) Bs3InitAllWithHighDlls_rm(void);

/**
 * Initializes the REAL and TILED memory pools.
 *
 * For proper operation on OLDer CPUs, call #Bs3CpuDetect_mmm first.
 */
BS3_DECL_FAR(void) Bs3InitMemory_rm_far(void);

/**
 * Initializes the X0TEXT16 and X1TEXT16 GDT entries.
 */
BS3_DECL_FAR(void) Bs3InitGdt_rm_far(void);

/**
 * Initializes (loads) any high DLLs.
 *
 * @note This cannot be called after the PIC or traps have been initialized!
 */
BS3_DECL_FAR(void) Bs3InitHighDlls_rm_far(void);



/** @defgroup grp_bs3kit_mode   Mode Specific Functions and Data
 *
 * The mode specific functions come in bit count variations and CPU mode
 * variations.  The bs3kit-template-header.h/mac defines the BS3_NM macro to
 * mangle a function or variable name according to the target CPU mode.  In
 * non-templated code, it's common to spell the name out in full.
 *
 * @{
 */


/** @def BS3_MODE_PROTO_INT
 * Internal macro for emitting prototypes for mode functions.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_MODE_PROTO_STUB, BS3_MODE_PROTO_NOSB
 */
#define BS3_MODE_PROTO_INT(a_RetType, a_Name, a_Params) \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_rm)           a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pe16)         a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pe16_32)      a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pe16_v86)     a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pe32)         a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pe32_16)      a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pev86)        a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pp16)         a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pp16_32)      a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pp16_v86)     a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pp32)         a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pp32_16)      a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_ppv86)        a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pae16)        a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pae16_32)     a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pae16_v86)    a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pae32)        a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_pae32_16)     a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_paev86)       a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_lm16)         a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_lm32)         a_Params; \
    BS3_DECL_NEAR(a_RetType) RT_CONCAT(a_Name,_lm64)         a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_rm_far)       a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pe16_far)     a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pe16_v86_far) a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pe32_16_far)  a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pev86_far)    a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pp16_far)     a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pp16_v86_far) a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pp32_16_far)  a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_ppv86_far)    a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pae16_far)    a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pae16_v86_far)a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_pae32_16_far) a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_paev86_far)   a_Params; \
    BS3_DECL_FAR(a_RetType)  RT_CONCAT(a_Name,_lm16_far)     a_Params

/** @def BS3_MODE_PROTO_STUB
 * Macro for prototyping all the variations of a mod function with automatic
 * near -> far stub.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_MODE_PROTO_STUB, BS3_MODE_PROTO_NOSB
 */
#define BS3_MODE_PROTO_STUB(a_RetType, a_Name, a_Params) BS3_MODE_PROTO_INT(a_RetType, a_Name, a_Params)

/** @def BS3_MODE_PROTO_STUB
 * Macro for prototyping all the variations of a mod function without any
 * near -> far stub.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 * @sa      BS3_MODE_PROTO_STUB, BS3_MODE_PROTO_NOSB
 */
#define BS3_MODE_PROTO_NOSB(a_RetType, a_Name, a_Params) BS3_MODE_PROTO_INT(a_RetType, a_Name, a_Params)


/**
 * Macro for reducing typing.
 *
 * Doxygen knows how to expand this, well, kind of.
 *
 * @remarks Variables instantiated in assembly code should define two labels,
 *          with and without leading underscore.  Variables instantiated from
 *          C/C++ code doesn't need to as the object file convert does this for
 *          64-bit object files.
 */
#define BS3_MODE_EXPAND_EXTERN_DATA16(a_VarType, a_VarName, a_Suffix) \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_rm)       a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pe16)     a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pe16_32)  a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pe16_v86) a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pe32)     a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pe32_16)  a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pev86)    a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pp16)     a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pp16_32)  a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pp16_v86) a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pp32)     a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pp32_16)  a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_ppv86)    a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pae16)    a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pae16_32) a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pae16_v86)a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pae32)    a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_pae32_16) a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_paev86)   a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_lm16)     a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_lm32)     a_Suffix; \
    extern a_VarType BS3_FAR_DATA RT_CONCAT(a_VarName,_lm64)     a_Suffix


/** The TMPL_MODE_STR value for each mode.
 * These are all in DATA16 so they can be accessed from any code.  */
BS3_MODE_EXPAND_EXTERN_DATA16(const char, g_szBs3ModeName, []);
/** The TMPL_MODE_LNAME value for each mode.
 * These are all in DATA16 so they can be accessed from any code.  */
BS3_MODE_EXPAND_EXTERN_DATA16(const char, g_szBs3ModeNameShortLower, []);


/**
 * Basic CPU detection.
 *
 * This sets the #g_uBs3CpuDetected global variable to the return value.
 *
 * @returns BS3CPU_XXX value with the BS3CPU_F_CPUID flag set depending on
 *          capabilities.
 */
BS3_MODE_PROTO_NOSB(uint8_t, Bs3CpuDetect,(void));

/** @name BS3CPU_XXX - CPU detected by BS3CpuDetect_c16() and friends.
 * @{ */
#define BS3CPU_8086                 UINT16_C(0x0001)    /**< Both 8086 and 8088. */
#define BS3CPU_V20                  UINT16_C(0x0002)    /**< Both NEC V20, V30 and relatives. */
#define BS3CPU_80186                UINT16_C(0x0003)    /**< Both 80186 and 80188. */
#define BS3CPU_80286                UINT16_C(0x0004)
#define BS3CPU_80386                UINT16_C(0x0005)
#define BS3CPU_80486                UINT16_C(0x0006)
#define BS3CPU_Pentium              UINT16_C(0x0007)
#define BS3CPU_PPro                 UINT16_C(0x0008)
#define BS3CPU_PProOrNewer          UINT16_C(0x0009)
/** CPU type mask.  This is a full byte so it's possible to use byte access
 * without and AND'ing to get the type value. */
#define BS3CPU_TYPE_MASK            UINT16_C(0x00ff)
/** Flag indicating that the CPUID instruction is supported by the CPU. */
#define BS3CPU_F_CPUID              UINT16_C(0x0100)
/** Flag indicating that extend CPUID leaves are available (at least two).   */
#define BS3CPU_F_CPUID_EXT_LEAVES   UINT16_C(0x0200)
/** Flag indicating that the CPU supports PAE. */
#define BS3CPU_F_PAE                UINT16_C(0x0400)
/** Flag indicating that the CPU supports the page size extension (4MB pages). */
#define BS3CPU_F_PSE                UINT16_C(0x0800)
/** Flag indicating that the CPU supports long mode. */
#define BS3CPU_F_LONG_MODE          UINT16_C(0x1000)
/** Flag indicating that the CPU supports NX. */
#define BS3CPU_F_NX                 UINT16_C(0x2000)
/** @} */

/** The return value of #Bs3CpuDetect_mmm. (Initial value is BS3CPU_TYPE_MASK.) */
extern uint16_t g_uBs3CpuDetected;

/**
 * Call 32-bit prot mode C function.
 *
 * This switches to 32-bit mode and calls the 32-bit @a fpfnCall C code with @a
 * cbParams on the stack, then returns in the original mode.  When called in
 * real mode, this will switch to PE32.
 *
 * @returns 32-bit status code if the function returned anything.
 * @param   fpfnCall        Address of the 32-bit C function to call.  When
 *                          called from 16-bit code, this is a far real mode
 *                          function pointer, i.e. as fixed up by the linker.
 *                          In 32-bit and 64-bit code, this is a flat address.
 * @param   cbParams        The size of the parameter list, in bytes.
 * @param   ...             The parameters.
 * @sa      Bs3SwitchFromV86To16BitAndCallC
 *
 * @remarks     WARNING! This probably doesn't work in 64-bit mode yet.
 *                       Only tested for 16-bit real mode.
 */
BS3_MODE_PROTO_STUB(int32_t, Bs3SwitchTo32BitAndCallC,(FPFNBS3FAR fpfnCall, unsigned cbParams, ...));

/**
 * Initializes trap handling for the current system.
 *
 * Calls the appropriate Bs3Trap16Init, Bs3Trap32Init or Bs3Trap64Init function.
 */
BS3_MODE_PROTO_STUB(void, Bs3TrapInit,(void));

/**
 * Special version of memcpy for copying from/to real mode.
 *
 * @returns pvDst
 * @param   uFlatDst        The flat address of the destination buffer.
 * @param   uFlatSrc        The flat address of the source buffer.
 * @param   cbToCopy        The number of bytes to copy.  Max 64KB.
 *
 * @todo    Only work on 386+ at present.   Could be made to work for 286 and
 *          addresses < 16MB if we care...
 */
BS3_MODE_PROTO_STUB(void BS3_FAR *, Bs3MemCopyFlat,(uint32_t uFlatDst, uint32_t uFlatSrc, uint32_t cbToCopy));

/**
 * Executes the array of tests in every possibly mode.
 *
 * @param   paEntries       The mode sub-test entries.
 * @param   cEntries        The number of sub-test entries.
 */
BS3_MODE_PROTO_NOSB(void, Bs3TestDoModes,(PCBS3TESTMODEENTRY paEntries, size_t cEntries));

/**
 * Executes the array of tests in every possibly mode, unified driver.
 *
 * This requires much less code space than Bs3TestDoModes as there is only one
 * instace of each sub-test driver code, instead of 3 (cmn) or 22 (per-mode)
 * copies.
 *
 * @param   paEntries       The mode sub-test-by-one entries.
 * @param   cEntries        The number of sub-test-by-one entries.
 * @param   fFlags          BS3TESTMODEBYONEENTRY_F_XXX.
 */
BS3_MODE_PROTO_NOSB(void, Bs3TestDoModesByOne,(PCBS3TESTMODEBYONEENTRY paEntries, size_t cEntries, uint32_t fFlags));

/**
 * Executes the array of tests in every possibly mode, using the max bit-count
 * worker for each.
 *
 * @param   paEntries       The mode sub-test entries.
 * @param   cEntries        The number of sub-test entries.
 */
BS3_MODE_PROTO_NOSB(void, Bs3TestDoModesByMax,(PCBS3TESTMODEBYMAXENTRY paEntries, size_t cEntries));

/** @} */


/** @defgroup grp_bs3kit_bios_int15     BIOS - int 15h
 * @{ */

/** An INT15E820 data entry. */
typedef struct INT15E820ENTRY
{
    uint64_t    uBaseAddr;
    uint64_t    cbRange;
    /** Memory type this entry describes, see INT15E820_TYPE_XXX. */
    uint32_t    uType;
    /** Optional.   */
    uint32_t    fAcpi3;
} INT15E820ENTRY;
AssertCompileSize(INT15E820ENTRY,24);


/** @name INT15E820_TYPE_XXX - Memory types returned by int 15h function 0xe820.
 * @{ */
#define INT15E820_TYPE_USABLE               1 /**< Usable RAM. */
#define INT15E820_TYPE_RESERVED             2 /**< Reserved by the system, unusable. */
#define INT15E820_TYPE_ACPI_RECLAIMABLE     3 /**< ACPI reclaimable memory, whatever that means. */
#define INT15E820_TYPE_ACPI_NVS             4 /**< ACPI non-volatile storage? */
#define INT15E820_TYPE_BAD                  5 /**< Bad memory, unusable. */
/** @} */


/**
 * Performs an int 15h function 0xe820 call.
 *
 * @returns Success indicator.
 * @param   pEntry              The return buffer.
 * @param   pcbEntry            Input: The size of the buffer (min 20 bytes);
 *                              Output: The size of the returned data.
 * @param   puContinuationValue Where to get and return the continuation value (EBX)
 *                              Set to zero the for the first call.  Returned as zero
 *                              after the last entry.
 */
BS3_MODE_PROTO_STUB(bool, Bs3BiosInt15hE820,(INT15E820ENTRY BS3_FAR *pEntry, uint32_t BS3_FAR *pcbEntry,
                                             uint32_t BS3_FAR *puContinuationValue));

/**
 * Performs an int 15h function 0x88 call.
 *
 * @returns UINT32_MAX on failure, number of KBs above 1MB otherwise.
 */
#if ARCH_BITS != 16 || !defined(BS3_BIOS_INLINE_RM)
BS3_MODE_PROTO_STUB(uint32_t, Bs3BiosInt15h88,(void));
#else
BS3_DECL(uint32_t) Bs3BiosInt15h88(void);
# pragma aux Bs3BiosInt15h88 = \
    ".286" \
    "clc" \
    "mov    ax, 08800h" \
    "int    15h" \
    "jc     failed" \
    "xor    dx, dx" \
    "jmp    done" \
    "failed:" \
    "xor    ax, ax" \
    "dec    ax" \
    "mov    dx, ax" \
    "done:" \
    value [ax dx] \
    modify exact [ax bx cx dx es];
#endif

/** @} */


/** @defgroup grp_bs3kit_kbd    Keyboard
 * @{
 */

/**
 * Waits for the keyboard controller to become ready.
 */
BS3_CMN_PROTO_NOSB(void, Bs3KbdWait,(void));

/**
 * Sends a read command to the keyboard controller and gets the result.
 *
 * The caller is responsible for making sure the keyboard controller is ready
 * for a command (call #Bs3KbdWait if unsure).
 *
 * @returns The value read is returned (in al).
 * @param   bCmd            The read command.
 */
BS3_CMN_PROTO_NOSB(uint8_t, Bs3KbdRead,(uint8_t bCmd));

/**
 * Sends a write command to the keyboard controller and then sends the data.
 *
 * The caller is responsible for making sure the keyboard controller is ready
 * for a command (call #Bs3KbdWait if unsure).
 *
 * @param   bCmd           The write command.
 * @param   bData          The data to write.
 */
BS3_CMN_PROTO_NOSB(void, Bs3KbdWrite,(uint8_t bCmd, uint8_t bData));

/** @} */


/** @defgroup grp_bs3kit_pic    PIC
 * @{
 */

/**
 * Configures the PIC, once only.
 *
 * Subsequent calls to this function will not do anything.
 *
 * The PIC will be programmed to use IDT/IVT vectors 0x70 thru 0x7f, auto
 * end-of-interrupt, and all IRQs masked.  The individual PIC users will have to
 * use #Bs3PicUpdateMask unmask their IRQ once they've got all the handlers
 * installed.
 *
 * @param   fForcedReInit   Force a reinitialization.
 */
BS3_CMN_PROTO_STUB(void, Bs3PicSetup,(bool fForcedReInit));

/**
 * Updates the PIC masks.
 *
 * @returns The new mask - master in low, slave in high byte.
 * @param   fAndMask    Things to keep as-is. Master in low, slave in high byte.
 * @param   fOrMask     Things to start masking. Ditto wrt bytes.
 */
BS3_CMN_PROTO_STUB(uint16_t, Bs3PicUpdateMask,(uint16_t fAndMask, uint16_t fOrMask));

/**
 * Disables all IRQs on the PIC.
 */
BS3_CMN_PROTO_STUB(void, Bs3PicMaskAll,(void));

/** @} */


/** @defgroup grp_bs3kit_pit    PIT
 * @{
 */

/**
 * Sets up the PIT for periodic callback.
 *
 * @param   cHzDesired      The desired Hz.  Zero means max interval length
 *                          (18.2Hz).  Plase check the various PIT globals for
 *                          the actual interval length.
 */
BS3_CMN_PROTO_STUB(void, Bs3PitSetupAndEnablePeriodTimer,(uint16_t cHzDesired));

/**
 * Disables the PIT if active.
 */
BS3_CMN_PROTO_STUB(void, Bs3PitDisable,(void));

/** The RIP/EIP value of where the PIT IRQ handle will return to. */
extern BS3REG volatile      g_Bs3PitIrqRip;
/** Nanoseconds (approx) since last the PIT timer was started. */
extern uint64_t volatile    g_cBs3PitNs;
/** Milliseconds seconds (very approx) since last the PIT timer was started. */
extern uint64_t volatile    g_cBs3PitMs;
/** Number of ticks since last the PIT timer was started.  */
extern uint32_t volatile    g_cBs3PitTicks;
/** The current interval in nanoseconds.
 * This is 0 if not yet started (cleared by Bs3PitDisable). */
extern uint32_t             g_cBs3PitIntervalNs;
/** The current interval in milliseconds (approximately).
 * This is 0 if not yet started (cleared by Bs3PitDisable). */
extern uint16_t             g_cBs3PitIntervalMs;
/** The current PIT frequency (approximately).
 * 0 if not yet started (cleared by Bs3PitDisable; used for checking the
 * state internally). */
extern uint16_t volatile    g_cBs3PitIntervalHz;

/** @} */

/** @defgroup grp_bs3kit_disk   Disk via INT 13h
 * @{
 */

/**
 * Performs a int 13h function AL=08h call to get the driver parameters.
 *
 * @returns 0 on success, non-zero error BIOS code on failure.
 * @param   bDrive          The drive to get parameters for.
 * @param   puMaxCylinder   Where to store the max cylinder value.
 *                          Range: 0 thru *pcMaxCylinder.
 * @param   puMaxHead       Where to store the max head value.
 *                          Range: 0 thru *pcMaxHead.
 * @param   puMaxSector     Where to store the max sector value.
 *                          Range: 1 thru *pcMaxSector.
 */
BS3_MODE_PROTO_STUB(uint8_t, Bs3DiskQueryGeometry,(uint8_t bDrive, uint16_t *puMaxCylinder,
                                                   uint8_t *puMaxHead, uint8_t *puMaxSector));

/**
 * Performs a int 13h function AL=08h call to get the driver parameters.
 *
 * @returns 0 on success, non-zero error BIOS code on failure.
 * @param   bDrive          The drive to read from.
 * @param   uCylinder       The cylinder to start read at (0-max).
 * @param   uHead           The head to start reading at (0-max).
 * @param   uSector         The sector to start reading at (1-max).
 * @param   cSectors        The number of sectors to read (1+).
 * @param   pvBuf           The buffer to read into.  This MUST be addressable
 *                          from real mode!
 */
BS3_MODE_PROTO_STUB(uint8_t, Bs3DiskRead,(uint8_t bDrive, uint16_t uCylinder, uint8_t uHead, uint8_t uSector,
                                          uint8_t cSectors, void RT_FAR *pvBuf));

/** @} */


/** @} */

RT_C_DECLS_END


/*
 * Include default function symbol mangling.
 */
#include "bs3kit-mangling-code.h"

/*
 * Change 16-bit text segment if requested.
 */
#if defined(BS3_USE_ALT_16BIT_TEXT_SEG) && ARCH_BITS == 16 && !defined(BS3_DONT_CHANGE_TEXT_SEG)
# if (defined(BS3_USE_RM_TEXT_SEG) + defined(BS3_USE_X0_TEXT_SEG) + defined(BS3_USE_X1_TEXT_SEG)) != 1
#  error "Cannot set more than one alternative 16-bit text segment!"
# elif defined(BS3_USE_RM_TEXT_SEG)
#  pragma code_seg("BS3RMTEXT16", "BS3CLASS16RMCODE")
# elif defined(BS3_USE_X0_TEXT_SEG)
#  pragma code_seg("BS3X0TEXT16", "BS3CLASS16X0CODE")
# elif defined(BS3_USE_X1_TEXT_SEG)
#  pragma code_seg("BS3X1TEXT16", "BS3CLASS16X1CODE")
# else
#  error "Huh? Which alternative text segment did you want again?"
# endif
#endif

#endif /* !BS3KIT_INCLUDED_bs3kit_h */

