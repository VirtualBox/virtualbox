/* $Id: tstRTFileAio.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - File Async I/O.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @todo make configurable through cmd line. */
#define TSTFILEAIO_MAX_REQS_IN_FLIGHT   64
#define TSTFILEAIO_BUFFER_SIZE          (64*_1K)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest = NIL_RTTEST;


static void tstFileAioTestReadWriteBasic(RTFILE File, bool fWrite, void *pvTestBuf,
                                         size_t cbTestBuf, size_t cbTestFile, uint32_t cMaxReqsInFlight)
{
    /* Allocate request array. */
    RTFILEAIOREQ *paReqs;
    paReqs = (PRTFILEAIOREQ)RTTestGuardedAllocHead(g_hTest, cMaxReqsInFlight * sizeof(RTFILEAIOREQ));
    RTTESTI_CHECK_RETV(paReqs);
    RT_BZERO(paReqs, cMaxReqsInFlight * sizeof(RTFILEAIOREQ));

    /* Allocate array holding pointer to data buffers. */
    void **papvBuf = (void **)RTTestGuardedAllocHead(g_hTest, cMaxReqsInFlight * sizeof(void *));
    RTTESTI_CHECK_RETV(papvBuf);

    /* Allocate the buffers*/
    for (unsigned i = 0; i < cMaxReqsInFlight; i++)
    {
        RTTESTI_CHECK_RC_OK_RETV(RTTestGuardedAlloc(g_hTest, cbTestBuf, PAGE_SIZE, true /*fHead*/, &papvBuf[i]));
        if (fWrite)
            memcpy(papvBuf[i], pvTestBuf, cbTestBuf);
        if (fWrite)
            memcpy(papvBuf[i], pvTestBuf, cbTestBuf);
        else
            RT_BZERO(papvBuf[i], cbTestBuf);
    }

    /* Allocate array holding completed requests. */
    RTFILEAIOREQ *paReqsCompleted;
    paReqsCompleted = (PRTFILEAIOREQ)RTTestGuardedAllocHead(g_hTest, cMaxReqsInFlight * sizeof(RTFILEAIOREQ));
    RTTESTI_CHECK_RETV(paReqsCompleted);
    RT_BZERO(paReqsCompleted, cMaxReqsInFlight * sizeof(RTFILEAIOREQ));

    /* Create a context and associate the file handle with it. */
    RTFILEAIOCTX hAioContext;
    RTTESTI_CHECK_RC_RETV(RTFileAioCtxCreate(&hAioContext, cMaxReqsInFlight, 0 /* fFlags */), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTFileAioCtxAssociateWithFile(hAioContext, File), VINF_SUCCESS);

    /* Initialize requests. */
    for (unsigned i = 0; i < cMaxReqsInFlight; i++)
        RTFileAioReqCreate(&paReqs[i]);

    RTFOFF      off    = 0;
    int         cRuns  = 0;
    uint64_t    NanoTS = RTTimeNanoTS();
    size_t      cbLeft = cbTestFile;
    while (cbLeft)
    {
        int rc;
        int cReqs = 0;
        for (unsigned i = 0; i < cMaxReqsInFlight; i++)
        {
            size_t cbTransfer = cbLeft < cbTestBuf ? cbLeft : cbTestBuf;
            if (!cbTransfer)
                break;

            if (fWrite)
                rc = RTFileAioReqPrepareWrite(paReqs[i], File, off, papvBuf[i],
                                              cbTransfer, papvBuf[i]);
            else
                rc = RTFileAioReqPrepareRead(paReqs[i], File, off, papvBuf[i],
                                             cbTransfer, papvBuf[i]);
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

            cbLeft -= cbTransfer;
            off    += cbTransfer;
            cReqs++;
        }

        rc = RTFileAioCtxSubmit(hAioContext, paReqs, cReqs);
        RTTESTI_CHECK_MSG(rc == VINF_SUCCESS, ("Failed to submit tasks after %d runs. rc=%Rrc\n", cRuns, rc));
        if (rc != VINF_SUCCESS)
            break;

        /* Wait */
        uint32_t cCompleted = 0;
        RTTESTI_CHECK_RC(rc = RTFileAioCtxWait(hAioContext, cReqs, RT_INDEFINITE_WAIT,
                                               paReqsCompleted, cMaxReqsInFlight, &cCompleted),
                         VINF_SUCCESS);
        if (rc != VINF_SUCCESS)
            break;

        if (!fWrite)
        {
            for (uint32_t i = 0; i < cCompleted; i++)
            {
                /* Compare that we read the right stuff. */
                void *pvBuf = RTFileAioReqGetUser(paReqsCompleted[i]);
                RTTESTI_CHECK(pvBuf);

                size_t cbTransfered;
                RTTESTI_CHECK_RC(rc = RTFileAioReqGetRC(paReqsCompleted[i], &cbTransfered), VINF_SUCCESS);
                if (rc != VINF_SUCCESS)
                    break;
                RTTESTI_CHECK_MSG(cbTransfered == cbTestBuf, ("cbTransfered=%zd\n", cbTransfered));
                RTTESTI_CHECK_RC_OK(rc = (memcmp(pvBuf, pvTestBuf, cbTestBuf) == 0 ? VINF_SUCCESS : VERR_BAD_EXE_FORMAT));
                if (rc != VINF_SUCCESS)
                    break;
                memset(pvBuf, 0, cbTestBuf);
            }
        }
        cRuns++;
        if (RT_FAILURE(rc))
            break;
    }

    NanoTS = RTTimeNanoTS() - NanoTS;
    uint64_t SpeedKBs = (uint64_t)((double)cbTestFile / ((double)NanoTS / 1000000000.0) / 1024);
    RTTestValue(g_hTest, "Throughput", SpeedKBs, RTTESTUNIT_KILOBYTES_PER_SEC);

    /* cleanup */
    for (unsigned i = 0; i < cMaxReqsInFlight; i++)
        RTTestGuardedFree(g_hTest, papvBuf[i]);
    RTTestGuardedFree(g_hTest, papvBuf);
    for (unsigned i = 0; i < cMaxReqsInFlight; i++)
        RTTESTI_CHECK_RC(RTFileAioReqDestroy(paReqs[i]), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTFileAioCtxDestroy(hAioContext), VINF_SUCCESS);
    RTTestGuardedFree(g_hTest, paReqs);
}

int main()
{
    int rc = RTTestInitAndCreate("tstRTFileAio", &g_hTest);
    if (rc)
        return rc;

    /* Check if the API is available. */
    RTTestSub(g_hTest, "RTFileAioGetLimits");
    RTFILEAIOLIMITS AioLimits;
    RT_ZERO(AioLimits);
    RTTESTI_CHECK_RC(rc = RTFileAioGetLimits(&AioLimits), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTestSub(g_hTest, "Write");
        RTFILE hFile;
        RTFSTYPE enmType;
        bool fAsyncMayFail = false;
        rc = RTFsQueryType("tstFileAio#1.tst", &enmType);
        if (   RT_SUCCESS(rc)
            && enmType == RTFSTYPE_TMPFS)
            fAsyncMayFail = true;
        rc = RTFileOpen(&hFile, "tstFileAio#1.tst",
                                RTFILE_O_READWRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE | RTFILE_O_ASYNC_IO);
        RTTESTI_CHECK(   rc == VINF_SUCCESS
                      || ((rc == VERR_ACCESS_DENIED || rc == VERR_INVALID_PARAMETER) && fAsyncMayFail));
        if (RT_SUCCESS(rc))
        {
            uint8_t *pbTestBuf = (uint8_t *)RTTestGuardedAllocTail(g_hTest, TSTFILEAIO_BUFFER_SIZE);
            for (unsigned i = 0; i < TSTFILEAIO_BUFFER_SIZE; i++)
                pbTestBuf[i] = i % 256;

            uint32_t cReqsMax = AioLimits.cReqsOutstandingMax < TSTFILEAIO_MAX_REQS_IN_FLIGHT
                              ? AioLimits.cReqsOutstandingMax
                              : TSTFILEAIO_MAX_REQS_IN_FLIGHT;

            /* Basic write test. */
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Preparing test file, this can take some time and needs quite a bit of harddisk space...\n");
            tstFileAioTestReadWriteBasic(hFile, true /*fWrite*/, pbTestBuf, TSTFILEAIO_BUFFER_SIZE, 100*_1M, cReqsMax);

            /* Reopen the file before doing the next test. */
            RTTESTI_CHECK_RC(RTFileClose(hFile), VINF_SUCCESS);
            if (RTTestErrorCount(g_hTest) == 0)
            {
                RTTestSub(g_hTest, "Read/Write");
                RTTESTI_CHECK_RC(rc = RTFileOpen(&hFile, "tstFileAio#1.tst",
                                                 RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_ASYNC_IO),
                                 VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                {
                    tstFileAioTestReadWriteBasic(hFile, false /*fWrite*/, pbTestBuf, TSTFILEAIO_BUFFER_SIZE, 100*_1M, cReqsMax);
                    RTFileClose(hFile);
                }
            }

            /* Cleanup */
            RTFileDelete("tstFileAio#1.tst");
        }
        else
            RTTestSkipped(g_hTest, "rc=%Rrc", rc);
    }

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(g_hTest);
}

