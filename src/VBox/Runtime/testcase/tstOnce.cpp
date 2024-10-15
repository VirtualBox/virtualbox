/* $Id: tstOnce.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTOnce.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <iprt/once.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int g_cErrors = 0;
static bool g_fOnceCB1 = false;
static uint32_t volatile g_cOnce2CB = 0;
static bool volatile g_fOnce2Ready = false;
static RTONCE g_Once2 = RTONCE_INITIALIZER;
static RTSEMEVENTMULTI g_hEventMulti = NIL_RTSEMEVENTMULTI;


static DECLCALLBACK(int) Once1CB(void *pvUser)
{
    if (g_fOnceCB1)
        return VERR_WRONG_ORDER;
    if (pvUser != (void *)1)
    {
        RTPrintf("tstOnce: ERROR - Once1CB: pvUser=%p!\n", pvUser);
        g_cErrors++;
        return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) Once2CB(void *pvUser)
{
    if (ASMAtomicIncU32(&g_cOnce2CB) != 1)
    {
        RTPrintf("tstOnce: ERROR - Once2CB: g_cOnce2CB not zero!\n");
        g_cErrors++;
        return VERR_WRONG_ORDER;
    }
    if (pvUser != (void *)42)
    {
        RTPrintf("tstOnce: ERROR - Once2CB: pvUser=%p!\n", pvUser);
        g_cErrors++;
        return VERR_INVALID_PARAMETER;
    }
    RTThreadSleep(2);
    Assert(!g_fOnce2Ready);
    ASMAtomicWriteBool(&g_fOnce2Ready, true);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) Once2Thread(RTTHREAD hThread, void *pvUser)
{
    NOREF(hThread); NOREF(pvUser);

    int rc = RTSemEventMultiWait(g_hEventMulti, RT_INDEFINITE_WAIT);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTOnce(&g_Once2, Once2CB, (void *)42);
    if (RT_SUCCESS(rc))
    {
        if (!ASMAtomicUoReadBool(&g_fOnce2Ready))
        {
            RTPrintf("tstOnce: ERROR - Once2CB: Not initialized!\n");
            g_cErrors++;
        }
    }
    return rc;
}


int main()
{
    RTR3InitExeNoArguments(0);

    /*
     * Just a simple testcase.
     */
    RTPrintf("tstOnce: TESTING - smoke...\n");
    RTONCE Once1 = RTONCE_INITIALIZER;
    g_fOnceCB1 = false;
    int rc = RTOnce(&Once1, Once1CB, (void *)1);
    if (rc != VINF_SUCCESS)
        RTPrintf("tstOnce: ERROR - Once1, 1 failed, rc=%Rrc\n", rc);
    g_fOnceCB1 = false;
    rc = RTOnce(&Once1, Once1CB, (void *)1);
    if (rc != VINF_SUCCESS)
        RTPrintf("tstOnce: ERROR - Once1, 2 failed, rc=%Rrc\n", rc);

    /*
     * Throw a bunch of threads up against a init once thing.
     */
    RTPrintf("tstOnce: TESTING - bunch of threads...\n");
    /* create the semaphore they'll be waiting on. */
    rc = RTSemEventMultiCreate(&g_hEventMulti);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstOnce: FATAL ERROR - RTSemEventMultiCreate returned %Rrc\n", rc);
        return 1;
    }

    /* create the threads */
    RTTHREAD aThreads[32];
    for (unsigned i = 0; i < RT_ELEMENTS(aThreads); i++)
    {
        char szName[16];
        RTStrPrintf(szName, sizeof(szName), "ONCE2-%d\n", i);
        rc = RTThreadCreate(&aThreads[i], Once2Thread, NULL, 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, szName);
        if (RT_FAILURE(rc))
        {
            RTPrintf("tstOnce: ERROR - failed to create thread #%d\n", i);
            g_cErrors++;
        }
    }

    /* kick them off and yield */
    rc = RTSemEventMultiSignal(g_hEventMulti);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstOnce: FATAL ERROR - RTSemEventMultiSignal returned %Rrc\n", rc);
        return 1;
    }
    RTThreadYield();

    /* wait for all of them to finish up, 30 seconds each. */
    for (unsigned i = 0; i < RT_ELEMENTS(aThreads); i++)
        if (aThreads[i] != NIL_RTTHREAD)
        {
            int rc2;
            rc = RTThreadWait(aThreads[i], 30*1000, &rc2);
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstOnce: ERROR - RTThreadWait on thread #%u returned %Rrc\n", i, rc);
                g_cErrors++;
            }
            else if (RT_FAILURE(rc2))
            {
                RTPrintf("tstOnce: ERROR - Thread #%u returned %Rrc\n", i, rc2);
                g_cErrors++;
            }
        }

    /*
     * Summary.
     */
    if (!g_cErrors)
        RTPrintf("tstOnce: SUCCESS\n");
    else
        RTPrintf("tstOnce: FAILURE - %d errors\n", g_cErrors);

    return !!g_cErrors;
}

