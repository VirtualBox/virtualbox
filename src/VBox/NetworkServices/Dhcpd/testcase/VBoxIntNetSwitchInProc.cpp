/* $Id: VBoxIntNetSwitchInProc.cpp 115072 2026-08-19 07:32:45Z andreas.loeffler@oracle.com $ */
/** @file
 * In-process local IPC IntNet switch for tstVBoxNetDhcpd.
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

#ifndef VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH
# define VBOX_INTNET_TESTCASE_EMBEDDED_SWITCH
#endif
#ifndef VBOX_INTNET_TESTCASE_LOCALIPC
# define VBOX_INTNET_TESTCASE_LOCALIPC
#endif

#include "../../IntNetSwitch/VBoxIntNetSwitch.cpp"

#include <iprt/asm.h>
#include <iprt/env.h>
#include <iprt/localipc.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Valid embedded switch handle magic. */
#define VBOXINTNETSWITCHTEST_MAGIC       UINT32_C(0x49505357)
/** Invalidated embedded switch handle magic. */
#define VBOXINTNETSWITCHTEST_MAGIC_DEAD  UINT32_C(0x69707377)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Embedded production IntNet local IPC service state. */
typedef struct VBOXINTNETSWITCHTEST
{
    /** Magic value (VBOXINTNETSWITCHTEST_MAGIC). */
    uint32_t            u32Magic;
    /** Local IPC server. */
    RTLOCALIPCSERVER    hServer;
    /** Waitable acceptor thread. */
    RTTHREAD            hAcceptor;
    /** Set when the acceptor should stop. */
    bool volatile       fStop;
    /** Whether the R3-built IntNet switching core was initialized. */
    bool                fIntNetInited;
    /** Unique service name selected through VBOX_INTNET_R3_SVC_NAME. */
    char                szService[INTNET_R3_IPC_MAX_SERVICE_NAME];
} VBOXINTNETSWITCHTEST;
typedef VBOXINTNETSWITCHTEST *PVBOXINTNETSWITCHTEST;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * Accepts local IPC clients and starts the session worker.
 *
 * @returns VBox status code.
 * @param   hThreadSelf     Thread handle, unused.
 * @param   pvUser          Embedded switch state.
 */
static DECLCALLBACK(int) vboxIntNetSwitchTestAcceptThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    PVBOXINTNETSWITCHTEST pTest = (PVBOXINTNETSWITCHTEST)pvUser;
    AssertPtrReturn(pTest, VERR_INVALID_POINTER);
    AssertReturn(pTest->u32Magic == VBOXINTNETSWITCHTEST_MAGIC, VERR_INVALID_HANDLE);

    for (;;)
    {
        RTLOCALIPCSESSION hClient = NIL_RTLOCALIPCSESSION;
        int rc = RTLocalIpcServerListen(pTest->hServer, &hClient);
#ifdef RT_OS_WINDOWS
        if (rc == VERR_TRY_AGAIN)
        {
            RTThreadSleep(10);
            continue;
        }
#endif
        if (RT_FAILURE(rc))
            break;
        if (ASMAtomicReadBool(&pTest->fStop))
        {
            RTLocalIpcSessionClose(hClient);
            break;
        }
        if (!intnetR3TryReserveSlot(&g_DevExt.cRefs, g_DevExt.cMaxConnections))
        {
            RTLocalIpcSessionClose(hClient);
            RTThreadSleep(10);
            continue;
        }

        PSUPDRVSESSION pSession = (PSUPDRVSESSION)RTMemAllocZ(sizeof(*pSession));
        if (pSession == NULL)
        {
            RTLocalIpcSessionClose(hClient);
            intnetR3ReleaseConnectionSlot(&g_DevExt);
            continue;
        }

        pSession->pDevExt     = &g_DevExt;
        pSession->hIpcSession = hClient;
        pSession->hThread     = NIL_RTTHREAD;
        pSession->hIpcPokeThread = NIL_RTTHREAD;
        pSession->hIpcPokeEvt = NIL_RTSEMEVENT;
        pSession->fIpcPokeStopping = false;
        pSession->hIpcIoMtx   = NIL_RTSEMMUTEX;
        int rcThread = RTSemMutexCreate(&pSession->hIpcIoMtx);
        if (RT_SUCCESS(rcThread))
            rcThread = RTSemEventCreate(&pSession->hIpcPokeEvt);
        if (RT_FAILURE(rcThread))
        {
            intnetR3SessionDestroy(pSession);
            continue;
        }

        if (!intnetR3TryReserveSlots(&g_DevExt.cThreads, g_DevExt.cMaxThreads,
                                     INTNETR3_THREADS_PER_LOCALIPC_SESSION))
        {
            intnetR3SessionDestroy(pSession);
            RTThreadSleep(10);
            continue;
        }

        rcThread = RTThreadCreate(&pSession->hIpcPokeThread, intnetR3LocalIpcPokeThread, pSession, 0 /*cbStack*/,
                                  RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "DhcpPoke");
        if (RT_FAILURE(rcThread))
        {
            intnetR3SessionDestroy(pSession);
            ASMAtomicSubU32(&g_DevExt.cThreads, INTNETR3_THREADS_PER_LOCALIPC_SESSION);
            continue;
        }
        rcThread = RTThreadCreate(&pSession->hThread, intnetR3LocalIpcSessionThread, pSession, 0 /*cbStack*/,
                                  RTTHREADTYPE_IO, 0 /*fFlags*/, "DhcpIntNet");
        if (RT_FAILURE(rcThread))
        {
            intnetR3SessionDestroy(pSession);
            ASMAtomicDecU32(&g_DevExt.cThreads); /* Reserved request worker was never started. */
        }
    }
    return VINF_SUCCESS;
}


/**
 * Releases an embedded switch after all service threads have stopped.
 *
 * @param   pTest           Embedded switch state.
 */
static void vboxIntNetSwitchTestFree(PVBOXINTNETSWITCHTEST pTest)
{
    AssertPtrReturnVoid(pTest);
    Assert(pTest->hServer == NIL_RTLOCALIPCSERVER);
    Assert(pTest->hAcceptor == NIL_RTTHREAD);
    Assert(ASMAtomicReadU32(&g_DevExt.cRefs) == 0);

    if (pTest->fIntNetInited)
    {
        IntNetR0Term();
        pTest->fIntNetInited = false;
    }
    RTCritSectDelete(&g_DevExt.CritSect);
    RTEnvUnset("VBOX_INTNET_R3_SVC_NAME");

    pTest->u32Magic = VBOXINTNETSWITCHTEST_MAGIC_DEAD;
    RTMemFree(pTest);
}


/**
 * Starts the IntNet switch using a unique local IPC service name.
 *
 * @returns VBox status code.
 * @param   ppvHandle       Where to return the opaque embedded switch handle.
 */
extern "C" int VBoxIntNetSwitchTestStart(void **ppvHandle)
{
    AssertPtrReturn(ppvHandle, VERR_INVALID_POINTER);
    *ppvHandle = NULL;

    PVBOXINTNETSWITCHTEST pTest = (PVBOXINTNETSWITCHTEST)RTMemAllocZ(sizeof(*pTest));
    if (pTest == NULL)
        return VERR_NO_MEMORY;
    pTest->u32Magic  = VBOXINTNETSWITCHTEST_MAGIC;
    pTest->hServer   = NIL_RTLOCALIPCSERVER;
    pTest->hAcceptor = NIL_RTTHREAD;

    int rc = RTCritSectInit(&g_DevExt.CritSect);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pTest);
        return rc;
    }
    g_DevExt.pObjs = NULL;
    intnetR3InitLimits(&g_DevExt);

    rc = IntNetR0Init();
    if (RT_SUCCESS(rc))
        pTest->fIntNetInited = true;
    if (RT_SUCCESS(rc))
    {
        RTUUID Uuid;
        char szUuid[RTUUID_STR_LENGTH];
        rc = RTUuidCreate(&Uuid);
        if (RT_SUCCESS(rc))
            rc = RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));
        if (RT_SUCCESS(rc))
        {
            ssize_t const cch = RTStrPrintf2(pTest->szService, sizeof(pTest->szService), "tst-vboxnetdhcp-%s", szUuid);
            if (cch <= 0 || (size_t)cch >= sizeof(pTest->szService))
                rc = VERR_BUFFER_OVERFLOW;
        }
    }
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SVC_NAME", pTest->szService);
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcServerCreate(&pTest->hServer, pTest->szService, RTLOCALIPC_FLAGS_RESTRICT_TO_USER);
    if (RT_SUCCESS(rc))
        rc = RTThreadCreate(&pTest->hAcceptor, vboxIntNetSwitchTestAcceptThread, pTest, 0 /*cbStack*/,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "DhcpIntNet0");
    if (RT_SUCCESS(rc))
    {
        *ppvHandle = pTest;
        return VINF_SUCCESS;
    }

    if (pTest->hServer != NIL_RTLOCALIPCSERVER)
    {
        RTLocalIpcServerDestroy(pTest->hServer);
        pTest->hServer = NIL_RTLOCALIPCSERVER;
    }
    vboxIntNetSwitchTestFree(pTest);
    return rc;
}


/**
 * Stops and destroys the embedded production IntNet switch.
 *
 * @returns VBox status code.
 * @param   pvHandle        Opaque handle returned by VBoxIntNetSwitchTestStart.
 */
extern "C" int VBoxIntNetSwitchTestStop(void *pvHandle)
{
    PVBOXINTNETSWITCHTEST pTest = (PVBOXINTNETSWITCHTEST)pvHandle;
    AssertPtrReturn(pTest, VERR_INVALID_HANDLE);
    AssertReturn(pTest->u32Magic == VBOXINTNETSWITCHTEST_MAGIC, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    if (pTest->hServer != NIL_RTLOCALIPCSERVER)
    {
        ASMAtomicWriteBool(&pTest->fStop, true);

        /* Wake the blocking server listen with a local dummy connection. */
        RTLOCALIPCSESSION hTmp = NIL_RTLOCALIPCSESSION;
        int rc2 = RTLocalIpcSessionConnect(&hTmp, pTest->szService,
                                           RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
        if (RT_SUCCESS(rc2))
            RTLocalIpcSessionClose(hTmp);

        rc2 = RTLocalIpcServerDestroy(pTest->hServer);
        if (RT_FAILURE(rc2))
            rc = rc2;
        pTest->hServer = NIL_RTLOCALIPCSERVER;
    }

    if (pTest->hAcceptor != NIL_RTTHREAD)
    {
        int rcThread = VINF_SUCCESS;
        int rc2 = RTThreadWait(pTest->hAcceptor, RT_MS_5SEC, &rcThread);
        if (RT_FAILURE(rc2))
            return rc2;
        pTest->hAcceptor = NIL_RTTHREAD;
        if (RT_FAILURE(rcThread) && RT_SUCCESS(rc))
            rc = rcThread;
    }

    uint64_t const msStart = RTTimeMilliTS();
    while (   (   ASMAtomicReadU32(&g_DevExt.cRefs) != 0
               || ASMAtomicReadU32(&g_DevExt.cThreads) != 0)
           && RTTimeMilliTS() - msStart < RT_MS_5SEC)
        RTThreadSleep(1);
    if (   ASMAtomicReadU32(&g_DevExt.cRefs) != 0
        || ASMAtomicReadU32(&g_DevExt.cThreads) != 0)
        return VERR_TIMEOUT;

    vboxIntNetSwitchTestFree(pTest);
    return rc;
}
