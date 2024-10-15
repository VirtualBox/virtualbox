/* $Id: tstRTProcCreatePrf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTProcCreate Profiling.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/process.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/path.h>
#include <iprt/env.h>
#include <iprt/string.h>


int main(int argc, char **argv)
{
    /* the child response. */
    if (argc != 1)
        return 0;

    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTProcCreatePrf", &hTest);
    if (rcExit)
        return rcExit;
    RTTestBanner(hTest);

    char szExecPath[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szExecPath, sizeof(szExecPath)))
        RTStrCopy(szExecPath, sizeof(szExecPath), argv[0]);

    const char *apszArgs[4] = { szExecPath, "child", "process", NULL };

    uint64_t NsStart = RTTimeNanoTS();
    uint32_t i;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2) || defined(RT_OS_DARWIN)
    for (i = 0; i < 1000; i++)
#else
    for (i = 0; i < 10000; i++)
#endif
    {
        RTPROCESS hProc;
        RTTEST_CHECK_RC_BREAK(hTest, RTProcCreate(szExecPath, apszArgs, RTENV_DEFAULT, 0 /* fFlags*/, &hProc), VINF_SUCCESS);
        RTPROCSTATUS ChildStatus;
        RTTEST_CHECK_RC_BREAK(hTest, RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ChildStatus), VINF_SUCCESS);
        RTTEST_CHECK_BREAK(hTest, ChildStatus.enmReason == RTPROCEXITREASON_NORMAL);
        RTTEST_CHECK_BREAK(hTest, ChildStatus.iStatus == 0);
    }
    uint64_t cNsElapsed = RTTimeNanoTS() - NsStart;
    if (i)
    {
        RTTestValue(hTest, "Time per process", cNsElapsed / i, RTTESTUNIT_NS);
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

