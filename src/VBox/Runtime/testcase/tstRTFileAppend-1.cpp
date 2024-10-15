/* $Id: tstRTFileAppend-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - File Appending.
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
#include <iprt/file.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>


static void tstFileAppend1(RTTEST hTest)
{
    /*
     * Open it write only and do some appending.
     * Checking that read fails and that the file position changes after the write.
     */
    RTTestSub(hTest, "Basic 1");
    RTFileDelete("tstFileAppend-1.tst");
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile,
                        "tstFileAppend-1.tst",
                        RTFILE_O_WRITE
                        | RTFILE_O_APPEND
                        | RTFILE_O_OPEN_CREATE
                        | RTFILE_O_DENY_NONE
                        | (0644 << RTFILE_O_CREATE_MODE_SHIFT)
                        );
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    uint64_t offActual = 42;
    uint64_t off       = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 0 || RT_FAILURE(rc), ("offActual=%llu", offActual));

    RTTESTI_CHECK_RC(RTFileWrite(hFile, "0123456789", 10, NULL), VINF_SUCCESS);

    offActual = 99;
    off = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 10 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    RTTestIPrintf(RTTESTLVL_INFO, "off=%llu after first write\n", offActual);

    size_t  cb = 4;
    char    szBuf[256];
    rc = RTFileRead(hFile, szBuf, 1, &cb);
    RTTESTI_CHECK_MSG(rc == VERR_ACCESS_DENIED || rc == VERR_INVALID_HANDLE, ("rc=%Rrc\n", rc));

    offActual = 999;
    off = 5;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_BEGIN, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 5 || RT_FAILURE(rc), ("offActual=%llu", offActual));

    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);


    /*
     * Open it write only and do some more appending.
     * Checking the initial position and that it changes after the write.
     */
    RTTestSub(hTest, "Basic 2");
    rc = RTFileOpen(&hFile,
                    "tstFileAppend-1.tst",
                      RTFILE_O_WRITE
                    | RTFILE_O_APPEND
                    | RTFILE_O_OPEN
                    | RTFILE_O_DENY_NONE
                   );
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    offActual = 99;
    off = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 0 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    RTTestIPrintf(RTTESTLVL_INFO, "off=%llu on 2nd open\n", offActual);

    RTTESTI_CHECK_RC(rc = RTFileWrite(hFile, "abcdefghij", 10, &cb), VINF_SUCCESS);

    offActual = 999;
    off = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 20 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    RTTestIPrintf(RTTESTLVL_INFO, "off=%llu after 2nd write\n", offActual);

    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);

    /*
     * Open it read/write.
     * Check the initial position and read stuff. Then append some more and
     * check the new position and see that read returns 0/EOF. Finally,
     * do some seeking and read from a new position.
     */
    RTTestSub(hTest, "Basic 3");
    rc = RTFileOpen(&hFile,
                    "tstFileAppend-1.tst",
                      RTFILE_O_READWRITE
                    | RTFILE_O_APPEND
                    | RTFILE_O_OPEN
                    | RTFILE_O_DENY_NONE
                   );
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    offActual = 9;
    off = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 0 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    RTTestIPrintf(RTTESTLVL_INFO, "off=%llu on 3rd open\n", offActual);

    cb = 99;
    RTTESTI_CHECK_RC(rc = RTFileRead(hFile, szBuf, 10, &cb), VINF_SUCCESS);
    RTTESTI_CHECK(RT_FAILURE(rc) || cb == 10);
    RTTESTI_CHECK_MSG(RT_FAILURE(rc) || !memcmp(szBuf, "0123456789", 10), ("read the wrong stuff: %.10s - expected 0123456789\n", szBuf));

    offActual = 999;
    off = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 10 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    RTTestIPrintf(RTTESTLVL_INFO, "off=%llu on 1st open\n", offActual);

    RTTESTI_CHECK_RC(RTFileWrite(hFile, "klmnopqrst", 10, NULL), VINF_SUCCESS);

    offActual = 9999;
    off = 0;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 30 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    RTTestIPrintf(RTTESTLVL_INFO, "off=%llu after 3rd write\n", offActual);

    RTTESTI_CHECK_RC(rc = RTFileRead(hFile, szBuf, 1, NULL), VERR_EOF);
    cb = 99;
    RTTESTI_CHECK_RC(rc = RTFileRead(hFile, szBuf, 1, &cb), VINF_SUCCESS);
    RTTESTI_CHECK(cb == 0);


    offActual = 99999;
    off = 15;
    RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_BEGIN, &offActual), VINF_SUCCESS);
    RTTESTI_CHECK_MSG(offActual == 15 || RT_FAILURE(rc), ("offActual=%llu", offActual));
    if (RT_SUCCESS(rc) && offActual == 15)
    {
        RTTESTI_CHECK_RC(rc = RTFileRead(hFile, szBuf, 10, NULL), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(RT_FAILURE(rc) || !memcmp(szBuf, "fghijklmno", 10), ("read the wrong stuff: %.10s - expected fghijklmno\n", szBuf));

        offActual = 9999999;
        off = 0;
        RTTESTI_CHECK_RC(rc = RTFileSeek(hFile, off, RTFILE_SEEK_CURRENT, &offActual), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(offActual == 25 || RT_FAILURE(rc), ("offActual=%llu", offActual));
        RTTestIPrintf(RTTESTLVL_INFO, "off=%llu after 2nd read\n", offActual);
    }

    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);

    /*
     * Open it read only + append and check that we cannot write to it.
     */
    RTTestSub(hTest, "Basic 4");
    rc = RTFileOpen(&hFile,
                    "tstFileAppend-1.tst",
                      RTFILE_O_READ
                    | RTFILE_O_APPEND
                    | RTFILE_O_OPEN
                    | RTFILE_O_DENY_NONE);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    rc = RTFileWrite(hFile, "pqrstuvwx", 10, &cb);
    RTTESTI_CHECK_MSG(rc == VERR_ACCESS_DENIED || rc == VERR_INVALID_HANDLE, ("rc=%Rrc\n", rc));

    RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTFileDelete("tstFileAppend-1.tst"), VINF_SUCCESS);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTFileAppend-1", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    tstFileAppend1(hTest);
    RTFileDelete("tstFileAppend-1.tst");
    return RTTestSummaryAndDestroy(hTest);
}

