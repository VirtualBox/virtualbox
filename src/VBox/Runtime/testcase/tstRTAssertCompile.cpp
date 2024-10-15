/* $Id: tstRTAssertCompile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - AssertCompile* - A Compile Time Testcase.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>


int main(int argc, char **argv)
{
    /* Only positive tests here. */
    NOREF(argc); NOREF(argv);

    AssertCompile(true);
    AssertCompile(1);
    AssertCompile(2);
    AssertCompile(99);

    uint8_t   u8; NOREF( u8);
    uint16_t u16; NOREF(u16);
    uint32_t u32; NOREF(u32);
    uint64_t u64; NOREF(u64);
    AssertCompileSize( u8, 1);
    AssertCompileSize(u16, 2);
    AssertCompileSize(u32, 4);
    AssertCompileSize(u64, 8);

    AssertCompileSizeAlignment( u8, 1);
    AssertCompileSizeAlignment(u16, 1);
    AssertCompileSizeAlignment(u16, 2);
    AssertCompileSizeAlignment(u32, 1);
    AssertCompileSizeAlignment(u32, 2);
    AssertCompileSizeAlignment(u32, 4);
    AssertCompileSizeAlignment(u64, 1);
    AssertCompileSizeAlignment(u64, 2);
    AssertCompileSizeAlignment(u64, 4);
    AssertCompileSizeAlignment(u64, 8);

    typedef struct STRUCT12S
    {
        uint8_t     u8;
        uint8_t     au8[8];
        uint64_t    u64;
        uint8_t     u8UnalignmentFiller1;
        uint32_t    u32;
        uint8_t     u8UnalignmentFiller2;
        uint16_t    u16;
        const char *psz;
        uint32_t    u32A;
        uint32_t    u32B;
    } STRUCT1, STRUCT2;

    AssertCompileMemberSize(STRUCT1,  u8, 1);
    AssertCompileMemberSize(STRUCT1, u16, 2);
    AssertCompileMemberSize(STRUCT1, u32, 4);
    AssertCompileMemberSize(STRUCT1, u64, 8);

    AssertCompileMemberSizeAlignment(STRUCT1,  u8, 1);
    AssertCompileMemberSizeAlignment(STRUCT1, u16, 1);
    AssertCompileMemberSizeAlignment(STRUCT1, u16, 2);
    AssertCompileMemberSizeAlignment(STRUCT1, u32, 1);
    AssertCompileMemberSizeAlignment(STRUCT1, u32, 2);
    AssertCompileMemberSizeAlignment(STRUCT1, u32, 4);
    AssertCompileMemberSizeAlignment(STRUCT1, u64, 1);
    AssertCompileMemberSizeAlignment(STRUCT1, u64, 2);
    AssertCompileMemberSizeAlignment(STRUCT1, u64, 4);
    AssertCompileMemberSizeAlignment(STRUCT1, u64, 8);
    AssertCompileMemberSizeAlignment(STRUCT1, psz, sizeof(void *));

    AssertCompileMemberAlignment(STRUCT1,  u8, 1);
    AssertCompileMemberAlignment(STRUCT1, u16, 1);
    AssertCompileMemberAlignment(STRUCT1, u16, 2);
    AssertCompileMemberAlignment(STRUCT1, u32, 1);
    AssertCompileMemberAlignment(STRUCT1, u32, 2);
    AssertCompileMemberAlignment(STRUCT1, u32, 4);
    AssertCompileMemberAlignment(STRUCT1, u64, 1);
    AssertCompileMemberAlignment(STRUCT1, u64, 2);
    AssertCompileMemberAlignment(STRUCT1, u64, 4);
#if defined(__GNUC__) && ARCH_BITS >= 64
    AssertCompileMemberAlignment(STRUCT1, u64, 8);
#endif
    AssertCompileMemberAlignment(STRUCT1, psz, sizeof(void *));

    AssertCompileMemberOffset(STRUCT1, u8, 0);
    AssertCompileMemberOffset(STRUCT1, au8, 1);
#ifndef _MSC_VER /** @todo figure out why MSC has trouble with these expressions */
    AssertCompileMemberOffset(STRUCT1, au8[0], 1);
    AssertCompileMemberOffset(STRUCT1, au8[8], 9);
#endif

    typedef union UNION1U
    {
        STRUCT1 s1;
        STRUCT2 s2;
    } UNION1;

    AssertCompile2MemberOffsets(UNION1, s1.u8,  s2.u8);
    AssertCompile2MemberOffsets(UNION1, s1.u16, s2.u16);
    AssertCompile2MemberOffsets(UNION1, s1.u32, s2.u32);
    AssertCompile2MemberOffsets(UNION1, s1.u64, s2.u64);
    AssertCompile2MemberOffsets(UNION1, s1.psz, s2.psz);

    AssertCompileAdjacentMembers(STRUCT1, u32A, u32B);
    AssertCompileAdjacentMembers(STRUCT1, u8, au8);
#ifndef _MSC_VER /** @todo figure out why MSC has trouble with these expressions */
    AssertCompileAdjacentMembers(STRUCT1, u8, au8[0]);
    AssertCompileAdjacentMembers(STRUCT1, au8[0], au8[1]);
#endif

    AssertCompileMembersAtSameOffset(STRUCT1,  u8, STRUCT2,  u8);
    AssertCompileMembersAtSameOffset(STRUCT1, au8, STRUCT2, au8);
    AssertCompileMembersAtSameOffset(STRUCT1, u16, STRUCT2, u16);
    AssertCompileMembersAtSameOffset(STRUCT1, u32, STRUCT2, u32);
    AssertCompileMembersAtSameOffset(STRUCT1, u64, STRUCT2, u64);

    AssertCompileMembersSameSize(STRUCT1,  u8, STRUCT2,  u8);
    AssertCompileMembersSameSize(STRUCT1, au8, STRUCT2, au8);
    AssertCompileMembersSameSize(STRUCT1, u16, STRUCT2, u16);
    AssertCompileMembersSameSize(STRUCT1, u32, STRUCT2, u32);
    AssertCompileMembersSameSize(STRUCT1, u64, STRUCT2, u64);

    AssertCompileMembersSameSizeAndOffset(STRUCT1,  u8, STRUCT2,  u8);
    AssertCompileMembersSameSizeAndOffset(STRUCT1, au8, STRUCT2, au8);
    AssertCompileMembersSameSizeAndOffset(STRUCT1, u16, STRUCT2, u16);
    AssertCompileMembersSameSizeAndOffset(STRUCT1, u32, STRUCT2, u32);
    AssertCompileMembersSameSizeAndOffset(STRUCT1, u64, STRUCT2, u64);

    /*
     * Check some cdefs.h macros while where here, we'll be using
     * AssertCompile so it's kind of related.
     */
#ifdef RT_COMPILER_SUPPORTS_VA_ARGS
# if 0
    AssertCompile(RT_COUNT_VA_ARGS() == 0);
    AssertCompile(RT_COUNT_VA_ARGS(RT_NOTHING) == 1);
# endif
    AssertCompile(RT_COUNT_VA_ARGS(asdf) == 1);
    AssertCompile(RT_COUNT_VA_ARGS(yyyy) == 1);
    AssertCompile(RT_COUNT_VA_ARGS(_) == 1);
    AssertCompile(RT_COUNT_VA_ARGS(1, 2, 3, 4, 5, 6, 7, 8, 9, 0) == 10);
#endif
    return 0;
}

