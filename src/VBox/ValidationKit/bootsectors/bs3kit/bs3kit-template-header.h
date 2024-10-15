/* $Id: bs3kit-template-header.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit header for multi-mode code templates.
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

#include "bs3kit.h"

/** @defgroup grp_bs3kit_tmpl       Multi-Mode Code Templates
 * @ingroup grp_bs3kit
 *
 * Multi-mode code templates avoid duplicating code for each of the CPU modes.
 * Instead the code is compiled multiple times, either via multiple inclusions
 * into a source files with different mode selectors defined or by multiple
 * compiler invocations.
 *
 * In C/C++ code we're restricted to the compiler target bit count, whereas in
 * assembly we can do everything in assembler run (with some 64-bit
 * restrictions, that is).
 *
 * Before \#defining the next mode selector and including
 * bs3kit-template-header.h again, include bs3kit-template-footer.h to undefine
 * all the previous mode selectors and the macros defined by the header.
 *
 * @{
 */

#ifdef DOXYGEN_RUNNING
/** @name Template mode selectors.
 *
 * Exactly one of these are defined by the file including the
 * bs3kit-template-header.h header file.  When building the code libraries, the
 * kBuild target defines this.
 *
 * @{ */
# define TMPL_RM            /**< real mode. */

# define TMPL_PE16          /**< 16-bit protected mode kernel+tss, running 16-bit code, unpaged. */
# define TMPL_PE16_32       /**< 16-bit protected mode kernel+tss, running 32-bit code, unpaged. */
# define TMPL_PE16_V86      /**< 16-bit protected mode kernel+tss, running virtual 8086 mode code, unpaged. */
# define TMPL_PE32          /**< 32-bit protected mode kernel+tss, running 32-bit code, unpaged. */
# define TMPL_PE32_16       /**< 32-bit protected mode kernel+tss, running 16-bit code, unpaged. */
# define TMPL_PEV86         /**< 32-bit protected mode kernel+tss, running virtual 8086 mode code, unpaged. */

# define TMPL_PP16          /**< 16-bit protected mode kernel+tss, running 16-bit code, paged. */
# define TMPL_PP16_32       /**< 16-bit protected mode kernel+tss, running 32-bit code, paged. */
# define TMPL_PP16_V86      /**< 16-bit protected mode kernel+tss, running virtual 8086 mode code, paged. */
# define TMPL_PP32          /**< 32-bit protected mode kernel+tss, running 32-bit code, paged. */
# define TMPL_PP32_16       /**< 32-bit protected mode kernel+tss, running 16-bit code, paged. */
# define TMPL_PPV86         /**< 32-bit protected mode kernel+tss, running virtual 8086 mode code, paged. */

# define TMPL_PAE16         /**< 16-bit protected mode kernel+tss, running 16-bit code, PAE paging. */
# define TMPL_PAE16_32      /**< 16-bit protected mode kernel+tss, running 32-bit code, PAE paging. */
# define TMPL_PAE16_V86     /**< 16-bit protected mode kernel+tss, running virtual 8086 mode code, PAE paging. */
# define TMPL_PAE32         /**< 32-bit protected mode kernel+tss, running 32-bit code, PAE paging. */
# define TMPL_PAE32_16      /**< 32-bit protected mode kernel+tss, running 16-bit code, PAE paging. */
# define TMPL_PAEV86        /**< 32-bit protected mode kernel+tss, running virtual 8086 mode code, PAE paging. */

# define TMPL_LM16          /**< 16-bit long mode (paged), kernel+tss always 64-bit. */
# define TMPL_LM32          /**< 32-bit long mode (paged), kernel+tss always 64-bit. */
# define TMPL_LM64          /**< 64-bit long mode (paged), kernel+tss always 64-bit. */
/** @} */

/** @name Derived Indicators
 * @{ */
# define TMPL_CMN_PE        /**< TMPL_PE16  | TMPL_PE16_32  | TMPL_PE16_V86  | TMPL_PE32  | TMPL_PE32_16  | TMPL_PEV86 */
# define TMPL_SYS_PE16      /**< TMPL_PE16  | TMPL_PE16_32  | TMPL_PE16_V86 */
# define TMPL_SYS_PE32      /**< TMPL_PE32  | TMPL_PE32_16  | TMPL_PEV86 */
# define TMPL_CMN_PP        /**< TMPL_PP16  | TMPL_PP16_32  | TMPL_PP16_V86  | TMPL_PP32  | TMPL_PP32_16  | TMPL_PPV86 */
# define TMPL_SYS_PP16      /**< TMPL_PP16  | TMPL_PP16_32  | TMPL_PP16_V86 */
# define TMPL_SYS_PP32      /**< TMPL_PP32  | TMPL_PP32_16  | TMPL_PPV86 */
# define TMPL_CMN_PAE       /**< TMPL_PAE16 | TMPL_PAE16_32 | TMPL_PAE16_V86 | TMPL_PAE32 | TMPL_PAE32_16 | TMPL_PAEV86 */
# define TMPL_SYS_PAE16     /**< TMPL_PAE16 | TMPL_PAE16_32 | TMPL_PAE16_V86 */
# define TMPL_SYS_PAE32     /**< TMPL_PAE32 | TMPL_PAE32_16 | TMPL_PAEV86 */
# define TMPL_CMN_LM        /**< TMPL_LM16  | TMPL_LM32  | TMPL_LM64 */
# define TMPL_CMN_V86       /**< TMPL_PEV86 | TMPL_PE16_V86 | TMPL_PPV86 | TMPL_PP16_V86 | TMPL_PAEV86 | TMPL_PAE16_V86 */
# define TMPL_CMN_R86       /**< TMPL_CMN_V86 | TMPL_RM */
# define TMPL_CMN_PAGING    /**< TMPL_CMN_PP | TMPL_CMN_PAE | TMPL_CMN_LM */
# define TMPL_CMN_WEIRD     /**< TMPL_PE16_32 | TMPL_PE32_16 | TMPL_PP16_32 | TMPL_PP32_16 | TMPL_PAE16_32 | TMPL_PAE32_16 | TMPL_CMN_WEIRD_V86 */
# define TMPL_CMN_WEIRD_V86 /**< TMPL_PE16_V86 | TMPL_PP16_V86 | TMPL_PAE16_V86 */
/** @} */

/** @def TMPL_NM
 * Name mangling macro for the current mode.
 *
 * Example: TMPL_NM(PrintChr)
 *
 * @param   Name        The function or variable name to mangle.
 * @sa      #TMPL_FAR_NM, #BS3_CMN_NM, #BS3_CMN_FAR_NM
 */
# define TMPL_NM(Name)  RT_CONCAT(Name,_mode)

/** @def TMPL_FAR_NM
 * Name mangling macro for the current mode into a far function name.
 *
 * In 32-bit and 64-bit code this does not differ from #TMPL_NM.
 *
 * Example: TMPL_FAR_NM(PrintChr)
 *
 * @param   Name        The function or variable name to mangle.
 * @sa      #TMPL_NM, #BS3_CMN_FAR_NM, #BS3_CMN_NM
 */
# define TMPL_FAR_NM(Name)  RT_CONCAT3(Name,_mode,_far)

/** @def TMPL_MODE_STR
 * Short mode description. */
# define TMPL_MODE_STR

/** @def TMPL_HAVE_BIOS
 * Indicates that we have direct access to the BIOS (only in real mode). */
# define TMPL_HAVE_BIOS


/** @name For ASM compatability
 * @{ */
/** @def TMPL_16BIT
 * For ASM compatibility - please use ARCH_BITS == 16. */
# define TMPL_16BIT
/** @def TMPL_32BIT
 * For ASM compatibility - please use ARCH_BITS == 32. */
# define TMPL_32BIT
/** @def TMPL_64BIT
 * For ASM compatibility - please use ARCH_BITS == 64. */
# define TMPL_64BIT

/** @def TMPL_BITS
 * For ASM compatibility - please use ARCH_BITS instead. */
# define TMPL_BITS  ARCH_BITS
/** @} */

#else /* !DOXYGEN_RUNNING */

//#undef BS3_CMN_NM
//#undef BS3_CMN_FAR_NM


/*
 * Convert TMPL_XXX to TMPL_MODE.
 */
#ifndef TMPL_MODE
# ifdef TMPL_RM
#  define TMPL_MODE         BS3_MODE_RM
# elif defined(TMPL_PE16)
#  define TMPL_MODE         BS3_MODE_PE16
# elif defined(TMPL_PE16_32)
#  define TMPL_MODE         BS3_MODE_PE16_32
# elif defined(TMPL_PE16_V86)
#  define TMPL_MODE         BS3_MODE_PE16_V86
# elif defined(TMPL_PE32)
#  define TMPL_MODE         BS3_MODE_PE32
# elif defined(TMPL_PE32_16)
#  define TMPL_MODE         BS3_MODE_PE32_16
# elif defined(TMPL_PEV86)
#  define TMPL_MODE         BS3_MODE_PEV86
# elif defined(TMPL_PP16)
#  define TMPL_MODE         BS3_MODE_PP16
# elif defined(TMPL_PP16_32)
#  define TMPL_MODE         BS3_MODE_PP16_32
# elif defined(TMPL_PP16_V86)
#  define TMPL_MODE         BS3_MODE_PP16_V86
# elif defined(TMPL_PP32)
#  define TMPL_MODE         BS3_MODE_PP32
# elif defined(TMPL_PP32_16)
#  define TMPL_MODE         BS3_MODE_PP32_16
# elif defined(TMPL_PPV86)
#  define TMPL_MODE         BS3_MODE_PPV86
# elif defined(TMPL_PAE16)
#  define TMPL_MODE         BS3_MODE_PAE16
# elif defined(TMPL_PAE16_32)
#  define TMPL_MODE         BS3_MODE_PAE16_32
# elif defined(TMPL_PAE16_V86)
#  define TMPL_MODE         BS3_MODE_PAE16_V86
# elif defined(TMPL_PAE32)
#  define TMPL_MODE         BS3_MODE_PAE32
# elif defined(TMPL_PAE32_16)
#  define TMPL_MODE         BS3_MODE_PAE32_16
# elif defined(TMPL_PAEV86)
#  define TMPL_MODE         BS3_MODE_PAEV86
# elif defined(TMPL_LM16)
#  define TMPL_MODE         BS3_MODE_LM16
# elif defined(TMPL_LM32)
#  define TMPL_MODE         BS3_MODE_LM32
# elif defined(TMPL_LM64)
#  define TMPL_MODE         BS3_MODE_LM64
# else
#  error "Unable to to figure out the template mode."
# endif
#endif


/*
 * Check the code bitness and set derived defines.
 */
#if (TMPL_MODE & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_16
# if ARCH_BITS != 16
#  error "BS3_MODE_CODE_16 requires ARCH_BITS to be 16."
# endif
# define TMPL_16BIT
# define TMPL_BITS              16
# define TMPL_UNDERSCORE        _
//# define BS3_CMN_NM(Name)       RT_CONCAT(Name,_c16)
//# define BS3_CMN_FAR_NM(Name)   RT_CONCAT(Name,_f16)


#elif (TMPL_MODE & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_32
# if ARCH_BITS != 32
#  error "BS3_MODE_CODE_32 requires ARCH_BITS to be 32."
# endif
# define TMPL_32BIT
# define TMPL_BITS              32
# define TMPL_UNDERSCORE        _
//# define BS3_CMN_NM(Name)       RT_CONCAT(Name,_c32)
//# define BS3_CMN_FAR_NM(a_Name) RT_CONCAT(Name,_c32)

#elif (TMPL_MODE & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_V86
# if ARCH_BITS != 16
#  error "BS3_MODE_CODE_V86 requires ARCH_BITS to be 16."
# endif
# define TMPL_16BIT
# define TMPL_BITS              16
# define TMPL_UNDERSCORE        _
//# define BS3_CMN_NM(Name)       RT_CONCAT(Name,_c16)
//# define BS3_CMN_FAR_NM(Name)   RT_CONCAT(Name,_f16)
# define TMPL_CMN_R86
# define TMPL_CMN_V86

#elif (TMPL_MODE & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_64
# if ARCH_BITS != 64
#  error "BS3_MODE_CODE_64 requires ARCH_BITS to be 64."
# endif
# define TMPL_64BIT
# define TMPL_BITS              64
# define TMPL_UNDERSCORE
//# define BS3_CMN_NM(Name)       RT_CONCAT(Name,_c64)
//# define BS3_CMN_FAR_NM(a_Name) RT_CONCAT(Name,_c64)

#else
# error "Invalid TMPL_MODE value!"
#endif


/*
 * Check the system specific mask and set derived values.
 */
#if (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_RM
# define TMPL_HAVE_BIOS
# define TMPL_CMN_R86

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PE16
# define TMPL_SYS_PE16
# define TMPL_CMN_PE

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PE32
# define TMPL_SYS_PE32
# define TMPL_CMN_PE

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PP16
# define TMPL_SYS_PP16
# define TMPL_CMN_PP
# define TMPL_CMN_PAGING

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PP32
# define TMPL_SYS_PP32
# define TMPL_CMN_PP
# define TMPL_CMN_PAGING

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PAE16
# define TMPL_SYS_PAE16
# define TMPL_CMN_PAE
# define TMPL_CMN_PAGING

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_PAE32
# define TMPL_SYS_PAE32
# define TMPL_CMN_PAE
# define TMPL_CMN_PAGING

#elif (TMPL_MODE & BS3_MODE_SYS_MASK) == BS3_MODE_SYS_LM
# define TMPL_SYS_LM
# define TMPL_CMN_LM
# define TMPL_CMN_PAGING

#else
# error "Invalid TMPL_MODE value!"
#endif


/*
 * Mode specific stuff.
 */
#if   TMPL_MODE == BS3_MODE_RM
# define TMPL_RM                1
# define TMPL_MODE_STR          "real mode"
# define TMPL_NM(Name)          RT_CONCAT(Name,_rm)
# define TMPL_MODE_LNAME        rm
# define TMPL_MODE_UNAME        RM


#elif TMPL_MODE == BS3_MODE_PE16
# define TMPL_PE16              1
# define TMPL_MODE_STR          "16-bit prot, 16-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pe16)
# define TMPL_MODE_LNAME        pe16
# define TMPL_MODE_UNAME        PE16

#elif TMPL_MODE == BS3_MODE_PE16_32
# define TMPL_PE16_32           1
# define TMPL_MODE_STR          "16-bit prot, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pe16_32)
# define TMPL_MODE_LNAME        pe16_32
# define TMPL_MODE_UNAME        PE16_32
# define TMPL_CMN_WEIRD

#elif TMPL_MODE == BS3_MODE_PE16_V86
# define TMPL_PE16_V86          1
# define TMPL_MODE_STR          "16-bit prot, v8086"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pe16_v86)
# define TMPL_MODE_LNAME        pe16_v86
# define TMPL_MODE_UNAME        PE16_v86
# define TMPL_CMN_WEIRD
# define TMPL_CMN_WEIRD_V86


#elif TMPL_MODE == BS3_MODE_PE32
# define TMPL_PE32              1
# define TMPL_MODE_STR          "32-bit prot, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pe32)
# define TMPL_MODE_LNAME        pe32
# define TMPL_MODE_UNAME        PE32

#elif TMPL_MODE == BS3_MODE_PE32_16
# define TMPL_PE32_16           1
# define TMPL_MODE_STR          "32-bit prot, 16-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pe32_16)
# define TMPL_MODE_LNAME        pe32_16
# define TMPL_MODE_UNAME        PE32_16
# define TMPL_CMN_WEIRD

#elif TMPL_MODE == BS3_MODE_PEV86
# define TMPL_PEV86             1
# define TMPL_MODE_STR          "32-bit prot, v8086"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pev86)
# define TMPL_MODE_LNAME        pev86
# define TMPL_MODE_UNAME        PEV86


#elif TMPL_MODE == BS3_MODE_PP16
# define TMPL_PP16              1
# define TMPL_MODE_STR          "16-bit paged, 16-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pp16)
# define TMPL_MODE_LNAME        pp16
# define TMPL_MODE_UNAME        PP16

#elif TMPL_MODE == BS3_MODE_PP16_32
# define TMPL_PP16_32           1
# define TMPL_MODE_STR          "16-bit paged, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pp16_32)
# define TMPL_MODE_LNAME        pp16_32
# define TMPL_MODE_UNAME        PP16_32
# define TMPL_CMN_WEIRD

#elif TMPL_MODE == BS3_MODE_PP16_V86
# define TMPL_PP16_V86          1
# define TMPL_MODE_STR          "16-bit paged, v8086"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pp16_v86)
# define TMPL_MODE_LNAME        pp16_v86
# define TMPL_MODE_UNAME        PP16_v86
# define TMPL_CMN_WEIRD
# define TMPL_CMN_WEIRD_V86


#elif TMPL_MODE == BS3_MODE_PP32
# define TMPL_PP32              1
# define TMPL_MODE_STR          "32-bit paged, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pp32)
# define TMPL_MODE_LNAME        pp32
# define TMPL_MODE_UNAME        PP32

#elif TMPL_MODE == BS3_MODE_PP32_16
# define TMPL_PP32_16           1
# define TMPL_MODE_STR          "32-bit paged, 16-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pp32_16)
# define TMPL_MODE_LNAME        pp32_16
# define TMPL_MODE_UNAME        PP32_16
# define TMPL_CMN_WEIRD

#elif TMPL_MODE == BS3_MODE_PPV86
# define TMPL_PPV86             1
# define TMPL_MODE_STR          "32-bit paged, v8086"
# define TMPL_NM(Name)          RT_CONCAT(Name,_ppv86)
# define TMPL_MODE_LNAME        ppv86
# define TMPL_MODE_UNAME        PPV86


#elif TMPL_MODE == BS3_MODE_PAE16
# define TMPL_PAE16             1
# define TMPL_MODE_STR          "16-bit pae, 16-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pae16)
# define TMPL_MODE_LNAME        pae16
# define TMPL_MODE_UNAME        PAE16

#elif TMPL_MODE == BS3_MODE_PAE16_32
# define TMPL_PAE16_32          1
# define TMPL_MODE_STR          "16-bit pae, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pae16_32)
# define TMPL_MODE_LNAME        pae16_32
# define TMPL_MODE_UNAME        PAE16_32
# define TMPL_CMN_WEIRD

#elif TMPL_MODE == BS3_MODE_PAE16_V86
# define TMPL_PAE16_V86         1
# define TMPL_MODE_STR          "16-bit pae, v8086"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pae16_v86)
# define TMPL_MODE_LNAME        pae16_v86
# define TMPL_MODE_UNAME        PAE16_v86
# define TMPL_CMN_WEIRD
# define TMPL_CMN_WEIRD_V86


#elif TMPL_MODE == BS3_MODE_PAE32
# define TMPL_PAE32             1
# define TMPL_MODE_STR          "32-bit pae, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pae32)
# define TMPL_MODE_LNAME        pae32
# define TMPL_MODE_UNAME        PAE32

#elif TMPL_MODE == BS3_MODE_PAE32_16
# define TMPL_PAE32_16          1
# define TMPL_MODE_STR          "32-bit pae, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_pae32_16)
# define TMPL_MODE_LNAME        pae32_16
# define TMPL_MODE_UNAME        PAE32_16
# define TMPL_CMN_WEIRD

#elif TMPL_MODE == BS3_MODE_PAEV86
# define TMPL_PAEV86            1
# define TMPL_MODE_STR          "32-bit pae, v8086 pae"
# define TMPL_NM(Name)          RT_CONCAT(Name,_paev86)
# define TMPL_MODE_LNAME        paev86
# define TMPL_MODE_UNAME        PAEV86


#elif TMPL_MODE == BS3_MODE_LM16
# define TMPL_LM16              1
# define TMPL_MODE_STR          "long, 16-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_lm16)
# define TMPL_MODE_LNAME        lm16
# define TMPL_MODE_UNAME        LM16

#elif TMPL_MODE == BS3_MODE_LM32
# define TMPL_LM32              1
# define TMPL_MODE_STR          "long, 32-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_lm32)
# define TMPL_MODE_LNAME        lm32
# define TMPL_MODE_UNAME        LM32

#elif TMPL_MODE == BS3_MODE_LM64
# define TMPL_LM64              1
# define TMPL_MODE_STR          "long, 64-bit"
# define TMPL_NM(Name)          RT_CONCAT(Name,_lm64)
# define TMPL_MODE_LNAME        lm64
# define TMPL_MODE_UNAME        LM64

#else
# error "Invalid TMPL_MODE value!!"
#endif


#if TMPL_MODE & (BS3_MODE_CODE_16 | BS3_MODE_CODE_V86)
# define TMPL_FAR_NM(Name)      RT_CONCAT3(TMPL_NM(Name),_f,ar) /* _far and far may be #defined already. */
#else
# define TMPL_FAR_NM(Name)      TMPL_NM(Name)
#endif


/** @def BS3_MODE_DEF
 * Macro for defining a mode specific function.
 *
 * This makes 16-bit mode functions far, while 32-bit and 64-bit are near.
 * You need to update the make file to generate near->far wrappers in most
 * cases.
 *
 * @param   a_RetType   The return type.
 * @param   a_Name      The function basename.
 * @param   a_Params    The parameter list (in parentheses).
 *
 * @sa      BS3_MODE_PROTO
 */
#if ARCH_BITS == 16
# define BS3_MODE_DEF(a_RetType, a_Name, a_Params) BS3_DECL_FAR(a_RetType) TMPL_FAR_NM(a_Name) a_Params
#else
# define BS3_MODE_DEF(a_RetType, a_Name, a_Params) BS3_DECL_NEAR(a_RetType)    TMPL_NM(a_Name) a_Params
#endif



#ifndef TMPL_MODE_STR
# error "internal error"
#endif

#endif /* !DOXYGEN_RUNNING */
/** @} */

