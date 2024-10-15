/* $Id: tstRTVfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - IPRT Virtual File System (VFS) API
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include <iprt/vfs.h>
#include <iprt/errcore.h>
#include <iprt/test.h>
#include <iprt/file.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

static const char *StandardHandleToString(RTHANDLESTD enmHandle)
{
    switch (enmHandle)
    {
        case RTHANDLESTD_INPUT:  return "STDIN";
        case RTHANDLESTD_OUTPUT: return "STDOUT";
        case RTHANDLESTD_ERROR:  return "STDERR";
        default: break;
    }

    return "unknown";
}

static void tstVfsIoFromStandardHandle(RTTEST hTest, RTHANDLESTD enmHandle)
{
    RTTestSubF(hTest, "RTVfsIoStrmFromStdHandle(%s)", StandardHandleToString(enmHandle));

    RTVFSIOSTREAM hVfsIos = NIL_RTVFSIOSTREAM;
    int rc = RTVfsIoStrmFromStdHandle(enmHandle, 0, true /*fLeaveOpen*/, &hVfsIos);
    if (RT_SUCCESS(rc))
    {
        if (   enmHandle == RTHANDLESTD_OUTPUT
            || enmHandle == RTHANDLESTD_ERROR)
        {
            char szTmp[80];
            size_t cchTmp = RTStrPrintf(szTmp, sizeof(szTmp), "Test output to %s\n", StandardHandleToString(enmHandle));

            size_t cbWritten;
            RTTESTI_CHECK_RC(rc = RTVfsIoStrmWrite(hVfsIos, szTmp, cchTmp, true /*fBlocking*/, &cbWritten), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
                RTTESTI_CHECK(cbWritten == cchTmp);
        }

        uint32_t cRefs = RTVfsIoStrmRelease(hVfsIos);
        RTTESTI_CHECK_MSG(cRefs == 0, ("cRefs=%#x\n", cRefs));
    }
    else
        RTTestFailed(hTest, "Error creating VFS I/O stream for %s: %Rrc\n", StandardHandleToString(enmHandle), rc);
}



int main(int argc, char **argv)
{
    RT_NOREF2(argc, argv);

    /*
     * Initialize IPRT and create the test.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTVfs", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstVfsIoFromStandardHandle(hTest, RTHANDLESTD_INPUT);
    tstVfsIoFromStandardHandle(hTest, RTHANDLESTD_OUTPUT);
    tstVfsIoFromStandardHandle(hTest, RTHANDLESTD_ERROR);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

