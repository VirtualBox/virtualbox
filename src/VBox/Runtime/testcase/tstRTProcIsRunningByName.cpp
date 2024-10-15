/* $Id: tstRTProcIsRunningByName.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTProcIsRunningByName
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
#include <iprt/process.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>



int main(int argc, char **argv)
{
    int cErrors = 0;

    RTR3InitExe(argc, &argv, 0);
    RTPrintf("tstRTPRocIsRunningByName: TESTING...\n");

    /*
     * Test 1: Check for a definitely not running process.
     */
    char szExecPath[RTPATH_MAX] = { "vbox-5b05e1ff-6ae2-4d10-885a-7d25018c4c5b" };
    if (!RTProcIsRunningByName(szExecPath))
        RTPrintf("tstRTProcIsRunningByName: Process '%s' is not running (expected).\n", szExecPath);
    else
    {
        RTPrintf("tstRTProcIsRunningByName: FAILURE - '%s' is running! (test 1)\n", szExecPath);
        cErrors++;
    }

    /*
     * Test 2: Check for a definitely not running process.
     */
    strcpy(szExecPath, "/bin/vbox-5b05e1ff-6ae2-4d10-885a-7d25018c4c5b");
    if (!RTProcIsRunningByName(szExecPath))
        RTPrintf("tstRTProcIsRunningByName: Process '%s' is not running (expected).\n", szExecPath);
    else
    {
        RTPrintf("tstRTProcIsRunningByName: FAILURE - '%s' is running! (test 1)\n", szExecPath);
        cErrors++;
    }

    /*
     * Test 3: Check for our own process, filename only.
     */
    if (RTProcGetExecutablePath(szExecPath, RTPATH_MAX))
    {
        /* Strip any path components */
        char *pszFilename = RTPathFilename(szExecPath);
        if (pszFilename)
        {
            if (RTProcIsRunningByName(pszFilename))
                RTPrintf("tstRTProcIsRunningByName: Process '%s' (self) is running\n", pszFilename);
            else
            {
                RTPrintf("tstRTProcIsRunningByName: FAILURE - Process '%s' (self) is not running!\n", pszFilename);
                cErrors++;
            }
        }
        else
        {
            RTPrintf("tstRTProcIsRunningByName: FAILURE - RTPathFilename failed!\n");
            cErrors++;
        }

        /*
         * Test 4: Check for our own process, full path.
         */
        if (RTProcIsRunningByName(szExecPath))
            RTPrintf("tstRTProcIsRunningByName: Process '%s' (self) is running\n", szExecPath);
        else
        {
            RTPrintf("tstRTProcIsRunningByName: FAILURE - Process '%s' (self) is not running!\n", szExecPath);
            cErrors++;
        }
    }
    else
    {
        RTPrintf("tstRTProcIsRunningByName: FAILURE - RTProcGetExecutablePath failed!\n");
        cErrors++;
    }


    /*
     * Summary.
     */
    if (!cErrors)
        RTPrintf("tstRTProcIsRunningByName: SUCCESS\n");
    else
        RTPrintf("tstRTProcIsRunningByName: FAILURE - %d errors\n", cErrors);
    return cErrors ? 1 : 0;
}
