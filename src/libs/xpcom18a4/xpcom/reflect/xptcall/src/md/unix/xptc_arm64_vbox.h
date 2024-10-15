/* $Id: xptc_arm64_vbox.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * XPCOM - Common stuff for xptcinvoke/xptcstubs for arm64.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef VBOX_INCLUDED_SRC_unix_xptc_arm64_vbox_h
#define VBOX_INCLUDED_SRC_unix_xptc_arm64_vbox_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if defined(RT_OS_DARWIN)
# define NAME_PREFIX        _
# define NAME_PREFIX_STR    "_"
#else
# define NAME_PREFIX
# define NAME_PREFIX_STR    ""
#endif
#define NUM_ARGS_IN_GPRS    8          /**< Number of arguments passed in general purpose registers (starting with x0). */
#define SZ_ARGS_IN_GPRS          (NUM_ARGS_IN_GPRS * 8)

#define NUM_ARGS_IN_FPRS    8          /**< Number of arguments passed in floating point registers (starting with d0). */
#define SZ_ARGS_IN_GPRS_AND_FPRS ((NUM_ARGS_IN_GPRS + NUM_ARGS_IN_FPRS) * 8)   /**< Number of arguments passed in floating point registers (starting with d0). */

/*
 * Inject some helper macros into the assembly file the invoke and stub code can make use of
 * in order to keep the mess to a minimum.
 */
__asm__(
#ifdef RT_OS_DARWIN
".macro .type, a_Name, a_Type\n" /* clang/macho doesn't support this directive, so stub it. */
".endm\n"
".macro .size, a_Name, a_Expr\n" /* clang/macho doesn't support this directive, so stub it. */
".endm\n"
#endif

".macro BEGINPROC, a_Name\n"
    ".p2align 2\n"
    ".globl "          NAME_PREFIX_STR "\\a_Name\n"
    ".type  "          NAME_PREFIX_STR "\\a_Name,@function\n"
NAME_PREFIX_STR "\\a_Name:\n"
".endm\n"

".macro BEGINPROC_HIDDEN, a_Name\n"
    ".p2align 2\n"
#ifdef RT_OS_DARWIN
    ".private_extern " NAME_PREFIX_STR "\\a_Name\n"
#else
    ".hidden "         NAME_PREFIX_STR "\\a_Name\n"
#endif
    ".globl "          NAME_PREFIX_STR "\\a_Name\n"
    ".type  "          NAME_PREFIX_STR "\\a_Name,@function\n"
NAME_PREFIX_STR "\\a_Name:\n"
".endm\n"

".macro BEGINCODE\n"
#ifdef RT_OS_DARWIN
    ".section __TEXT,__text,regular,pure_instructions\n"
#else
    ".section .text\n"
#endif
".endm\n"
);

#endif /* !VBOX_INCLUDED_SRC_unix_xptc_arm64_vbox_h */
