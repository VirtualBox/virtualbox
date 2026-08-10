/* $Id: tstVBoxIntNetR3Switch.cpp 114962 2026-08-10 15:04:45Z andreas.loeffler@oracle.com $ */
/** @file
 * tstVBoxIntNetR3Switch - Self-contained testcase for R3 IntNet/IntNetSwitch communication.
 *
 * This RTTest auto-starts a standalone helper built from the production Local
 * IPC switch service and R3 wrapper sources.  This exercises the same process,
 * IPC, shared-memory, and switching boundaries used on non-Darwin hosts.
 *
 * The client side uses NetLib/IntNetIf (IntNetR3If*) to exercise the production
 * driverless path which talks to the R3 service via local IPC.
 *
 * Covered scenarios (non-exhaustive, but focused on IPC correctness):
 *  - A squatter occupying the legacy global endpoint cannot block service startup.
 *  - Missing-service detection, automatic process startup, client attach, and shared-memory mapping.
 *  - Basic broadcast send on interface A and receive on interface B (same network name).
 *  - Concurrent Wait/Abort semantics: a blocked IntNetR3IfWait is woken by IntNetR3IfWaitAbort.
 *  - Repeated send/wait notification registration without lost wakeups.
 *  - Network isolation: interface C on a different network name does not receive frames.
 *  - Connection, worker-thread, and aggregate shared-memory limits with recovery.
 *  - Idle-unverified, partial-request, and partial-reply read timeouts.
 *
 * The production R3 path on macOS uses XPC.  To keep this testcase self-contained
 * while still exercising the production local IPC implementation, its build
 * selects local IPC on all host platforms, including Darwin.
 *
 * Usage:
 *  - Build with VBOX_WITH_TESTCASES=1 and VBOX_WITH_INTNET_SERVICE_IN_R3_LOCALIPC defined.
 *    kmk -C src/VBox/NetworkServices/testcase tstVBoxIntNetR3Switch
 *  - Run the produced binary from the staged testcase directory.
 *
 * Security: No external inputs; all frames, names, and shared memory are local-only.
 */

/**
 * @page pg_tstVBoxIntNetR3Switch R3 IntNet/IntNetSwitch IPC self-contained testcase
 *
 * Purpose:
 *  - Validate the production IntNet R3 service local IPC path across a real
 *    client/service process boundary, including automatic service startup.
 *
 * Scope:
 *  - Automatic server startup, client attach/map via IntNetR3If* APIs.
 *  - Broadcast delivery between two interfaces on the same internal network name.
 *  - Isolation for a third interface on a different internal network name.
 *  - Concurrent Wait/Abort and repeated notification coverage for the async notification path.
 *  - Deferred local IPC notification output that cannot block IntNet delivery.
 *
 * Build:
 *  - Ensure VBOX_WITH_TESTCASES=1 and VBOX_WITH_INTNET_SERVICE_IN_R3_LOCALIPC are set.
 *  - Build just this testcase: kmk -C src/VBox/NetworkServices/testcase tstVBoxIntNetR3Switch
 *
 * Run:
 *  - Run tstVBoxIntNetR3Switch from out/<host>.<arch>/<type>/testcase or the
 *    platform-specific staged testcase directory.
 *
 * Notes:
 *  - Darwin production builds use XPC.  This testcase explicitly selects the same
 *    local IPC implementation used by the other supported hosts.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/asm.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/uuid.h>
#include <iprt/env.h>

#include <string.h>

#include <iprt/errcore.h>
#include <VBox/cdefs.h>
#include <VBox/err.h>
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
# include <VBox/intnetr3ipc.h>
#endif
#include "../NetLib/IntNetIf.h"

#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
# include <iprt/localipc.h>
#endif


/*********************************************************************************************************************************
*   Constants & Helpers                                                                                                          *
*********************************************************************************************************************************/
static RTTEST g_hTest = NIL_RTTEST;

#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
DECLHIDDEN(void) intnetR3IfTestSetWaitRace(INTNETIFCTX hIfCtx, RTSEMEVENT hReached, RTSEMEVENT hContinue);

# define TST_MAX_FRAME     1600
# define TST_WAIT_MS       1000
# define TST_STRESS_FRAMES 128
# define TST_READ_TIMEOUT_MS 1000

# define TST_ENV_MAX_CONNECTIONS "VBOX_INTNET_R3_TEST_MAX_CONNECTIONS"
# define TST_ENV_MAX_THREADS     "VBOX_INTNET_R3_TEST_MAX_THREADS"
# define TST_ENV_MAX_SHMEM       "VBOX_INTNET_R3_TEST_MAX_SHMEM"
# define TST_ENV_READ_TIMEOUT    "VBOX_INTNET_R3_TEST_READ_TIMEOUT_MS"
# define TST_ENV_REPLY_TIMEOUT   "VBOX_INTNET_R3_TEST_REPLY_TIMEOUT_MS"
# define TST_ENV_POKE_BLOCK_FILE "VBOX_INTNET_R3_TEST_POKE_BLOCK_FILE"

#define TST_CHECK(a_Expr) \
    do { if (!(a_Expr)) RTTestFailed(g_hTest, "%s:%u: %s", __FILE__, __LINE__, #a_Expr); } while (0)

#define TST_CHECK_RC_OK(a_rc) \
    do { int rc__ = (a_rc); if (RT_FAILURE(rc__)) RTTestFailed(g_hTest, "%s:%u: %s -> %Rrc", __FILE__, __LINE__, #a_rc, rc__); } while (0)

static int tstMakeUuidName(const char *pszPrefix, char *pszOut, size_t cbOut)
{
    RTUUID Uuid; char szUuid[RTUUID_STR_LENGTH];
    int rc = RTUuidCreate(&Uuid); if (RT_FAILURE(rc)) return rc;
    RTUuidToStr(&Uuid, szUuid, sizeof(szUuid));
    ssize_t cch = RTStrPrintf2(pszOut, cbOut, "%s-%s", pszPrefix, szUuid);
    return cch > 0 && (size_t)cch < cbOut ? VINF_SUCCESS : VERR_BUFFER_OVERFLOW;
}


/*********************************************************************************************************************************
*   External R3 Switch Service                                                                                                   *
*********************************************************************************************************************************/
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)

typedef struct TSTSWITCHSVC
{
    RTPROCESS hProcess;
    bool      fProcessReaped;
    bool      fTempDirCreated;
# ifdef RT_OS_WINDOWS
    RTLOCALIPCSERVER hLegacySquatter;
# else
    bool      fLegacySquatterCreated;
    bool      fRuntimeDirCreated;
    bool      fRuntimeDirChanged;
    bool      fRuntimeDirWasSet;
    char     *pszSavedRuntimeDir;
    char      szLegacySquatter[RTPATH_MAX];
    char      szRuntimeDir[RTPATH_MAX];
# endif
    char      szService[INTNET_R3_IPC_MAX_SERVICE_NAME];
    char      szExec[RTPATH_MAX];
    char      szTempDir[RTPATH_MAX];
    char      szPidFile[RTPATH_MAX];
    char      szLockFile[RTPATH_MAX];
} TSTSWITCHSVC;

static bool svcIsAbsent(int rc)
{
    return    rc == VERR_FILE_NOT_FOUND
           || rc == VERR_PATH_NOT_FOUND
           || rc == VERR_NET_CONNECTION_REFUSED
           || rc == VERR_PIPE_NOT_CONNECTED;
}


/** Returns whether @a rc is a transport-disconnect status from a rejected or closed peer. */
static bool svcIsDisconnected(int rc)
{
    return    rc == VERR_BROKEN_PIPE
           || rc == VERR_PIPE_NOT_CONNECTED
           || rc == VERR_NET_CONNECTION_RESET
           || rc == VERR_NET_CONNECTION_RESET_BY_PEER
           || rc == VERR_NET_CONNECTION_REFUSED;
}


static int svcReadPid(TSTSWITCHSVC *pSvc)
{
    RTFILE hFile = NIL_RTFILE;
    int rc = RTFileOpen(&hFile, pSvc->szPidFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
    {
        char szPid[32];
        size_t cbRead = 0;
        rc = RTFileRead(hFile, szPid, sizeof(szPid) - 1, &cbRead);
        int const rc2 = RTFileClose(hFile);
        if (RT_SUCCESS(rc))
            rc = rc2;
        if (RT_SUCCESS(rc))
        {
            szPid[cbRead] = '\0';
            uint32_t uPid = NIL_RTPROCESS;
            rc = RTStrToUInt32Full(szPid, 10, &uPid);
            if (RT_SUCCESS(rc))
            {
                if (uPid == NIL_RTPROCESS || uPid == RTProcSelf())
                    rc = VERR_INVALID_PARAMETER;
                else
                    pSvc->hProcess = uPid;
            }
        }
    }
    return rc;
}


static int svcStart(TSTSWITCHSVC *pSvc)
{
    RT_ZERO(*pSvc);
    pSvc->hProcess = NIL_RTPROCESS;
# ifdef RT_OS_WINDOWS
    pSvc->hLegacySquatter = NIL_RTLOCALIPCSERVER;
# endif

    int rc = RTPathTemp(pSvc->szTempDir, sizeof(pSvc->szTempDir));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pSvc->szTempDir, sizeof(pSvc->szTempDir), "tstVBoxIntNetR3Switch-XXXXXX");
    if (RT_SUCCESS(rc))
    {
        rc = RTDirCreateTemp(pSvc->szTempDir, 0700);
        if (RT_SUCCESS(rc))
            pSvc->fTempDirCreated = true;
    }
    if (RT_SUCCESS(rc))
        rc = tstMakeUuidName("tst-vbi", pSvc->szService, sizeof(pSvc->szService));
# ifndef RT_OS_WINDOWS
    if (RT_SUCCESS(rc))
    {
        pSvc->fRuntimeDirWasSet = RTEnvExist("XDG_RUNTIME_DIR");
        if (pSvc->fRuntimeDirWasSet)
        {
            pSvc->pszSavedRuntimeDir = RTEnvDupEx(RTENV_DEFAULT, "XDG_RUNTIME_DIR");
            if (!pSvc->pszSavedRuntimeDir)
                rc = VERR_NO_MEMORY;
        }
    }
    if (RT_SUCCESS(rc))
    {
        ssize_t const cch = RTStrPrintf2(pSvc->szRuntimeDir, sizeof(pSvc->szRuntimeDir),
                                         "/tmp/tst-vbi-runtime-%u-XXXXXX", (unsigned)RTProcSelf());
        if (cch <= 0 || (size_t)cch >= sizeof(pSvc->szRuntimeDir))
            rc = VERR_BUFFER_OVERFLOW;
        else
        {
            rc = RTDirCreateTemp(pSvc->szRuntimeDir, 0700);
            if (RT_SUCCESS(rc))
                pSvc->fRuntimeDirCreated = true;
        }
    }
    if (RT_SUCCESS(rc))
    {
        rc = RTEnvSet("XDG_RUNTIME_DIR", pSvc->szRuntimeDir);
        if (RT_SUCCESS(rc))
            pSvc->fRuntimeDirChanged = true;
    }
# endif
    if (RT_SUCCESS(rc))
    {
# ifdef RT_OS_WINDOWS
        rc = RTLocalIpcServerCreate(&pSvc->hLegacySquatter, pSvc->szService, 0 /*fFlags*/);
# else
        ssize_t const cch = RTStrPrintf2(pSvc->szLegacySquatter, sizeof(pSvc->szLegacySquatter),
                                         "/tmp/.iprt-localipc-%s", pSvc->szService);
        if (cch <= 0 || (size_t)cch >= sizeof(pSvc->szLegacySquatter))
            rc = VERR_BUFFER_OVERFLOW;
        else
        {
            RTDirRemove(pSvc->szLegacySquatter);
            rc = RTDirCreate(pSvc->szLegacySquatter, 0700, 0 /*fCreate*/);
            if (RT_SUCCESS(rc))
                pSvc->fLegacySquatterCreated = true;
        }
# endif
    }
    if (RT_SUCCESS(rc))
        rc = RTPathExecDir(pSvc->szExec, sizeof(pSvc->szExec));
    if (RT_SUCCESS(rc))
# ifdef RT_OS_WINDOWS
        rc = RTPathAppend(pSvc->szExec, sizeof(pSvc->szExec), "VBoxIntNetR3SwitchTestHelper.exe");
# else
        rc = RTPathAppend(pSvc->szExec, sizeof(pSvc->szExec), "VBoxIntNetR3SwitchTestHelper");
# endif
    if (RT_SUCCESS(rc))
        rc = RTStrCopy(pSvc->szPidFile, sizeof(pSvc->szPidFile), pSvc->szTempDir);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pSvc->szPidFile, sizeof(pSvc->szPidFile), "switch.pid");
    if (RT_SUCCESS(rc))
        rc = RTStrCopy(pSvc->szLockFile, sizeof(pSvc->szLockFile), pSvc->szTempDir);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(pSvc->szLockFile, sizeof(pSvc->szLockFile), "switch.lock");
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SVC_NAME", pSvc->szService);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SWITCH_EXE", pSvc->szExec);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SWITCH_PID_FILE", pSvc->szPidFile);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet("VBOX_INTNET_R3_SWITCH_LOCK_FILE", pSvc->szLockFile);
    if (RT_SUCCESS(rc))
    {
        RTLOCALIPCSESSION hExisting = NIL_RTLOCALIPCSESSION;
        rc = RTLocalIpcSessionConnect(&hExisting, pSvc->szService,
                                      RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
        if (RT_SUCCESS(rc))
        {
            RTLocalIpcSessionClose(hExisting);
            rc = VERR_ALREADY_EXISTS;
        }
        else if (svcIsAbsent(rc))
            rc = VINF_SUCCESS;
    }
    return rc;
}


static void svcCleanupEndpoint(TSTSWITCHSVC *pSvc)
{
    if (!pSvc->szService[0])
        return;

    RTLOCALIPCSESSION hExisting = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcSessionConnect(&hExisting, pSvc->szService,
                                      RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
    if (RT_SUCCESS(rc))
    {
        RTLocalIpcSessionClose(hExisting);
        RTTestFailed(g_hTest, "Switch IPC endpoint still accepts clients after service exit");
        return;
    }
    if (rc == VERR_NET_CONNECTION_REFUSED)
        RTTestFailed(g_hTest, "Switch IPC endpoint pathname was left behind after service exit");
    else if (!svcIsAbsent(rc))
    {
        RTTestFailed(g_hTest, "Checking switch IPC endpoint failed: %Rrc", rc);
        return;
    }

    RTLOCALIPCSERVER hCleanup = NIL_RTLOCALIPCSERVER;
    rc = RTLocalIpcServerCreate(&hCleanup, pSvc->szService, RTLOCALIPC_FLAGS_RESTRICT_TO_USER);
    if (RT_SUCCESS(rc))
        RTLocalIpcServerDestroy(hCleanup);
    else
        RTTestFailed(g_hTest, "Cleaning switch IPC endpoint failed: %Rrc", rc);
}


static void svcStop(TSTSWITCHSVC *pSvc)
{
    if (   !pSvc->fProcessReaped
        && pSvc->hProcess == NIL_RTPROCESS
        && pSvc->szPidFile[0])
        svcReadPid(pSvc);

    if (pSvc->hProcess != NIL_RTPROCESS)
    {
        RTPROCSTATUS Status;
        RT_ZERO(Status);
        int rc = VERR_PROCESS_RUNNING;
        uint64_t const msStart = RTTimeMilliTS();
        while (   rc == VERR_PROCESS_RUNNING
               && RTTimeMilliTS() - msStart < RT_MS_5SEC)
        {
            rc = RTProcWait(pSvc->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &Status);
            if (rc == VERR_PROCESS_RUNNING)
                RTThreadSleep(10);
        }
        if (rc == VERR_PROCESS_RUNNING)
        {
            RTTestFailed(g_hTest, "Switch helper did not exit after its last client disconnected");
            int const rcTerm = RTProcTerminate(pSvc->hProcess);
            if (RT_SUCCESS(rcTerm))
                rc = RTProcWait(pSvc->hProcess, RTPROCWAIT_FLAGS_BLOCK, &Status);
            else
                rc = rcTerm;
        }
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Waiting for switch helper failed: %Rrc", rc);
        else if (   Status.enmReason != RTPROCEXITREASON_NORMAL
                 || Status.iStatus != RTEXITCODE_SUCCESS)
            RTTestFailed(g_hTest, "Switch helper exit reason/status: %d/%d", Status.enmReason, Status.iStatus);
        if (RT_SUCCESS(rc))
            pSvc->fProcessReaped = true;
        pSvc->hProcess = NIL_RTPROCESS;
    }

    svcCleanupEndpoint(pSvc);

    RTEnvUnset("VBOX_INTNET_R3_SWITCH_LOCK_FILE");
    RTEnvUnset("VBOX_INTNET_R3_SWITCH_PID_FILE");
    RTEnvUnset("VBOX_INTNET_R3_SWITCH_EXE");
    RTEnvUnset("VBOX_INTNET_R3_SVC_NAME");
    RTEnvUnset(TST_ENV_MAX_CONNECTIONS);
    RTEnvUnset(TST_ENV_MAX_THREADS);
    RTEnvUnset(TST_ENV_MAX_SHMEM);
    RTEnvUnset(TST_ENV_READ_TIMEOUT);
    RTEnvUnset(TST_ENV_REPLY_TIMEOUT);
    RTEnvUnset(TST_ENV_POKE_BLOCK_FILE);

# ifndef RT_OS_WINDOWS
    if (pSvc->fRuntimeDirChanged)
    {
        int const rc = pSvc->fRuntimeDirWasSet
                     ? RTEnvSet("XDG_RUNTIME_DIR", pSvc->pszSavedRuntimeDir)
                     : RTEnvUnset("XDG_RUNTIME_DIR");
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Restoring XDG_RUNTIME_DIR failed: %Rrc", rc);
        pSvc->fRuntimeDirChanged = false;
    }
    RTStrFree(pSvc->pszSavedRuntimeDir);
    pSvc->pszSavedRuntimeDir = NULL;
# endif

# ifdef RT_OS_WINDOWS
    if (pSvc->hLegacySquatter != NIL_RTLOCALIPCSERVER)
    {
        int const rc = RTLocalIpcServerDestroy(pSvc->hLegacySquatter);
        if (rc != VINF_OBJECT_DESTROYED)
            RTTestFailed(g_hTest, "Destroying legacy endpoint squatter failed: %Rrc", rc);
        pSvc->hLegacySquatter = NIL_RTLOCALIPCSERVER;
    }
# else
    if (pSvc->fLegacySquatterCreated)
    {
        int const rc = RTDirRemove(pSvc->szLegacySquatter);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Removing legacy endpoint squatter failed: %Rrc", rc);
        else
            pSvc->fLegacySquatterCreated = false;
    }
    if (pSvc->fRuntimeDirCreated)
    {
        int const rc = RTDirRemove(pSvc->szRuntimeDir);
        if (RT_FAILURE(rc) && rc != VERR_PATH_NOT_FOUND && rc != VERR_FILE_NOT_FOUND)
            RTTestFailed(g_hTest, "Removing local IPC runtime directory failed: %Rrc", rc);
        else
            pSvc->fRuntimeDirCreated = false;
    }
# endif

    if (pSvc->szPidFile[0])
        RTFileDelete(pSvc->szPidFile);
    if (pSvc->szLockFile[0])
        RTFileDelete(pSvc->szLockFile);
    if (pSvc->fTempDirCreated)
    {
        int const rc = RTDirRemove(pSvc->szTempDir);
        if (RT_FAILURE(rc) && rc != VERR_PATH_NOT_FOUND && rc != VERR_FILE_NOT_FOUND)
            RTTestFailed(g_hTest, "Removing switch helper temp directory failed: %Rrc", rc);
        else
            pSvc->fTempDirCreated = false;
    }
}

#endif /* !RT_OS_DARWIN || VBOX_INTNET_TESTCASE_LOCALIPC */


/*********************************************************************************************************************************
*   Simple client-side RX collector                                                                                              *
*********************************************************************************************************************************/
typedef struct TSTRXCOLLECT
{
    INTNETIFCTX hIf;
    RTSEMEVENT  hEvt;
    RTTHREAD    hThread;
    uint32_t    cFrames;
    uint8_t     abLast[TST_MAX_FRAME];
    uint32_t    cbLast;
} TSTRXCOLLECT;

static DECLCALLBACK(void) tstRxCb(void *pvUser, void *pvFrame, uint32_t cbFrame)
{
    TSTRXCOLLECT *p = (TSTRXCOLLECT *)pvUser;
    if (cbFrame > sizeof(p->abLast))
        cbFrame = sizeof(p->abLast);
    memcpy(p->abLast, pvFrame, cbFrame);
    p->cbLast = cbFrame;
    ASMAtomicIncU32(&p->cFrames);
    RTSemEventSignal(p->hEvt);
}

static DECLCALLBACK(int) tstRxThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    TSTRXCOLLECT *p = (TSTRXCOLLECT *)pvUser;
    return IntNetR3IfPumpPkts(p->hIf, tstRxCb, p, NULL, NULL);
}

static int tstRxStart(TSTRXCOLLECT *p, INTNETIFCTX hIf)
{
    RT_ZERO(*p); p->hIf = hIf; p->hEvt = NIL_RTSEMEVENT; p->hThread = NIL_RTTHREAD;
    int rc = RTSemEventCreate(&p->hEvt); if (RT_FAILURE(rc)) return rc;
    return RTThreadCreate(&p->hThread, tstRxThread, p, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "IntNetRx");
}

static bool tstRxStop(TSTRXCOLLECT *p)
{
    if (p->hIf)
    {
        int const rc = IntNetR3IfWaitAbort(p->hIf);
        if (RT_FAILURE(rc))
            RTTestFailed(g_hTest, "Failed to abort receive thread wait: %Rrc", rc);
    }
    if (p->hThread != NIL_RTTHREAD)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        int const rc = RTThreadWait(p->hThread, RT_MS_5SEC, &rcThread);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "Receive thread did not terminate: %Rrc", rc);
            return false; /* The thread may still be using the event and interface. */
        }
        if (rcThread != VERR_SEM_DESTROYED)
            RTTestFailed(g_hTest, "Receive thread returned %Rrc, expected %Rrc", rcThread, VERR_SEM_DESTROYED);
        p->hThread = NIL_RTTHREAD;
    }
    if (p->hEvt != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(p->hEvt);
        p->hEvt = NIL_RTSEMEVENT;
    }
    return true;
}


/** Joins a receive thread after svcStop has forced its IPC connection closed. */
static bool tstRxJoinAfterServiceStop(TSTRXCOLLECT *p)
{
    if (p->hThread != NIL_RTTHREAD)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        int const rc = RTThreadWait(p->hThread, RT_INDEFINITE_WAIT, &rcThread);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "Joining receive thread after service stop failed: %Rrc", rc);
            return false;
        }
        if (rcThread != VERR_SEM_DESTROYED)
            RTTestFailed(g_hTest, "Receive thread returned %Rrc after service stop, expected %Rrc",
                         rcThread, VERR_SEM_DESTROYED);
        p->hThread = NIL_RTTHREAD;
    }
    if (p->hEvt != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(p->hEvt);
        p->hEvt = NIL_RTSEMEVENT;
    }
    return true;
}


static DECLCALLBACK(int) tstWaitThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    return IntNetR3IfWait((INTNETIFCTX)pvUser, RT_INDEFINITE_WAIT);
}

static bool tstConcurrentWaitAbort(INTNETIFCTX hIf, PRTTHREAD phThread)
{
    *phThread = NIL_RTTHREAD;
    int rc = RTThreadCreate(phThread, tstWaitThread, hIf, 0 /*cbStack*/, RTTHREADTYPE_IO,
                            RTTHREADFLAGS_WAITABLE, "IntNetWait");
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Failed to create wait thread: %Rrc", rc);
        return true;
    }

    /* Once the client is sleeping on its receive event, its WAIT request has
       already been written to the ordered IPC stream.  A following ABORT must
       therefore cancel a registered wait, not merely win a scheduling race. */
    uint64_t const msStart = RTTimeMilliTS();
    RTTHREADSTATE enmState;
    do
    {
        enmState = RTThreadGetState(*phThread);
        if (enmState == RTTHREADSTATE_EVENT || enmState == RTTHREADSTATE_TERMINATED)
            break;
        RTThreadSleep(1);
    } while (RTTimeMilliTS() - msStart < RT_MS_5SEC);

    if (enmState != RTTHREADSTATE_EVENT)
        RTTestFailed(g_hTest, "Wait thread did not block on the receive event (state %d)", enmState);

    rc = IntNetR3IfWaitAbort(hIf);
    TST_CHECK_RC_OK(rc);

    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    rc = RTThreadWait(*phThread, RT_MS_5SEC, &rcThread);
    TST_CHECK_RC_OK(rc);
    if (RT_FAILURE(rc))
        return false; /* The thread may still be using the interface. */
    *phThread = NIL_RTTHREAD;
    if (RT_SUCCESS(rc) && rcThread != VERR_SEM_DESTROYED)
        RTTestFailed(g_hTest, "Wait thread returned %Rrc, expected %Rrc", rcThread, VERR_SEM_DESTROYED);

    rc = IntNetR3IfWait(hIf, 0 /*cMillies*/);
    if (rc != VERR_SEM_DESTROYED)
        RTTestFailed(g_hTest, "Wait after abort returned %Rrc, expected %Rrc", rc, VERR_SEM_DESTROYED);
    return true;
}


typedef struct TSTWAITRACE
{
    RTTHREAD   hThread;
    RTSEMEVENT hReached;
    RTSEMEVENT hContinue;
} TSTWAITRACE;


/** Exercises Abort completing between Wait's initial and serialized no-more-waits checks. */
static bool tstInverseWaitAbortRace(INTNETIFCTX hIf, TSTWAITRACE *pRace)
{
    RT_ZERO(*pRace);
    pRace->hThread   = NIL_RTTHREAD;
    pRace->hReached  = NIL_RTSEMEVENT;
    pRace->hContinue = NIL_RTSEMEVENT;

    int rc = RTSemEventCreate(&pRace->hReached);
    if (RT_SUCCESS(rc))
        rc = RTSemEventCreate(&pRace->hContinue);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Creating inverse-race events failed: %Rrc", rc);
        if (pRace->hReached != NIL_RTSEMEVENT)
            RTSemEventDestroy(pRace->hReached);
        pRace->hReached = NIL_RTSEMEVENT;
        return true;
    }

    intnetR3IfTestSetWaitRace(hIf, pRace->hReached, pRace->hContinue);
    rc = RTThreadCreate(&pRace->hThread, tstWaitThread, hIf, 0 /*cbStack*/, RTTHREADTYPE_IO,
                        RTTHREADFLAGS_WAITABLE, "IntNetRace");
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Creating inverse-race wait thread failed: %Rrc", rc);
        intnetR3IfTestSetWaitRace(NULL, NIL_RTSEMEVENT, NIL_RTSEMEVENT);
        RTSemEventDestroy(pRace->hContinue);
        RTSemEventDestroy(pRace->hReached);
        pRace->hContinue = NIL_RTSEMEVENT;
        pRace->hReached = NIL_RTSEMEVENT;
        return true;
    }

    rc = RTSemEventWait(pRace->hReached, RT_MS_5SEC);
    TST_CHECK_RC_OK(rc);

    int const rcAbort = IntNetR3IfWaitAbort(hIf);
    TST_CHECK_RC_OK(rcAbort);
    TST_CHECK_RC_OK(RTSemEventSignal(pRace->hContinue));
    intnetR3IfTestSetWaitRace(NULL, NIL_RTSEMEVENT, NIL_RTSEMEVENT);

    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    rc = RTThreadWait(pRace->hThread, RT_MS_5SEC, &rcThread);
    TST_CHECK_RC_OK(rc);
    if (RT_FAILURE(rc))
        return false;
    pRace->hThread = NIL_RTTHREAD;
    if (rcThread != VERR_SEM_DESTROYED)
        RTTestFailed(g_hTest, "Inverse-race wait returned %Rrc, expected %Rrc", rcThread, VERR_SEM_DESTROYED);

    rc = IntNetR3IfWait(hIf, 0 /*cMillies*/);
    if (rc != VERR_SEM_DESTROYED)
        RTTestFailed(g_hTest, "Post-abort wait returned %Rrc, expected %Rrc", rc, VERR_SEM_DESTROYED);

    RTSemEventDestroy(pRace->hContinue);
    RTSemEventDestroy(pRace->hReached);
    pRace->hContinue = NIL_RTSEMEVENT;
    pRace->hReached = NIL_RTSEMEVENT;
    return true;
}


/** Joins and releases an inverse-race waiter after the service has been stopped. */
static bool tstWaitRaceJoinAfterServiceStop(TSTWAITRACE *pRace)
{
    if (pRace->hThread != NIL_RTTHREAD)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        int const rc = RTThreadWait(pRace->hThread, RT_INDEFINITE_WAIT, &rcThread);
        if (RT_FAILURE(rc))
        {
            RTTestFailed(g_hTest, "Joining inverse-race wait after service stop failed: %Rrc", rc);
            return false;
        }
        if (rcThread != VERR_SEM_DESTROYED)
            RTTestFailed(g_hTest, "Inverse-race wait returned %Rrc after service stop, expected %Rrc",
                         rcThread, VERR_SEM_DESTROYED);
        pRace->hThread = NIL_RTTHREAD;
    }
    if (pRace->hContinue != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pRace->hContinue);
        pRace->hContinue = NIL_RTSEMEVENT;
    }
    if (pRace->hReached != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pRace->hReached);
        pRace->hReached = NIL_RTSEMEVENT;
    }
    return true;
}

static int tstSendFrame(INTNETIFCTX hIf, uint8_t bFill)
{
    INTNETFRAME Frame;
    int rc = IntNetR3IfQueryOutputFrame(hIf, 64, &Frame);
    if (RT_SUCCESS(rc))
    {
        memset(Frame.pvFrame, bFill, 64);
        rc = IntNetR3IfOutputFrameCommit(hIf, &Frame);
    }
    return rc;
}


/** Sends one frame from a worker so the test can detect callback blocking. */
static DECLCALLBACK(int) tstSendThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    return tstSendFrame((INTNETIFCTX)pvUser, 0x5a);
}


/** Ensures a blocked notification writer cannot stall the IntNet delivery callback. */
static void tstDeferredNotification(void)
{
    RTTestSub(g_hTest, "Deferred receive notification");

    TSTSWITCHSVC Svc;
    int rc = svcStart(&Svc);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Preparing switch helper failed: %Rrc", rc);
        svcStop(&Svc);
        return;
    }

    char szBlockFile[RTPATH_MAX];
    rc = RTStrCopy(szBlockFile, sizeof(szBlockFile), Svc.szTempDir);
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szBlockFile, sizeof(szBlockFile), "poke-block");
    RTFILE hBlockFile = NIL_RTFILE;
    bool fBlockFileCreated = false;
    if (RT_SUCCESS(rc))
    {
        rc = RTFileOpen(&hBlockFile, szBlockFile, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_ALL
                                                | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
        if (RT_SUCCESS(rc))
        {
            fBlockFileCreated = true;
            rc = RTFileClose(hBlockFile);
        }
    }
    TST_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
        rc = RTEnvSet(TST_ENV_POKE_BLOCK_FILE, szBlockFile);
    TST_CHECK_RC_OK(rc);

    INTNETIFCTX hTx = NULL;
    INTNETIFCTX hSlow = NULL;
    RTTHREAD hWaitThread = NIL_RTTHREAD;
    RTTHREAD hSendThread = NIL_RTTHREAD;
    bool fNotificationTriggered = false;
    bool fServiceStopped = false;
    do
    {
        if (RT_FAILURE(rc))
            break;

        char szNetwork[128];
        RTTESTI_CHECK_RC_OK_BREAK(tstMakeUuidName("tst-IntNet-deferred", szNetwork, sizeof(szNetwork)));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hTx, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));
        RTTESTI_CHECK_RC_OK_BREAK(svcReadPid(&Svc));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hSlow, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hTx, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hSlow, true));

        RTTESTI_CHECK_RC_OK_BREAK(RTThreadCreate(&hWaitThread, tstWaitThread, hSlow, 0 /*cbStack*/,
                                                  RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "IntNetSlow"));
        uint64_t const msWaitStart = RTTimeMilliTS();
        RTTHREADSTATE enmState;
        do
        {
            enmState = RTThreadGetState(hWaitThread);
            if (enmState == RTTHREADSTATE_EVENT || enmState == RTTHREADSTATE_TERMINATED)
                break;
            RTThreadSleep(1);
        } while (RTTimeMilliTS() - msWaitStart < RT_MS_5SEC);
        if (enmState != RTTHREADSTATE_EVENT)
        {
            RTTestFailed(g_hTest, "Slow receive waiter did not block on its event (state %d)", enmState);
            break;
        }

        /* This ordered synchronous request proves that the preceding asynchronous
           WAIT has reached the service and armed its delivery callback. */
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hSlow, true));

        RTTESTI_CHECK_RC_OK_BREAK(RTThreadCreate(&hSendThread, tstSendThread, hTx, 0 /*cbStack*/,
                                                  RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "IntNetSend"));
        fNotificationTriggered = true;
        int rcSend = VERR_IPE_UNINITIALIZED_STATUS;
        rc = RTThreadWait(hSendThread, RT_MS_5SEC, &rcSend);
        if (rc == VERR_TIMEOUT)
            RTTestFailed(g_hTest, "Frame delivery remained blocked behind the notification writer");
        else
        {
            TST_CHECK_RC_OK(rc);
            if (RT_SUCCESS(rc))
            {
                hSendThread = NIL_RTTHREAD;
                TST_CHECK_RC_OK(rcSend);
            }
        }
    } while (0);

    if (fNotificationTriggered && hWaitThread != NIL_RTTHREAD)
    {
        int rcWait = VERR_IPE_UNINITIALIZED_STATUS;
        int const rc2 = RTThreadWait(hWaitThread, 100 /*cMillies*/, &rcWait);
        if (rc2 != VERR_TIMEOUT)
        {
            RTTestFailed(g_hTest, "Notification block gate was ineffective: %Rrc (%Rrc)", rc2, rcWait);
            if (RT_SUCCESS(rc2))
                hWaitThread = NIL_RTTHREAD;
        }
    }

    if (fBlockFileCreated)
    {
        int const rc2 = RTFileDelete(szBlockFile);
        TST_CHECK_RC_OK(rc2);
        if (   RT_SUCCESS(rc2)
            || rc2 == VERR_FILE_NOT_FOUND
            || rc2 == VERR_PATH_NOT_FOUND)
            fBlockFileCreated = false;
    }
    if (fBlockFileCreated)
    {
        /* The helper gate is still active, so synchronous cleanup requests may
           block as well.  Stop the helper before joining either client thread. */
        svcStop(&Svc);
        fServiceStopped = true;

        int const rc2 = RTFileDelete(szBlockFile);
        if (   RT_SUCCESS(rc2)
            || rc2 == VERR_FILE_NOT_FOUND
            || rc2 == VERR_PATH_NOT_FOUND)
            fBlockFileCreated = false;
    }

    if (hSendThread != NIL_RTTHREAD)
    {
        int rcSend = VERR_IPE_UNINITIALIZED_STATUS;
        rc = RTThreadWait(hSendThread, RT_MS_5SEC, &rcSend);
        TST_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
        {
            if (!fServiceStopped)
            {
                svcStop(&Svc);
                fServiceStopped = true;
            }
            rc = RTThreadWait(hSendThread, RT_INDEFINITE_WAIT, &rcSend);
        }
        if (RT_SUCCESS(rc))
        {
            hSendThread = NIL_RTTHREAD;
            if (!fServiceStopped)
                TST_CHECK_RC_OK(rcSend);
        }
    }
    if (hWaitThread != NIL_RTTHREAD)
    {
        int rcWait = VERR_IPE_UNINITIALIZED_STATUS;
        rc = RTThreadWait(hWaitThread, RT_MS_5SEC, &rcWait);
        if (rc == VERR_TIMEOUT)
        {
            if (fNotificationTriggered)
                RTTestFailed(g_hTest, "Slow receive waiter was not notified after releasing the writer");
            if (!fServiceStopped)
            {
                svcStop(&Svc);
                fServiceStopped = true;
            }
            rc = RTThreadWait(hWaitThread, RT_INDEFINITE_WAIT, &rcWait);
        }
        TST_CHECK_RC_OK(rc);
        if (RT_SUCCESS(rc))
        {
            hWaitThread = NIL_RTTHREAD;
            if (!fServiceStopped && rcWait != VINF_SUCCESS)
                RTTestFailed(g_hTest, "Slow receive waiter returned %Rrc, expected %Rrc", rcWait, VINF_SUCCESS);
        }
    }

    if (hSlow)
        IntNetR3IfDestroy(hSlow);
    if (hTx)
        IntNetR3IfDestroy(hTx);
    svcStop(&Svc);
}


/** Retries interface creation while a just-closed worker releases its slot. */
static int tstCreateIfRetry(PINTNETIFCTX phIf, const char *pszNetwork, uint32_t cbSend, uint32_t cbRecv)
{
    uint64_t const msStart = RTTimeMilliTS();
    int rc;
    do
    {
        rc = IntNetR3IfCreateEx(phIf, pszNetwork, kIntNetTrunkType_WhateverNone, "", cbSend, cbRecv, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
            return rc;
        Assert(*phIf == NULL);
        RTThreadSleep(10);
    } while (RTTimeMilliTS() - msStart < RT_MS_5SEC);
    return rc;
}


/** Exercises either the connection or worker-thread admission limit. */
static void tstSessionSlotLimit(const char *pszSubTest, const char *pszMaxConnections, const char *pszMaxThreads)
{
    RTTestSub(g_hTest, pszSubTest);

    TSTSWITCHSVC Svc;
    int rc = svcStart(&Svc);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Preparing switch helper failed: %Rrc", rc);
        svcStop(&Svc);
        return;
    }

    TST_CHECK_RC_OK(RTEnvSet(TST_ENV_MAX_CONNECTIONS, pszMaxConnections));
    TST_CHECK_RC_OK(RTEnvSet(TST_ENV_MAX_THREADS, pszMaxThreads));

    char szNetwork[128];
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-limit", szNetwork, sizeof(szNetwork)));
    INTNETIFCTX hAnchor = NULL;
    INTNETIFCTX hHolder = NULL;
    INTNETIFCTX hRejected = NULL;
    INTNETIFCTX hRetry = NULL;
    do
    {
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hAnchor, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));
        RTTESTI_CHECK_RC_OK_BREAK(svcReadPid(&Svc));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hHolder, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));

        rc = IntNetR3IfCreateEx(&hRejected, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                _64K, _64K, 0 /*fFlags*/);
        if (RT_SUCCESS(rc) || hRejected != NULL)
            RTTestFailed(g_hTest, "Over-limit interface unexpectedly succeeded: %Rrc (%p)", rc, hRejected);
        else if (!svcIsDisconnected(rc))
            RTTestFailed(g_hTest, "Over-limit interface returned unexpected status: %Rrc", rc);

        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hAnchor, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hAnchor, false));

        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfDestroy(hHolder));
        hHolder = NULL;
        RTTESTI_CHECK_RC_OK_BREAK(tstCreateIfRetry(&hRetry, szNetwork, _64K, _64K));
    } while (0);

    if (hRejected)
        IntNetR3IfDestroy(hRejected);
    if (hRetry)
        IntNetR3IfDestroy(hRetry);
    if (hHolder)
        IntNetR3IfDestroy(hHolder);
    if (hAnchor)
        IntNetR3IfDestroy(hAnchor);
    svcStop(&Svc);
}


/** Exercises the service-wide shared-memory byte quota and release accounting. */
static void tstAggregateShMemLimit(void)
{
    RTTestSub(g_hTest, "Aggregate shared-memory limit");

    TSTSWITCHSVC Svc;
    int rc = svcStart(&Svc);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Preparing switch helper failed: %Rrc", rc);
        svcStop(&Svc);
        return;
    }
    TST_CHECK_RC_OK(RTEnvSet(TST_ENV_MAX_SHMEM, "1048576"));

    char szNetwork[128];
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-shmem", szNetwork, sizeof(szNetwork)));
    INTNETIFCTX hAnchor = NULL;
    INTNETIFCTX hLarge = NULL;
    INTNETIFCTX hRejected = NULL;
    INTNETIFCTX hRetry = NULL;
    do
    {
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hAnchor, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));
        RTTESTI_CHECK_RC_OK_BREAK(svcReadPid(&Svc));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hLarge, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _256K, _256K, 0 /*fFlags*/));

        rc = IntNetR3IfCreateEx(&hRejected, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                _256K, _256K, 0 /*fFlags*/);
        if (rc != VERR_OUT_OF_RESOURCES)
            RTTestFailed(g_hTest, "Over-quota interface returned %Rrc, expected %Rrc", rc, VERR_OUT_OF_RESOURCES);
        if (hRejected != NULL)
            RTTestFailed(g_hTest, "Over-quota interface unexpectedly returned a context");

        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfDestroy(hLarge));
        hLarge = NULL;
        RTTESTI_CHECK_RC_OK_BREAK(tstCreateIfRetry(&hRetry, szNetwork, _256K, _256K));
    } while (0);

    if (hRejected)
        IntNetR3IfDestroy(hRejected);
    if (hRetry)
        IntNetR3IfDestroy(hRetry);
    if (hLarge)
        IntNetR3IfDestroy(hLarge);
    if (hAnchor)
        IntNetR3IfDestroy(hAnchor);
    svcStop(&Svc);
}


/** Sends a deliberately incomplete request and waits for the service to close it. */
static void tstServerReadTimeoutOne(TSTSWITCHSVC *pSvc, unsigned iPart)
{
    RTLOCALIPCSESSION hSession = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcSessionConnect(&hSession, pSvc->szService,
                                      RTLOCALIPC_C_FLAGS_ALLOW_IDENTIFICATION
                                    | RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
    TST_CHECK_RC_OK(rc);
    if (RT_FAILURE(rc))
        return;

    if (iPart == 1)
    {
        uint8_t bPartial = 0;
        rc = RTLocalIpcSessionWrite(hSession, &bPartial, sizeof(bPartial));
    }
    else if (iPart == 2 || iPart == 3)
    {
        INTNETR3IPCREQHDR Hdr;
        Hdr.u32Magic   = INTNET_R3_IPC_REQ_MAGIC;
        Hdr.u16Version = INTNET_R3_IPC_VERSION;
        Hdr.cbHdr      = sizeof(Hdr);
        Hdr.cbReq      = sizeof(INTNETIFCLOSEREQ);
        Hdr.uOperation = VMMR0_DO_INTNET_IF_CLOSE;
        rc = RTLocalIpcSessionWrite(hSession, &Hdr, sizeof(Hdr));
        if (RT_SUCCESS(rc))
        {
            uint8_t bPartial = 0;
            rc = RTLocalIpcSessionWrite(hSession, &bPartial, sizeof(bPartial));
        }
    }
    if (RT_SUCCESS(rc) && iPart != 0)
        rc = RTLocalIpcSessionFlush(hSession);

    if (RT_SUCCESS(rc) && iPart == 3)
    {
        uint64_t const msStart = RTTimeMilliTS();
        do
        {
            rc = RTLocalIpcSessionWaitForData(hSession, 0 /*cMillies*/);
            if (svcIsDisconnected(rc))
                break;
            if (rc != VERR_TIMEOUT)
                break;
            RTThreadSleep(TST_READ_TIMEOUT_MS / 4);
            uint8_t bPartial = 0;
            rc = RTLocalIpcSessionWrite(hSession, &bPartial, sizeof(bPartial));
            if (RT_SUCCESS(rc))
                rc = RTLocalIpcSessionFlush(hSession);
        } while (   RT_SUCCESS(rc)
                 && RTTimeMilliTS() - msStart < TST_READ_TIMEOUT_MS * 2);
    }
    else if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionWaitForData(hSession, RT_MS_5SEC);

    if (!svcIsDisconnected(rc))
        RTTestFailed(g_hTest, "Incomplete request part %u returned %Rrc, expected a disconnect", iPart, rc);
    RTLocalIpcSessionClose(hSession);
}


/** Ensures a complete but unsuccessful pre-OPEN request does not disable the idle deadline. */
static void tstServerPreOpenIdleTimeout(TSTSWITCHSVC *pSvc)
{
    RTLOCALIPCSESSION hSession = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcSessionConnect(&hSession, pSvc->szService,
                                      RTLOCALIPC_C_FLAGS_ALLOW_IDENTIFICATION
                                    | RTLOCALIPC_C_FLAGS_RESTRICT_TO_USER);
    TST_CHECK_RC_OK(rc);
    if (RT_FAILURE(rc))
        return;

    INTNETIFCLOSEREQ Req;
    RT_ZERO(Req);
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.hIf          = INTNET_HANDLE_INVALID;

    INTNETR3IPCREQHDR Hdr;
    Hdr.u32Magic   = INTNET_R3_IPC_REQ_MAGIC;
    Hdr.u16Version = INTNET_R3_IPC_VERSION;
    Hdr.cbHdr      = sizeof(Hdr);
    Hdr.cbReq      = sizeof(Req);
    Hdr.uOperation = VMMR0_DO_INTNET_IF_CLOSE;
    rc = RTLocalIpcSessionWrite(hSession, &Hdr, sizeof(Hdr));
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionWrite(hSession, &Req, sizeof(Req));
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionFlush(hSession);

    INTNETR3IPCREPLYHDR ReplyHdr;
    RT_ZERO(ReplyHdr);
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionRead(hSession, &ReplyHdr, sizeof(ReplyHdr), NULL /*pcbRead*/);
    if (RT_SUCCESS(rc))
    {
        if (   ReplyHdr.u32Magic != INTNET_R3_IPC_REPLY_MAGIC
            || ReplyHdr.u16Version != INTNET_R3_IPC_VERSION
            || ReplyHdr.cbHdr != sizeof(ReplyHdr)
            || ReplyHdr.cbReply != sizeof(Req)
            || ReplyHdr.cbShMemName != 0
            || ReplyHdr.cbShMem != 0)
            rc = VERR_INVALID_PARAMETER;
    }
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionRead(hSession, &Req, sizeof(Req), NULL /*pcbRead*/);
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionWaitForData(hSession, RT_MS_5SEC);
    if (!svcIsDisconnected(rc))
        RTTestFailed(g_hTest, "Idle pre-OPEN session returned %Rrc, expected a disconnect", rc);
    RTLocalIpcSessionClose(hSession);
}


/** Exercises initial-byte and absolute request-frame deadlines. */
static void tstServerReadTimeout(void)
{
    RTTestSub(g_hTest, "Server read timeout");

    TSTSWITCHSVC Svc;
    int rc = svcStart(&Svc);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Preparing switch helper failed: %Rrc", rc);
        svcStop(&Svc);
        return;
    }
    TST_CHECK_RC_OK(RTEnvSet(TST_ENV_READ_TIMEOUT, "1000"));

    char szNetwork[128];
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-timeout", szNetwork, sizeof(szNetwork)));
    INTNETIFCTX hAnchor = NULL;
    INTNETIFCTX hFresh = NULL;
    do
    {
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hAnchor, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));
        RTTESTI_CHECK_RC_OK_BREAK(svcReadPid(&Svc));

        /* Established interfaces may be idle for longer than the frame timeout. */
        RTThreadSleep(TST_READ_TIMEOUT_MS * 2);
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hAnchor, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hAnchor, false));

        tstServerReadTimeoutOne(&Svc, 0 /* no bytes */);
        tstServerReadTimeoutOne(&Svc, 1 /* partial header */);
        tstServerReadTimeoutOne(&Svc, 2 /* partial payload */);
        tstServerReadTimeoutOne(&Svc, 3 /* drip-fed payload */);
        tstServerPreOpenIdleTimeout(&Svc);

        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hFresh, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0 /*fFlags*/));
    } while (0);

    if (hFresh)
        IntNetR3IfDestroy(hFresh);
    if (hAnchor)
        IntNetR3IfDestroy(hAnchor);
    svcStop(&Svc);
}


/** Fake restricted service sending an incomplete reply frame. */
typedef struct TSTPARTIALREPLYSERVER
{
    RTLOCALIPCSERVER hServer;
    RTSEMEVENT       hRelease;
    unsigned         iMode;
} TSTPARTIALREPLYSERVER;


/** Reads and validates one request accepted by the fake service. */
static int tstPartialReplyServerReadRequest(RTLOCALIPCSESSION hSession, bool fVerifyPeer,
                                            PINTNETR3IPCREQHDR pReqHdr, void **ppvReq)
{
    *ppvReq = NULL;
    int rc = RTLocalIpcSessionRead(hSession, pReqHdr, sizeof(*pReqHdr), NULL /*pcbRead*/);
    if (RT_SUCCESS(rc) && fVerifyPeer)
    {
        rc = RTLocalIpcSessionVerifySameUser(hSession);
        if (rc == VERR_NOT_SUPPORTED)
            rc = VINF_SUCCESS;
    }
    if (RT_SUCCESS(rc))
    {
        if (   pReqHdr->u32Magic != INTNET_R3_IPC_REQ_MAGIC
            || pReqHdr->u16Version != INTNET_R3_IPC_VERSION
            || pReqHdr->cbHdr != sizeof(*pReqHdr)
            || pReqHdr->cbReq < sizeof(SUPVMMR0REQHDR)
            || pReqHdr->cbReq > INTNET_R3_IPC_MAX_REQ)
            rc = VERR_INVALID_PARAMETER;
    }
    if (RT_SUCCESS(rc))
    {
        *ppvReq = RTMemTmpAlloc(pReqHdr->cbReq);
        rc = *ppvReq ? RTLocalIpcSessionRead(hSession, *ppvReq, pReqHdr->cbReq, NULL /*pcbRead*/)
                     : VERR_NO_TMP_MEMORY;
    }
    return rc;
}


/** Sends one complete successful fake-service reply. */
static int tstPartialReplyServerSendReply(RTLOCALIPCSESSION hSession, INTNETR3IPCREQHDR const *pReqHdr,
                                          const void *pvReq)
{
    INTNETR3IPCREPLYHDR ReplyHdr;
    ReplyHdr.u32Magic    = INTNET_R3_IPC_REPLY_MAGIC;
    ReplyHdr.u16Version  = INTNET_R3_IPC_VERSION;
    ReplyHdr.cbHdr       = sizeof(ReplyHdr);
    ReplyHdr.rc          = VINF_SUCCESS;
    ReplyHdr.cbReply     = pReqHdr->cbReq;
    ReplyHdr.cbShMemName = 0;
    ReplyHdr.cbShMem     = 0;
    int rc = RTLocalIpcSessionWrite(hSession, &ReplyHdr, sizeof(ReplyHdr));
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionWrite(hSession, pvReq, pReqHdr->cbReq);
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionFlush(hSession);
    return rc;
}


/** Accepts requests and deliberately stalls before or during a reply. */
static DECLCALLBACK(int) tstPartialReplyServerThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread);
    TSTPARTIALREPLYSERVER *pServer = (TSTPARTIALREPLYSERVER *)pvUser;

    RTLOCALIPCSESSION hSession = NIL_RTLOCALIPCSESSION;
    int rc = RTLocalIpcServerListen(pServer->hServer, &hSession);
    if (RT_SUCCESS(rc))
    {
        INTNETR3IPCREQHDR ReqHdr;
        void *pvReq = NULL;
        rc = tstPartialReplyServerReadRequest(hSession, true /*fVerifyPeer*/, &ReqHdr, &pvReq);
        if (RT_SUCCESS(rc) && pServer->iMode == 3)
        {
            if (ReqHdr.uOperation != VMMR0_DO_INTNET_OPEN || ReqHdr.cbReq != sizeof(INTNETOPENREQ))
                rc = VERR_INVALID_PARAMETER;
            else
            {
                ((PINTNETOPENREQ)pvReq)->hIf = 1;
                rc = tstPartialReplyServerSendReply(hSession, &ReqHdr, pvReq);
            }
            RTMemTmpFree(pvReq);
            pvReq = NULL;
            if (RT_SUCCESS(rc))
                rc = tstPartialReplyServerReadRequest(hSession, false /*fVerifyPeer*/, &ReqHdr, &pvReq);
            if (   RT_SUCCESS(rc)
                && ReqHdr.uOperation != VMMR0_DO_INTNET_IF_GET_BUFFER_PTRS)
                rc = VERR_INVALID_PARAMETER;
        }
        if (RT_SUCCESS(rc))
        {
            INTNETR3IPCREPLYHDR ReplyHdr;
            ReplyHdr.u32Magic    = INTNET_R3_IPC_REPLY_MAGIC;
            ReplyHdr.u16Version  = INTNET_R3_IPC_VERSION;
            ReplyHdr.cbHdr       = sizeof(ReplyHdr);
            ReplyHdr.rc          = VINF_SUCCESS;
            ReplyHdr.cbReply     = ReqHdr.cbReq;
            ReplyHdr.cbShMemName = 0;
            ReplyHdr.cbShMem     = 0;
            if (pServer->iMode == 1)
                rc = RTLocalIpcSessionWrite(hSession, &ReplyHdr, 1);
            else if (pServer->iMode == 2)
            {
                rc = RTLocalIpcSessionWrite(hSession, &ReplyHdr, sizeof(ReplyHdr));
                if (RT_SUCCESS(rc))
                {
                    uint8_t bPartial = 0;
                    rc = RTLocalIpcSessionWrite(hSession, &bPartial, sizeof(bPartial));
                }
            }
            if (RT_SUCCESS(rc) && (pServer->iMode == 1 || pServer->iMode == 2))
                rc = RTLocalIpcSessionFlush(hSession);
            if (RT_SUCCESS(rc))
                rc = RTSemEventWait(pServer->hRelease, RT_MS_5SEC);
        }
        RTMemTmpFree(pvReq);
        RTLocalIpcSessionClose(hSession);
    }
    return rc;
}


/** Exercises no-reply, partial-frame, and post-OPEN client read deadlines. */
static void tstClientReadTimeout(void)
{
    RTTestSub(g_hTest, "Client read timeout");

    TSTSWITCHSVC Svc;
    int rc = svcStart(&Svc);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Preparing fake service failed: %Rrc", rc);
        svcStop(&Svc);
        return;
    }
    TST_CHECK_RC_OK(RTEnvSet(TST_ENV_READ_TIMEOUT, "1000"));

    RTLOCALIPCSERVER hServer = NIL_RTLOCALIPCSERVER;
    rc = RTLocalIpcServerCreate(&hServer, Svc.szService, RTLOCALIPC_FLAGS_RESTRICT_TO_USER);
    TST_CHECK_RC_OK(rc);
    if (RT_SUCCESS(rc))
    {
        for (unsigned iMode = 0; iMode < 4; iMode++)
        {
            /* A partial frame must be retired by the receiver's frame deadline,
               not by the indistinguishable synchronous-call deadline. */
            TST_CHECK_RC_OK(RTEnvSet(TST_ENV_REPLY_TIMEOUT,
                                    iMode == 1 || iMode == 2 ? "4294967295" : "1000"));

            TSTPARTIALREPLYSERVER Server;
            Server.hServer  = hServer;
            Server.hRelease = NIL_RTSEMEVENT;
            Server.iMode    = iMode;
            RTTHREAD hThread = NIL_RTTHREAD;
            rc = RTSemEventCreate(&Server.hRelease);
            if (RT_SUCCESS(rc))
                rc = RTThreadCreate(&hThread, tstPartialReplyServerThread, &Server, 0 /*cbStack*/,
                                    RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "IntNetFake");
            TST_CHECK_RC_OK(rc);
            if (RT_SUCCESS(rc))
            {
                char szNetwork[128];
                TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-client-timeout", szNetwork, sizeof(szNetwork)));
                INTNETIFCTX hIf = NULL;
                rc = IntNetR3IfCreateEx(&hIf, szNetwork, kIntNetTrunkType_WhateverNone, "",
                                        _64K, _64K, 0 /*fFlags*/);
                if (rc != VERR_TIMEOUT)
                    RTTestFailed(g_hTest, "Incomplete reply mode %u returned %Rrc, expected %Rrc",
                                 iMode, rc, VERR_TIMEOUT);
                if (hIf != NULL)
                {
                    RTTestFailed(g_hTest, "Incomplete reply mode %u unexpectedly returned an interface", iMode);
                    IntNetR3IfDestroy(hIf);
                }

                TST_CHECK_RC_OK(RTSemEventSignal(Server.hRelease));
                int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
                rc = RTThreadWait(hThread, RT_MS_5SEC, &rcThread);
                TST_CHECK_RC_OK(rc);
                if (RT_FAILURE(rc))
                    rc = RTThreadWait(hThread, RT_INDEFINITE_WAIT, &rcThread);
                if (RT_SUCCESS(rc) && RT_FAILURE(rcThread))
                    RTTestFailed(g_hTest, "Fake service mode %u returned %Rrc", iMode, rcThread);
            }
            if (Server.hRelease != NIL_RTSEMEVENT)
                RTSemEventDestroy(Server.hRelease);
        }
        RTLocalIpcServerDestroy(hServer);
    }
    svcStop(&Svc);
}
#endif /* !RT_OS_DARWIN || VBOX_INTNET_TESTCASE_LOCALIPC */


/*********************************************************************************************************************************
*   Test body                                                                                                                    *
*********************************************************************************************************************************/
static void tstIntNetR3Switch(void)
{
    RTTestSub(g_hTest, "R3 switch IPC (local IPC)");

#if defined(RT_OS_DARWIN) && !defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    RTTestSub(g_hTest, "R3 switch IPC (Darwin/XPC)");
    RTTestSkipped(g_hTest, "Darwin uses XPC for R3 IntNet switch; in-process embedding is not implemented in this testcase.");
    return;
#else

    TSTSWITCHSVC Svc;
    int rc = svcStart(&Svc);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Failed to prepare standalone switch service: %Rrc", rc);
        svcStop(&Svc);
        return;
    }

    char szNetA[128], szNetB[128], szNetWait[128], szNetRace[128];
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-A", szNetA, sizeof(szNetA)));
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-B", szNetB, sizeof(szNetB)));
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-Wait", szNetWait, sizeof(szNetWait)));
    TST_CHECK_RC_OK(tstMakeUuidName("tst-IntNet-Race", szNetRace, sizeof(szNetRace)));
    INTNETIFCTX hA = NULL, hB = NULL, hC = NULL, hWait = NULL, hRace = NULL;
    RTSEMEVENT hWaitEvt = NIL_RTSEMEVENT;
    RTTHREAD hWaitThread = NIL_RTTHREAD;
    TSTRXCOLLECT RxB; RT_ZERO(RxB);
    TSTRXCOLLECT RxC; RT_ZERO(RxC);
    TSTWAITRACE WaitRace; RT_ZERO(WaitRace);
    WaitRace.hThread = NIL_RTTHREAD;
    WaitRace.hReached = NIL_RTSEMEVENT;
    WaitRace.hContinue = NIL_RTSEMEVENT;
    bool fWaitThreadStopped = true;
    bool fRaceThreadStopped = true;
    do
    {
        /* No service is running yet.  This first interface creation must auto-start it. */
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hA, szNetA, kIntNetTrunkType_WhateverNone, "", _256K, _256K, 0));
        RTTESTI_CHECK_RC_OK_BREAK(svcReadPid(&Svc));
        RTPROCSTATUS Status;
        rc = RTProcWait(Svc.hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &Status);
        if (rc != VERR_PROCESS_RUNNING)
        {
            RTTestFailed(g_hTest, "Auto-started switch helper is not running: %Rrc", rc);
            if (RT_SUCCESS(rc))
            {
                Svc.fProcessReaped = true;
                Svc.hProcess = NIL_RTPROCESS;
            }
            break;
        }

        /* Reject hostile sizes before they reach IntNet's uint32_t alignment arithmetic. */
        INTNETIFCTX hOversized = NULL;
        rc = IntNetR3IfCreateEx(&hOversized, szNetA, kIntNetTrunkType_WhateverNone, "",
                                UINT32_MAX, UINT32_MAX, 0);
        if (rc != VERR_OUT_OF_RANGE)
            RTTestFailed(g_hTest, "Oversized open returned %Rrc, expected %Rrc", rc, VERR_OUT_OF_RANGE);
        if (hOversized != NULL)
        {
            RTTestFailed(g_hTest, "Oversized open unexpectedly returned an interface context");
            IntNetR3IfDestroy(hOversized);
        }

        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hB, szNetA, kIntNetTrunkType_WhateverNone, "", _256K, _256K, 0));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hC, szNetB, kIntNetTrunkType_WhateverNone, "", _128K, _128K, 0));
        RTTESTI_CHECK_RC_OK_BREAK(RTSemEventCreate(&hWaitEvt));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateExWithRecvEvent(&hWait, szNetWait, kIntNetTrunkType_WhateverNone, "",
                                                                  _64K, _64K, 0, hWaitEvt));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfCreateEx(&hRace, szNetRace, kIntNetTrunkType_WhateverNone, "",
                                                     _64K, _64K, 0));

        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hA, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hB, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hC, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hWait, true));
        RTTESTI_CHECK_RC_OK_BREAK(IntNetR3IfSetActive(hRace, true));

        RTTESTI_CHECK_RC_OK_BREAK(tstRxStart(&RxB, hB));
        RTTESTI_CHECK_RC_OK_BREAK(tstRxStart(&RxC, hC));

        /* Build and send a small broadcast-like frame on A. */
        RTTESTI_CHECK_RC_OK_BREAK(tstSendFrame(hA, 0x42)); /* content isn't parsed by switch */

        /* Wait for B to receive, ensure C did not (isolation by network name). */
        uint64_t msStart = RTTimeMilliTS();
        bool fGotB = false; bool fGotC = false;
        while (RTTimeMilliTS() - msStart < TST_WAIT_MS)
        {
            fGotB = ASMAtomicReadU32(&RxB.cFrames) != 0;
            fGotC = ASMAtomicReadU32(&RxC.cFrames) != 0;
            if (fGotC)
                break;
            RTThreadSleep(10);
        }
        TST_CHECK(fGotB);
        TST_CHECK(!fGotC);

        /* Send the next frame as soon as the preceding callback runs.  This
           repeatedly overlaps delivery with the collector re-registering its
           wait and exercises the server's lost-wakeup avoidance. */
        uint32_t const cFramesBefore = ASMAtomicReadU32(&RxB.cFrames);
        uint64_t const msStressStart = RTTimeMilliTS();
        uint32_t cFramesSent = 0;
        while (cFramesSent < TST_STRESS_FRAMES)
        {
            rc = tstSendFrame(hA, (uint8_t)cFramesSent);
            if (RT_FAILURE(rc))
            {
                RTTestFailed(g_hTest, "Stress frame %RU32 failed: %Rrc", cFramesSent, rc);
                break;
            }
            cFramesSent++;
            while (   ASMAtomicReadU32(&RxB.cFrames) < cFramesBefore + cFramesSent
                   && RTTimeMilliTS() - msStressStart < RT_MS_5SEC)
                RTThreadSleep(1);
            if (ASMAtomicReadU32(&RxB.cFrames) < cFramesBefore + cFramesSent)
            {
                RTTestFailed(g_hTest, "Stress receive stalled after %RU32 frames", cFramesSent);
                break;
            }
        }
        TST_CHECK(ASMAtomicReadU32(&RxB.cFrames) >= cFramesBefore + cFramesSent);
        TST_CHECK(ASMAtomicReadU32(&RxC.cFrames) == 0);

        fWaitThreadStopped = tstConcurrentWaitAbort(hWait, &hWaitThread);
        fRaceThreadStopped = tstInverseWaitAbortRace(hRace, &WaitRace);

    } while (0);

    /* Cleanup RX helpers before tearing down interfaces, including error paths. */
    bool const fRxBStopped = tstRxStop(&RxB);
    bool const fRxCStopped = tstRxStop(&RxC);

    if (hA) { IntNetR3IfSetActive(hA, false); IntNetR3IfDestroy(hA); }
    if (hB && fRxBStopped) { IntNetR3IfSetActive(hB, false); IntNetR3IfDestroy(hB); }
    if (hC && fRxCStopped) { IntNetR3IfSetActive(hC, false); IntNetR3IfDestroy(hC); }
    if (hWait && fWaitThreadStopped)
    {
        IntNetR3IfSetActive(hWait, false);
        IntNetR3IfDestroy(hWait);
        hWait = NULL;

        /* The caller-owned event must remain valid after interface teardown. */
        TST_CHECK_RC_OK(RTSemEventSignal(hWaitEvt));
        TST_CHECK_RC_OK(RTSemEventWait(hWaitEvt, 0 /*cMillies*/));
    }
    if (hWaitEvt != NIL_RTSEMEVENT && fWaitThreadStopped)
        RTSemEventDestroy(hWaitEvt);
    if (hRace && fRaceThreadStopped)
    {
        IntNetR3IfSetActive(hRace, false);
        IntNetR3IfDestroy(hRace);
        hRace = NULL;
    }

    svcStop(&Svc);

    /* If a bounded pre-stop join timed out, forcing the helper down closes the
       IPC stream.  Join before stack storage, events, or interfaces disappear. */
    if (!fRxBStopped && tstRxJoinAfterServiceStop(&RxB) && hB)
    {
        IntNetR3IfDestroy(hB);
        hB = NULL;
    }
    if (!fRxCStopped && tstRxJoinAfterServiceStop(&RxC) && hC)
    {
        IntNetR3IfDestroy(hC);
        hC = NULL;
    }
    if (!fWaitThreadStopped)
    {
        int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
        rc = RTThreadWait(hWaitThread, RT_INDEFINITE_WAIT, &rcThread);
        TST_CHECK_RC_OK(rc);
        if (RT_SUCCESS(rc) && rcThread != VERR_SEM_DESTROYED)
            RTTestFailed(g_hTest, "Wait thread returned %Rrc after service stop, expected %Rrc",
                         rcThread, VERR_SEM_DESTROYED);
        if (RT_SUCCESS(rc) && hWait)
        {
            IntNetR3IfDestroy(hWait);
            hWait = NULL;
        }
        if (RT_SUCCESS(rc) && hWaitEvt != NIL_RTSEMEVENT)
        {
            RTSemEventDestroy(hWaitEvt);
            hWaitEvt = NIL_RTSEMEVENT;
        }
    }
    if (!fRaceThreadStopped && tstWaitRaceJoinAfterServiceStop(&WaitRace) && hRace)
    {
        IntNetR3IfDestroy(hRace);
        hRace = NULL;
    }
#endif
}


int main(int argc, char **argv)
{
    /* The testcase build of IntNetIf forces the driverless R3 path and does not initialize SUPLib. */
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTTEST hTest = NIL_RTTEST;
    rc = RTTestCreate("tstVBoxIntNetR3Switch", &hTest);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    g_hTest = hTest;

    tstIntNetR3Switch();
#if !defined(RT_OS_DARWIN) || defined(VBOX_INTNET_TESTCASE_LOCALIPC)
    tstDeferredNotification();
    tstSessionSlotLimit("Connection limit", "2", "8");
    tstSessionSlotLimit("Worker-thread limit", "8", "4");
    tstAggregateShMemLimit();
    tstServerReadTimeout();
    tstClientReadTimeout();
#endif

    return RTTestSummaryAndDestroy(hTest);
}
