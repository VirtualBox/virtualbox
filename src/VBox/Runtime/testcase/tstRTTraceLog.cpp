/* $Id: tstRTTraceLog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTTraceLog.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/tracelog.h>

#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>


/**
 * Trace log buffer.
 */
typedef struct TSTRTTRACELOGBUF
{
    /** Size of the buffer. */
    size_t              cbBuf;
    /** Current write offset. */
    size_t              offBuf;
    /** Streamed data - variable in size. */
    uint8_t             abBuf[1];
} TSTRTTRACELOGBUF;
/** Pointer to a trace log buffer. */
typedef TSTRTTRACELOGBUF *PTSTRTTRACELOGBUF;


/**
 * Structure matching the event descriptor.
 */
typedef struct RTTESTTRACELOGEVTDATA
{
    /** Test pointer. */
    uintptr_t                   ptr;
    /** Test size_t value. */
    size_t                      sz;
    /** Test 32bit value. */
    uint32_t                    u32;
    /** Test boolean. */
    bool                        f;
    /** Test raw data. */
    uint8_t                     abRaw[42];
} RTTESTTRACELOGEVTDATA;
/** Pointer to event data. */
typedef RTTESTTRACELOGEVTDATA *PRTTESTTRACELOGEVTDATA;


/**
 * Test event item descriptor.
 */
static RTTRACELOGEVTITEMDESC g_EvtItemDesc[] =
{
    {"TestPtr",       NULL,                         RTTRACELOGTYPE_POINTER,  0},
    {"TestSz",        NULL,                         RTTRACELOGTYPE_SIZE,     0},
    {"TestU32",       NULL,                         RTTRACELOGTYPE_UINT32,   0},
    {"TestBool",      "This is a test description", RTTRACELOGTYPE_BOOL,     0},
    {"TestRawStatic", NULL,                         RTTRACELOGTYPE_RAWDATA, 42}
};


/**
 * Test event descriptor.
 */
static RTTRACELOGEVTDESC g_EvtDesc =
{
    "idTest",
    "This is a test event",
    RTTRACELOGEVTSEVERITY_INFO,
    RT_ELEMENTS(g_EvtItemDesc),
    g_EvtItemDesc
};



/**
 * Allocates a new buffer for the raw trace log stream.
 *
 * @returns IPRT status code.
 * @param   cbBuf               Size of the buffer in bytes.
 * @param   ppBuf               Where to store the pointer to the buffer on success.
 */
static int tstRTTraceLogBufAlloc(size_t cbBuf, PTSTRTTRACELOGBUF *ppBuf)
{
    PTSTRTTRACELOGBUF pBuf = (PTSTRTTRACELOGBUF)RTMemAllocZ(RT_UOFFSETOF_DYN(TSTRTTRACELOGBUF, abBuf[cbBuf]));
    if (RT_LIKELY(pBuf))
    {
        pBuf->cbBuf  = cbBuf;
        pBuf->offBuf = 0;
        *ppBuf = pBuf;
        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}


/**
 * @copydoc{FNRTTRACELOGWRSTREAM}
 */
static DECLCALLBACK(int) tstRTTraceLogStreamOut(void *pvUser, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    PTSTRTTRACELOGBUF pBuf = (PTSTRTTRACELOGBUF)pvUser;

    if (pBuf)
    {
        size_t cbWrite = RT_MIN(cbBuf, pBuf->cbBuf - pBuf->offBuf);
        if (   cbWrite != 0
            && (   cbWrite >= cbBuf
                || pcbWritten))
        {
            memcpy(&pBuf->abBuf[pBuf->offBuf], pvBuf, cbWrite);
            pBuf->offBuf += cbWrite;
            if (pcbWritten)
                *pcbWritten = cbWrite;
            return VINF_SUCCESS;
        }

        return VERR_DISK_FULL;
    }

    /* Benchmark mode, forget everything immediately. */
    return VINF_SUCCESS;
}


/**
 * @copydoc{FNRTTRACELOGRDRSTREAM}
 */
static DECLCALLBACK(int) tstRTTraceLogStreamIn(void *pvUser, void *pvBuf, size_t cbBuf, size_t *pcbRead,
                                               RTMSINTERVAL cMsTimeout)
{
    RT_NOREF(cMsTimeout);
    PTSTRTTRACELOGBUF pBuf = (PTSTRTTRACELOGBUF)pvUser;

    size_t cbRead = RT_MIN(cbBuf, pBuf->cbBuf - pBuf->offBuf);
    if (   cbRead != 0
        && (   cbRead >= cbBuf
            || pcbRead))
    {
        memcpy(pvBuf, &pBuf->abBuf[pBuf->offBuf], cbRead);
        pBuf->offBuf += cbRead;
        if (pcbRead)
            *pcbRead = cbRead;
        return VINF_SUCCESS;
    }

    return VERR_EOF;
}


/**
 * @copydoc{FNRTTRACELOGSTREAMCLOSE}
 */
static DECLCALLBACK(int) tstRTTraceLogStreamClose(void *pvUser)
{
    RT_NOREF(pvUser);
    return VINF_SUCCESS;
}


static PTSTRTTRACELOGBUF tstRTTraceLogWriter(void)
{
    RTTRACELOGWR hTraceLogWr = NIL_RTTRACELOGWR;
    PTSTRTTRACELOGBUF pLogBuf = NULL;
    RTTESTTRACELOGEVTDATA EvtData;

    EvtData.ptr = (uintptr_t)&EvtData;
    EvtData.sz  = 0xdeadcafe;
    EvtData.u32 = 0;
    EvtData.f   = true;
    memset(&EvtData.abRaw[0], 0x42, sizeof(EvtData.abRaw));

    /*
     * Bad set pointer and handle values.
     */
    RTTestSub(NIL_RTTEST, "Writer");
    RTTESTI_CHECK_RC(RTTraceLogWrCreate(NULL, NULL, NULL, NULL, NULL), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(RTTraceLogWrCreate(&hTraceLogWr, NULL, NULL, NULL, NULL), VERR_INVALID_POINTER);
    RTTRACELOGWR hTraceLogWrInvl = (RTTRACELOGWR)(intptr_t)-3;
    RTTESTI_CHECK_RC(RTTraceLogWrDestroy(hTraceLogWrInvl), VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(RTTraceLogWrAddEvtDesc(hTraceLogWr, NULL), VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(RTTraceLogWrEvtAdd(hTraceLogWr, NULL, 0, 0, 0, NULL, NULL), VERR_INVALID_HANDLE);

    RTTESTI_CHECK_RC_RET(tstRTTraceLogBufAlloc(_4K, &pLogBuf), VINF_SUCCESS, NULL);
    RTTESTI_CHECK_RC_RET(RTTraceLogWrCreate(&hTraceLogWr, NULL, tstRTTraceLogStreamOut,
                                            tstRTTraceLogStreamClose, pLogBuf), VINF_SUCCESS, NULL);
    RTTESTI_CHECK_RC_RET(RTTraceLogWrAddEvtDesc(hTraceLogWr, &g_EvtDesc), VINF_SUCCESS, NULL);
    RTTESTI_CHECK_RC_RET(RTTraceLogWrAddEvtDesc(hTraceLogWr, &g_EvtDesc), VERR_ALREADY_EXISTS, NULL);
    RTTESTI_CHECK_RC_RET(RTTraceLogWrEvtAdd(hTraceLogWr, &g_EvtDesc, 0, 0, 0, &EvtData, NULL), VINF_SUCCESS, NULL);
    RTTESTI_CHECK_RC_RET(RTTraceLogWrDestroy(hTraceLogWr), VINF_SUCCESS, NULL);

    return pLogBuf;
}


static void tstRTTraceLogWriterBenchmark(void)
{
    RTTRACELOGWR hTraceLogWr = NIL_RTTRACELOGWR;
    RTTESTTRACELOGEVTDATA EvtData;

    EvtData.ptr = (uintptr_t)&EvtData;
    EvtData.sz  = 0xdeadcafe;
    EvtData.u32 = 0;
    EvtData.f   = true;
    memset(&EvtData.abRaw[0], 0x42, sizeof(EvtData.abRaw));

    RTTestSub(NIL_RTTEST, "Writer Benchmark");
    RTTESTI_CHECK_RC_RETV(RTTraceLogWrCreate(&hTraceLogWr, NULL, tstRTTraceLogStreamOut,
                                             tstRTTraceLogStreamClose, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTTraceLogWrAddEvtDesc(hTraceLogWr, &g_EvtDesc), VINF_SUCCESS);

    uint64_t tsStart = RTTimeNanoTS();
    for (uint32_t i = 0; i < 1000000; i++)
    {
        RTTESTI_CHECK_RC_BREAK(RTTraceLogWrEvtAdd(hTraceLogWr, &g_EvtDesc, 0, 0, 0, &EvtData, NULL), VINF_SUCCESS);
    }
    uint64_t tsRuntime = RTTimeNanoTS() - tsStart;
    RTTestValue(NIL_RTTEST, "RTTraceLogWrEvtAdd()", tsRuntime / 1000000, RTTESTUNIT_NS_PER_CALL);
    RTTESTI_CHECK_RC(RTTraceLogWrDestroy(hTraceLogWr), VINF_SUCCESS);
}

static void tstRTTraceLogReader(PTSTRTTRACELOGBUF pLogBuf)
{
    RTTRACELOGRDRPOLLEVT enmEvt = RTTRACELOGRDRPOLLEVT_INVALID;
    RTTRACELOGRDR hTraceLogRdr = NIL_RTTRACELOGRDR;

    RTTestSub(NIL_RTTEST, "Reader");

    /*
     * Bad set pointer and handle values.
     */
    RTTESTI_CHECK_RC(RTTraceLogRdrCreate(NULL, NULL, NULL, NULL), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(RTTraceLogRdrCreate(&hTraceLogRdr, NULL, NULL, NULL), VERR_INVALID_POINTER);
    RTTRACELOGRDR hTraceLogRdrInvl = (RTTRACELOGRDR)(intptr_t)-3;
    RTTESTI_CHECK_RC(RTTraceLogRdrDestroy(hTraceLogRdrInvl), VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(RTTraceLogRdrEvtPoll(hTraceLogRdrInvl, NULL, RT_INDEFINITE_WAIT), VERR_INVALID_HANDLE);

    /*
     * Test with log buffer created previously.
     */
    RTTESTI_CHECK_RC_RETV(RTTraceLogRdrCreate(&hTraceLogRdr, tstRTTraceLogStreamIn, tstRTTraceLogStreamClose, pLogBuf),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTTraceLogRdrEvtPoll(hTraceLogRdr, &enmEvt, RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(enmEvt == RTTRACELOGRDRPOLLEVT_HDR_RECVD);
    RTTESTI_CHECK_RC_RETV(RTTraceLogRdrEvtPoll(hTraceLogRdr, &enmEvt, RT_INDEFINITE_WAIT), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(enmEvt == RTTRACELOGRDRPOLLEVT_TRACE_EVENT_RECVD);
    RTTESTI_CHECK_RC_RETV(RTTraceLogRdrDestroy(hTraceLogRdr), VINF_SUCCESS);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTTraceLog", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * The tests.
     */
    bool fMayPanic = RTAssertMayPanic();
    bool fQuiet    = RTAssertAreQuiet();
    RTAssertSetMayPanic(false);
    RTAssertSetQuiet(true);
    PTSTRTTRACELOGBUF pLogBuf = tstRTTraceLogWriter();
    if (RTTestErrorCount(hTest) == 0)
    {
        pLogBuf->offBuf = 0;
        tstRTTraceLogReader(pLogBuf);
    }
    RTMemFree(pLogBuf);
    tstRTTraceLogWriterBenchmark();
    RTAssertSetQuiet(fQuiet);
    RTAssertSetMayPanic(fMayPanic);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

