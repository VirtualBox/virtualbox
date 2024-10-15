/* $Id: tstRTStrFormat.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - String formatting.
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
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <iprt/initterm.h>
#include <iprt/net.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/uuid.h>


/** See FNRTSTRFORMATTYPE. */
static DECLCALLBACK(size_t) TstType(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                    const char *pszType, void const *pvValue,
                                    int cchWidth, int cchPrecision, unsigned fFlags,
                                    void *pvUser)
{
    /* validate */
    if (strncmp(pszType, "type", 4))
        RTTestIFailed("pszType=%s expected 'typeN'\n", pszType);

    int iType = pszType[4] - '0';
    if ((uintptr_t)pvUser != (uintptr_t)TstType + iType)
        RTTestIFailed("pvValue=%p expected %p\n", pvUser, (void *)((uintptr_t)TstType + iType));

    /* format */
    size_t cch = pfnOutput(pvArgOutput, pszType, 5);
    cch += pfnOutput(pvArgOutput, "=", 1);
    char szNum[64];
    size_t cchNum = RTStrFormatNumber(szNum, (uintptr_t)pvValue, 10, cchWidth, cchPrecision, fFlags);
    cch += pfnOutput(pvArgOutput, szNum, cchNum);
    return cch;
}


static void testNested(int iLine, const char *pszExpect, const char *pszFormat, ...)
{
    size_t  cchExpect = strlen(pszExpect);
    char    szBuf[512];

    va_list va;
    va_start(va, pszFormat);
    size_t cch = RTStrPrintf(szBuf, sizeof(szBuf), "%N", pszFormat, &va);
    va_end(va);
    if (strcmp(szBuf, pszExpect))
        RTTestIFailed("at line %d: nested format '%s'\n"
                      "    output: '%s'\n"
                      "    wanted: '%s'\n",
                      iLine, pszFormat, szBuf, pszExpect);
    else if (cch != cchExpect)
        RTTestIFailed("at line %d: Invalid length %d returned, expected %u!\n",
                      iLine, cch, cchExpect);

    va_start(va, pszFormat);
    cch = RTStrPrintf(szBuf, sizeof(szBuf), "%uxxx%Nyyy%u", 43, pszFormat, &va, 43);
    va_end(va);
    if (   strncmp(szBuf, "43xxx", 5)
        || strncmp(szBuf + 5, pszExpect, cchExpect)
        || strcmp( szBuf + 5 + cchExpect, "yyy43") )
        RTTestIFailed("at line %d: nested format '%s'\n"
                      "    output: '%s'\n"
                      "    wanted: '43xxx%syyy43'\n",
                      iLine, pszFormat, szBuf, pszExpect);
    else if (cch != 5 + cchExpect + 5)
        RTTestIFailed("at line %d: Invalid length %d returned, expected %u!\n",
                      iLine, cch, 5 + cchExpect + 5);
}


static void testUtf16Printf(RTTEST hTest)
{
    RTTestSub(hTest, "RTUtf16Printf");
    size_t const   cwcBuf  = 120;
    PRTUTF16 const pwszBuf = (PRTUTF16)RTTestGuardedAllocTail(hTest, cwcBuf * sizeof(RTUTF16));

    static const char    s_szSimpleExpect[] = "Hello world!";
    static const ssize_t s_cwcSimpleExpect  = sizeof(s_szSimpleExpect) - 1;
    ssize_t cwc = RTUtf16Printf(pwszBuf, cwcBuf, "Hello%c%s!", ' ', "world");
    if (RTUtf16CmpAscii(pwszBuf, s_szSimpleExpect))
        RTTestIFailed("error: '%ls'\n"
                      "wanted '%s'\n", pwszBuf, s_szSimpleExpect);
    if (cwc != s_cwcSimpleExpect)
        RTTestIFailed("error: got %zd, expected %zd (#1)\n", cwc, s_cwcSimpleExpect);

    RTTestDisableAssertions(hTest);
    for (size_t cwcThisBuf = 0; cwcThisBuf < sizeof(s_szSimpleExpect) + 8; cwcThisBuf++)
    {
        memset(pwszBuf, 0x88, cwcBuf * sizeof(*pwszBuf));

        PRTUTF16 pwszThisBuf = &pwszBuf[cwcBuf - cwcThisBuf];
        cwc = RTUtf16Printf(pwszThisBuf, cwcThisBuf, "Hello%c%s!", ' ', "world");

        if (cwcThisBuf <= (size_t)s_cwcSimpleExpect)
        {
            if (cwcThisBuf > 1)
            {
                if (RTUtf16NCmpAscii(pwszThisBuf, s_szSimpleExpect, cwcThisBuf - 1))
                    RTTestIFailed("error: '%.*ls'\n"
                                  "wanted '%.*s'\n", cwcThisBuf - 1, pwszThisBuf, cwcThisBuf - 1, s_szSimpleExpect);
            }
            if (cwcThisBuf > 1 && pwszThisBuf[cwcThisBuf - 1] != '\0')
                RTTestIFailed("error: cwcThisBuf=%zu not null terminated! %#x\n", cwcThisBuf, pwszThisBuf[cwcThisBuf - 1]);
            if (cwc != -s_cwcSimpleExpect - 1)
                RTTestIFailed("error: cwcThisBuf=%zu got %zd, expected %zd (#1)\n", cwcThisBuf, cwc, -s_cwcSimpleExpect - 1);
        }
        else
        {
            if (RTUtf16CmpAscii(pwszThisBuf, s_szSimpleExpect))
                RTTestIFailed("error: '%ls'\n"
                              "wanted '%s'\n", pwszThisBuf, s_szSimpleExpect);
            if (cwc != s_cwcSimpleExpect)
                RTTestIFailed("error: cwcThisBuf=%zu got %zd, expected %zd (#1)\n", cwcThisBuf, cwc, s_cwcSimpleExpect);
        }
    }
    RTTestRestoreAssertions(hTest);
}



static void testAllocPrintf(RTTEST hTest)
{
    RTTestSub(hTest, "RTStrAPrintf");
    char *psz = (char *)~0;
    int cch3 = RTStrAPrintf(&psz, "Hey there! %s%s", "This is a test", "!");
    if (cch3 < 0)
        RTTestIFailed("RTStrAPrintf failed, cch3=%d\n", cch3);
    else if (strcmp(psz, "Hey there! This is a test!"))
        RTTestIFailed("RTStrAPrintf failed\n"
                      "got   : '%s'\n"
                      "wanted: 'Hey there! This is a test!'\n",
                      psz);
    else if ((int)strlen(psz) != cch3)
        RTTestIFailed("RTStrAPrintf failed, cch3 == %d expected %u\n", cch3, strlen(psz));
    RTStrFree(psz);
}


/*
 * This next portion used to all be in main() but gcc cannot handle
 * that in asan + -O2 mode.
 */



#define BUF_SIZE    120

/* This used to be very simple, but is not doing overflow handling checks and two APIs. */
#define CHECK42(fmt, arg, out) \
    do { \
        static const char g_szCheck42Fmt[]    = fmt " 42=%d " fmt " 42=%d" ; \
        static const char g_szCheck42Expect[] = out " 42=42 " out " 42=42" ; \
        \
        size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, g_szCheck42Fmt, arg, 42, arg, 42); \
        if (memcmp(pszBuf, g_szCheck42Expect, sizeof(g_szCheck42Expect)) != 0) \
            RTTestIFailed("at line %d: format '%s'\n" \
                          "    output: '%s'\n"  \
                          "    wanted: '%s'\n", \
                          __LINE__, fmt, pszBuf, g_szCheck42Expect); \
        else if (cch != sizeof(g_szCheck42Expect) - 1) \
            RTTestIFailed("at line %d: Invalid length %d returned, expected %u!\n", \
                          __LINE__, cch, sizeof(g_szCheck42Expect) - 1); \
        \
        RTTestIDisableAssertions(); \
        for (size_t cbBuf = 0; cbBuf <= BUF_SIZE; cbBuf++) \
        { \
            memset(pszBuf, 0xcc, BUF_SIZE); \
            const char   chAfter    = cbBuf != 0 ? '\0' : 0xcc; \
            const size_t cchCompare = cbBuf >= sizeof(g_szCheck42Expect) ? sizeof(g_szCheck42Expect) - 1 \
                                    : cbBuf > 0 ? cbBuf - 1 : 0; \
            size_t       cch1Expect = cchCompare; \
            ssize_t      cch2Expect = cbBuf >= sizeof(g_szCheck42Expect) \
                                    ? sizeof(g_szCheck42Expect) - 1 : -(ssize_t)sizeof(g_szCheck42Expect); \
            \
            cch = RTStrPrintf(pszBuf, cbBuf, g_szCheck42Fmt, arg, 42, arg, 42);\
            if (   memcmp(pszBuf, g_szCheck42Expect, cchCompare) != 0 \
                || pszBuf[cchCompare] != chAfter) \
                RTTestIFailed("at line %d: format '%s' (#1, cbBuf=%zu)\n" \
                              "    output: '%s'\n"  \
                              "    wanted: '%s'\n", \
                              __LINE__, fmt, cbBuf, cbBuf ? pszBuf : "", g_szCheck42Expect); \
            if (cch != cch1Expect) \
                 RTTestIFailed("at line %d: Invalid length %d returned for cbBuf=%zu, expected %zd! (#1)\n", \
                               __LINE__, cch, cbBuf, cch1Expect); \
            \
            ssize_t cch2 = RTStrPrintf2(pszBuf, cbBuf, g_szCheck42Fmt, arg, 42, arg, 42);\
            if (   memcmp(pszBuf, g_szCheck42Expect, cchCompare) != 0 \
                || pszBuf[cchCompare] != chAfter) \
                RTTestIFailed("at line %d: format '%s' (#2, cbBuf=%zu)\n" \
                              "    output: '%s'\n"  \
                              "    wanted: '%s'\n", \
                              __LINE__, fmt, cbBuf, cbBuf ? pszBuf : "", g_szCheck42Expect); \
            if (cch2 != cch2Expect) \
                RTTestIFailed("at line %d: Invalid length %d returned for cbBuf=%zu, expected %zd! (#2)\n", \
                               __LINE__, cch2, cbBuf, cch2Expect); \
        } \
        RTTestIRestoreAssertions(); \
    } while (0)

#define CHECKSTR(Correct) \
    if (strcmp(pszBuf, Correct)) \
        RTTestIFailed("error:    '%s'\n" \
                      "expected: '%s'\n", pszBuf, Correct);

static void testBasics(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "Basics");

    uint32_t    u32 = 0x010;
    uint64_t    u64 = 0x100;

    /* simple */
    static const char s_szSimpleExpect[] = "u32=16 u64=256 u64=0x100";
    size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, "u32=%d u64=%lld u64=%#llx", u32, u64, u64);
    if (strcmp(pszBuf, s_szSimpleExpect))
        RTTestIFailed("error: '%s'\n"
                      "wanted '%s'\n", pszBuf, s_szSimpleExpect);
    else if (cch != sizeof(s_szSimpleExpect) - 1)
        RTTestIFailed("error: got %zd, expected %zd (#1)\n", cch, sizeof(s_szSimpleExpect) - 1);

    ssize_t cch2 = RTStrPrintf2(pszBuf, BUF_SIZE, "u32=%d u64=%lld u64=%#llx", u32, u64, u64);
    if (strcmp(pszBuf, "u32=16 u64=256 u64=0x100"))
        RTTestIFailed("error: '%s' (#2)\n"
                      "wanted '%s' (#2)\n", pszBuf, s_szSimpleExpect);
    else if (cch2 != sizeof(s_szSimpleExpect) - 1)
        RTTestIFailed("error: got %zd, expected %zd (#2)\n", cch2, sizeof(s_szSimpleExpect) - 1);

    /* just big. */
    u64 = UINT64_C(0x7070605040302010);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "u64=%#llx 42=%d u64=%lld 42=%d", u64, 42, u64, 42);
    if (strcmp(pszBuf, "u64=0x7070605040302010 42=42 u64=8102081627430068240 42=42"))
    {
        RTTestIFailed("error: '%s'\n"
                      "wanted 'u64=0x8070605040302010 42=42 u64=8102081627430068240 42=42'\n", pszBuf);
        RTTestIPrintf(RTTESTLVL_FAILURE, "%d\n", (int)(u64 % 10));
    }

    /* huge and negative. */
    u64 = UINT64_C(0x8070605040302010);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "u64=%#llx 42=%d u64=%llu 42=%d u64=%lld 42=%d", u64, 42, u64, 42, u64, 42);
    /* Not sure if this is the correct decimal representation... But both */
    if (strcmp(pszBuf, "u64=0x8070605040302010 42=42 u64=9255003132036915216 42=42 u64=-9191740941672636400 42=42"))
    {
        RTTestIFailed("error: '%s'\n"
                      "wanted 'u64=0x8070605040302010 42=42 u64=9255003132036915216 42=42 u64=-9191740941672636400 42=42'\n", pszBuf);
        RTTestIPrintf(RTTESTLVL_FAILURE, "%d\n", (int)(u64 % 10));
    }

    /* 64-bit value bug. */
    u64 = 0xa0000000;
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "u64=%#llx 42=%d u64=%lld 42=%d", u64, 42, u64, 42);
    if (strcmp(pszBuf, "u64=0xa0000000 42=42 u64=2684354560 42=42"))
        RTTestIFailed("error: '%s'\n"
                      "wanted 'u64=0xa0000000 42=42 u64=2684354560 42=42'\n", pszBuf);

    /* uuid */
    RTUUID Uuid;
    RTUuidCreate(&Uuid);
    char szCorrect[RTUUID_STR_LENGTH];
    RTUuidToStr(&Uuid, szCorrect, sizeof(szCorrect));
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%RTuuid", &Uuid);
    if (strcmp(pszBuf, szCorrect))
        RTTestIFailed("error:    '%s'\n"
                      "expected: '%s'\n",
                      pszBuf, szCorrect);

    /* overflow */
    size_t const cbOverflowBuf = 40;
    static char const s_szOverflowExpect[] = "i=8                                    ";
    char *pszBuf3 = (char *)RTTestGuardedAllocTail(hTest, cbOverflowBuf);
    cch = RTStrPrintf(pszBuf3, cbOverflowBuf, "i=%d%*s\n", (int)8, cbOverflowBuf, "");
    if (strcmp(pszBuf3, s_szOverflowExpect))
        RTTestIFailed("error: '%s' (#2)\n"
                      "wanted '%s' (#2)\n", pszBuf, s_szOverflowExpect);
    else if (cch != sizeof(s_szOverflowExpect) - 1)
        RTTestIFailed("error: got %zd, expected %zd (#2)\n", cch2, sizeof(s_szOverflowExpect) - 1);
    RTTestGuardedFree(hTest, pszBuf3);
}


static void testRuntimeExtensions(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "Runtime format types (%R*)");
    CHECK42("%RGi", (RTGCINT)127, "127");
    CHECK42("%RGi", (RTGCINT)-586589, "-586589");

    CHECK42("%RGp", (RTGCPHYS)0x0000000044505045, "0000000044505045");
    CHECK42("%RGp", ~(RTGCPHYS)0, "ffffffffffffffff");

    CHECK42("%RGu", (RTGCUINT)586589, "586589");
    CHECK42("%RGu", (RTGCUINT)1, "1");
    CHECK42("%RGu", (RTGCUINT)3000000000U, "3000000000");

#if GC_ARCH_BITS == 32
    CHECK42("%RGv", (RTGCUINTPTR)0, "00000000");
    CHECK42("%RGv", ~(RTGCUINTPTR)0, "ffffffff");
    CHECK42("%RGv", (RTGCUINTPTR)0x84342134, "84342134");
#else
    CHECK42("%RGv", (RTGCUINTPTR)0, "0000000000000000");
    CHECK42("%RGv", ~(RTGCUINTPTR)0, "ffffffffffffffff");
    CHECK42("%RGv", (RTGCUINTPTR)0x84342134, "0000000084342134");
#endif

    CHECK42("%RGx", (RTGCUINT)0x234, "234");
    CHECK42("%RGx", (RTGCUINT)0xffffffff, "ffffffff");

    CHECK42("%RRv", (RTRCUINTPTR)0, "00000000");
    CHECK42("%RRv", ~(RTRCUINTPTR)0, "ffffffff");
    CHECK42("%RRv", (RTRCUINTPTR)0x84342134, "84342134");

    CHECK42("%RHi", (RTHCINT)127, "127");
    CHECK42("%RHi", (RTHCINT)-586589, "-586589");

    CHECK42("%RHp", (RTHCPHYS)0x0000000044505045, "0000000044505045");
    CHECK42("%RHp", ~(RTHCPHYS)0, "ffffffffffffffff");

    CHECK42("%RHu", (RTHCUINT)586589, "586589");
    CHECK42("%RHu", (RTHCUINT)1, "1");
    CHECK42("%RHu", (RTHCUINT)3000000000U, "3000000000");

    if (sizeof(void*) == 8)
    {
        CHECK42("%RHv", (RTHCUINTPTR)0, "0000000000000000");
        CHECK42("%RHv", ~(RTHCUINTPTR)0, "ffffffffffffffff");
        CHECK42("%RHv", (RTHCUINTPTR)0x84342134, "0000000084342134");
    }
    else
    {
        CHECK42("%RHv", (RTHCUINTPTR)0, "00000000");
        CHECK42("%RHv", ~(RTHCUINTPTR)0, "ffffffff");
        CHECK42("%RHv", (RTHCUINTPTR)0x84342134, "84342134");
    }

    CHECK42("%RHx", (RTHCUINT)0x234, "234");
    CHECK42("%RHx", (RTHCUINT)0xffffffff, "ffffffff");

    CHECK42("%RI16", (int16_t)1, "1");
    CHECK42("%RI16", (int16_t)-16384, "-16384");
    CHECK42("%RI16", INT16_MAX, "32767");
    CHECK42("%RI16", INT16_MIN, "-32768");

    CHECK42("%RI32", (int32_t)1123, "1123");
    CHECK42("%RI32", (int32_t)-86596, "-86596");
    CHECK42("%RI32", INT32_MAX, "2147483647");
    CHECK42("%RI32", INT32_MIN, "-2147483648");
    CHECK42("%RI32", INT32_MIN+1, "-2147483647");
    CHECK42("%RI32", INT32_MIN+2, "-2147483646");

    CHECK42("%RI64", (int64_t)112345987345LL, "112345987345");
    CHECK42("%RI64", (int64_t)-8659643985723459LL, "-8659643985723459");
    CHECK42("%RI64", INT64_MAX, "9223372036854775807");
    CHECK42("%RI64", INT64_MIN, "-9223372036854775808");
    CHECK42("%RI64", INT64_MIN+1, "-9223372036854775807");
    CHECK42("%RI64", INT64_MIN+2, "-9223372036854775806");

    CHECK42("%RI8", (int8_t)1, "1");
    CHECK42("%RI8", (int8_t)-128, "-128");

    CHECK42("%Rbn", "file.c", "file.c");
    CHECK42("%Rbn", "foo/file.c", "file.c");
    CHECK42("%Rbn", "/foo/file.c", "file.c");
    CHECK42("%Rbn", "/dir/subdir/", "subdir/");

    CHECK42("%Rfn", "function", "function");
    CHECK42("%Rfn", "void function(void)", "function");

    CHECK42("%RTfile", (RTFILE)127, "127");
    CHECK42("%RTfile", (RTFILE)12341234, "12341234");

    CHECK42("%RTfmode", (RTFMODE)0x123403, "00123403");

    CHECK42("%RTfoff", (RTFOFF)12342312, "12342312");
    CHECK42("%RTfoff", (RTFOFF)-123123123, "-123123123");
    CHECK42("%RTfoff", (RTFOFF)858694596874568LL, "858694596874568");

    RTFAR16 fp16;
    fp16.off = 0x34ff;
    fp16.sel = 0x0160;
    CHECK42("%RTfp16", fp16, "0160:34ff");

    RTFAR32 fp32;
    fp32.off = 0xff094030;
    fp32.sel = 0x0168;
    CHECK42("%RTfp32", fp32, "0168:ff094030");

    RTFAR64 fp64;
    fp64.off = 0xffff003401293487ULL;
    fp64.sel = 0x0ff8;
    CHECK42("%RTfp64", fp64, "0ff8:ffff003401293487");
    fp64.off = 0x0;
    fp64.sel = 0x0;
    CHECK42("%RTfp64", fp64, "0000:0000000000000000");

    CHECK42("%RTgid", (RTGID)-1, "-1");
    CHECK42("%RTgid", (RTGID)1004, "1004");

    CHECK42("%RTino", (RTINODE)0, "0000000000000000");
    CHECK42("%RTino", (RTINODE)0x123412341324ULL, "0000123412341324");

    CHECK42("%RTint", (RTINT)127, "127");
    CHECK42("%RTint", (RTINT)-586589, "-586589");
    CHECK42("%RTint", (RTINT)-23498723, "-23498723");

    CHECK42("%RTiop", (RTIOPORT)0x3c4, "03c4");
    CHECK42("%RTiop", (RTIOPORT)0xffff, "ffff");

    RTMAC Mac;
    Mac.au8[0] = 0;
    Mac.au8[1] = 0x1b;
    Mac.au8[2] = 0x21;
    Mac.au8[3] = 0x0a;
    Mac.au8[4] = 0x1d;
    Mac.au8[5] = 0xd9;
    CHECK42("%RTmac", &Mac, "00:1b:21:0a:1d:d9");
    Mac.au16[0] = 0xffff;
    Mac.au16[1] = 0xffff;
    Mac.au16[2] = 0xffff;
    CHECK42("%RTmac", &Mac, "ff:ff:ff:ff:ff:ff");

    RTNETADDRIPV4 Ipv4Addr;
    Ipv4Addr.u = RT_H2N_U32_C(0xf040d003);
    CHECK42("%RTnaipv4", Ipv4Addr.u, "240.64.208.3");
    Ipv4Addr.u = RT_H2N_U32_C(0xffffffff);
    CHECK42("%RTnaipv4", Ipv4Addr.u, "255.255.255.255");

    RTNETADDRIPV6 Ipv6Addr;

    /* any */
    memset(&Ipv6Addr, 0, sizeof(Ipv6Addr));
    CHECK42("%RTnaipv6", &Ipv6Addr, "::");

    /* loopback */
    Ipv6Addr.au8[15] = 1;
    CHECK42("%RTnaipv6", &Ipv6Addr, "::1");

    /* IPv4-compatible */
    Ipv6Addr.au8[12] = 1;
    Ipv6Addr.au8[13] = 1;
    Ipv6Addr.au8[14] = 1;
    Ipv6Addr.au8[15] = 1;
    CHECK42("%RTnaipv6", &Ipv6Addr, "::1.1.1.1");

    /* IPv4-mapped */
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0xffff);
    CHECK42("%RTnaipv6", &Ipv6Addr, "::ffff:1.1.1.1");

    /* IPv4-translated */
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0xffff);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0000);
    CHECK42("%RTnaipv6", &Ipv6Addr, "::ffff:0:1.1.1.1");

    /* single zero word is not abbreviated, leading zeroes are not printed */
    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x0001);
    CHECK42("%RTnaipv6", &Ipv6Addr, "0:1:0:1:0:1:0:1");

    /* longest run is abbreviated (here: at the beginning) */
    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x0000);
    CHECK42("%RTnaipv6", &Ipv6Addr, "::1:0:0:1:0");

    /* longest run is abbreviated (here: first) */
    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x0001);
    CHECK42("%RTnaipv6", &Ipv6Addr, "1::1:0:0:1");

    /* longest run is abbreviated (here: second) */
    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x0001);
    CHECK42("%RTnaipv6", &Ipv6Addr, "1:0:0:1::1");

    /* longest run is abbreviated (here: at the end) */
    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x0000);
    CHECK42("%RTnaipv6", &Ipv6Addr, "1:0:0:1::");

    /* first of the two runs of equal length is abbreviated */
    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x2001);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0db8);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0001);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x0001);
    CHECK42("%RTnaipv6", &Ipv6Addr, "2001:db8::1:0:0:1");

    Ipv6Addr.au16[0] = RT_H2N_U16_C(0x2001);
    Ipv6Addr.au16[1] = RT_H2N_U16_C(0x0db8);
    Ipv6Addr.au16[2] = RT_H2N_U16_C(0x85a3);
    Ipv6Addr.au16[3] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[4] = RT_H2N_U16_C(0x0000);
    Ipv6Addr.au16[5] = RT_H2N_U16_C(0x8a2e);
    Ipv6Addr.au16[6] = RT_H2N_U16_C(0x0370);
    Ipv6Addr.au16[7] = RT_H2N_U16_C(0x7334);
    CHECK42("%RTnaipv6", &Ipv6Addr, "2001:db8:85a3::8a2e:370:7334");

    Ipv6Addr.au64[0] = UINT64_MAX;
    Ipv6Addr.au64[1] = UINT64_MAX;
    CHECK42("%RTnaipv6", &Ipv6Addr, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");

    RTNETADDR NetAddr;
    memset(&NetAddr, 0, sizeof(NetAddr));

    /* plain IPv6 address if port is not specified */
    NetAddr.enmType = RTNETADDRTYPE_IPV6;
    NetAddr.uAddr.au16[0] = RT_H2N_U16_C(0x0001);
    NetAddr.uAddr.au16[7] = RT_H2N_U16_C(0x0001);
    NetAddr.uPort = RTNETADDR_PORT_NA;
    CHECK42("%RTnaddr", &NetAddr, "1::1");

    /* square brackets around IPv6 address if port is specified */
    NetAddr.uPort = 1;
    CHECK42("%RTnaddr", &NetAddr, "[1::1]:1");

    CHECK42("%RTproc", (RTPROCESS)0xffffff, "00ffffff");
    CHECK42("%RTproc", (RTPROCESS)0x43455443, "43455443");

#if (HC_ARCH_BITS == 64 || GC_ARCH_BITS == 64)
    CHECK42("%RTptr", (RTUINTPTR)0, "0000000000000000");
    CHECK42("%RTptr", ~(RTUINTPTR)0, "ffffffffffffffff");
    CHECK42("%RTptr", (RTUINTPTR)(uintptr_t)0x84342134, "0000000084342134");
#else
    CHECK42("%RTptr", (RTUINTPTR)0, "00000000");
    CHECK42("%RTptr", ~(RTUINTPTR)0, "ffffffff");
    CHECK42("%RTptr", (RTUINTPTR)(uintptr_t)0x84342134, "84342134");
#endif

#if ARCH_BITS == 64
    AssertCompileSize(RTCCUINTREG, 8);
    CHECK42("%RTreg", (RTCCUINTREG)0, "0000000000000000");
    CHECK42("%RTreg", ~(RTCCUINTREG)0, "ffffffffffffffff");
    CHECK42("%RTreg", (RTCCUINTREG)0x84342134, "0000000084342134");
    CHECK42("%RTreg", (RTCCUINTREG)0x23484342134ULL, "0000023484342134");
#elif ARCH_BITS == 32
    AssertCompileSize(RTCCUINTREG, 4);
    CHECK42("%RTreg", (RTCCUINTREG)0, "00000000");
    CHECK42("%RTreg", ~(RTCCUINTREG)0, "ffffffff");
    CHECK42("%RTreg", (RTCCUINTREG)0x84342134, "84342134");
#else
# error ARCH_BITS
#endif

    CHECK42("%RTsel", (RTSEL)0x543, "0543");
    CHECK42("%RTsel", (RTSEL)0xf8f8, "f8f8");

#if ARCH_BITS == 64
    CHECK42("%RTsem", (RTSEMEVENT)0, "0000000000000000");
    CHECK42("%RTsem", (RTSEMEVENT)(uintptr_t)0x23484342134ULL, "0000023484342134");
#else
    CHECK42("%RTsem", (RTSEMEVENT)0, "00000000");
    CHECK42("%RTsem", (RTSEMEVENT)(uintptr_t)0x84342134, "84342134");
#endif

    CHECK42("%RTsock", (RTSOCKET)(uintptr_t)12234, "12234");
    CHECK42("%RTsock", (RTSOCKET)(uintptr_t)584854543, "584854543");

#if ARCH_BITS == 64
    CHECK42("%RTthrd", (RTTHREAD)0, "0000000000000000");
    CHECK42("%RTthrd", (RTTHREAD)~(uintptr_t)0, "ffffffffffffffff");
    CHECK42("%RTthrd", (RTTHREAD)(uintptr_t)0x63484342134ULL, "0000063484342134");
#else
    CHECK42("%RTthrd", (RTTHREAD)0, "00000000");
    CHECK42("%RTthrd", (RTTHREAD)~(uintptr_t)0, "ffffffff");
    CHECK42("%RTthrd", (RTTHREAD)(uintptr_t)0x54342134, "54342134");
#endif

    CHECK42("%RTuid", (RTUID)-2, "-2");
    CHECK42("%RTuid", (RTUID)90344, "90344");

    CHECK42("%RTuint", (RTUINT)584589, "584589");
    CHECK42("%RTuint", (RTUINT)3, "3");
    CHECK42("%RTuint", (RTUINT)2400000000U, "2400000000");

    RTUUID Uuid;
    char szCorrect[RTUUID_STR_LENGTH];
    RTUuidCreate(&Uuid);
    RTUuidToStr(&Uuid, szCorrect, sizeof(szCorrect));
    RTStrPrintf(pszBuf, BUF_SIZE, "%RTuuid", &Uuid);
    if (strcmp(pszBuf, szCorrect))
        RTTestIFailed("error:    '%s'\n"
                      "expected: '%s'\n",
                      pszBuf, szCorrect);

    CHECK42("%RTxint", (RTUINT)0x2345, "2345");
    CHECK42("%RTxint", (RTUINT)0xffff8fff, "ffff8fff");

    CHECK42("%RU16", (uint16_t)7, "7");
    CHECK42("%RU16", (uint16_t)46384, "46384");

    CHECK42("%RU32", (uint32_t)1123, "1123");
    CHECK42("%RU32", (uint32_t)86596, "86596");
    CHECK42("%4RU32",  (uint32_t)42, "  42");
    CHECK42("%04RU32", (uint32_t)42, "0042");
    CHECK42("%.4RU32", (uint32_t)42, "0042");

    CHECK42("%RU64", (uint64_t)112345987345ULL, "112345987345");
    CHECK42("%RU64", (uint64_t)8659643985723459ULL, "8659643985723459");
    CHECK42("%14RU64",  (uint64_t)4, "             4");
    CHECK42("%014RU64", (uint64_t)4, "00000000000004");
    CHECK42("%.14RU64", (uint64_t)4, "00000000000004");

    CHECK42("%RU8", (uint8_t)1, "1");
    CHECK42("%RU8", (uint8_t)254, "254");
    CHECK42("%RU8", 256, "0");

    CHECK42("%RX16", (uint16_t)0x7, "7");
    CHECK42("%RX16", 0x46384, "6384");
    CHECK42("%RX16", UINT16_MAX, "ffff");

    CHECK42("%RX32", (uint32_t)0x1123, "1123");
    CHECK42("%RX32", (uint32_t)0x49939493, "49939493");
    CHECK42("%RX32", UINT32_MAX, "ffffffff");

    CHECK42("%RX64", UINT64_C(0x348734), "348734");
    CHECK42("%RX64", UINT64_C(0x12312312312343f), "12312312312343f");
    CHECK42("%RX64", UINT64_MAX, "ffffffffffffffff");
    CHECK42("%5RX64",   UINT64_C(0x42), "   42");
    CHECK42("%05RX64",  UINT64_C(0x42), "00042");
    CHECK42("%.5RX64",  UINT64_C(0x42), "00042");
    CHECK42("%.05RX64", UINT64_C(0x42), "00042"); /* '0' is ignored */

    CHECK42("%RX8", (uint8_t)1, "1");
    CHECK42("%RX8", (uint8_t)0xff, "ff");
    CHECK42("%RX8", UINT8_MAX, "ff");
    CHECK42("%RX8", 0x100, "0");
}

static void testThousandSeparators(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "Thousand Separators (%'*)");

    RTStrFormatNumber(pszBuf,       1, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("1");              memset(pszBuf, '!', BUF_SIZE);
    RTStrFormatNumber(pszBuf,      10, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("10");             memset(pszBuf, '!', BUF_SIZE);
    RTStrFormatNumber(pszBuf,     100, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("100");            memset(pszBuf, '!', BUF_SIZE);
    RTStrFormatNumber(pszBuf,    1000, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("1 000");          memset(pszBuf, '!', BUF_SIZE);
    RTStrFormatNumber(pszBuf,   10000, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("10 000");         memset(pszBuf, '!', BUF_SIZE);
    RTStrFormatNumber(pszBuf,  100000, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("100 000");        memset(pszBuf, '!', BUF_SIZE);
    RTStrFormatNumber(pszBuf, 1000000, 10, 0, 0, RTSTR_F_THOUSAND_SEP); CHECKSTR("1 000 000");      memset(pszBuf, '!', BUF_SIZE);

    CHECK42("%'u", 1,                              "1");
    CHECK42("%'u", 10,                            "10");
    CHECK42("%'u", 100,                          "100");
    CHECK42("%'u", 1000,                       "1 000");
    CHECK42("%'u", 10000,                     "10 000");
    CHECK42("%'u", 100000,                   "100 000");
    CHECK42("%'u", 1000000,                "1 000 000");
    CHECK42("%'RU64", _1T,         "1 099 511 627 776");
    CHECK42("%'RU64", _1E, "1 152 921 504 606 846 976");
}

static void testStringFormatter(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "String formatting (%s)");

//            0         1         2         3         4         5         6         7
//            0....5....0....5....0....5....0....5....0....5....0....5....0....5....0
    size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, "%-10s %-30s %s", "cmd", "args", "description");
    CHECKSTR("cmd        args                           description");

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%-10s %-30s %s", "cmd", "", "description");
    CHECKSTR("cmd                                       description");


    cch = RTStrPrintf(pszBuf, BUF_SIZE,  "%*s", 0, "");
    CHECKSTR("");

    /* automatic conversions. */
    static RTUNICP s_usz1[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', 0 }; //assumes ascii.
    static RTUTF16 s_wsz1[] = { 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', 0 }; //assumes ascii.

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%ls", s_wsz1);
    CHECKSTR("hello world");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Ls", s_usz1);
    CHECKSTR("hello world");

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.5ls", s_wsz1);
    CHECKSTR("hello");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.5Ls", s_usz1);
    CHECKSTR("hello");
}

static void testUnicodeStringFormatter(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "Unicode string formatting (%ls)");
    static RTUTF16 s_wszEmpty[]  = { 0 }; //assumes ascii.
    static RTUTF16 s_wszCmd[]    = { 'c', 'm', 'd', 0 }; //assumes ascii.
    static RTUTF16 s_wszArgs[]   = { 'a', 'r', 'g', 's', 0 }; //assumes ascii.
    static RTUTF16 s_wszDesc[]   = { 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n', 0 }; //assumes ascii.

//            0         1         2         3         4         5         6         7
//            0....5....0....5....0....5....0....5....0....5....0....5....0....5....0
    size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, "%-10ls %-30ls %ls", s_wszCmd, s_wszArgs, s_wszDesc);
    CHECKSTR("cmd        args                           description");

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%-10ls %-30ls %ls", s_wszCmd, s_wszEmpty, s_wszDesc);
    CHECKSTR("cmd                                       description");


#if 0
    static RTUNICP s_usz2[] = { 0xc5, 0xc6, 0xf8, 0 };
    static RTUTF16 s_wsz2[] = { 0xc5, 0xc6, 0xf8, 0 };
    static char    s_sz2[]  = { 0xc5, 0xc6, 0xf8, 0 };/// @todo multibyte tests.

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%ls", s_wsz2);
    CHECKSTR(s_sz2);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Ls", s_usz2);
    CHECKSTR(s_sz2);
#endif
}

static void testHexFormatter(RTTEST hTest, char *pszBuf, char *pszBuf2)
{
    RTTestSub(hTest, "Hex dump formatting (%Rhx*)");
    static uint8_t const s_abHex1[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
    size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.1Rhxs", s_abHex1);
    CHECKSTR("00");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.2Rhxs", s_abHex1);
    CHECKSTR("00 01");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Rhxs", s_abHex1);
    CHECKSTR("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.*Rhxs", sizeof(s_abHex1), s_abHex1);
    CHECKSTR("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%4.*Rhxs", sizeof(s_abHex1), s_abHex1);
    CHECKSTR("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%1.*Rhxs", sizeof(s_abHex1), s_abHex1);
    CHECKSTR("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%256.*Rhxs", sizeof(s_abHex1), s_abHex1);
    CHECKSTR("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%256.*RhXs", sizeof(s_abHex1), s_abHex1, (uint64_t)0x1234);
    CHECKSTR("00001234: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%256.*RhXs", sizeof(s_abHex1), s_abHex1, (uint64_t)UINT64_C(0x987654321abcdef));
    CHECKSTR("0987654321abcdef: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10 11 12 13 14");

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%4.8Rhxd", s_abHex1);
    RTStrPrintf(pszBuf2, BUF_SIZE,
                "%p/0000: 00 01 02 03 ....\n"
                "%p/0004: 04 05 06 07 ....",
                &s_abHex1[0], &s_abHex1[4]);
    CHECKSTR(pszBuf2);

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%4.6Rhxd", s_abHex1);
    RTStrPrintf(pszBuf2, BUF_SIZE,
                "%p/0000: 00 01 02 03 ....\n"
                "%p/0004: 04 05       ..",
                &s_abHex1[0], &s_abHex1[4]);
    CHECKSTR(pszBuf2);

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.*Rhxd", sizeof(s_abHex1), s_abHex1);
    RTStrPrintf(pszBuf2, BUF_SIZE,
                "%p/0000: 00 01 02 03 04 05 06 07-08 09 0a 0b 0c 0d 0e 0f ................\n"
                "%p/0010: 10 11 12 13 14                                  ....."
                ,
                &s_abHex1[0], &s_abHex1[0x10]);
    CHECKSTR(pszBuf2);

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.*RhXd", sizeof(s_abHex1), s_abHex1, (uint64_t)0xf304);
    RTStrPrintf(pszBuf2, BUF_SIZE,
                "0000f304/0000: 00 01 02 03 04 05 06 07-08 09 0a 0b 0c 0d 0e 0f ................\n"
                "0000f314/0010: 10 11 12 13 14                                  .....");
    CHECKSTR(pszBuf2);

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.*RhXd", sizeof(s_abHex1), s_abHex1, (uint64_t)UINT64_C(0x123456789abcdef));
    RTStrPrintf(pszBuf2, BUF_SIZE,
                "0123456789abcdef/0000: 00 01 02 03 04 05 06 07-08 09 0a 0b 0c 0d 0e 0f ................\n"
                "0123456789abcdff/0010: 10 11 12 13 14                                  .....");
    CHECKSTR(pszBuf2);
}

static void testHumanReadableNumbers(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "Human readable (%Rhc?, %Rhn?)");
    size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Rhcb%u", UINT64_C(1235467), 42);
    CHECKSTR("1.1MiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Rhcb%u", UINT64_C(999), 42);
    CHECKSTR("999B42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Rhcb%u", UINT64_C(8), 42);
    CHECKSTR("8B42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%Rhcb%u", UINT64_C(0), 42);
    CHECKSTR("0B42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.2Rhcb%u", UINT64_C(129957349834756374), 42);
    CHECKSTR("115.42PiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.3Rhcb%u", UINT64_C(1957349834756374), 42);
    CHECKSTR("1.738PiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%.0Rhcb%u", UINT64_C(1957349834756374), 42);
    CHECKSTR("1780TiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%10Rhcb%u", UINT64_C(6678345), 42);
    CHECKSTR("    6.3MiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%10Rhcb%u", UINT64_C(6710886), 42);
    CHECKSTR("    6.3MiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%10Rhcb%u", UINT64_C(6710887), 42);
    CHECKSTR("    6.4MiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "% 10Rhcb%u", UINT64_C(6710887), 42);
    CHECKSTR("   6.4 MiB42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "% 10RhcB%u", UINT64_C(6710887), 42);
    CHECKSTR("    6.4 MB42");

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%10Rhub%u", UINT64_C(6678345), 42);
    CHECKSTR("     6.3Mi42");
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%10RhuB%u", UINT64_C(6678345), 42);
    CHECKSTR("      6.3M42");

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%10Rhci%u", UINT64_C(6678345), 42);
    CHECKSTR("     6.7MB42"); /* rounded, unlike the binary variant.*/
}

static void testX86RegisterFormatter(RTTEST hTest, char *pszBuf)
{

    RTTestSub(hTest, "x86 register format types (%RAx86[*])");
    CHECK42("%RAx86[cr0]",  UINT64_C(0x80000011), "80000011{PE,ET,PG}");
    CHECK42("%RAx86[cr0]",  UINT64_C(0x80000001), "80000001{PE,PG}");
    CHECK42("%RAx86[cr0]",  UINT64_C(0x00000001), "00000001{PE}");
    CHECK42("%RAx86[cr0]",  UINT64_C(0x80000000), "80000000{PG}");
    CHECK42("%RAx86[cr4]",  UINT64_C(0x80000001), "80000001{VME,unkn=80000000}");
    CHECK42("%#RAx86[cr4]", UINT64_C(0x80000001), "0x80000001{VME,unkn=0x80000000}");
}

static void testCustomTypes(RTTEST hTest, char *pszBuf)
{
    RTTestSub(hTest, "Custom format types (%R[*])");
    RTTESTI_CHECK_RC(RTStrFormatTypeRegister("type3", TstType, (void *)((uintptr_t)TstType)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type3",           (void *)((uintptr_t)TstType + 3)), VINF_SUCCESS);
    size_t cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3]", (void *)1);
    CHECKSTR("type3=1");

    RTTESTI_CHECK_RC(RTStrFormatTypeRegister("type1", TstType, (void *)((uintptr_t)TstType)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type1",           (void *)((uintptr_t)TstType + 1)), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1]", (void *)1, (void *)2);
    CHECKSTR("type3=1 type1=2");

    RTTESTI_CHECK_RC(RTStrFormatTypeRegister("type4", TstType, (void *)((uintptr_t)TstType)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type4",           (void *)((uintptr_t)TstType + 4)), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1] %R[type4]", (void *)1, (void *)2, (void *)3);
    CHECKSTR("type3=1 type1=2 type4=3");

    RTTESTI_CHECK_RC(RTStrFormatTypeRegister("type2", TstType, (void *)((uintptr_t)TstType)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type2",           (void *)((uintptr_t)TstType + 2)), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1] %R[type4] %R[type2]", (void *)1, (void *)2, (void *)3, (void *)4);
    CHECKSTR("type3=1 type1=2 type4=3 type2=4");

    RTTESTI_CHECK_RC(RTStrFormatTypeRegister("type5", TstType, (void *)((uintptr_t)TstType)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type5",           (void *)((uintptr_t)TstType + 5)), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1] %R[type4] %R[type2] %R[type5]", (void *)1, (void *)2, (void *)3, (void *)4, (void *)5);
    CHECKSTR("type3=1 type1=2 type4=3 type2=4 type5=5");

    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type1",           (void *)((uintptr_t)TstType + 1)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type2",           (void *)((uintptr_t)TstType + 2)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type3",           (void *)((uintptr_t)TstType + 3)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type4",           (void *)((uintptr_t)TstType + 4)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTStrFormatTypeSetUser("type5",           (void *)((uintptr_t)TstType + 5)), VINF_SUCCESS);

    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1] %R[type4] %R[type2] %R[type5]", (void *)10, (void *)20, (void *)30, (void *)40, (void *)50);
    CHECKSTR("type3=10 type1=20 type4=30 type2=40 type5=50");

    RTTESTI_CHECK_RC(RTStrFormatTypeDeregister("type2"), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1] %R[type4] %R[type5]", (void *)10, (void *)20, (void *)30, (void *)40);
    CHECKSTR("type3=10 type1=20 type4=30 type5=40");

    RTTESTI_CHECK_RC(RTStrFormatTypeDeregister("type5"), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1] %R[type4]", (void *)10, (void *)20, (void *)30);
    CHECKSTR("type3=10 type1=20 type4=30");

    RTTESTI_CHECK_RC(RTStrFormatTypeDeregister("type4"), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3] %R[type1]", (void *)10, (void *)20);
    CHECKSTR("type3=10 type1=20");

    RTTESTI_CHECK_RC(RTStrFormatTypeDeregister("type1"), VINF_SUCCESS);
    cch = RTStrPrintf(pszBuf, BUF_SIZE, "%R[type3]", (void *)10);
    CHECKSTR("type3=10");

    RTTESTI_CHECK_RC(RTStrFormatTypeDeregister("type3"), VINF_SUCCESS);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTStrFormat", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    char       *pszBuf  = (char *)RTTestGuardedAllocHead(hTest, BUF_SIZE);
    char       *pszBuf2 = (char *)RTTestGuardedAllocHead(hTest, BUF_SIZE);

    /*
     * Do the basics.
     */
    testBasics(hTest, pszBuf);

    /*
     * Nested
     */
    RTTestSub(hTest, "Nested (%N)");
    testNested(__LINE__, "42 2684354560 42 asdf 42", "42 %u 42 %s 42", 2684354560U, "asdf");
    testNested(__LINE__, "", "");

    /*
     * allocation
     */
    testAllocPrintf(hTest);

    /*
     * Test the waters.
     */
    CHECK42("%d", 127, "127");
    CHECK42("%s", "721", "721");

    /*
     * Runtime extensions.
     */
    testRuntimeExtensions(hTest, pszBuf);

    /*
     * Thousand separators.
     */
    testThousandSeparators(hTest, pszBuf);

    /*
     * String formatting.
     */
    testStringFormatter(hTest, pszBuf);

    /*
     * Unicode string formatting.
     */
    testUnicodeStringFormatter(hTest, pszBuf);

    /*
     * Hex formatting.
     */
    testHexFormatter(hTest, pszBuf, pszBuf2);

    /*
     * human readable sizes and numbers.
     */
    testHumanReadableNumbers(hTest, pszBuf);

    /*
     * x86 register formatting.
     */
    testX86RegisterFormatter(hTest, pszBuf);

    /*
     * Custom types.
     */
    testCustomTypes(hTest, pszBuf);

    testUtf16Printf(hTest);

    /*
     * Summarize and exit.
     */
    return RTTestSummaryAndDestroy(hTest);
}

