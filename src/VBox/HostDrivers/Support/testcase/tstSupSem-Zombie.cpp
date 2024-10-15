/* $Id: tstSupSem-Zombie.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Support Library Testcase - Ring-3 Semaphore interface - Zombie bugs.
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
#include <VBox/sup.h>

#include <VBox/param.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
static PSUPDRVSESSION   g_pSession;
static RTTEST           g_hTest;



static DECLCALLBACK(int) tstSupSemSRETimed(RTTHREAD hSelf, void *pvUser)
{
    SUPSEMEVENT hEvent = (SUPSEMEVENT)pvUser;
    RTThreadUserSignal(hSelf);
    int rc = SUPSemEventWaitNoResume(g_pSession, hEvent, 120*1000);
    AssertReleaseMsg(rc == VERR_INTERRUPTED, ("%Rrc\n", rc));
    return rc;
}


static DECLCALLBACK(int) tstSupSemMRETimed(RTTHREAD hSelf, void *pvUser)
{
    SUPSEMEVENTMULTI hEventMulti = (SUPSEMEVENTMULTI)pvUser;
    RTThreadUserSignal(hSelf);
    int rc = SUPSemEventMultiWaitNoResume(g_pSession, hEventMulti, 120*1000);
    AssertReleaseMsg(rc == VERR_INTERRUPTED, ("%Rrc\n", rc));
    return rc;
}


static DECLCALLBACK(int) tstSupSemSREInf(RTTHREAD hSelf, void *pvUser)
{
    SUPSEMEVENT hEvent = (SUPSEMEVENT)pvUser;
    RTThreadUserSignal(hSelf);
    int rc = SUPSemEventWaitNoResume(g_pSession, hEvent, RT_INDEFINITE_WAIT);
    AssertReleaseMsg(rc == VERR_INTERRUPTED, ("%Rrc\n", rc));
    return rc;
}


static DECLCALLBACK(int) tstSupSemMREInf(RTTHREAD hSelf, void *pvUser)
{
    SUPSEMEVENTMULTI hEventMulti = (SUPSEMEVENTMULTI)pvUser;
    RTThreadUserSignal(hSelf);
    int rc = SUPSemEventMultiWaitNoResume(g_pSession, hEventMulti, RT_INDEFINITE_WAIT);
    AssertReleaseMsg(rc == VERR_INTERRUPTED, ("%Rrc\n", rc));
    return rc;
}

static int mainChild(void)
{
    /*
     * Init.
     */
    int rc = RTR3InitExeNoArguments(RTR3INIT_FLAGS_TRY_SUPLIB);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSupSem-Zombie-Child: fatal error: RTR3InitExeNoArguments failed with rc=%Rrc\n", rc);
        return 1;
    }

    RTTEST hTest;
    rc = RTTestCreate("tstSupSem-Zombie-Child", &hTest);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSupSem-Zombie-Child: fatal error: RTTestCreate failed with rc=%Rrc\n", rc);
        return 1;
    }
    g_hTest = hTest;

    PSUPDRVSESSION pSession;
    rc = SUPR3Init(&pSession);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(hTest, "SUPR3Init failed with rc=%Rrc\n", rc);
        return RTTestSummaryAndDestroy(hTest);
    }
    g_pSession = pSession;

    /*
     * A semaphore of each kind and throw a bunch of threads on them.
     */
    SUPSEMEVENT hEvent = NIL_SUPSEMEVENT;
    RTTESTI_CHECK_RC(rc = SUPSemEventCreate(pSession, &hEvent), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        SUPSEMEVENTMULTI hEventMulti = NIL_SUPSEMEVENT;
        RTTESTI_CHECK_RC(SUPSemEventMultiCreate(pSession, &hEventMulti), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            for (uint32_t cThreads = 0; cThreads < 5; cThreads++)
            {
                RTTHREAD hThread;
                RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemSRETimed, (void *)hEvent,      0, RTTHREADTYPE_TIMER, 0 /*fFlags*/, "IntSRE"), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemMRETimed, (void *)hEventMulti, 0, RTTHREADTYPE_TIMER, 0 /*fFlags*/, "IntMRE"), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemSREInf,   (void *)hEvent,      0, RTTHREADTYPE_TIMER, 0 /*fFlags*/, "IntSRE"), VINF_SUCCESS);
                RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstSupSemMREInf,   (void *)hEventMulti, 0, RTTHREADTYPE_TIMER, 0 /*fFlags*/, "IntMRE"), VINF_SUCCESS);
                RTThreadSleep(2);
            }
            RTThreadSleep(50);

            /*
             * This is where the test really starts...
             */
            return 0;
        }
    }

    return RTTestSummaryAndDestroy(hTest);
}


/**
 * The parent main routine.
 * @param   argv0       The executable name (or whatever).
 */
static int mainParent(const char *argv0)
{
    /*
     * Init.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstSupSem-Zombie", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Spin of the child process which may or may not turn into a zombie
     */
    for (uint32_t iPass = 0; iPass < 32; iPass++)
    {
        RTTestSubF(hTest, "Pass %u", iPass);

        RTPROCESS hProcess;
        const char *apszArgs[3] = { argv0, "--child", NULL };
        RTTESTI_CHECK_RC_OK(rc = RTProcCreate(argv0, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, &hProcess));
        if (RT_SUCCESS(rc))
        {
            /*
             * Wait for 60 seconds then give up.
             */
            RTPROCSTATUS    Status;
            uint64_t        StartTS = RTTimeMilliTS();
            for (;;)
            {
                rc = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &Status);
                if (RT_SUCCESS(rc))
                    break;
                uint64_t cElapsed = RTTimeMilliTS() - StartTS;
                if (cElapsed > 60*1000)
                    break;
                RTThreadSleep(cElapsed < 60 ? 30 : cElapsed < 200 ? 10 : 100);
            }
            RTTESTI_CHECK_RC_OK(rc);
            if (    RT_SUCCESS(rc)
                &&  (   Status.enmReason != RTPROCEXITREASON_NORMAL
                     || Status.iStatus != 0))
            {
                RTTestIFailed("child %d (%#x) reason %d\n", Status.iStatus, Status.iStatus, Status.enmReason);
                rc = VERR_PERMISSION_DENIED;
            }
        }
        /* one zombie process is enough. */
        if (RT_FAILURE(rc))
            break;
    }

    return RTTestSummaryAndDestroy(hTest);
}


int main(int argc, char **argv)
{
    if (    argc == 2
        &&  !strcmp(argv[1], "--child"))
        return mainChild();
    return mainParent(argv[0]);
}

