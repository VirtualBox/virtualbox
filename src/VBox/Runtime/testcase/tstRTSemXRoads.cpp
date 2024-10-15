/* $Id: tstRTSemXRoads.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTSemXRoads.
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
#include <iprt/semaphore.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;

static uint32_t volatile    g_cNSCrossings;
static uint32_t volatile    g_cEWCrossings;
static uint64_t             g_u64StartMilliTS;
static uint32_t             g_cSecs;
static RTSEMXROADS          g_hXRoads;


static int tstTrafficThreadCommon(uintptr_t iThread, bool fNS)
{
    RT_NOREF_PV(iThread);

    for (uint32_t iLoop = 0; RTTimeMilliTS() - g_u64StartMilliTS < g_cSecs*1000; iLoop++)
    {
        /* fudge */
        if ((iLoop % 223) == 222)
            RTThreadYield();
        else if ((iLoop % 16127) == 16126)
            RTThreadSleep(1);

        if (fNS)
        {
            RTTEST_CHECK_RC(g_hTest,RTSemXRoadsNSEnter(g_hXRoads), VINF_SUCCESS);
            ASMAtomicIncU32(&g_cNSCrossings);
            RTTEST_CHECK_RC(g_hTest,RTSemXRoadsNSLeave(g_hXRoads), VINF_SUCCESS);
        }
        else
        {
            RTTEST_CHECK_RC(g_hTest,RTSemXRoadsEWEnter(g_hXRoads), VINF_SUCCESS);
            ASMAtomicIncU32(&g_cEWCrossings);
            RTTEST_CHECK_RC(g_hTest,RTSemXRoadsEWLeave(g_hXRoads), VINF_SUCCESS);
        }
    }
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) tstTrafficNSThread(RTTHREAD hSelf, void *pvUser)
{
    RT_NOREF_PV(hSelf);

    uintptr_t iThread = (uintptr_t)pvUser;
    return tstTrafficThreadCommon(iThread, true);
}


static DECLCALLBACK(int) tstTrafficEWThread(RTTHREAD hSelf, void *pvUser)
{
    RT_NOREF_PV(hSelf);

    uintptr_t iThread = (uintptr_t)pvUser;
    return tstTrafficThreadCommon(iThread, false);
}


static void tstTraffic(unsigned cThreads, unsigned cSecs)
{
    RTTestSubF(g_hTest, "Traffic - %u threads per direction, %u sec", cThreads, cSecs);

    /*
     * Create X worker threads which drives in the south/north direction and Y
     * worker threads which drives in the west/east direction.  Let them drive
     * in a loop for 15 seconds with slight delays between some of the runs and
     * then check the numbers.
     */

    /* init */
    RTTHREAD ahThreadsX[8];
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreadsX); i++)
        ahThreadsX[i] = NIL_RTTHREAD;
    AssertRelease(RT_ELEMENTS(ahThreadsX) >= cThreads);

    RTTHREAD ahThreadsY[8];
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreadsY); i++)
        ahThreadsY[i] = NIL_RTTHREAD;
    AssertRelease(RT_ELEMENTS(ahThreadsY) >= cThreads);

    g_cNSCrossings      = 0;
    g_cEWCrossings      = 0;
    g_cSecs             = cSecs;
    g_u64StartMilliTS   = RTTimeMilliTS();

    /* create */
    RTTEST_CHECK_RC_RETV(g_hTest, RTSemXRoadsCreate(&g_hXRoads), VINF_SUCCESS);

    int rc = VINF_SUCCESS;
    for (unsigned i = 0; i < cThreads && RT_SUCCESS(rc); i++)
    {
        rc = RTThreadCreateF(&ahThreadsX[i], tstTrafficNSThread, (void *)(uintptr_t)i, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "NS-%u", i);
        RTTEST_CHECK_RC_OK(g_hTest, rc);
    }

    for (unsigned i = 0; i < cThreads && RT_SUCCESS(rc); i++)
    {
        rc = RTThreadCreateF(&ahThreadsX[i], tstTrafficEWThread, (void *)(uintptr_t)i, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "NS-%u", i);
        RTTEST_CHECK_RC_OK(g_hTest, rc);
    }

    /* wait */
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreadsX); i++)
        if (ahThreadsX[i] != NIL_RTTHREAD)
        {
            int rc2 = RTThreadWaitNoResume(ahThreadsX[i], (60 + cSecs) * 1000, NULL);
            RTTEST_CHECK_RC_OK(g_hTest, rc2);
        }

    for (unsigned i = 0; i < RT_ELEMENTS(ahThreadsY); i++)
        if (ahThreadsY[i] != NIL_RTTHREAD)
        {
            int rc2 = RTThreadWaitNoResume(ahThreadsY[i], (60 + cSecs) * 1000, NULL);
            RTTEST_CHECK_RC_OK(g_hTest, rc2);
        }

    RTTEST_CHECK_MSG_RETV(g_hTest, g_cEWCrossings > 10 && g_cNSCrossings,
                          (g_hTest, "cEWCrossings=%u g_cNSCrossings=%u\n", g_cEWCrossings, g_cNSCrossings));
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "cNSCrossings=%u\n", g_cNSCrossings);
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "cEWCrossings=%u\n", g_cEWCrossings);
}



static bool tstBasics(void)
{
    RTTestSub(g_hTest, "Basics");

    RTSEMXROADS hXRoads;
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsCreate(&hXRoads), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsNSEnter(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsNSLeave(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsEWEnter(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsEWLeave(hXRoads), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsEWEnter(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsEWLeave(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsNSEnter(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsNSLeave(hXRoads), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsNSEnter(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsNSLeave(hXRoads), VINF_SUCCESS, false);

    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsDestroy(hXRoads), VINF_SUCCESS, false);
    RTTEST_CHECK_RC_RET(g_hTest, RTSemXRoadsDestroy(NIL_RTSEMXROADS), VINF_SUCCESS, false);

    return true;
}


int main()
{
    int rc = RTTestInitAndCreate("tstRTSemXRoads", &g_hTest);
    if (rc)
        return rc;
    RTTestBanner(g_hTest);

    if (tstBasics())
    {
        tstTraffic(1, 5);
        tstTraffic(2, 5);
        tstTraffic(4, 15);
        tstTraffic(8, 10);
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

