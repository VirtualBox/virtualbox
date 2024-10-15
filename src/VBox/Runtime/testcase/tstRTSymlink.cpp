/* $Id: tstRTSymlink.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Symbolic Links.
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
#include <iprt/symlink.h>

#include <iprt/test.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/initterm.h>


static void test1Worker(RTTEST hTest, const char *pszBaseDir,
                        const char *pszTarget, RTSYMLINKTYPE enmType, bool fDangling)
{
    char    szPath1[RTPATH_MAX];
    char    szPath2[RTPATH_MAX];
    size_t  cchTarget = strlen(pszTarget);
    char    szPath3[RTPATH_MAX];

    RTStrCopy(szPath3, sizeof(szPath3), pszTarget);

#ifdef RT_OS_WINDOWS
    /* see RTSymlinkCreate in symlink-win.cpp */
    char c;
    char *psz = szPath3;
    while ((c = *psz) != '\0')
    {
        if (c == '/')
            *psz = '\\';
        psz++;
    }
#endif

    /* Create it.*/
    RTTESTI_CHECK_RC_OK_RETV(RTPathJoin(szPath1, sizeof(szPath1), pszBaseDir, "tstRTSymlink-link-1"));
    RTSymlinkDelete(szPath1, 0); /* clean up previous run */
    int rc = RTSymlinkCreate(szPath1, pszTarget, enmType, 0);
    if (rc == VERR_NOT_SUPPORTED)
    {
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "VERR_NOT_SUPPORTED - skipping\n");
        return;
    }
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    /* Check the predicate functions. */
    RTTESTI_CHECK(RTSymlinkExists(szPath1));
    RTTESTI_CHECK(RTSymlinkIsDangling(szPath1) == fDangling);

    /* Read it. */
    memset(szPath2, 0xff, sizeof(szPath2));
    szPath2[sizeof(szPath2) - 1] = '\0';
    RTTESTI_CHECK_RC(RTSymlinkRead(szPath1, szPath2, sizeof(szPath2), 0), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(strcmp(szPath2, szPath3) == 0, ("got=\"%s\" expected=\"%s\"", szPath2, szPath3));

    memset(szPath2, 0xff, sizeof(szPath2));
    szPath2[sizeof(szPath2) - 1] = '\0';
    RTTESTI_CHECK_RC(RTSymlinkRead(szPath1, szPath2, cchTarget + 1, 0), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(strcmp(szPath2, szPath3) == 0, ("got=\"%s\" expected=\"%s\"", szPath2, szPath3));

    memset(szPath2, 0xff, sizeof(szPath2));
    szPath2[sizeof(szPath2) - 1] = '\0';
    RTTESTI_CHECK_RC(RTSymlinkRead(szPath1, szPath2, cchTarget, 0), VERR_BUFFER_OVERFLOW);
    RTTESTI_CHECK_MSG(   strncmp(szPath2, szPath3, cchTarget - 1) == 0
                      && szPath2[cchTarget - 1] == '\0',
                      ("got=\"%s\" expected=\"%.*s\"", szPath2, cchTarget - 1, szPath3));

    /* Other APIs that have to handle symlinks carefully. */
    RTFSOBJINFO ObjInfo;
    RTTESTI_CHECK_RC(rc = RTPathQueryInfo(szPath1, &ObjInfo, RTFSOBJATTRADD_NOTHING), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTESTI_CHECK(RTFS_IS_SYMLINK(ObjInfo.Attr.fMode));
    RTTESTI_CHECK_RC(rc = RTPathQueryInfoEx(szPath1, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        RTTESTI_CHECK(RTFS_IS_SYMLINK(ObjInfo.Attr.fMode));

    if (!fDangling)
    {
        RTTESTI_CHECK_RC(rc = RTPathQueryInfoEx(szPath1, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK(!RTFS_IS_SYMLINK(ObjInfo.Attr.fMode));
        else
            RT_ZERO(ObjInfo);

        if (enmType == RTSYMLINKTYPE_DIR)
        {
            RTTESTI_CHECK(RTDirExists(szPath1));
            RTTESTI_CHECK(RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode));
        }
        else if (enmType == RTSYMLINKTYPE_FILE)
        {
            RTTESTI_CHECK(RTFileExists(szPath1));
            RTTESTI_CHECK(RTFS_IS_FILE(ObjInfo.Attr.fMode));
        }

        /** @todo Check more APIs */
    }

    /* Finally, the removal of the symlink. */
    RTTESTI_CHECK_RC(RTSymlinkDelete(szPath1, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTSymlinkDelete(szPath1, 0), VERR_FILE_NOT_FOUND);
}


static void test1(RTTEST hTest, const char *pszBaseDir)
{
    char szPath1[RTPATH_MAX];

    /*
     * Making some assumptions about how we are executed from to start with...
     */
    RTTestISub("Negative RTSymlinkRead, Exists & IsDangling");
    char szExecDir[RTPATH_MAX];
    RTTESTI_CHECK_RC_OK_RETV(RTPathExecDir(szExecDir, sizeof(szExecDir)));
    RTTESTI_CHECK(RTDirExists(szExecDir));

    char szExecFile[RTPATH_MAX];
    RTTESTI_CHECK_RETV(RTProcGetExecutablePath(szExecFile, sizeof(szExecFile)) != NULL);
    RTTESTI_CHECK(RTFileExists(szExecFile));

    RTTESTI_CHECK(!RTSymlinkExists(szExecFile));
    RTTESTI_CHECK(!RTSymlinkExists(szExecDir));
    RTTESTI_CHECK(!RTSymlinkIsDangling(szExecFile));
    RTTESTI_CHECK(!RTSymlinkIsDangling(szExecDir));
    RTTESTI_CHECK(!RTSymlinkExists("/"));
    RTTESTI_CHECK(!RTSymlinkIsDangling("/"));
    RTTESTI_CHECK(!RTSymlinkExists("/some/non-existing/directory/name/iprt"));
    RTTESTI_CHECK(!RTSymlinkExists("/some/non-existing/directory/name/iprt/"));
    RTTESTI_CHECK(!RTSymlinkIsDangling("/some/non-existing/directory/name/iprt"));
    RTTESTI_CHECK(!RTSymlinkIsDangling("/some/non-existing/directory/name/iprt/"));

    RTTESTI_CHECK_RC(RTSymlinkRead(szExecFile, szPath1, sizeof(szPath1), 0), VERR_NOT_SYMLINK);
    RTTESTI_CHECK_RC(RTSymlinkRead(szExecDir,  szPath1, sizeof(szPath1), 0), VERR_NOT_SYMLINK);

    /*
     * Do some symlinking.  ASSUME they are supported on the test file system.
     */
    RTTestISub("Basics");
    RTTESTI_CHECK_RETV(RTDirExists(pszBaseDir));
    test1Worker(hTest, pszBaseDir, szExecFile, RTSYMLINKTYPE_FILE,    false /*fDangling*/);
    test1Worker(hTest, pszBaseDir, szExecDir,  RTSYMLINKTYPE_DIR,     false /*fDangling*/);
    test1Worker(hTest, pszBaseDir, szExecFile, RTSYMLINKTYPE_UNKNOWN, false /*fDangling*/);
    test1Worker(hTest, pszBaseDir, szExecDir,  RTSYMLINKTYPE_UNKNOWN, false /*fDangling*/);

    /*
     * Create a few dangling links.
     */
    RTTestISub("Dangling links");
    test1Worker(hTest, pszBaseDir, "../dangle/dangle",  RTSYMLINKTYPE_FILE,     true /*fDangling*/);
    test1Worker(hTest, pszBaseDir, "../dangle/dangle",  RTSYMLINKTYPE_DIR,      true /*fDangling*/);
    test1Worker(hTest, pszBaseDir, "../dangle/dangle",  RTSYMLINKTYPE_UNKNOWN,  true /*fDangling*/);
    test1Worker(hTest, pszBaseDir, "../dangle/dangle/", RTSYMLINKTYPE_UNKNOWN,  true /*fDangling*/);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTSymlink", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    test1(hTest, ".");

    return RTTestSummaryAndDestroy(hTest);
}

