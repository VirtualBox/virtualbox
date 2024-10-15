/* $Id: tstRTBitOperations.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Inlined Bit Operations.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/asm-mem.h>
#include <iprt/asm.h>

#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*
 * Test 2 - ID allocation using a bitmap.
 */

#define NIL_TEST2_ID    0
#define TEST2_ID_LAST   ((RT_BIT_32(28) - 1) >> 8)

struct TestMap2
{
    uint32_t    idNil;
    uint32_t    idLast;
    uint32_t    idChunkPrev;
    uint32_t    bmChunkId[(TEST2_ID_LAST + 1 + 31) / 32];
};

static uint32_t test2AllocId(struct TestMap2 *p2)
{
    /*
     * Scan sequentially from the last one + 1.
     */
    int32_t idChunk = ++p2->idChunkPrev;
    if (    (uint32_t)idChunk < TEST2_ID_LAST
        &&  idChunk > NIL_TEST2_ID)
    {
        idChunk = ASMBitNextClear(&p2->bmChunkId[0], TEST2_ID_LAST + 1, idChunk);
        if (idChunk > NIL_TEST2_ID)
        {
            if (ASMAtomicBitTestAndSet(&p2->bmChunkId[0], idChunk))
            {
                RTTestFailed(NIL_RTTEST, "line %d: idChunk=%#x", __LINE__, idChunk);
                return NIL_TEST2_ID;
            }
            return p2->idChunkPrev = idChunk;
        }
    }

    /*
     * Ok, scan from the start.
     */
    idChunk = ASMBitFirstClear(&p2->bmChunkId[0], TEST2_ID_LAST + 1);
    if (idChunk <= NIL_TEST2_ID)
    {
        RTTestFailed(NIL_RTTEST, "line %d: idChunk=%#x", __LINE__, idChunk);
        return NIL_TEST2_ID;
    }
    if (ASMAtomicBitTestAndSet(&p2->bmChunkId[0], idChunk))
    {
        RTTestFailed(NIL_RTTEST, "line %d: idChunk=%#x", __LINE__, idChunk);
        return NIL_TEST2_ID;
    }

    return p2->idChunkPrev = idChunk;
}


static void test2(RTTEST hTest)
{
    struct TestMap2 *p2 = (struct TestMap2 *)RTTestGuardedAllocTail(hTest, sizeof(TestMap2));
    p2->idNil  = NIL_TEST2_ID;
    p2->idLast = TEST2_ID_LAST;

    /* Some simple tests first. */
    RT_ZERO(p2->bmChunkId);
    RTTEST_CHECK(hTest, ASMBitFirstSet(&p2->bmChunkId[0], TEST2_ID_LAST + 1) == -1);
    for (uint32_t iBit = 0; iBit <= TEST2_ID_LAST; iBit++)
        RTTEST_CHECK(hTest, !ASMBitTest(&p2->bmChunkId[0], iBit));

    memset(&p2->bmChunkId[0], 0xff, sizeof(p2->bmChunkId));
    RTTEST_CHECK(hTest, ASMBitFirstClear(&p2->bmChunkId[0], TEST2_ID_LAST + 1) == -1);
    for (uint32_t iBit = 0; iBit <= TEST2_ID_LAST; iBit++)
        RTTEST_CHECK(hTest, ASMBitTest(&p2->bmChunkId[0], iBit));

    /* The real test. */
    p2->idChunkPrev = 0;
    RT_ZERO(p2->bmChunkId);
    ASMBitSet(p2->bmChunkId, NIL_TEST2_ID);
    uint32_t cLeft = TEST2_ID_LAST;
    while (cLeft-- > 0)
        test2AllocId(p2);

    RTTEST_CHECK(hTest, ASMBitFirstClear(&p2->bmChunkId[0], TEST2_ID_LAST + 1) == -1);
}


int main()
{
    /*
     * Init the runtime and stuff.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTBitOperations", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    int i;
    int j;
    int k;

    /*
     * Tests
     */
    struct TestMap
    {
        uint32_t au32[4];
    };
#if 0
    struct TestMap sTest;
    struct TestMap *p = &sTest;
#else
    struct TestMap *p = (struct TestMap *)RTTestGuardedAllocTail(hTest, sizeof(*p));
#endif
#define DUMP()          RTTestPrintf(hTest, RTTESTLVL_INFO, "au32={%08x,%08x,%08x,%08x}", p->au32[0], p->au32[1], p->au32[2], p->au32[3])
#define CHECK(expr)     do { if (!(expr)) { RTTestFailed(hTest, "line %d: %s", __LINE__, #expr); DUMP(); } CHECK_GUARD(s); } while (0)
#define CHECK_BIT(expr,  b1)            do { if (!(expr)) { RTTestFailed(hTest, "line %d, b1=%d: %s", __LINE__, b1, #expr); } CHECK_GUARD(s); } while (0)
#define CHECK_BIT2(expr, b1, b2)        do { if (!(expr)) { RTTestFailed(hTest, "line %d, b1=%d b2=%d: %s", __LINE__, b1, b2, #expr); } CHECK_GUARD(s); } while (0)
#define CHECK_BIT3(expr, b1, b2, b3)    do { if (!(expr)) { RTTestFailed(hTest, "line %d, b1=%d b2=%d b3=%d: %s", __LINE__, b1, b2, b3, #expr); } CHECK_GUARD(s); } while (0)
#define CHECK_VAL(a_RetType, a_Fmt, a_ValueExpr, a_Expect) \
                        do { a_RetType rcCheckRet = (a_ValueExpr); \
                             if (RT_LIKELY((rcCheckRet) == (a_Expect))) {} \
                             else RTTestFailed(hTest, "line %d: %s -> " a_Fmt ", expected " a_Fmt "", \
                                               __LINE__, #a_ValueExpr, rcCheckRet, a_Expect); \
                             CHECK_GUARD(s); \
                        } while (0)

#define GUARD_MAP(p)    do {  } while (0)
#define CHECK_GUARD(p)  do {  } while (0)
#define MAP_CLEAR(p)    do { RT_ZERO(*(p)); GUARD_MAP(p); } while (0)
#define MAP_SET(p)      do { memset(p, 0xff, sizeof(*(p))); GUARD_MAP(p); } while (0)

    /* self check. */
    MAP_CLEAR(p);
    CHECK_GUARD(p);

    /*
     * Check the primitives first:
     */
    CHECK_VAL(unsigned, "%u", ASMBitLastSetU32(0), 0);
    CHECK(ASMBitLastSetU32(1) == 1);
    CHECK(ASMBitLastSetU32(0x80000000) == 32);
    CHECK(ASMBitLastSetU32(0xffffffff) == 32);
    CHECK(ASMBitLastSetU32(RT_BIT(23) | RT_BIT(11)) == 24);
    for (i = 0; i < 32; i++)
        CHECK(ASMBitLastSetU32(1 << i) == (unsigned)i + 1);

    CHECK(ASMBitFirstSetU32(0) == 0);
    CHECK(ASMBitFirstSetU32(1) == 1);
    CHECK(ASMBitFirstSetU32(0x80000000) == 32);
    CHECK(ASMBitFirstSetU32(0xffffffff) == 1);
    CHECK(ASMBitFirstSetU32(RT_BIT(23) | RT_BIT(11)) == 12);
    for (i = 0; i < 32; i++)
        CHECK(ASMBitFirstSetU32(1 << i) == (unsigned)i + 1);

    CHECK(ASMBitLastSetU64(UINT64_C(0)) == 0);
    CHECK(ASMBitLastSetU64(UINT64_C(1)) == 1);
    CHECK(ASMBitLastSetU64(UINT64_C(0x80000000)) == 32);
    CHECK(ASMBitLastSetU64(UINT64_C(0xffffffff)) == 32);
    CHECK(ASMBitLastSetU64(RT_BIT_64(33) | RT_BIT_64(11)) == 34);
    for (i = 0; i < 64; i++)
        CHECK(ASMBitLastSetU64(UINT64_C(1) << i) == (unsigned)i + 1);

    CHECK(ASMBitFirstSetU64(UINT64_C(0)) == 0);
    CHECK(ASMBitFirstSetU64(UINT64_C(1)) == 1);
    CHECK(ASMBitFirstSetU64(UINT64_C(0x80000000)) == 32);
    CHECK(ASMBitFirstSetU64(UINT64_C(0x800000000000)) == 48);
    CHECK(ASMBitFirstSetU64(UINT64_C(0x8000000000000000)) == 64);
    CHECK(ASMBitFirstSetU64(UINT64_C(0xffffffff)) == 1);
    CHECK(ASMBitFirstSetU64(UINT64_C(0xffffffffffffffff)) == 1);
    CHECK(ASMBitFirstSetU64(RT_BIT_64(33) | RT_BIT_64(11)) == 12);
    for (i = 0; i < 64; i++)
        CHECK(ASMBitFirstSetU64(UINT64_C(1) << i) == (unsigned)i + 1);

    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU32(0), 0);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU32(UINT32_C(0x84210000)), 16+1);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU32(UINT32_C(0xffffffff)),  0+1);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU32(UINT32_C(0x80000000)), 31+1);

    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU64(0), 0);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU64(UINT64_C(0xffffeeee84210000)), 16+1);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU64(UINT64_C(0xffffeeee00000000)), 33+1);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU64(UINT64_C(0x8000000000000000)), 63+1);
    CHECK_VAL(unsigned, "%u", ASMBitFirstSetU64(UINT64_C(0xffffffffffffffff)),  0+1);

    CHECK_VAL(unsigned, "%u", ASMBitLastSetU32(0), 0);
    CHECK_VAL(unsigned, "%u", ASMBitLastSetU32(UINT32_C(0xffffffff)), 31+1);
    CHECK_VAL(unsigned, "%u", ASMBitLastSetU32(UINT32_C(0x00000001)),  0+1);
    CHECK_VAL(unsigned, "%u", ASMBitLastSetU32(UINT32_C(0x0001ffff)), 16+1);
    CHECK_VAL(unsigned, "%u", ASMBitLastSetU32(UINT32_C(0x01234567)), 24+1);

    CHECK_VAL(unsigned, "%u", ASMBitLastSetU64(0), 0);
    CHECK_VAL(unsigned, "%u", ASMBitLastSetU64(UINT64_C(0x0000807060504030)), 47+1);

    CHECK_VAL(uint16_t, "%#x", ASMByteSwapU16(UINT16_C(0x1234)), UINT16_C(0x3412));

    CHECK_VAL(uint32_t, "%#x", ASMByteSwapU32(UINT32_C(0x12345678)), UINT32_C(0x78563412));

    CHECK_VAL(uint64_t, "%#llx", ASMByteSwapU64(UINT64_C(0x1122334455667788)), UINT64_C(0x8877665544332211));

    CHECK_VAL(uint32_t, "%#x", ASMRotateLeftU32(UINT32_C(0x12345678),  4), UINT32_C(0x23456781));
    CHECK_VAL(uint32_t, "%#x", ASMRotateLeftU32(UINT32_C(0x12345678), 16), UINT32_C(0x56781234));
    CHECK_VAL(uint32_t, "%#x", ASMRotateLeftU32(UINT32_C(0x82868080), 29), UINT32_C(0x1050d010));
    CHECK_VAL(uint32_t, "%#x", ASMRotateLeftU32(UINT32_C(0xfedcba89),  1), UINT32_C(0xfdb97513));

    CHECK_VAL(uint32_t, "%#x", ASMRotateRightU32(UINT32_C(0x12345678),  4), UINT32_C(0x81234567));
    CHECK_VAL(uint32_t, "%#x", ASMRotateRightU32(UINT32_C(0x12345678), 16), UINT32_C(0x56781234));
    CHECK_VAL(uint32_t, "%#x", ASMRotateRightU32(UINT32_C(0x82868080), 29), UINT32_C(0x14340404));
    CHECK_VAL(uint32_t, "%#x", ASMRotateRightU32(UINT32_C(0xfedcba89),  1), UINT32_C(0xff6e5d44));

    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x123456789abcdef0),  4), UINT64_C(0x23456789abcdef01));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x123456789abcdef0), 16), UINT64_C(0x56789abcdef01234));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x123456789abcdef0), 32), UINT64_C(0x9abcdef012345678));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x123456789abcdef0), 48), UINT64_C(0xdef0123456789abc));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x123456789abcdef0), 56), UINT64_C(0xf0123456789abcde));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x123456789abcdef0), 60), UINT64_C(0x0123456789abcdef));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x8182838485868788), 63), UINT64_C(0x40c141c242c343c4));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x8182838485868788),  1), UINT64_C(0x030507090b0d0f11));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateLeftU64(UINT64_C(0x8182838485868788), 29), UINT64_C(0x90b0d0f110305070));

    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x123456789abcdef0),  4), UINT64_C(0x0123456789abcdef));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x123456789abcdef0), 16), UINT64_C(0xdef0123456789abc));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x123456789abcdef0), 32), UINT64_C(0x9abcdef012345678));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x123456789abcdef0), 48), UINT64_C(0x56789abcdef01234));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x123456789abcdef0), 56), UINT64_C(0x3456789abcdef012));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x123456789abcdef0), 60), UINT64_C(0x23456789abcdef01));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x8182838485868788), 63), UINT64_C(0x030507090b0d0f11));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x8182838485868788),  1), UINT64_C(0x40c141c242c343c4));
    CHECK_VAL(uint64_t, "%#llx", ASMRotateRightU64(UINT64_C(0x8182838485868788), 29), UINT64_C(0x2c343c440c141c24));


    /*
     * Variable sized bitmaps:
     */

    /* bit set */
    MAP_CLEAR(p);
    ASMBitSet(&p->au32[0], 0);
    ASMBitSet(&p->au32[0], 31);
    ASMBitSet(&p->au32[0], 65);
    CHECK(p->au32[0] == 0x80000001U);
    CHECK(p->au32[2] == 0x00000002U);
    CHECK(ASMBitTestAndSet(&p->au32[0], 0)   && p->au32[0] == 0x80000001U);
    CHECK(!ASMBitTestAndSet(&p->au32[0], 16) && p->au32[0] == 0x80010001U);
    CHECK(ASMBitTestAndSet(&p->au32[0], 16)  && p->au32[0] == 0x80010001U);
    CHECK(!ASMBitTestAndSet(&p->au32[0], 80) && p->au32[2] == 0x00010002U);

    MAP_CLEAR(p);
    ASMAtomicBitSet(&p->au32[0], 0);
    ASMAtomicBitSet(&p->au32[0], 30);
    ASMAtomicBitSet(&p->au32[0], 64);
    CHECK(p->au32[0] == 0x40000001U);
    CHECK(p->au32[2] == 0x00000001U);
    CHECK(ASMAtomicBitTestAndSet(&p->au32[0], 0)   && p->au32[0] == 0x40000001U);
    CHECK(!ASMAtomicBitTestAndSet(&p->au32[0], 16) && p->au32[0] == 0x40010001U);
    CHECK(ASMAtomicBitTestAndSet(&p->au32[0], 16)  && p->au32[0] == 0x40010001U);
    CHECK(!ASMAtomicBitTestAndSet(&p->au32[0], 80) && p->au32[2] == 0x00010001U);

    /* bit clear */
    MAP_SET(p);
    ASMBitClear(&p->au32[0], 0);
    ASMBitClear(&p->au32[0], 31);
    ASMBitClear(&p->au32[0], 65);
    CHECK(p->au32[0] == ~0x80000001U);
    CHECK(p->au32[2] == ~0x00000002U);
    CHECK(!ASMBitTestAndClear(&p->au32[0], 0)   && p->au32[0] == ~0x80000001U);
    CHECK(ASMBitTestAndClear(&p->au32[0], 16)   && p->au32[0] == ~0x80010001U);
    CHECK(!ASMBitTestAndClear(&p->au32[0], 16)  && p->au32[0] == ~0x80010001U);
    CHECK(ASMBitTestAndClear(&p->au32[0], 80)   && p->au32[2] == ~0x00010002U);

    MAP_SET(p);
    ASMAtomicBitClear(&p->au32[0], 0);
    ASMAtomicBitClear(&p->au32[0], 30);
    ASMAtomicBitClear(&p->au32[0], 64);
    CHECK(p->au32[0] == ~0x40000001U);
    CHECK(p->au32[2] == ~0x00000001U);
    CHECK(!ASMAtomicBitTestAndClear(&p->au32[0], 0)   && p->au32[0] == ~0x40000001U);
    CHECK(ASMAtomicBitTestAndClear(&p->au32[0], 16)   && p->au32[0] == ~0x40010001U);
    CHECK(!ASMAtomicBitTestAndClear(&p->au32[0], 16)  && p->au32[0] == ~0x40010001U);
    CHECK(ASMAtomicBitTestAndClear(&p->au32[0], 80)   && p->au32[2] == ~0x00010001U);

    /* toggle */
    MAP_SET(p);
    ASMBitToggle(&p->au32[0], 0);
    ASMBitToggle(&p->au32[0], 31);
    ASMBitToggle(&p->au32[0], 65);
    ASMBitToggle(&p->au32[0], 47);
    ASMBitToggle(&p->au32[0], 47);
    CHECK(p->au32[0] == ~0x80000001U);
    CHECK(p->au32[2] == ~0x00000002U);
    CHECK(!ASMBitTestAndToggle(&p->au32[0], 0)   && p->au32[0] == ~0x80000000U);
    CHECK(ASMBitTestAndToggle(&p->au32[0], 0)    && p->au32[0] == ~0x80000001U);
    CHECK(ASMBitTestAndToggle(&p->au32[0], 16)   && p->au32[0] == ~0x80010001U);
    CHECK(!ASMBitTestAndToggle(&p->au32[0], 16)  && p->au32[0] == ~0x80000001U);
    CHECK(ASMBitTestAndToggle(&p->au32[0], 80)   && p->au32[2] == ~0x00010002U);

    MAP_SET(p);
    ASMAtomicBitToggle(&p->au32[0], 0);
    ASMAtomicBitToggle(&p->au32[0], 30);
    ASMAtomicBitToggle(&p->au32[0], 64);
    ASMAtomicBitToggle(&p->au32[0], 47);
    ASMAtomicBitToggle(&p->au32[0], 47);
    CHECK(p->au32[0] == ~0x40000001U);
    CHECK(p->au32[2] == ~0x00000001U);
    CHECK(!ASMAtomicBitTestAndToggle(&p->au32[0], 0)   && p->au32[0] == ~0x40000000U);
    CHECK(ASMAtomicBitTestAndToggle(&p->au32[0], 0)    && p->au32[0] == ~0x40000001U);
    CHECK(ASMAtomicBitTestAndToggle(&p->au32[0], 16)   && p->au32[0] == ~0x40010001U);
    CHECK(!ASMAtomicBitTestAndToggle(&p->au32[0], 16)  && p->au32[0] == ~0x40000001U);
    CHECK(ASMAtomicBitTestAndToggle(&p->au32[0], 80)   && p->au32[2] == ~0x00010001U);

    /* test bit. */
    for (i = 0; i < 128; i++)
    {
        MAP_SET(p);
        CHECK_BIT(ASMBitTest(&p->au32[0], i), i);
        ASMBitToggle(&p->au32[0], i);
        CHECK_BIT(!ASMBitTest(&p->au32[0], i), i);
        CHECK_BIT(!ASMBitTestAndToggle(&p->au32[0], i), i);
        CHECK_BIT(ASMBitTest(&p->au32[0], i), i);
        CHECK_BIT(ASMBitTestAndToggle(&p->au32[0], i), i);
        CHECK_BIT(!ASMBitTest(&p->au32[0], i), i);

        MAP_SET(p);
        CHECK_BIT(ASMBitTest(&p->au32[0], i), i);
        ASMAtomicBitToggle(&p->au32[0], i);
        CHECK_BIT(!ASMBitTest(&p->au32[0], i), i);
        CHECK_BIT(!ASMAtomicBitTestAndToggle(&p->au32[0], i), i);
        CHECK_BIT(ASMBitTest(&p->au32[0], i), i);
        CHECK_BIT(ASMAtomicBitTestAndToggle(&p->au32[0], i), i);
        CHECK_BIT(!ASMBitTest(&p->au32[0], i), i);
    }

    /* bit searching */
    MAP_SET(p);
    CHECK_VAL(int32_t, "%d", ASMBitFirstClear(&p->au32[0], sizeof(p->au32) * 8), -1);
    CHECK_VAL(int32_t, "%d", ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8), 0);

    ASMBitClear(&p->au32[0], 1);
    CHECK_VAL(int32_t, "%d", ASMBitFirstClear(&p->au32[0], sizeof(p->au32) * 8), 1);
    CHECK_VAL(int32_t, "%d", ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8), 0);

    MAP_SET(p);
    ASMBitClear(&p->au32[0], 95);
    CHECK_VAL(int32_t, "%d", ASMBitFirstClear(&p->au32[0], sizeof(p->au32) * 8), 95);
    CHECK_VAL(int32_t, "%d", ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8), 0);

    MAP_SET(p);
    ASMBitClear(&p->au32[0], 127);
    CHECK(ASMBitFirstClear(&p->au32[0], sizeof(p->au32) * 8) == 127);
    CHECK(ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8) == 0);
    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 0) == 1);
    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 1) == 2);
    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 2) == 3);


    MAP_SET(p);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, 0) == -1);
    ASMBitClear(&p->au32[0], 32);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, 32) == -1);
    ASMBitClear(&p->au32[0], 88);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8,  57) ==  88);

    MAP_SET(p);
    ASMBitClear(&p->au32[0], 31);
    ASMBitClear(&p->au32[0], 57);
    ASMBitClear(&p->au32[0], 88);
    ASMBitClear(&p->au32[0], 101);
    ASMBitClear(&p->au32[0], 126);
    ASMBitClear(&p->au32[0], 127);
    CHECK(ASMBitFirstClear(&p->au32[0], sizeof(p->au32) * 8) == 31);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8,  31) ==  57);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8,  57) ==  88);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8,  88) == 101);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, 101) == 126);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, 126) == 127);
    CHECK(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, 127) == -1);

    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 29) == 30);
    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 30) == 32);

    MAP_CLEAR(p);
    for (i = 1; i < 128; i++)
        CHECK_BIT(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, i - 1) == i, i);
    for (i = 0; i < 128; i++)
    {
        MAP_SET(p);
        ASMBitClear(&p->au32[0], i);
        CHECK_BIT(ASMBitFirstClear(&p->au32[0], sizeof(p->au32) * 8) == i, i);
        for (j = 0; j < i; j++)
            CHECK_BIT(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, j) == i, i);
        for (j = i; j < 128; j++)
            CHECK_BIT(ASMBitNextClear(&p->au32[0], sizeof(p->au32) * 8, j) == -1, i);
    }

    /* clear range. */
    MAP_SET(p);
    ASMBitClearRange(&p->au32, 0, 128);
    CHECK(!p->au32[0] && !p->au32[1] && !p->au32[2] && !p->au32[3]);
    for (i = 0; i < 128; i++)
    {
        for (j = i + 1; j <= 128; j++)
        {
            MAP_SET(p);
            ASMBitClearRange(&p->au32, i, j);
            for (k = 0; k < i; k++)
                CHECK_BIT3(ASMBitTest(&p->au32[0], k), i, j, k);
            for (k = i; k < j; k++)
                CHECK_BIT3(!ASMBitTest(&p->au32[0], k), i, j, k);
            for (k = j; k < 128; k++)
                CHECK_BIT3(ASMBitTest(&p->au32[0], k), i, j, k);
        }
    }

    /* set range. */
    MAP_CLEAR(p);
    ASMBitSetRange(&p->au32[0], 0, 5);
    ASMBitSetRange(&p->au32[0], 6, 44);
    ASMBitSetRange(&p->au32[0], 64, 65);
    CHECK(p->au32[0] == UINT32_C(0xFFFFFFDF));
    CHECK(p->au32[1] == UINT32_C(0x00000FFF));
    CHECK(p->au32[2] == UINT32_C(0x00000001));

    MAP_CLEAR(p);
    ASMBitSetRange(&p->au32[0], 0, 1);
    ASMBitSetRange(&p->au32[0], 62, 63);
    ASMBitSetRange(&p->au32[0], 63, 64);
    ASMBitSetRange(&p->au32[0], 127, 128);
    CHECK(p->au32[0] == UINT32_C(0x00000001) && p->au32[1] == UINT32_C(0xC0000000));
    CHECK(p->au32[2] == UINT32_C(0x00000000) && p->au32[3] == UINT32_C(0x80000000));

    MAP_CLEAR(p);
    ASMBitSetRange(&p->au32, 0, 128);
    CHECK(!~p->au32[0] && !~p->au32[1] && !~p->au32[2] && !~p->au32[3]);
    for (i = 0; i < 128; i++)
    {
        for (j = i + 1; j <= 128; j++)
        {
            MAP_CLEAR(p);
            ASMBitSetRange(&p->au32, i, j);
            for (k = 0; k < i; k++)
                CHECK_BIT3(!ASMBitTest(&p->au32[0], k), i, j, k);
            for (k = i; k < j; k++)
                CHECK_BIT3(ASMBitTest(&p->au32[0], k), i, j, k);
            for (k = j; k < 128; k++)
                CHECK_BIT3(!ASMBitTest(&p->au32[0], k), i, j, k);
        }
    }

    /* searching for set bits. */
    MAP_CLEAR(p);
    CHECK(ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8) == -1);

    ASMBitSet(&p->au32[0], 65);
    CHECK(ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8) == 65);
    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 65) == -1);
    for (i = 0; i < 65; i++)
        CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, i) == 65);
    for (i = 65; i < 128; i++)
        CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, i) == -1);

    ASMBitSet(&p->au32[0], 17);
    CHECK(ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8) == 17);
    CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, 17) == 65);
    for (i = 0; i < 16; i++)
        CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, i) == 17);
    for (i = 17; i < 65; i++)
        CHECK(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, i) == 65);

    MAP_SET(p);
    for (i = 1; i < 128; i++)
        CHECK_BIT(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, i - 1) == i, i);
    for (i = 0; i < 128; i++)
    {
        MAP_CLEAR(p);
        ASMBitSet(&p->au32[0], i);
        CHECK_BIT(ASMBitFirstSet(&p->au32[0], sizeof(p->au32) * 8) == i, i);
        for (j = 0; j < i; j++)
            CHECK_BIT(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, j) == i, i);
        for (j = i; j < 128; j++)
            CHECK_BIT(ASMBitNextSet(&p->au32[0], sizeof(p->au32) * 8, j) == -1, i);
    }

    /*
     * Special tests.
     */
    test2(hTest);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

