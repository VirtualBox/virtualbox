/* $Id: tstRand.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Testcase for the RTRand API.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/rand.h>
#include <iprt/errcore.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TSTMEMAUTOPTRSTRUCT
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
} TSTMEMAUTOPTRSTRUCT;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CHECK_EXPR(expr) \
    do { bool const f = !!(expr); if (RT_UNLIKELY(!f)) { RTPrintf("tstRand(%d): %s!\n", __LINE__, #expr); g_cErrors++; } } while (0)
#define CHECK_EXPR_MSG(expr, msg) \
    do { \
        bool const f = !!(expr); \
        if (RT_UNLIKELY(!f)) { \
            RTPrintf("tstRand(%d): %s!\n", __LINE__, #expr); \
            RTPrintf("tstRand: "); \
            RTPrintf msg; \
            if (++g_cErrors > 25) return 1; \
        } \
    } while (0)


#define TST_RAND_SAMPLE_RANGES  16


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static unsigned g_cErrors = 0;


static void tstRandCheckDist(uint32_t *pacHits, unsigned iTest)
{
    RTPrintf("tstRand:");
    uint32_t iMin = UINT32_MAX;
    uint32_t iMax = 0;
    uint32_t iAvg = 0;
    for (unsigned iRange = 0; iRange < TST_RAND_SAMPLE_RANGES; iRange++)
    {
        RTPrintf(" %04RX32", pacHits[iRange]);
        if (iMax < pacHits[iRange])
            iMax = pacHits[iRange];
        if (iMin > pacHits[iRange])
            iMin = pacHits[iRange];
        iAvg += pacHits[iRange];
    }
    iAvg /= TST_RAND_SAMPLE_RANGES;
    RTPrintf(" min=%RX32 (%%%d) max=%RX32 (%%%d) calc avg=%RX32 [test=%d]\n",
             iMin, (iAvg - iMin) * 100 / iAvg, iMax,
             (iMax - iAvg) * 100 / iAvg,
             iAvg,
             iTest);
    CHECK_EXPR(iMin >= iAvg - iAvg / 4);
    CHECK_EXPR(iMax <= iAvg + iAvg / 4);
}


static int tstRandAdv(RTRAND hRand)
{
    /*
     * Test distribution.
     */
#if 1
    /* unsigned 32-bit */
    static const struct
    {
        uint32_t u32First;
        uint32_t u32Last;
    } s_aU32Tests[] =
    {
        { 0, UINT32_MAX },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 4 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 8 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 16 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 64 },
        { 0, UINT32_MAX / 2 },
        { UINT32_MAX / 4, UINT32_MAX / 4 * 3 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { 1234, 1234 + TST_RAND_SAMPLE_RANGES - 1 },
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aU32Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        uint32_t const uFirst = s_aU32Tests[iTest].u32First;
        uint32_t const uLast  = s_aU32Tests[iTest].u32Last;
        uint32_t const uRange = uLast - uFirst; Assert(uLast >= uFirst);
        uint32_t const uDivisor = uRange / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand:   TESTING RTRandAdvU32Ex(,%#RX32, %#RX32) distribution... [div=%#RX32 range=%#RX32]\n", uFirst, uLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            uint32_t uRand = RTRandAdvU32Ex(hRand, uFirst, uLast);
            CHECK_EXPR_MSG(uRand >= uFirst, ("%#RX32 %#RX32\n", uRand, uFirst));
            CHECK_EXPR_MSG(uRand <= uLast,  ("%#RX32 %#RX32\n", uRand, uLast));
            uint32_t off = uRand - uFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

#if 1
    /* unsigned 64-bit */
    static const struct
    {
        uint64_t u64First;
        uint64_t u64Last;
    } s_aU64Tests[] =
    {
        { 0, UINT64_MAX },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 4 },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 8 },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 16 },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 64 },
        { 0, UINT64_MAX / 2 },
        { UINT64_MAX / 4, UINT64_MAX / 4 * 3 },
        { 0, UINT32_MAX },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 4 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 8 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 16 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 64 },
        { 0, UINT32_MAX / 2 },
        { UINT32_MAX / 4, UINT32_MAX / 4 * 3 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { 1234, 1234 + TST_RAND_SAMPLE_RANGES - 1 },
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aU64Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        uint64_t const uFirst = s_aU64Tests[iTest].u64First;
        uint64_t const uLast  = s_aU64Tests[iTest].u64Last;
        uint64_t const uRange = uLast - uFirst;  Assert(uLast >= uFirst);
        uint64_t const uDivisor = uRange / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand:   TESTING RTRandAdvU64Ex(,%#RX64, %#RX64) distribution... [div=%#RX64 range=%#RX64]\n", uFirst, uLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            uint64_t uRand = RTRandAdvU64Ex(hRand, uFirst, uLast);
            CHECK_EXPR_MSG(uRand >= uFirst, ("%#RX64 %#RX64\n", uRand, uFirst));
            CHECK_EXPR_MSG(uRand <= uLast,  ("%#RX64 %#RX64\n", uRand, uLast));
            uint64_t off = uRand - uFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

#if 1
    /* signed 32-bit */
    static const struct
    {
        int32_t i32First;
        int32_t i32Last;
    } s_aS32Tests[] =
    {
        { -429496729, 429496729 },
        { INT32_MIN, INT32_MAX },
        { INT32_MIN, INT32_MAX / 2 },
        { -0x20000000, INT32_MAX },
        { -0x10000000, INT32_MAX },
        { -0x08000000, INT32_MAX },
        { -0x00800000, INT32_MAX },
        { -0x00080000, INT32_MAX },
        { -0x00008000, INT32_MAX },
        { -0x00000800, INT32_MAX },
        { 2, INT32_MAX / 2 },
        { 4000000, INT32_MAX / 2 },
        { -4000000, INT32_MAX / 2 },
        { INT32_MIN / 2, INT32_MAX / 2 },
        { INT32_MIN / 3, INT32_MAX / 2 },
        { INT32_MIN / 3, INT32_MAX / 3 },
        { INT32_MIN / 3, INT32_MAX / 4 },
        { INT32_MIN / 4, INT32_MAX / 4 },
        { INT32_MIN / 5, INT32_MAX / 5 },
        { INT32_MIN / 6, INT32_MAX / 6 },
        { INT32_MIN / 7, INT32_MAX / 6 },
        { INT32_MIN / 7, INT32_MAX / 7 },
        { INT32_MIN / 7, INT32_MAX / 8 },
        { INT32_MIN / 8, INT32_MAX / 8 },
        { INT32_MIN / 9, INT32_MAX / 9 },
        { INT32_MIN / 9, INT32_MAX / 12 },
        { INT32_MIN / 12, INT32_MAX / 12 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { -TST_RAND_SAMPLE_RANGES / 2, TST_RAND_SAMPLE_RANGES / 2 - 1 },
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aS32Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        int32_t const  iFirst = s_aS32Tests[iTest].i32First;
        int32_t const  iLast  = s_aS32Tests[iTest].i32Last;
        uint32_t const uRange = iLast - iFirst; AssertMsg(iLast >= iFirst, ("%d\n", iTest));
        uint32_t const uDivisor = (uRange ? uRange : UINT32_MAX) / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand:   TESTING RTRandAdvS32Ex(,%#RI32, %#RI32) distribution... [div=%#RX32 range=%#RX32]\n", iFirst, iLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            int32_t iRand = RTRandAdvS32Ex(hRand, iFirst, iLast);
            CHECK_EXPR_MSG(iRand >= iFirst, ("%#RI32 %#RI32\n", iRand, iFirst));
            CHECK_EXPR_MSG(iRand <= iLast,  ("%#RI32 %#RI32\n", iRand, iLast));
            uint32_t off = iRand - iFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

#if 1
    /* signed 64-bit */
    static const struct
    {
        int64_t i64First;
        int64_t i64Last;
    } s_aS64Tests[] =
    {
        { INT64_MIN, INT64_MAX },
        { INT64_MIN, INT64_MAX / 2 },
        { INT64_MIN / 2, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 4, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 8, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 16, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 64, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 64, INT64_MAX / 2 + INT64_MAX / 64 },
        { INT64_MIN / 2, INT64_MAX / 2 + INT64_MAX / 64 },
        { INT64_MIN / 2, INT64_MAX / 2 + INT64_MAX / 8 },
        { INT64_MIN / 2, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 4, INT64_MAX / 2 - INT64_MAX / 4 },
        { INT64_MIN / 2 - INT64_MIN / 4, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 8, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 16, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 16, INT64_MAX / 2 - INT64_MAX / 16 },
        { INT64_MIN / 2 - INT64_MIN / 32, INT64_MAX / 2 - INT64_MAX / 16 },
        { INT64_MIN / 2 - INT64_MIN / 32, INT64_MAX / 2 - INT64_MAX / 32 },
        { INT64_MIN / 2 - INT64_MIN / 64, INT64_MAX / 2 - INT64_MAX / 64 },
        { INT64_MIN / 2 - INT64_MIN / 8, INT64_MAX / 2 },
        { INT64_MIN / 4, INT64_MAX / 4 },
        { INT64_MIN / 5, INT64_MAX / 5 },
        { INT64_MIN / 6, INT64_MAX / 6 },
        { INT64_MIN / 7, INT64_MAX / 7 },
        { INT64_MIN / 8, INT64_MAX / 8 },
        { INT32_MIN, INT32_MAX },
        { INT32_MIN, INT32_MAX / 2 },
        { -0x20000000, INT32_MAX },
        { -0x10000000, INT32_MAX },
        { -0x7f000000, INT32_MAX },
        { -0x08000000, INT32_MAX },
        { -0x00800000, INT32_MAX },
        { -0x00080000, INT32_MAX },
        { -0x00008000, INT32_MAX },
        { 2, INT32_MAX / 2 },
        { 4000000, INT32_MAX / 2 },
        { -4000000, INT32_MAX / 2 },
        { INT32_MIN / 2, INT32_MAX / 2 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { -TST_RAND_SAMPLE_RANGES / 2, TST_RAND_SAMPLE_RANGES / 2 - 1 }
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aS64Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        int64_t const  iFirst = s_aS64Tests[iTest].i64First;
        int64_t const  iLast  = s_aS64Tests[iTest].i64Last;
        uint64_t const uRange = iLast - iFirst; AssertMsg(iLast >= iFirst, ("%d\n", iTest));
        uint64_t const uDivisor = (uRange ? uRange : UINT64_MAX) / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand:   TESTING RTRandAdvS64Ex(,%#RI64, %#RI64) distribution... [div=%#RX64 range=%#016RX64]\n", iFirst, iLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            int64_t iRand = RTRandAdvS64Ex(hRand, iFirst, iLast);
            CHECK_EXPR_MSG(iRand >= iFirst, ("%#RI64 %#RI64\n", iRand, iFirst));
            CHECK_EXPR_MSG(iRand <= iLast,  ("%#RI64 %#RI64\n", iRand, iLast));
            uint64_t off = iRand - iFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

    /*
     * Test saving and restoring the state.
     */
    RTPrintf("tstRand:   TESTING RTRandAdvSave/RestoreSave\n");
    char szState[256];
    size_t cbState = sizeof(szState);
    int rc = RTRandAdvSaveState(hRand, szState, &cbState);
    if (rc != VERR_NOT_SUPPORTED)
    {
        CHECK_EXPR_MSG(rc == VINF_SUCCESS,  ("RTRandAdvSaveState(%p,,256) -> %Rrc (%d)\n", (uintptr_t)hRand, rc, rc));
        uint32_t const u32A1 = RTRandAdvU32(hRand);
        uint32_t const u32B1 = RTRandAdvU32(hRand);
        RTPrintf("tstRand:   state:\"%s\"  A=%RX32 B=%RX32\n", szState, u32A1, u32B1);

        rc = RTRandAdvRestoreState(hRand, szState);
        CHECK_EXPR_MSG(rc == VINF_SUCCESS,  ("RTRandAdvRestoreState(%p,\"%s\") -> %Rrc (%d)\n", (uintptr_t)hRand, szState, rc, rc));
        uint32_t const u32A2 = RTRandAdvU32(hRand);
        uint32_t const u32B2 = RTRandAdvU32(hRand);
        CHECK_EXPR_MSG(u32A1 == u32A2, ("u32A1=%RX32 u32A2=%RX32\n", u32A1, u32A2));
        CHECK_EXPR_MSG(u32B1 == u32B2, ("u32B1=%RX32 u32B2=%RX32\n", u32B1, u32B2));
    }
    else
    {
        szState[0] = '\0';
        rc = RTRandAdvRestoreState(hRand, szState);
        CHECK_EXPR_MSG(rc == VERR_NOT_SUPPORTED,  ("RTRandAdvRestoreState(%p,\"\") -> %Rrc (%d)\n", (uintptr_t)hRand, rc, rc));
    }


    /*
     * Destroy it.
     */
    rc = RTRandAdvDestroy(hRand);
    CHECK_EXPR_MSG(rc == VINF_SUCCESS,  ("RTRandAdvDestroy(%p) -> %Rrc (%d)\n", (uintptr_t)hRand, rc, rc));

    return 0;
}



int main()
{
    RTR3InitExeNoArguments(0);
    RTPrintf("tstRand: TESTING...\n");

    /*
     * Do some smoke tests first?
     */
    /** @todo RTRand smoke testing. */

#if 1
    /*
     * Test distribution.
     */
#if 1
    /* unsigned 32-bit */
    static const struct
    {
        uint32_t u32First;
        uint32_t u32Last;
    } s_aU32Tests[] =
    {
        { 0, UINT32_MAX },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 4 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 8 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 16 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 64 },
        { 0, UINT32_MAX / 2 },
        { UINT32_MAX / 4, UINT32_MAX / 4 * 3 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { 1234, 1234 + TST_RAND_SAMPLE_RANGES - 1 },
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aU32Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        uint32_t const uFirst = s_aU32Tests[iTest].u32First;
        uint32_t const uLast  = s_aU32Tests[iTest].u32Last;
        uint32_t const uRange = uLast - uFirst; Assert(uLast >= uFirst);
        uint32_t const uDivisor = uRange / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand: TESTING RTRandU32Ex(%#RX32, %#RX32) distribution... [div=%#RX32 range=%#RX32]\n", uFirst, uLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            uint32_t uRand = RTRandU32Ex(uFirst, uLast);
            CHECK_EXPR_MSG(uRand >= uFirst, ("%#RX32 %#RX32\n", uRand, uFirst));
            CHECK_EXPR_MSG(uRand <= uLast,  ("%#RX32 %#RX32\n", uRand, uLast));
            uint32_t off = uRand - uFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

#if 1
    /* unsigned 64-bit */
    static const struct
    {
        uint64_t u64First;
        uint64_t u64Last;
    } s_aU64Tests[] =
    {
        { 0, UINT64_MAX },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 4 },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 8 },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 16 },
        { 0, UINT64_MAX / 2 + UINT64_MAX / 64 },
        { 0, UINT64_MAX / 2 },
        { UINT64_MAX / 4, UINT64_MAX / 4 * 3 },
        { 0, UINT32_MAX },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 4 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 8 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 16 },
        { 0, UINT32_MAX / 2 + UINT32_MAX / 64 },
        { 0, UINT32_MAX / 2 },
        { UINT32_MAX / 4, UINT32_MAX / 4 * 3 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { 1234, 1234 + TST_RAND_SAMPLE_RANGES - 1 },
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aU64Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        uint64_t const uFirst = s_aU64Tests[iTest].u64First;
        uint64_t const uLast  = s_aU64Tests[iTest].u64Last;
        uint64_t const uRange = uLast - uFirst;  Assert(uLast >= uFirst);
        uint64_t const uDivisor = uRange / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand: TESTING RTRandU64Ex(%#RX64, %#RX64) distribution... [div=%#RX64 range=%#RX64]\n", uFirst, uLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            uint64_t uRand = RTRandU64Ex(uFirst, uLast);
            CHECK_EXPR_MSG(uRand >= uFirst, ("%#RX64 %#RX64\n", uRand, uFirst));
            CHECK_EXPR_MSG(uRand <= uLast,  ("%#RX64 %#RX64\n", uRand, uLast));
            uint64_t off = uRand - uFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

#if 1
    /* signed 32-bit */
    static const struct
    {
        int32_t i32First;
        int32_t i32Last;
    } s_aS32Tests[] =
    {
        { -429496729, 429496729 },
        { INT32_MIN, INT32_MAX },
        { INT32_MIN, INT32_MAX / 2 },
        { -0x20000000, INT32_MAX },
        { -0x10000000, INT32_MAX },
        { -0x08000000, INT32_MAX },
        { -0x00800000, INT32_MAX },
        { -0x00080000, INT32_MAX },
        { -0x00008000, INT32_MAX },
        { -0x00000800, INT32_MAX },
        { 2, INT32_MAX / 2 },
        { 4000000, INT32_MAX / 2 },
        { -4000000, INT32_MAX / 2 },
        { INT32_MIN / 2, INT32_MAX / 2 },
        { INT32_MIN / 3, INT32_MAX / 2 },
        { INT32_MIN / 3, INT32_MAX / 3 },
        { INT32_MIN / 3, INT32_MAX / 4 },
        { INT32_MIN / 4, INT32_MAX / 4 },
        { INT32_MIN / 5, INT32_MAX / 5 },
        { INT32_MIN / 6, INT32_MAX / 6 },
        { INT32_MIN / 7, INT32_MAX / 6 },
        { INT32_MIN / 7, INT32_MAX / 7 },
        { INT32_MIN / 7, INT32_MAX / 8 },
        { INT32_MIN / 8, INT32_MAX / 8 },
        { INT32_MIN / 9, INT32_MAX / 9 },
        { INT32_MIN / 9, INT32_MAX / 12 },
        { INT32_MIN / 12, INT32_MAX / 12 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { -TST_RAND_SAMPLE_RANGES / 2, TST_RAND_SAMPLE_RANGES / 2 - 1 },
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aS32Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        int32_t const  iFirst = s_aS32Tests[iTest].i32First;
        int32_t const  iLast  = s_aS32Tests[iTest].i32Last;
        uint32_t const uRange = iLast - iFirst; AssertMsg(iLast >= iFirst, ("%d\n", iTest));
        uint32_t const uDivisor = (uRange ? uRange : UINT32_MAX) / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand: TESTING RTRandS32Ex(%#RI32, %#RI32) distribution... [div=%#RX32 range=%#RX32]\n", iFirst, iLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            int32_t iRand = RTRandS32Ex(iFirst, iLast);
            CHECK_EXPR_MSG(iRand >= iFirst, ("%#RI32 %#RI32\n", iRand, iFirst));
            CHECK_EXPR_MSG(iRand <= iLast,  ("%#RI32 %#RI32\n", iRand, iLast));
            uint32_t off = iRand - iFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif

#if 1
    /* signed 64-bit */
    static const struct
    {
        int64_t i64First;
        int64_t i64Last;
    } s_aS64Tests[] =
    {
        { INT64_MIN, INT64_MAX },
        { INT64_MIN, INT64_MAX / 2 },
        { INT64_MIN / 2, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 4, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 8, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 16, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 64, INT64_MAX / 2 },
        { INT64_MIN / 2 + INT64_MIN / 64, INT64_MAX / 2 + INT64_MAX / 64 },
        { INT64_MIN / 2, INT64_MAX / 2 + INT64_MAX / 64 },
        { INT64_MIN / 2, INT64_MAX / 2 + INT64_MAX / 8 },
        { INT64_MIN / 2, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 4, INT64_MAX / 2 - INT64_MAX / 4 },
        { INT64_MIN / 2 - INT64_MIN / 4, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 8, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 16, INT64_MAX / 2 - INT64_MAX / 8 },
        { INT64_MIN / 2 - INT64_MIN / 16, INT64_MAX / 2 - INT64_MAX / 16 },
        { INT64_MIN / 2 - INT64_MIN / 32, INT64_MAX / 2 - INT64_MAX / 16 },
        { INT64_MIN / 2 - INT64_MIN / 32, INT64_MAX / 2 - INT64_MAX / 32 },
        { INT64_MIN / 2 - INT64_MIN / 64, INT64_MAX / 2 - INT64_MAX / 64 },
        { INT64_MIN / 2 - INT64_MIN / 8, INT64_MAX / 2 },
        { INT64_MIN / 4, INT64_MAX / 4 },
        { INT64_MIN / 5, INT64_MAX / 5 },
        { INT64_MIN / 6, INT64_MAX / 6 },
        { INT64_MIN / 7, INT64_MAX / 7 },
        { INT64_MIN / 8, INT64_MAX / 8 },
        { INT32_MIN, INT32_MAX },
        { INT32_MIN, INT32_MAX / 2 },
        { -0x20000000, INT32_MAX },
        { -0x10000000, INT32_MAX },
        { -0x7f000000, INT32_MAX },
        { -0x08000000, INT32_MAX },
        { -0x00800000, INT32_MAX },
        { -0x00080000, INT32_MAX },
        { -0x00008000, INT32_MAX },
        { 2, INT32_MAX / 2 },
        { 4000000, INT32_MAX / 2 },
        { -4000000, INT32_MAX / 2 },
        { INT32_MIN / 2, INT32_MAX / 2 },
        { 0, TST_RAND_SAMPLE_RANGES - 1 },
        { -TST_RAND_SAMPLE_RANGES / 2, TST_RAND_SAMPLE_RANGES / 2 - 1 }
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(s_aS64Tests); iTest++)
    {
        uint32_t       acHits[TST_RAND_SAMPLE_RANGES] = {0};
        int64_t const  iFirst = s_aS64Tests[iTest].i64First;
        int64_t const  iLast  = s_aS64Tests[iTest].i64Last;
        uint64_t const uRange = iLast - iFirst; AssertMsg(iLast >= iFirst, ("%d\n", iTest));
        uint64_t const uDivisor = (uRange ? uRange : UINT64_MAX) / TST_RAND_SAMPLE_RANGES + 1;
        RTPrintf("tstRand: TESTING RTRandS64Ex(%#RI64, %#RI64) distribution... [div=%#RX64 range=%#016RX64]\n", iFirst, iLast, uDivisor, uRange);
        for (unsigned iSample = 0; iSample < TST_RAND_SAMPLE_RANGES * 10240; iSample++)
        {
            int64_t iRand = RTRandS64Ex(iFirst, iLast);
            CHECK_EXPR_MSG(iRand >= iFirst, ("%#RI64 %#RI64\n", iRand, iFirst));
            CHECK_EXPR_MSG(iRand <= iLast,  ("%#RI64 %#RI64\n", iRand, iLast));
            uint64_t off = iRand - iFirst;
            acHits[off / uDivisor]++;
        }
        tstRandCheckDist(acHits, iTest);
    }
#endif
#endif /* Testing RTRand */

#if 1
    /*
     * Test the various random generators.
     */
    RTPrintf("tstRand: TESTING RTRandAdvCreateParkerMiller\n");
    RTRAND hRand;
    int rc = RTRandAdvCreateParkMiller(&hRand);
    CHECK_EXPR_MSG(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc));
    if (RT_SUCCESS(rc))
        if (tstRandAdv(hRand))
            return 1;

#endif /* Testing RTRandAdv */

    /*
     * Summary.
     */
    if (!g_cErrors)
        RTPrintf("tstRand: SUCCESS\n");
    else
        RTPrintf("tstRand: FAILED - %d errors\n", g_cErrors);
    return !!g_cErrors;
}

