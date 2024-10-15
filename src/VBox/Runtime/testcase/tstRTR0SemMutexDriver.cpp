/* $Id: tstRTR0SemMutexDriver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#ifdef VBOX
# include <VBox/sup.h>
# include "tstRTR0SemMutex.h"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TSTRTR0SEMMUTEXREQ
{
    SUPR0SERVICEREQHDR  Hdr;
    char                szMsg[256];
} TSTRTR0SEMMUTEXREQ;
typedef TSTRTR0SEMMUTEXREQ *PTSTRTR0SEMMUTEXREQ;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


/**
 * Thread function employed by tstDoThreadedTest.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf         The thread.
 * @param   pvUser              The operation to perform.
 */
static DECLCALLBACK(int) tstThreadFn(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF1(hThreadSelf);
    uint32_t            u32   = (uint32_t)(uintptr_t)pvUser;
    TSTRTR0SEMMUTEX     enmDo = (TSTRTR0SEMMUTEX)RT_LOWORD(u32);
    uint32_t            cSecs = RT_HIWORD(u32);
    TSTRTR0SEMMUTEXREQ Req;
    RT_ZERO(Req);
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTEST_CHECK_RC_RET(g_hTest, SUPR3CallR0Service("tstRTR0SemMutex", sizeof("tstRTR0SemMutex") - 1,
                                                    enmDo, cSecs, &Req.Hdr),
                        VINF_SUCCESS,
                        rcCheck);
    if (Req.szMsg[0] == '!')
    {
        RTTestFailed(g_hTest, "%s", &Req.szMsg[1]);
        return VERR_GENERAL_FAILURE;
    }
    if (Req.szMsg[0])
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "%s", Req.szMsg);
    return VINF_SUCCESS;
}

/**
 * Performs a threaded test.
 *
 * @returns true on success, false on failure.
 * @param   enmSetup            The setup operation number.
 * @param   enmDo               The do-it operation number.
 * @param   enmCleanup          The cleanup operation number.
 * @param   cThreads            The number of threads.
 * @param   pReq                The request structure.
 * @param   pszTest             The test name.
 */
static bool tstDoThreadedTest(TSTRTR0SEMMUTEX enmSetup, TSTRTR0SEMMUTEX enmDo, TSTRTR0SEMMUTEX enmCleanup,
                              unsigned cThreads, unsigned cSecs, PTSTRTR0SEMMUTEXREQ pReq, const char *pszTest)
{
    RTTestSubF(g_hTest, "%s - %u threads", pszTest, cThreads);
    RTTHREAD ahThreads[32];
    RTTESTI_CHECK_RET(cThreads <= RT_ELEMENTS(ahThreads), false);

    /*
     * Setup.
     */
    pReq->Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    pReq->Hdr.cbReq = sizeof(*pReq);
    pReq->szMsg[0] = '\0';
    RTTESTI_CHECK_RC_RET(SUPR3CallR0Service("tstRTR0SemMutex", sizeof("tstRTR0SemMutex") - 1, enmSetup, 0, &pReq->Hdr),
                         VINF_SUCCESS, false);
    if (pReq->szMsg[0] == '!')
        return !!RTTestIFailedRc(false, "%s", &pReq->szMsg[1]);
    if (pReq->szMsg[0])
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", pReq->szMsg);

    /*
     * Test execution.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
        ahThreads[i] = NIL_RTTHREAD;

    int rc = VINF_SUCCESS;
    for (unsigned i = 0; i < cThreads && RT_SUCCESS(rc); i++)
        rc = RTThreadCreateF(&ahThreads[i], tstThreadFn, (void *)(uintptr_t)RT_MAKE_U32(enmDo, cSecs), 0, RTTHREADTYPE_DEFAULT,
                             RTTHREADFLAGS_WAITABLE, "test-%u", i);

    for (unsigned i = 0; i < cThreads; i++)
        if (ahThreads[i] != NIL_RTTHREAD)
        {
            int rcThread = VINF_SUCCESS;
            int rc2 = RTThreadWait(ahThreads[i], 3600*1000, &rcThread);
            if (RT_SUCCESS(rc2))
            {
                ahThreads[i] = NIL_RTTHREAD;
                if (RT_FAILURE(rcThread) && RT_SUCCESS(rc2))
                    rc = rcThread;
            }
            else if (RT_SUCCESS(rc))
                rc = rc2;
        }

    /*
     * Cleanup.
     */
    pReq->Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    pReq->Hdr.cbReq = sizeof(*pReq);
    pReq->szMsg[0] = '\0';
    RTTESTI_CHECK_RC_RET(rc = SUPR3CallR0Service("tstRTR0SemMutex", sizeof("tstRTR0SemMutex") - 1, enmCleanup, 0, &pReq->Hdr),
                         VINF_SUCCESS, false);
    if (pReq->szMsg[0] == '!')
        return !!RTTestIFailedRc(false, "%s", &pReq->szMsg[1]);
    if (pReq->szMsg[0])
        RTTestIPrintf(RTTESTLVL_ALWAYS, "%s", pReq->szMsg);

    if (RT_FAILURE(rc))
        for (unsigned i = 0; i < cThreads; i++)
            if (ahThreads[i] != NIL_RTTHREAD)
                RTThreadWait(ahThreads[i], 1000, NULL);

    return RT_SUCCESS(rc);
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
    int rc = RTTestInitAndCreate("tstRTR0SemMutex", &hTest);
    if (rc)
        return rc;
    g_hTest = hTest;
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
        rc = RTPathAppend(szPath, sizeof(szPath), "tstRTR0SemMutex.r0");
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "Failed constructing .r0 filename (rc=%Rrc)", rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    void *pvImageBase;
    rc = SUPR3LoadServiceModule(szPath, "tstRTR0SemMutex",
                                "TSTRTR0SemMutexSrvReqHandler",
                                &pvImageBase);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "SUPR3LoadServiceModule(%s,,,) failed with rc=%Rrc\n", szPath, rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    /* test request */
    TSTRTR0SEMMUTEXREQ Req;

    /*
     * Sanity checks.
     */
    RTTestSub(hTest, "Sanity");
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0SemMutex", sizeof("tstRTR0SemMutex") - 1,
                                             TSTRTR0SEMMUTEX_SANITY_OK, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(hTest);
    RTTESTI_CHECK_MSG(Req.szMsg[0] == '\0', ("%s", Req.szMsg));
    if (Req.szMsg[0] != '\0')
        return RTTestSummaryAndDestroy(hTest);

    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.szMsg[0] = '\0';
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0SemMutex", sizeof("tstRTR0SemMutex") - 1,
                                             TSTRTR0SEMMUTEX_SANITY_FAILURE, 0, &Req.Hdr), VINF_SUCCESS);
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
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service("tstRTR0SemMutex", sizeof("tstRTR0SemMutex") - 1,
                                             TSTRTR0SEMMUTEX_BASIC, 0, &Req.Hdr), VINF_SUCCESS);
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
     * Tests with multiple threads for bugs in the contention part of the code.
     *     Test #2: Try to hold the semaphore for 1 ms.
     *     Test #3: Grab and release immediately.
     *     Test #4: Timeout checks. Try grab it for 0-32 ms and hold it for 1 s.
     */
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST2_SETUP, TSTRTR0SEMMUTEX_TEST2_DO, TSTRTR0SEMMUTEX_TEST2_CLEANUP, 1, 1, &Req, "test #2");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST2_SETUP, TSTRTR0SEMMUTEX_TEST2_DO, TSTRTR0SEMMUTEX_TEST2_CLEANUP, 2, 3, &Req, "test #2");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST2_SETUP, TSTRTR0SEMMUTEX_TEST2_DO, TSTRTR0SEMMUTEX_TEST2_CLEANUP, 3, 3, &Req, "test #2");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST2_SETUP, TSTRTR0SEMMUTEX_TEST2_DO, TSTRTR0SEMMUTEX_TEST2_CLEANUP, 9, 3, &Req, "test #2");

    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST3_SETUP, TSTRTR0SEMMUTEX_TEST3_DO, TSTRTR0SEMMUTEX_TEST3_CLEANUP, 1, 1, &Req, "test #3");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST3_SETUP, TSTRTR0SEMMUTEX_TEST3_DO, TSTRTR0SEMMUTEX_TEST3_CLEANUP, 2, 3, &Req, "test #3");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST3_SETUP, TSTRTR0SEMMUTEX_TEST3_DO, TSTRTR0SEMMUTEX_TEST3_CLEANUP, 3, 3, &Req, "test #3");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST3_SETUP, TSTRTR0SEMMUTEX_TEST3_DO, TSTRTR0SEMMUTEX_TEST3_CLEANUP, 9, 3, &Req, "test #3");

    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST4_SETUP, TSTRTR0SEMMUTEX_TEST4_DO, TSTRTR0SEMMUTEX_TEST4_CLEANUP, 1, 1, &Req, "test #4");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST4_SETUP, TSTRTR0SEMMUTEX_TEST4_DO, TSTRTR0SEMMUTEX_TEST4_CLEANUP, 2, 3, &Req, "test #4");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST4_SETUP, TSTRTR0SEMMUTEX_TEST4_DO, TSTRTR0SEMMUTEX_TEST4_CLEANUP, 3, 3, &Req, "test #4");
    tstDoThreadedTest(TSTRTR0SEMMUTEX_TEST4_SETUP, TSTRTR0SEMMUTEX_TEST4_DO, TSTRTR0SEMMUTEX_TEST4_CLEANUP, 9, 3, &Req, "test #4");

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

