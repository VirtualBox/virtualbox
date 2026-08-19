/* $Id: VBoxNetDhcpdInProc.cpp 115072 2026-08-19 07:32:45Z andreas.loeffler@oracle.com $ */
/** @file
 * In-process VBoxNetDHCP source shim for tstVBoxNetDhcpd.
 *
 * This guarantees that VBoxNetDhcpd.cpp is compiled with the testcase hooks,
 * independent of target-global preprocessor definitions on a specific kBuild
 * host/toolchain combination.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VBOXNETDHCPD_INPROC_TESTING
# define VBOXNETDHCPD_INPROC_TESTING 1
#endif

#include "../VBoxNetDhcpd.cpp"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <new>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Valid in-process DHCP daemon handle magic. */
#define VBOXNETDHCPDTEST_MAGIC       UINT32_C(0x44484350)
/** Invalidated in-process DHCP daemon handle magic. */
#define VBOXNETDHCPDTEST_MAGIC_DEAD  UINT32_C(0x64686370)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** In-process VBoxNetDHCP testcase context. */
typedef struct VBOXNETDHCPDTEST
{
    /** Magic value (VBOXNETDHCPDTEST_MAGIC). */
    uint32_t        u32Magic;
    /** Daemon instance. */
    VBoxNetDhcpd   *pDhcpd;
    /** Waitable daemon thread. */
    RTTHREAD        hThread;
    /** Daemon startup completion event. */
    RTSEMEVENT      hEvtStartup;
    /** Duplicated argument count. */
    int             cArgs;
    /** Duplicated argument vector. */
    char          **papszArgs;
} VBOXNETDHCPDTEST;
typedef VBOXNETDHCPDTEST *PVBOXNETDHCPDTEST;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * Releases an in-process daemon context after its thread has stopped.
 *
 * @param   pTest           In-process daemon context.
 */
static void vboxNetDhcpdTestFree(PVBOXNETDHCPDTEST pTest)
{
    AssertPtrReturnVoid(pTest);
    Assert(pTest->hThread == NIL_RTTHREAD);

    Config *pConfig = pTest->pDhcpd != NULL ? pTest->pDhcpd->testTakeConfig() : NULL;
    delete pTest->pDhcpd;
    pTest->pDhcpd = NULL;
    delete pConfig;

    if (pTest->hEvtStartup != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pTest->hEvtStartup);
        pTest->hEvtStartup = NIL_RTSEMEVENT;
    }

    if (pTest->papszArgs != NULL)
    {
        for (int i = 0; i < pTest->cArgs; i++)
            RTStrFree(pTest->papszArgs[i]);
        RTMemFree(pTest->papszArgs);
        pTest->papszArgs = NULL;
    }

    pTest->u32Magic = VBOXNETDHCPDTEST_MAGIC_DEAD;
    RTMemFree(pTest);
}


/**
 * Runs the production VBoxNetDHCP daemon in-process.
 *
 * @returns VBox status code.
 * @param   hThreadSelf     Thread handle, unused.
 * @param   pvUser          In-process daemon context.
 */
static DECLCALLBACK(int) vboxNetDhcpdTestThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    PVBOXNETDHCPDTEST pTest = (PVBOXNETDHCPDTEST)pvUser;
    AssertPtrReturn(pTest, VERR_INVALID_POINTER);
    AssertReturn(pTest->u32Magic == VBOXNETDHCPDTEST_MAGIC, VERR_INVALID_HANDLE);

    int rc = pTest->pDhcpd->main(pTest->cArgs, pTest->papszArgs);
    if (rc == VERR_SEM_DESTROYED)
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Duplicates an argument vector for the daemon thread.
 *
 * @returns VBox status code.
 * @param   pTest           In-process daemon context.
 * @param   argc            Argument count.
 * @param   argv            Argument vector.
 */
static int vboxNetDhcpdTestDupArgv(PVBOXNETDHCPDTEST pTest, int argc, char **argv)
{
    pTest->papszArgs = (char **)RTMemAllocZ((argc + 1) * sizeof(pTest->papszArgs[0]));
    if (pTest->papszArgs == NULL)
        return VERR_NO_MEMORY;

    for (int i = 0; i < argc; i++)
    {
        AssertPtrReturn(argv[i], VERR_INVALID_POINTER);
        pTest->papszArgs[i] = RTStrDup(argv[i]);
        if (pTest->papszArgs[i] == NULL)
            return VERR_NO_MEMORY;
        pTest->cArgs = i + 1;
    }
    return VINF_SUCCESS;
}


/**
 * Starts VBoxNetDHCP in-process and waits for its IntNet/lwIP initialization.
 *
 * @returns VBox status code.
 * @param   argc            Argument count for VBoxNetDHCP.
 * @param   argv            Argument vector for VBoxNetDHCP.
 * @param   ppvHandle       Where to return the opaque daemon handle.
 */
extern "C" int VBoxNetDhcpdTestStart(int argc, char **argv, void **ppvHandle)
{
    AssertPtrReturn(ppvHandle, VERR_INVALID_POINTER);
    *ppvHandle = NULL;
    AssertReturn(argc > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(argv, VERR_INVALID_POINTER);

    PVBOXNETDHCPDTEST pTest = (PVBOXNETDHCPDTEST)RTMemAllocZ(sizeof(*pTest));
    if (pTest == NULL)
        return VERR_NO_MEMORY;
    pTest->u32Magic    = VBOXNETDHCPDTEST_MAGIC;
    pTest->hThread     = NIL_RTTHREAD;
    pTest->hEvtStartup = NIL_RTSEMEVENT;

    int rc = vboxNetDhcpdTestDupArgv(pTest, argc, argv);
    if (RT_SUCCESS(rc))
        rc = RTSemEventCreate(&pTest->hEvtStartup);
    if (RT_SUCCESS(rc))
    {
        pTest->pDhcpd = new (std::nothrow) VBoxNetDhcpd();
        if (pTest->pDhcpd == NULL)
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        pTest->pDhcpd->testSetStartupEvent(pTest->hEvtStartup);
        rc = RTThreadCreate(&pTest->hThread, vboxNetDhcpdTestThread, pTest, 0 /*cbStack*/,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "DhcpdTest");
    }
    if (RT_FAILURE(rc))
    {
        vboxNetDhcpdTestFree(pTest);
        return rc;
    }

    rc = RTSemEventWait(pTest->hEvtStartup, RT_MS_30SEC);
    if (RT_SUCCESS(rc))
        rc = pTest->pDhcpd->testQueryStartupStatus();
    if (RT_SUCCESS(rc))
    {
        *ppvHandle = pTest;
        return VINF_SUCCESS;
    }

    int rcThread = VINF_SUCCESS;
    int rcWait = RTThreadWait(pTest->hThread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rcWait))
    {
        pTest->hThread = NIL_RTTHREAD;
        vboxNetDhcpdTestFree(pTest);
    }
    else
        *ppvHandle = pTest;
    return rc;
}


/**
 * Stops and destroys an in-process VBoxNetDHCP daemon.
 *
 * @returns VBox status code.
 * @param   pvHandle        Opaque daemon handle returned by VBoxNetDhcpdTestStart.
 */
extern "C" int VBoxNetDhcpdTestStop(void *pvHandle)
{
    PVBOXNETDHCPDTEST pTest = (PVBOXNETDHCPDTEST)pvHandle;
    AssertPtrReturn(pTest, VERR_INVALID_HANDLE);
    AssertReturn(pTest->u32Magic == VBOXNETDHCPDTEST_MAGIC, VERR_INVALID_HANDLE);

    int rc = pTest->pDhcpd->testStop();
    int rcThread = VINF_SUCCESS;
    int rcWait = RTThreadWait(pTest->hThread, RT_MS_30SEC, &rcThread);
    if (RT_FAILURE(rcWait))
        return rcWait;

    pTest->hThread = NIL_RTTHREAD;
    vboxNetDhcpdTestFree(pTest);
    if (RT_FAILURE(rc))
        return rc;
    return rcThread;
}


/**
 * Checks whether an in-process VBoxNetDHCP daemon is running.
 *
 * @returns true if the daemon is running, false otherwise.
 * @param   pvHandle        Opaque daemon handle returned by VBoxNetDhcpdTestStart.
 */
extern "C" bool VBoxNetDhcpdTestIsRunning(void *pvHandle)
{
    PVBOXNETDHCPDTEST pTest = (PVBOXNETDHCPDTEST)pvHandle;
    if (   pTest == NULL
        || pTest->u32Magic != VBOXNETDHCPDTEST_MAGIC)
        return false;
    return pTest->pDhcpd->testIsRunning();
}
