/* $Id: tstRTMemCache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTMemCache.
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
#include <iprt/memcache.h>

#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TST3THREAD
{
    RTTHREAD            hThread;
    RTSEMEVENTMULTI     hEvt;
    uint64_t volatile   cIterations;
    uint32_t            cbObject;
    bool                fUseCache;
} TST3THREAD, *PTST3THREAD;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test handle */
static RTTEST               g_hTest;
/** Global mem cache handle for use in some of the testcases. */
static RTMEMCACHE           g_hMemCache;
/** Stop indicator for tst3 threads.  */
static bool volatile        g_fTst3Stop;


/**
 * Basic API checks.
 * We'll return if any of these fails.
 */
static void tst1(void)
{
    RTTestISub("Basics");

    /* Create one without constructor or destructor. */
    uint32_t const cObjects = PAGE_SIZE * 2 / 256;
    RTMEMCACHE hMemCache;
    RTTESTI_CHECK_RC_RETV(RTMemCacheCreate(&hMemCache, 256, 32 /*cbAlignment*/, cObjects, NULL, NULL, NULL, 0 /*fFlags*/), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(hMemCache != NIL_RTMEMCACHE);

    /* Allocate a bit and free it again. */
    void *pv = NULL;
    RTTESTI_CHECK_RC_RETV(RTMemCacheAllocEx(hMemCache, &pv), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(pv != NULL);
    RTTESTI_CHECK_RETV(RT_ALIGN_P(pv, 32) == pv);
    RTMemCacheFree(hMemCache, pv);

    RTTESTI_CHECK((pv = RTMemCacheAlloc(hMemCache)) != NULL);
    RTMemCacheFree(hMemCache, pv);

    /* Allocate everything and free it again, checking size constraints. */
    for (uint32_t iLoop = 0; iLoop < 20; iLoop++)
    {
        /* Allocate everything. */
        void *apv[cObjects];
        for (uint32_t i = 0; i < cObjects; i++)
        {
            apv[i] = NULL;
            RTTESTI_CHECK_RC(RTMemCacheAllocEx(hMemCache, &apv[i]), VINF_SUCCESS);
        }

        /* Check that we've got it all. */
        int rc;
        RTTESTI_CHECK_RC(rc = RTMemCacheAllocEx(hMemCache, &pv), VERR_MEM_CACHE_MAX_SIZE);
        if (RT_SUCCESS(rc))
            RTMemCacheFree(hMemCache, pv);

        RTTESTI_CHECK((pv = RTMemCacheAlloc(hMemCache)) == NULL);
        RTMemCacheFree(hMemCache, pv);

        /* Free all the allocations. */
        for (uint32_t i = 0; i < cObjects; i++)
        {
            RTMemCacheFree(hMemCache, apv[i]);

            RTTESTI_CHECK((pv = RTMemCacheAlloc(hMemCache)) != NULL);
            RTMemCacheFree(hMemCache, pv);
        }
    }

    /* Destroy it. */
    RTTESTI_CHECK_RC(RTMemCacheDestroy(hMemCache), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTMemCacheDestroy(NIL_RTMEMCACHE), VINF_SUCCESS);
}



/** Constructor for tst2. */
static DECLCALLBACK(int) tst2Ctor(RTMEMCACHE hMemCache, void *pvObj, void *pvUser)
{
    RTTESTI_CHECK(hMemCache == g_hMemCache);
    RTTESTI_CHECK(ASMMemIsZero(pvObj, 256));

    if (*(bool *)pvUser)
        return VERR_RESOURCE_BUSY;

    strcat((char *)pvObj, "ctor was called\n");
    return VINF_SUCCESS;
}


/** Destructor for tst2.  Checks that it was constructed and used twice. */
static DECLCALLBACK(void) tst2Dtor(RTMEMCACHE hMemCache, void *pvObj, void *pvUser)
{
    RT_NOREF_PV(hMemCache); RT_NOREF_PV(pvUser);

    RTTESTI_CHECK(!strcmp((char *)pvObj, "ctor was called\nused\nused\n"));
    strcat((char *)pvObj, "dtor was called\n");
}

/**
 * Test constructor / destructor.
 */
static void tst2(void)
{
    RTTestISub("Ctor/Dtor");

    /* Create one without constructor or destructor. */
    bool            fFail    = false;
    uint32_t const  cObjects = PAGE_SIZE * 2 / 256;
    RTTESTI_CHECK_RC_RETV(RTMemCacheCreate(&g_hMemCache, 256, 32 /*cbAlignemnt*/, cObjects, tst2Ctor, tst2Dtor, &fFail, 0 /*fFlags*/), VINF_SUCCESS);

    /* A failure run first. */
    fFail = true;
    void *pv = (void *)0x42;
    RTTESTI_CHECK_RC_RETV(RTMemCacheAllocEx(g_hMemCache, &pv), VERR_RESOURCE_BUSY);
    RTTESTI_CHECK(pv == (void *)0x42);
    fFail = false;

    /* To two rounds where we allocate all the objects and free them again. */
    for (uint32_t iLoop = 0; iLoop < 2; iLoop++)
    {
        void *apv[cObjects];
        for (uint32_t i = 0; i < cObjects; i++)
        {
            apv[i] = NULL;
            RTTESTI_CHECK_RC_RETV(RTMemCacheAllocEx(g_hMemCache, &apv[i]), VINF_SUCCESS);
            if (iLoop == 0)
                RTTESTI_CHECK(!strcmp((char *)apv[i], "ctor was called\n"));
            else
                RTTESTI_CHECK(!strcmp((char *)apv[i], "ctor was called\nused\n"));
            strcat((char *)apv[i], "used\n");
        }

        RTTESTI_CHECK_RETV((pv = RTMemCacheAlloc(g_hMemCache)) == NULL);
        RTMemCacheFree(g_hMemCache, pv);

        for (uint32_t i = 0; i < cObjects; i++)
            RTMemCacheFree(g_hMemCache, apv[i]);
    }

    /* Cone, destroy the cache. */
    RTTESTI_CHECK_RC(RTMemCacheDestroy(g_hMemCache), VINF_SUCCESS);
}


/**
 * Thread that allocates
 * @returns
 * @param   hThreadSelf         The thread.
 * @param   pvArg               Pointer to fUseCache.
 */
static DECLCALLBACK(int) tst3Thread(RTTHREAD hThreadSelf, void *pvArg)
{
    PTST3THREAD     pThread     = (PTST3THREAD)(pvArg);
    size_t          cbObject    = pThread->cbObject;
    uint64_t        cIterations = 0;
    RT_NOREF_PV(hThreadSelf);

    /* wait for the kick-off */
    RTTEST_CHECK_RC_OK(g_hTest, RTSemEventMultiWait(pThread->hEvt, RT_INDEFINITE_WAIT));

    /* allocate and free loop */
    if (pThread->fUseCache)
    {
        while (!g_fTst3Stop)
        {
            void *apv[64];
            for (unsigned i = 0; i < RT_ELEMENTS(apv); i++)
            {
                apv[i] = RTMemCacheAlloc(g_hMemCache);
                RTTEST_CHECK(g_hTest, apv[i] != NULL);
            }
            for (unsigned i = 0; i < RT_ELEMENTS(apv); i++)
                RTMemCacheFree(g_hMemCache, apv[i]);

            cIterations += RT_ELEMENTS(apv);
        }
    }
    else
    {
        while (!g_fTst3Stop)
        {
            void *apv[64];

            for (unsigned i = 0; i < RT_ELEMENTS(apv); i++)
            {
                apv[i] = RTMemAlloc(cbObject);
                RTTEST_CHECK(g_hTest, apv[i] != NULL);
            }

            for (unsigned i = 0; i < RT_ELEMENTS(apv); i++)
                RTMemFree(apv[i]);

            cIterations += RT_ELEMENTS(apv);
        }
    }

    /* report back the status */
    pThread->cIterations = cIterations;
    return VINF_SUCCESS;
}

/**
 * Time constrained test with and unlimited  N threads.
 */
static void tst3(uint32_t cThreads, uint32_t cbObject, int iMethod, uint32_t cSecs)
{
    RTTestISubF("Benchmark - %u threads, %u bytes, %u secs, %s", cThreads, cbObject, cSecs,
                iMethod == 0 ? "RTMemCache"
                : "RTMemAlloc");

    /*
     * Create a cache with unlimited space, a start semaphore and line up
     * the threads.
     */
    RTTESTI_CHECK_RC_RETV(RTMemCacheCreate(&g_hMemCache, cbObject, 0 /*cbAlignment*/, UINT32_MAX, NULL, NULL, NULL, 0 /*fFlags*/), VINF_SUCCESS);

    RTSEMEVENTMULTI hEvt;
    RTTESTI_CHECK_RC_OK_RETV(RTSemEventMultiCreate(&hEvt));

    TST3THREAD aThreads[64];
    RTTESTI_CHECK_RETV(cThreads < RT_ELEMENTS(aThreads));

    ASMAtomicWriteBool(&g_fTst3Stop, false);
    for (uint32_t i = 0; i < cThreads; i++)
    {
        aThreads[i].hThread     = NIL_RTTHREAD;
        aThreads[i].cIterations = 0;
        aThreads[i].fUseCache   = iMethod == 0;
        aThreads[i].cbObject    = cbObject;
        aThreads[i].hEvt        = hEvt;
        RTTESTI_CHECK_RC_OK_RETV(RTThreadCreateF(&aThreads[i].hThread, tst3Thread, &aThreads[i], 0,
                                                 RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "tst3-%u", i));
    }

    /*
     * Start the race.
     */
    RTTimeNanoTS(); /* warmup */

    uint64_t uStartTS = RTTimeNanoTS();
    RTTESTI_CHECK_RC_OK_RETV(RTSemEventMultiSignal(hEvt));
    RTThreadSleep(cSecs * 1000);
    ASMAtomicWriteBool(&g_fTst3Stop, true);
    for (uint32_t i = 0; i < cThreads; i++)
        RTTESTI_CHECK_RC_OK_RETV(RTThreadWait(aThreads[i].hThread, 60*1000, NULL));
    uint64_t cElapsedNS = RTTimeNanoTS() - uStartTS;

    /*
     * Sum up the counts.
     */
    uint64_t cIterations = 0;
    for (uint32_t i = 0; i < cThreads; i++)
        cIterations += aThreads[i].cIterations;

    RTTestIPrintf(RTTESTLVL_ALWAYS, "%'8u iterations per second, %'llu ns on avg\n",
                  (unsigned)((long double)cIterations * 1000000000.0 / (long double)cElapsedNS),
                  cElapsedNS / cIterations);

    /* clean up */
    RTTESTI_CHECK_RC(RTMemCacheDestroy(g_hMemCache), VINF_SUCCESS);
    RTTESTI_CHECK_RC_OK(RTSemEventMultiDestroy(hEvt));
}

static void tst3AllMethods(uint32_t cThreads, uint32_t cbObject, uint32_t cSecs)
{
    tst3(cThreads, cbObject, 0, cSecs);
    tst3(cThreads, cbObject, 1, cSecs);
}


int main(int argc, char **argv)
{
    RT_NOREF_PV(argc); RT_NOREF_PV(argv);

    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTMemCache", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    g_hTest = hTest;

    tst1();
    tst2();
    if (RTTestIErrorCount() == 0)
    {
        uint32_t cSecs = argc == 1 ? 5 : 2;
        /*            threads, cbObj, cSecs */
        tst3AllMethods(     1,   256, cSecs);
        tst3AllMethods(     1,    32, cSecs);
        tst3AllMethods(     1,     8, cSecs);
        tst3AllMethods(     1,     2, cSecs);
        tst3AllMethods(     1,     1, cSecs);

        tst3AllMethods(     3,   256, cSecs);
        tst3AllMethods(     3,   128, cSecs);
        tst3AllMethods(     3,    64, cSecs);
        tst3AllMethods(     3,    32, cSecs);
        tst3AllMethods(     3,     2, cSecs);
        tst3AllMethods(     3,     1, cSecs);

        tst3AllMethods(    16,    32, cSecs);
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

