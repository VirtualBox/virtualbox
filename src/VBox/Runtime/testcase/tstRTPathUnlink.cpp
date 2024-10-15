/* $Id: tstRTPathUnlink.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Test RTPathUnlink
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>

#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TMP_PATH_STRCOPY(a_szName) memcpy(&g_szTmpName[g_cchTmpNamePrefix], a_szName, sizeof(a_szName))


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
/** Temporary filename buffer.
 * The main function initializes this with a unique filename/dirname prefix in
 * a temporary directory. */
static char   g_szTmpName[RTPATH_MAX];
/** Length of g_szTmpPrefix.   */
static size_t g_cchTmpNamePrefix;


static void specialStatuses(void)
{
    RTTestSub(g_hTest, "special status codes");

    /* VERR_FILE_NOT_FOUND */
    TMP_PATH_STRCOPY("no-such-file.tmp");
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_FILE_NOT_FOUND);

    /* VERR_PATH_NOT_FOUND */
    TMP_PATH_STRCOPY("no-such-dir.tmp" RTPATH_SLASH_STR "no-such-file.tmp");
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_PATH_NOT_FOUND);

    /* VERR_NOT_A_DIRECTORY (posix) / VERR_PATH_NOT_FOUND (win) */
    TMP_PATH_STRCOPY("not-a-directory-file.tmp");
    size_t offSlash = strlen(g_szTmpName);
    RTFILE hFile = NIL_RTFILE;
    RTTESTI_CHECK_RC(RTFileOpen(&hFile, g_szTmpName, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);

    TMP_PATH_STRCOPY("not-a-directory-file.tmp" RTPATH_SLASH_STR "sub-dir-name.tmp");
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_PATH_NOT_FOUND);

    /* Cleanup. */
    g_szTmpName[offSlash] = '\0';
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_FILE_NOT_FOUND);
}


static void removeSymlink(void)
{
    RTTestSub(g_hTest, "remove symlink");

    /* Create a directory to use as a symlink target. */
    TMP_PATH_STRCOPY("targetdir.tmp");
    char szTargetDir[RTPATH_MAX];
    RTStrCopy(szTargetDir, sizeof(szTargetDir), g_szTmpName);
    RTTESTI_CHECK_RC_RETV(RTDirCreate(szTargetDir, 0755, 0), VINF_SUCCESS);

    /* Create a symlink pointing to it. */
    TMP_PATH_STRCOPY("symlink.tmp");
    int rc = RTSymlinkCreate(g_szTmpName, szTargetDir, RTSYMLINKTYPE_DIR, 0);
    if (RT_SUCCESS(rc))
    {
        /* Remove the symlink. */
        RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VINF_SUCCESS);

        /* The 2nd try should fail. */
        RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_FILE_NOT_FOUND);
    }
    else if (rc == VERR_NOT_SUPPORTED)
        RTTestSkipped(g_hTest, "Symlink creation not supported");
    else
        RTTestFailed(g_hTest, "Failed to create symlink %s: %Rrc", g_szTmpName, rc);

    /* Cleanup the target directory. */
    RTTESTI_CHECK_RC(RTPathUnlink(szTargetDir, 0), VINF_SUCCESS);
}


static void removeDir(void)
{
    RTTestSub(g_hTest, "remove dir");

    /* Create an empty directory. */
    TMP_PATH_STRCOPY("emptydir.tmp");
    RTTESTI_CHECK_RC_RETV(RTDirCreate(g_szTmpName, 0755, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_FILE_NOT_FOUND); /* A 2nd call fails. */

    /* Create a directory with a subdirectory in it. */
    TMP_PATH_STRCOPY("nonemptydir.tmp");
    size_t offSlash = strlen(g_szTmpName);
    RTTESTI_CHECK_RC(RTDirCreate(g_szTmpName, 0755, 0), VINF_SUCCESS);
    TMP_PATH_STRCOPY("nonemptydir.tmp" RTPATH_SLASH_STR "subdir.tmp");
    RTTESTI_CHECK_RC(RTDirCreate(g_szTmpName, 0755, 0), VINF_SUCCESS);

    /* Removing the parent directory should fail. */
    g_szTmpName[offSlash] = '\0';
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_DIR_NOT_EMPTY);

    /* Remove the subdir and then the parent, though, should work fine. */
    g_szTmpName[offSlash] = RTPATH_SLASH;
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VINF_SUCCESS);
    g_szTmpName[offSlash] = '\0';
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VINF_SUCCESS);
}


static void removeFile(void)
{
    RTTestSub(g_hTest, "remove file");

    /* Create a file. */
    TMP_PATH_STRCOPY("file.tmp");
    RTFILE hFile = NIL_RTFILE;
    RTTESTI_CHECK_RC_RETV(RTFileOpen(&hFile, g_szTmpName, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VINF_SUCCESS);

    /* A 2nd call fails. */
    RTTESTI_CHECK_RC(RTPathUnlink(g_szTmpName, 0), VERR_FILE_NOT_FOUND);
}


int main()
{
    /*
     * Init RT+Test.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTPathUnlink", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /*
     * Determine temporary a directory with testboxes in mind.
     */
    AssertCompile(sizeof(g_szTmpName) >= 128*2);
    int rc = RTEnvGetEx(RTENV_DEFAULT, "TESTBOX_PATH_SCRATCH", g_szTmpName, sizeof(g_szTmpName) - 128, NULL);
    if (RT_FAILURE(rc))
        rc = RTPathTemp(g_szTmpName, sizeof(g_szTmpName) - 128);
    if (RT_SUCCESS(rc))
    {
        /* Add a new prefix. */
        size_t off = RTPathEnsureTrailingSeparator(g_szTmpName, sizeof(g_szTmpName));
        off += RTStrPrintf(&g_szTmpName[off], sizeof(g_szTmpName) - off, "tstRTPathUnlink-%RX32-", RTRandU32());
        g_cchTmpNamePrefix = off;

        /*
         * Do the testing.
         */
        removeFile();
        removeDir();
        removeSymlink();
        specialStatuses();
    }
    else
        RTTestIFailed("Failed to get the path to a temporary directory: %Rrc", rc);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(g_hTest);
}

