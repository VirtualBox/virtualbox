/* $Id: tstRTPrfIO.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Profile IPRT I/O APIs.
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
#include <iprt/file.h>
#include <iprt/dir.h>
#include <iprt/fs.h>

#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test instance handle. */
static RTTEST       g_hTest;
/** The max number of nanoseconds to benchmark an operation. */
static uint64_t     g_cNsPerOperation = 1000*1000*1000;
/** The max operation count. */
static uint32_t     g_cMaxOperations  = 1000000;
/** The path to the test directory. */
static const char  *g_pszTestDir = ".";

/** The path to the primary test file. */
static char         g_szTestFile1[RTPATH_MAX];
/** The path to the primary test directory. */
static char         g_szTestDir1[RTPATH_MAX];

/** The path to a nonexistent file in an existing directory. */
static char         g_szNotExitingFile[RTPATH_MAX];
/** The path to a nonexistent directory. */
static char         g_szNotExitingDir[RTPATH_MAX];
/** The path to a nonexistent file in a nonexistent directory. */
static char         g_szNotExitingDirFile[RTPATH_MAX];


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/**
 * Benchmark an operation.
 * @param   stmt        Statement to benchmark.
 * @param   what        String literal describing what's being benchmarked..
 */
#define TIME_OP(stmt, what) \
    do \
    { \
        /* warm-up */ \
        stmt; \
        stmt; \
        \
        /* the real thing */ \
        uint32_t cOps       = 0; \
        uint64_t cNsElapsed = 0; \
        uint64_t u64StartTS = RTTimeNanoTS(); \
        for (;;) \
        { \
            stmt; \
            cOps++; \
            if ((cOps & 127) == 127) \
            { \
                cNsElapsed = RTTimeNanoTS() - u64StartTS; \
                if (cNsElapsed >= g_cNsPerOperation || cOps >= g_cMaxOperations) \
                    break; \
            } \
        } \
        RTTestValue(g_hTest, what, cNsElapsed / cOps, RTTESTUNIT_NS_PER_CALL); \
        RTTestValue(g_hTest, what " cps", UINT64_C(10000000000) / (cNsElapsed * 10 / cOps), RTTESTUNIT_CALLS_PER_SEC); \
    } while (0)



static void benchmarkPathQueryInfo(void)
{
    RTTestSub(g_hTest, "RTPathQueryInfo");

    RTFSOBJINFO ObjInfo;

    RTTESTI_CHECK_RC_RETV(RTPathQueryInfo(g_szNotExitingFile, &ObjInfo, RTFSOBJATTRADD_NOTHING), VERR_FILE_NOT_FOUND);
    TIME_OP(RTPathQueryInfo(g_szNotExitingFile, &ObjInfo, RTFSOBJATTRADD_NOTHING), "RTPathQueryInfo(g_szNotExitingFile)");

    int rc = RTPathQueryInfo(g_szNotExitingDirFile, &ObjInfo, RTFSOBJATTRADD_NOTHING);
    RTTESTI_CHECK_RETV(rc == VERR_PATH_NOT_FOUND || VERR_FILE_NOT_FOUND);
    TIME_OP(RTPathQueryInfo(g_szNotExitingDirFile, &ObjInfo, RTFSOBJATTRADD_NOTHING), "RTPathQueryInfo(g_szNotExitingDirFile)");

    RTTESTI_CHECK_RC_RETV(RTPathQueryInfo(g_pszTestDir, &ObjInfo, RTFSOBJATTRADD_NOTHING), VINF_SUCCESS);
    TIME_OP(RTPathQueryInfo(g_pszTestDir, &ObjInfo, RTFSOBJATTRADD_NOTHING), "RTPathQueryInfo(g_pszTestDir)");

    RTTESTI_CHECK_RC_RETV(RTPathQueryInfo(g_pszTestDir, &ObjInfo, RTFSOBJATTRADD_UNIX), VINF_SUCCESS);
    TIME_OP(RTPathQueryInfo(g_pszTestDir, &ObjInfo, RTFSOBJATTRADD_UNIX), "RTPathQueryInfo(g_pszTestDir,UNIX)");

    RTTestSubDone(g_hTest);
}


DECL_FORCE_INLINE(int) benchmarkFileOpenCloseOp(const char *pszFilename)
{
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
    if (RT_SUCCESS(rc))
        rc = RTFileClose(hFile);
    return rc;
}

static void benchmarkFileOpenClose(void)
{
    RTTestSub(g_hTest, "RTFileOpen + RTFileClose");

    RTTESTI_CHECK_RC_RETV(benchmarkFileOpenCloseOp(g_szNotExitingFile), VERR_FILE_NOT_FOUND);
    TIME_OP(benchmarkFileOpenCloseOp(g_szNotExitingFile), "RTFileOpen(g_szNotExitingFile)");

    RTTESTI_CHECK_RC_RETV(benchmarkFileOpenCloseOp(g_szNotExitingFile), VERR_FILE_NOT_FOUND);
    TIME_OP(benchmarkFileOpenCloseOp(g_szNotExitingFile), "RTFileOpen(g_szNotExitingFile)");

    int rc = benchmarkFileOpenCloseOp(g_szNotExitingDirFile);
    RTTESTI_CHECK_RETV(rc == VERR_PATH_NOT_FOUND || VERR_FILE_NOT_FOUND);
    TIME_OP(benchmarkFileOpenCloseOp(g_szNotExitingDirFile), "RTFileOpen(g_szNotExitingDirFile)");

    RTTestSubDone(g_hTest);
}


static void benchmarkFileWriteByte(void)
{
    RTTestSub(g_hTest, "RTFileWrite(byte)");

    RTFILE hFile;

    RTTESTI_CHECK_RC_RETV(RTFileOpen(&hFile, g_szTestFile1,
                                     RTFILE_O_WRITE | RTFILE_O_DENY_NONE | RTFILE_O_CREATE_REPLACE
                                     | (0655 << RTFILE_O_CREATE_MODE_SHIFT)),
                          VINF_SUCCESS);
    static const char   s_szContent[] = "0123456789abcdef";
    uint32_t            offContent = 0;
    int rc;;
    RTTESTI_CHECK_RC(rc = RTFileWrite(hFile, &s_szContent[offContent++ % RT_ELEMENTS(s_szContent)], 1, NULL), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        TIME_OP(RTFileWrite(hFile, &s_szContent[offContent++ % RT_ELEMENTS(s_szContent)], 1, NULL), "RTFileWrite(byte)");
    }
    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);

    RTTestSubDone(g_hTest);
}



int main(int argc, char **argv)
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTPrfIO", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /*
     * Parse arguments
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--test-dir",     'd',    RTGETOPT_REQ_STRING },
    };
    bool fFileOpenCloseTest = true;
    bool fFileWriteByteTest = true;
    bool fPathQueryInfoTest = true;
    //bool fFileTests = true;
    //bool fDirTests  = true;

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'd':
                g_pszTestDir = ValueUnion.psz;
                break;

            case 'V':
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "$Revision: 106320 $\n");
                return RTTestSummaryAndDestroy(g_hTest);

            case 'h':
                RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "usage: testname [-d <testdir>]\n");
                return RTTestSummaryAndDestroy(g_hTest);

            default:
                RTTestFailed(g_hTest, "invalid argument");
                RTGetOptPrintError(ch, &ValueUnion);
                return RTTestSummaryAndDestroy(g_hTest);
        }
    }

    /*
     * Set up and check the prerequisites.
     */
    RTTESTI_CHECK_RC(RTPathJoin(g_szTestFile1,      sizeof(g_szTestFile1),      g_pszTestDir, "tstRTPrfIO-TestFile1"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathJoin(g_szTestDir1,       sizeof(g_szTestDir1),       g_pszTestDir, "tstRTPrfIO-TestDir1"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathJoin(g_szNotExitingFile, sizeof(g_szNotExitingFile), g_pszTestDir, "tstRTPrfIO-nonexistent-file"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathJoin(g_szNotExitingDir,  sizeof(g_szNotExitingDir),  g_pszTestDir, "tstRTPrfIO-nonexistent-dir"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPathJoin(g_szNotExitingDirFile, sizeof(g_szNotExitingDirFile),  g_szNotExitingDir, "nonexistent-file"), VINF_SUCCESS);
    RTTESTI_CHECK(RTDirExists(g_pszTestDir));
    if (RTPathExists(g_szTestDir1))
        RTTestFailed(g_hTest, "The primary test directory (%s) already exist, please remove it", g_szTestDir1);
    if (RTPathExists(g_szTestFile1))
        RTTestFailed(g_hTest, "The primary test file (%s) already exist, please remove it", g_szTestFile1);
    if (RTPathExists(g_szNotExitingFile))
        RTTestFailed(g_hTest, "'%s' exists, remove it", g_szNotExitingFile);
    if (RTPathExists(g_szNotExitingDir))
        RTTestFailed(g_hTest, "'%s' exists, remove it", g_szNotExitingDir);
    if (RTPathExists(g_szNotExitingDirFile))
        RTTestFailed(g_hTest, "'%s' exists, remove it", g_szNotExitingDirFile);

    /*
     * Do the testing.
     */
    if (RTTestIErrorCount() == 0)
    {
#if 1
        if (fPathQueryInfoTest)
            benchmarkPathQueryInfo();
        if (fFileOpenCloseTest)
            benchmarkFileOpenClose();
#endif
        if (fFileWriteByteTest)
            benchmarkFileWriteByte();
        //if (fFileTests)
        //    benchmarkFile();
        //if (fDirTests)
        //    benchmarkDir();

        /*
         * Cleanup.
         */
        RTFileDelete(g_szTestFile1);
        RTDirRemoveRecursive(g_szTestDir1, 0);
        RTTESTI_CHECK(RTDirExists(g_pszTestDir));
        RTTESTI_CHECK(!RTPathExists(g_szTestDir1));
        RTTESTI_CHECK(!RTPathExists(g_szTestFile1));
    }

    return RTTestSummaryAndDestroy(g_hTest);
}

