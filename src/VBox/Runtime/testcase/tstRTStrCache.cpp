/* $Id: tstRTStrCache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - StrCache.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/strcache.h>

#include <iprt/asm-mem.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>


static void tstShowStats(RTSTRCACHE hStrCache)
{
    size_t   cbStrings;
    size_t   cbChunks;
    size_t   cbBigEntries;
    uint32_t cHashCollisions;
    uint32_t cHashCollisions2;
    uint32_t cHashInserts;
    uint32_t cRehashes;
    uint32_t cStrings = RTStrCacheGetStats(hStrCache, &cbStrings, &cbChunks, &cbBigEntries,
                                           &cHashCollisions, &cHashCollisions2, &cHashInserts, &cRehashes);
    if (cbStrings == UINT32_MAX)
    {
        RTTESTI_CHECK(!RTStrCacheIsRealImpl());
        return;
    }

    RTTestIValue("Strings", cStrings, RTTESTUNIT_OCCURRENCES);
    RTTestIValue("Memory overhead", (uint64_t)(cbChunks + cbBigEntries - cbStrings) * 100 / cbStrings, RTTESTUNIT_PCT);
    if (cHashInserts > 0)
    {
        RTTestIValue("Collisions", (uint64_t)cHashCollisions * 100 / cHashInserts, RTTESTUNIT_PCT);
        RTTestIValue("Collisions2", (uint64_t)cHashCollisions2 * 100 / cHashInserts, RTTESTUNIT_PCT);
    }
    RTTestIPrintf(RTTESTLVL_ALWAYS, "cHashInserts=%u cHashCollisions=%u cHashCollisions2=%u cRehashes=%u\n",
                  cHashInserts, cHashCollisions, cHashCollisions2, cRehashes);
    RTTestIPrintf(RTTESTLVL_ALWAYS, "cbChunks=%zu cbBigEntries=%zu cbStrings=%zu\n", cbChunks, cbBigEntries, cbStrings);
}


/**
 * Check hash and memory performance.
 */
static void tst2(void)
{
    RTTestISub("Hash performance");

    /*
     * Generate test strings using a specific pseudo random generator.
     */
    size_t cbStrings = 0;
    char  *apszTests[8192];
    RTRAND hRand;
    RTTESTI_CHECK_RC_RETV(RTRandAdvCreateParkMiller(&hRand), VINF_SUCCESS);
    for (uint32_t i = 0; i < 8192; i++)
    {
        char szBuf[8192];
        uint32_t cch = RTRandAdvU32Ex(hRand, 3, sizeof(szBuf) - 1);
        RTRandAdvBytes(hRand, szBuf, cch);
        szBuf[cch] = '\0';
        for (uint32_t off = 0; off < cch; off++)
        {
            uint8_t b = szBuf[off];
            b &= 0x7f;
            if (!b || b == 0x7f)
                b = ' ';
            else if (RTLocCIsCntrl(b) && b != '\n' && b != '\r' && b != '\t')
                b += 0x30;
            szBuf[off] = b;
        }
        apszTests[i] = (char *)RTMemDup(szBuf, cch + 1);
        RTTESTI_CHECK_RETV(apszTests[i] != NULL);
        cbStrings += cch + 1;
    }
    RTRandAdvDestroy(hRand);
    RTTestIValue("Average string", cbStrings / RT_ELEMENTS(apszTests), RTTESTUNIT_BYTES);

    /*
     * Test new insertion first time around.
     */
    RTSTRCACHE hStrCache;
    RTTESTI_CHECK_RC_RETV(RTStrCacheCreate(&hStrCache, "hash performance"), VINF_SUCCESS);

    uint64_t nsTsStart = RTTimeNanoTS();
    for (uint32_t i = 0; i < RT_ELEMENTS(apszTests); i++)
        RTTESTI_CHECK_RETV(RTStrCacheEnter(hStrCache, apszTests[i]) != NULL);
    uint64_t cNsElapsed = RTTimeNanoTS() - nsTsStart;
    RTTestIValue("First insert", cNsElapsed / RT_ELEMENTS(apszTests), RTTESTUNIT_NS_PER_CALL);

    /*
     * Insert existing strings.
     */
    nsTsStart = RTTimeNanoTS();
    for (uint32_t i = 0; i < 8192; i++)
        RTTESTI_CHECK(RTStrCacheEnter(hStrCache, apszTests[i]) != NULL);
    cNsElapsed = RTTimeNanoTS() - nsTsStart;
    RTTestIValue("Duplicate insert", cNsElapsed / RT_ELEMENTS(apszTests), RTTESTUNIT_NS_PER_CALL);

    tstShowStats(hStrCache);
    RTTESTI_CHECK_RC(RTStrCacheDestroy(hStrCache), VINF_SUCCESS);

    for (uint32_t i = 0; i < 8192; i++)
        RTMemFree(apszTests[i]);
}


/**
 * Basic API checks.
 * We'll return if any of these fails.
 */
static void tst1(RTSTRCACHE hStrCache)
{
    const char *psz;

    /* Simple string entering and length. */
    RTTESTI_CHECK_RETV(psz = RTStrCacheEnter(hStrCache, "abcdefgh"));
    RTTESTI_CHECK_RETV(strcmp(psz, "abcdefgh") == 0);
    RTTESTI_CHECK_RETV(RTStrCacheLength(psz) == strlen("abcdefgh"));
    RTTESTI_CHECK_RETV(RTStrCacheRelease(hStrCache, psz) == 0);

    RTTESTI_CHECK_RETV(psz = RTStrCacheEnter(hStrCache, "abcdefghijklmnopqrstuvwxyz"));
    RTTESTI_CHECK_RETV(strcmp(psz, "abcdefghijklmnopqrstuvwxyz") == 0);
    RTTESTI_CHECK_RETV(RTStrCacheLength(psz) == strlen("abcdefghijklmnopqrstuvwxyz"));
    RTTESTI_CHECK_RETV(RTStrCacheRelease(hStrCache, psz) == 0);

    /* Unterminated strings. */
    RTTESTI_CHECK_RETV(psz = RTStrCacheEnterN(hStrCache, "0123456789", 3));
    RTTESTI_CHECK_RETV(strcmp(psz, "012") == 0);
    RTTESTI_CHECK_RETV(RTStrCacheLength(psz) == strlen("012"));
    RTTESTI_CHECK_RETV(RTStrCacheRelease(hStrCache, psz) == 0);

    RTTESTI_CHECK_RETV(psz = RTStrCacheEnterN(hStrCache, "0123456789abcdefghijklmnopqrstuvwxyz", 16));
    RTTESTI_CHECK_RETV(strcmp(psz, "0123456789abcdef") == 0);
    RTTESTI_CHECK_RETV(RTStrCacheLength(psz) == strlen("0123456789abcdef"));
    RTTESTI_CHECK_RETV(RTStrCacheRelease(hStrCache, psz) == 0);

    /* String referencing. */
    char szTest[4096+16];
    memset(szTest, 'a', sizeof(szTest));
    char szTest2[4096+16];
    memset(szTest2, 'f', sizeof(szTest));
    for (int32_t i = 4096; i > 3; i /= 3)
    {
        void *pv2;
        RTTESTI_CHECK_RETV(psz = RTStrCacheEnterN(hStrCache, szTest, i));
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(psz, i, 'a')) == NULL && !psz[i], ("i=%#x psz=%p off=%#x\n", i, psz, (uintptr_t)pv2 - (uintptr_t)psz));
        RTTESTI_CHECK(RTStrCacheRetain(psz) == 2);
        RTTESTI_CHECK(RTStrCacheRetain(psz) == 3);
        RTTESTI_CHECK(RTStrCacheRetain(psz) == 4);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(psz, i, 'a')) == NULL && !psz[i], ("i=%#x psz=%p off=%#x\n", i, psz, (uintptr_t)pv2 - (uintptr_t)psz));
        RTTESTI_CHECK(RTStrCacheRelease(hStrCache, psz) == 3);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(psz, i, 'a')) == NULL && !psz[i], ("i=%#x psz=%p off=%#x\n", i, psz, (uintptr_t)pv2 - (uintptr_t)psz));
        RTTESTI_CHECK(RTStrCacheRetain(psz) == 4);
        RTTESTI_CHECK(RTStrCacheRetain(psz) == 5);
        RTTESTI_CHECK(RTStrCacheRetain(psz) == 6);
        RTTESTI_CHECK(RTStrCacheRelease(hStrCache, psz) == 5);
        RTTESTI_CHECK(RTStrCacheRelease(hStrCache, psz) == 4);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(psz, i, 'a')) == NULL && !psz[i], ("i=%#x psz=%p off=%#x\n", i, psz, (uintptr_t)pv2 - (uintptr_t)psz));

        for (uint32_t cRefs = 3;; cRefs--)
        {
            RTTESTI_CHECK(RTStrCacheRelease(hStrCache, psz) == cRefs);
            if (cRefs == 0)
                break;
            RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(psz, i, 'a')) == NULL && !psz[i], ("i=%#x psz=%p off=%#x cRefs=%d\n", i, psz, (uintptr_t)pv2 - (uintptr_t)psz, cRefs));
            for (uint32_t j = 0; j < 42; j++)
            {
                const char *psz2;
                RTTESTI_CHECK_RETV(psz2 = RTStrCacheEnterN(hStrCache, szTest2, i));
                RTTESTI_CHECK_RETV(psz2 != psz);
                RTTESTI_CHECK(RTStrCacheRelease(hStrCache, psz2) == 0);
                RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(psz, i, 'a')) == NULL && !psz[i], ("i=%#x psz=%p off=%#x cRefs=%d\n", i, psz, (uintptr_t)pv2 - (uintptr_t)psz, cRefs));
            }
        }
    }

    /* Lots of allocations. */
    memset(szTest, 'b', sizeof(szTest));
    memset(szTest2, 'e', sizeof(szTest));
    const char *pszTest1Rets[4096 + 16];
    const char *pszTest2Rets[4096 + 16];
    for (uint32_t i = 1; i < RT_ELEMENTS(pszTest1Rets); i++)
    {
        RTTESTI_CHECK(pszTest1Rets[i] = RTStrCacheEnterN(hStrCache, szTest, i));
        RTTESTI_CHECK(strlen(pszTest1Rets[i]) == i);
        RTTESTI_CHECK(pszTest2Rets[i] = RTStrCacheEnterN(hStrCache, szTest2, i));
        RTTESTI_CHECK(strlen(pszTest2Rets[i]) == i);
    }

    if (RTStrCacheIsRealImpl())
    {
        for (uint32_t i = 1; i < RT_ELEMENTS(pszTest1Rets); i++)
        {
            uint32_t cRefs;
            const char *psz1, *psz2;
            RTTESTI_CHECK((psz1 = RTStrCacheEnterN(hStrCache, szTest,  i)) == pszTest1Rets[i]);
            RTTESTI_CHECK((psz2 = RTStrCacheEnterN(hStrCache, szTest2, i)) == pszTest2Rets[i]);
            RTTESTI_CHECK_MSG((cRefs = RTStrCacheRelease(hStrCache, psz1)) == 1, ("cRefs=%#x i=%#x\n", cRefs, i));
            RTTESTI_CHECK_MSG((cRefs = RTStrCacheRelease(hStrCache, psz2)) == 1, ("cRefs=%#x i=%#x\n", cRefs, i));
        }
    }

    for (uint32_t i = 1; i < RT_ELEMENTS(pszTest1Rets); i++)
    {
        uint32_t cRefs;
        RTTESTI_CHECK(strlen(pszTest1Rets[i]) == i);
        RTTESTI_CHECK_MSG((cRefs = RTStrCacheRelease(hStrCache, pszTest1Rets[i])) == 0, ("cRefs=%#x i=%#x\n", cRefs, i));
        RTTESTI_CHECK(strlen(pszTest2Rets[i]) == i);
        RTTESTI_CHECK_MSG((cRefs = RTStrCacheRelease(hStrCache, pszTest2Rets[i])) == 0, ("cRefs=%#x i=%#x\n", cRefs, i));
    }
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTStrCache", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Smoke tests using first the default and then a custom pool.
     */
    RTTestSub(hTest, "Smoke test on default cache");
    tst1(RTSTRCACHE_DEFAULT);

    RTTestSub(hTest, "Smoke test on custom cache");
    RTSTRCACHE hStrCache;
    RTTESTI_CHECK_RC(rc = RTStrCacheCreate(&hStrCache, "test 2a"), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTESTI_CHECK_RC(rc = RTStrCacheDestroy(hStrCache), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rc = RTStrCacheDestroy(NIL_RTSTRCACHE), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rc = RTStrCacheDestroy(RTSTRCACHE_DEFAULT), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rc = RTStrCacheDestroy(RTSTRCACHE_DEFAULT), VINF_SUCCESS);

    RTTESTI_CHECK_RC(rc = RTStrCacheCreate(&hStrCache, "test 2b"), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        tst1(hStrCache);
        RTTESTI_CHECK_RC(rc = RTStrCacheDestroy(hStrCache), VINF_SUCCESS);
    }

    /*
     * Cache performance on relatively real world examples.
     */
    tst2();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

