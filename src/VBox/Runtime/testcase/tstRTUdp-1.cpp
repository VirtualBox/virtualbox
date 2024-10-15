/* $Id: tstRTUdp-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT testcase - UDP.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include <iprt/udp.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>

/* Server address must be "localhost" */
#define RT_TEST_UDP_LOCAL_HOST         "localhost"
#define RT_TEST_UDP_SERVER_PORT        52000


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


/* * * * * * * *   Test 1    * * * * * * * */

static DECLCALLBACK(int) test1Server(RTSOCKET hSocket, void *pvUser)
{
    RTTestSetDefault(g_hTest, NULL);

    char szBuf[512];
    RTTESTI_CHECK_RET(pvUser == NULL, VERR_UDP_SERVER_STOP);

    RTNETADDR ClientAddress;

    /* wait for exclamation! */
    size_t cbReallyRead;
    RTTESTI_CHECK_RC_RET(RTSocketReadFrom(hSocket, szBuf, sizeof("dude!\n") - 1, &cbReallyRead, &ClientAddress), VINF_SUCCESS,
                         VERR_UDP_SERVER_STOP);
    szBuf[sizeof("dude!\n") - 1] = '\0';
    RTTESTI_CHECK_RET(cbReallyRead == sizeof("dude!\n") - 1, VERR_UDP_SERVER_STOP);
    RTTESTI_CHECK_RET(strcmp(szBuf, "dude!\n") == 0, VERR_UDP_SERVER_STOP);

    /* say hello. */
    RTTESTI_CHECK_RC_RET(RTSocketWriteTo(hSocket, "hello\n", sizeof("hello\n") - 1, &ClientAddress), VINF_SUCCESS,
                         VERR_UDP_SERVER_STOP);

    /* wait for goodbye. */
    RTTESTI_CHECK_RC_RET(RTSocketReadFrom(hSocket, szBuf, sizeof("byebye\n") - 1, &cbReallyRead, &ClientAddress), VINF_SUCCESS,
                         VERR_UDP_SERVER_STOP);
    RTTESTI_CHECK_RET(cbReallyRead == sizeof("byebye\n") - 1, VERR_UDP_SERVER_STOP);
    szBuf[sizeof("byebye\n") - 1] = '\0';
    RTTESTI_CHECK_RET(strcmp(szBuf, "byebye\n") == 0, VERR_UDP_SERVER_STOP);

    /* say buhbye */
    RTTESTI_CHECK_RC_RET(RTSocketWriteTo(hSocket, "buh bye\n", sizeof("buh bye\n") - 1, &ClientAddress), VINF_SUCCESS,
                         VERR_UDP_SERVER_STOP);

    return VINF_SUCCESS;
}


static void test1()
{
    RTTestSub(g_hTest, "Simple server-client setup");

    /*
     * Set up server address (port) for UDP.
     */
    RTNETADDR ServerAddress;
    RTTESTI_CHECK_RC_RETV(RTSocketParseInetAddress(RT_TEST_UDP_LOCAL_HOST, RT_TEST_UDP_SERVER_PORT, &ServerAddress),
                          VINF_SUCCESS);

    PRTUDPSERVER pServer;
    RTTESTI_CHECK_RC_RETV(RTUdpServerCreate(RT_TEST_UDP_LOCAL_HOST, RT_TEST_UDP_SERVER_PORT, RTTHREADTYPE_DEFAULT, "server-1",
                                            test1Server, NULL, &pServer), VINF_SUCCESS);

    int rc;
    RTSOCKET hSocket;
    RTTESTI_CHECK_RC(rc = RTUdpCreateClientSocket(RT_TEST_UDP_LOCAL_HOST, RT_TEST_UDP_SERVER_PORT, NULL, &hSocket), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        do /* break non-loop */
        {
            char szBuf[512];
            RT_ZERO(szBuf);
            RTTESTI_CHECK_RC_BREAK(RTSocketWrite(hSocket, "dude!\n", sizeof("dude!\n") - 1), VINF_SUCCESS);

            RTTESTI_CHECK_RC_BREAK(RTSocketRead(hSocket, szBuf, sizeof("hello\n") - 1, NULL), VINF_SUCCESS);
            szBuf[sizeof("hello!\n") - 1] = '\0';
            RTTESTI_CHECK_BREAK(strcmp(szBuf, "hello\n") == 0);

            RTTESTI_CHECK_RC_BREAK(RTSocketWrite(hSocket, "byebye\n", sizeof("byebye\n") - 1), VINF_SUCCESS);
            RT_ZERO(szBuf);
            RTTESTI_CHECK_RC_BREAK(RTSocketRead(hSocket, szBuf, sizeof("buh bye\n") - 1, NULL), VINF_SUCCESS);
            RTTESTI_CHECK_BREAK(strcmp(szBuf, "buh bye\n") == 0);
        } while (0);

        RTTESTI_CHECK_RC(RTSocketClose(hSocket), VINF_SUCCESS);
    }

    RTTESTI_CHECK_RC(RTUdpServerDestroy(pServer), VINF_SUCCESS);
}


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTUdp-1", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    test1();

    /** @todo test the full RTUdp API. */

    return RTTestSummaryAndDestroy(g_hTest);
}

