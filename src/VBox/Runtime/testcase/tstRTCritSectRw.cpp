/* $Id: tstRTCritSectRw.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Reader/Writer Critical Sections.
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
#include <iprt/critsect.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/lockvalidator.h>
#include <iprt/mp.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST               g_hTest;
static RTCRITSECTRW         g_CritSectRw;
static bool volatile        g_fTerminate;
static bool                 g_fYield;
static bool                 g_fQuiet;
static unsigned             g_uWritePercent;
static uint32_t volatile    g_cConcurrentWriters;
static uint32_t volatile    g_cConcurrentReaders;


static DECLCALLBACK(int) Test4Thread(RTTHREAD ThreadSelf, void *pvUser)
{
    /* Use randomization to get a little more variation of the sync pattern.
       We use a pseudo random generator here so that we don't end up testing
       the speed of the /dev/urandom implementation, but rather the read-write
       semaphores. */
    int rc;
    RTRAND hRand;
    RTTEST_CHECK_RC_OK_RET(g_hTest, rc = RTRandAdvCreateParkMiller(&hRand), rc);
    RTTEST_CHECK_RC_OK_RET(g_hTest, rc = RTRandAdvSeed(hRand, (uintptr_t)ThreadSelf), rc);
    unsigned c100 = RTRandAdvU32Ex(hRand, 0, 99);

    uint64_t *pcItr = (uint64_t *)pvUser;
    bool fWrite;
    for (;;)
    {
        unsigned readrec = RTRandAdvU32Ex(hRand, 0, 3);
        unsigned writerec = RTRandAdvU32Ex(hRand, 0, 3);
        /* Don't overdo recursion testing. */
        if (readrec > 1)
            readrec--;
        if (writerec > 1)
            writerec--;

        fWrite = (c100 < g_uWritePercent);
        if (fWrite)
        {
            for (unsigned i = 0; i <= writerec; i++)
            {
                rc = RTCritSectRwEnterExcl(&g_CritSectRw);
                if (RT_FAILURE(rc))
                {
                    RTTestFailed(g_hTest, "Write recursion %u on %s failed with rc=%Rrc", i, RTThreadSelfName(), rc);
                    break;
                }
            }
            if (RT_FAILURE(rc))
                break;
            if (ASMAtomicIncU32(&g_cConcurrentWriters) != 1)
            {
                RTTestFailed(g_hTest, "g_cConcurrentWriters=%u on %s after write locking it",
                             g_cConcurrentWriters, RTThreadSelfName());
                break;
            }
            if (g_cConcurrentReaders != 0)
            {
                RTTestFailed(g_hTest, "g_cConcurrentReaders=%u on %s after write locking it",
                             g_cConcurrentReaders, RTThreadSelfName());
                break;
            }
        }
        else
        {
            rc = RTCritSectRwEnterShared(&g_CritSectRw);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "Read locking on %s failed with rc=%Rrc", RTThreadSelfName(), rc);
                break;
            }
            ASMAtomicIncU32(&g_cConcurrentReaders);
            if (g_cConcurrentWriters != 0)
            {
                RTTestFailed(g_hTest, "g_cConcurrentWriters=%u on %s after read locking it",
                             g_cConcurrentWriters, RTThreadSelfName());
                break;
            }
        }
        for (unsigned i = 0; i < readrec; i++)
        {
            rc = RTCritSectRwEnterShared(&g_CritSectRw);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "Read recursion %u on %s failed with rc=%Rrc", i, RTThreadSelfName(), rc);
                break;
            }
        }
        if (RT_FAILURE(rc))
            break;

        /*
         * Check for fairness: The values of the threads should not differ too much
         */
        (*pcItr)++;

        /*
         * Check for correctness: Give other threads a chance. If the implementation is
         * correct, no other thread will be able to enter this lock now.
         */
        if (g_fYield)
            RTThreadYield();

        for (unsigned i = 0; i < readrec; i++)
        {
            rc = RTCritSectRwLeaveShared(&g_CritSectRw);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "Read release %u on %s failed with rc=%Rrc", i, RTThreadSelfName(), rc);
                break;
            }
        }
        if (RT_FAILURE(rc))
            break;

        if (fWrite)
        {
            if (ASMAtomicDecU32(&g_cConcurrentWriters) != 0)
            {
                RTTestFailed(g_hTest, "g_cConcurrentWriters=%u on %s before write release",
                             g_cConcurrentWriters, RTThreadSelfName());
                break;
            }
            if (g_cConcurrentReaders != 0)
            {
                RTTestFailed(g_hTest, "g_cConcurrentReaders=%u on %s before write release",
                             g_cConcurrentReaders, RTThreadSelfName());
                break;
            }
            for (unsigned i = 0; i <= writerec; i++)
            {
                rc = RTCritSectRwLeaveExcl(&g_CritSectRw);
                if (RT_FAILURE(rc))
                {
                    RTTestFailed(g_hTest, "Write release %u on %s failed with rc=%Rrc", i, RTThreadSelfName(), rc);
                    break;
                }
            }
        }
        else
        {
            if (g_cConcurrentWriters != 0)
            {
                RTTestFailed(g_hTest, "g_cConcurrentWriters=%u on %s before read release",
                             g_cConcurrentWriters, RTThreadSelfName());
                break;
            }
            ASMAtomicDecU32(&g_cConcurrentReaders);
            rc = RTCritSectRwLeaveShared(&g_CritSectRw);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "Read release on %s failed with rc=%Rrc", RTThreadSelfName(), rc);
                break;
            }
        }

        if (g_fTerminate)
            break;

        c100++;
        c100 %= 100;
    }
    if (!g_fQuiet)
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Thread %s exited with %lld\n", RTThreadSelfName(), *pcItr);
    RTRandAdvDestroy(hRand);
    return VINF_SUCCESS;
}


static void Test4(unsigned cThreads, unsigned cSeconds, unsigned uWritePercent, bool fYield, bool fQuiet)
{
    unsigned i;
    uint64_t acIterations[32];
    RTTHREAD aThreads[RT_ELEMENTS(acIterations)];
    AssertRelease(cThreads <= RT_ELEMENTS(acIterations));

    RTTestSubF(g_hTest, "Test4 - %u threads, %u sec, %u%% writes, %syielding",
               cThreads, cSeconds, uWritePercent, fYield ? "" : "non-");

    /*
     * Init globals.
     */
    g_fYield = fYield;
    g_fQuiet = fQuiet;
    g_fTerminate = false;
    g_uWritePercent = uWritePercent;
    g_cConcurrentWriters = 0;
    g_cConcurrentReaders = 0;

    RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectRwInit(&g_CritSectRw), VINF_SUCCESS);

    /*
     * Create the threads and let them block on the semrw.
     */
    RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectRwEnterExcl(&g_CritSectRw), VINF_SUCCESS);

    for (i = 0; i < cThreads; i++)
    {
        acIterations[i] = 0;
        RTTEST_CHECK_RC_RETV(g_hTest, RTThreadCreateF(&aThreads[i], Test4Thread, &acIterations[i], 0,
                                                      RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE,
                                                      "test-%u", i), VINF_SUCCESS);
    }

    /*
     * Do the test run.
     */
    uint32_t cErrorsBefore = RTTestErrorCount(g_hTest);
    uint64_t u64StartTS = RTTimeNanoTS();
    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveExcl(&g_CritSectRw), VINF_SUCCESS);
    RTThreadSleep(cSeconds * 1000);
    ASMAtomicWriteBool(&g_fTerminate, true);
    uint64_t ElapsedNS = RTTimeNanoTS() - u64StartTS;

    /*
     * Clean up the threads and semaphore.
     */
    for (i = 0; i < cThreads; i++)
        RTTEST_CHECK_RC(g_hTest, RTThreadWait(aThreads[i], 5000, NULL), VINF_SUCCESS);

    RTTEST_CHECK_MSG(g_hTest, g_cConcurrentWriters == 0, (g_hTest, "g_cConcurrentWriters=%u at end of test\n", g_cConcurrentWriters));
    RTTEST_CHECK_MSG(g_hTest, g_cConcurrentReaders == 0, (g_hTest, "g_cConcurrentReaders=%u at end of test\n", g_cConcurrentReaders));

    RTTEST_CHECK_RC(g_hTest, RTCritSectRwDelete(&g_CritSectRw), VINF_SUCCESS);

    if (RTTestErrorCount(g_hTest) != cErrorsBefore)
        RTThreadSleep(100);

    /*
     * Collect and display the results.
     */
    uint64_t cItrTotal = acIterations[0];
    for (i = 1; i < cThreads; i++)
        cItrTotal += acIterations[i];

    uint64_t cItrNormal = cItrTotal / cThreads;
    uint64_t cItrMinOK = cItrNormal / 20; /* 5% */
    uint64_t cItrMaxDeviation = 0;
    for (i = 0; i < cThreads; i++)
    {
        uint64_t cItrDelta = RT_ABS((int64_t)(acIterations[i] - cItrNormal));
        if (acIterations[i] < cItrMinOK)
            RTTestFailed(g_hTest, "Thread %u did less than 5%% of the iterations - %llu (it) vs. %llu (5%%) - %llu%%\n",
                         i, acIterations[i], cItrMinOK, cItrDelta * 100 / cItrNormal);
        else if (cItrDelta > cItrNormal / 2)
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
                         "Warning! Thread %u deviates by more than 50%% - %llu (it) vs. %llu (avg) - %llu%%\n",
                         i, acIterations[i], cItrNormal, cItrDelta * 100 / cItrNormal);
        if (cItrDelta > cItrMaxDeviation)
            cItrMaxDeviation = cItrDelta;

    }

    //RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS,
    //             "Threads: %u  Total: %llu  Per Sec: %llu  Avg: %llu ns  Max dev: %llu%%\n",
    //             cThreads,
    //             cItrTotal,
    //             cItrTotal / cSeconds,
    //             ElapsedNS / cItrTotal,
    //             cItrMaxDeviation * 100 / cItrNormal
    //             );
    //
    RTTestValue(g_hTest, "Thruput", cItrTotal * UINT32_C(1000000000) / ElapsedNS, RTTESTUNIT_CALLS_PER_SEC);
    RTTestValue(g_hTest, "Max diviation", cItrMaxDeviation * 100 / cItrNormal, RTTESTUNIT_PCT);
}


static void TestNegative(void)
{
    RTTestSub(g_hTest, "Negative");
    bool fSavedAssertQuiet    = RTAssertSetQuiet(true);
    bool fSavedAssertMayPanic = RTAssertSetMayPanic(false);
    bool fSavedLckValEnabled  = RTLockValidatorSetEnabled(false);

    RTCRITSECTRW CritSectRw;
    RTTEST_CHECK_RC_RETV(g_hTest, RTCritSectRwInit(&CritSectRw), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VERR_NOT_OWNER);
    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VERR_NOT_OWNER);

    RTTEST_CHECK_RC(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VERR_NOT_OWNER);

    RTTEST_CHECK_RC(g_hTest, RTCritSectRwEnterShared(&CritSectRw), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VERR_WRONG_ORDER); /* cannot release the final write before the reads. */
    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS);
    RTTEST_CHECK_RC(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS);

    RTTEST_CHECK_RC(g_hTest, RTCritSectRwDelete(&CritSectRw), VINF_SUCCESS);

    RTLockValidatorSetEnabled(fSavedLckValEnabled);
    RTAssertSetMayPanic(fSavedAssertMayPanic);
    RTAssertSetQuiet(fSavedAssertQuiet);
}


static bool Test1(void)
{
    RTTestSub(g_hTest, "Basics");

    RTCRITSECTRW CritSectRw;
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwInit(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsInitialized(&CritSectRw), false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);

    for (int iRun = 0; iRun < 3; iRun++)
    {
        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 1, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 0, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 2, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 0, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterShared(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 2, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 1, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwEnterExcl(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 3, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 1, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        /*  midway  */

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 2, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 1, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 2, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 0, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 1, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 0, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == true, false);

        RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriteRecursion(&CritSectRw) == 0, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwGetWriterReadRecursion(&CritSectRw) == 0, false);
        RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsWriteOwner(&CritSectRw) == false, false);
    }

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterExcl(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwTryEnterShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveShared(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwLeaveExcl(&CritSectRw), VINF_SUCCESS, false);


    RTTEST_CHECK_RET(g_hTest, RTCritSectRwIsInitialized(&CritSectRw), false);
    RTTEST_CHECK_RC_RET(g_hTest, RTCritSectRwDelete(&CritSectRw), VINF_SUCCESS, false);
    RTTEST_CHECK_RET(g_hTest, !RTCritSectRwIsInitialized(&CritSectRw), false);

    return true;
}

int main(int argc, char **argv)
{
    RT_NOREF_PV(argv);
    int rc = RTTestInitAndCreate("tstRTCritSectRw", &g_hTest);
    if (rc)
        return 1;
    RTTestBanner(g_hTest);

    if (Test1())
    {
        RTCPUID cCores = RTMpGetOnlineCoreCount();
        if (argc == 1)
        {
            TestNegative();

            /*    threads, seconds, writePercent,  yield,  quiet */
            Test4(      1,       1,            0,   true,  false);
            Test4(      1,       1,            1,   true,  false);
            Test4(      1,       1,            5,   true,  false);
            Test4(      2,       1,            3,   true,  false);
            Test4(     10,       1,            5,   true,  false);
            Test4(     10,      10,           10,  false,  false);

            if (cCores > 1)
            {
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "benchmarking (%u CPU cores)...\n", cCores);
                for (unsigned cThreads = 1; cThreads < 32; cThreads++)
                Test4(cThreads,  2,            1,  false,   true);
            }
            else
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "skipping benchmarking (only %u core available)\n", cCores);

            /** @todo add a testcase where some stuff times out. */
        }
        else
        {
            if (cCores > 1)
            {
                /*    threads, seconds, writePercent,  yield,  quiet */
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "benchmarking (%u CPU cores)...\n", cCores);
                Test4(      1,       3,            1,  false,   true);
                Test4(      1,       3,            1,  false,   true);
                Test4(      1,       3,            1,  false,   true);
                Test4(      2,       3,            1,  false,   true);
                Test4(      2,       3,            1,  false,   true);
                Test4(      2,       3,            1,  false,   true);
                Test4(      3,       3,            1,  false,   true);
                Test4(      3,       3,            1,  false,   true);
                Test4(      3,       3,            1,  false,   true);
            }
            else
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "skipping benchmarking (only %u core available)\n", cCores);
        }
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

