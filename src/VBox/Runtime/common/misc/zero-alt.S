/* $Id: zero-alt.S 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Zero Memory, mach-o version (for arm/sparc).
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


#ifdef ASM_FORMAT_MACHO
; Putting it in the code segment/section for now.
        .section    __TEXT,__text,regular,pure_instructions
        .section    __TEXT,__const
# define NAME(a) _##a
#elif defined(ASM_FORMAT_ELF) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_SPARC) || defined(RT_ARCH_SPARC64))
/* Putting it in the rodata segment/section for now. */
        .file       "zero-alt.S"
        .section    ".rodata"
# define NAME(a) a
# ifndef RT_ARCH_ARM64 /* RTSignTool references g_abRTZero4K from VBoxRT.so */
#  define NEED_HIDDEN
# endif
#else
# error "PORT ME!"
#endif

/* 64KB of zero memory with various sized labels. */
        .globl      NAME(g_abRTZeroPage)
#ifdef ASM_FORMAT_ELF
# ifdef NEED_HIDDEN
        .hidden     NAME(g_abRTZeroPage)
# endif
        .type       NAME(g_abRTZeroPage),#object
# if defined(RT_ARCH_SPARC) || defined(RT_ARCH_SPARC64)
        .size       NAME(g_abRTZeroPage),8192
# else
        .size       NAME(g_abRTZeroPage),4096
# endif
#endif
NAME(g_abRTZeroPage):
        .globl      NAME(g_abRTZero4K)
#ifdef ASM_FORMAT_ELF
# ifdef NEED_HIDDEN
        .hidden     NAME(g_abRTZero4K)
# endif
        .type       NAME(g_abRTZero4K),#object
        .size       NAME(g_abRTZero4K),4096
#endif
NAME(g_abRTZero4K):
        .globl      NAME(g_abRTZero8K)
#ifdef ASM_FORMAT_ELF
# ifdef NEED_HIDDEN
        .hidden     NAME(g_abRTZero8K)
# endif
        .type       NAME(g_abRTZero8K),#object
        .size       NAME(g_abRTZero8K),8192
#endif
NAME(g_abRTZero8K):
        .globl      NAME(g_abRTZero16K)
#ifdef ASM_FORMAT_ELF
# ifdef NEED_HIDDEN
        .hidden     NAME(g_abRTZero16K)
# endif
        .type       NAME(g_abRTZero16K),#object
        .size       NAME(g_abRTZero16K),16384
#endif
NAME(g_abRTZero16K):
        .globl      NAME(g_abRTZero32K)
#ifdef ASM_FORMAT_ELF
# ifdef NEED_HIDDEN
        .hidden     NAME(g_abRTZero32K)
# endif
        .type       NAME(g_abRTZero32K),#object
        .size       NAME(g_abRTZero32K),32768
#endif
NAME(g_abRTZero32K):
        .globl      NAME(g_abRTZero64K)
#ifdef ASM_FORMAT_ELF
# ifdef NEED_HIDDEN
        .hidden     NAME(g_abRTZero64K)
# endif
        .type       NAME(g_abRTZero64K),#object
        .size       NAME(g_abRTZero64K),65536
#endif
NAME(g_abRTZero64K):

#ifdef ASM_FORMAT_MACHO
        .space      65536
#elif defined(ASM_FORMAT_ELF)
        .skip       65536
#endif

