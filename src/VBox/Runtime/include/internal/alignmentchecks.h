/* $Id: alignmentchecks.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal header for hacking alignment checks on x86 and AMD64.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_INTERNAL_alignmentchecks_h
#define IPRT_INCLUDED_INTERNAL_alignmentchecks_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @def IPRT_WITH_ALIGNMENT_CHECKS
 * Enables or disables the alignment check feature and related hacks. */

#ifndef IPRT_WITH_ALIGNMENT_CHECKS
# if ( defined(DEBUG) && !defined(IN_GUEST) ) || defined(DOXYGEN_RUNNING)
#  define IPRT_WITH_ALIGNMENT_CHECKS 1
# endif
#endif

/** @def IPRT_ALIGNMENT_CHECKS_DISABLE
 * Disables alignment checks.
 * Typically used before calling problematic library functions.
 */

/** @def IPRT_ALIGNMENT_CHECKS_ENABLE
 * (re-)Enables alignment checks if they are supposed to be active.
 * This is used to counter IPRT_ALIGNMENT_CHECKS_DISABLE as well as enabling
 * them for the first time.
 */

#if defined(IPRT_WITH_ALIGNMENT_CHECKS) \
 && (   defined(RT_ARCH_AMD64) \
     || defined(RT_ARCH_X86) )
# include <iprt/asm-amd64-x86.h>

RT_C_DECLS_BEGIN
extern RTDATADECL(bool) g_fRTAlignmentChecks;
RT_C_DECLS_END

# define IPRT_ALIGNMENT_CHECKS_DISABLE() \
    do { if (g_fRTAlignmentChecks) ASMSetFlags(ASMGetFlags() & ~RT_BIT_32(18)); } while (0)

# define IPRT_ALIGNMENT_CHECKS_ENABLE() \
    do { if (g_fRTAlignmentChecks) ASMSetFlags(ASMGetFlags() | RT_BIT_32(18)); } while (0)

#else
# define IPRT_ALIGNMENT_CHECKS_DISABLE() do {} while (0)
# define IPRT_ALIGNMENT_CHECKS_ENABLE()  do {} while (0)
#endif

#endif /* !IPRT_INCLUDED_INTERNAL_alignmentchecks_h */

