/* $Id: tstRTFileQuerySize-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTFileQuerySize.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/file.h>

#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>


static void test1(const char *pszSubTest, const char *pszFilename)
{
    int rc;
    RTTestISub(pszSubTest);

    RTFILE hFile;
    rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
    if (RT_FAILURE(rc))
    {
        if (   rc == VERR_ACCESS_DENIED
            || rc == VERR_PERMISSION_DENIED
            || rc == VERR_FILE_NOT_FOUND)
        {
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Cannot access '%s', skipping.", pszFilename);
            return;
        }
        RTTESTI_CHECK_RC_RETV(RTFileOpen(&hFile, pszFilename, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN), VINF_SUCCESS);
    }

    uint64_t    cbFile = UINT64_MAX - 42;
    RTTESTI_CHECK_RC(rc = RTFileQuerySize(hFile, &cbFile), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK(cbFile != UINT64_MAX - 42);
        RTTestIValue(pszSubTest, cbFile, RTTESTUNIT_BYTES);
    }

    RTFileClose(hFile);
    RTTestISubDone();
}


int main(int argc, char **argv)
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTFileQuerySize-1", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    for (int i = 0; i < argc; i++)
    {
        char *pszNm = RTPathFilename(argv[i]);
        if (!pszNm)
            pszNm = argv[i];
        test1(pszNm, argv[i]);
    }

#ifdef RT_OS_WINDOWS
    test1("//./PhysicalDrive0", "//./PhysicalDrive0");
    test1("//./HarddiskVolume1", "//./HarddiskVolume1");
    test1("//./nul", "//./nul");
#else
    test1("/dev/null", "/dev/null");
# ifdef RT_OS_LINUX
    test1("/dev/sda",  "/dev/sda");
    test1("/dev/sda1", "/dev/sda1");
    test1("/dev/sda5", "/dev/sda5");
# endif
#endif

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

