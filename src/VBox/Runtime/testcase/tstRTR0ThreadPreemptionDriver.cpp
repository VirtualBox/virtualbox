/* $Id: tstRTR0ThreadPreemptionDriver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Thread Preemption, driver program.
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
#include <iprt/initterm.h>

#include <iprt/asm.h>
#include <iprt/cpuset.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#ifdef VBOX
# include <VBox/sup.h>
# include "tstRTR0ThreadPreemption.h"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static bool volatile g_fTerminate = false;


/**
 * Try make sure all online CPUs will be engaged.
 */
static DECLCALLBACK(int) MyThreadProc(RTTHREAD hSelf, void *pvCpuIdx)
{
    RT_NOREF1(hSelf);
    RTCPUSET Affinity;
    RTCpuSetEmpty(&Affinity);
    RTCpuSetAddByIndex(&Affinity, (intptr_t)pvCpuIdx);
    RTThreadSetAffinity(&Affinity); /* ignore return code as it's not supported on all hosts. */

    while (!g_fTerminate)
    {
        uint64_t tsStart = RTTimeMilliTS();
        do
        {
            ASMNopPause();
        } while (RTTimeMilliTS() - tsStart < 8);
        RTThreadSleep(4);
    }

    return VINF_SUCCESS;
}


/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF3(argc, argv, envp);
#ifndef VBOX
    RTPrintf("tstSup: SKIPPED\n");
    return 0;
#else
    /*
     * Init.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTR0ThreadPreemption", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    PSUPDRVSESSION pSession;
    rc = SUPR3Init(&pSession);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "SUPR3Init failed with rc=%Rrc\n", rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    char szPath[RTPATH_MAX];
    rc = RTPathExecDir(szPath, sizeof(szPath));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szPath, sizeof(szPath), "tstRTR0ThreadPreemption.r0");
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "Failed constructing .r0 filename (rc=%Rrc)", rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    void *pvImageBase;
    rc = SUPR3LoadServiceModule(szPath, "tstRTR0ThreadPreemption",
                                "TSTRTR0ThreadPreemptionSrvReqHandler",
                                &pvImageBase);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "SUPR3LoadServiceModule(%s,,,) failed with rc=%Rrc\n", szPath, rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    /* test request */
    struct
    {
        SUPR0SERVICEREQHDR  Hdr;
        char                szMsg[256];
    } Req;

    /*
     * Sanity checks.
     */
    RTTestSub(hTest, "Sanity");
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                             TSTRTR0THREADPREEMPTION_SANITY_OK, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    RTTESTI_CHECK_MSG(Req.szMsg[0] == '\0', ("%s", Req.szMsg));
    if (Req.szMsg[0] != '\0')
        return RTTestSummaryAndDestroy(hTest);

    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                             TSTRTR0THREADPREEMPTION_SANITY_FAILURE, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    RTTESTI_CHECK_MSG(!strncmp(Req.szMsg, RT_STR_TUPLE("!42failure42")), ("%s", Req.szMsg));
    if (strncmp(Req.szMsg, RT_STR_TUPLE("!42failure42")))
        return RTTestSummaryAndDestroy(hTest);

    /*
     * Basic tests, bail out on failure.
     */
    RTTestSub(hTest, "Basics");
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                             TSTRTR0THREADPREEMPTION_BASIC, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    if (Req.szMsg[0] == '!')
    {
        RTTestIFailed("%s", &Req.szMsg[1]);
        return RTTestSummaryAndDestroy(hTest);
    }
    if (Req.szMsg[0])
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", Req.szMsg);

    /*
     * Is it trusty.
     */
    RTTestSub(hTest, "RTThreadPreemptIsPendingTrusty");
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                             TSTRTR0THREADPREEMPTION_IS_TRUSTY, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    if (Req.szMsg[0] == '!')
        RTTestIFailed("%s", &Req.szMsg[1]);
    else if (Req.szMsg[0])
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", Req.szMsg);

    /*
     * Stay in ring-0 until preemption is pending.
     */
    RTTHREAD ahThreads[RTCPUSET_MAX_CPUS];
    RTCPUSET OnlineSet;
    RTMpGetOnlineSet(&OnlineSet);
    for (uint32_t i = 0; i < RT_ELEMENTS(ahThreads); i++)
    {
        ahThreads[i] = NIL_RTTHREAD;
        if (RTCpuSetIsMemberByIndex(&OnlineSet, i))
            RTThreadCreateF(&ahThreads[i], MyThreadProc, (void *)(uintptr_t)i, 0, RTTHREADTYPE_DEFAULT,
                            RTTHREADFLAGS_WAITABLE, "cpu=%u", i);
    }


    RTTestSub(hTest, "Pending Preemption");
    RTThreadSleep(250); /** @todo fix GIP initialization? */
    for (int i = 0; ; i++)
    {
        Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
        Req.Hdr.cbReq = sizeof(Req);
        Req.szMsg[0] = '\0';
        RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                                 TSTRTR0THREADPREEMPTION_IS_PENDING, 0, &Req.Hdr), VINF_SUCCESS);
        if (    strcmp(Req.szMsg, "!cLoops=1\n")
            ||  i >= 64)
        {
            if (Req.szMsg[0] == '!')
                RTTestIFailed("%s", &Req.szMsg[1]);
            else if (Req.szMsg[0])
                RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", Req.szMsg);
            break;
        }
        if ((i % 3) == 0)
            RTThreadYield();
        else if ((i % 16) == 0)
            RTThreadSleep(8);
    }

    ASMAtomicWriteBool(&g_fTerminate, true);
    for (uint32_t i = 0; i < RT_ELEMENTS(ahThreads); i++)
        if (ahThreads[i] != NIL_RTTHREAD)
            RTThreadWait(ahThreads[i], 5000, NULL);

    /*
     * Test nested RTThreadPreemptDisable calls.
     */
    RTTestSub(hTest, "Nested");
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                             TSTRTR0THREADPREEMPTION_NESTED, 0, &Req.Hdr), VINF_SUCCESS);
    if (Req.szMsg[0] == '!')
        RTTestIFailed("%s", &Req.szMsg[1]);
    else if (Req.szMsg[0])
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", Req.szMsg);


    /*
     * Test thread-context hooks.
     */
    RTTestSub(hTest, "RTThreadCtxHooks");
    uint64_t u64StartTS = RTTimeMilliTS();
    uint64_t cMsMax     = 60000;        /* ca. 1 minute timeout. */
    uint64_t cMsElapsed;
    for (unsigned i = 0; i < 50; i++)
    {
        Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
        Req.Hdr.cbReq = sizeof(Req);
        Req.szMsg[0] = '\0';
        RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0ThreadPreemption", sizeof("tstRTR0ThreadPreemption") - 1,
                                                 TSTRTR0THREADPREEMPTION_CTXHOOKS, 0, &Req.Hdr), VINF_SUCCESS);
        if (RT_FAILURE(rc))
            return RTTestSummaryAndDestroy(hTest);
        if (Req.szMsg[0] == '!')
            RTTestIFailed("%s", &Req.szMsg[1]);
        else if (Req.szMsg[0])
            RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", Req.szMsg);
        if (!(i % 10))
            RTTestIPrintf(RTTESTLVL_ALWAYS, "RTThreadCtxHooks passed %u iteration(s)\n", i);

        /* Check timeout and bail. */
        cMsElapsed = RTTimeMilliTS() - u64StartTS;
        if (cMsElapsed > cMsMax)
        {
            RTTestIPrintf(RTTESTLVL_INFO, "RTThreadCtxHooks Stopping iterations. %RU64 ms. for %u iterations.\n",
                          cMsElapsed, i);
            break;
        }
    }

    /*
     * Done.
     */
    return RTTestSummaryAndDestroy(hTest);
#endif
}


#if !defined(VBOX_WITH_HARDENING) || !defined(RT_OS_WINDOWS)
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    return TrustedMain(argc, argv, envp);
}
#endif

