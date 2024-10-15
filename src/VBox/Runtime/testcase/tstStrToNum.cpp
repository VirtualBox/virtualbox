/* $Id: tstStrToNum.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - String To Number Conversion.
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
#include <iprt/test.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/err.h>

#include <float.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct TstI64
{
    const char *psz;
    unsigned    uBase;
    int         rc;
    int64_t     Result;
};

struct TstU64
{
    const char *psz;
    unsigned    uBase;
    int         rc;
    uint64_t    Result;
};

struct TstI32
{
    const char *psz;
    unsigned    uBase;
    int         rc;
    int32_t     Result;
};

struct TstU32
{
    const char *psz;
    unsigned    uBase;
    int         rc;
    uint32_t    Result;
};


struct TstRD
{
    const char *psz;
    unsigned    cchMax;
    int         rc;
    double      rd;
};

struct TstR64
{
    const char *psz;
    unsigned    cchMax;
    int         rc;
    RTFLOAT64U  r64;
};


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TEST(Test, Type, Fmt, Fun, iTest) \
    do \
    { \
        Type Result; \
        int rc = Fun(Test.psz, NULL, Test.uBase, &Result); \
        if (Result != Test.Result) \
            RTTestIFailed("'%s' -> " Fmt " expected " Fmt ". (%s/%u)\n", Test.psz, Result, Test.Result, #Fun, iTest); \
        else if (rc != Test.rc) \
            RTTestIFailed("'%s' -> rc=%Rrc expected %Rrc. (%s/%u)\n", Test.psz, rc, Test.rc, #Fun, iTest); \
    } while (0)


#define RUN_TESTS(aTests, Type, Fmt, Fun) \
    do \
    { \
        RTTestISub(#Fun); \
        for (unsigned iTest = 0; iTest < RT_ELEMENTS(aTests); iTest++) \
        { \
            TEST(aTests[iTest], Type, Fmt, Fun, iTest); \
        } \
    } while (0)

#define FULL_TEST(Test, Type, Fmt, Fun, iTest) \
    do \
    { \
        Type Result; \
        int rc = Fun(Test.psz, Test.uBase, &Result); \
        if (Result != Test.Result) \
            RTTestIFailed("'%s' -> " Fmt " expected " Fmt ". (%s/%u)\n", Test.psz, Result, Test.Result, #Fun, iTest); \
        else if (rc != Test.rc) \
            RTTestIFailed("'%s' -> rc=%Rrc expected %Rrc. (%s/%u)\n", Test.psz, rc, Test.rc, #Fun, iTest); \
    } while (0)


#define RUN_FULL_TESTS(aTests, Type, Fmt, Fun) \
    do \
    { \
        RTTestISub(#Fun); \
        for (unsigned iTest = 0; iTest < RT_ELEMENTS(aTests); iTest++) \
        { \
            FULL_TEST(aTests[iTest], Type, Fmt, Fun, iTest); \
        } \
    } while (0)



int main()
{
    RTTEST     hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTStrToNum", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    static const struct TstU64 aTstU64[] =
    {
        { "0",                      0,  VINF_SUCCESS,           0 },
        { "1",                      0,  VINF_SUCCESS,           1 },
        { "-1",                     0,  VWRN_NEGATIVE_UNSIGNED,  ~0ULL },
        { "0x",                     0,  VWRN_TRAILING_CHARS,    0 },
        { "0x1",                    0,  VINF_SUCCESS,           1 },
        { "0x0fffffffffffffff",     0,  VINF_SUCCESS,           0x0fffffffffffffffULL },
        { "0x0ffffffffffffffffffffff",0,  VWRN_NUMBER_TOO_BIG,  0xffffffffffffffffULL },
        { "0x0ffffffffffffffffffffff", 10 << 8,  VINF_SUCCESS,  0x0fffffff },
        { "asdfasdfasdf",           0,  VERR_NO_DIGITS,         0 },
        { "0x111111111",            0,  VINF_SUCCESS,           0x111111111ULL },
        { "4D9702C5CBD9B778",      16,  VINF_SUCCESS,           UINT64_C(0x4D9702C5CBD9B778) },
    };
    RUN_TESTS(aTstU64, uint64_t, "%#llx", RTStrToUInt64Ex);

    static const struct TstU64 aTstFullU64[] =
    {
        { "42",                         0,  VINF_SUCCESS,           42 },
        { "42 ",                        0,  VERR_TRAILING_SPACES,   42 },
        { "42! ",                       0,  VERR_TRAILING_CHARS,    42 },
        { "42 !",                       0,  VERR_TRAILING_CHARS,    42 },
        { "42 !",                    2<<8,  VINF_SUCCESS,           42 },
        { "42 !",                    3<<8,  VERR_TRAILING_SPACES,   42 },
        { "42 !",                    4<<8,  VERR_TRAILING_CHARS,    42 },
        { "-1",                         0,  VWRN_NEGATIVE_UNSIGNED, UINT64_MAX },
        { "-1 ",                        0,  VERR_TRAILING_SPACES,   UINT64_MAX },
        { "-1 ",                     2<<8,  VWRN_NEGATIVE_UNSIGNED, UINT64_MAX },
        { "-1 ",                     3<<8,  VERR_TRAILING_SPACES,   UINT64_MAX },
        { "0x0fffffffffffffff",         0,  VINF_SUCCESS,           0x0fffffffffffffffULL },
        { "0x0ffffffffffffffffffff",    0,  VWRN_NUMBER_TOO_BIG,    0xffffffffffffffffULL },
        { "0x0ffffffffffffffffffff ",   0,  VERR_TRAILING_SPACES,   0xffffffffffffffffULL },
        { "0x0ffffffffffffffffffff! ",  0,  VERR_TRAILING_CHARS,    0xffffffffffffffffULL },
        { "0x0ffffffffffffffffffff !",  0,  VERR_TRAILING_CHARS,    0xffffffffffffffffULL },
        { "0x0ffffffffffffffffffff",   10 << 8,  VINF_SUCCESS,      0x0fffffff },
    };
    RUN_FULL_TESTS(aTstFullU64, uint64_t, "%#llx", RTStrToUInt64Full);

    static const struct TstI64 aTstI64[] =
    {
        { "0",                      0,  VINF_SUCCESS,           0 },
        { "1",                      0,  VINF_SUCCESS,           1 },
        { "-1",                     0,  VINF_SUCCESS,          -1 },
        { "-1",                    10,  VINF_SUCCESS,          -1 },
        { "-31",                    0,  VINF_SUCCESS,          -31 },
        { "-31",                   10,  VINF_SUCCESS,          -31 },
        { "-32",                    0,  VINF_SUCCESS,          -32 },
        { "-33",                    0,  VINF_SUCCESS,          -33 },
        { "-64",                    0,  VINF_SUCCESS,          -64 },
        { "-127",                   0,  VINF_SUCCESS,          -127 },
        { "-128",                   0,  VINF_SUCCESS,          -128 },
        { "-129",                   0,  VINF_SUCCESS,          -129 },
        { "-254",                   0,  VINF_SUCCESS,          -254 },
        { "-255",                   0,  VINF_SUCCESS,          -255 },
        { "-256",                   0,  VINF_SUCCESS,          -256 },
        { "-257",                   0,  VINF_SUCCESS,          -257 },
        { "-511",                   0,  VINF_SUCCESS,          -511 },
        { "-512",                   0,  VINF_SUCCESS,          -512 },
        { "-513",                   0,  VINF_SUCCESS,          -513 },
        { "-1023",                  0,  VINF_SUCCESS,          -1023 },
        { "-1023",                  0,  VINF_SUCCESS,          -1023 },
        { "-1023",                  0,  VINF_SUCCESS,          -1023},
        { "-1023",                 10,  VINF_SUCCESS,          -1023 },
        { "-4564678",               0,  VINF_SUCCESS,          -4564678 },
        { "-4564678",              10,  VINF_SUCCESS,          -4564678 },
        { "-1234567890123456789",   0,  VINF_SUCCESS,          -1234567890123456789LL },
        { "-1234567890123456789",  10,  VINF_SUCCESS,          -1234567890123456789LL },
        { "0x",                     0,  VWRN_TRAILING_CHARS,    0 },
        { "0x1",                    0,  VINF_SUCCESS,           1 },
        { "0x1",                   10,  VWRN_TRAILING_CHARS,    0 },
        { "0x1",                   16,  VINF_SUCCESS,           1 },
        { "0x0fffffffffffffff",     0,  VINF_SUCCESS,           0x0fffffffffffffffULL },
        { "0x7fffffffffffffff",     0,  VINF_SUCCESS,           0x7fffffffffffffffULL },
        { "0xffffffffffffffff",     0,  VWRN_NUMBER_TOO_BIG,    -1 },
        { "0x01111111111111111111111",0,  VWRN_NUMBER_TOO_BIG,  0x1111111111111111ULL },
        { "0x02222222222222222222222",0,  VWRN_NUMBER_TOO_BIG,  0x2222222222222222ULL },
        { "0x03333333333333333333333",0,  VWRN_NUMBER_TOO_BIG,  0x3333333333333333ULL },
        { "0x04444444444444444444444",0,  VWRN_NUMBER_TOO_BIG,  0x4444444444444444ULL },
        { "0x07777777777777777777777",0,  VWRN_NUMBER_TOO_BIG,  0x7777777777777777ULL },
        { "0x07f7f7f7f7f7f7f7f7f7f7f",0,  VWRN_NUMBER_TOO_BIG,  0x7f7f7f7f7f7f7f7fULL },
        { "0x0ffffffffffffffffffffff",0,  VWRN_NUMBER_TOO_BIG,  (int64_t)0xffffffffffffffffULL },
        { "0x0ffffffffffffffffffffff", 10 << 8, VINF_SUCCESS,   INT64_C(0x0fffffff) },
        { "0x0ffffffffffffffffffffff", 18 << 8, VINF_SUCCESS,   INT64_C(0x0fffffffffffffff) },
        { "0x0ffffffffffffffffffffff", 19 << 8, VWRN_NUMBER_TOO_BIG, -1 },
        { "asdfasdfasdf",           0,  VERR_NO_DIGITS,         0 },
        { "0x111111111",            0,  VINF_SUCCESS,           0x111111111ULL },
    };
    RUN_TESTS(aTstI64, int64_t, "%#lld", RTStrToInt64Ex);

    static const struct TstI64 aTstI64Full[] =
    {
        { "1",                          0,  VINF_SUCCESS,           1 },
        { "1 ",                         0,  VERR_TRAILING_SPACES,   1 },
        { "1! ",                        0,  VERR_TRAILING_CHARS,    1 },
        { "1 !",                        0,  VERR_TRAILING_CHARS,    1 },
        { "1 !",                     1<<8,  VINF_SUCCESS,           1 },
        { "1 !",                     2<<8,  VERR_TRAILING_SPACES,   1 },
        { "1 !",                     3<<8,  VERR_TRAILING_CHARS,    1 },
        { "0xffffffffffffffff",         0,  VWRN_NUMBER_TOO_BIG,    -1 },
        { "0xffffffffffffffff ",        0,  VERR_TRAILING_SPACES,   -1 },
        { "0xffffffffffffffff!",        0,  VERR_TRAILING_CHARS,    -1 },
        { "0xffffffffffffffff !",   18<<8,  VWRN_NUMBER_TOO_BIG,    -1 },
        { "0xffffffffffffffff !",   19<<8,  VERR_TRAILING_SPACES,   -1 },
        { "0xffffffffffffffff !",   20<<8,  VERR_TRAILING_CHARS,    -1 },
    };
    RUN_FULL_TESTS(aTstI64Full, int64_t, "%#lld", RTStrToInt64Full);


    static const struct TstI32 aTstI32[] =
    {
        { "0",                      0,  VINF_SUCCESS,           0 },
        { "1",                      0,  VINF_SUCCESS,           1 },
        { "-1",                     0,  VINF_SUCCESS,          -1 },
        { "-1",                    10,  VINF_SUCCESS,          -1 },
        { "-31",                    0,  VINF_SUCCESS,          -31 },
        { "-31",                   10,  VINF_SUCCESS,          -31 },
        { "-32",                    0,  VINF_SUCCESS,          -32 },
        { "-33",                    0,  VINF_SUCCESS,          -33 },
        { "-64",                    0,  VINF_SUCCESS,          -64 },
        { "-127",                   0,  VINF_SUCCESS,          -127 },
        { "-128",                   0,  VINF_SUCCESS,          -128 },
        { "-129",                   0,  VINF_SUCCESS,          -129 },
        { "-254",                   0,  VINF_SUCCESS,          -254 },
        { "-255",                   0,  VINF_SUCCESS,          -255 },
        { "-256",                   0,  VINF_SUCCESS,          -256 },
        { "-257",                   0,  VINF_SUCCESS,          -257 },
        { "-511",                   0,  VINF_SUCCESS,          -511 },
        { "-512",                   0,  VINF_SUCCESS,          -512 },
        { "-513",                   0,  VINF_SUCCESS,          -513 },
        { "-1023",                  0,  VINF_SUCCESS,          -1023 },
        { "-1023",                  0,  VINF_SUCCESS,          -1023 },
        { "-1023",                  0,  VINF_SUCCESS,          -1023},
        { "-1023",                 10,  VINF_SUCCESS,          -1023 },
        { "-4564678",               0,  VINF_SUCCESS,          -4564678 },
        { "-4564678",              10,  VINF_SUCCESS,          -4564678 },
        { "4564678",                0,  VINF_SUCCESS,          4564678 },
        { "4564678",               10,  VINF_SUCCESS,          4564678 },
        { "-1234567890123456789",   0,  VWRN_NUMBER_TOO_BIG,   (int32_t)((uint64_t)INT64_C(-1234567890123456789) & UINT32_MAX) },
        { "-1234567890123456789",  10,  VWRN_NUMBER_TOO_BIG,   (int32_t)((uint64_t)INT64_C(-1234567890123456789) & UINT32_MAX) },
        { "1234567890123456789",    0,  VWRN_NUMBER_TOO_BIG,   (int32_t)(INT64_C(1234567890123456789)            & UINT32_MAX) },
        { "1234567890123456789",   10,  VWRN_NUMBER_TOO_BIG,   (int32_t)(INT64_C(1234567890123456789)            & UINT32_MAX) },
        { "0x",                     0,  VWRN_TRAILING_CHARS,    0 },
        { "0x1",                    0,  VINF_SUCCESS,           1 },
        { "0x1",                   10,  VWRN_TRAILING_CHARS,    0 },
        { "0x1",                   16,  VINF_SUCCESS,           1 },
        { "0x7fffffff",             0,  VINF_SUCCESS,           0x7fffffff },
        { "0x80000000",             0,  VWRN_NUMBER_TOO_BIG,    INT32_MIN },
        { "0xffffffff",             0,  VWRN_NUMBER_TOO_BIG,    -1 },
        { "0x0fffffffffffffff",     0,  VWRN_NUMBER_TOO_BIG,    (int32_t)0xffffffff },
        { "0x01111111111111111111111",0,  VWRN_NUMBER_TOO_BIG,  0x11111111 },
        { "0x0ffffffffffffffffffffff",0,  VWRN_NUMBER_TOO_BIG,  (int32_t)0xffffffff },
        { "0x0ffffffffffffffffffffff", 10 << 8, VINF_SUCCESS,   0x0fffffff },
        { "0x0ffffffffffffffffffffff", 11 << 8, VWRN_NUMBER_TOO_BIG, -1 },
        { "asdfasdfasdf",           0,  VERR_NO_DIGITS,         0 },
        { "0x1111111",              0,  VINF_SUCCESS,           0x01111111 },
    };
    RUN_TESTS(aTstI32, int32_t, "%#d", RTStrToInt32Ex);

    static const struct TstU32 aTstU32[] =
    {
        { "0",                          0,  VINF_SUCCESS,           0 },
        { "1",                          0,  VINF_SUCCESS,           1 },
        /// @todo { "-1",                         0,  VWRN_NEGATIVE_UNSIGNED, ~0 }, - no longer true. bad idea?
        { "-1",                         0,  VWRN_NUMBER_TOO_BIG,    ~0U },
        { "0x",                         0,  VWRN_TRAILING_CHARS,    0 },
        { "0x1",                        0,  VINF_SUCCESS,           1 },
        { "0x1 ",                       0,  VWRN_TRAILING_SPACES,   1 },
        { "0x0fffffffffffffff",         0,  VWRN_NUMBER_TOO_BIG,    0xffffffffU },
        { "0x0ffffffffffffffffffffff",  0,  VWRN_NUMBER_TOO_BIG,    0xffffffffU },
        { "asdfasdfasdf",               0,  VERR_NO_DIGITS,         0 },
        { "0x1111111",                  0,  VINF_SUCCESS,           0x1111111 },
    };
    RUN_TESTS(aTstU32, uint32_t, "%#x", RTStrToUInt32Ex);


    static const struct TstU32 aTstFullU32[] =
    {
        { "0",                          0,  VINF_SUCCESS,           0 },
        { "0x0fffffffffffffff",         0,  VWRN_NUMBER_TOO_BIG,    0xffffffffU },
        { "0x0fffffffffffffffffffff",   0,  VWRN_NUMBER_TOO_BIG,    0xffffffffU },
        { "asdfasdfasdf",               0,  VERR_NO_DIGITS,         0 },
        { "42 ",                        0,  VERR_TRAILING_SPACES,   42 },
        { "42 ",                     2<<8,  VINF_SUCCESS,           42 },
        { "42! ",                       0,  VERR_TRAILING_CHARS,    42 },
        { "42! ",                    2<<8,  VINF_SUCCESS,           42 },
        { "42 !",                       0,  VERR_TRAILING_CHARS,    42 },
        { "42 !",                    2<<8,  VINF_SUCCESS,           42 },
        { "42 !",                    3<<8,  VERR_TRAILING_SPACES,   42 },
        { "42 !",                    4<<8,  VERR_TRAILING_CHARS,    42 },
        { "0x0fffffffffffffffffffff ",  0,  VERR_TRAILING_SPACES,   0xffffffffU },
        { "0x0fffffffffffffffffffff !", 0,  VERR_TRAILING_CHARS,    0xffffffffU },
    };
    RUN_FULL_TESTS(aTstFullU32, uint32_t, "%#x", RTStrToUInt32Full);

    /*
     * Test the some hex stuff too.
     */
    RTTestSub(hTest, "RTStrConvertHexBytesEx");
    static const struct
    {
        const char *pszHex;
        size_t      cbOut;
        size_t      offNext;
        uint8_t     bLast;
        bool        fColon;
        int         rc;
    } s_aConvertHexTests[] =
    {
        { "00",          1,  2, 0x00,  true, VINF_SUCCESS },
        { "00",          1,  2, 0x00, false, VINF_SUCCESS },
        { "000102",      3,  6, 0x02,  true, VINF_SUCCESS },
        { "00019",       2,  4, 0x01, false, VERR_UNEVEN_INPUT },
        { "00019",       2,  4, 0x01,  true, VERR_UNEVEN_INPUT },
        { "0001:9",      3,  6, 0x09,  true, VINF_SUCCESS},
        { "000102",      3,  6, 0x02, false, VINF_SUCCESS },
        { "0:1",         2,  3, 0x01,  true, VINF_SUCCESS },
        { ":",           2,  1, 0x00,  true, VINF_SUCCESS },
        { "0:01",        2,  4, 0x01,  true, VINF_SUCCESS },
        { "00:01",       2,  5, 0x01,  true, VINF_SUCCESS },
        { ":1:2:3:4:5",  6, 10, 0x05,  true, VINF_SUCCESS },
        { ":1:2:3::5",   6,  9, 0x05,  true, VINF_SUCCESS },
        { ":1:2:3:4:",   6,  9, 0x00,  true, VINF_SUCCESS },
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_aConvertHexTests); i++)
    {
        uint8_t abBuf[1024];
        memset(abBuf, 0xf6, sizeof(abBuf));
        const char *pszExpectNext = &s_aConvertHexTests[i].pszHex[s_aConvertHexTests[i].offNext];
        const char *pszNext       = "";
        size_t      cbReturned    = 77777;
        int rc = RTStrConvertHexBytesEx(s_aConvertHexTests[i].pszHex, abBuf, s_aConvertHexTests[i].cbOut,
                                        s_aConvertHexTests[i].fColon ? RTSTRCONVERTHEXBYTES_F_SEP_COLON : 0,
                                        &pszNext, &cbReturned);
        if (   rc      != s_aConvertHexTests[i].rc
            || pszNext != pszExpectNext
            || abBuf[s_aConvertHexTests[i].cbOut - 1] != s_aConvertHexTests[i].bLast
            )
            RTTestFailed(hTest, "RTStrConvertHexBytesEx/#%u %s -> %Rrc %p %#zx %#02x, expected %Rrc %p %#zx %#02x\n",
                         i, s_aConvertHexTests[i].pszHex,
                         rc, pszNext, cbReturned, abBuf[s_aConvertHexTests[i].cbOut - 1],
                         s_aConvertHexTests[i].rc, pszExpectNext, s_aConvertHexTests[i].cbOut, s_aConvertHexTests[i].bLast);
    }


    /*
     * Floating point string conversion.
     */
#ifdef RT_OS_WINDOWS /** @todo debug elsewhere */
    char szActual[128], szExpect[128];

    RTTestSub(hTest, "RTStrToDoubleEx");
    static const struct TstRD s_aTstDouble[] =
    {
        { "1",                              0, VINF_SUCCESS,              1.0 },
        { "2.0",                            0, VINF_SUCCESS,              2.0 },
        { "2.0000",                         0, VINF_SUCCESS,              2.0 },
        { "-2.0000",                        0, VINF_SUCCESS,              -2.0 },
        { "-2.0000",                        1, VERR_NO_DIGITS,            -0.0 },
        { "-2.0000",                        2, VINF_SUCCESS,              -2.0 },
        { "0.5",                            0, VINF_SUCCESS,              0.5 },
        { "1.5",                            0, VINF_SUCCESS,              1.5 },
        { "42.",                            0, VINF_SUCCESS,              42.0 },
        { "243.598605987",                  0, VINF_SUCCESS,              243.598605987 },
        { "3.14159265358979323846",         0, VINF_SUCCESS,              3.14159265358979323846 },
        { "3.1415926535897932384626433832", 0, VINF_SUCCESS,              3.14159265358979323846 },
        { "2.9979245800e+008",              0, VINF_SUCCESS,              299792458.0 },        /* speed of light (c)  */
        { "1.602176487e-19",                0, VINF_SUCCESS,              1.602176487e-19 },    /* electron volt (eV) */
        { "6.62606896e-34",                 0, VINF_SUCCESS,              6.62606896e-34 },     /* Planck's constant (h) */
        { "6.02214199e+23",                 0, VINF_SUCCESS,              6.02214199e23 },      /* Avogadro's number (Na) */
        { "1.66053e-0",                     0, VINF_SUCCESS,              1.66053e-0 },
        { "1.66053e-1",                     0, VINF_SUCCESS,              1.66053e-1 },
        { "1.66053e-2",                     0, VINF_SUCCESS,              1.66053e-2 },
        { "1.66053e-3",                     0, VINF_SUCCESS,              1.66053e-3 },
        { "1.66053e-4",                     0, VINF_SUCCESS,              1.66053e-4 },
        { "1.66053e-5",                     0, VINF_SUCCESS,              1.66053e-5 },
        { "1.66053e-6",                     0, VINF_SUCCESS,              1.66053e-6 },
        { "1.660538780e-27",                0, VINF_SUCCESS,              1.660538780e-27 },
        { "1.660538781e-27",                0, VINF_SUCCESS,              1.660538781e-27 },
        { "1.660538782e-27",                0, VINF_SUCCESS,              1.660538782e-27 },    /* Unified atomic mass (amu) [rounding issue with simple scale10 code] */
        { "1.660538783e-27",                0, VINF_SUCCESS,              1.660538783e-27 },
        { "1.660538784e-27",                0, VINF_SUCCESS,              1.660538784e-27 },
        { "1.660538785e-27",                0, VINF_SUCCESS,              1.660538785e-27 },
        { "1e1",                            0, VINF_SUCCESS,              1.0e1 },
        { "99e98",                          0, VINF_SUCCESS,              99.0e98 },
        { "1.2398039e206",                  0, VINF_SUCCESS,              1.2398039e206 },
        { "-1.2398039e-205",                0, VINF_SUCCESS,              -1.2398039e-205 },
        { "-1.2398039e-305",                0, VINF_SUCCESS,              -1.2398039e-305 },
        { "-1.2398039e-306",                0, VINF_SUCCESS,              -1.2398039e-306 }, /* RTStrFormatR64 get weird about these numbers... */
        { "-1.2398039e-307",                0, VINF_SUCCESS,              -1.2398039e-307 },
        { "-1.2398039e-308",                0, VWRN_FLOAT_UNDERFLOW,      -1.2398039e-308 }, /* subnormal */
        { "-1.2398039e-309",                0, VWRN_FLOAT_UNDERFLOW,      -1.2398039e-309 }, /* subnormal */
        { "-1.2398039e-310",                0, VWRN_FLOAT_UNDERFLOW,      -1.2398039e-310 }, /* subnormal */
        { "-1.2398039e-315",                0, VWRN_FLOAT_UNDERFLOW,      -1.2398039e-315 }, /* subnormal */
        { "-1.2398039e-323",                0, VWRN_FLOAT_UNDERFLOW,      -1.2398039e-323 }, /* subnormal */
#if 0 /* problematic in softfloat mode */
        { "-1.2398039e-324",                0, VWRN_FLOAT_UNDERFLOW,      -1.2398039e-324 }, /* subnormal */
#endif
        { "-1.2398039e-325",                0, VERR_FLOAT_UNDERFLOW,      -0.0 },
        { "1.7976931348623158e+308",        0, VINF_SUCCESS,              +DBL_MAX },
        { "-1.7976931348623158e+308",       0, VINF_SUCCESS,              -DBL_MAX },
        { "2.2250738585072014e-308",        0, VINF_SUCCESS,              +DBL_MIN },
        { "-2.2250738585072014e-308",       0, VINF_SUCCESS,              -DBL_MIN },
        { "-2.2250738585072010e-308",       0, VWRN_FLOAT_UNDERFLOW,      -2.2250738585072010E-308 }, /* subnormal close to -DBL_MIN */
#if __cplusplus >= 201700
        { "0x1",                            0, VINF_SUCCESS,              0x1.0p0 },
        { "0x2",                            0, VINF_SUCCESS,              0x2.0p0 },
        { "0x3",                            0, VINF_SUCCESS,              0x3.0p0 },
        { "0x3p1",                          0, VINF_SUCCESS,              0x3.0p1 },
        { "0x9.2p42",                       0, VINF_SUCCESS,              0x9.2p42 },
        { "-0x48f0405.24986e5f794bp42",     0, VINF_SUCCESS,              -0x48f0405.24986e5f794bp42 },
#endif
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_aTstDouble); i++)
    {
        //RTTestPrintf(hTest,RTTESTLVL_ALWAYS, "----- #%u: %s\n", i, s_aTstDouble[i].psz);
        RTFLOAT64U uRes = RTFLOAT64U_INIT_ZERO(1);
        char *pszNext = (char *)42;
        int rc = RTStrToDoubleEx(s_aTstDouble[i].psz, &pszNext, s_aTstDouble[i].cchMax, &uRes.rd);

        RTFLOAT64U uExpect;
        uExpect.rd = s_aTstDouble[i].rd;
        if (rc != s_aTstDouble[i].rc || !RTFLOAT64U_ARE_IDENTICAL(&uRes, &uExpect))
        {
            RTStrFormatR64(szActual, sizeof(szActual), &uRes, 0, 0, RTSTR_F_SPECIAL);
            RTStrFormatR64(szExpect, sizeof(szExpect), &uExpect, 0, 0, RTSTR_F_SPECIAL);
            RTTestFailed(hTest, "RTStrToDoubleEx/%#u: '%s' L %u -> %Rrc & %s, expected %Rrc & %s\n",
                         i, s_aTstDouble[i].psz, s_aTstDouble[i].cchMax, rc, szActual, s_aTstDouble[i].rc, szExpect);
        }
    }

    static const struct TstR64 s_aTstR64[] =
    {
        { "Inf",                            0, VINF_SUCCESS,            RTFLOAT64U_INIT_INF(0) },
        { "+Inf",                           0, VINF_SUCCESS,            RTFLOAT64U_INIT_INF(0) },
        { "-Inf",                           0, VINF_SUCCESS,            RTFLOAT64U_INIT_INF(1) },
        { "-Inf0",                          0, VWRN_TRAILING_CHARS,     RTFLOAT64U_INIT_INF(1) },
        { "-Inf ",                          0, VWRN_TRAILING_SPACES,    RTFLOAT64U_INIT_INF(1) },
        { "-Inf 0",                         0, VWRN_TRAILING_CHARS,     RTFLOAT64U_INIT_INF(1) },
        { "-Inf 0",                         1, VERR_NO_DIGITS,          RTFLOAT64U_INIT_ZERO(1) },
        { "-Inf 0",                         2, VERR_NO_DIGITS,          RTFLOAT64U_INIT_ZERO(1) },
        { "-Inf 0",                         3, VERR_NO_DIGITS,          RTFLOAT64U_INIT_ZERO(1) },
        { "-Inf 0",                         4, VINF_SUCCESS,            RTFLOAT64U_INIT_INF(1) },
        { "Nan",                            0, VINF_SUCCESS,            RTFLOAT64U_INIT_QNAN(0) },
        { "+Nan",                           0, VINF_SUCCESS,            RTFLOAT64U_INIT_QNAN(0) },
        { "+Nan(1)",                        0, VINF_SUCCESS,            RTFLOAT64U_INIT_QNAN_EX(0, 1) },
        { "-NaN",                           0, VINF_SUCCESS,            RTFLOAT64U_INIT_QNAN(1) },
        { "-nAn(1)",                        0, VINF_SUCCESS,            RTFLOAT64U_INIT_QNAN_EX(1, 1) },
        { "-nAn(q)",                        0, VINF_SUCCESS,            RTFLOAT64U_INIT_QNAN(1) },
        { "-nAn(s)",                        0, VINF_SUCCESS,            RTFLOAT64U_INIT_SNAN(1) },
        { "-nAn(_sig)",                     0, VINF_SUCCESS,            RTFLOAT64U_INIT_SNAN(1) },
        { "-nAn(22420102_sig)12",           0, VWRN_TRAILING_CHARS,     RTFLOAT64U_INIT_SNAN_EX(1, 0x22420102) },
        { "-nAn(22420102_sig)  ",           0, VWRN_TRAILING_SPACES,    RTFLOAT64U_INIT_SNAN_EX(1, 0x22420102) },
        { "-nAn(22420102_sig) 2",           0, VWRN_TRAILING_CHARS,     RTFLOAT64U_INIT_SNAN_EX(1, 0x22420102) },
        { "-1.2398039e-500",                0, VERR_FLOAT_UNDERFLOW,    RTFLOAT64U_INIT_ZERO(1) },
        { "-1.2398039e-5000",               0, VERR_FLOAT_UNDERFLOW,    RTFLOAT64U_INIT_ZERO(1) },
        { "-1.2398039e-50000",              0, VERR_FLOAT_UNDERFLOW,    RTFLOAT64U_INIT_ZERO(1) },
        { "-1.2398039e-500000",             0, VERR_FLOAT_UNDERFLOW,    RTFLOAT64U_INIT_ZERO(1) },
        { "-1.2398039e-500000000",          0, VERR_FLOAT_UNDERFLOW,    RTFLOAT64U_INIT_ZERO(1) },
        { "+1.7976931348623159e+308",       0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(0) },
        { "-1.7976931348623159e+308",       0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+309",                0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+350",                0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+400",                0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+450",                0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+500",                0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+5000",               0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+50000",              0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+500000",             0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
        { "-1.2398039e+5000000000",         0, VERR_FLOAT_OVERFLOW,     RTFLOAT64U_INIT_INF(1) },
    };
    for (unsigned i = 0; i < RT_ELEMENTS(s_aTstR64); i++)
    {
        //RTTestPrintf(hTest,RTTESTLVL_ALWAYS, "----- #%u: %s\n", i, s_aTstDouble[i].psz);
        RTFLOAT64U uRes = RTFLOAT64U_INIT_ZERO(1);
        char *pszNext = (char *)42;
        int rc = RTStrToDoubleEx(s_aTstR64[i].psz, &pszNext, s_aTstR64[i].cchMax, &uRes.rd);

        if (rc != s_aTstR64[i].rc || !RTFLOAT64U_ARE_IDENTICAL(&uRes, &s_aTstR64[i].r64))
        {
            RTStrFormatR64(szActual, sizeof(szActual), &uRes, 0, 0, RTSTR_F_SPECIAL);
            RTStrFormatR64(szExpect, sizeof(szExpect), &s_aTstR64[i].r64, 0, 0, RTSTR_F_SPECIAL);
            RTTestFailed(hTest, "RTStrToDoubleEx/%#u: '%s' L %u -> %Rrc & %s, expected %Rrc & %s\n",
                         i, s_aTstR64[i].psz, s_aTstR64[i].cchMax, rc, szActual, s_aTstR64[i].rc, szExpect);
        }
    }
#endif /* RT_OS_WINDOWS - debug elsewhere first */

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}
