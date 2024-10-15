/* $Id: sanity.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Setup Sanity Checks, C and C++.
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

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assert.h>

/*
 * Check that the IN_[RING3|RING0|GC] and [|R3_|R0_|GC_]ARCH_BITS
 * match up correctly.
 *
 * IPRT assumes r0 and r3 to has the same bit count.
 */

#if defined(IN_RING3) && ARCH_BITS != R3_ARCH_BITS
# error "defined(IN_RING3) && ARCH_BITS != R3_ARCH_BITS"
#endif
#if defined(IN_RING0) && ARCH_BITS != R0_ARCH_BITS
# error "defined(IN_RING0) && ARCH_BITS != R0_ARCH_BITS"
#endif
#if defined(IN_RC) && ARCH_BITS != 32
# error "defined(IN_RC) && ARCH_BITS != 32"
#endif
#if (defined(IN_RING0) || defined(IN_RING3)) && HC_ARCH_BITS != ARCH_BITS
# error "(defined(IN_RING0) || defined(IN_RING3)) && HC_ARCH_BITS != ARCH_BITS"
#endif
#if defined(IN_RC) && GC_ARCH_BITS != 64 && GC_ARCH_BITS != ARCH_BITS
# error "defined(IN_RC) && GC_ARCH_BITS != ARCH_BITS"
#endif


/*
 * Check basic host (hc/r0/r3) types.
 */
#if HC_ARCH_BITS == 64

AssertCompileSize(RTHCPTR, 8);
AssertCompileSize(RTHCINT, 4);
AssertCompileSize(RTHCUINT, 4);
AssertCompileSize(RTHCINTPTR, 8);
AssertCompileSize(RTHCUINTPTR, 8);
/*AssertCompileSize(RTHCINTREG, 8);*/
AssertCompileSize(RTHCUINTREG, 8);
AssertCompileSize(RTR0PTR, 8);
/*AssertCompileSize(RTR0INT, 4);*/
/*AssertCompileSize(RTR0UINT, 4);*/
AssertCompileSize(RTR0INTPTR, 8);
AssertCompileSize(RTR0UINTPTR, 8);
/*AssertCompileSize(RTR3PTR, 8);*/
/*AssertCompileSize(RTR3INT, 4);*/
/*AssertCompileSize(RTR3UINT, 4);*/
AssertCompileSize(RTR3INTPTR, 8);
AssertCompileSize(RTR3UINTPTR, 8);
AssertCompileSize(RTUINTPTR, 8);

# if defined(IN_RING3) || defined(IN_RING0)
/*AssertCompileSize(RTCCINTREG, 8);*/
AssertCompileSize(RTCCUINTREG, 8);
# endif

#else

AssertCompileSize(RTHCPTR, 4);
AssertCompileSize(RTHCINT, 4);
AssertCompileSize(RTHCUINT, 4);
/*AssertCompileSize(RTHCINTPTR, 4);*/
AssertCompileSize(RTHCUINTPTR, 4);
AssertCompileSize(RTR0PTR, 4);
/*AssertCompileSize(RTR0INT, 4);*/
/*AssertCompileSize(RTR0UINT, 4);*/
AssertCompileSize(RTR0INTPTR, 4);
AssertCompileSize(RTR0UINTPTR, 4);
/*AssertCompileSize(RTR3PTR, 4);*/
/*AssertCompileSize(RTR3INT, 4);*/
/*AssertCompileSize(RTR3UINT, 4);*/
AssertCompileSize(RTR3INTPTR, 4);
AssertCompileSize(RTR3UINTPTR, 4);
# if GC_ARCH_BITS == 64
AssertCompileSize(RTUINTPTR, 8);
# else
AssertCompileSize(RTUINTPTR, 4);
# endif

# if defined(IN_RING3) || defined(IN_RING0)
/*AssertCompileSize(RTCCINTREG, 4);*/
AssertCompileSize(RTCCUINTREG, 4);
# endif

#endif

AssertCompileSize(RTHCPHYS, 8);


/*
 * Check basic guest context types.
 */
#if GC_ARCH_BITS == 64

AssertCompileSize(RTGCINT, 8);
AssertCompileSize(RTGCUINT, 8);
AssertCompileSize(RTGCINTPTR, 8);
AssertCompileSize(RTGCUINTPTR, 8);
/*AssertCompileSize(RTGCINTREG, 8);*/
AssertCompileSize(RTGCUINTREG, 8);

# ifdef IN_RC
/*AssertCompileSize(RTCCINTREG, 8);*/
/* Hack alert: there is no such thing as a GC context when GC_ARCH_BITS == 64; it's still 32 bits */
AssertCompileSize(RTCCUINTREG, 4);
# endif

#else

AssertCompileSize(RTGCINT, 4);
AssertCompileSize(RTGCUINT, 4);
AssertCompileSize(RTGCINTPTR, 4);
AssertCompileSize(RTGCUINTPTR, 4);
/*AssertCompileSize(RTGCINTREG, 4);*/
AssertCompileSize(RTGCUINTREG, 4);

# ifdef IN_RC
/*AssertCompileSize(RTCCINTREG, 4);*/
AssertCompileSize(RTCCUINTREG, 4);
# endif

#endif

AssertCompileSize(RTGCPHYS64, 8);
AssertCompileSize(RTGCPHYS32, 4);
AssertCompileSize(RTGCPHYS, 8);


/*
 * Check basic current context types.
 */
#if ARCH_BITS == 64

AssertCompileSize(void *, 8);
AssertCompileSize(intptr_t, 8);
AssertCompileSize(uintptr_t, 8);
AssertCompileSize(size_t, 8);
AssertCompileSize(ssize_t, 8);

#else

AssertCompileSize(void *, 4);
AssertCompileSize(intptr_t, 4);
AssertCompileSize(uintptr_t, 4);
AssertCompileSize(size_t, 4);
AssertCompileSize(ssize_t, 4);

#endif


/*
 * Standard sized types.
 */
AssertCompileSize(uint8_t,  1);
AssertCompileSize(uint16_t, 2);
AssertCompileSize(uint32_t, 4);
AssertCompileSize(uint64_t, 8);

#define TEST_CONST_MACRO(c,t) \
    AssertCompile(sizeof(c) == sizeof(t) || (sizeof(c) == sizeof(int) && sizeof(t) < sizeof(int)) )

TEST_CONST_MACRO(UINT8_C(1),  uint8_t);
TEST_CONST_MACRO(UINT16_C(1), uint16_t);
TEST_CONST_MACRO(UINT32_C(1), uint32_t);
TEST_CONST_MACRO(UINT64_C(1), uint64_t);

TEST_CONST_MACRO(INT8_C(1),   int8_t);
TEST_CONST_MACRO(INT8_C(-1),  int8_t);
TEST_CONST_MACRO(INT16_C(1),  int16_t);
TEST_CONST_MACRO(INT16_C(-1), int16_t);
TEST_CONST_MACRO(INT32_C(1),  int32_t);
TEST_CONST_MACRO(INT32_C(-1), int32_t);
TEST_CONST_MACRO(INT64_C(1),  int64_t);
TEST_CONST_MACRO(INT64_C(-1), int64_t);


/*
 * Our union types.
 */
AssertCompileSize(RTUINT16U,  2);
AssertCompileSize(RTUINT32U,  4);
AssertCompileSize(RTUINT64U,  8);
AssertCompileSize(RTUINT128U, 16);
/*AssertCompileSize(RTFLOAT32U, 8);*/
AssertCompileSize(RTFLOAT64U, 8);
AssertCompileSize(RTFLOAT80U, 10);
/*AssertCompileSize(RTFLOAT128U, 16);*/

