/* $Id: tstRTNoCrt-5.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Testcase for the No-CRT 64-bit integer support.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <iprt/uint64.h>
#include <iprt/test.h>
#include <iprt/string.h>
#include <iprt/rand.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef DEBUG
# define RANDOM_LOOPS        _256K
#else
# define RANDOM_LOOPS        _1M
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TSTRTNOCRT5SHIFT
{
    uint64_t    uValue;
    uint8_t     cShift;
    uint64_t    uExpected;
} TSTRTNOCRT5SHIFT;

typedef struct TSTRTNOCRT5MULT
{
    uint64_t    uFactor1, uFactor2;
    uint64_t    uExpected;
} TSTRTNOCRT5MULT;

typedef struct TSTRTNOCRT5DIV
{
    uint64_t    uDividend, uDivisor;
    uint64_t    uQuotient, uRemainder;
} TSTRTNOCRT5DIV;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
RTTEST  g_hTest;

TSTRTNOCRT5SHIFT volatile const g_aShiftRight[] =
{
    { UINT64_C(0x8e7e6e5e4e3e2e1e),   0, UINT64_C(0x8e7e6e5e4e3e2e1e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),   8, UINT64_C(0x008e7e6e5e4e3e2e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  16, UINT64_C(0x00008e7e6e5e4e3e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  24, UINT64_C(0x0000008e7e6e5e4e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  28, UINT64_C(0x00000008e7e6e5e4) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  32, UINT64_C(0x000000008e7e6e5e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  36, UINT64_C(0x0000000008e7e6e5) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  40, UINT64_C(0x00000000008e7e6e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  44, UINT64_C(0x000000000008e7e6) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  48, UINT64_C(0x0000000000008e7e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  52, UINT64_C(0x00000000000008e7) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  56, UINT64_C(0x000000000000008e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  60, UINT64_C(0x0000000000000008) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  64, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  65, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  99, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e), 127, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e), 132, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e), 255, UINT64_C(0x0000000000000000) },
};


static void tstShiftRight()
{
    RTTestSub(g_hTest, "64-bit unsigned shift right");

    /* static tests from array. */
    for (size_t i = 0; i < RT_ELEMENTS(g_aShiftRight); i++)
    {
        uint64_t const uResult = g_aShiftRight[i].uValue >> g_aShiftRight[i].cShift;
        if (uResult != g_aShiftRight[i].uExpected)
            RTTestFailed(g_hTest, "i=%u uValue=%#018RX64 SHR %u => %#018RX64, expected %#018RX64",
                         i, g_aShiftRight[i].uValue, g_aShiftRight[i].cShift, uResult, g_aShiftRight[i].uExpected);
    }

    /* Random values via uint64. */
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        uint64_t const uValue  = RTRandU64();
        uint8_t const  cShift  = (uint8_t)RTRandU32Ex(0, i & 3 ? 63 : 255);
        uint64_t const uResult = uValue >> cShift;
        RTUINT64U uExpected = { uValue };
        if (cShift <= 63)
            RTUInt64AssignShiftRight(&uExpected, cShift);
        else
            uExpected.u = 0;
        if (uResult != uExpected.u)
            RTTestFailed(g_hTest, "uValue=%#018RX64 SHR %u => %#018RX64, expected %#018RX64", uValue, cShift, uResult, uExpected.u);
    }
}


TSTRTNOCRT5SHIFT volatile const g_aShiftSignedRight[] =
{
    { UINT64_C(0x8e7e6e5e4e3e2e1e),   0, UINT64_C(0x8e7e6e5e4e3e2e1e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),   8, UINT64_C(0xff8e7e6e5e4e3e2e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  16, UINT64_C(0xffff8e7e6e5e4e3e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  24, UINT64_C(0xffffff8e7e6e5e4e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  28, UINT64_C(0xfffffff8e7e6e5e4) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  32, UINT64_C(0xffffffff8e7e6e5e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  36, UINT64_C(0xfffffffff8e7e6e5) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  40, UINT64_C(0xffffffffff8e7e6e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  44, UINT64_C(0xfffffffffff8e7e6) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  48, UINT64_C(0xffffffffffff8e7e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  52, UINT64_C(0xfffffffffffff8e7) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  56, UINT64_C(0xffffffffffffff8e) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  60, UINT64_C(0xfffffffffffffff8) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  64, UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  65, UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e),  99, UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e), 127, UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e), 132, UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x8e7e6e5e4e3e2e1e), 255, UINT64_C(0xffffffffffffffff) },

    { UINT64_C(0x7e8e6e5e4e3e2e1e),   0, UINT64_C(0x7e8e6e5e4e3e2e1e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),   8, UINT64_C(0x007e8e6e5e4e3e2e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  16, UINT64_C(0x00007e8e6e5e4e3e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  24, UINT64_C(0x0000007e8e6e5e4e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  28, UINT64_C(0x00000007e8e6e5e4) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  32, UINT64_C(0x000000007e8e6e5e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  36, UINT64_C(0x0000000007e8e6e5) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  40, UINT64_C(0x00000000007e8e6e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  44, UINT64_C(0x000000000007e8e6) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  48, UINT64_C(0x0000000000007e8e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  52, UINT64_C(0x00000000000007e8) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  56, UINT64_C(0x000000000000007e) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  60, UINT64_C(0x0000000000000007) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  64, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  65, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e),  99, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e), 127, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e), 132, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x7e8e6e5e4e3e2e1e), 255, UINT64_C(0x0000000000000000) },
};

static void tstShiftRightArithmetic()
{
    RTTestSub(g_hTest, "64-bit signed shift right");

    /* static tests from array. */
    for (size_t i = 0; i < RT_ELEMENTS(g_aShiftSignedRight); i++)
    {
        int64_t const iResult = (int64_t)g_aShiftSignedRight[i].uValue >> g_aShiftSignedRight[i].cShift;
        if ((uint64_t)iResult != g_aShiftSignedRight[i].uExpected)
            RTTestFailed(g_hTest, "i=%u iValue=%#018RX64 SAR %u => %#018RX64, expected %#018RX64", i, g_aShiftSignedRight[i].uValue,
                         g_aShiftSignedRight[i].cShift, iResult, g_aShiftSignedRight[i].uExpected);
    }

    /* Random values via uint64. */
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        uint64_t const uValue  = RTRandU64();
        uint8_t const  cShift  = (uint8_t)RTRandU32Ex(0, i & 3 ? 63 : 255);
        int64_t const  iResult = (int64_t)uValue >> cShift;
        RTUINT64U uExpected = { uValue };
        if (cShift > 63)
            uExpected.u = RT_BIT_64(63) & uValue ? UINT64_MAX : 0;
        else
        {
            RTUInt64AssignShiftRight(&uExpected, cShift);
            if (RT_BIT_64(63) & uValue)
                uExpected.u |= UINT64_MAX << (63 - cShift);
        }
        if ((uint64_t)iResult != uExpected.u)
            RTTestFailed(g_hTest, "uValue=%#018RX64 SHR %u => %#018RX64, expected %#018RX64", uValue, cShift, iResult, uExpected.u);
    }
}

TSTRTNOCRT5SHIFT volatile const g_aShiftLeft[] =
{
    { UINT64_C(0x8e7d6c5e4a3e2b1e),   0, UINT64_C(0x8e7d6c5e4a3e2b1e) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),   8, UINT64_C(0x7d6c5e4a3e2b1e00) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  16, UINT64_C(0x6c5e4a3e2b1e0000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  24, UINT64_C(0x5e4a3e2b1e000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  28, UINT64_C(0xe4a3e2b1e0000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  32, UINT64_C(0x4a3e2b1e00000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  36, UINT64_C(0xa3e2b1e000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  40, UINT64_C(0x3e2b1e0000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  44, UINT64_C(0xe2b1e00000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  48, UINT64_C(0x2b1e000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  52, UINT64_C(0xb1e0000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  56, UINT64_C(0x1e00000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  60, UINT64_C(0xe000000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  64, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  65, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e),  99, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e), 127, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e), 132, UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8e7d6c5e4a3e2b1e), 255, UINT64_C(0x0000000000000000) },
};

static void tstShiftLeft()
{
    RTTestSub(g_hTest, "64-bit shift left");

    /* static tests from array. */
    for (size_t i = 0; i < RT_ELEMENTS(g_aShiftLeft); i++)
    {
        uint64_t const uResult = g_aShiftLeft[i].uValue << g_aShiftLeft[i].cShift;
        if (uResult != g_aShiftLeft[i].uExpected)
            RTTestFailed(g_hTest, "i=%u iValue=%#018RX64 SHL %u => %#018RX64, expected %#018RX64", i, g_aShiftLeft[i].uValue,
                         g_aShiftLeft[i].cShift, uResult, g_aShiftLeft[i].uExpected);
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aShiftLeft); i++)
    {
        int64_t const iResult = (int64_t)g_aShiftLeft[i].uValue << g_aShiftLeft[i].cShift;
        if ((uint64_t)iResult != g_aShiftLeft[i].uExpected)
            RTTestFailed(g_hTest, "i=%u iValue=%#018RX64 SHL %u => %#018RX64, expected %#018RX64 [signed]", i, g_aShiftLeft[i].uValue,
                         g_aShiftLeft[i].cShift, iResult, g_aShiftLeft[i].uExpected);
    }

    /* Random values via uint64. */
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        uint64_t const uValue  = RTRandU64();
        uint8_t const  cShift  = (uint8_t)RTRandU32Ex(0, i & 3 ? 63 : 255);
        uint64_t const uResult = uValue << cShift;
        RTUINT64U uExpected = { uValue };
        if (cShift > 63)
            uExpected.u = 0;
        else
            RTUInt64AssignShiftLeft(&uExpected, cShift);

        if (uResult != uExpected.u)
            RTTestFailed(g_hTest, "uValue=%#018RX64 SHR %u => %#018RX64, expected %#018RX64", uValue, cShift, uResult, uExpected.u);
    }

    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        uint64_t const uValue  = RTRandU64();
        uint8_t const  cShift  = (uint8_t)RTRandU32Ex(0, i & 3 ? 63 : 255);
        int64_t const  iResult = (int64_t)uValue << cShift;
        RTUINT64U uExpected = { uValue };
        if (cShift > 63)
            uExpected.u = 0;
        else
            RTUInt64AssignShiftLeft(&uExpected, cShift);

        if ((uint64_t)iResult != uExpected.u)
            RTTestFailed(g_hTest, "uValue=%#018RX64 SHR %u => %#018RX64, expected %#018RX64", uValue, cShift, iResult, uExpected.u);
    }
}


TSTRTNOCRT5MULT volatile const g_aMult[] =
{
    { UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000), /* => */ UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x0000000000000001) },
    { UINT64_C(0x000000001203879f), UINT64_C(0x0000000094585638), /* => */ UINT64_C(0x0A7041AFAAFD14C8) },
    { UINT64_C(0x0000000100000000), UINT64_C(0x0000000010329484), /* => */ UINT64_C(0x1032948400000000) },
    { UINT64_C(0x0958609457ad0f03), UINT64_C(0x8f9d0a07d9f83145), /* => */ UINT64_C(0xC77D9538D76C9ECF) },
};

static void tstMultiplication()
{
    RTTestSub(g_hTest, "64-bit multiplication");

    /* static tests from array. */
    for (size_t i = 0; i < RT_ELEMENTS(g_aMult); i++)
    {
        uint64_t const uResult = g_aMult[i].uFactor1 * g_aMult[i].uFactor2;
        if (uResult != g_aMult[i].uExpected)
            RTTestFailed(g_hTest, "i=%u %#018RX64 * %#018RX64 => %#018RX64, expected %#018RX64",
                         i, g_aMult[i].uFactor1, g_aMult[i].uFactor2, uResult, g_aMult[i].uExpected);
    }
    for (size_t i = 0; i < RT_ELEMENTS(g_aMult); i++)
    {
        uint64_t const uResult = g_aMult[i].uFactor2 * g_aMult[i].uFactor1;
        if (uResult != g_aMult[i].uExpected)
            RTTestFailed(g_hTest, "i=%u %#018RX64 * %#018RX64 => %#018RX64, expected %#018RX64 (f2*f1)",
                         i, g_aMult[i].uFactor2, g_aMult[i].uFactor1, uResult, g_aMult[i].uExpected);
    }
    for (size_t i = 0; i < RT_ELEMENTS(g_aMult); i++)
    {
        int64_t const iResult = (int64_t)g_aMult[i].uFactor1 * (int64_t)g_aMult[i].uFactor2;
        if ((uint64_t)iResult != g_aMult[i].uExpected)
            RTTestFailed(g_hTest, "i=%u %#018RX64 * %#018RX64 => %#018RX64, expected %#018RX64 (signed)",
                         i, g_aMult[i].uFactor1, g_aMult[i].uFactor2, iResult, g_aMult[i].uExpected);
    }

    /* Random values via uint64. */
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uFactor1 = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uFactor2 = { RTRandU64Ex(0, i & 3 ? UINT64_MAX : UINT32_MAX) };
        uint64_t  const uResult  = uFactor1.u * uFactor2.u;
        RTUINT64U uExpected;
        RTUInt64Mul(&uExpected, &uFactor1, &uFactor2);

        if (uResult != uExpected.u)
            RTTestFailed(g_hTest, "%#018RX64 * %#018RX64 => %#018RX64, expected %#018RX64", uFactor1.u, uFactor2.u, uResult, uExpected.u);
    }
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uFactor1 = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uFactor2 = { RTRandU64Ex(0, i & 3 ? UINT64_MAX : UINT32_MAX) };
        int64_t  const  iResult  = (int64_t)uFactor1.u * (int64_t)uFactor2.u;
        RTUINT64U uExpected;
        RTUInt64Mul(&uExpected, &uFactor1, &uFactor2);

        if ((uint64_t)iResult != uExpected.u)
            RTTestFailed(g_hTest, "%#018RX64 * %#018RX64 => %#018RX64, expected %#018RX64 (signed)",
                         uFactor1.u, uFactor2.u, iResult, uExpected.u);
    }
}


TSTRTNOCRT5DIV volatile const g_aDivU[] =
{
    { UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000002), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000003), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000001) },
    { UINT64_C(0x0000000009821348), UINT64_C(0x0000000023949583), /* => */ UINT64_C(0x0000000000000000), UINT64_C(0x0000000009821348) },
    { UINT64_C(0x0000000079821348), UINT64_C(0x0000000023949583), /* => */ UINT64_C(0x0000000000000003), UINT64_C(0x000000000EC452BF) },
    { UINT64_C(0x0439583044583049), UINT64_C(0x0984987539485732), /* => */ UINT64_C(0x0000000000000000), UINT64_C(0x0439583044583049) },
    { UINT64_C(0xf439583044583049), UINT64_C(0x0984987539485732), /* => */ UINT64_C(0x0000000000000019), UINT64_C(0x064674BDAC47AC67) },
    { UINT64_C(0xdf8305930df94306), UINT64_C(0x00000043d9dfa039), /* => */ UINT64_C(0x00000000034B4D9D), UINT64_C(0x0000000990EFDB11) },
    { UINT64_C(0xff9f939d0f0302d9), UINT64_C(0x0000000000000042), /* => */ UINT64_C(0x03DF823C8FBE1B31), UINT64_C(0x0000000000000037) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000042), /* => */ UINT64_C(0x03E0F83E0F83E0F8), UINT64_C(0x000000000000000f) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000007), /* => */ UINT64_C(0x2492492492492492), UINT64_C(0x0000000000000001) },
    { UINT64_C(0xe1f17ac834b412b4), UINT64_C(0xda38027453291b1e), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x07b97853e18af796) },
    /* These should trigger the rare overflow condition in the 32-bit approximation algorithm. */
    { UINT64_C(0xe101721f65eb6226), UINT64_C(0x0000028180a483fa), /* => */ UINT64_C(0x000000000059CA9C), UINT64_C(0x000002814F9DB1CE) },
    { UINT64_C(0x8b0a3ed1cda21100), UINT64_C(0x8b0a3ed1cda231d4), /* => */ UINT64_C(0x0000000000000000), UINT64_C(0x8B0A3ED1CDA21100) },
    { UINT64_C(0xf1387d27f3a0c583), UINT64_C(0x000735f0d9661f93), /* => */ UINT64_C(0x0000000000002173), UINT64_C(0x000735F020AEA37A) },
    { UINT64_C(0xb690b755d6f4496f), UINT64_C(0x000143027675d0d7), /* => */ UINT64_C(0x00000000000090b0), UINT64_C(0x00014302207bc59f) },
    { UINT64_C(0x78a5b3efc6c82cf7), UINT64_C(0x00000f9b4400a9f0), /* => */ UINT64_C(0x000000000007bb06), UINT64_C(0x00000f9b0d11e157) },
    { UINT64_C(0x8ae75b071b094efc), UINT64_C(0x0020904259dedd1e), /* => */ UINT64_C(0x0000000000000443), UINT64_C(0x002090421a40f822) },
    { UINT64_C(0x90c9fb203c85fa7c), UINT64_C(0x000001ef807ef1e9), /* => */ UINT64_C(0x00000000004ace0e), UINT64_C(0x000001ef219141be) },
    { UINT64_C(0xf9ae8ea6b31751df), UINT64_C(0x00004281110e2327), /* => */ UINT64_C(0x000000000003c11e), UINT64_C(0x00004280a179cc4d) },
    /* These trigger an even more special case, where the QapproxDividend calculation overflows. */
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x00003C11D54B525f), /* => */ UINT64_C(0x00000000000442FF), UINT64_C(0x00003C11D540755E) },
    { UINT64_C(0xfffffffffefa1235), UINT64_C(0x0001001702112f8c), /* => */ UINT64_C(0x000000000000FFE8), UINT64_C(0x00010017010A8755) },
};

static void tstUnsignedDivision()
{
    RTTestSub(g_hTest, "64-bit unsigned division");

    /*
     * static tests from array.
     */
    for (size_t i = 0; i < RT_ELEMENTS(g_aDivU); i++)
    {
        uint64_t const uResult = g_aDivU[i].uDividend / g_aDivU[i].uDivisor;  /* aulldiv */
        if (uResult != g_aDivU[i].uQuotient)
            RTTestFailed(g_hTest, "i=%u %#018RX64 / %#018RX64 => %#018RX64, expected %#018RX64",
                         i, g_aDivU[i].uDividend, g_aDivU[i].uDivisor, uResult, g_aDivU[i].uQuotient);
    }
    for (size_t i = 0; i < RT_ELEMENTS(g_aDivU); i++)
    {
        uint64_t const uResult = g_aDivU[i].uDividend % g_aDivU[i].uDivisor; /* aullrem */
        if (uResult != g_aDivU[i].uRemainder)
            RTTestFailed(g_hTest, "i=%u %#018RX64 %% %#018RX64 => %#018RX64, expected %#018RX64",
                         i, g_aDivU[i].uDividend, g_aDivU[i].uDivisor, uResult, g_aDivU[i].uRemainder);
    }
    for (size_t i = 0; i < RT_ELEMENTS(g_aDivU); i++)
    {
        uint64_t const uDividend  = g_aDivU[i].uDividend;
        uint64_t const uDivisor   = g_aDivU[i].uDivisor;
        uint64_t const uQuotient  = uDividend / uDivisor; /* auldvrm hopefully - only not in unoptimized builds. */
        uint64_t const uRemainder = uDividend % uDivisor;
        if (   uQuotient  != g_aDivU[i].uQuotient
            || uRemainder != g_aDivU[i].uRemainder)
            RTTestFailed(g_hTest, "i=%u %#018RX64 / %#018RX64 => q=%#018RX64 r=%#018RX64, expected q=%#018RX64 r=%#018RX64",
                         i, g_aDivU[i].uDividend, g_aDivU[i].uDivisor,
                         uQuotient, uRemainder, g_aDivU[i].uQuotient, g_aDivU[i].uRemainder);
    }

    /*
     * Same but with random values via uint64.
     */
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uDividend = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uDivisor  = { RTRandU64Ex(0, i & 3 ? UINT64_MAX : UINT32_MAX) };
        uint64_t  const uResult   = uDividend.u / uDivisor.u;
        RTUINT64U uExpected;
        RTUInt64Div(&uExpected, &uDividend, &uDivisor);
        if (uResult != uExpected.u)
            RTTestFailed(g_hTest, "%#018RX64 / %#018RX64 => %#018RX64, expected %#018RX64", uDividend.u, uDivisor.u, uResult, uExpected.u);
    }
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uDividend = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uDivisor  = { RTRandU64Ex(0, i & 3 ? UINT64_MAX : UINT32_MAX) };
        uint64_t  const uResult   = uDividend.u % uDivisor.u;
        RTUINT64U uExpected;
        RTUInt64Mod(&uExpected, &uDividend, &uDivisor);
        if (uResult != uExpected.u)
            RTTestFailed(g_hTest, "%#018RX64 %% %#018RX64 => %#018RX64, expected %#018RX64", uDividend.u, uDivisor.u, uResult, uExpected.u);
    }
    for (uint64_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uDividend  = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uDivisor   = { RTRandU64Ex(0, i & 3 ? UINT64_MAX : UINT32_MAX) };
        uint64_t  const uRemainder = uDividend.u % uDivisor.u;
        uint64_t  const uQuotient  = uDividend.u / uDivisor.u;
        RTUINT64U uExpectedQ, uExpectedR;
        RTUInt64DivRem(&uExpectedQ, &uExpectedR, &uDividend, &uDivisor);
        if (   uQuotient  != uExpectedQ.u
            || uRemainder != uExpectedR.u)
            RTTestFailed(g_hTest, "%#018RX64 / %#018RX64 => q=%#018RX64 r=%#018RX64, expected q=%#018RX64 r=%#018RX64",
                         uDividend.u, uDivisor.u, uQuotient, uRemainder, uExpectedQ.u, uExpectedR.u);
    }
}


TSTRTNOCRT5DIV volatile const g_aDivS[] =
{
    { UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000000), UINT64_C(0xffffffffffffffff), /* => */ UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000) },

    { UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x0000000000000001), UINT64_C(0xffffffffffffffff), /* => */ UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000000) },
    { UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000000) },

    { UINT64_C(0x0000000000000002), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000000) },

    { UINT64_C(0x0000000000000003), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000001) },
    { UINT64_C(0xfffffffffffffffd), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0xffffffffffffffff), UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x0000000000000003), UINT64_C(0xfffffffffffffffe), /* => */ UINT64_C(0xffffffffffffffff), UINT64_C(0x0000000000000001) },
    { UINT64_C(0xfffffffffffffffd), UINT64_C(0xfffffffffffffffe), /* => */ UINT64_C(0x0000000000000001), UINT64_C(0xffffffffffffffff) },

    { UINT64_C(0x8000000000000001), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x8000000000000001), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8000000000000001), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0xc000000000000001), UINT64_C(0xffffffffffffffff) },
    { UINT64_C(0x8000000000000001), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0xc000000000000001), UINT64_C(0xffffffffffffffff) },

    { UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000001), /* => */ UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000002), /* => */ UINT64_C(0xc000000000000000), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8000000000000000), UINT64_C(0xffffffffffffffff), /* => */ UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000000) },
    { UINT64_C(0x8000000000000000), UINT64_C(0xfffffffffffffffe), /* => */ UINT64_C(0x4000000000000000), UINT64_C(0x0000000000000000) },
};

static void tstSignedDivision()
{
    RTTestSub(g_hTest, "64-bit signed division");

    /*
     * static tests from array.
     */
    for (size_t i = 0; i < RT_ELEMENTS(g_aDivS); i++)
    {
        int64_t const iResult = (int64_t)g_aDivS[i].uDividend / (int64_t)g_aDivS[i].uDivisor;  /* aulldiv */
        if ((uint64_t)iResult != g_aDivS[i].uQuotient)
            RTTestFailed(g_hTest, "i=%u %#018RX64 / %#018RX64 => %#018RX64, expected %#018RX64",
                         i, g_aDivS[i].uDividend, g_aDivS[i].uDivisor, iResult, g_aDivS[i].uQuotient);
    }
    for (size_t i = 0; i < RT_ELEMENTS(g_aDivS); i++)
    {
        int64_t const iResult = (int64_t)g_aDivS[i].uDividend % (int64_t)g_aDivS[i].uDivisor; /* aullrem */
        if ((uint64_t)iResult != g_aDivS[i].uRemainder)
            RTTestFailed(g_hTest, "i=%u %#018RX64 %% %#018RX64 => %#018RX64, expected %#018RX64",
                         i, g_aDivS[i].uDividend, g_aDivS[i].uDivisor, iResult, g_aDivS[i].uRemainder);
    }
    for (size_t i = 0; i < RT_ELEMENTS(g_aDivS); i++)
    {
        int64_t const iDividend  = (int64_t)g_aDivS[i].uDividend;
        int64_t const iDivisor   = (int64_t)g_aDivS[i].uDivisor;
        int64_t const iQuotient  = iDividend / iDivisor; /* auldvrm hopefully - only not in unoptimized builds. */
        int64_t const iRemainder = iDividend % iDivisor;
        if (   (uint64_t)iQuotient  != g_aDivS[i].uQuotient
            || (uint64_t)iRemainder != g_aDivS[i].uRemainder)
            RTTestFailed(g_hTest, "i=%u %#018RX64 / %#018RX64 => q=%#018RX64 r=%#018RX64, expected q=%#018RX64 r=%#018RX64",
                         i, g_aDivS[i].uDividend, g_aDivS[i].uDivisor,
                         iQuotient, iRemainder, g_aDivS[i].uQuotient, g_aDivS[i].uRemainder);
    }

    /* Check that uint64 works: */
    {
        RTUINT64U Tmp = { 42 };
        RTTEST_CHECK(g_hTest, !RTUInt64IsSigned(&Tmp));
        RTUInt64AssignNeg(&Tmp);
        int64_t   iExpect = -42;
        RTTEST_CHECK(g_hTest, Tmp.u == (uint64_t)iExpect);
        RTTEST_CHECK(g_hTest, RTUInt64IsSigned(&Tmp));
    }

    /*
     * Same but with random values via uint64.
     */
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uDividend = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uDivisor  = { RTRandU64Ex(1, i & 3 ? UINT64_MAX : UINT32_MAX) };
        int64_t   const iResult   = (int64_t)uDividend.u / (int64_t)uDivisor.u;
        RTUINT64U uExpected;
        RTUInt64DivSigned(&uExpected, &uDividend, &uDivisor);
        if ((uint64_t)iResult != uExpected.u)
            RTTestFailed(g_hTest, "%#018RX64 / %#018RX64 => %#018RX64, expected %#018RX64", uDividend.u, uDivisor.u, iResult, uExpected.u);
    }
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uDividend = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uDivisor  = { RTRandU64Ex(1, i & 3 ? UINT64_MAX : UINT32_MAX) };
        int64_t   const iResult   = (int64_t)uDividend.u % (int64_t)uDivisor.u;
        RTUINT64U uExpected;
        RTUInt64ModSigned(&uExpected, &uDividend, &uDivisor);
        if ((uint64_t)iResult != uExpected.u)
            RTTestFailed(g_hTest, "%#018RX64 %% %#018RX64 => %#018RX64, expected %#018RX64", uDividend.u, uDivisor.u, iResult, uExpected.u);
    }
    for (size_t i = 0; i < RANDOM_LOOPS; i++)
    {
        RTUINT64U const uDividend  = { RTRandU64Ex(0, i & 7 ? UINT64_MAX : UINT32_MAX) };
        RTUINT64U const uDivisor   = { RTRandU64Ex(1, i & 3 ? UINT64_MAX : UINT32_MAX) };
        int64_t   const iRemainder = (int64_t)uDividend.u % (int64_t)uDivisor.u;
        int64_t   const iQuotient  = (int64_t)uDividend.u / (int64_t)uDivisor.u;
        RTUINT64U uExpectedQ, uExpectedR;
        RTUInt64DivRemSigned(&uExpectedQ, &uExpectedR, &uDividend, &uDivisor);
        if (   (uint64_t)iQuotient  != uExpectedQ.u
            || (uint64_t)iRemainder != uExpectedR.u)
            RTTestFailed(g_hTest, "%#018RX64 / %#018RX64 => q=%#018RX64 r=%#018RX64, expected q=%#018RX64 r=%#018RX64",
                         uDividend.u, uDivisor.u, iQuotient, iRemainder, uExpectedQ.u, uExpectedR.u);
    }
}


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTNoCrt-5", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    tstShiftRight();
    tstShiftRightArithmetic();
    tstShiftLeft();
    tstMultiplication();
    tstUnsignedDivision();
    tstSignedDivision();

    return RTTestSummaryAndDestroy(g_hTest);
}

