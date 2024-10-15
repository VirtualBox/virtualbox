/* $Id: ClientWatcher.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox API client session crash watcher
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_MAIN
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/process.h>

#include <VBox/log.h>
#include <VBox/com/defs.h>

#include <vector>

#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "ClientWatcher.h"
#include "ClientToken.h"
#include "VirtualBoxImpl.h"
#include "MachineImpl.h"

#if defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER) || defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
/** Table for adaptive timeouts. After an update the counter starts at the
 * maximum value and decreases to 0, i.e. first the short timeouts are used
 * and then the longer ones. This minimizes the detection latency in the
 * cases where a change is expected, for crashes. */
static const RTMSINTERVAL s_aUpdateTimeoutSteps[] = { 500, 200, 100, 50, 20, 10, 5 };
#endif



VirtualBox::ClientWatcher::ClientWatcher() :
    mLock(LOCKCLASS_OBJECTSTATE, "ClientWatcher")
{
    AssertReleaseFailed();
}

VirtualBox::ClientWatcher::~ClientWatcher()
{
    if (mThread != NIL_RTTHREAD)
    {
        /* signal the client watcher thread, should be exiting now */
        update();
        /* wait for termination */
        RTThreadWait(mThread, RT_INDEFINITE_WAIT, NULL);
        mThread = NIL_RTTHREAD;
    }
    mProcesses.clear();
#if defined(RT_OS_WINDOWS)
    if (mUpdateReq != NULL)
    {
        ::CloseHandle(mUpdateReq);
        mUpdateReq = NULL;
    }
#elif defined(RT_OS_OS2) || defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER) || defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
    if (mUpdateReq != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(mUpdateReq);
        mUpdateReq = NIL_RTSEMEVENT;
    }
#else
# error "Port me!"
#endif
}

VirtualBox::ClientWatcher::ClientWatcher(const ComObjPtr<VirtualBox> &pVirtualBox) :
    mVirtualBox(pVirtualBox),
    mThread(NIL_RTTHREAD),
    mUpdateReq(CWUPDATEREQARG),
    mLock(LOCKCLASS_OBJECTSTATE, "ClientWatcher")
{
#if defined(RT_OS_WINDOWS)
    /* Misc state. */
    mfTerminate         = false;
    mcMsWait            = INFINITE;
    mcActiveSubworkers  = 0;

    /* Update request.  The UpdateReq event is also used to wake up subthreads. */
    mfUpdateReq         = false;
    mUpdateReq          = ::CreateEvent(NULL /*pSecAttr*/, TRUE /*fManualReset*/, FALSE /*fInitialState*/, NULL /*pszName*/);
    AssertRelease(mUpdateReq != NULL);

    /* Initialize the handle array. */
    for (uint32_t i = 0; i < RT_ELEMENTS(mahWaitHandles); i++)
        mahWaitHandles[i] = NULL;
    for (uint32_t i = 0; i < RT_ELEMENTS(mahWaitHandles); i += CW_MAX_HANDLES_PER_THREAD)
        mahWaitHandles[i] = mUpdateReq;
    mcWaitHandles = 1;

#elif defined(RT_OS_OS2)
    RTSemEventCreate(&mUpdateReq);
#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER) || defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
    RTSemEventCreate(&mUpdateReq);
    /* start with high timeouts, nothing to do */
    ASMAtomicUoWriteU8(&mUpdateAdaptCtr, 0);
#else
# error "Port me!"
#endif

    int vrc = RTThreadCreate(&mThread,
                             worker,
                             (void *)this,
                             0,
                             RTTHREADTYPE_MAIN_WORKER,
                             RTTHREADFLAGS_WAITABLE,
                             "Watcher");
    AssertRC(vrc);
}

bool VirtualBox::ClientWatcher::isReady()
{
    return mThread != NIL_RTTHREAD;
}

/**
 * Sends a signal to the thread to rescan the clients/VMs having open sessions.
 */
void VirtualBox::ClientWatcher::update()
{
    AssertReturnVoid(mThread != NIL_RTTHREAD);
    LogFlowFunc(("ping!\n"));

    /* sent an update request */
#if defined(RT_OS_WINDOWS)
    ASMAtomicWriteBool(&mfUpdateReq, true);
    ::SetEvent(mUpdateReq);

#elif defined(RT_OS_OS2)
    RTSemEventSignal(mUpdateReq);

#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)
    /* use short timeouts, as we expect changes */
    ASMAtomicUoWriteU8(&mUpdateAdaptCtr, RT_ELEMENTS(s_aUpdateTimeoutSteps) - 1);
    RTSemEventSignal(mUpdateReq);

#elif defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
    RTSemEventSignal(mUpdateReq);

#else
# error "Port me!"
#endif
}

/**
 * Adds a process to the list of processes to be reaped. This call should be
 * followed by a call to update() to cause the necessary actions immediately,
 * in case the process crashes straight away.
 */
void VirtualBox::ClientWatcher::addProcess(RTPROCESS pid)
{
    AssertReturnVoid(mThread != NIL_RTTHREAD);
    AutoWriteLock alock(mLock COMMA_LOCKVAL_SRC_POS);
    mProcesses.push_back(pid);
}

/**
 * Reaps dead processes in the mProcesses list.
 *
 * @returns Number of reaped processes.
 */
uint32_t VirtualBox::ClientWatcher::reapProcesses(void)
{
    uint32_t cReaped = 0;

    AutoWriteLock alock(mLock COMMA_LOCKVAL_SRC_POS);
    if (mProcesses.size())
    {
        LogFlowFunc(("UPDATE: child process count = %zu\n", mProcesses.size()));
        VirtualBox::ClientWatcher::ProcessList::iterator it = mProcesses.begin();
        while (it != mProcesses.end())
        {
            RTPROCESS pid = *it;
            RTPROCSTATUS Status;
            int vrc = ::RTProcWait(pid, RTPROCWAIT_FLAGS_NOBLOCK, &Status);
            if (vrc == VINF_SUCCESS)
            {
                if (   Status.enmReason != RTPROCEXITREASON_NORMAL
                    || Status.iStatus   != RTEXITCODE_SUCCESS)
                {
                    switch (Status.enmReason)
                    {
                        default:
                        case RTPROCEXITREASON_NORMAL:
                            LogRel(("Reaper: Pid %d (%#x) exited normally: %d (%#x)\n",
                                    pid, pid, Status.iStatus, Status.iStatus));
                            break;
                        case RTPROCEXITREASON_ABEND:
                            LogRel(("Reaper: Pid %d (%#x) abended: %d (%#x)\n",
                                    pid, pid, Status.iStatus, Status.iStatus));
                            break;
                        case RTPROCEXITREASON_SIGNAL:
                            LogRel(("Reaper: Pid %d (%#x) was signalled: %s (%d / %#x)\n",
                                    pid, pid, RTProcSignalName(Status.iStatus), Status.iStatus, Status.iStatus));
                            break;
                    }
                }
                else
                    LogFlowFunc(("pid %d (%x) was reaped, status=%d, reason=%d\n", pid, pid, Status.iStatus, Status.enmReason));
                it = mProcesses.erase(it);
                cReaped++;
            }
            else
            {
                LogFlowFunc(("pid %d (%x) was NOT reaped, vrc=%Rrc\n", pid, pid, vrc));
                if (vrc != VERR_PROCESS_RUNNING)
                {
                    /* remove the process if it is not already running */
                    it = mProcesses.erase(it);
                    cReaped++;
                }
                else
                    ++it;
            }
        }
    }

    return cReaped;
}

#ifdef RT_OS_WINDOWS

/**
 * Closes all the client process handles in mahWaitHandles.
 *
 * The array is divided into two ranges, first range are mutext handles of
 * established sessions, the second range is zero or more process handles of
 * spawning sessions.  It's the latter that we close here, the former will just
 * be NULLed out.
 *
 * @param   cProcHandles        The number of process handles.
 */
void VirtualBox::ClientWatcher::winResetHandleArray(uint32_t cProcHandles)
{
    uint32_t idxHandle = mcWaitHandles;
    Assert(cProcHandles < idxHandle);
    Assert(idxHandle > 0);

    /* Spawning process handles. */
    while (cProcHandles-- > 0 && idxHandle > 0)
    {
        idxHandle--;
        if (idxHandle % CW_MAX_HANDLES_PER_THREAD)
        {
            Assert(mahWaitHandles[idxHandle] != mUpdateReq);
            LogFlow(("UPDATE: closing %p\n", mahWaitHandles[idxHandle]));
            CloseHandle(mahWaitHandles[idxHandle]);
            mahWaitHandles[idxHandle] = NULL;
        }
        else
            Assert(mahWaitHandles[idxHandle] == mUpdateReq);
    }

    /* Mutex handles (not to be closed). */
    while (idxHandle-- > 0)
        if (idxHandle % CW_MAX_HANDLES_PER_THREAD)
        {
            Assert(mahWaitHandles[idxHandle] != mUpdateReq);
            mahWaitHandles[idxHandle] = NULL;
        }
        else
            Assert(mahWaitHandles[idxHandle] == mUpdateReq);

    /* Reset the handle count. */
    mcWaitHandles = 1;
}

/**
 * Does the waiting on a section of the handle array.
 *
 * @param   pSubworker      Pointer to the calling thread's data.
 * @param   cMsWait         Number of milliseconds to wait.
 */
void VirtualBox::ClientWatcher::subworkerWait(VirtualBox::ClientWatcher::PerSubworker *pSubworker, uint32_t cMsWait)
{
    /*
     * Figure out what section to wait on and do the waiting.
     */
    uint32_t idxHandle = pSubworker->iSubworker * CW_MAX_HANDLES_PER_THREAD;
    uint32_t cHandles  = CW_MAX_HANDLES_PER_THREAD;
    if (idxHandle + cHandles > mcWaitHandles)
    {
        cHandles = mcWaitHandles - idxHandle;
        AssertStmt(idxHandle < mcWaitHandles, cHandles = 1);
    }
    Assert(mahWaitHandles[idxHandle] == mUpdateReq);

    DWORD dwWait = ::WaitForMultipleObjects(cHandles,
                                            &mahWaitHandles[idxHandle],
                                            FALSE /*fWaitAll*/,
                                            cMsWait);
    pSubworker->dwWait = dwWait;

    /*
     * If we didn't wake up because of the UpdateReq handle, signal it to make
     * sure everyone else wakes up too.
     */
    if (dwWait != WAIT_OBJECT_0)
    {
        BOOL fRc = SetEvent(mUpdateReq);
        Assert(fRc); NOREF(fRc);
    }

    /*
     * Last one signals the main thread.
     */
    if (ASMAtomicDecU32(&mcActiveSubworkers) == 0)
    {
        int vrc = RTThreadUserSignal(maSubworkers[0].hThread);
        AssertLogRelMsg(RT_SUCCESS(vrc), ("RTThreadUserSignal -> %Rrc\n", vrc));
    }

}

/**
 * Thread worker function that watches the termination of all client processes
 * that have open sessions using IMachine::LockMachine()
 */
/*static*/
DECLCALLBACK(int) VirtualBox::ClientWatcher::subworkerThread(RTTHREAD hThreadSelf, void *pvUser)
{
    VirtualBox::ClientWatcher::PerSubworker *pSubworker = (VirtualBox::ClientWatcher::PerSubworker *)pvUser;
    VirtualBox::ClientWatcher               *pThis = pSubworker->pSelf;
    int                                      vrc;
    while (!pThis->mfTerminate)
    {
        /* Before we start waiting, reset the event semaphore. */
        vrc = RTThreadUserReset(pSubworker->hThread);
        AssertLogRelMsg(RT_SUCCESS(vrc), ("RTThreadUserReset [iSubworker=%#u] -> %Rrc", pSubworker->iSubworker, vrc));

        /* Do the job. */
        pThis->subworkerWait(pSubworker, pThis->mcMsWait);

        /* Wait for the next job. */
        do
        {
            vrc = RTThreadUserWaitNoResume(hThreadSelf, RT_INDEFINITE_WAIT);
            Assert(vrc == VINF_SUCCESS || vrc == VERR_INTERRUPTED);
        }
        while (   vrc != VINF_SUCCESS
               && !pThis->mfTerminate);
    }
    return VINF_SUCCESS;
}


#endif /* RT_OS_WINDOWS */

/**
 * Thread worker function that watches the termination of all client processes
 * that have open sessions using IMachine::LockMachine()
 */
/*static*/
DECLCALLBACK(int) VirtualBox::ClientWatcher::worker(RTTHREAD hThreadSelf, void *pvUser)
{
    LogFlowFuncEnter();
    NOREF(hThreadSelf);

    VirtualBox::ClientWatcher *that = (VirtualBox::ClientWatcher *)pvUser;
    Assert(that);

    typedef std::vector<ComObjPtr<Machine> > MachineVector;
    typedef std::vector<ComObjPtr<SessionMachine> > SessionMachineVector;

    SessionMachineVector machines;
    MachineVector spawnedMachines;

    size_t cnt = 0;
    size_t cntSpawned = 0;

    VirtualBoxBase::initializeComForThread();

#if defined(RT_OS_WINDOWS)

    int vrc;

    /* Initialize all the subworker data. */
    that->maSubworkers[0].hThread = hThreadSelf;
    for (uint32_t iSubworker = 1; iSubworker < RT_ELEMENTS(that->maSubworkers); iSubworker++)
        that->maSubworkers[iSubworker].hThread    = NIL_RTTHREAD;
    for (uint32_t iSubworker = 0; iSubworker < RT_ELEMENTS(that->maSubworkers); iSubworker++)
    {
        that->maSubworkers[iSubworker].pSelf      = that;
        that->maSubworkers[iSubworker].iSubworker = iSubworker;
    }

    do
    {
        /* VirtualBox has been early uninitialized, terminate. */
        AutoCaller autoCaller(that->mVirtualBox);
        if (!autoCaller.isOk())
            break;

        bool fPidRace = false;          /* We poll if the PID of a spawning session hasn't been established yet.  */
        bool fRecentDeath = false;      /* We slowly poll if a session has recently been closed to do reaping. */
        for (;;)
        {
            /* release the caller to let uninit() ever proceed */
            autoCaller.release();

            /* Kick of the waiting. */
            uint32_t const cSubworkers = (that->mcWaitHandles + CW_MAX_HANDLES_PER_THREAD - 1) / CW_MAX_HANDLES_PER_THREAD;
            uint32_t const cMsWait     = fPidRace ? 500 : fRecentDeath ? 5000 : INFINITE;
            LogFlowFunc(("UPDATE: Waiting. %u handles, %u subworkers, %u ms wait\n", that->mcWaitHandles, cSubworkers, cMsWait));

            that->mcMsWait = cMsWait;
            ASMAtomicWriteU32(&that->mcActiveSubworkers, cSubworkers);
            RTThreadUserReset(hThreadSelf);

            for (uint32_t iSubworker = 1; iSubworker < cSubworkers; iSubworker++)
            {
                if (that->maSubworkers[iSubworker].hThread != NIL_RTTHREAD)
                {
                    vrc = RTThreadUserSignal(that->maSubworkers[iSubworker].hThread);
                    AssertLogRelMsg(RT_SUCCESS(vrc), ("RTThreadUserSignal -> %Rrc\n", vrc));
                }
                else
                {
                    vrc = RTThreadCreateF(&that->maSubworkers[iSubworker].hThread,
                                          VirtualBox::ClientWatcher::subworkerThread, &that->maSubworkers[iSubworker],
                                          _128K, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "Watcher%u", iSubworker);
                    AssertLogRelMsgStmt(RT_SUCCESS(vrc), ("%Rrc iSubworker=%u\n", vrc, iSubworker),
                                        that->maSubworkers[iSubworker].hThread = NIL_RTTHREAD);
                }
                if (RT_FAILURE(vrc))
                    that->subworkerWait(&that->maSubworkers[iSubworker], 1);
            }

            /* Wait ourselves. */
            that->subworkerWait(&that->maSubworkers[0], cMsWait);

            /* Make sure all waiters are done waiting. */
            BOOL fRc = SetEvent(that->mUpdateReq);
            Assert(fRc); NOREF(fRc);

            vrc = RTThreadUserWait(hThreadSelf, RT_INDEFINITE_WAIT);
            AssertLogRelMsg(RT_SUCCESS(vrc), ("RTThreadUserWait -> %Rrc\n", vrc));
            Assert(that->mcActiveSubworkers == 0);

            /* Consume pending update request before proceeding with processing the wait results. */
            fRc = ResetEvent(that->mUpdateReq);
            Assert(fRc);

            bool update = ASMAtomicXchgBool(&that->mfUpdateReq, false);
            if (update)
                LogFlowFunc(("UPDATE: Update request pending\n"));
            update |= fPidRace;

            /* Process the wait results. */
            autoCaller.add();
            if (!autoCaller.isOk())
                break;
            fRecentDeath = false;
            for (uint32_t iSubworker = 0; iSubworker < cSubworkers; iSubworker++)
            {
                DWORD dwWait = that->maSubworkers[iSubworker].dwWait;
                LogFlowFunc(("UPDATE: subworker #%u: dwWait=%#x\n", iSubworker, dwWait));
                if (   (dwWait > WAIT_OBJECT_0    && dwWait < WAIT_OBJECT_0    + CW_MAX_HANDLES_PER_THREAD)
                    || (dwWait > WAIT_ABANDONED_0 && dwWait < WAIT_ABANDONED_0 + CW_MAX_HANDLES_PER_THREAD) )
                {
                    uint32_t idxHandle = iSubworker * CW_MAX_HANDLES_PER_THREAD;
                    if (dwWait > WAIT_OBJECT_0    && dwWait < WAIT_OBJECT_0    + CW_MAX_HANDLES_PER_THREAD)
                        idxHandle += dwWait - WAIT_OBJECT_0;
                    else
                        idxHandle += dwWait - WAIT_ABANDONED_0;

                    uint32_t const idxMachine = idxHandle - (iSubworker + 1);
                    if (idxMachine < cnt)
                    {
                        /* Machine mutex is released or abandond due to client process termination. */
                        LogFlowFunc(("UPDATE: Calling i_checkForDeath on idxMachine=%u (idxHandle=%u) dwWait=%#x\n",
                                     idxMachine, idxHandle, dwWait));
                        fRecentDeath |= (machines[idxMachine])->i_checkForDeath();
                    }
                    else if (idxMachine < cnt + cntSpawned)
                    {
                        /* Spawned VM process has terminated normally. */
                        Assert(dwWait < WAIT_ABANDONED_0);
                        LogFlowFunc(("UPDATE: Calling i_checkForSpawnFailure on idxMachine=%u/%u idxHandle=%u dwWait=%#x\n",
                                     idxMachine, idxMachine - cnt, idxHandle, dwWait));
                        fRecentDeath |= (spawnedMachines[idxMachine - cnt])->i_checkForSpawnFailure();
                    }
                    else
                        AssertFailed();
                    update = true;
                }
                else
                    Assert(dwWait == WAIT_OBJECT_0 || dwWait == WAIT_TIMEOUT);
            }

            if (update)
            {
                LogFlowFunc(("UPDATE: Update pending (cnt=%u cntSpawned=%u)...\n", cnt, cntSpawned));

                /* close old process handles */
                that->winResetHandleArray((uint32_t)cntSpawned);

                // get reference to the machines list in VirtualBox
                VirtualBox::MachinesOList &allMachines = that->mVirtualBox->i_getMachinesList();

                // lock the machines list for reading
                AutoReadLock thatLock(allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);

                /* obtain a new set of opened machines */
                cnt = 0;
                machines.clear();
                uint32_t idxHandle = 0;

                for (MachinesOList::iterator it = allMachines.begin();
                     it != allMachines.end();
                     ++it)
                {
                    AssertMsgBreak(idxHandle < CW_MAX_CLIENTS, ("CW_MAX_CLIENTS reached"));

                    ComObjPtr<SessionMachine> sm;
                    if ((*it)->i_isSessionOpenOrClosing(sm))
                    {
                        AutoCaller smCaller(sm);
                        if (smCaller.isOk())
                        {
                            AutoReadLock smLock(sm COMMA_LOCKVAL_SRC_POS);
                            Machine::ClientToken *ct = sm->i_getClientToken();
                            if (ct)
                            {
                                HANDLE ipcSem = ct->getToken();
                                machines.push_back(sm);
                                if (!(idxHandle % CW_MAX_HANDLES_PER_THREAD))
                                    idxHandle++;
                                that->mahWaitHandles[idxHandle++] = ipcSem;
                                ++cnt;
                            }
                        }
                    }
                }

                LogFlowFunc(("UPDATE: direct session count = %d\n", cnt));

                /* obtain a new set of spawned machines */
                fPidRace = false;
                cntSpawned = 0;
                spawnedMachines.clear();

                for (MachinesOList::iterator it = allMachines.begin();
                     it != allMachines.end();
                     ++it)
                {
                    AssertMsgBreak(idxHandle < CW_MAX_CLIENTS, ("CW_MAX_CLIENTS reached"));

                    if ((*it)->i_isSessionSpawning())
                    {
                        ULONG pid;
                        HRESULT hrc = (*it)->COMGETTER(SessionPID)(&pid);
                        if (SUCCEEDED(hrc))
                        {
                            if (pid != NIL_RTPROCESS)
                            {
                                HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, pid);
                                AssertMsg(hProc != NULL, ("OpenProcess (pid=%d) failed with %d\n", pid, GetLastError()));
                                if (hProc != NULL)
                                {
                                    spawnedMachines.push_back(*it);
                                    if (!(idxHandle % CW_MAX_HANDLES_PER_THREAD))
                                        idxHandle++;
                                    that->mahWaitHandles[idxHandle++] = hProc;
                                    ++cntSpawned;
                                }
                            }
                            else
                                fPidRace = true;
                        }
                    }
                }

                LogFlowFunc(("UPDATE: spawned session count = %d\n", cntSpawned));

                /* Update mcWaitHandles and make sure there is at least one handle to wait on. */
                that->mcWaitHandles = RT_MAX(idxHandle, 1);

                // machines lock unwinds here
            }
            else
                LogFlowFunc(("UPDATE: No update pending.\n"));

            /* reap child processes */
            that->reapProcesses();

        } /* for ever (well, till autoCaller fails). */

    } while (0);

    /* Terminate subworker threads. */
    ASMAtomicWriteBool(&that->mfTerminate, true);
    for (uint32_t iSubworker = 1; iSubworker < RT_ELEMENTS(that->maSubworkers); iSubworker++)
        if (that->maSubworkers[iSubworker].hThread != NIL_RTTHREAD)
            RTThreadUserSignal(that->maSubworkers[iSubworker].hThread);
    for (uint32_t iSubworker = 1; iSubworker < RT_ELEMENTS(that->maSubworkers); iSubworker++)
        if (that->maSubworkers[iSubworker].hThread != NIL_RTTHREAD)
        {
            vrc = RTThreadWait(that->maSubworkers[iSubworker].hThread, RT_MS_1MIN, NULL /*prc*/);
            if (RT_SUCCESS(vrc))
                that->maSubworkers[iSubworker].hThread = NIL_RTTHREAD;
            else
                AssertLogRelMsgFailed(("RTThreadWait -> %Rrc\n", vrc));
        }

    /* close old process handles */
    that->winResetHandleArray((uint32_t)cntSpawned);

    /* release sets of machines if any */
    machines.clear();
    spawnedMachines.clear();

    ::CoUninitialize();

#elif defined(RT_OS_OS2)

    /* according to PMREF, 64 is the maximum for the muxwait list */
    SEMRECORD handles[64];

    HMUX muxSem = NULLHANDLE;

    do
    {
        AutoCaller autoCaller(that->mVirtualBox);
        /* VirtualBox has been early uninitialized, terminate */
        if (!autoCaller.isOk())
            break;

        for (;;)
        {
            /* release the caller to let uninit() ever proceed */
            autoCaller.release();

            int vrc = RTSemEventWait(that->mUpdateReq, 500);

            /* Restore the caller before using VirtualBox. If it fails, this
             * means VirtualBox is being uninitialized and we must terminate. */
            autoCaller.add();
            if (!autoCaller.isOk())
                break;

            bool update = false;
            bool updateSpawned = false;

            if (RT_SUCCESS(vrc))
            {
                /* update event is signaled */
                update = true;
                updateSpawned = true;
            }
            else
            {
                AssertMsg(vrc == VERR_TIMEOUT || vrc == VERR_INTERRUPTED,
                          ("RTSemEventWait returned %Rrc\n", vrc));

                /* are there any mutexes? */
                if (cnt > 0)
                {
                    /* figure out what's going on with machines */

                    unsigned long semId = 0;
                    APIRET arc = ::DosWaitMuxWaitSem(muxSem,
                                                     SEM_IMMEDIATE_RETURN, &semId);

                    if (arc == NO_ERROR)
                    {
                        /* machine mutex is normally released */
                        Assert(semId >= 0 && semId < cnt);
                        if (semId >= 0 && semId < cnt)
                        {
#if 0//def DEBUG
                            {
                                AutoReadLock machineLock(machines[semId] COMMA_LOCKVAL_SRC_POS);
                                LogFlowFunc(("released mutex: machine='%ls'\n",
                                             machines[semId]->name().raw()));
                            }
#endif
                            machines[semId]->i_checkForDeath();
                        }
                        update = true;
                    }
                    else if (arc == ERROR_SEM_OWNER_DIED)
                    {
                        /* machine mutex is abandoned due to client process
                         * termination; find which mutex is in the Owner Died
                         * state */
                        for (size_t i = 0; i < cnt; ++i)
                        {
                            PID pid; TID tid;
                            unsigned long reqCnt;
                            arc = DosQueryMutexSem((HMTX)handles[i].hsemCur, &pid, &tid, &reqCnt);
                            if (arc == ERROR_SEM_OWNER_DIED)
                            {
                                /* close the dead mutex as asked by PMREF */
                                ::DosCloseMutexSem((HMTX)handles[i].hsemCur);

                                Assert(i >= 0 && i < cnt);
                                if (i >= 0 && i < cnt)
                                {
#if 0//def DEBUG
                                    {
                                        AutoReadLock machineLock(machines[semId] COMMA_LOCKVAL_SRC_POS);
                                        LogFlowFunc(("mutex owner dead: machine='%ls'\n",
                                                     machines[i]->name().raw()));
                                    }
#endif
                                    machines[i]->i_checkForDeath();
                                }
                            }
                        }
                        update = true;
                    }
                    else
                        AssertMsg(arc == ERROR_INTERRUPT || arc == ERROR_TIMEOUT,
                                  ("DosWaitMuxWaitSem returned %d\n", arc));
                }

                /* are there any spawning sessions? */
                if (cntSpawned > 0)
                {
                    for (size_t i = 0; i < cntSpawned; ++i)
                        updateSpawned |= (spawnedMachines[i])->
                            i_checkForSpawnFailure();
                }
            }

            if (update || updateSpawned)
            {
                // get reference to the machines list in VirtualBox
                VirtualBox::MachinesOList &allMachines = that->mVirtualBox->i_getMachinesList();

                // lock the machines list for reading
                AutoReadLock thatLock(allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);

                if (update)
                {
                    /* close the old muxsem */
                    if (muxSem != NULLHANDLE)
                        ::DosCloseMuxWaitSem(muxSem);

                    /* obtain a new set of opened machines */
                    cnt = 0;
                    machines.clear();

                    for (MachinesOList::iterator it = allMachines.begin();
                         it != allMachines.end(); ++it)
                    {
                        /// @todo handle situations with more than 64 objects
                        AssertMsg(cnt <= 64 /* according to PMREF */,
                                  ("maximum of 64 mutex semaphores reached (%d)",
                                   cnt));

                        ComObjPtr<SessionMachine> sm;
                        if ((*it)->i_isSessionOpenOrClosing(sm))
                        {
                            AutoCaller smCaller(sm);
                            if (smCaller.isOk())
                            {
                                AutoReadLock smLock(sm COMMA_LOCKVAL_SRC_POS);
                                ClientToken *ct = sm->i_getClientToken();
                                if (ct)
                                {
                                    HMTX ipcSem = ct->getToken();
                                    machines.push_back(sm);
                                    handles[cnt].hsemCur = (HSEM)ipcSem;
                                    handles[cnt].ulUser = cnt;
                                    ++cnt;
                                }
                            }
                        }
                    }

                    LogFlowFunc(("UPDATE: direct session count = %d\n", cnt));

                    if (cnt > 0)
                    {
                        /* create a new muxsem */
                        APIRET arc = ::DosCreateMuxWaitSem(NULL, &muxSem, cnt,
                                                           handles,
                                                           DCMW_WAIT_ANY);
                        AssertMsg(arc == NO_ERROR,
                                  ("DosCreateMuxWaitSem returned %d\n", arc));
                        NOREF(arc);
                    }
                }

                if (updateSpawned)
                {
                    /* obtain a new set of spawned machines */
                    spawnedMachines.clear();

                    for (MachinesOList::iterator it = allMachines.begin();
                         it != allMachines.end(); ++it)
                    {
                        if ((*it)->i_isSessionSpawning())
                            spawnedMachines.push_back(*it);
                    }

                    cntSpawned = spawnedMachines.size();
                    LogFlowFunc(("UPDATE: spawned session count = %d\n", cntSpawned));
                }
            }

            /* reap child processes */
            that->reapProcesses();

        } /* for ever (well, till autoCaller fails). */

    } while (0);

    /* close the muxsem */
    if (muxSem != NULLHANDLE)
        ::DosCloseMuxWaitSem(muxSem);

    /* release sets of machines if any */
    machines.clear();
    spawnedMachines.clear();

#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)

    bool update = false;
    bool updateSpawned = false;

    do
    {
        AutoCaller autoCaller(that->mVirtualBox);
        if (!autoCaller.isOk())
            break;

        do
        {
            /* release the caller to let uninit() ever proceed */
            autoCaller.release();

            /* determine wait timeout adaptively: after updating information
             * relevant to the client watcher, check a few times more
             * frequently. This ensures good reaction time when the signalling
             * has to be done a bit before the actual change for technical
             * reasons, and saves CPU cycles when no activities are expected. */
            RTMSINTERVAL cMillies;
            {
                uint8_t uOld, uNew;
                do
                {
                    uOld = ASMAtomicUoReadU8(&that->mUpdateAdaptCtr);
                    uNew = uOld ? uOld - 1 : uOld;
                } while (!ASMAtomicCmpXchgU8(&that->mUpdateAdaptCtr, uNew, uOld));
                Assert(uOld <= RT_ELEMENTS(s_aUpdateTimeoutSteps) - 1);
                cMillies = s_aUpdateTimeoutSteps[uOld];
            }

            int vrc = RTSemEventWait(that->mUpdateReq, cMillies);

            /*
             *  Restore the caller before using VirtualBox. If it fails, this
             *  means VirtualBox is being uninitialized and we must terminate.
             */
            autoCaller.add();
            if (!autoCaller.isOk())
                break;

            if (RT_SUCCESS(vrc) || update || updateSpawned)
            {
                /* RT_SUCCESS(vrc) means an update event is signaled */

                // get reference to the machines list in VirtualBox
                VirtualBox::MachinesOList &allMachines = that->mVirtualBox->i_getMachinesList();

                // lock the machines list for reading
                AutoReadLock thatLock(allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);

                if (RT_SUCCESS(vrc) || update)
                {
                    /* obtain a new set of opened machines */
                    machines.clear();

                    for (MachinesOList::iterator it = allMachines.begin();
                         it != allMachines.end();
                         ++it)
                    {
                        ComObjPtr<SessionMachine> sm;
                        if ((*it)->i_isSessionOpenOrClosing(sm))
                            machines.push_back(sm);
                    }

                    cnt = machines.size();
                    LogFlowFunc(("UPDATE: direct session count = %d\n", cnt));
                }

                if (RT_SUCCESS(vrc) || updateSpawned)
                {
                    /* obtain a new set of spawned machines */
                    spawnedMachines.clear();

                    for (MachinesOList::iterator it = allMachines.begin();
                         it != allMachines.end();
                         ++it)
                    {
                        if ((*it)->i_isSessionSpawning())
                            spawnedMachines.push_back(*it);
                    }

                    cntSpawned = spawnedMachines.size();
                    LogFlowFunc(("UPDATE: spawned session count = %d\n", cntSpawned));
                }

                // machines lock unwinds here
            }

            update = false;
            for (size_t i = 0; i < cnt; ++i)
                update |= (machines[i])->i_checkForDeath();

            updateSpawned = false;
            for (size_t i = 0; i < cntSpawned; ++i)
                updateSpawned |= (spawnedMachines[i])->i_checkForSpawnFailure();

            /* reap child processes */
            that->reapProcesses();
        }
        while (true);
    }
    while (0);

    /* release sets of machines if any */
    machines.clear();
    spawnedMachines.clear();

#elif defined(VBOX_WITH_GENERIC_SESSION_WATCHER)

    bool updateSpawned = false;

    do
    {
        AutoCaller autoCaller(that->mVirtualBox);
        if (!autoCaller.isOk())
            break;

        do
        {
            /* release the caller to let uninit() ever proceed */
            autoCaller.release();

            /* determine wait timeout adaptively: after updating information
             * relevant to the client watcher, check a few times more
             * frequently. This ensures good reaction time when the signalling
             * has to be done a bit before the actual change for technical
             * reasons, and saves CPU cycles when no activities are expected. */
            RTMSINTERVAL cMillies;
            {
                uint8_t uOld, uNew;
                do
                {
                    uOld = ASMAtomicUoReadU8(&that->mUpdateAdaptCtr);
                    uNew = uOld ? (uint8_t)(uOld - 1) : uOld;
                } while (!ASMAtomicCmpXchgU8(&that->mUpdateAdaptCtr, uNew, uOld));
                Assert(uOld <= RT_ELEMENTS(s_aUpdateTimeoutSteps) - 1);
                cMillies = s_aUpdateTimeoutSteps[uOld];
            }

            int vrc = RTSemEventWait(that->mUpdateReq, cMillies);

            /*
             *  Restore the caller before using VirtualBox. If it fails, this
             *  means VirtualBox is being uninitialized and we must terminate.
             */
            autoCaller.add();
            if (!autoCaller.isOk())
                break;

            /** @todo this quite big effort for catching machines in spawning
             * state which can't be caught by the token mechanism (as the token
             * can't be in the other process yet) could be eliminated if the
             * reaping is made smarter, having cross-reference information
             * from the pid to the corresponding machine object. Both cases do
             * more or less the same thing anyway. */
            if (RT_SUCCESS(vrc) || updateSpawned)
            {
                /* RT_SUCCESS(vrc) means an update event is signaled */

                // get reference to the machines list in VirtualBox
                VirtualBox::MachinesOList &allMachines = that->mVirtualBox->i_getMachinesList();

                // lock the machines list for reading
                AutoReadLock thatLock(allMachines.getLockHandle() COMMA_LOCKVAL_SRC_POS);

                if (RT_SUCCESS(vrc) || updateSpawned)
                {
                    /* obtain a new set of spawned machines */
                    spawnedMachines.clear();

                    for (MachinesOList::iterator it = allMachines.begin();
                         it != allMachines.end();
                         ++it)
                    {
                        if ((*it)->i_isSessionSpawning())
                            spawnedMachines.push_back(*it);
                    }

                    cntSpawned = spawnedMachines.size();
                    LogFlowFunc(("UPDATE: spawned session count = %d\n", cntSpawned));
                }

                NOREF(cnt);
                // machines lock unwinds here
            }

            updateSpawned = false;
            for (size_t i = 0; i < cntSpawned; ++i)
                updateSpawned |= (spawnedMachines[i])->i_checkForSpawnFailure();

            /* reap child processes */
            that->reapProcesses();
        }
        while (true);
    }
    while (0);

    /* release sets of machines if any */
    machines.clear();
    spawnedMachines.clear();

#else
# error "Port me!"
#endif

    VirtualBoxBase::uninitializeComForThread();

    LogFlowFuncLeave();
    return 0;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
