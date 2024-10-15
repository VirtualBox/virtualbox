/* $Id: tstSemPingPong.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTSemPing/RTSemPong.
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
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/thread.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define TSTSEMPINGPONG_ITERATIONS   1000000


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static volatile uint32_t g_cErrors = 0;

static DECLCALLBACK(int) tstSemPingPongThread(RTTHREAD hThread, void *pvPP)
{
    RT_NOREF_PV(hThread);

    int rc = VINF_SUCCESS; /* (MSC powers of deduction are rather weak. sigh) */
    PRTPINGPONG pPP = (PRTPINGPONG)pvPP;
    for (uint32_t i = 0; i < TSTSEMPINGPONG_ITERATIONS; i++)
    {
        if (!RTSemPongShouldWait(pPP))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPongShouldWait returned false before RTSemPongWait.\n");
        }

        rc = RTSemPongWait(pPP, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPongWait -> %Rrc\n", rc);
            break;
        }

        if (!RTSemPongIsSpeaker(pPP))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPongIsSpeaker returned false before RTSemPong.\n");
        }

        rc = RTSemPong(pPP);
        if (RT_FAILURE(rc))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPong -> %Rrc\n", rc);
            break;
        }
    }
    return rc;
}


int main()
{
    RTR3InitExeNoArguments(0);

    /*
     * Create a ping pong and kick off a second thread which we'll
     * exchange TSTSEMPINGPONG_ITERATIONS messages with.
     */
    RTPINGPONG PingPong;
    int rc = RTSemPingPongInit(&PingPong);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSemPingPong: FATAL ERROR - RTSemPingPongInit -> %Rrc\n", rc);
        return 1;
    }

    RTTHREAD hThread;
    rc = RTThreadCreate(&hThread, tstSemPingPongThread, &PingPong, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "PONG");
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstSemPingPong: FATAL ERROR - RTSemPingPongInit -> %Rrc\n", rc);
        return 1;
    }

    RTPrintf("tstSemPingPong: TESTING - %u iterations...\n", TSTSEMPINGPONG_ITERATIONS);
    for (uint32_t i = 0; i < TSTSEMPINGPONG_ITERATIONS; i++)
    {
        if (!RTSemPingIsSpeaker(&PingPong))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPingIsSpeaker returned false before RTSemPing.\n");
        }

        rc = RTSemPing(&PingPong);
        if (RT_FAILURE(rc))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPing -> %Rrc\n", rc);
            break;
        }

        if (!RTSemPingShouldWait(&PingPong))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPingShouldWait returned false before RTSemPingWait.\n");
        }

        rc = RTSemPingWait(&PingPong, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc))
        {
            ASMAtomicIncU32(&g_cErrors);
            RTPrintf("tstSemPingPong: ERROR - RTSemPingWait -> %Rrc\n", rc);
            break;
        }
    }

    int rcThread;
    rc = RTThreadWait(hThread, 5000, &rcThread);
    if (RT_FAILURE(rc))
    {
        ASMAtomicIncU32(&g_cErrors);
        RTPrintf("tstSemPingPong: ERROR - RTSemPingWait -> %Rrc\n", rc);
    }

    rc = RTSemPingPongDelete(&PingPong);
    if (RT_FAILURE(rc))
    {
        ASMAtomicIncU32(&g_cErrors);
        RTPrintf("tstSemPingPong: ERROR - RTSemPingPongDestroy -> %Rrc\n", rc);
    }

    /*
     * Summary.
     */
    uint32_t cErrors = ASMAtomicUoReadU32(&g_cErrors);
    if (cErrors)
        RTPrintf("tstSemPingPong: FAILED - %d errors\n", cErrors);
    else
        RTPrintf("tstSemPingPong: SUCCESS\n");
    return cErrors ? 1 : 0;
}

