/* $Id: tstRTFileOpenEx-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - File Opening, extended API.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char g_szTestFile[] = "tstFileOpenEx-1.tst";


/** @note FsPerf have a copy of this code.   */
static void tstOpenExTest(unsigned uLine, int cbExist, int cbNext, const char *pszFilename, uint64_t fAction,
                          int rcExpect, RTFILEACTION enmActionExpected)
{
    uint64_t const  fCreateMode = (0644 << RTFILE_O_CREATE_MODE_SHIFT);
    RTFILE          hFile;
    int             rc;

    /*
     * File existence and size.
     */
    bool fOkay = false;
    RTFSOBJINFO ObjInfo;
    rc = RTPathQueryInfoEx(pszFilename, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
        fOkay = cbExist == (int64_t)ObjInfo.cbObject;
    else
        fOkay = rc == VERR_FILE_NOT_FOUND && cbExist < 0;
    if (!fOkay)
    {
        if (cbExist >= 0)
        {
            rc = RTFileOpen(&hFile, pszFilename, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE | fCreateMode);
            if (RT_SUCCESS(rc))
            {
                while (cbExist > 0)
                {
                    int cbToWrite = (int)strlen(pszFilename);
                    if (cbToWrite > cbExist)
                        cbToWrite = cbExist;
                    rc = RTFileWrite(hFile, pszFilename, cbToWrite, NULL);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("%u: RTFileWrite(%s,%#x) -> %Rrc\n", uLine, pszFilename, cbToWrite, rc);
                        break;
                    }
                    cbExist -= cbToWrite;
                }

                RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);
            }
            else
                RTTestIFailed("%u: RTFileOpen(%s) -> %Rrc\n", uLine, pszFilename, rc);

        }
        else
        {
            rc = RTFileDelete(pszFilename);
            if (rc != VINF_SUCCESS && rc != VERR_FILE_NOT_FOUND)
                RTTestIFailed("%u: RTFileDelete(%s) -> %Rrc\n", uLine, pszFilename, rc);
        }
    }

    /*
     * The actual test.
     */
    RTFILEACTION enmActuallyTaken = RTFILEACTION_END;
    hFile = NIL_RTFILE;
    rc = RTFileOpenEx(pszFilename, fAction | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | fCreateMode, &hFile, &enmActuallyTaken);
    if (   rc != rcExpect
        || enmActuallyTaken != enmActionExpected
        || (RT_SUCCESS(rc) ? hFile == NIL_RTFILE : hFile != NIL_RTFILE))
        RTTestIFailed("%u: RTFileOpenEx(%s, %#llx) -> %Rrc + %d  (hFile=%p), expected %Rrc + %d\n",
                      uLine, pszFilename, fAction, rc, enmActuallyTaken, hFile, rcExpect, enmActionExpected);
    if (RT_SUCCESS(rc))
    {
        if (   enmActionExpected == RTFILEACTION_REPLACED
            || enmActionExpected == RTFILEACTION_TRUNCATED)
        {
            uint8_t abBuf[16];
            rc = RTFileRead(hFile, abBuf, 1, NULL);
            if (rc != VERR_EOF)
                RTTestIFailed("%u: RTFileRead(%s,,1,) -> %Rrc, expected VERR_EOF\n", uLine, pszFilename, rc);
        }

        while (cbNext > 0)
        {
            int cbToWrite = (int)strlen(pszFilename);
            if (cbToWrite > cbNext)
                cbToWrite = cbNext;
            rc = RTFileWrite(hFile, pszFilename, cbToWrite, NULL);
            if (RT_FAILURE(rc))
            {
                RTTestIFailed("%u: RTFileWrite(%s,%#x) -> %Rrc\n", uLine, pszFilename, cbToWrite, rc);
                break;
            }
            cbNext -= cbToWrite;
        }

        rc = RTFileClose(hFile);
        if (RT_FAILURE(rc))
            RTTestIFailed("%u: RTFileClose(%p) -> %Rrc\n", uLine, hFile, rc);
    }
}


/** @note FsPerf have a copy of this code.   */
static void tstFileActionTaken(RTTEST hTest)
{
    RTTestSub(hTest, "Action taken");

    /*
     * RTFILE_O_OPEN and RTFILE_O_OPEN_CREATE.
     */
    /* RTFILE_O_OPEN - non-existing: */
    tstOpenExTest(__LINE__, -1, -1, g_szTestFile, RTFILE_O_OPEN,                         VERR_FILE_NOT_FOUND, RTFILEACTION_INVALID);

    /* RTFILE_O_OPEN_CREATE - non-existing: */
    tstOpenExTest(__LINE__, -1, -1, g_szTestFile, RTFILE_O_OPEN_CREATE,                         VINF_SUCCESS, RTFILEACTION_CREATED);

    /* RTFILE_O_OPEN_CREATE - existing: */
    tstOpenExTest(__LINE__,  0,  0, g_szTestFile, RTFILE_O_OPEN_CREATE,                         VINF_SUCCESS, RTFILEACTION_OPENED);

    /* RTFILE_O_OPEN - existing: */
    tstOpenExTest(__LINE__,  0,  0, g_szTestFile, RTFILE_O_OPEN,                                VINF_SUCCESS, RTFILEACTION_OPENED);

    /*
     * RTFILE_O_OPEN and RTFILE_O_OPEN_CREATE w/ TRUNCATE variations.
     */
    /* RTFILE_O_OPEN + TRUNCATE - existing zero sized file: */
    tstOpenExTest(__LINE__,  0,  0, g_szTestFile, RTFILE_O_OPEN | RTFILE_O_TRUNCATE,            VINF_SUCCESS, RTFILEACTION_TRUNCATED);

    /* RTFILE_O_OPEN_CREATE + TRUNCATE - existing zero sized file: */
    tstOpenExTest(__LINE__,  0, 10, g_szTestFile, RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE,     VINF_SUCCESS, RTFILEACTION_TRUNCATED);

    /* RTFILE_O_OPEN_CREATE + TRUNCATE - existing non-zero sized file: */
    tstOpenExTest(__LINE__, 10, 10, g_szTestFile, RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE,     VINF_SUCCESS, RTFILEACTION_TRUNCATED);

    /* RTFILE_O_OPEN + TRUNCATE - existing non-zero sized file: */
    tstOpenExTest(__LINE__, 10, -1, g_szTestFile, RTFILE_O_OPEN | RTFILE_O_TRUNCATE,            VINF_SUCCESS, RTFILEACTION_TRUNCATED);

    /* RTFILE_O_OPEN + TRUNCATE - non-existing file: */
    tstOpenExTest(__LINE__, -1, -1, g_szTestFile, RTFILE_O_OPEN | RTFILE_O_TRUNCATE,     VERR_FILE_NOT_FOUND, RTFILEACTION_INVALID);

    /* RTFILE_O_OPEN_CREATE + TRUNCATE - non-existing file: */
    tstOpenExTest(__LINE__, -1,  0, g_szTestFile, RTFILE_O_OPEN_CREATE | RTFILE_O_TRUNCATE,     VINF_SUCCESS, RTFILEACTION_CREATED);

    /*
     * RTFILE_O_CREATE and RTFILE_O_CREATE_REPLACE.
     */
    /* RTFILE_O_CREATE_REPLACE - existing: */
    tstOpenExTest(__LINE__,  0, -1, g_szTestFile, RTFILE_O_CREATE_REPLACE,                      VINF_SUCCESS, RTFILEACTION_REPLACED);

    /* RTFILE_O_CREATE_REPLACE - non-existing: */
    tstOpenExTest(__LINE__, -1,  0, g_szTestFile, RTFILE_O_CREATE_REPLACE,                      VINF_SUCCESS, RTFILEACTION_CREATED);

    /* RTFILE_O_CREATE - existing: */
    tstOpenExTest(__LINE__,  0, -1, g_szTestFile, RTFILE_O_CREATE,                       VERR_ALREADY_EXISTS, RTFILEACTION_ALREADY_EXISTS);

    /* RTFILE_O_CREATE - non-existing: */
    tstOpenExTest(__LINE__, -1, -1, g_szTestFile, RTFILE_O_CREATE,                              VINF_SUCCESS, RTFILEACTION_CREATED);

    /*
     * RTFILE_O_CREATE and RTFILE_O_CREATE_REPLACE w/ TRUNCATE variations.
     */
    /* RTFILE_O_CREATE+TRUNCATE - non-existing: */
    tstOpenExTest(__LINE__, -1, 10, g_szTestFile, RTFILE_O_CREATE | RTFILE_O_TRUNCATE,          VINF_SUCCESS, RTFILEACTION_CREATED);

    /* RTFILE_O_CREATE+TRUNCATE - existing: */
    tstOpenExTest(__LINE__, 10, 10, g_szTestFile,  RTFILE_O_CREATE | RTFILE_O_TRUNCATE,  VERR_ALREADY_EXISTS, RTFILEACTION_ALREADY_EXISTS);

    /* RTFILE_O_CREATE_REPLACE+TRUNCATE - existing: */
    tstOpenExTest(__LINE__, 10, -1, g_szTestFile,  RTFILE_O_CREATE_REPLACE | RTFILE_O_TRUNCATE, VINF_SUCCESS, RTFILEACTION_REPLACED);

    /* RTFILE_O_CREATE_REPLACE+TRUNCATE - non-existing: */
    tstOpenExTest(__LINE__, -1, -1, g_szTestFile, RTFILE_O_CREATE_REPLACE | RTFILE_O_TRUNCATE,  VINF_SUCCESS, RTFILEACTION_CREATED);

    RTTESTI_CHECK_RC(RTFileDelete(g_szTestFile), VINF_SUCCESS);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTFileOpenEx-1", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    tstFileActionTaken(hTest);
    RTFileDelete(g_szTestFile);
    return RTTestSummaryAndDestroy(hTest);
}

