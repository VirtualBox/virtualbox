/* $Id: tstRTCoreDump.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Core Dumper.
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
#include <iprt/coredumper.h>

#include <iprt/test.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Globals                                                                                                                      *
*********************************************************************************************************************************/
static DECLCALLBACK(int) SleepyThread(RTTHREAD hThread, void *pvUser)
{
    NOREF(pvUser);
    RTThreadUserWait(hThread, 90000000);
    return VINF_SUCCESS;
}

int main()
{
    RTTEST     hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTCoreDump", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Setup core dumping.
     */
    int rc;
    RTTESTI_CHECK_RC(rc = RTCoreDumperSetup(NULL, RTCOREDUMPER_FLAGS_REPLACE_SYSTEM_DUMP | RTCOREDUMPER_FLAGS_LIVE_CORE),
                     VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        /*
         * Spawn a few threads.
         */
        RTTHREAD ahThreads[5];
        unsigned i;
        for (i = 0; i < RT_ELEMENTS(ahThreads); i++)
        {
            RTTESTI_CHECK_RC_BREAK(RTThreadCreate(&ahThreads[i], SleepyThread, &ahThreads[i], 0, RTTHREADTYPE_DEFAULT,
                                                  RTTHREADFLAGS_WAITABLE, "TEST1"),
                                   VINF_SUCCESS);
        }
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Spawned %d threads.\n", i);

        /*
         * Write the core to disk.
         */
        RTTESTI_CHECK_RC(RTCoreDumperTakeDump(NULL, true /* fLiveCore*/), VINF_SUCCESS);

        /*
         * Clean up.
         */
        RTTESTI_CHECK_RC(RTCoreDumperDisable(), VINF_SUCCESS);
        while (i-- > 0)
        {
            RTTESTI_CHECK_RC(RTThreadUserSignal(ahThreads[i]), VINF_SUCCESS);
            RTTESTI_CHECK_RC(RTThreadWait(ahThreads[i], 60*1000, NULL), VINF_SUCCESS);
        }
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

