/* $Id: SerialTest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SerialTest - Serial port testing utility.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/serialport.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Number of times to toggle the status lines during the test. */
#define SERIALTEST_STS_LINE_TOGGLE_COUNT 100


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Serial test mode.
 */
typedef enum SERIALTESTMODE
{
    /** Invalid mode. */
    SERIALTESTMODE_INVALID = 0,
    /** Serial port is looped back to itself */
    SERIALTESTMODE_LOOPBACK,
    /** A secondary serial port is used with a null modem cable in between. */
    SERIALTESTMODE_SECONDARY,
    /** The serial port is connected externally over which we have no control. */
    SERIALTESTMODE_EXTERNAL,
    /** Usual 32bit hack. */
    SERIALTESTMODE_32BIT_HACK = 0x7fffffff
} SERIALTESTMODE;
/** Pointer to a serial test mode. */
typedef SERIALTESTMODE *PSERIALTESTMDOE;

/** Pointer to the serial test data instance. */
typedef struct SERIALTEST *PSERIALTEST;

/**
 * Test callback function.
 *
 * @returns IPRT status code.
 * @param   pSerialTest         The serial test instance data.
 */
typedef DECLCALLBACKTYPE(int, FNSERIALTESTRUN,(PSERIALTEST pSerialTest));
/** Pointer to the serial test callback. */
typedef FNSERIALTESTRUN *PFNSERIALTESTRUN;


/**
 * The serial test instance data.
 */
typedef struct SERIALTEST
{
    /** The assigned test handle. */
    RTTEST                      hTest;
    /** The assigned serial port. */
    RTSERIALPORT                hSerialPort;
    /** The currently active config. */
    PCRTSERIALPORTCFG           pSerialCfg;
} SERIALTEST;


/**
 * Test descriptor.
 */
typedef struct SERIALTESTDESC
{
    /** Test ID. */
    const char                  *pszId;
    /** Test description. */
    const char                  *pszDesc;
    /** Test run callback. */
    PFNSERIALTESTRUN            pfnRun;
} SERIALTESTDESC;
/** Pointer to a test descriptor. */
typedef SERIALTESTDESC *PSERIALTESTDESC;
/** Pointer to a constant test descriptor. */
typedef const SERIALTESTDESC *PCSERIALTESTDESC;


/**
 * TX/RX buffer containing a simple counter.
 */
typedef struct SERIALTESTTXRXBUFCNT
{
    /** The current counter value. */
    uint32_t                    iCnt;
    /** Number of bytes left to receive/transmit. */
    size_t                      cbTxRxLeft;
    /** The offset into the buffer to receive to/send from. */
    size_t                      offBuf;
    /** Maximum size to send/receive before processing is needed again. */
    size_t                      cbTxRxMax;
    /** The data buffer. */
    uint8_t                     abBuf[_1K];
} SERIALTESTTXRXBUFCNT;
/** Pointer to a TX/RX buffer. */
typedef SERIALTESTTXRXBUFCNT *PSERIALTESTTXRXBUFCNT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


/** Command line parameters */
static const RTGETOPTDEF g_aCmdOptions[] =
{
    {"--device",           'd', RTGETOPT_REQ_STRING },
    {"--baudrate",         'b', RTGETOPT_REQ_UINT32 },
    {"--parity",           'p', RTGETOPT_REQ_STRING },
    {"--databits",         'c', RTGETOPT_REQ_UINT32 },
    {"--stopbits",         's', RTGETOPT_REQ_STRING },
    {"--mode",             'm', RTGETOPT_REQ_STRING },
    {"--secondarydevice",  'l', RTGETOPT_REQ_STRING },
    {"--tests",            't', RTGETOPT_REQ_STRING },
    {"--txbytes",          'x', RTGETOPT_REQ_UINT32 },
    {"--abort-on-error",   'a', RTGETOPT_REQ_NOTHING},
    {"--verbose",          'v', RTGETOPT_REQ_NOTHING},
    {"--help",             'h', RTGETOPT_REQ_NOTHING}
};


static DECLCALLBACK(int) serialTestRunReadWrite(PSERIALTEST pSerialTest);
static DECLCALLBACK(int) serialTestRunWrite(PSERIALTEST pSerialTest);
static DECLCALLBACK(int) serialTestRunReadVerify(PSERIALTEST pSerialTest);
static DECLCALLBACK(int) serialTestRunStsLines(PSERIALTEST pSerialTest);
static DECLCALLBACK(int) serialTestRunEcho(PSERIALTEST pSerialTest);

/** Implemented tests. */
static const SERIALTESTDESC g_aSerialTests[] =
{
    {"readwrite",  "Simple Read/Write test on the same serial port",                 serialTestRunReadWrite  },
    {"write",      "Simple write test (verification done somewhere else)",           serialTestRunWrite      },
    {"readverify", "Counterpart to write test (reads and verifies data)",            serialTestRunReadVerify },
    {"stslines",   "Testing the status line setting and receiving",                  serialTestRunStsLines   },
    {"echo",       "Echoes received data back to the sender (not real test)",        serialTestRunEcho       },
};

/** Verbosity value. */
static unsigned        g_cVerbosity           = 0;
/** The test handle. */
static RTTEST          g_hTest                = NIL_RTTEST;
/** The serial test mode. */
static SERIALTESTMODE  g_enmMode              = SERIALTESTMODE_LOOPBACK;
/** Random number generator. */
static RTRAND          g_hRand                = NIL_RTRAND;
/** The serial port handle. */
static RTSERIALPORT    g_hSerialPort          = NIL_RTSERIALPORT;
/** The loopback serial port handle if configured. */
static RTSERIALPORT    g_hSerialPortSecondary = NIL_RTSERIALPORT;
/** Number of bytes to transmit for read/write tests. */
static size_t          g_cbTx                 = _1M;
/** Flag whether to abort the tool when encountering the first error. */
static bool            g_fAbortOnError        = false;
/** The config used. */
static RTSERIALPORTCFG g_SerialPortCfg =
{
    /* uBaudRate */
    115200,
    /* enmParity */
    RTSERIALPORTPARITY_NONE,
    /* enmDataBitCount */
    RTSERIALPORTDATABITS_8BITS,
    /* enmStopBitCount */
    RTSERIALPORTSTOPBITS_ONE
};


/**
 * RTTestFailed() wrapper which aborts the program if the option is set.
 */
static void serialTestFailed(RTTEST hTest, const char *pszFmt, ...)
{
    va_list va;
    va_start(va, pszFmt);
    RTTestFailedV(hTest, pszFmt, va);
    va_end(va);
    if (g_fAbortOnError)
        RT_BREAKPOINT();
}


/**
 * Initializes a TX buffer.
 *
 * @param   pSerBuf             The serial buffer to initialize.
 * @param   cbTx                Maximum number of bytes to transmit.
 */
static void serialTestTxBufInit(PSERIALTESTTXRXBUFCNT pSerBuf, size_t cbTx)
{
    pSerBuf->iCnt      = 0;
    pSerBuf->offBuf    = 0;
    pSerBuf->cbTxRxMax = 0;
    pSerBuf->cbTxRxLeft = cbTx;
    RT_ZERO(pSerBuf->abBuf);
}


/**
 * Initializes a RX buffer.
 *
 * @param   pSerBuf             The serial buffer to initialize.
 * @param   cbRx                Maximum number of bytes to receive.
 */
static void serialTestRxBufInit(PSERIALTESTTXRXBUFCNT pSerBuf, size_t cbRx)
{
    pSerBuf->iCnt      = 0;
    pSerBuf->offBuf    = 0;
    pSerBuf->cbTxRxMax = sizeof(pSerBuf->abBuf);
    pSerBuf->cbTxRxLeft = cbRx;
    RT_ZERO(pSerBuf->abBuf);
}


/**
 * Prepares the given TX buffer with data for sending it out.
 *
 * @param   pSerBuf             The TX buffer pointer.
 */
static void serialTestTxBufPrepare(PSERIALTESTTXRXBUFCNT pSerBuf)
{
    /* Move the data to the front to make room at the end to fill. */
    if (pSerBuf->offBuf)
    {
        memmove(&pSerBuf->abBuf[0], &pSerBuf->abBuf[pSerBuf->offBuf], sizeof(pSerBuf->abBuf) - pSerBuf->offBuf);
        pSerBuf->offBuf = 0;
    }

    /* Fill up with data. */
    uint32_t offData = 0;
    while (pSerBuf->cbTxRxMax + sizeof(uint32_t) <= sizeof(pSerBuf->abBuf))
    {
        pSerBuf->iCnt++;
        *(uint32_t *)&pSerBuf->abBuf[pSerBuf->offBuf + offData] = pSerBuf->iCnt;
        pSerBuf->cbTxRxMax += sizeof(uint32_t);
        offData            += sizeof(uint32_t);
    }
}


/**
 * Sends a new batch of data from the TX buffer preapring new data if required.
 *
 * @returns IPRT status code.
 * @param   hSerialPort         The serial port handle to send the data to.
 * @param   pSerBuf             The TX buffer pointer.
 */
static int serialTestTxBufSend(RTSERIALPORT hSerialPort, PSERIALTESTTXRXBUFCNT pSerBuf)
{
    int rc = VINF_SUCCESS;

    if (pSerBuf->cbTxRxLeft)
    {
        if (!pSerBuf->cbTxRxMax)
            serialTestTxBufPrepare(pSerBuf);

        size_t cbToWrite = RT_MIN(pSerBuf->cbTxRxMax, pSerBuf->cbTxRxLeft);
        size_t cbWritten = 0;
        rc = RTSerialPortWriteNB(hSerialPort, &pSerBuf->abBuf[pSerBuf->offBuf], cbToWrite, &cbWritten);
        if (RT_SUCCESS(rc))
        {
            pSerBuf->cbTxRxMax  -= cbWritten;
            pSerBuf->offBuf     += cbWritten;
            pSerBuf->cbTxRxLeft -= cbWritten;
        }
    }

    return rc;
}


/**
 * Receives dat from the given serial port into the supplied RX buffer and does some validity checking.
 *
 * @returns IPRT status code.
 * @param   hSerialPort         The serial port handle to receive data from.
 * @param   pSerBuf             The RX buffer pointer.
 */
static int serialTestRxBufRecv(RTSERIALPORT hSerialPort, PSERIALTESTTXRXBUFCNT pSerBuf)
{
    int rc = VINF_SUCCESS;

    if (pSerBuf->cbTxRxLeft)
    {
        size_t cbToRead = RT_MIN(pSerBuf->cbTxRxMax, pSerBuf->cbTxRxLeft);
        size_t cbRead = 0;
        rc = RTSerialPortReadNB(hSerialPort, &pSerBuf->abBuf[pSerBuf->offBuf], cbToRead, &cbRead);
        if (RT_SUCCESS(rc))
        {
            pSerBuf->offBuf     += cbRead;
            pSerBuf->cbTxRxMax  -= cbRead;
            pSerBuf->cbTxRxLeft -= cbRead;
        }
    }

    return rc;
}


/**
 * Verifies the data in the given RX buffer for correct transmission.
 *
 * @returns Flag whether verification failed.
 * @param   hTest               The test handle to report errors to.
 * @param   pSerBuf             The RX buffer pointer.
 * @param   iCntTx              The current TX counter value the RX buffer should never get ahead of,
 *                              UINT32_MAX disables this check.
 */
static bool serialTestRxBufVerify(RTTEST hTest, PSERIALTESTTXRXBUFCNT pSerBuf, uint32_t iCntTx)
{
    uint32_t offRx = 0;
    bool fFailed = false;

    while (offRx + sizeof(uint32_t) < pSerBuf->offBuf)
    {
        uint32_t u32Val = *(uint32_t *)&pSerBuf->abBuf[offRx];
        offRx += sizeof(uint32_t);

        if (RT_UNLIKELY(u32Val != ++pSerBuf->iCnt))
        {
            fFailed = true;
            if (g_cVerbosity > 0)
                serialTestFailed(hTest, "Data corruption/loss detected, expected counter value %u got %u\n",
                                 pSerBuf->iCnt, u32Val);
        }
    }

    if (RT_UNLIKELY(pSerBuf->iCnt > iCntTx))
    {
        fFailed = true;
        serialTestFailed(hTest, "Overtook the send buffer, expected maximum counter value %u got %u\n",
                         iCntTx, pSerBuf->iCnt);
    }

    /* Remove processed data from the buffer and move the rest to the front. */
    if (offRx)
    {
        memmove(&pSerBuf->abBuf[0], &pSerBuf->abBuf[offRx], sizeof(pSerBuf->abBuf) - offRx);
        pSerBuf->offBuf    -= offRx;
        pSerBuf->cbTxRxMax += offRx;
    }

    return fFailed;
}


DECLINLINE(bool) serialTestRndTrue(void)
{
    return RTRandAdvU32Ex(g_hRand, 0, 1) == 1;
}


/**
 * Runs a simple read/write test.
 *
 * @returns IPRT status code.
 * @param   pSerialTest         The serial test configuration.
 */
static DECLCALLBACK(int) serialTestRunReadWrite(PSERIALTEST pSerialTest)
{
    uint64_t tsStart = RTTimeNanoTS();
    bool fFailed = false;
    SERIALTESTTXRXBUFCNT SerBufTx;
    SERIALTESTTXRXBUFCNT SerBufRx;

    serialTestTxBufInit(&SerBufTx, g_cbTx);
    serialTestRxBufInit(&SerBufRx, g_cbTx);

    int rc = serialTestTxBufSend(pSerialTest->hSerialPort, &SerBufTx);
    while (   RT_SUCCESS(rc)
           && (   SerBufTx.cbTxRxLeft
               || SerBufRx.cbTxRxLeft))
    {
        uint32_t fEvts = 0;
        uint32_t fEvtsQuery = 0;
        if (SerBufTx.cbTxRxLeft)
            fEvtsQuery |= RTSERIALPORT_EVT_F_DATA_TX;
        if (SerBufRx.cbTxRxLeft)
            fEvtsQuery |= RTSERIALPORT_EVT_F_DATA_RX;

        rc = RTSerialPortEvtPoll(pSerialTest->hSerialPort, fEvtsQuery, &fEvts, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;

        if (fEvts & RTSERIALPORT_EVT_F_DATA_RX)
        {
            rc = serialTestRxBufRecv(pSerialTest->hSerialPort, &SerBufRx);
            if (RT_FAILURE(rc))
                break;

            bool fRes = serialTestRxBufVerify(pSerialTest->hTest, &SerBufRx, SerBufTx.iCnt);
            if (fRes && !fFailed)
            {
                fFailed = true;
                serialTestFailed(pSerialTest->hTest, "Data corruption/loss detected\n");
            }
        }
        if (   RT_SUCCESS(rc)
            && (fEvts & RTSERIALPORT_EVT_F_DATA_TX))
            rc = serialTestTxBufSend(pSerialTest->hSerialPort, &SerBufTx);
    }

    uint64_t tsRuntime = RTTimeNanoTS() - tsStart;
    size_t cNsPerByte = tsRuntime / g_cbTx;
    uint64_t cbBytesPerSec = RT_NS_1SEC / cNsPerByte;
    RTTestValue(pSerialTest->hTest, "Throughput", cbBytesPerSec, RTTESTUNIT_BYTES_PER_SEC);

    return rc;
}


/**
 * Runs a simple write test without doing any verification.
 *
 * @returns IPRT status code.
 * @param   pSerialTest         The serial test configuration.
 */
static DECLCALLBACK(int) serialTestRunWrite(PSERIALTEST pSerialTest)
{
    uint64_t tsStart = RTTimeNanoTS();
    SERIALTESTTXRXBUFCNT SerBufTx;

    serialTestTxBufInit(&SerBufTx, g_cbTx);

    int rc = serialTestTxBufSend(pSerialTest->hSerialPort, &SerBufTx);
    while (   RT_SUCCESS(rc)
           && SerBufTx.cbTxRxLeft)
    {
        uint32_t fEvts = 0;

        rc = RTSerialPortEvtPoll(pSerialTest->hSerialPort, RTSERIALPORT_EVT_F_DATA_TX, &fEvts, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;

        if (fEvts & RTSERIALPORT_EVT_F_DATA_TX)
            rc = serialTestTxBufSend(pSerialTest->hSerialPort, &SerBufTx);
    }

    uint64_t tsRuntime = RTTimeNanoTS() - tsStart;
    size_t cNsPerByte = tsRuntime / g_cbTx;
    uint64_t cbBytesPerSec = RT_NS_1SEC / cNsPerByte;
    RTTestValue(pSerialTest->hTest, "Throughput", cbBytesPerSec, RTTESTUNIT_BYTES_PER_SEC);

    return rc;
}


/**
 * Runs the counterpart to the write test, reading and verifying data.
 *
 * @returns IPRT status code.
 * @param   pSerialTest         The serial test configuration.
 */
static DECLCALLBACK(int) serialTestRunReadVerify(PSERIALTEST pSerialTest)
{
    int rc = VINF_SUCCESS;
    uint64_t tsStart = RTTimeNanoTS();
    bool fFailed = false;
    SERIALTESTTXRXBUFCNT SerBufRx;

    serialTestRxBufInit(&SerBufRx, g_cbTx);

    while (   RT_SUCCESS(rc)
           && SerBufRx.cbTxRxLeft)
    {
        uint32_t fEvts = 0;
        uint32_t fEvtsQuery = RTSERIALPORT_EVT_F_DATA_RX;

        rc = RTSerialPortEvtPoll(pSerialTest->hSerialPort, fEvtsQuery, &fEvts, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;

        if (fEvts & RTSERIALPORT_EVT_F_DATA_RX)
        {
            rc = serialTestRxBufRecv(pSerialTest->hSerialPort, &SerBufRx);
            if (RT_FAILURE(rc))
                break;

            bool fRes = serialTestRxBufVerify(pSerialTest->hTest, &SerBufRx, UINT32_MAX);
            if (fRes && !fFailed)
            {
                fFailed = true;
                serialTestFailed(pSerialTest->hTest, "Data corruption/loss detected\n");
            }
        }
    }

    uint64_t tsRuntime = RTTimeNanoTS() - tsStart;
    size_t cNsPerByte = tsRuntime / g_cbTx;
    uint64_t cbBytesPerSec = RT_NS_1SEC / cNsPerByte;
    RTTestValue(pSerialTest->hTest, "Throughput", cbBytesPerSec, RTTESTUNIT_BYTES_PER_SEC);

    return rc;
}


/**
 * Tests setting status lines and getting notified about status line changes.
 *
 * @returns IPRT status code.
 * @param   pSerialTest         The serial test configuration.
 */
static DECLCALLBACK(int) serialTestRunStsLines(PSERIALTEST pSerialTest)
{
    int rc = VINF_SUCCESS;

    if (g_enmMode == SERIALTESTMODE_LOOPBACK)
    {
        uint32_t fStsLinesQueriedOld = 0;

        rc = RTSerialPortChgStatusLines(pSerialTest->hSerialPort,
                                        RTSERIALPORT_CHG_STS_LINES_F_RTS | RTSERIALPORT_CHG_STS_LINES_F_DTR,
                                        0);
        if (RT_SUCCESS(rc))
        {
            rc = RTSerialPortQueryStatusLines(pSerialTest->hSerialPort, &fStsLinesQueriedOld);
            if (RT_SUCCESS(rc))
            {
                /* Everything should be clear at this stage. */
                if (!fStsLinesQueriedOld)
                {
                    uint32_t fStsLinesSetOld = 0;

                    for (uint32_t i = 0; i < SERIALTEST_STS_LINE_TOGGLE_COUNT; i++)
                    {
                        uint32_t fStsLinesSet = 0;
                        uint32_t fStsLinesClear = 0;

                        /* Change RTS? */
                        if (serialTestRndTrue())
                        {
                            /* Clear, if set previously otherwise set it. */
                            if (fStsLinesSetOld & RTSERIALPORT_CHG_STS_LINES_F_RTS)
                                fStsLinesClear |= RTSERIALPORT_CHG_STS_LINES_F_RTS;
                            else
                                fStsLinesSet   |= RTSERIALPORT_CHG_STS_LINES_F_RTS;
                        }

                        /* Change DTR? */
                        if (serialTestRndTrue())
                        {
                            /* Clear, if set previously otherwise set it. */
                            if (fStsLinesSetOld & RTSERIALPORT_CHG_STS_LINES_F_DTR)
                                fStsLinesClear |= RTSERIALPORT_CHG_STS_LINES_F_DTR;
                            else
                                fStsLinesSet   |= RTSERIALPORT_CHG_STS_LINES_F_DTR;
                        }

                        rc = RTSerialPortChgStatusLines(pSerialTest->hSerialPort, fStsLinesClear, fStsLinesSet);
                        if (RT_FAILURE(rc))
                        {
                            serialTestFailed(g_hTest, "Changing status lines failed with %Rrc on iteration %u (fSet=%#x fClear=%#x)\n",
                                             rc, i, fStsLinesSet, fStsLinesClear);
                            break;
                        }

                        /* Wait for status line monitor event. */
                        uint32_t fEvtsRecv = 0;
                        rc = RTSerialPortEvtPoll(pSerialTest->hSerialPort, RTSERIALPORT_EVT_F_STATUS_LINE_CHANGED,
                                                 &fEvtsRecv, RT_MS_1SEC);
                        if (   RT_FAILURE(rc)
                            && (rc != VERR_TIMEOUT && !fStsLinesSet && !fStsLinesClear))
                        {
                            serialTestFailed(g_hTest, "Waiting for status line change failed with %Rrc on iteration %u\n",
                                             rc, i);
                            break;
                        }

                        uint32_t fStsLinesQueried = 0;
                        rc = RTSerialPortQueryStatusLines(pSerialTest->hSerialPort, &fStsLinesQueried);
                        if (RT_FAILURE(rc))
                        {
                            serialTestFailed(g_hTest, "Querying status lines failed with %Rrc on iteration %u\n",
                                             rc, i);
                            break;
                        }

                        /* Compare expected and real result. */
                        if (   (fStsLinesQueried & RTSERIALPORT_STS_LINE_DSR)
                            != (fStsLinesQueriedOld & RTSERIALPORT_STS_LINE_DSR))
                        {
                            if (   (fStsLinesQueried & RTSERIALPORT_STS_LINE_DSR)
                                && !(fStsLinesSet & RTSERIALPORT_CHG_STS_LINES_F_DTR))
                                serialTestFailed(g_hTest, "DSR line got set when it shouldn't be on iteration %u\n", i);
                            else if (   !(fStsLinesQueried & RTSERIALPORT_STS_LINE_DSR)
                                     && !(fStsLinesClear & RTSERIALPORT_CHG_STS_LINES_F_DTR))
                                serialTestFailed(g_hTest, "DSR line got cleared when it shouldn't be on iteration %u\n", i);
                        }
                        else if (   (fStsLinesSet & RTSERIALPORT_CHG_STS_LINES_F_DTR)
                                 || (fStsLinesClear & RTSERIALPORT_CHG_STS_LINES_F_DTR))
                                serialTestFailed(g_hTest, "DSR line didn't change when it should have on iteration %u\n", i);

                        if (   (fStsLinesQueried & RTSERIALPORT_STS_LINE_DCD)
                            != (fStsLinesQueriedOld & RTSERIALPORT_STS_LINE_DCD))
                        {
                            if (   (fStsLinesQueried & RTSERIALPORT_STS_LINE_DCD)
                                && !(fStsLinesSet & RTSERIALPORT_CHG_STS_LINES_F_DTR))
                                serialTestFailed(g_hTest, "DCD line got set when it shouldn't be on iteration %u\n", i);
                            else if (   !(fStsLinesQueried & RTSERIALPORT_STS_LINE_DCD)
                                     && !(fStsLinesClear & RTSERIALPORT_CHG_STS_LINES_F_DTR))
                                serialTestFailed(g_hTest, "DCD line got cleared when it shouldn't be on iteration %u\n", i);
                        }
                        else if (   (fStsLinesSet & RTSERIALPORT_CHG_STS_LINES_F_DTR)
                                 || (fStsLinesClear & RTSERIALPORT_CHG_STS_LINES_F_DTR))
                                serialTestFailed(g_hTest, "DCD line didn't change when it should have on iteration %u\n", i);

                        if (   (fStsLinesQueried & RTSERIALPORT_STS_LINE_CTS)
                            != (fStsLinesQueriedOld & RTSERIALPORT_STS_LINE_CTS))
                        {
                            if (   (fStsLinesQueried & RTSERIALPORT_STS_LINE_CTS)
                                && !(fStsLinesSet & RTSERIALPORT_CHG_STS_LINES_F_RTS))
                                serialTestFailed(g_hTest, "CTS line got set when it shouldn't be on iteration %u\n", i);
                            else if (   !(fStsLinesQueried & RTSERIALPORT_STS_LINE_CTS)
                                     && !(fStsLinesClear & RTSERIALPORT_CHG_STS_LINES_F_RTS))
                                serialTestFailed(g_hTest, "CTS line got cleared when it shouldn't be on iteration %u\n", i);
                        }
                        else if (   (fStsLinesSet & RTSERIALPORT_CHG_STS_LINES_F_RTS)
                                 || (fStsLinesClear & RTSERIALPORT_CHG_STS_LINES_F_RTS))
                                serialTestFailed(g_hTest, "CTS line didn't change when it should have on iteration %u\n", i);

                        if (RTTestErrorCount(g_hTest) > 0)
                            break;

                        fStsLinesSetOld |= fStsLinesSet;
                        fStsLinesSetOld &= ~fStsLinesClear;
                        fStsLinesQueriedOld = fStsLinesQueried;
                    }
                }
                else
                    serialTestFailed(g_hTest, "Status lines active which should be clear (%#x, but expected %#x)\n",
                                     fStsLinesQueriedOld, 0);
            }
            else
                serialTestFailed(g_hTest, "Querying status lines failed with %Rrc\n", rc);
        }
        else
            serialTestFailed(g_hTest, "Clearing status lines failed with %Rrc\n", rc);
    }
    else
        rc = VERR_NOT_IMPLEMENTED;

    return rc;
}


/**
 * Runs a simple echo service (not a real test on its own).
 *
 * @returns IPRT status code.
 * @param   pSerialTest         The serial test configuration.
 */
static DECLCALLBACK(int) serialTestRunEcho(PSERIALTEST pSerialTest)
{
    int rc = VINF_SUCCESS;
    uint64_t tsStart = RTTimeNanoTS();
    uint8_t abBuf[_1K];
    size_t cbLeft = g_cbTx;
    size_t cbInBuf = 0;

    while (   RT_SUCCESS(rc)
           && (   cbLeft
               || cbInBuf))
    {
        uint32_t fEvts = 0;
        uint32_t fEvtsQuery = 0;
        if (cbInBuf)
            fEvtsQuery |= RTSERIALPORT_EVT_F_DATA_TX;
        if (cbLeft && cbInBuf < sizeof(abBuf))
            fEvtsQuery |= RTSERIALPORT_EVT_F_DATA_RX;

        rc = RTSerialPortEvtPoll(pSerialTest->hSerialPort, fEvtsQuery, &fEvts, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
            break;

        if (fEvts & RTSERIALPORT_EVT_F_DATA_RX)
        {
            size_t cbThisRead = RT_MIN(cbLeft, sizeof(abBuf) - cbInBuf);
            size_t cbRead = 0;
            rc = RTSerialPortReadNB(pSerialTest->hSerialPort, &abBuf[cbInBuf], cbThisRead, &cbRead);
            if (RT_SUCCESS(rc))
            {
                cbInBuf += cbRead;
                cbLeft  -= cbRead;
            }
            else if (RT_FAILURE(rc))
                break;
        }

        if (fEvts & RTSERIALPORT_EVT_F_DATA_TX)
        {
            size_t cbWritten = 0;
            rc = RTSerialPortWriteNB(pSerialTest->hSerialPort, &abBuf[0], cbInBuf, &cbWritten);
            if (RT_SUCCESS(rc))
            {
                memmove(&abBuf[0], &abBuf[cbWritten], cbInBuf - cbWritten);
                cbInBuf -= cbWritten;
            }
        }
    }

    uint64_t tsRuntime = RTTimeNanoTS() - tsStart;
    size_t cNsPerByte = tsRuntime / g_cbTx;
    uint64_t cbBytesPerSec = RT_NS_1SEC / cNsPerByte;
    RTTestValue(pSerialTest->hTest, "Throughput", cbBytesPerSec, RTTESTUNIT_BYTES_PER_SEC);

    return rc;
}


/**
 * Returns an array of test descriptors get from the given string.
 *
 * @returns Pointer to the array of test descriptors.
 * @param   pszTests            The string containing the tests separated with ':'.
 */
static PSERIALTESTDESC serialTestSelectFromCmdLine(const char *pszTests)
{
    size_t cTests = 1;

    const char *pszNext = strchr(pszTests, ':');
    while (pszNext)
    {
        pszNext++;
        cTests++;
        pszNext = strchr(pszNext, ':');
    }

    PSERIALTESTDESC paTests = (PSERIALTESTDESC)RTMemAllocZ((cTests + 1) * sizeof(SERIALTESTDESC));
    if (RT_LIKELY(paTests))
    {
        uint32_t iTest = 0;

        pszNext = strchr(pszTests, ':');
        while (pszNext)
        {
            bool fFound = false;

            pszNext++; /* Skip : character. */

            for (unsigned i = 0; i < RT_ELEMENTS(g_aSerialTests); i++)
            {
                if (!RTStrNICmp(pszTests, g_aSerialTests[i].pszId, pszNext - pszTests - 1))
                {
                    memcpy(&paTests[iTest], &g_aSerialTests[i], sizeof(SERIALTESTDESC));
                    fFound = true;
                    break;
                }
            }

            if (RT_UNLIKELY(!fFound))
            {
                RTPrintf("Testcase \"%.*s\" not known\n", pszNext - pszTests - 1, pszTests);
                RTMemFree(paTests);
                return NULL;
            }

            pszTests = pszNext;
            pszNext = strchr(pszTests, ':');
        }

        /* Fill last descriptor. */
        bool fFound = false;
        for (unsigned i = 0; i < RT_ELEMENTS(g_aSerialTests); i++)
        {
            if (!RTStrICmp(pszTests, g_aSerialTests[i].pszId))
            {
                memcpy(&paTests[iTest], &g_aSerialTests[i], sizeof(SERIALTESTDESC));
                fFound = true;
                break;
            }
        }

        if (RT_UNLIKELY(!fFound))
        {
            RTPrintf("Testcase \"%s\" not known\n", pszTests);
            RTMemFree(paTests);
            paTests = NULL;
        }
    }
    else
        RTPrintf("Failed to allocate test descriptors for %u selected tests\n", cTests);

    return paTests;
}


/**
 * Shows tool usage text.
 */
static void serialTestUsage(PRTSTREAM pStrm)
{
    char szExec[RTPATH_MAX];
    RTStrmPrintf(pStrm, "usage: %s [options]\n",
                 RTPathFilename(RTProcGetExecutablePath(szExec, sizeof(szExec))));
    RTStrmPrintf(pStrm, "\n");
    RTStrmPrintf(pStrm, "options: \n");


    for (unsigned i = 0; i < RT_ELEMENTS(g_aCmdOptions); i++)
    {
        const char *pszHelp;
        switch (g_aCmdOptions[i].iShort)
        {
            case 'h':
                pszHelp = "Displays this help and exit";
                break;
            case 'd':
                pszHelp = "Use the specified serial port device";
                break;
            case 'b':
                pszHelp = "Use the given baudrate";
                break;
            case 'p':
                pszHelp = "Use the given parity, valid modes are: none, even, odd, mark, space";
                break;
            case 'c':
                pszHelp = "Use the given data bitcount, valid are: 5, 6, 7, 8";
                break;
            case 's':
                pszHelp = "Use the given stop bitcount, valid are: 1, 1.5, 2";
                break;
            case 'm':
                pszHelp = "Mode of the serial port, valid are: loopback, secondary, external";
                break;
            case 'l':
                pszHelp = "Use the given serial port device as the secondary device";
                break;
            case 't':
                pszHelp = "The tests to run separated by ':'";
                break;
            case 'x':
                pszHelp = "Number of bytes to transmit during read/write tests";
                break;
            default:
                pszHelp = "Option undocumented";
                break;
        }
        char szOpt[256];
        RTStrPrintf(szOpt, sizeof(szOpt), "%s, -%c", g_aCmdOptions[i].pszLong, g_aCmdOptions[i].iShort);
        RTStrmPrintf(pStrm, "  %-30s%s\n", szOpt, pszHelp);
    }
}


int main(int argc, char *argv[])
{
    /*
     * Init IPRT and globals.
     */
    int rc = RTTestInitAndCreate("SerialTest", &g_hTest);
    if (rc)
        return rc;

    /*
     * Default values.
     */
    const char *pszDevice = NULL;
    const char *pszDeviceSecondary = NULL;
    PSERIALTESTDESC paTests = NULL;

    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aCmdOptions, RT_ELEMENTS(g_aCmdOptions), 1, 0 /* fFlags */);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 'h':
                serialTestUsage(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case 'v':
                g_cVerbosity++;
                break;
            case 'd':
                pszDevice = ValueUnion.psz;
                break;
            case 'l':
                pszDeviceSecondary = ValueUnion.psz;
                break;
            case 'b':
                g_SerialPortCfg.uBaudRate = ValueUnion.u32;
                break;
            case 'p':
                if (!RTStrICmp(ValueUnion.psz, "none"))
                    g_SerialPortCfg.enmParity = RTSERIALPORTPARITY_NONE;
                else if (!RTStrICmp(ValueUnion.psz, "even"))
                    g_SerialPortCfg.enmParity = RTSERIALPORTPARITY_EVEN;
                else if (!RTStrICmp(ValueUnion.psz, "odd"))
                    g_SerialPortCfg.enmParity = RTSERIALPORTPARITY_ODD;
                else if (!RTStrICmp(ValueUnion.psz, "mark"))
                    g_SerialPortCfg.enmParity = RTSERIALPORTPARITY_MARK;
                else if (!RTStrICmp(ValueUnion.psz, "space"))
                    g_SerialPortCfg.enmParity = RTSERIALPORTPARITY_SPACE;
                else
                {
                    RTPrintf("Unknown parity \"%s\" given\n", ValueUnion.psz);
                    return RTEXITCODE_FAILURE;
                }
                break;
            case 'c':
                if (ValueUnion.u32 == 5)
                    g_SerialPortCfg.enmDataBitCount = RTSERIALPORTDATABITS_5BITS;
                else if (ValueUnion.u32 == 6)
                    g_SerialPortCfg.enmDataBitCount = RTSERIALPORTDATABITS_6BITS;
                else if (ValueUnion.u32 == 7)
                    g_SerialPortCfg.enmDataBitCount = RTSERIALPORTDATABITS_7BITS;
                else if (ValueUnion.u32 == 8)
                    g_SerialPortCfg.enmDataBitCount = RTSERIALPORTDATABITS_8BITS;
                else
                {
                    RTPrintf("Unknown data bitcount \"%u\" given\n", ValueUnion.u32);
                    return RTEXITCODE_FAILURE;
                }
                break;
            case 's':
                if (!RTStrICmp(ValueUnion.psz, "1"))
                    g_SerialPortCfg.enmStopBitCount = RTSERIALPORTSTOPBITS_ONE;
                else if (!RTStrICmp(ValueUnion.psz, "1.5"))
                    g_SerialPortCfg.enmStopBitCount = RTSERIALPORTSTOPBITS_ONEPOINTFIVE;
                else if (!RTStrICmp(ValueUnion.psz, "2"))
                    g_SerialPortCfg.enmStopBitCount = RTSERIALPORTSTOPBITS_TWO;
                else
                {
                    RTPrintf("Unknown stop bitcount \"%s\" given\n", ValueUnion.psz);
                    return RTEXITCODE_FAILURE;
                }
                break;
            case 'm':
                if (!RTStrICmp(ValueUnion.psz, "loopback"))
                    g_enmMode = SERIALTESTMODE_LOOPBACK;
                else if (!RTStrICmp(ValueUnion.psz, "secondary"))
                    g_enmMode = SERIALTESTMODE_SECONDARY;
                else if (!RTStrICmp(ValueUnion.psz, "external"))
                    g_enmMode = SERIALTESTMODE_EXTERNAL;
                else
                {
                    RTPrintf("Unknown serial test mode \"%s\" given\n", ValueUnion.psz);
                    return RTEXITCODE_FAILURE;
                }
                break;
            case 't':
                paTests = serialTestSelectFromCmdLine(ValueUnion.psz);
                if (!paTests)
                    return RTEXITCODE_FAILURE;
                break;
            case 'x':
                g_cbTx = ValueUnion.u32;
                break;
            case 'a':
                g_fAbortOnError = true;
                break;
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    if (g_enmMode == SERIALTESTMODE_SECONDARY && !pszDeviceSecondary)
    {
        RTPrintf("Mode set to secondary device but no secondary device given\n");
        return RTEXITCODE_FAILURE;
    }

    if (!paTests)
    {
        /* Select all. */
        paTests = (PSERIALTESTDESC)RTMemAllocZ((RT_ELEMENTS(g_aSerialTests) + 1) * sizeof(SERIALTESTDESC));
        if (RT_UNLIKELY(!paTests))
        {
            RTPrintf("Failed to allocate memory for test descriptors\n");
            return RTEXITCODE_FAILURE;
        }
        memcpy(paTests, &g_aSerialTests[0], RT_ELEMENTS(g_aSerialTests) * sizeof(SERIALTESTDESC));
    }

    rc = RTRandAdvCreateParkMiller(&g_hRand);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Failed to create random number generator: %Rrc\n", rc);
        return RTEXITCODE_FAILURE;
    }

    rc = RTRandAdvSeed(g_hRand, UINT64_C(0x123456789abcdef));
    AssertRC(rc);

    /*
     * Start testing.
     */
    RTTestBanner(g_hTest);

    if (pszDevice)
    {
        uint32_t fFlags =   RTSERIALPORT_OPEN_F_READ
                          | RTSERIALPORT_OPEN_F_WRITE
                          | RTSERIALPORT_OPEN_F_SUPPORT_STATUS_LINE_MONITORING;

        RTTestSub(g_hTest, "Opening device");
        rc = RTSerialPortOpen(&g_hSerialPort, pszDevice, fFlags);
        if (RT_SUCCESS(rc))
        {
            if (g_enmMode == SERIALTESTMODE_SECONDARY)
            {
                RTTestSub(g_hTest, "Opening secondary device");
                rc = RTSerialPortOpen(&g_hSerialPortSecondary, pszDeviceSecondary, fFlags);
                if (RT_FAILURE(rc))
                    serialTestFailed(g_hTest, "Opening secondary device \"%s\" failed with %Rrc\n", pszDevice, rc);
            }

            if (RT_SUCCESS(rc))
            {
                RTTestSub(g_hTest, "Setting serial port configuration");

                rc = RTSerialPortCfgSet(g_hSerialPort, &g_SerialPortCfg ,NULL);
                if (RT_SUCCESS(rc))
                {
                    if (g_enmMode == SERIALTESTMODE_SECONDARY)
                    {
                        RTTestSub(g_hTest, "Setting serial port configuration for secondary device");
                        rc = RTSerialPortCfgSet(g_hSerialPortSecondary, &g_SerialPortCfg, NULL);
                        if (RT_FAILURE(rc))
                            serialTestFailed(g_hTest, "Setting configuration of secondary device \"%s\" failed with %Rrc\n", pszDevice, rc);
                    }

                    if (RT_SUCCESS(rc))
                    {
                        SERIALTEST Test;
                        PSERIALTESTDESC pTest = &paTests[0];

                        Test.hTest       = g_hTest;
                        Test.hSerialPort = g_hSerialPort;
                        Test.pSerialCfg  = &g_SerialPortCfg;

                        while (pTest->pszId)
                        {
                            RTTestSub(g_hTest, pTest->pszDesc);
                            rc = pTest->pfnRun(&Test);
                            if (   RT_FAILURE(rc)
                                || RTTestErrorCount(g_hTest) > 0)
                                serialTestFailed(g_hTest, "Running test \"%s\" failed (%Rrc, cErrors=%u)\n",
                                                 pTest->pszId, rc, RTTestErrorCount(g_hTest));

                            RTTestSubDone(g_hTest);
                            pTest++;
                        }
                    }
                }
                else
                    serialTestFailed(g_hTest, "Setting configuration of device \"%s\" failed with %Rrc\n", pszDevice, rc);

                RTSerialPortClose(g_hSerialPort);
            }
        }
        else
            serialTestFailed(g_hTest, "Opening device \"%s\" failed with %Rrc\n", pszDevice, rc);
    }
    else
        serialTestFailed(g_hTest, "No device given on command line\n");

    RTRandAdvDestroy(g_hRand);
    RTMemFree(paTests);
    RTEXITCODE rcExit = RTTestSummaryAndDestroy(g_hTest);
    return rcExit;
}

