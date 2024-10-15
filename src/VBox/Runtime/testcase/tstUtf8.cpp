/* $Id: tstUtf8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - UTF-8 and UTF-16 string conversions.
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
#include <iprt/latin1.h>
#include <iprt/utf16.h>

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/uni.h>
#include <iprt/uuid.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h> /* For GetACP(). */
#endif


/**
 * Generate a random codepoint for simple UTF-16 encoding.
 */
static RTUTF16 GetRandUtf16(void)
{
    RTUTF16 wc;
    do
    {
        wc = (RTUTF16)RTRandU32Ex(1, 0xfffd);
    } while (wc >= 0xd800 && wc <= 0xdfff);
    return wc;
}


/**
 *
 */
static void test1(RTTEST hTest)
{
    static const char s_szBadString1[] = "Bad \xe0\x13\x0";
    static const char s_szBadString2[] = "Bad \xef\xbf\xc3";
    int rc;
    char *pszUtf8;
    char *pszCurrent;
    PRTUTF16 pwsz;
    PRTUTF16 pwszRand;

    /*
     * Invalid UTF-8 to UCS-2 test.
     */
    RTTestSub(hTest, "Feeding bad UTF-8 to RTStrToUtf16");
    rc = RTStrToUtf16(s_szBadString1, &pwsz);
    RTTEST_CHECK_MSG(hTest, rc == VERR_NO_TRANSLATION || rc == VERR_INVALID_UTF8_ENCODING,
                     (hTest, "Conversion of first bad UTF-8 string to UTF-16 apparently succeeded. It shouldn't. rc=%Rrc\n", rc));
    rc = RTStrToUtf16(s_szBadString2, &pwsz);
    RTTEST_CHECK_MSG(hTest, rc == VERR_NO_TRANSLATION || rc == VERR_INVALID_UTF8_ENCODING,
                     (hTest, "Conversion of second bad UTF-8 strings to UTF-16 apparently succeeded. It shouldn't. rc=%Rrc\n", rc));

    /*
     * Test current CP conversion.
     */
    RTTestSub(hTest, "Rand UTF-16 -> UTF-8 -> CP -> UTF-8");
    pwszRand = (PRTUTF16)RTMemAlloc(31 * sizeof(*pwsz));
    for (int i = 0; i < 30; i++)
        pwszRand[i] = GetRandUtf16();
    pwszRand[30] = 0;

    rc = RTUtf16ToUtf8(pwszRand, &pszUtf8);
    if (rc == VINF_SUCCESS)
    {
        rc = RTStrUtf8ToCurrentCP(&pszCurrent, pszUtf8);
        if (rc == VINF_SUCCESS)
        {
            RTStrFree(pszUtf8);
            rc = RTStrCurrentCPToUtf8(&pszUtf8, pszCurrent);
            if (rc == VINF_SUCCESS)
                RTTestPassed(hTest, "Random UTF-16 -> UTF-8 -> Current -> UTF-8 successful.\n");
            else
                RTTestFailed(hTest, "%d: The third part of random UTF-16 -> UTF-8 -> Current -> UTF-8 failed with return value %Rrc.",
                             __LINE__, rc);
            if (RT_SUCCESS(rc))
                RTStrFree(pszUtf8);
            RTStrFree(pszCurrent);
        }
        else
        {
            if (rc == VERR_NO_TRANSLATION)
                RTTestPassed(hTest, "The second part of random UTF-16 -> UTF-8 -> Current -> UTF-8 returned VERR_NO_TRANSLATION.  This is probably as it should be.\n");
            else if (rc == VWRN_NO_TRANSLATION)
                RTTestPassed(hTest, "The second part of random UTF-16 -> UTF-8 -> Current -> UTF-8 returned VWRN_NO_TRANSLATION.  This is probably as it should be.\n");
            else
                RTTestFailed(hTest, "%d: The second part of random UTF-16 -> UTF-8 -> Current -> UTF-8 failed with return value %Rrc.",
                             __LINE__, rc);
            if (RT_SUCCESS(rc))
                RTStrFree(pszCurrent);
            RTStrFree(pszUtf8);
        }
    }
    else
        RTTestFailed(hTest, "%d: The first part of random UTF-16 -> UTF-8 -> Current -> UTF-8 failed with return value %Rrc.",
                     __LINE__, rc);
    RTMemFree(pwszRand);

    /*
     * Generate a new random string.
     */
    RTTestSub(hTest, "Random UTF-16 -> UTF-8 -> UTF-16");
    pwszRand = (PRTUTF16)RTMemAlloc(31 * sizeof(*pwsz));
    for (int i = 0; i < 30; i++)
        pwszRand[i] = GetRandUtf16();
    pwszRand[30] = 0;
    rc = RTUtf16ToUtf8(pwszRand, &pszUtf8);
    if (rc == VINF_SUCCESS)
    {
        rc = RTStrToUtf16(pszUtf8, &pwsz);
        if (rc == VINF_SUCCESS)
        {
            int i;
            for (i = 0; pwszRand[i] == pwsz[i] && pwsz[i] != 0; i++)
                /* nothing */;
            if (pwszRand[i] == pwsz[i] && pwsz[i] == 0)
                RTTestPassed(hTest, "Random UTF-16 -> UTF-8 -> UTF-16 successful.\n");
            else
            {
                RTTestFailed(hTest, "%d: The second part of random UTF-16 -> UTF-8 -> UTF-16 failed.", __LINE__);
                RTTestPrintf(hTest, RTTESTLVL_FAILURE, "First differing character is at position %d and has the value %x.\n", i, pwsz[i]);
            }
            RTUtf16Free(pwsz);
        }
        else
            RTTestFailed(hTest, "%d: The second part of random UTF-16 -> UTF-8 -> UTF-16 failed with return value %Rrc.",
                         __LINE__, rc);
        RTStrFree(pszUtf8);
    }
    else
        RTTestFailed(hTest, "%d: The first part of random UTF-16 -> UTF-8 -> UTF-16 failed with return value %Rrc.",
                     __LINE__, rc);
    RTMemFree(pwszRand);

    /*
     * Generate yet another random string and convert it to a buffer.
     */
    RTTestSub(hTest, "Random RTUtf16ToUtf8Ex + RTStrToUtf16");
    pwszRand = (PRTUTF16)RTMemAlloc(31 * sizeof(*pwsz));
    for (int i = 0; i < 30; i++)
        pwszRand[i] = GetRandUtf16();
    pwszRand[30] = 0;

    char szUtf8Array[120];
    char *pszUtf8Array  = szUtf8Array;
    rc = RTUtf16ToUtf8Ex(pwszRand, RTSTR_MAX, &pszUtf8Array, 120, NULL);
    if (rc == 0)
    {
        rc = RTStrToUtf16(pszUtf8Array, &pwsz);
        if (rc == 0)
        {
            int i;
            for (i = 0; pwszRand[i] == pwsz[i] && pwsz[i] != 0; i++)
                ;
            if (pwsz[i] == 0 && i >= 8)
                RTTestPassed(hTest, "Random UTF-16 -> fixed length UTF-8 -> UTF-16 successful.\n");
            else
            {
                RTTestFailed(hTest, "%d: Incorrect conversion of UTF-16 -> fixed length UTF-8 -> UTF-16.\n", __LINE__);
                RTTestPrintf(hTest, RTTESTLVL_FAILURE, "First differing character is at position %d and has the value %x.\n", i, pwsz[i]);
            }
            RTUtf16Free(pwsz);
        }
        else
            RTTestFailed(hTest, "%d: The second part of random UTF-16 -> fixed length UTF-8 -> UTF-16 failed with return value %Rrc.\n", __LINE__, rc);
    }
    else
        RTTestFailed(hTest, "%d: The first part of random UTF-16 -> fixed length UTF-8 -> UTF-16 failed with return value %Rrc.\n", __LINE__, rc);
    RTMemFree(pwszRand);

    /*
     * And again.
     */
    RTTestSub(hTest, "Random RTUtf16ToUtf8 + RTStrToUtf16Ex");
    pwszRand = (PRTUTF16)RTMemAlloc(31 * sizeof(*pwsz));
    for (int i = 0; i < 30; i++)
        pwszRand[i] = GetRandUtf16();
    pwszRand[30] = 0;

    RTUTF16     wszBuf[70];
    PRTUTF16    pwsz2Buf = wszBuf;
    rc = RTUtf16ToUtf8(pwszRand, &pszUtf8);
    if (rc == 0)
    {
        rc = RTStrToUtf16Ex(pszUtf8, RTSTR_MAX, &pwsz2Buf, 70, NULL);
        if (rc == 0)
        {
            int i;
            for (i = 0; pwszRand[i] == pwsz2Buf[i] && pwsz2Buf[i] != 0; i++)
                ;
            if (pwszRand[i] == 0 && pwsz2Buf[i] == 0)
                RTTestPassed(hTest, "Random UTF-16 -> UTF-8 -> fixed length UTF-16 successful.\n");
            else
            {
                RTTestFailed(hTest, "%d: Incorrect conversion of random UTF-16 -> UTF-8 -> fixed length UTF-16.\n", __LINE__);
                RTTestPrintf(hTest, RTTESTLVL_FAILURE, "First differing character is at position %d and has the value %x.\n", i, pwsz2Buf[i]);
            }
        }
        else
            RTTestFailed(hTest, "%d: The second part of random UTF-16 -> UTF-8 -> fixed length UTF-16 failed with return value %Rrc.\n", __LINE__, rc);
        RTStrFree(pszUtf8);
    }
    else
        RTTestFailed(hTest, "%d: The first part of random UTF-16 -> UTF-8 -> fixed length UTF-16 failed with return value %Rrc.\n",
                     __LINE__, rc);
    RTMemFree(pwszRand);

    pwszRand = (PRTUTF16)RTMemAlloc(31 * sizeof(*pwsz));
    for (int i = 0; i < 30; i++)
        pwszRand[i] = GetRandUtf16();
    pwszRand[30] = 0;

    rc = RTUtf16ToUtf8Ex(pwszRand, RTSTR_MAX, &pszUtf8Array, 20, NULL);
    if (rc == VERR_BUFFER_OVERFLOW)
        RTTestPassed(hTest, "Random UTF-16 -> fixed length UTF-8 with too short buffer successfully rejected.\n");
    else
        RTTestFailed(hTest, "%d: Random UTF-16 -> fixed length UTF-8 with too small buffer returned value %d instead of VERR_BUFFER_OVERFLOW.\n",
                     __LINE__, rc);
    RTMemFree(pwszRand);

    /*
     * last time...
     */
    RTTestSub(hTest, "Random RTUtf16ToUtf8 + RTStrToUtf16Ex");
    pwszRand = (PRTUTF16)RTMemAlloc(31 * sizeof(*pwsz));
    for (int i = 0; i < 30; i++)
        pwszRand[i] = GetRandUtf16();
    pwszRand[30] = 0;

    rc = RTUtf16ToUtf8(pwszRand, &pszUtf8);
    if (rc == VINF_SUCCESS)
    {
        rc = RTStrToUtf16Ex(pszUtf8, RTSTR_MAX, &pwsz2Buf, 20, NULL);
        if (rc == VERR_BUFFER_OVERFLOW)
            RTTestPassed(hTest, "Random UTF-16 -> UTF-8 -> fixed length UTF-16 with too short buffer successfully rejected.\n");
        else
            RTTestFailed(hTest, "%d: The second part of random UTF-16 -> UTF-8 -> fixed length UTF-16 with too short buffer returned value %Rrc instead of VERR_BUFFER_OVERFLOW.\n",
                         __LINE__, rc);
        RTStrFree(pszUtf8);
    }
    else
        RTTestFailed(hTest, "%d:The first part of random UTF-16 -> UTF-8 -> fixed length UTF-16 failed with return value %Rrc.\n",
                     __LINE__, rc);
    RTMemFree(pwszRand);

    RTTestSubDone(hTest);
}


static RTUNICP g_uszAll[0x110000 - 1 - 0x800 - 2 + 1];
static RTUTF16 g_wszAll[0xfffe - (0xe000 - 0xd800) + (0x110000 - 0x10000) * 2];
static char     g_szAll[0x7f + (0x800 - 0x80) * 2 + (0xfffe - 0x800 - (0xe000 - 0xd800))* 3 + (0x110000 - 0x10000) * 4 + 1];

static void whereami(int cBits, size_t off)
{
    if (cBits == 8)
    {
        if (off < 0x7f)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-8 U+%#x\n", off + 1);
        else if (off < 0xf7f)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-8 U+%#x\n", (off - 0x7f) / 2 + 0x80);
        else if (off < 0x27f7f)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-8 U+%#x\n", (off - 0xf7f) / 3 + 0x800);
        else if (off < 0x2df79)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-8 U+%#x\n", (off - 0x27f7f) / 3 + 0xe000);
        else if (off < 0x42df79)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-8 U+%#x\n", (off - 0x2df79) / 4 + 0x10000);
        else
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-8 ???\n");
    }
    else if (cBits == 16)
    {
        if (off < 0xd7ff*2)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-16 U+%#x\n", off / 2 + 1);
        else if (off < 0xf7fd*2)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-16 U+%#x\n", (off - 0xd7ff*2) / 2 + 0xe000);
        else if (off < 0x20f7fd)
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-16 U+%#x\n", (off - 0xf7fd*2) / 4 + 0x10000);
        else
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "UTF-16 ???\n");
    }
    else
    {
        if (off < (0xd800 - 1) * sizeof(RTUNICP))
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "RTUNICP U+%#x\n", off / sizeof(RTUNICP) + 1);
        else if (off < (0xfffe - 0x800 - 1) * sizeof(RTUNICP))
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "RTUNICP U+%#x\n", off / sizeof(RTUNICP) + 0x800 + 1);
        else
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "RTUNICP U+%#x\n", off / sizeof(RTUNICP) + 0x800 + 1 + 2);
    }
}

static int mymemcmp(const void *pv1, const void *pv2, size_t cb, int cBits)
{
    const uint8_t  *pb1 = (const uint8_t *)pv1;
    const uint8_t  *pb2 = (const uint8_t *)pv2;
    for (size_t off = 0; off < cb; off++)
    {
        if (pb1[off] != pb2[off])
        {
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "mismatch at %#x: ", off);
            whereami(cBits, off);
            if (off > 0)
                RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, " %#x: %02x != %02x!\n", off-1, pb1[off-1], pb2[off-1]);
            RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, "*%#x: %02x != %02x!\n", off,   pb1[off], pb2[off]);
            for (size_t i = 1; i < 10; i++)
                if (off + i < cb)
                    RTTestPrintf(NIL_RTTEST, RTTESTLVL_FAILURE, " %#x: %02x != %02x!\n", off+i, pb1[off+i], pb2[off+i]);
            return 1;
        }
    }
    return 0;
}


static void InitStrings()
{
    /*
     * Generate unicode string containing all the legal UTF-16 codepoints, both UTF-16 and UTF-8 version.
     */
    /* the simple code point array first */
    unsigned i = 0;
    RTUNICP  uc = 1;
    while (uc < 0xd800)
        g_uszAll[i++] = uc++;
    uc = 0xe000;
    while (uc < 0xfffe)
        g_uszAll[i++] = uc++;
    uc = 0x10000;
    while (uc < 0x110000)
        g_uszAll[i++] = uc++;
    g_uszAll[i++] = 0;
    Assert(RT_ELEMENTS(g_uszAll) == i);

    /* the utf-16 one */
    i = 0;
    uc = 1;
    //RTPrintf("tstUtf8: %#x=%#x", i, uc);
    while (uc < 0xd800)
        g_wszAll[i++] = uc++;
    uc = 0xe000;
    //RTPrintf(" %#x=%#x", i, uc);
    while (uc < 0xfffe)
        g_wszAll[i++] = uc++;
    uc = 0x10000;
    //RTPrintf(" %#x=%#x", i, uc);
    while (uc < 0x110000)
    {
        g_wszAll[i++] = 0xd800 | ((uc - 0x10000) >> 10);
        g_wszAll[i++] = 0xdc00 | ((uc - 0x10000) & 0x3ff);
        uc++;
    }
    //RTPrintf(" %#x=%#x\n", i, uc);
    g_wszAll[i++] = '\0';
    Assert(RT_ELEMENTS(g_wszAll) == i);

    /*
     * The utf-8 one
     */
    i = 0;
    uc = 1;
    //RTPrintf("tstUtf8: %#x=%#x", i, uc);
    while (uc < 0x80)
        g_szAll[i++] = uc++;
    //RTPrintf(" %#x=%#x", i, uc);
    while (uc < 0x800)
    {
        g_szAll[i++] = 0xc0 | (uc >> 6);
        g_szAll[i++] = 0x80 | (uc & 0x3f);
        Assert(!((uc >> 6) & ~0x1f));
        uc++;
    }
    //RTPrintf(" %#x=%#x", i, uc);
    while (uc < 0xd800)
    {
        g_szAll[i++] = 0xe0 |  (uc >> 12);
        g_szAll[i++] = 0x80 | ((uc >>  6) & 0x3f);
        g_szAll[i++] = 0x80 |  (uc & 0x3f);
        Assert(!((uc >> 12) & ~0xf));
        uc++;
    }
    uc = 0xe000;
    //RTPrintf(" %#x=%#x", i, uc);
    while (uc < 0xfffe)
    {
        g_szAll[i++] = 0xe0 |  (uc >> 12);
        g_szAll[i++] = 0x80 | ((uc >>  6) & 0x3f);
        g_szAll[i++] = 0x80 |  (uc & 0x3f);
        Assert(!((uc >> 12) & ~0xf));
        uc++;
    }
    uc = 0x10000;
    //RTPrintf(" %#x=%#x", i, uc);
    while (uc < 0x110000)
    {
        g_szAll[i++] = 0xf0 |  (uc >> 18);
        g_szAll[i++] = 0x80 | ((uc >> 12) & 0x3f);
        g_szAll[i++] = 0x80 | ((uc >>  6) & 0x3f);
        g_szAll[i++] = 0x80 |  (uc & 0x3f);
        Assert(!((uc >> 18) & ~0x7));
        uc++;
    }
    //RTPrintf(" %#x=%#x\n", i, uc);
    g_szAll[i++] = '\0';
    Assert(RT_ELEMENTS(g_szAll) == i);
}


static void test2(RTTEST hTest)
{
    /*
     * Convert to UTF-8 and back.
     */
    RTTestSub(hTest, "UTF-16 -> UTF-8 -> UTF-16");
    char *pszUtf8;
    int rc = RTUtf16ToUtf8(&g_wszAll[0], &pszUtf8);
    if (rc == VINF_SUCCESS)
    {
        pszUtf8[0] = 1;
        if (mymemcmp(pszUtf8, g_szAll, sizeof(g_szAll), 8))
            RTTestFailed(hTest, "UTF-16 -> UTF-8 mismatch!");

        PRTUTF16 pwszUtf16;
        rc = RTStrToUtf16(pszUtf8, &pwszUtf16);
        if (rc == VINF_SUCCESS)
        {
            if (mymemcmp(pwszUtf16, g_wszAll, sizeof(g_wszAll), 16))
                RTTestFailed(hTest, "UTF-8 -> UTF-16 failed compare!");
            RTUtf16Free(pwszUtf16);
        }
        else
            RTTestFailed(hTest, "UTF-8 -> UTF-16 failed, rc=%Rrc.", rc);
        RTStrFree(pszUtf8);
    }
    else
        RTTestFailed(hTest, "UTF-16 -> UTF-8 failed, rc=%Rrc.", rc);


    /*
     * Convert to UTF-16 and back. (just in case the above test fails)
     */
    RTTestSub(hTest, "UTF-8 -> UTF-16 -> UTF-8");
    PRTUTF16 pwszUtf16;
    rc = RTStrToUtf16(&g_szAll[0], &pwszUtf16);
    if (rc == VINF_SUCCESS)
    {
        if (mymemcmp(pwszUtf16, g_wszAll, sizeof(g_wszAll), 16))
            RTTestFailed(hTest, "UTF-8 -> UTF-16 failed compare!");

        rc = RTUtf16ToUtf8(pwszUtf16, &pszUtf8);
        if (rc == VINF_SUCCESS)
        {
            if (mymemcmp(pszUtf8, g_szAll, sizeof(g_szAll), 8))
                RTTestFailed(hTest, "UTF-16 -> UTF-8 failed compare!");
            RTStrFree(pszUtf8);
        }
        else
            RTTestFailed(hTest, "UTF-16 -> UTF-8 failed, rc=%Rrc.", rc);
        RTUtf16Free(pwszUtf16);
    }
    else
        RTTestFailed(hTest, "UTF-8 -> UTF-16 failed, rc=%Rrc.", rc);

    /*
     * Convert UTF-8 to CPs.
     */
    RTTestSub(hTest, "UTF-8 -> UNI -> UTF-8");
    PRTUNICP paCps;
    rc = RTStrToUni(g_szAll, &paCps);
    if (rc == VINF_SUCCESS)
    {
        if (mymemcmp(paCps, g_uszAll, sizeof(g_uszAll), 32))
            RTTestFailed(hTest, "UTF-8 -> UTF-16 failed, rc=%Rrc.", rc);

        size_t cCps;
        rc = RTStrToUniEx(g_szAll, RTSTR_MAX, &paCps, RT_ELEMENTS(g_uszAll), &cCps);
        if (rc == VINF_SUCCESS)
        {
            if (cCps != RT_ELEMENTS(g_uszAll) - 1)
                RTTestFailed(hTest, "wrong Code Point count %zu, expected %zu\n", cCps, RT_ELEMENTS(g_uszAll) - 1);
        }
        else
            RTTestFailed(hTest, "UTF-8 -> Code Points failed, rc=%Rrc.\n", rc);

        /** @todo RTCpsToUtf8 or something. */
        RTUniFree(paCps);
    }
    else
        RTTestFailed(hTest, "UTF-8 -> Code Points failed, rc=%Rrc.\n", rc);

    /*
     * Check the various string lengths.
     */
    RTTestSub(hTest, "Lengths");
    size_t cuc1 = RTStrCalcUtf16Len(g_szAll);
    size_t cuc2 = RTUtf16Len(g_wszAll);
    if (cuc1 != cuc2)
        RTTestFailed(hTest, "cuc1=%zu != cuc2=%zu\n", cuc1, cuc2);
    //size_t cuc3 = RTUniLen(g_uszAll);


    /*
     * Enumerate the strings.
     */
    RTTestSub(hTest, "Code Point Getters and Putters");
    char *pszPut1Base = (char *)RTMemAlloc(sizeof(g_szAll));
    AssertRelease(pszPut1Base);
    char *pszPut1 = pszPut1Base;
    PRTUTF16 pwszPut2Base = (PRTUTF16)RTMemAlloc(sizeof(g_wszAll));
    AssertRelease(pwszPut2Base);
    PRTUTF16 pwszPut2 = pwszPut2Base;
    const char *psz1 = g_szAll;
    const char *psz2 = g_szAll;
    PCRTUTF16   pwsz3 = g_wszAll;
    PCRTUTF16   pwsz4 = g_wszAll;
    for (;;)
    {
        /*
         * getters
         */
        RTUNICP uc1;
        rc = RTStrGetCpEx(&psz1, &uc1);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(hTest, "RTStrGetCpEx failed with rc=%Rrc at %.10Rhxs", rc, psz2);
            whereami(8, psz2 - &g_szAll[0]);
            break;
        }
        char *pszPrev1 = RTStrPrevCp(g_szAll, psz1);
        if (pszPrev1 != psz2)
        {
            RTTestFailed(hTest, "RTStrPrevCp returned %p expected %p!", pszPrev1, psz2);
            whereami(8, psz2 - &g_szAll[0]);
            break;
        }
        RTUNICP uc2 = RTStrGetCp(psz2);
        if (uc2 != uc1)
        {
            RTTestFailed(hTest, "RTStrGetCpEx and RTStrGetCp returned different CPs: %RTunicp != %RTunicp", uc2, uc1);
            whereami(8, psz2 - &g_szAll[0]);
            break;
        }
        psz2 = RTStrNextCp(psz2);
        if (psz2 != psz1)
        {
            RTTestFailed(hTest, "RTStrGetCpEx and RTStrGetNext returned different next pointer!");
            whereami(8, psz2 - &g_szAll[0]);
            break;
        }

        RTUNICP uc3;
        rc = RTUtf16GetCpEx(&pwsz3, &uc3);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(hTest, "RTUtf16GetCpEx failed with rc=%Rrc at %.10Rhxs", rc, pwsz4);
            whereami(16, pwsz4 - &g_wszAll[0]);
            break;
        }
        if (uc3 != uc2)
        {
            RTTestFailed(hTest, "RTUtf16GetCpEx and RTStrGetCp returned different CPs: %RTunicp != %RTunicp", uc3, uc2);
            whereami(16, pwsz4 - &g_wszAll[0]);
            break;
        }
        RTUNICP uc4 = RTUtf16GetCp(pwsz4);
        if (uc3 != uc4)
        {
            RTTestFailed(hTest, "RTUtf16GetCpEx and RTUtf16GetCp returned different CPs: %RTunicp != %RTunicp", uc3, uc4);
            whereami(16, pwsz4 - &g_wszAll[0]);
            break;
        }
        pwsz4 = RTUtf16NextCp(pwsz4);
        if (pwsz4 != pwsz3)
        {
            RTTestFailed(hTest, "RTUtf16GetCpEx and RTUtf16GetNext returned different next pointer!");
            whereami(8, pwsz4 - &g_wszAll[0]);
            break;
        }


        /*
         * putters
         */
        pszPut1 = RTStrPutCp(pszPut1, uc1);
        if (pszPut1 - pszPut1Base != psz1 - &g_szAll[0])
        {
            RTTestFailed(hTest, "RTStrPutCp is not at the same offset! %p != %p",
                         pszPut1 - pszPut1Base, psz1 - &g_szAll[0]);
            whereami(8, psz2 - &g_szAll[0]);
            break;
        }

        pwszPut2 = RTUtf16PutCp(pwszPut2, uc3);
        if (pwszPut2 - pwszPut2Base != pwsz3 - &g_wszAll[0])
        {
            RTTestFailed(hTest, "RTStrPutCp is not at the same offset! %p != %p",
                         pwszPut2 - pwszPut2Base, pwsz3 - &g_wszAll[0]);
            whereami(8, pwsz4 - &g_wszAll[0]);
            break;
        }


        /* the end? */
        if (!uc1)
            break;
    }

    /* check output if we seems to have made it thru it all. */
    if (psz2 == &g_szAll[sizeof(g_szAll)])
    {
        if (mymemcmp(pszPut1Base, g_szAll, sizeof(g_szAll), 8))
            RTTestFailed(hTest, "RTStrPutCp encoded the string incorrectly.");
        if (mymemcmp(pwszPut2Base, g_wszAll, sizeof(g_wszAll), 16))
            RTTestFailed(hTest, "RTUtf16PutCp encoded the string incorrectly.");
    }

    RTMemFree(pszPut1Base);
    RTMemFree(pwszPut2Base);

    RTTestSubDone(hTest);
}


/**
 * Check case insensitivity.
 */
static void test3(RTTEST hTest)
{
    RTTestSub(hTest, "Case Sensitivity");

    if (    RTUniCpToLower('a') != 'a'
        ||  RTUniCpToLower('A') != 'a'
        ||  RTUniCpToLower('b') != 'b'
        ||  RTUniCpToLower('B') != 'b'
        ||  RTUniCpToLower('Z') != 'z'
        ||  RTUniCpToLower('z') != 'z'
        ||  RTUniCpToUpper('c') != 'C'
        ||  RTUniCpToUpper('C') != 'C'
        ||  RTUniCpToUpper('z') != 'Z'
        ||  RTUniCpToUpper('Z') != 'Z')
        RTTestFailed(hTest, "RTUniToUpper/Lower failed basic tests.\n");

    if (RTUtf16ICmp(g_wszAll, g_wszAll))
        RTTestFailed(hTest, "RTUtf16ICmp failed the basic test.\n");

    if (RTUtf16Cmp(g_wszAll, g_wszAll))
        RTTestFailed(hTest, "RTUtf16Cmp failed the basic test.\n");

    static RTUTF16 s_wszTst1a[] = { 'a', 'B', 'c', 'D', 'E', 'f', 'g', 'h', 'i', 'j', 'K', 'L', 'm', 'N', 'o', 'P', 'q', 'r', 'S', 't', 'u', 'V', 'w', 'x', 'Y', 'Z', 0xc5, 0xc6, 0xf8, 0 };
    static RTUTF16 s_wszTst1b[] = { 'A', 'B', 'c', 'd', 'e', 'F', 'G', 'h', 'i', 'J', 'k', 'l', 'M', 'n', 'O', 'p', 'Q', 'R', 's', 't', 'U', 'v', 'w', 'X', 'y', 'z', 0xe5, 0xe6, 0xd8, 0 };
    if (    RTUtf16ICmp(s_wszTst1b, s_wszTst1b)
        ||  RTUtf16ICmp(s_wszTst1a, s_wszTst1a)
        ||  RTUtf16ICmp(s_wszTst1a, s_wszTst1b)
        ||  RTUtf16ICmp(s_wszTst1b, s_wszTst1a)
        )
        RTTestFailed(hTest, "RTUtf16ICmp failed the alphabet test.\n");

    if (    RTUtf16Cmp(s_wszTst1b, s_wszTst1b)
        ||  RTUtf16Cmp(s_wszTst1a, s_wszTst1a)
        ||  !RTUtf16Cmp(s_wszTst1a, s_wszTst1b)
        ||  !RTUtf16Cmp(s_wszTst1b, s_wszTst1a)
        )
        RTTestFailed(hTest, "RTUtf16Cmp failed the alphabet test.\n");

    RTTestSubDone(hTest);
}


/**
 * Test the RTStr*Cmp functions.
 */
static void TstRTStrXCmp(RTTEST hTest)
{
#define CHECK_DIFF(expr, op) \
    do \
    { \
        int iDiff = expr; \
        if (!(iDiff op 0)) \
            RTTestFailed(hTest, "%d: %d " #op " 0: %s\n", __LINE__, iDiff, #expr); \
    } while (0)

/** @todo test the non-ascii bits. */

    RTTestSub(hTest, "RTStrCmp");
    CHECK_DIFF(RTStrCmp(NULL, NULL), == );
    CHECK_DIFF(RTStrCmp(NULL, ""), < );
    CHECK_DIFF(RTStrCmp("", NULL), > );
    CHECK_DIFF(RTStrCmp("", ""), == );
    CHECK_DIFF(RTStrCmp("abcdef", "abcdef"), == );
    CHECK_DIFF(RTStrCmp("abcdef", "abcde"), > );
    CHECK_DIFF(RTStrCmp("abcde", "abcdef"), < );
    CHECK_DIFF(RTStrCmp("abcdeg", "abcdef"), > );
    CHECK_DIFF(RTStrCmp("abcdef", "abcdeg"), < );
    CHECK_DIFF(RTStrCmp("abcdeF", "abcdef"), < );
    CHECK_DIFF(RTStrCmp("abcdef", "abcdeF"), > );


    RTTestSub(hTest, "RTStrNCmp");
    CHECK_DIFF(RTStrNCmp(NULL, NULL, RTSTR_MAX), == );
    CHECK_DIFF(RTStrNCmp(NULL, "", RTSTR_MAX), < );
    CHECK_DIFF(RTStrNCmp("", NULL, RTSTR_MAX), > );
    CHECK_DIFF(RTStrNCmp("", "", RTSTR_MAX), == );
    CHECK_DIFF(RTStrNCmp("abcdef", "abcdef", RTSTR_MAX), == );
    CHECK_DIFF(RTStrNCmp("abcdef", "abcde", RTSTR_MAX), > );
    CHECK_DIFF(RTStrNCmp("abcde", "abcdef", RTSTR_MAX), < );
    CHECK_DIFF(RTStrNCmp("abcdeg", "abcdef", RTSTR_MAX), > );
    CHECK_DIFF(RTStrNCmp("abcdef", "abcdeg", RTSTR_MAX), < );
    CHECK_DIFF(RTStrNCmp("abcdeF", "abcdef", RTSTR_MAX), < );
    CHECK_DIFF(RTStrNCmp("abcdef", "abcdeF", RTSTR_MAX), > );

    CHECK_DIFF(RTStrNCmp("abcdef", "fedcba", 0), ==);
    CHECK_DIFF(RTStrNCmp("abcdef", "abcdeF", 5), ==);
    CHECK_DIFF(RTStrNCmp("abcdef", "abcdeF", 6), > );


    RTTestSub(hTest, "RTStrICmp");
    CHECK_DIFF(RTStrICmp(NULL, NULL), == );
    CHECK_DIFF(RTStrICmp(NULL, ""), < );
    CHECK_DIFF(RTStrICmp("", NULL), > );
    CHECK_DIFF(RTStrICmp("", ""), == );
    CHECK_DIFF(RTStrICmp("abcdef", "abcdef"), == );
    CHECK_DIFF(RTStrICmp("abcdef", "abcde"), > );
    CHECK_DIFF(RTStrICmp("abcde", "abcdef"), < );
    CHECK_DIFF(RTStrICmp("abcdeg", "abcdef"), > );
    CHECK_DIFF(RTStrICmp("abcdef", "abcdeg"), < );

    CHECK_DIFF(RTStrICmp("abcdeF", "abcdef"), ==);
    CHECK_DIFF(RTStrICmp("abcdef", "abcdeF"), ==);
    CHECK_DIFF(RTStrICmp("ABCDEF", "abcdef"), ==);
    CHECK_DIFF(RTStrICmp("abcdef", "ABCDEF"), ==);
    CHECK_DIFF(RTStrICmp("AbCdEf", "aBcDeF"), ==);
    CHECK_DIFF(RTStrICmp("AbCdEg", "aBcDeF"), > );
    CHECK_DIFF(RTStrICmp("AbCdEG", "aBcDef"), > ); /* diff performed on the lower case cp. */


    RTTestSub(hTest, "RTStrICmpAscii");
    CHECK_DIFF(RTStrICmpAscii(NULL, NULL), == );
    CHECK_DIFF(RTStrICmpAscii(NULL, ""), < );
    CHECK_DIFF(RTStrICmpAscii("", NULL), > );
    CHECK_DIFF(RTStrICmpAscii("", ""), == );
    CHECK_DIFF(RTStrICmpAscii("abcdef", "abcdef"), == );
    CHECK_DIFF(RTStrICmpAscii("abcdef", "abcde"), > );
    CHECK_DIFF(RTStrICmpAscii("abcde", "abcdef"), < );
    CHECK_DIFF(RTStrICmpAscii("abcdeg", "abcdef"), > );
    CHECK_DIFF(RTStrICmpAscii("abcdef", "abcdeg"), < );

    CHECK_DIFF(RTStrICmpAscii("abcdeF", "abcdef"), ==);
    CHECK_DIFF(RTStrICmpAscii("abcdef", "abcdeF"), ==);
    CHECK_DIFF(RTStrICmpAscii("ABCDEF", "abcdef"), ==);
    CHECK_DIFF(RTStrICmpAscii("abcdef", "ABCDEF"), ==);
    CHECK_DIFF(RTStrICmpAscii("AbCdEf", "aBcDeF"), ==);
    CHECK_DIFF(RTStrICmpAscii("AbCdEg", "aBcDeF"), > );
    CHECK_DIFF(RTStrICmpAscii("AbCdEG", "aBcDef"), > ); /* diff performed on the lower case cp. */


    RTTestSub(hTest, "RTStrNICmp");
    CHECK_DIFF(RTStrNICmp(NULL, NULL, RTSTR_MAX), == );
    CHECK_DIFF(RTStrNICmp(NULL, "", RTSTR_MAX), < );
    CHECK_DIFF(RTStrNICmp("", NULL, RTSTR_MAX), > );
    CHECK_DIFF(RTStrNICmp("", "", RTSTR_MAX), == );
    CHECK_DIFF(RTStrNICmp(NULL, NULL, 0), == );
    CHECK_DIFF(RTStrNICmp(NULL, "", 0), == );
    CHECK_DIFF(RTStrNICmp("", NULL, 0), == );
    CHECK_DIFF(RTStrNICmp("", "", 0), == );
    CHECK_DIFF(RTStrNICmp("abcdef", "abcdef", RTSTR_MAX), == );
    CHECK_DIFF(RTStrNICmp("abcdef", "abcde", RTSTR_MAX), > );
    CHECK_DIFF(RTStrNICmp("abcde", "abcdef", RTSTR_MAX), < );
    CHECK_DIFF(RTStrNICmp("abcdeg", "abcdef", RTSTR_MAX), > );
    CHECK_DIFF(RTStrNICmp("abcdef", "abcdeg", RTSTR_MAX), < );

    CHECK_DIFF(RTStrNICmp("abcdeF", "abcdef", RTSTR_MAX), ==);
    CHECK_DIFF(RTStrNICmp("abcdef", "abcdeF", RTSTR_MAX), ==);
    CHECK_DIFF(RTStrNICmp("ABCDEF", "abcdef", RTSTR_MAX), ==);
    CHECK_DIFF(RTStrNICmp("abcdef", "ABCDEF", RTSTR_MAX), ==);
    CHECK_DIFF(RTStrNICmp("AbCdEf", "aBcDeF", RTSTR_MAX), ==);
    CHECK_DIFF(RTStrNICmp("AbCdEg", "aBcDeF", RTSTR_MAX), > );
    CHECK_DIFF(RTStrNICmp("AbCdEG", "aBcDef", RTSTR_MAX), > ); /* diff performed on the lower case cp. */

    CHECK_DIFF(RTStrNICmp("ABCDEF", "fedcba", 0), ==);
    CHECK_DIFF(RTStrNICmp("AbCdEg", "aBcDeF", 5), ==);
    CHECK_DIFF(RTStrNICmp("AbCdEf", "aBcDeF", 5), ==);
    CHECK_DIFF(RTStrNICmp("AbCdE",  "aBcDe", 5), ==);
    CHECK_DIFF(RTStrNICmp("AbCdE",  "aBcDeF", 5), ==);
    CHECK_DIFF(RTStrNICmp("AbCdEf", "aBcDe", 5), ==);
    CHECK_DIFF(RTStrNICmp("AbCdEg", "aBcDeF", 6), > );
    CHECK_DIFF(RTStrNICmp("AbCdEG", "aBcDef", 6), > ); /* diff performed on the lower case cp. */
    /* We should continue using byte comparison when we hit the invalid CP.  Will assert in debug builds. */
    // CHECK_DIFF(RTStrNICmp("AbCd\xff""eg", "aBcD\xff""eF", 6), ==);

    RTTestSubDone(hTest);
}



/**
 * Check UTF-8 encoding purging.
 */
static void TstRTStrPurgeEncoding(RTTEST hTest)
{
    RTTestSub(hTest, "RTStrPurgeEncoding");

    /*
     * Test some good strings.
     */
    char sz1[] = "1234567890wertyuiopsdfghjklzxcvbnm";
    char sz1Copy[sizeof(sz1)];
    memcpy(sz1Copy, sz1, sizeof(sz1));

    RTTESTI_CHECK_RETV(RTStrPurgeEncoding(sz1) == 0);
    RTTESTI_CHECK_RETV(!memcmp(sz1, sz1Copy, sizeof(sz1)));

    char *pszAll = RTStrDup(g_szAll);
    if (pszAll)
    {
        RTTESTI_CHECK(RTStrPurgeEncoding(pszAll) == 0);
        RTTESTI_CHECK(!memcmp(pszAll, g_szAll, sizeof(g_szAll)));
        RTStrFree(pszAll);
    }

    /*
     * Test some bad stuff.
     */
    struct
    {
        size_t          cErrors;
        unsigned char   szIn[5];
        const char     *pszExpect;
    } aTests[] =
    {
        { 0, {  '1',  '2',  '3',  '4', '\0' }, "1234" },
        { 1, { 0x80,  '2',  '3',  '4', '\0' }, "?234" },
        { 1, {  '1', 0x80,  '3',  '4', '\0' }, "1?34" },
        { 1, {  '1',  '2', 0x80,  '4', '\0' }, "12?4" },
        { 1, {  '1',  '2',  '3', 0x80, '\0' }, "123?" },
        { 2, { 0x80, 0x81,  '3',  '4', '\0' }, "??34" },
        { 2, {  '1', 0x80, 0x81,  '4', '\0' }, "1??4" },
        { 2, {  '1',  '2', 0x80, 0x81, '\0' }, "12??" },
    };
    for (size_t i = 0; i < RT_ELEMENTS(aTests); i++)
    {
        size_t cErrors = RTStrPurgeEncoding((char *)aTests[i].szIn);
        if (cErrors != aTests[i].cErrors)
            RTTestFailed(hTest, "#%u: cErrors=%u expected %u\n", i, cErrors, aTests[i].cErrors);
        else if (strcmp((char *)aTests[i].szIn, aTests[i].pszExpect))
            RTTestFailed(hTest, "#%u: %.5Rhxs expected %.5Rhxs (%s)\n", i, aTests[i].szIn, aTests[i].pszExpect, aTests[i].pszExpect);
    }

    RTTestSubDone(hTest);
}


/**
 * Check string sanitising.
 */
static void TstRTStrPurgeComplementSet(RTTEST hTest)
{
    RTTestSub(hTest, "RTStrPurgeComplementSet");
    RTUNICP aCpSet[]    = { '1', '5', 'w', 'w', 'r', 'r', 'e', 'f', 't', 't',
                            '\0' };
    RTUNICP aCpBadSet[] = { '1', '5', 'w', 'w', 'r', 'r', 'e', 'f', 't', 't',
                            '7', '\0' };  /* Contains an incomplete pair. */
    struct
    {
        const char *pcszIn;
        const char *pcszOut;
        PCRTUNICP   pcCpSet;
        char        chReplacement;
        ssize_t     cExpected;
    }
    aTests[] =
    {
        { "1234werttrew4321", "1234werttrew4321", aCpSet, '_', 0 },
        { "123654wert\xc2\xa2trew\xe2\x82\xac""4321",
          "123_54wert__trew___4321", aCpSet, '_', 3 },
        { "hjhj8766", "????????", aCpSet, '?', 8 },
        { "123\xf0\xa4\xad\xa2""4", "123____4", aCpSet, '_', 1 },
        { "\xff", "\xff", aCpSet, '_', -1 },
        { "____", "____", aCpBadSet, '_', -1 }
    };
    enum { MAX_IN_STRING = 256 };

    for (unsigned i = 0; i < RT_ELEMENTS(aTests); ++i)
    {
        char szCopy[MAX_IN_STRING];
        ssize_t cReplacements;
        AssertRC(RTStrCopy(szCopy, RT_ELEMENTS(szCopy), aTests[i].pcszIn));
        RTTestDisableAssertions(hTest);
        cReplacements = RTStrPurgeComplementSet(szCopy, aTests[i].pcCpSet, aTests[i].chReplacement);
        RTTestRestoreAssertions(hTest);
        if (cReplacements != aTests[i].cExpected)
            RTTestFailed(hTest, "#%u: expected %lld, actual %lld\n", i,
                         (long long) aTests[i].cExpected,
                         (long long) cReplacements);
        if (strcmp(aTests[i].pcszOut, szCopy))
            RTTestFailed(hTest, "#%u: expected %s, actual %s\n", i,
                         aTests[i].pcszOut, szCopy);
    }
}


/**
 * Check string sanitising.
 */
static void TstRTUtf16PurgeComplementSet(RTTEST hTest)
{
    RTTestSub(hTest, "RTUtf16PurgeComplementSet");
    RTUNICP aCpSet[]    = { '1', '5', 'w', 'w', 'r', 'r', 'e', 'f', 't', 't',
                            '\0' };
    RTUNICP aCpBadSet[] = { '1', '5', 'w', 'w', 'r', 'r', 'e', 'f', 't', 't',
                            '7', '\0' };  /* Contains an incomplete pair. */
    struct
    {
        const char *pcszIn;
        const char *pcszOut;
        size_t      cwc;  /* Zero means the strings are Utf-8. */
        PCRTUNICP   pcCpSet;
        char        chReplacement;
        ssize_t     cExpected;
    }
    aTests[] =
    {
        { "1234werttrew4321", "1234werttrew4321", 0, aCpSet, '_', 0 },
        { "123654wert\xc2\xa2trew\xe2\x82\xac""4321",
          "123_54wert_trew_4321", 0, aCpSet, '_', 3 },
        { "hjhj8766", "????????", 0, aCpSet, '?', 8 },
        { "123\xf0\xa4\xad\xa2""4", "123__4", 0, aCpSet, '_', 1 },
        { "\xff\xff\0", "\xff\xff\0", 2, aCpSet, '_', -1 },
        { "\xff\xff\0", "\xff\xff\0", 2, aCpSet, '_', -1 },
        { "____", "____", 0, aCpBadSet, '_', -1 }
    };
    enum { MAX_IN_STRING = 256 };

    for (unsigned i = 0; i < RT_ELEMENTS(aTests); ++i)
    {
        RTUTF16 wszInCopy[MAX_IN_STRING],  *pwszInCopy  = wszInCopy;
        RTUTF16 wszOutCopy[MAX_IN_STRING], *pwszOutCopy = wszOutCopy;
        ssize_t cReplacements;
        if (!aTests[i].cwc)
        {
            AssertRC(RTStrToUtf16Ex(aTests[i].pcszIn, RTSTR_MAX, &pwszInCopy,
                                    RT_ELEMENTS(wszInCopy), NULL));
            AssertRC(RTStrToUtf16Ex(aTests[i].pcszOut, RTSTR_MAX, &pwszOutCopy,
                                    RT_ELEMENTS(wszOutCopy), NULL));
        }
        else
        {
            Assert(aTests[i].cwc <= RT_ELEMENTS(wszInCopy));
            memcpy(wszInCopy, aTests[i].pcszIn, aTests[i].cwc * 2);
            memcpy(wszOutCopy, aTests[i].pcszOut, aTests[i].cwc * 2);
        }

        RTTestDisableAssertions(hTest);
        cReplacements = RTUtf16PurgeComplementSet(wszInCopy, aTests[i].pcCpSet, aTests[i].chReplacement);
        RTTestRestoreAssertions(hTest);

        if (cReplacements != aTests[i].cExpected)
            RTTestFailed(hTest, "#%u: expected %lld, actual %lld\n", i,
                         (long long) aTests[i].cExpected,
                         (long long) cReplacements);
        if (RTUtf16Cmp(wszInCopy, wszOutCopy))
            RTTestFailed(hTest, "#%u: expected %ls, actual %ls\n", i,
                         wszOutCopy, wszInCopy);
    }
}


/**
 * Benchmark stuff.
 */
static void Benchmarks(RTTEST hTest)
{
    static union
    {
        RTUTF16 wszBuf[sizeof(g_wszAll)];
        char szBuf[sizeof(g_szAll)];
    } s_Buf;

    RTTestSub(hTest, "Benchmarks");
/** @todo add RTTest* methods for reporting benchmark results. */
    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Benchmarking RTStrToUtf16Ex:  "); /** @todo figure this stuff into the test framework. */
    PRTUTF16 pwsz = &s_Buf.wszBuf[0];
    int rc = RTStrToUtf16Ex(&g_szAll[0], RTSTR_MAX, &pwsz, RT_ELEMENTS(s_Buf.wszBuf), NULL);
    if (RT_SUCCESS(rc))
    {
        int i;
        uint64_t u64Start = RTTimeNanoTS();
        for (i = 0; i < 100; i++)
        {
            rc = RTStrToUtf16Ex(&g_szAll[0], RTSTR_MAX, &pwsz, RT_ELEMENTS(s_Buf.wszBuf), NULL);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(hTest, "UTF-8 -> UTF-16 benchmark failed at i=%d, rc=%Rrc\n", i, rc);
                break;
            }
        }
        uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%d in %'RI64 ns\n", i, u64Elapsed);
    }

    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Benchmarking RTUtf16ToUtf8Ex: ");
    char *psz = &s_Buf.szBuf[0];
    rc = RTUtf16ToUtf8Ex(&g_wszAll[0], RTSTR_MAX, &psz, RT_ELEMENTS(s_Buf.szBuf), NULL);
    if (RT_SUCCESS(rc))
    {
        int i;
        uint64_t u64Start = RTTimeNanoTS();
        for (i = 0; i < 100; i++)
        {
            rc = RTUtf16ToUtf8Ex(&g_wszAll[0], RTSTR_MAX, &psz, RT_ELEMENTS(s_Buf.szBuf), NULL);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(hTest, "UTF-16 -> UTF-8 benchmark failed at i=%d, rc=%Rrc\n", i, rc);
                break;
            }
        }
        uint64_t u64Elapsed = RTTimeNanoTS() - u64Start;
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%d in %'RI64 ns\n", i, u64Elapsed);
    }

    RTTestSubDone(hTest);
}


/**
 * Tests RTStrEnd
 */
static void testStrEnd(RTTEST hTest)
{
    RTTestSub(hTest, "RTStrEnd");

    static char const s_szEmpty[1] = "";
    RTTESTI_CHECK(RTStrEnd(s_szEmpty, 0) == NULL);
    RTTESTI_CHECK(RTStrEnd(s_szEmpty, 1) == &s_szEmpty[0]);
    for (size_t i = 0; i < _1M; i++)
        RTTESTI_CHECK(RTStrEnd(s_szEmpty, ~i) == &s_szEmpty[0]);

    /* Check the implementation won't ever overshoot the '\0' in the input in
       anyway that may lead to a SIGSEV. (VC++ 14.1 does this) */
    size_t const cchStr = 1023;
    char *pszStr = (char *)RTTestGuardedAllocTail(hTest, cchStr + 1);
    memset(pszStr, ' ', cchStr);
    char * const pszStrEnd = &pszStr[cchStr];
    *pszStrEnd = '\0';
    RTTEST_CHECK_RETV(hTest, strlen(pszStr) == cchStr);

    for (size_t off = 0; off <= cchStr; off++)
    {
        RTTEST_CHECK(hTest, RTStrEnd(&pszStr[off], cchStr + 1 - off) == pszStrEnd);
        RTTEST_CHECK(hTest, RTStrEnd(&pszStr[off], RTSTR_MAX) == pszStrEnd);

        RTTEST_CHECK(hTest, memchr(&pszStr[off], '\0', cchStr + 1 - off) == pszStrEnd);
        RTTEST_CHECK(hTest, strchr(&pszStr[off], '\0') == pszStrEnd);
        RTTEST_CHECK(hTest, strchr(&pszStr[off], '?') == NULL);

        size_t cchMax = 0;
        for (; cchMax <= cchStr - off; cchMax++)
        {
            const char *pszRet = RTStrEnd(&pszStr[off], cchMax);
            if (pszRet != NULL)
            {
                RTTestFailed(hTest, "off=%zu cchMax=%zu: %p, expected NULL\n", off, cchMax, pszRet);
                break;
            }
        }
        for (; cchMax <= _8K; cchMax++)
        {
            const char *pszRet = RTStrEnd(&pszStr[off], cchMax);
            if (pszRet != pszStrEnd)
            {
                RTTestFailed(hTest, "off=%zu cchMax=%zu: off by %p\n", off, cchMax, pszRet);
                break;
            }
        }
    }
    RTTestGuardedFree(hTest, pszStr);
}


/**
 * Tests RTStrStr and RTStrIStr.
 */
static void testStrStr(RTTEST hTest)
{
#define CHECK_NULL(expr) \
    do { \
        const char *pszRet = expr; \
        if (pszRet != NULL) \
            RTTestFailed(hTest, "%d: %s -> %s expected NULL", __LINE__, #expr, pszRet); \
    } while (0)

#define CHECK(expr, expect) \
    do { \
        const char * const pszRet = expr; \
        const char * const pszExpect = (expect); \
        if (   (pszRet != NULL && pszExpect == NULL) \
            || (pszRet == NULL && pszExpect != NULL) \
            || strcmp(pszRet, pszExpect) \
            ) \
            RTTestFailed(hTest, "%d: %s -> %s expected %s", __LINE__, #expr, pszRet, pszExpect); \
    } while (0)


    RTTestSub(hTest, "RTStrStr");
    CHECK(RTStrStr("abcdef", ""), "abcdef");
    CHECK_NULL(RTStrStr("abcdef", NULL));
    CHECK_NULL(RTStrStr(NULL, ""));
    CHECK_NULL(RTStrStr(NULL, NULL));
    CHECK(RTStrStr("abcdef", "abcdef"), "abcdef");
    CHECK(RTStrStr("abcdef", "b"), "bcdef");
    CHECK(RTStrStr("abcdef", "bcdef"), "bcdef");
    CHECK(RTStrStr("abcdef", "cdef"), "cdef");
    CHECK(RTStrStr("abcdef", "cde"), "cdef");
    CHECK(RTStrStr("abcdef", "cd"), "cdef");
    CHECK(RTStrStr("abcdef", "c"), "cdef");
    CHECK(RTStrStr("abcdef", "f"), "f");
    CHECK(RTStrStr("abcdef", "ef"), "ef");
    CHECK(RTStrStr("abcdef", "e"), "ef");
    CHECK_NULL(RTStrStr("abcdef", "z"));
    CHECK_NULL(RTStrStr("abcdef", "A"));
    CHECK_NULL(RTStrStr("abcdef", "F"));

    RTTestSub(hTest, "RTStrIStr");
    CHECK(RTStrIStr("abcdef", ""), "abcdef");
    CHECK_NULL(RTStrIStr("abcdef", NULL));
    CHECK_NULL(RTStrIStr(NULL, ""));
    CHECK_NULL(RTStrIStr(NULL, NULL));
    CHECK(RTStrIStr("abcdef", "abcdef"), "abcdef");
    CHECK(RTStrIStr("abcdef", "Abcdef"), "abcdef");
    CHECK(RTStrIStr("abcdef", "ABcDeF"), "abcdef");
    CHECK(RTStrIStr("abcdef", "b"), "bcdef");
    CHECK(RTStrIStr("abcdef", "B"), "bcdef");
    CHECK(RTStrIStr("abcdef", "bcdef"), "bcdef");
    CHECK(RTStrIStr("abcdef", "BCdEf"), "bcdef");
    CHECK(RTStrIStr("abcdef", "bCdEf"), "bcdef");
    CHECK(RTStrIStr("abcdef", "bcdEf"), "bcdef");
    CHECK(RTStrIStr("abcdef", "BcdEf"), "bcdef");
    CHECK(RTStrIStr("abcdef", "cdef"), "cdef");
    CHECK(RTStrIStr("abcdef", "cde"), "cdef");
    CHECK(RTStrIStr("abcdef", "cd"), "cdef");
    CHECK(RTStrIStr("abcdef", "c"), "cdef");
    CHECK(RTStrIStr("abcdef", "f"), "f");
    CHECK(RTStrIStr("abcdeF", "F"), "F");
    CHECK(RTStrIStr("abcdef", "F"), "f");
    CHECK(RTStrIStr("abcdef", "ef"), "ef");
    CHECK(RTStrIStr("EeEef", "e"), "EeEef");
    CHECK(RTStrIStr("EeEef", "E"), "EeEef");
    CHECK(RTStrIStr("EeEef", "EE"), "EeEef");
    CHECK(RTStrIStr("EeEef", "EEE"), "EeEef");
    CHECK(RTStrIStr("EeEef", "EEEF"), "eEef");
    CHECK_NULL(RTStrIStr("EeEef", "z"));

#undef CHECK
#undef CHECK_NULL
    RTTestSubDone(hTest);
}


static void testUtf8Latin1(RTTEST hTest)
{
    RTTestSub(hTest, "Latin-1 <-> Utf-8 conversion functions");

    /* Test Utf8 -> Latin1 */
    size_t cch_szAll = 0;
    size_t cbShort = RTStrCalcLatin1Len(g_szAll);
    RTTEST_CHECK(hTest, cbShort == 0);
    int rc = RTStrCalcLatin1LenEx(g_szAll, 383, &cch_szAll);
    RTTEST_CHECK(hTest, (cch_szAll == 255));
    rc = RTStrCalcLatin1LenEx(g_szAll, RTSTR_MAX, &cch_szAll);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_TRANSLATION);
    char *psz = NULL;
    char szShort[256] = { 0 };
    memcpy(szShort, g_szAll, 255);
    cbShort = RTStrCalcLatin1Len(szShort);
    RTTEST_CHECK(hTest, cbShort == 191);
    rc = RTStrToLatin1(szShort, &psz);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (strlen(psz) == 191));
        for (unsigned i = 0, j = 1; psz[i] != '\0'; ++i, ++j)
            if (psz[i] != (char) j)
            {
                RTTestFailed(hTest, "conversion of g_szAll to Latin1 failed at position %u\n", i);
                break;
            }
    }
    RTStrFree(psz);
    rc = RTStrToLatin1(g_szAll, &psz);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_TRANSLATION);
    char sz[512];
    char *psz2 = &sz[0];
    size_t cchActual = 0;
    rc = RTStrToLatin1Ex(g_szAll, sizeof(sz) - 1, &psz2, sizeof(sz),
                          &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_TRANSLATION);
    RTTEST_CHECK_MSG(hTest, cchActual == 0,
                     (hTest, "cchActual=%lu\n", cchActual));
    rc = RTStrToLatin1Ex(g_szAll, 383, &psz2, sizeof(sz),
                          &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 255));
        RTTEST_CHECK(hTest, (cchActual == strlen(sz)));
        for (unsigned i = 0, j = 1; psz2[i] != '\0'; ++i, ++j)
            if (psz2[i] != (char) j)
            {
                RTTestFailed(hTest, "second conversion of g_szAll to Latin1 failed at position %u\n", i);
                break;
            }
    }
    rc = RTStrToLatin1Ex(g_szAll, 129, &psz2, 128, &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_BUFFER_OVERFLOW);
    RTTEST_CHECK_MSG(hTest, cchActual == 128,
                     (hTest, "cchActual=%lu\n", cchActual));
    rc = RTStrToLatin1Ex(g_szAll, 383, &psz, 0, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 255));
        RTTEST_CHECK(hTest, (cchActual == strlen(psz)));
        for (unsigned i = 0, j = 1; psz[i] != '\0'; ++i, ++j)
            if (   ((j < 0x100) && (psz[i] != (char) j))
                || ((j > 0xff) && psz[i] != '?'))
            {
                RTTestFailed(hTest, "third conversion of g_szAll to Latin1 failed at position %u\n", i);
                break;
            }
    }
    const char *pszBad = "Hello\xDC\xD8";
    rc = RTStrToLatin1Ex(pszBad, RTSTR_MAX, &psz2, sizeof(sz),
                           &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_INVALID_UTF8_ENCODING);
    RTStrFree(psz);

    /* Test Latin1 -> Utf8 */
    const char *pszLat1 = "\x01\x20\x40\x80\x81";
    RTTEST_CHECK(hTest, RTLatin1CalcUtf8Len(pszLat1) == 7);
    rc = RTLatin1CalcUtf8LenEx(pszLat1, 3, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest, cchActual == 3);
    rc = RTLatin1CalcUtf8LenEx(pszLat1, RTSTR_MAX, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest, cchActual == 7);
    char *pch = NULL;
    char ch[8];
    char *pch2 = &ch[0];
    cchActual = 0;
    rc = RTLatin1ToUtf8(pszLat1, &pch);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest, !strcmp(pch, "\x01\x20\x40\xC2\x80\xC2\x81"));
    RTStrFree(pch);
    rc = RTLatin1ToUtf8Ex(pszLat1, RTSTR_MAX, &pch, 0, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 7));
        RTTEST_CHECK(hTest, !strcmp(pch, "\x01\x20\x40\xC2\x80\xC2\x81"));
    }
    RTStrFree(pch);
    rc = RTLatin1ToUtf8Ex(pszLat1, RTSTR_MAX, &pch, 0, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest, !strcmp(pch, "\x01\x20\x40\xC2\x80\xC2\x81"));
    RTStrFree(pch);
    rc = RTLatin1ToUtf8Ex(pszLat1, RTSTR_MAX, &pch2, RT_ELEMENTS(ch),
                          &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 7));
        RTTEST_CHECK(hTest, !strcmp(pch2, "\x01\x20\x40\xC2\x80\xC2\x81"));
    }
    rc = RTLatin1ToUtf8Ex(pszLat1, 3, &pch2, RT_ELEMENTS(ch),
                           &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 3));
        RTTEST_CHECK(hTest, !strcmp(pch2, "\x01\x20\x40"));
    }
    rc = RTLatin1ToUtf8Ex(pszLat1, RTSTR_MAX, &pch2, RT_ELEMENTS(ch) - 1,
                          &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_BUFFER_OVERFLOW);
    RTTEST_CHECK(hTest, (cchActual == 7));
    RTTestSubDone(hTest);
}


static void testUtf16Latin1(RTTEST hTest)
{
    RTTestSub(hTest, "Latin-1 <-> Utf-16 conversion functions");

    /* Test Utf16 -> Latin1 */
    size_t cch_szAll = 0;
    size_t cbShort = RTUtf16CalcLatin1Len(g_wszAll);
    RTTEST_CHECK(hTest, cbShort == 0);
    int rc = RTUtf16CalcLatin1LenEx(g_wszAll, 255, &cch_szAll);
    RTTEST_CHECK(hTest, (cch_szAll == 255));
    rc = RTUtf16CalcLatin1LenEx(g_wszAll, RTSTR_MAX, &cch_szAll);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_TRANSLATION);
    char *psz = NULL;
    RTUTF16 wszShort[256] = { 0 };
    for (unsigned i = 0; i < 255; ++i)
        wszShort[i] = i + 1;
    cbShort = RTUtf16CalcLatin1Len(wszShort);
    RTTEST_CHECK(hTest, cbShort == 255);
    rc = RTUtf16ToLatin1(wszShort, &psz);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (strlen(psz) == 255));
        for (unsigned i = 0, j = 1; psz[i] != '\0'; ++i, ++j)
            if (psz[i] != (char) j)
            {
                RTTestFailed(hTest, "conversion of g_wszAll to Latin1 failed at position %u\n", i);
                break;
            }
    }
    RTStrFree(psz);
    rc = RTUtf16ToLatin1(g_wszAll, &psz);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_TRANSLATION);
    char sz[512];
    char *psz2 = &sz[0];
    size_t cchActual = 0;
    rc = RTUtf16ToLatin1Ex(g_wszAll, sizeof(sz) - 1, &psz2, sizeof(sz),
                           &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_TRANSLATION);
    RTTEST_CHECK_MSG(hTest, cchActual == 0,
                     (hTest, "cchActual=%lu\n", cchActual));
    rc = RTUtf16ToLatin1Ex(g_wszAll, 255, &psz2, sizeof(sz),
                           &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 255));
        RTTEST_CHECK(hTest, (cchActual == strlen(sz)));
        for (unsigned i = 0, j = 1; psz2[i] != '\0'; ++i, ++j)
            if (psz2[i] != (char) j)
            {
                RTTestFailed(hTest, "second conversion of g_wszAll to Latin1 failed at position %u\n", i);
                break;
            }
    }
    rc = RTUtf16ToLatin1Ex(g_wszAll, 128, &psz2, 128, &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_BUFFER_OVERFLOW);
    RTTEST_CHECK_MSG(hTest, cchActual == 128,
                     (hTest, "cchActual=%lu\n", cchActual));
    rc = RTUtf16ToLatin1Ex(g_wszAll, 255, &psz, 0, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cchActual == 255));
        RTTEST_CHECK(hTest, (cchActual == strlen(psz)));
        for (unsigned i = 0, j = 1; psz[i] != '\0'; ++i, ++j)
            if (   ((j < 0x100) && (psz[i] != (char) j))
                || ((j > 0xff) && psz[i] != '?'))
            {
                RTTestFailed(hTest, "third conversion of g_wszAll to Latin1 failed at position %u\n", i);
                break;
            }
    }
    const char *pszBad = "H\0e\0l\0l\0o\0\0\xDC\0\xD8\0";
    rc = RTUtf16ToLatin1Ex((RTUTF16 *) pszBad, RTSTR_MAX, &psz2, sizeof(sz),
                           &cchActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_INVALID_UTF16_ENCODING);
    RTStrFree(psz);

    /* Test Latin1 -> Utf16 */
    const char *pszLat1 = "\x01\x20\x40\x80\x81";
    RTTEST_CHECK(hTest, RTLatin1CalcUtf16Len(pszLat1) == 5);
    rc = RTLatin1CalcUtf16LenEx(pszLat1, 3, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest, cchActual == 3);
    rc = RTLatin1CalcUtf16LenEx(pszLat1, RTSTR_MAX, &cchActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest, cchActual == 5);
    RTUTF16 *pwc = NULL;
    RTUTF16 wc[6];
    RTUTF16 *pwc2 = &wc[0];
    size_t cwActual = 0;
    rc = RTLatin1ToUtf16(pszLat1, &pwc);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest,    (pwc[0] == 1) && (pwc[1] == 0x20)
                            && (pwc[2] == 0x40) && (pwc[3] == 0x80)
                            && (pwc[4] == 0x81) && (pwc[5] == '\0'));
    RTUtf16Free(pwc);
    rc = RTLatin1ToUtf16Ex(pszLat1, RTSTR_MAX, &pwc, 0, &cwActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cwActual == 5));
        RTTEST_CHECK(hTest,    (pwc[0] == 1) && (pwc[1] == 0x20)
                            && (pwc[2] == 0x40) && (pwc[3] == 0x80)
                            && (pwc[4] == 0x81) && (pwc[5] == '\0'));
    }
    RTUtf16Free(pwc);
    rc = RTLatin1ToUtf16Ex(pszLat1, RTSTR_MAX, &pwc, 0, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
        RTTEST_CHECK(hTest,    (pwc[0] == 1) && (pwc[1] == 0x20)
                            && (pwc[2] == 0x40) && (pwc[3] == 0x80)
                            && (pwc[4] == 0x81) && (pwc[5] == '\0'));
    RTUtf16Free(pwc);
    rc = RTLatin1ToUtf16Ex(pszLat1, RTSTR_MAX, &pwc2, RT_ELEMENTS(wc),
                           &cwActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cwActual == 5));
        RTTEST_CHECK(hTest,    (wc[0] == 1) && (wc[1] == 0x20)
                            && (wc[2] == 0x40) && (wc[3] == 0x80)
                            && (wc[4] == 0x81) && (wc[5] == '\0'));
    }
    rc = RTLatin1ToUtf16Ex(pszLat1, 3, &pwc2, RT_ELEMENTS(wc),
                           &cwActual);
    RTTEST_CHECK_RC_OK(hTest, rc);
    if (RT_SUCCESS(rc))
    {
        RTTEST_CHECK(hTest, (cwActual == 3));
        RTTEST_CHECK(hTest,    (wc[0] == 1) && (wc[1] == 0x20)
                            && (wc[2] == 0x40) && (wc[3] == '\0'));
    }
    rc = RTLatin1ToUtf16Ex(pszLat1, RTSTR_MAX, &pwc2, RT_ELEMENTS(wc) - 1,
                           &cwActual);
    RTTEST_CHECK_RC(hTest, rc, VERR_BUFFER_OVERFLOW);
    RTTEST_CHECK(hTest, (cwActual == 5));
    RTTestSubDone(hTest);
}


static void testNoTranslation(RTTEST hTest)
{
    /*
     * Try trigger a VERR_NO_TRANSLATION error in convert to
     * current CP to latin-1.
     *
     * On Windows / DOS OSes this is codepage 850.
     *
     * Note! On Windows-y systems there ALWAYS are two codepages active:
     *       the OEM codepage for legacy (console) applications, and the ACP (ANSI CodePage).
     *       'chcp' only will tell you the OEM codepage, however.
     */

    /* Unicode code points (some of it on 2300-23FF -> misc. technical) to try. */
    const RTUTF16 s_swzTest1[] = { 0x2358, 0x2242, 0x2357, 0x2359,  0x22f9, 0x2c4e, 0x0030, 0x0060,
                                   0x0092, 0x00c1, 0x00f2, 0x1f80,  0x0088, 0x2c38, 0x2c30, 0x0000 };
    char *pszTest1;
    int rc = RTUtf16ToUtf8(s_swzTest1, &pszTest1);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

#ifdef RT_OS_WINDOWS
    UINT const uACP = GetACP();
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Current Windows ANSI codepage is: %u%s\n",
                  uACP, uACP == 65001 /* UTF-8 */ ? " (UTF-8)" : "");
#endif

    RTTestSub(hTest, "VERR_NO_TRANSLATION/RTStrUtf8ToCurrentCP");
    char *pszOut;
    rc = RTStrUtf8ToCurrentCP(&pszOut, pszTest1);
    if (rc == VINF_SUCCESS)
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS, "CurrentCP is UTF-8 or similar (LC_ALL=%s LANG=%s LC_CTYPE=%s)\n",
                      RTEnvGet("LC_ALL"), RTEnvGet("LANG"), RTEnvGet("LC_CTYPE"));
#ifdef RT_OS_WINDOWS
        if (uACP == 65001 /* UTF-8 */)
        {
            /* The following string comparison will fail if the active ACP isn't UTF-8 (65001), so skip this then.
             * This applies to older Windows OSes like NT4. */
#endif
            if (strcmp(pszOut, pszTest1))
                RTTestFailed(hTest, "mismatch\nutf8: %.*Rhxs\n got: %.*Rhxs\n", strlen(pszTest1), pszTest1, strlen(pszOut), pszOut);
#ifdef RT_OS_WINDOWS
        }
#endif
        RTStrFree(pszOut);
    }
    else
        RTTESTI_CHECK_MSG(rc == VWRN_NO_TRANSLATION || rc == VERR_NO_TRANSLATION, ("rc=%Rrc\n", rc));

    RTTestSub(hTest, "VERR_NO_TRANSLATION/RTUtf16ToLatin1");
    rc = RTUtf16ToLatin1(s_swzTest1, &pszOut);
    RTTESTI_CHECK_RC(rc, VERR_NO_TRANSLATION);
    if (RT_SUCCESS(rc))
        RTStrFree(pszOut);

    RTStrFree(pszTest1);
    RTTestSubDone(hTest);
}

static void testGetPut(RTTEST hTest)
{
    /*
     * Test RTStrPutCp, RTStrGetCp and RTStrGetCpEx.
     */
    RTTestSub(hTest, "RTStrPutCp, RTStrGetCp and RTStrGetCpEx");

    RTUNICP uc = 0;
    while (uc <= 0x10fffd)
    {
        /* Figure the range - skip illegal ranges. */
        RTUNICP ucFirst = uc;
        if (ucFirst - UINT32_C(0xd800) <= 0x7ff)
            ucFirst = 0xe000;
        else if (ucFirst == UINT32_C(0xfffe) || ucFirst == UINT32_C(0xffff))
            ucFirst = 0x10000;

        RTUNICP ucLast  = ucFirst + 1023;
        if (ucLast - UINT32_C(0xd800) <= 0x7ff)
            ucLast = 0xd7ff;
        else if (ucLast == UINT32_C(0xfffe) || ucLast == UINT32_C(0xffff))
            ucLast = 0xfffd;

        /* Encode the range into a string, decode each code point as we go along. */
        char sz1[8192];
        char *pszDst = sz1;
        for (uc = ucFirst; uc <= ucLast; uc++)
        {
            char *pszBefore = pszDst;
            pszDst = RTStrPutCp(pszDst, uc);
            RTTESTI_CHECK(pszBefore - pszDst < 6);

            RTUNICP uc2 = RTStrGetCp(pszBefore);
            RTTESTI_CHECK_MSG(uc2 == uc, ("uc2=%#x uc=%#x\n", uc2, uc));

            const char *pszSrc = pszBefore;
            RTUNICP uc3 = 42;
            RTTESTI_CHECK_RC(RTStrGetCpEx(&pszSrc, &uc3), VINF_SUCCESS);
            RTTESTI_CHECK_MSG(uc3 == uc, ("uc3=%#x uc=%#x\n", uc3, uc));
            RTTESTI_CHECK_MSG(pszSrc == pszDst, ("pszSrc=%p pszDst=%p\n", pszSrc, pszDst));
        }

        /* Decode and re-encode it. */
        const char *pszSrc = pszDst = sz1;
        for (uc = ucFirst; uc <= ucLast; uc++)
        {
            RTUNICP uc2 = RTStrGetCp(pszSrc);
            RTTESTI_CHECK_MSG(uc2 == uc, ("uc2=%#x uc=%#x\n", uc2, uc));

            RTUNICP uc3 = 42;
            RTTESTI_CHECK_RC(RTStrGetCpEx(&pszSrc, &uc3), VINF_SUCCESS);
            RTTESTI_CHECK_MSG(uc3 == uc, ("uc3=%#x uc=%#x\n", uc3, uc));

            pszDst = RTStrPutCp(pszDst, uc);
            RTTESTI_CHECK_MSG(pszSrc == pszDst, ("pszSrc=%p pszDst=%p\n", pszSrc, pszDst));
            pszSrc = pszDst;
        }

        /* Decode and wipe it (checking compiler optimizations). */
        pszSrc = pszDst = sz1;
        for (uc = ucFirst; uc <= ucLast; uc++)
        {
            RTUNICP uc2 = RTStrGetCp(pszSrc);
            RTTESTI_CHECK_MSG(uc2 == uc, ("uc2=%#x uc=%#x\n", uc2, uc));

            RTUNICP uc3 = 42;
            RTTESTI_CHECK_RC(RTStrGetCpEx(&pszSrc, &uc3), VINF_SUCCESS);
            RTTESTI_CHECK_MSG(uc3 == uc, ("uc3=%#x uc=%#x\n", uc3, uc));

            pszDst = RTStrPutCp(pszDst, 0);
        }

        /* advance */
        uc = ucLast + 1;
    }

}


int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstUtf8", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Run the tests.
     */
    InitStrings();
    test1(hTest);
    test2(hTest);
    test3(hTest);
    TstRTStrXCmp(hTest);
    TstRTStrPurgeEncoding(hTest);
    /* TstRT*PurgeComplementSet test conditions which assert. */
    TstRTStrPurgeComplementSet(hTest);
    TstRTUtf16PurgeComplementSet(hTest);
    testStrEnd(hTest);
    testStrStr(hTest);
    testUtf8Latin1(hTest);
    testUtf16Latin1(hTest);
    testNoTranslation(hTest);
    testGetPut(hTest);

    Benchmarks(hTest);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

