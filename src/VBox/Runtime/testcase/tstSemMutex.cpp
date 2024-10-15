/* $Id: tstSemMutex.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Simple Mutex Semaphore Smoke Test.
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
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/stream.h>
#include <iprt/time.h>
#include <iprt/initterm.h>
#include <iprt/asm.h>
#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTSEMMUTEX           g_hMutex = NIL_RTSEMMUTEX;
static bool volatile        g_fTerminate;
static bool                 g_fYield;
static bool                 g_fQuiet;
static uint32_t volatile    g_cbConcurrent;
static uint32_t volatile    g_cErrors;


static int PrintError(const char *pszFormat, ...)
{
    ASMAtomicIncU32(&g_cErrors);

    RTPrintf("tstSemMutex: FAILURE - ");
    va_list va;
    va_start(va, pszFormat);
    RTPrintfV(pszFormat, va);
    va_end(va);

    return 1;
}


static DECLCALLBACK(int) ThreadTest1(RTTHREAD ThreadSelf, void *pvUser)
{
    uint64_t *pu64 = (uint64_t *)pvUser;
    for (;;)
    {
        int rc = RTSemMutexRequestNoResume(g_hMutex, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
        {
            PrintError("%x: RTSemMutexRequestNoResume failed with %Rrc\n", rc);
            break;
        }
        if (ASMAtomicIncU32(&g_cbConcurrent) != 1)
        {
            PrintError("g_cbConcurrent=%d after request!\n", g_cbConcurrent);
            break;
        }

        /*
         * Check for fairness: The values of the threads should not differ too much
         */
        (*pu64)++;

        /*
         * Check for correctness: Give other threads a chance. If the implementation is
         * correct, no other thread will be able to enter this lock now.
         */
        if (g_fYield)
            RTThreadYield();
        if (ASMAtomicDecU32(&g_cbConcurrent) != 0)
        {
            PrintError("g_cbConcurrent=%d before release!\n", g_cbConcurrent);
            break;
        }
        rc = RTSemMutexRelease(g_hMutex);
        if (RT_FAILURE(rc))
        {
            PrintError("%x: RTSemMutexRelease failed with %Rrc\n", rc);
            break;
        }
        if (g_fTerminate)
            break;
    }
    if (!g_fQuiet)
        RTPrintf("tstSemMutex: Thread %08x exited with %lld\n", ThreadSelf, *pu64);
    return VINF_SUCCESS;
}


static int Test1(unsigned cThreads, unsigned cSeconds, bool fYield, bool fQuiet)
{
    int rc;
    unsigned i;
    uint64_t g_au64[32];
    RTTHREAD aThreads[RT_ELEMENTS(g_au64)];
    AssertRelease(cThreads <= RT_ELEMENTS(g_au64));

    /*
     * Init globals.
     */
    g_fYield = fYield;
    g_fQuiet = fQuiet;
    g_fTerminate = false;

    rc = RTSemMutexCreate(&g_hMutex);
    if (RT_FAILURE(rc))
        return PrintError("RTSemMutexCreate failed (rc=%Rrc)\n", rc);

    /*
     * Create the threads and let them block on the mutex.
     */
    rc = RTSemMutexRequest(g_hMutex, RT_INDEFINITE_WAIT);
    if (RT_FAILURE(rc))
        return PrintError("RTSemMutexRequest failed (rc=%Rrc)\n", rc);

    for (i = 0; i < cThreads; i++)
    {
        g_au64[i] = 0;
        rc = RTThreadCreate(&aThreads[i], ThreadTest1, &g_au64[i], 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "test");
        if (RT_FAILURE(rc))
            return PrintError("RTThreadCreate failed for thread %u (rc=%Rrc)\n", i, rc);
    }

    if (!fQuiet)
        RTPrintf("tstSemMutex: %zu Threads created. Racing them for %u seconds (%s) ...\n",
                 cThreads, cSeconds, g_fYield ? "yielding" : "no yielding");

    uint64_t u64StartTS = RTTimeNanoTS();
    rc = RTSemMutexRelease(g_hMutex);
    if (RT_FAILURE(rc))
        PrintError("RTSemMutexRelease failed (rc=%Rrc)\n", rc);
    RTThreadSleep(cSeconds * 1000);
    ASMAtomicXchgBool(&g_fTerminate, true);
    uint64_t ElapsedNS = RTTimeNanoTS() - u64StartTS;

    for (i = 0; i < cThreads; i++)
    {
        rc = RTThreadWait(aThreads[i], 5000, NULL);
        if (RT_FAILURE(rc))
            PrintError("RTThreadWait failed for thread %u (rc=%Rrc)\n", i, rc);
    }

    rc = RTSemMutexDestroy(g_hMutex);
    if (RT_FAILURE(rc))
        PrintError("RTSemMutexDestroy failed - %Rrc\n", rc);
    g_hMutex = NIL_RTSEMMUTEX;
    if (g_cErrors)
        RTThreadSleep(100);

    /*
     * Collect and display the results.
     */
    uint64_t Total = g_au64[0];
    for (i = 1; i < cThreads; i++)
        Total += g_au64[i];

    uint64_t Normal = Total / cThreads;
    uint64_t MaxDeviation = 0;
    for (i = 0; i < cThreads; i++)
    {
        uint64_t Delta = RT_ABS((int64_t)(g_au64[i] - Normal));
        if (Delta > Normal / 2)
            RTPrintf("tstSemMutex: Warning! Thread %d deviates by more than 50%% - %llu (it) vs. %llu (avg)\n",
                     i, g_au64[i], Normal);
        if (Delta > MaxDeviation)
            MaxDeviation = Delta;

    }

    RTPrintf("tstSemMutex: Threads: %u  Total: %llu  Per Sec: %llu  Avg: %llu ns  Max dev: %llu%%\n",
             cThreads,
             Total,
             Total / cSeconds,
             ElapsedNS / Total,
             MaxDeviation * 100 / Normal
             );
    return 0;
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSemMutex: RTR3InitExe failed (rc=%Rrc)\n", rc);
        return 1;
    }
    RTPrintf("tstSemMutex: TESTING...\n");

    if (argc == 1)
    {
        /*    threads, seconds,  yield,  quiet */
        Test1(      1,       1,   true,  false);
        Test1(      2,       1,   true,  false);
        Test1(     10,       1,   true,  false);
        Test1(     10,      10,  false,  false);

        RTPrintf("tstSemMutex: benchmarking...\n");
        for (unsigned cThreads = 1; cThreads < 32; cThreads++)
            Test1(cThreads,  2,  false,   true);

        /** @todo add a testcase where some stuff times out. */
    }
    else
    {
        /*    threads, seconds,  yield,  quiet */
        RTPrintf("tstSemMutex: benchmarking...\n");
        Test1(      1,       3,  false,   true);
        Test1(      1,       3,  false,   true);
        Test1(      1,       3,  false,   true);
        Test1(      2,       3,  false,   true);
        Test1(      2,       3,  false,   true);
        Test1(      2,       3,  false,   true);
        Test1(      3,       3,  false,   true);
        Test1(      3,       3,  false,   true);
        Test1(      3,       3,  false,   true);
    }

    if (!g_cErrors)
        RTPrintf("tstSemMutex: SUCCESS\n");
    else
        RTPrintf("tstSemMutex: FAILURE - %u errors\n", g_cErrors);
    return g_cErrors != 0;
}

