/* $Id: tstRTMemPool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - MemPool.
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
#include <iprt/mempool.h>

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/rand.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test handle */
static RTTEST       g_hTest;
/** Memory pool for tst4. */
static RTMEMPOOL    g_hMemPool4;


/**
 * Basic API checks.
 * We'll return if any of these fails.
 */
static void tst1(RTMEMPOOL hMemPool)
{
    void *pv;

    /* Normal alloc. */
    RTTESTI_CHECK_RETV(pv = RTMemPoolAlloc(hMemPool, 1));
    RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);

    RTTESTI_CHECK_RETV(pv = RTMemPoolAlloc(hMemPool, 0));
    RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);

    /* Zeroed allocation. */
    for (uint32_t i = 0; i < 512; i++)
    {
        RTTESTI_CHECK_RETV(pv = RTMemPoolAllocZ(hMemPool, 1024));
        RTTESTI_CHECK(ASMMemFirstMismatchingU32(pv, 1024, 0) == NULL);
        memset(pv, 'a', 1024);
        RTTESTI_CHECK_RETV(RTMemPoolRefCount(pv) == 1);
        RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);
    }

    RTTESTI_CHECK_RETV(pv = RTMemPoolAllocZ(hMemPool, 0));
    RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);

    /* Duped allocation. */
    static const char szTest[] = "test string abcdef";
    RTTESTI_CHECK_RETV(pv = RTMemPoolDup(hMemPool, szTest, sizeof(szTest)));
    RTTESTI_CHECK(memcmp(pv, szTest, sizeof(szTest)) == 0);
    RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);

    for (uint32_t i = 0; i < 512; i++)
    {
        size_t const cb = 256 - sizeof(szTest);
        RTTESTI_CHECK_RETV(pv = RTMemPoolDupEx(hMemPool, szTest, sizeof(szTest), cb));
        RTTESTI_CHECK(memcmp(pv, szTest, sizeof(szTest)) == 0);
        RTTESTI_CHECK(ASMMemIsZero((uint8_t *)pv + sizeof(szTest), cb));
        memset(pv, 'b', sizeof(szTest) + cb);
        RTTESTI_CHECK_RETV(RTMemPoolRefCount(pv) == 1);
        RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);
    }

    /* Reallocation */
    RTTESTI_CHECK_RETV(pv = RTMemPoolRealloc(hMemPool, NULL, 1));
    RTTESTI_CHECK_RETV(pv = RTMemPoolRealloc(hMemPool, pv, 2));
    RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);

    RTTESTI_CHECK_RETV(pv = RTMemPoolAlloc(hMemPool, 42));
    RTTESTI_CHECK_RETV(pv = RTMemPoolRealloc(hMemPool, pv, 32));
    RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, pv) == 0);

    RTTESTI_CHECK_RETV(pv = RTMemPoolRealloc(hMemPool, NULL, 128));
    RTTESTI_CHECK_RETV(pv = RTMemPoolRealloc(hMemPool, pv, 256));
    RTTESTI_CHECK_RETV(RTMemPoolRealloc(hMemPool, pv, 0) == NULL);

    /* Free (a bit hard to test) */
    RTMemPoolFree(hMemPool, NULL);
    RTMemPoolFree(hMemPool, RTMemPoolAlloc(hMemPool, 42));

    /* Memory referencing. */
    for (uint32_t i = 1; i <= 4096; i *= 3)
    {
        void *pv2;
        RTTESTI_CHECK_RETV(pv = RTMemPoolAlloc(hMemPool, i));
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 1);
        memset(pv, 'a', i);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(pv, i, 'a')) == NULL, ("i=%#x pv=%p off=%#x\n", i, pv,(uintptr_t)pv2 - (uintptr_t)pv));
        RTTESTI_CHECK(RTMemPoolRetain(pv) == 2);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 2);
        RTTESTI_CHECK(RTMemPoolRetain(pv) == 3);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 3);
        RTTESTI_CHECK(RTMemPoolRetain(pv) == 4);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 4);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(pv, i, 'a')) == NULL, ("i=%#x pv=%p off=%#x\n", i, pv, (uintptr_t)pv2 - (uintptr_t)pv));
        RTTESTI_CHECK(RTMemPoolRelease(hMemPool, pv) == 3);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 3);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(pv, i, 'a')) == NULL, ("i=%#x pv=%p off=%#x\n", i, pv, (uintptr_t)pv2 - (uintptr_t)pv));
        RTTESTI_CHECK(RTMemPoolRetain(pv) == 4);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 4);
        RTTESTI_CHECK(RTMemPoolRetain(pv) == 5);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 5);
        RTTESTI_CHECK(RTMemPoolRetain(pv) == 6);
        RTTESTI_CHECK(RTMemPoolRefCount(pv) == 6);
        RTTESTI_CHECK(RTMemPoolRelease(NIL_RTMEMPOOL, pv) == 5);
        RTTESTI_CHECK(RTMemPoolRelease(NIL_RTMEMPOOL, pv) == 4);
        RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(pv, i, 'a')) == NULL, ("i=%#x pv=%p off=%#x\n", i, pv, (uintptr_t)pv2 - (uintptr_t)pv));

        for (uint32_t cRefs = 3;; cRefs--)
        {
            RTTESTI_CHECK(RTMemPoolRelease(hMemPool, pv) == cRefs);
            if (cRefs == 0)
                break;
            RTTESTI_CHECK(RTMemPoolRefCount(pv) == cRefs);
            RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(pv, i, 'a')) == NULL, ("i=%#x pv=%p off=%#x cRefs=%d\n", i, pv, (uintptr_t)pv2 - (uintptr_t)pv, cRefs));
            for (uint32_t j = 0; j < 42; j++)
            {
                RTTESTI_CHECK_RETV(pv2 = RTMemPoolAlloc(hMemPool, i));
                RTTESTI_CHECK_RETV(pv2 != pv);
                memset(pv2, 'f', i);
                RTTESTI_CHECK(RTMemPoolRelease(hMemPool, pv2) == 0);
                RTTESTI_CHECK_MSG_RETV((pv2 = ASMMemFirstMismatchingU8(pv, i, 'a')) == NULL, ("i=%#x pv=%p off=%#x cRefs=%d\n", i, pv, (uintptr_t)pv2 - (uintptr_t)pv, cRefs));
            }
        }
    }
}


/**
 * Test automatic cleanup upon destruction.
 */
static void tst3(void)
{
    RTTestISub("Destroy non-empty pool");

    /*
     * Nothing freed.
     */
    RTMEMPOOL hMemPool;
    RTTESTI_CHECK_RC_RETV(RTMemPoolCreate(&hMemPool, "test 3a"), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(RTMemPoolAlloc(hMemPool, 10));
    RTTESTI_CHECK_RETV(RTMemPoolAlloc(hMemPool, 20));
    RTTESTI_CHECK_RETV(RTMemPoolAlloc(hMemPool, 40));
    RTTESTI_CHECK_RETV(RTMemPoolAlloc(hMemPool, 80));
    RTTESTI_CHECK_RC_RETV(RTMemPoolDestroy(hMemPool), VINF_SUCCESS);

    /*
     * Pseudo random freeing to test list maintenance.
     */
    RTRAND hRand;
    RTTESTI_CHECK_RC_OK_RETV(RTRandAdvCreateParkMiller(&hRand));

    for (uint32_t i = 0; i < 10; i++)
    {
        RTTESTI_CHECK_RC_RETV(RTMemPoolCreate(&hMemPool, "test 3b"), VINF_SUCCESS);

        void *apvHistory[256];
        RT_ZERO(apvHistory);

        uint32_t cBlocks = 0;
        uint32_t j;
        for (j = 0; j < RT_ELEMENTS(apvHistory) - i * 7; j++)
        {
            RTTESTI_CHECK_RETV(apvHistory[j] = RTMemPoolAlloc(hMemPool, j));
            memset(apvHistory[j], 'a', j);
            cBlocks++;

            if (RTRandAdvU32Ex(hRand, 0, 4) == 4)
            {
                uint32_t iFree = RTRandAdvU32Ex(hRand, 0, j);
                cBlocks -= apvHistory[iFree] != NULL;
                RTTESTI_CHECK_RETV(RTMemPoolRelease(hMemPool, apvHistory[iFree]) == 0);
                apvHistory[iFree] = NULL;
            }
        }

        RTTESTI_CHECK_RC_RETV(RTMemPoolDestroy(hMemPool), VINF_SUCCESS);
        RTTestIPrintf(RTTESTLVL_INFO, "cBlocks=%u j=%u\n", cBlocks, j);
    }

    RTRandAdvDestroy(hRand);
}


/** Thread function for tst4. */
static DECLCALLBACK(int) tst4Thread(RTTHREAD hSelf, void *pvArg)
{
//    uint32_t    iThread  = (uint32_t)(uintptr_t)pvArg;
    RTMEMPOOL   hMemPool = g_hMemPool4;
    RT_NOREF_PV(pvArg);


    /* setup. */
    RTTestSetDefault(g_hTest, NULL);

    /* wait for the kick-off */
    RTThreadUserWait(hSelf, RT_INDEFINITE_WAIT);

    /* do the work. */
    for (uint32_t i = 0; i < 1024; i++)
    {
        void *apvHistory[256];
        RT_ZERO(apvHistory);
        uint32_t j;
        for (j = 0; j < RT_ELEMENTS(apvHistory) - (i % 200); j++)
            RTTESTI_CHECK_RET(apvHistory[j] = RTMemPoolAlloc(hMemPool, (i & 15) + (j & 63)), VERR_NO_MEMORY);
        for (uint32_t k = i & 7; k < j; k += 3)
        {
            RTTESTI_CHECK_RET(RTMemPoolRelease(hMemPool, apvHistory[k]) == 0, VERR_INTERNAL_ERROR);
            apvHistory[k] = NULL;
        }
        while (j-- > 0)
            RTTESTI_CHECK_RET(RTMemPoolRelease(hMemPool, apvHistory[j]) == 0, VERR_INTERNAL_ERROR);
    }

    return VINF_SUCCESS;
}

/** sub test */
static void tst4Sub(uint32_t cThreads)
{
    RTTestISubF("Serialization - %u threads", cThreads);
    RTMEMPOOL hMemPool;
    RTTESTI_CHECK_RC_RETV(RTMemPoolCreate(&hMemPool, "test 2a"), VINF_SUCCESS);
    g_hMemPool4 = hMemPool;

    PRTTHREAD pahThreads = (PRTTHREAD)RTMemPoolAlloc(hMemPool, cThreads * sizeof(RTTHREAD));
    RTTESTI_CHECK(pahThreads);
    if (pahThreads)
    {
        /* start them. */
        for (uint32_t i = 0; i < cThreads; i++)
        {
            int rc = RTThreadCreateF(&pahThreads[i], tst4Thread, (void *)(uintptr_t)i, 0,
                                     RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "tst4-%u/%u", i, cThreads);
            RTTESTI_CHECK_RC_OK(rc);
            if (RT_FAILURE(rc))
                pahThreads[i] = NIL_RTTHREAD;
        }
        RTThreadYield();

        /* kick them off. */
        for (uint32_t i = 0; i < cThreads; i++)
            if (pahThreads[i] != NIL_RTTHREAD)
                RTTESTI_CHECK_RC_OK(RTThreadUserSignal(pahThreads[i]));

        /* wait for them. */
        for (uint32_t i = 0; i < cThreads; i++)
            if (pahThreads[i] != NIL_RTTHREAD)
            {
                int rc = RTThreadWait(pahThreads[i], 2*60*1000, NULL);
                RTTESTI_CHECK_RC_OK(rc);
            }
    }

    RTTESTI_CHECK_RC(RTMemPoolDestroy(hMemPool), VINF_SUCCESS);
}


/**
 * Starts a bunch of threads beating on a pool to test serialization.
 */
static void tst4(void)
{
    /*
     * Test it with a few different thread counts.
     */
    tst4Sub(1);
    tst4Sub(2);
    tst4Sub(3);
    tst4Sub(4);
    tst4Sub(8);
    tst4Sub(16);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTMemPool", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    g_hTest = hTest;

    /*
     * Smoke tests using first the default and then a custom pool.
     */
    RTTestSub(hTest, "Smoke test on default pool");
    tst1(RTMEMPOOL_DEFAULT);

    RTTestSub(hTest, "Smoke test on custom pool");
    RTMEMPOOL hMemPool;
    RTTESTI_CHECK_RC(rc = RTMemPoolCreate(&hMemPool, "test 2a"), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTESTI_CHECK_RC(rc = RTMemPoolDestroy(hMemPool), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rc = RTMemPoolDestroy(NIL_RTMEMPOOL), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rc = RTMemPoolDestroy(RTMEMPOOL_DEFAULT), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rc = RTMemPoolDestroy(RTMEMPOOL_DEFAULT), VINF_SUCCESS);

    RTTESTI_CHECK_RC(rc = RTMemPoolCreate(&hMemPool, "test 2b"), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        tst1(hMemPool);
        RTTESTI_CHECK_RC(rc = RTMemPoolDestroy(hMemPool), VINF_SUCCESS);
    }

    /*
     * Further tests.
     */
    tst3();
    tst4();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

