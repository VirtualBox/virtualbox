/* $Id: ClientWatcher.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox API client session watcher
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MAIN_INCLUDED_ClientWatcher_h
#define MAIN_INCLUDED_ClientWatcher_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#include <list>
#include <VBox/com/ptr.h>
#include <VBox/com/AutoLock.h>

#include "VirtualBoxImpl.h"

#if defined(RT_OS_WINDOWS)
# define CWUPDATEREQARG NULL
# define CWUPDATEREQTYPE HANDLE
# define CW_MAX_CLIENTS  _16K            /**< Max number of clients we can watch (windows). */
# ifndef DEBUG /* The debug version triggers worker thread code much much earlier. */
#  define CW_MAX_CLIENTS_PER_THREAD 63   /**< Max clients per watcher thread (windows). */
# else
#  define CW_MAX_CLIENTS_PER_THREAD 3    /**< Max clients per watcher thread (windows). */
# endif
# define CW_MAX_HANDLES_PER_THREAD (CW_MAX_CLIENTS_PER_THREAD + 1) /**< Max handles per thread. */

#elif defined(RT_OS_OS2)
# define CWUPDATEREQARG NIL_RTSEMEVENT
# define CWUPDATEREQTYPE RTSEMEVENT

#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER) || defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
# define CWUPDATEREQARG NIL_RTSEMEVENT
# define CWUPDATEREQTYPE RTSEMEVENT

#else
# error "Port me!"
#endif

/**
 * Class which checks for API clients which have crashed/exited, and takes
 * the necessary cleanup actions. Singleton.
 */
class VirtualBox::ClientWatcher
{
public:
    /**
     * Constructor which creates a usable instance
     *
     * @param pVirtualBox   Reference to VirtualBox object
     */
    ClientWatcher(const ComObjPtr<VirtualBox> &pVirtualBox);

    /**
     * Default destructor. Cleans everything up.
     */
    ~ClientWatcher();

    bool isReady();

    void update();
    void addProcess(RTPROCESS pid);

private:
    /**
     * Default constructor. Don't use, will not create a sensible instance.
     */
    ClientWatcher();

    static DECLCALLBACK(int) worker(RTTHREAD hThreadSelf, void *pvUser);
    uint32_t reapProcesses(void);

    VirtualBox *mVirtualBox;
    RTTHREAD mThread;
    CWUPDATEREQTYPE mUpdateReq;
    util::RWLockHandle mLock;

    typedef std::list<RTPROCESS> ProcessList;
    ProcessList mProcesses;

#if defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER) || defined(VBOX_WITH_GENERIC_SESSION_WATCHER)
    uint8_t mUpdateAdaptCtr;
#endif
#ifdef RT_OS_WINDOWS
    /** Indicate a real update request is pending.
     * To avoid race conditions this must be set before mUpdateReq is signalled and
     * read after resetting mUpdateReq. */
    volatile bool mfUpdateReq;
    /** Set when the worker threads are supposed to shut down. */
    volatile bool mfTerminate;
    /** Number of active subworkers.
     * When decremented to 0, subworker zero is signalled. */
    uint32_t volatile mcActiveSubworkers;
    /** Number of valid handles in mahWaitHandles. */
    uint32_t    mcWaitHandles;
    /** The wait interval (usually INFINITE). */
    uint32_t    mcMsWait;
    /** Per subworker data. Subworker 0 is the main worker and does not have a
     *  pReq pointer since. */
    struct PerSubworker
    {
        /** The wait result. */
        DWORD                       dwWait;
        /** The subworker index. */
        uint32_t                    iSubworker;
        /** The subworker thread handle. */
        RTTHREAD                    hThread;
        /** Self pointer (for worker thread). */
        VirtualBox::ClientWatcher  *pSelf;
    } maSubworkers[(CW_MAX_CLIENTS + CW_MAX_CLIENTS_PER_THREAD - 1) / CW_MAX_CLIENTS_PER_THREAD];
    /** Wait handle array. The mUpdateReq manual reset event handle is inserted
     * every 64 entries, first entry being 0. */
    HANDLE      mahWaitHandles[CW_MAX_CLIENTS + (CW_MAX_CLIENTS + CW_MAX_CLIENTS_PER_THREAD - 1) / CW_MAX_CLIENTS_PER_THREAD];

    void subworkerWait(VirtualBox::ClientWatcher::PerSubworker *pSubworker, uint32_t cMsWait);
    static DECLCALLBACK(int) subworkerThread(RTTHREAD hThreadSelf, void *pvUser);
    void winResetHandleArray(uint32_t cProcHandles);
#endif
};

#endif /* !MAIN_INCLUDED_ClientWatcher_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
