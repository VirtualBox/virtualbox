/* $Id: tstRTLocalIpc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTLocalIpc API.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/localipc.h>

#include <iprt/asm.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test instance.*/
static RTTEST g_hTest;



static void testBasics(void)
{
    RTTestISub("Basics");

    /* Server-side. */
    RTTESTI_CHECK_RC(RTLocalIpcServerCreate(NULL, NULL, 0), VERR_INVALID_POINTER);
    RTLOCALIPCSERVER hIpcServer;
    int rc;
    RTTESTI_CHECK_RC(rc = RTLocalIpcServerCreate(&hIpcServer, NULL, 0), VERR_INVALID_POINTER);
    if (RT_SUCCESS(rc)) RTLocalIpcServerDestroy(hIpcServer);
    RTTESTI_CHECK_RC(rc = RTLocalIpcServerCreate(&hIpcServer, "", 0), VERR_INVALID_NAME);
    if (RT_SUCCESS(rc)) RTLocalIpcServerDestroy(hIpcServer);
    RTTESTI_CHECK_RC(rc = RTLocalIpcServerCreate(&hIpcServer, "BasicTest", 1234 /* Invalid flags */), VERR_INVALID_FLAGS);
    if (RT_SUCCESS(rc)) RTLocalIpcServerDestroy(hIpcServer);

    RTTESTI_CHECK_RC(RTLocalIpcServerCancel(NULL), VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(NULL), VINF_SUCCESS);

    /* Basic server creation / destruction. */
    RTTESTI_CHECK_RC_RETV(RTLocalIpcServerCreate(&hIpcServer, "BasicTest", 0), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTLocalIpcServerCancel(hIpcServer), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(hIpcServer), VINF_OBJECT_DESTROYED);

    /* Client-side (per session). */
    RTTESTI_CHECK_RC(RTLocalIpcSessionConnect(NULL, NULL, 0), VERR_INVALID_POINTER);
    RTLOCALIPCSESSION hIpcSession;
    RTTESTI_CHECK_RC(RTLocalIpcSessionConnect(&hIpcSession, NULL, 0), VERR_INVALID_POINTER);
    if (RT_SUCCESS(rc)) RTLocalIpcSessionClose(hIpcSession);
    RTTESTI_CHECK_RC(RTLocalIpcSessionConnect(&hIpcSession, "", 0), VERR_INVALID_NAME);
    if (RT_SUCCESS(rc)) RTLocalIpcSessionClose(hIpcSession);
    RTTESTI_CHECK_RC(RTLocalIpcSessionConnect(&hIpcSession, "BasicTest", 1234 /* Invalid flags */), VERR_INVALID_FLAGS);
    if (RT_SUCCESS(rc)) RTLocalIpcSessionClose(hIpcSession);

    RTTESTI_CHECK_RC(RTLocalIpcSessionCancel(NULL), VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(RTLocalIpcSessionClose(NULL), VINF_SUCCESS);

    /* Basic client creation / destruction. */
    RTTESTI_CHECK_RC_RETV(rc = RTLocalIpcSessionConnect(&hIpcSession, "BasicTest", 0), VERR_FILE_NOT_FOUND);
    if (RT_SUCCESS(rc)) RTLocalIpcSessionClose(hIpcSession);
    //RTTESTI_CHECK_RC(RTLocalIpcServerCancel(hIpcServer), VERR_INVALID_HANDLE);  - accessing freed memory, bad idea.
    //RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(hIpcServer), VERR_INVALID_HANDLE); - accessing freed memory, bad idea.
}



/*********************************************************************************************************************************
*                                                                                                                                *
*   testSessionConnection - Connecting.                                                                                          *
*                                                                                                                                *
*********************************************************************************************************************************/

static DECLCALLBACK(int) testServerListenThread(RTTHREAD hSelf, void *pvUser)
{
    RTLOCALIPCSERVER hIpcServer = (RTLOCALIPCSERVER)pvUser;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);

    RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));

    int rc;
    for (;;)
    {
        RTLOCALIPCSESSION hIpcSession;
        rc = RTLocalIpcServerListen(hIpcServer, &hIpcSession);
        if (RT_SUCCESS(rc))
        {
            RTThreadSleep(8); /* windows output fudge (purely esthetical) */
            RTTestIPrintf(RTTESTLVL_INFO, "testServerListenThread: Got new client connection.\n");
            RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hIpcSession), VINF_OBJECT_DESTROYED);
        }
        else
        {
            RTTESTI_CHECK_RC(rc, VERR_CANCELLED);
            break;
        }
    }
    return rc;
}


/**
 * Used both as a thread procedure and child process worker.
 */
static DECLCALLBACK(int) tstRTLocalIpcSessionConnectionChild(RTTHREAD hSelf, void *pvUser)
{
    RTLOCALIPCSESSION hClientSession;
    RT_NOREF_PV(hSelf); RT_NOREF_PV(pvUser);

    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);

    RTTEST_CHECK_RC_RET(g_hTest, RTLocalIpcSessionConnect(&hClientSession, "tstRTLocalIpcSessionConnection",0 /* Flags */),
                        VINF_SUCCESS, rcCheck);
    RTTEST_CHECK_RC_RET(g_hTest, RTLocalIpcSessionClose(hClientSession),
                        VINF_OBJECT_DESTROYED, rcCheck);

    return VINF_SUCCESS;
}


static void testSessionConnection(const char *pszExecPath)
{
    RTTestISub(!pszExecPath ? "Connect from thread" : "Connect from child");

    /*
     * Create the test server.
     */
    RTLOCALIPCSERVER hIpcServer;
    RTTESTI_CHECK_RC_RETV(RTLocalIpcServerCreate(&hIpcServer, "tstRTLocalIpcSessionConnection", 0), VINF_SUCCESS);

    /*
     * Create worker thread that listens and closes incoming connections until
     * cancelled.
     */
    int rc;
    RTTHREAD hListenThread;
    RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hListenThread, testServerListenThread, hIpcServer, 0 /* Stack */,
                                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "listen-1"));
    if (RT_SUCCESS(rc))
    {
        RTThreadUserWait(hListenThread, 32);

        /*
         * Two variations here: Client connects from thread or a child process.
         */
        if (pszExecPath)
        {
            RTPROCESS hClientProc;
            const char *apszArgs[4] = { pszExecPath, "child", "tstRTLocalIpcSessionConnectionChild", NULL };
            RTTESTI_CHECK_RC_OK(rc = RTProcCreate(pszExecPath, apszArgs, RTENV_DEFAULT, 0 /* fFlags*/, &hClientProc));
            if (RT_SUCCESS(rc))
            {
                RTPROCSTATUS ProcStatus;
                RTTESTI_CHECK_RC_OK(rc = RTProcWait(hClientProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus));
                if (RT_SUCCESS(rc) && (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0))
                    RTTestIFailed("Chiled exited with enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
            }
        }
        else
        {
            RTTHREAD hClientThread;
            RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hClientThread, tstRTLocalIpcSessionConnectionChild, NULL,
                                                    0 /* Stack */, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "client-1"));
            if (RT_SUCCESS(rc))
            {
                int rcThread;
                RTTESTI_CHECK_RC_OK(rc = RTThreadWait(hClientThread, RT_MS_1MIN / 2, &rcThread));
                if (RT_SUCCESS(rc))
                    RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);
            }
        }


        /*
         * Terminate the server thread.
         */
        //RTTestIPrintf(RTTESTLVL_INFO, "Child terminated, waiting for server thread ...\n");
        RTTESTI_CHECK_RC(RTLocalIpcServerCancel(hIpcServer), VINF_SUCCESS);
        int rcThread;
        RTTESTI_CHECK_RC(rc = RTThreadWait(hListenThread, 30 * 1000 /* 30s timeout */, &rcThread), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC(rcThread, VERR_CANCELLED);
    }

    RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(hIpcServer), VINF_OBJECT_DESTROYED);
}



/*********************************************************************************************************************************
*                                                                                                                                *
*   testSessionWait - RTLocalIpcSessionWaitForData.                                                                              *
*                                                                                                                                *
*********************************************************************************************************************************/

static DECLCALLBACK(int) testSessionWaitThread(RTTHREAD hSelf, void *pvUser)
{
    RTLOCALIPCSERVER hIpcServer = (RTLOCALIPCSERVER)pvUser;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);

    int rc;
    for (;;)
    {
        RTLOCALIPCSESSION hIpcSession;
        rc = RTLocalIpcServerListen(hIpcServer, &hIpcSession);
        if (RT_SUCCESS(rc))
        {
            RTTestIPrintf(RTTESTLVL_INFO, "testSessionWaitThread: Got new client connection.\n");

            /* Wait for the client to trigger a disconnect by writing us something. */
            RTTESTI_CHECK_RC(RTLocalIpcSessionWaitForData(hIpcSession, RT_MS_1MIN), VINF_SUCCESS);

            size_t cbRead;
            char szCmd[64];
            RT_ZERO(szCmd);
            RTTESTI_CHECK_RC(rc = RTLocalIpcSessionReadNB(hIpcSession, szCmd, sizeof(szCmd) - 1, &cbRead), VINF_SUCCESS);
            if (RT_SUCCESS(rc) && (cbRead != sizeof("disconnect") - 1 || strcmp(szCmd, "disconnect")) )
                RTTestIFailed("cbRead=%zu, expected %zu; szCmd='%s', expected 'disconnect'\n",
                              cbRead, sizeof("disconnect") - 1, szCmd);

            RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hIpcSession), VINF_OBJECT_DESTROYED);
            RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));
        }
        else
        {
            RTTESTI_CHECK_RC(rc, VERR_CANCELLED);
            break;
        }
    }
    RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));
    return rc;
}


/**
 * Used both as a thread procedure and child process worker.
 */
static DECLCALLBACK(int) tstRTLocalIpcSessionWaitChild(RTTHREAD hSelf, void *pvUser)
{
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);
    RT_NOREF_PV(hSelf); RT_NOREF_PV(pvUser);

    RTLOCALIPCSESSION hClientSession;
    RTTESTI_CHECK_RC_RET(RTLocalIpcSessionConnect(&hClientSession, "tstRTLocalIpcSessionWait", 0 /*fFlags*/),
                         VINF_SUCCESS, rcCheck);

    /*
     * The server side won't write anything.  It will close the connection
     * as soon as we write something.
     */
    RTTESTI_CHECK_RC(RTLocalIpcSessionWaitForData(hClientSession, 0 /*cMsTimeout*/), VERR_TIMEOUT);
    RTTESTI_CHECK_RC(RTLocalIpcSessionWaitForData(hClientSession, 8 /*cMsTimeout*/), VERR_TIMEOUT);
    uint8_t abBuf[4];
    size_t cbRead = _4M-1;
    RTTESTI_CHECK_RC(RTLocalIpcSessionReadNB(hClientSession, abBuf, sizeof(abBuf), &cbRead), VINF_TRY_AGAIN);
    RTTESTI_CHECK(cbRead == 0);

    /* Trigger server disconnect. */
    int rc;
    RTTESTI_CHECK_RC(rc = RTLocalIpcSessionWrite(hClientSession, RT_STR_TUPLE("disconnect")), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        /*
         * When we wait now, we should get an broken pipe error as
         * the server has close its end.
         */
        RTTESTI_CHECK_RC(rc = RTLocalIpcSessionWaitForData(hClientSession, RT_MS_1MIN), VERR_BROKEN_PIPE);
        RTTESTI_CHECK_RC(RTLocalIpcSessionWaitForData(hClientSession, 0), VERR_BROKEN_PIPE);
        RTTESTI_CHECK_RC(RTLocalIpcSessionWaitForData(hClientSession, RT_MS_1SEC), VERR_BROKEN_PIPE);

        bool fMayPanic = RTAssertSetMayPanic(false);
        bool fQuiet    = RTAssertSetQuiet(true);

        RTTESTI_CHECK_RC(RTLocalIpcSessionWrite(hClientSession, RT_STR_TUPLE("broken")), VERR_BROKEN_PIPE);
        RTTESTI_CHECK_RC(RTLocalIpcSessionRead(hClientSession, abBuf, sizeof(abBuf), NULL), VERR_BROKEN_PIPE);
        cbRead = _4M-1;
        RTTESTI_CHECK_RC(RTLocalIpcSessionRead(hClientSession, abBuf, sizeof(abBuf), &cbRead), VERR_BROKEN_PIPE);
        cbRead = _1G/2;
        RTTESTI_CHECK_RC(RTLocalIpcSessionReadNB(hClientSession, abBuf, sizeof(abBuf), &cbRead), VERR_BROKEN_PIPE);

        RTAssertSetMayPanic(fMayPanic);
        RTAssertSetQuiet(fQuiet);
    }

    RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hClientSession), VINF_OBJECT_DESTROYED);

    return VINF_SUCCESS;
}


/**
 * @note This is identical to testSessionData with a couple of string and
 *       function pointers replaced.
 */
static void testSessionWait(const char *pszExecPath)
{
    RTTestISub(!pszExecPath ? "Wait for data in thread" : "Wait for data in child");

    /*
     * Create the test server.
     */
    RTLOCALIPCSERVER hIpcServer;
    RTTESTI_CHECK_RC_RETV(RTLocalIpcServerCreate(&hIpcServer, "tstRTLocalIpcSessionWait", 0), VINF_SUCCESS);

    /*
     * Create worker thread that listens and processes incoming connections
     * until cancelled.
     */
    int rc;
    RTTHREAD hListenThread;
    RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hListenThread, testSessionWaitThread, hIpcServer, 0 /* Stack */,
                                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "listen-2"));
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a client process or thread and connects to the server.
         * It will perform the wait-for-data test.
         */
        RTPROCESS hClientProc = NIL_RTPROCESS;
        RTTHREAD  hClientThread = NIL_RTTHREAD;
        if (pszExecPath)
        {
            const char *apszArgs[4] = { pszExecPath, "child", "tstRTLocalIpcSessionWaitChild", NULL };
            RTTESTI_CHECK_RC_OK(rc = RTProcCreate(pszExecPath, apszArgs, RTENV_DEFAULT, 0 /* fFlags*/, &hClientProc));
        }
        else
            RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hClientThread, tstRTLocalIpcSessionWaitChild, g_hTest, 0 /*cbStack*/,
                                                    RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "client-2"));

        /*
         * Wait for the server thread to indicate that it has processed one
         * connection, then shut it all down.
         */
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC_OK(RTThreadUserWait(hListenThread, RT_MS_1MIN / 2));

        RTTESTI_CHECK_RC(RTLocalIpcServerCancel(hIpcServer), VINF_SUCCESS);
        int rcThread;
        RTTESTI_CHECK_RC(rc = RTThreadWait(hListenThread, RT_MS_1MIN / 2, &rcThread), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC(rcThread, VERR_CANCELLED);

        RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(hIpcServer), VINF_OBJECT_DESTROYED);

        /*
         * Check that client ran successfully.
         */
        if (pszExecPath)
        {
            if (hClientProc != NIL_RTPROCESS)
            {
                RTPROCSTATUS ProcStatus;
                RTTESTI_CHECK_RC_OK(rc = RTProcWait(hClientProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus));
                if (RT_SUCCESS(rc) && (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0))
                    RTTestIFailed("Chiled exited with enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
            }
        }
        else if (hClientThread != NIL_RTTHREAD)
        {
            RTTESTI_CHECK_RC_OK(rc = RTThreadWait(hClientThread, RT_MS_1MIN / 2, &rcThread));
            if (RT_SUCCESS(rc))
                RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);
        }
    }
}



/*********************************************************************************************************************************
*                                                                                                                                *
*   testSessionData - Data transfer integrity.                                                                                   *
*                                                                                                                                *
*********************************************************************************************************************************/

/** The max message size. */
#define MAX_DATA_MSG_SIZE   _1M

static int testSessionDataReadMessages(RTLOCALIPCSESSION hIpcSession, uint32_t cRounds)
{
    /*
     * Message scratch buffer.  Search message starts with a uint32_t word
     * that indicates the message length.  The remaining words are set to
     * the message number.
     */
    uint32_t *pau32ScratchBuf = (uint32_t *)RTMemAlloc(MAX_DATA_MSG_SIZE);
    RTTESTI_CHECK_RET(pau32ScratchBuf != NULL, VERR_NO_MEMORY);

    int rc = VINF_SUCCESS;
    for (uint32_t iRound = 0; iRound < cRounds && rc == VINF_SUCCESS; iRound++)
    {
        /* Read the message length. */
        uint32_t cbMsg;
        RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionRead(hIpcSession, &cbMsg, sizeof(cbMsg), NULL), VINF_SUCCESS);
        if (cbMsg >= sizeof(cbMsg) && cbMsg <= MAX_DATA_MSG_SIZE)
        {
            pau32ScratchBuf[0] = cbMsg;

            /* Read the message body. */
            uint32_t cbLeft = cbMsg - sizeof(uint32_t);
            uint8_t *pbCur  = (uint8_t *)&pau32ScratchBuf[1];
            while (cbLeft > 0)
            {
                uint32_t cbCur = RTRandU32Ex(1, cbLeft + cbLeft / 4);
                cbCur = RT_MIN(cbCur, cbLeft);
                if ((iRound % 3) == 1)
                {
                    size_t cbRead = _1G;
                    RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionRead(hIpcSession, pbCur, cbCur, &cbRead), VINF_SUCCESS);
                    RTTESTI_CHECK(cbCur >= cbRead);
                    cbCur = (uint32_t)cbRead;
                }
                else
                    RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionRead(hIpcSession, pbCur, cbCur, NULL), VINF_SUCCESS);
                pbCur  += cbCur;
                cbLeft -= cbCur;
            }

            /* Check the message body. */
            if (RT_SUCCESS(rc))
            {
                uint32_t offLast = cbMsg & (sizeof(uint32_t) - 1);
                if (offLast)
                    memcpy((uint8_t *)pau32ScratchBuf + cbMsg, (uint8_t const *)&iRound + offLast, sizeof(uint32_t) - offLast);

                ASMCompilerBarrier(); /* Guard against theoretical alias issues in the above code.  */

                uint32_t cWords = RT_ALIGN_32(cbMsg, sizeof(uint32_t)) / sizeof(uint32_t);
                for (uint32_t iWord = 1; iWord < cWords; iWord++)
                    if (pau32ScratchBuf[iWord] != iRound)
                    {
                        RTTestIFailed("Message body word #%u mismatch: %#x, expected %#x", iWord, pau32ScratchBuf[iWord], iRound);
                        break;
                    }
            }
        }
        else
        {
            RTTestIFailed("cbMsg=%#x is out of range", cbMsg);
            rc = VERR_OUT_OF_RANGE;
        }
    }

    RTMemFree(pau32ScratchBuf);
    return rc;
}


static int testSessionDataWriteMessages(RTLOCALIPCSESSION hIpcSession, uint32_t cRounds)
{
    /*
     * Message scratch buffer.  Search message starts with a uint32_t word
     * that indicates the message length.  The remaining words are set to
     * the message number.
     */
    uint32_t   cbScratchBuf = RTRandU32Ex(64, MAX_DATA_MSG_SIZE);
    cbScratchBuf = RT_ALIGN_32(cbScratchBuf, sizeof(uint32_t));

    uint32_t *pau32ScratchBuf = (uint32_t *)RTMemAlloc(cbScratchBuf);
    RTTESTI_CHECK_RET(pau32ScratchBuf != NULL, VERR_NO_MEMORY);

    size_t cbSent = 0;
    int rc = VINF_SUCCESS;
    for (uint32_t iRound = 0; iRound < cRounds && rc == VINF_SUCCESS; iRound++)
    {
        /* Construct the message. */
        uint32_t cbMsg  = RTRandU32Ex(sizeof(uint32_t), cbScratchBuf);
        uint32_t cWords = RT_ALIGN_32(cbMsg, sizeof(uint32_t)) / sizeof(uint32_t);

        uint32_t iWord  = 0;
        pau32ScratchBuf[iWord++] = cbMsg;
        while (iWord < cWords)
            pau32ScratchBuf[iWord++] = iRound;

        /* Send it. */
        uint32_t cbLeft = cbMsg;
        uint8_t const *pbCur = (uint8_t *)pau32ScratchBuf;
        while (cbLeft > 0)
        {
            uint32_t cbCur = RT_MIN(iRound + 1, cbLeft);
            RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionWrite(hIpcSession, pbCur, cbCur), VINF_SUCCESS);
            pbCur  += cbCur;
            cbSent += cbCur;
            cbLeft -= cbCur;
        }
    }

    RTTestIPrintf(RTTESTLVL_ALWAYS, "Sent %'zu bytes over %u rounds.\n", cbSent, cRounds);
    RTMemFree(pau32ScratchBuf);
    return rc;
}


static DECLCALLBACK(int) testSessionDataThread(RTTHREAD hSelf, void *pvUser)
{
    RTLOCALIPCSERVER hIpcServer = (RTLOCALIPCSERVER)pvUser;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);

    int rc;
    for (;;)
    {
        RTLOCALIPCSESSION hIpcSession;
        rc = RTLocalIpcServerListen(hIpcServer, &hIpcSession);
        if (RT_SUCCESS(rc))
        {
            RTTestIPrintf(RTTESTLVL_INFO, "testSessionDataThread: Got new client connection\n");

            /* The server is the initator. First message sets the number of rounds. */
            uint32_t cRounds = RTRandU32Ex(32, _1K);
            RTTESTI_CHECK_RC(rc = RTLocalIpcSessionWrite(hIpcSession, &cRounds, sizeof(cRounds)), VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                rc = testSessionDataWriteMessages(hIpcSession, cRounds);
                if (RT_SUCCESS(rc))
                    rc = testSessionDataReadMessages(hIpcSession, cRounds);
            }

            RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hIpcSession), VINF_OBJECT_DESTROYED);
            RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));
        }
        else
        {
            RTTESTI_CHECK_RC(rc, VERR_CANCELLED);
            break;
        }
    }
    RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));
    return rc;
}


/**
 * Used both as a thread procedure and child process worker.
 */
static DECLCALLBACK(int) tstRTLocalIpcSessionDataChild(RTTHREAD hSelf, void *pvUser)
{
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);
    RT_NOREF_PV(hSelf); RT_NOREF_PV(pvUser);

    /*
     * Connect.
     */
    RTLOCALIPCSESSION hClientSession;
    RTTESTI_CHECK_RC_RET(RTLocalIpcSessionConnect(&hClientSession, "tstRTLocalIpcSessionData", 0 /*fFlags*/),
                         VINF_SUCCESS, rcCheck);

    /*
     * The server first sends us a rounds count.
     */
    int rc;
    uint32_t cRounds = 0;
    RTTESTI_CHECK_RC(rc = RTLocalIpcSessionRead(hClientSession, &cRounds, sizeof(cRounds), NULL), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        if (cRounds >= 32 && cRounds <= _1K)
        {
            rc = testSessionDataReadMessages(hClientSession, cRounds);
            if (RT_SUCCESS(rc))
                rc = testSessionDataWriteMessages(hClientSession, cRounds);
        }
        else
            RTTestIFailed("cRounds=%#x is out of range", cRounds);
    }

    RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hClientSession), VINF_OBJECT_DESTROYED);

    return rc;
}


/**
 * @note This is identical to testSessionWait with a couple of strings, function
 *       pointers, and timeouts replaced.
 */
static void testSessionData(const char *pszExecPath)
{
    RTTestISub(!pszExecPath ? "Data exchange with thread" : "Data exchange with child");

    /*
     * Create the test server.
     */
    RTLOCALIPCSERVER hIpcServer;
    RTTESTI_CHECK_RC_RETV(RTLocalIpcServerCreate(&hIpcServer, "tstRTLocalIpcSessionData", 0), VINF_SUCCESS);

    /*
     * Create worker thread that listens and processes incoming connections
     * until cancelled.
     */
    int rc;
    RTTHREAD hListenThread;
    RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hListenThread, testSessionDataThread, hIpcServer, 0 /* Stack */,
                                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "listen-3"));
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a client thread or process.
         */
        RTPROCESS hClientProc   = NIL_RTPROCESS;
        RTTHREAD  hClientThread = NIL_RTTHREAD;
        if (pszExecPath)
        {
            const char *apszArgs[4] = { pszExecPath, "child", "tstRTLocalIpcSessionDataChild", NULL };
            RTTESTI_CHECK_RC_OK(rc = RTProcCreate(pszExecPath, apszArgs, RTENV_DEFAULT, 0 /* fFlags*/, &hClientProc));
        }
        else
            RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hClientThread, tstRTLocalIpcSessionDataChild, g_hTest, 0 /*cbStack*/,
                                                    RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "client-2"));

        /*
         * Wait for the server thread to indicate that it has processed one
         * connection, then shut it all down.
         */
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC_OK(RTThreadUserWait(hListenThread, RT_MS_1MIN * 3));

        RTTESTI_CHECK_RC(RTLocalIpcServerCancel(hIpcServer), VINF_SUCCESS);
        int rcThread;
        RTTESTI_CHECK_RC(rc = RTThreadWait(hListenThread, RT_MS_1MIN / 2, &rcThread), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC(rcThread, VERR_CANCELLED);

        RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(hIpcServer), VINF_OBJECT_DESTROYED);

        /*
         * Check that client ran successfully.
         */
        if (pszExecPath)
        {
            if (hClientProc != NIL_RTPROCESS)
            {
                RTPROCSTATUS ProcStatus;
                RTTESTI_CHECK_RC_OK(rc = RTProcWait(hClientProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus));
                if (RT_SUCCESS(rc) && (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0))
                    RTTestIFailed("Chiled exited with enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
            }
        }
        else if (hClientThread != NIL_RTTHREAD)
        {
            RTTESTI_CHECK_RC_OK(rc = RTThreadWait(hClientThread, RT_MS_1MIN / 2, &rcThread));
            if (RT_SUCCESS(rc))
                RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);
        }
    }
}


/*********************************************************************************************************************************
*                                                                                                                                *
*   testSessionPerf - Performance measurements.                                                                                  *
*                                                                                                                                *
*********************************************************************************************************************************/

#define IPC_PERF_LAST_MSG   UINT32_C(0x7fffeeee)
#define IPC_PERF_MSG_REPLY(uMsg)    ((uMsg) | RT_BIT_32(31))


static DECLCALLBACK(int) testSessionPerfThread(RTTHREAD hSelf, void *pvUser)
{
    RTLOCALIPCSERVER hIpcServer = (RTLOCALIPCSERVER)pvUser;
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);

    int rc;
    for (;;)
    {
        RTLOCALIPCSESSION hIpcSession;
        rc = RTLocalIpcServerListen(hIpcServer, &hIpcSession);
        if (RT_SUCCESS(rc))
        {
            RTTestIPrintf(RTTESTLVL_INFO, "testSessionPerfThread: Got new client connection\n");

            /* The server is the initator, so we start sending messages. */
            uint64_t cNsElapsed = _4G;
            uint64_t nsStart    = RTTimeNanoTS();
            uint32_t cMessages  = 0;
            for (;; )
            {
                uint32_t uMsg = cMessages;
                RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionWrite(hIpcSession, &uMsg, sizeof(uMsg)), VINF_SUCCESS);
                uMsg = UINT32_MAX;
                RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionRead(hIpcSession, &uMsg, sizeof(uMsg), NULL), VINF_SUCCESS);
                if (uMsg == IPC_PERF_MSG_REPLY(cMessages))
                { /* likely */ }
                else
                {
                    RTTestIFailed("uMsg=%#x expected %#x", uMsg, IPC_PERF_MSG_REPLY(cMessages));
                    rc = VERR_OUT_OF_RANGE;
                    break;
                }

                /* next */
                cMessages++;
                if (cMessages & _16K)
                { /* likely */ }
                else
                {
                    cNsElapsed = RTTimeNanoTS() - nsStart;
                    if (cNsElapsed > 2*RT_NS_1SEC_64)
                    {
                        uMsg = IPC_PERF_LAST_MSG;
                        RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionWrite(hIpcSession, &uMsg, sizeof(uMsg)), VINF_SUCCESS);
                        break;
                    }
                }
            }
            if (RT_SUCCESS(rc))
            {
                RTThreadSleep(8); /* windows output fudge (purely esthetical) */
                RTTestIValue("roundtrip", cNsElapsed / cMessages, RTTESTUNIT_NS_PER_ROUND_TRIP);
                RTTestIValue("roundtrips", RT_NS_1SEC / (cNsElapsed / cMessages), RTTESTUNIT_OCCURRENCES_PER_SEC);
            }

            RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hIpcSession), VINF_OBJECT_DESTROYED);
            RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));
        }
        else
        {
            RTTESTI_CHECK_RC(rc, VERR_CANCELLED);
            break;
        }
    }
    RTTESTI_CHECK_RC_OK(RTThreadUserSignal(hSelf));
    return rc;
}


/**
 * Used both as a thread procedure and child process worker.
 */
static DECLCALLBACK(int) tstRTLocalIpcSessionPerfChild(RTTHREAD hSelf, void *pvUser)
{
    RTTEST_CHECK_RC_OK_RET(g_hTest, RTTestSetDefault(g_hTest, NULL), rcCheck);
    RT_NOREF_PV(hSelf); RT_NOREF_PV(pvUser);

    /*
     * Connect.
     */
    RTLOCALIPCSESSION hClientSession;
    RTTESTI_CHECK_RC_RET(RTLocalIpcSessionConnect(&hClientSession, "tstRTLocalIpcSessionPerf", 0 /*fFlags*/),
                         VINF_SUCCESS, rcCheck);

    /*
     * Process messages.  Server does all the timing and stuff.
     */
    int rc = VINF_SUCCESS;
    for (uint32_t cMessages = 0; ; cMessages++)
    {
        /* Read the next message from the server. */
        uint32_t uMsg = UINT32_MAX;
        RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionRead(hClientSession, &uMsg, sizeof(uMsg), NULL), VINF_SUCCESS);
        if (uMsg == cMessages)
        {
            uMsg = IPC_PERF_MSG_REPLY(uMsg);
            RTTESTI_CHECK_RC_BREAK(rc = RTLocalIpcSessionWrite(hClientSession, &uMsg, sizeof(uMsg)), VINF_SUCCESS);
        }
        else if (uMsg == IPC_PERF_LAST_MSG)
            break;
        else
        {
            RTTestIFailed("uMsg=%#x expected %#x", uMsg, cMessages);
            rc = VERR_OUT_OF_RANGE;
            break;
        }
    }

    RTTESTI_CHECK_RC(RTLocalIpcSessionClose(hClientSession), VINF_OBJECT_DESTROYED);
    return rc;
}


/**
 * @note This is identical to testSessionWait with a couple of string and
 *       function pointers replaced.
 */
static void testSessionPerf(const char *pszExecPath)
{
    RTTestISub(!pszExecPath ? "Thread performance" : "Child performance");

    /*
     * Create the test server.
     */
    RTLOCALIPCSERVER hIpcServer;
    RTTESTI_CHECK_RC_RETV(RTLocalIpcServerCreate(&hIpcServer, "tstRTLocalIpcSessionPerf", 0), VINF_SUCCESS);

    /*
     * Create worker thread that listens and processes incoming connections
     * until cancelled.
     */
    int rc;
    RTTHREAD hListenThread;
    RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hListenThread, testSessionPerfThread, hIpcServer, 0 /* Stack */,
                                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "listen-3"));
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a client thread or process.
         */
        RTPROCESS hClientProc   = NIL_RTPROCESS;
        RTTHREAD  hClientThread = NIL_RTTHREAD;
        if (pszExecPath)
        {
            const char *apszArgs[4] = { pszExecPath, "child", "tstRTLocalIpcSessionPerfChild", NULL };
            RTTESTI_CHECK_RC_OK(rc = RTProcCreate(pszExecPath, apszArgs, RTENV_DEFAULT, 0 /* fFlags*/, &hClientProc));
        }
        else
            RTTESTI_CHECK_RC_OK(rc = RTThreadCreate(&hClientThread, tstRTLocalIpcSessionPerfChild, g_hTest, 0 /*cbStack*/,
                                                    RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "client-2"));

        /*
         * Wait for the server thread to indicate that it has processed one
         * connection, then shut it all down.
         */
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC_OK(RTThreadUserWait(hListenThread, RT_MS_1MIN / 2));

        RTTESTI_CHECK_RC(RTLocalIpcServerCancel(hIpcServer), VINF_SUCCESS);
        int rcThread;
        RTTESTI_CHECK_RC(rc = RTThreadWait(hListenThread, RT_MS_1MIN / 2, &rcThread), VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            RTTESTI_CHECK_RC(rcThread, VERR_CANCELLED);

        RTTESTI_CHECK_RC(RTLocalIpcServerDestroy(hIpcServer), VINF_OBJECT_DESTROYED);

        /*
         * Check that client ran successfully.
         */
        if (pszExecPath)
        {
            if (hClientProc != NIL_RTPROCESS)
            {
                RTPROCSTATUS ProcStatus;
                RTTESTI_CHECK_RC_OK(rc = RTProcWait(hClientProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus));
                if (RT_SUCCESS(rc) && (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0))
                    RTTestIFailed("Chiled exited with enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
            }
        }
        else if (hClientThread != NIL_RTTHREAD)
        {
            RTTESTI_CHECK_RC_OK(rc = RTThreadWait(hClientThread, RT_MS_1MIN / 2, &rcThread));
            if (RT_SUCCESS(rc))
                RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);
        }
    }
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Main process.
     */
    if (argc == 1)
    {
        rc = RTTestCreate("tstRTLocalIpc", &g_hTest);
        if (RT_FAILURE(rc))
            return RTEXITCODE_FAILURE;
        RTTestBanner(g_hTest);

        /* Basics first. */
        bool fMayPanic = RTAssertSetMayPanic(false);
        bool fQuiet    = RTAssertSetQuiet(true);
        testBasics();
        RTAssertSetMayPanic(fMayPanic);
        RTAssertSetQuiet(fQuiet);

        /* Do real tests if the basics are fine. */
        char szExecPath[RTPATH_MAX];
        if (RTProcGetExecutablePath(szExecPath, sizeof(szExecPath)))
        {
            if (RTTestErrorCount(g_hTest) == 0)
                testSessionConnection(NULL);
            if (RTTestErrorCount(g_hTest) == 0)
                testSessionConnection(szExecPath);

            if (RTTestErrorCount(g_hTest) == 0)
                testSessionWait(NULL);
            if (RTTestErrorCount(g_hTest) == 0)
                testSessionWait(szExecPath);

            if (RTTestErrorCount(g_hTest) == 0)
                testSessionData(NULL);
            if (RTTestErrorCount(g_hTest) == 0)
                testSessionData(szExecPath);

            if (RTTestErrorCount(g_hTest) == 0)
                testSessionPerf(NULL);
            if (RTTestErrorCount(g_hTest) == 0)
                testSessionPerf(szExecPath);
        }
        else
            RTTestIFailed("RTProcGetExecutablePath failed");
    }
    /*
     * Child process.
     */
    else if (   argc == 3
             && !strcmp(argv[1], "child"))
    {
        rc = RTTestCreateChild(argv[2], &g_hTest);
        if (RT_FAILURE(rc))
            return RTEXITCODE_FAILURE;

        if (!strcmp(argv[2], "tstRTLocalIpcSessionConnectionChild"))
            tstRTLocalIpcSessionConnectionChild(RTThreadSelf(), g_hTest);
        else if (!strcmp(argv[2], "tstRTLocalIpcSessionWaitChild"))
            tstRTLocalIpcSessionWaitChild(RTThreadSelf(), g_hTest);
        else if (!strcmp(argv[2], "tstRTLocalIpcSessionDataChild"))
            tstRTLocalIpcSessionDataChild(RTThreadSelf(), g_hTest);
        else if (!strcmp(argv[2], "tstRTLocalIpcSessionPerfChild"))
            tstRTLocalIpcSessionPerfChild(RTThreadSelf(), g_hTest);
        else
            RTTestIFailed("Unknown child function '%s'", argv[2]);
    }
    /*
     * Invalid parameters.
     */
    else
        return RTEXITCODE_SYNTAX;

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(g_hTest);
}

