/* $Id: ClientTokenHolder.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VirtualBox API client session token holder (in the client process)
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

#define LOG_GROUP LOG_GROUP_MAIN_SESSION
#include "LoggingNew.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/semaphore.h>
#include <iprt/process.h>

#ifdef VBOX_WITH_SYS_V_IPC_SESSION_WATCHER
# include <errno.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/ipc.h>
# include <sys/sem.h>
#endif

#include <VBox/com/defs.h>

#include "ClientTokenHolder.h"
#include "SessionImpl.h"


#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
/** client token holder thread */
static DECLCALLBACK(int) ClientTokenHolderThread(RTTHREAD hThreadSelf, void *pvUser);
#endif


Session::ClientTokenHolder::ClientTokenHolder()
{
    AssertReleaseFailed();
}

Session::ClientTokenHolder::~ClientTokenHolder()
{
    /* release the client token */
#if defined(RT_OS_WINDOWS)

    if (mSem && mThreadSem)
    {
        /*
         *  tell the thread holding the token to release it;
         *  it will close mSem handle
         */
        ::SetEvent(mSem);
        /* wait for the thread to finish */
        ::WaitForSingleObject(mThreadSem, INFINITE);
        ::CloseHandle(mThreadSem);

        mThreadSem = NULL;
        mSem = NULL;
        mThread = NIL_RTTHREAD;
    }

#elif defined(RT_OS_OS2)

    if (mThread != NIL_RTTHREAD)
    {
        Assert(mSem != NIL_RTSEMEVENT);

        /* tell the thread holding the token to release it */
        int vrc = RTSemEventSignal(mSem);
        AssertRC(vrc == NO_ERROR);

        /* wait for the thread to finish */
        vrc = RTThreadUserWait(mThread, RT_INDEFINITE_WAIT);
        Assert(RT_SUCCESS(vrc) || vrc == VERR_INTERRUPTED);

        mThread = NIL_RTTHREAD;
    }

    if (mSem != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(mSem);
        mSem = NIL_RTSEMEVENT;
    }

#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)

    if (mSem >= 0)
    {
        ::sembuf sop = { 0, 1, SEM_UNDO };
        ::semop(mSem, &sop, 1);

        mSem = -1;
    }

#elif defined(VBOX_WITH_GENERIC_SESSION_WATCHER)

    if (!mToken.isNull())
    {
        mToken->Abandon();
        mToken.setNull();
    }

#else
# error "Port me!"
#endif
}

#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
Session::ClientTokenHolder::ClientTokenHolder(const Utf8Str &strTokenId) :
    mClientTokenId(strTokenId)
#else /* VBOX_WITH_GENERIC_SESSION_WATCHER */
Session::ClientTokenHolder::ClientTokenHolder(IToken *aToken) :
    mToken(aToken)
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */
{
#ifdef CTHSEMTYPE
    mSem = CTHSEMARG;
#endif
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    mThread = NIL_RTTHREAD;
#endif

#if defined(RT_OS_WINDOWS)
    mThreadSem = CTHTHREADSEMARG;

    /*
     * Since there is no guarantee that the constructor and destructor will be
     * called in the same thread, we need a separate thread to hold the token.
     */

    mThreadSem = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    AssertMsgReturnVoid(mThreadSem,
                        ("Cannot create an event sem, err=%d", ::GetLastError()));

    void *data[3];
    data[0] = (void*)strTokenId.c_str();
    data[1] = (void*)mThreadSem;
    data[2] = 0; /* will get an output from the thread */

    /* create a thread to hold the token until signalled to release it */
    int vrc = RTThreadCreate(&mThread, ClientTokenHolderThread, (void*)data, 0, RTTHREADTYPE_MAIN_WORKER, 0, "IPCHolder");
    AssertRCReturnVoid(vrc);

    /* wait until thread init is completed */
    DWORD wrc = ::WaitForSingleObject(mThreadSem, INFINITE);
    AssertMsg(wrc == WAIT_OBJECT_0, ("Wait failed, err=%d\n", ::GetLastError()));
    Assert(data[2]);

    if (wrc == WAIT_OBJECT_0 && data[2])
    {
        /* memorize the event sem we should signal in close() */
        mSem = (HANDLE)data[2];
    }
    else
    {
        ::CloseHandle(mThreadSem);
        mThreadSem = NULL;
    }
#elif defined(RT_OS_OS2)
    /*
     * Since there is no guarantee that the constructor and destructor will be
     * called in the same thread, we need a separate thread to hold the token.
     */

    int vrc = RTSemEventCreate(&mSem);
    AssertRCReturnVoid(vrc);

    void *data[3];
    data[0] = (void*)strTokenId.c_str();
    data[1] = (void*)mSem;
    data[2] = (void*)false; /* will get the thread result here */

    /* create a thread to hold the token until signalled to release it */
    vrc = RTThreadCreate(&mThread, ClientTokenHolderThread, (void *) data,
                         0, RTTHREADTYPE_MAIN_WORKER, 0, "IPCHolder");
    AssertRCReturnVoid(vrc);
    /* wait until thread init is completed */
    vrc = RTThreadUserWait(mThread, RT_INDEFINITE_WAIT);
    AssertReturnVoid(RT_SUCCESS(vrc) || vrc == VERR_INTERRUPTED);

    /* the thread must succeed */
    AssertReturnVoid((bool)data[2]);

#elif defined(VBOX_WITH_SYS_V_IPC_SESSION_WATCHER)

# ifdef VBOX_WITH_NEW_SYS_V_KEYGEN
    key_t key = RTStrToUInt32(strTokenId.c_str());
    AssertMsgReturnVoid(key != 0,
                        ("Key value of 0 is not valid for client token"));
# else /* !VBOX_WITH_NEW_SYS_V_KEYGEN */
    char *pszSemName = NULL;
    RTStrUtf8ToCurrentCP(&pszSemName, strTokenId);
    key_t key = ::ftok(pszSemName, 'V');
    RTStrFree(pszSemName);
# endif /* !VBOX_WITH_NEW_SYS_V_KEYGEN */
    int s = ::semget(key, 0, 0);
    AssertMsgReturnVoid(s >= 0,
                        ("Cannot open semaphore, errno=%d", errno));

    /* grab the semaphore */
    ::sembuf sop = { 0,  -1, SEM_UNDO };
    int rv = ::semop(s, &sop, 1);
    AssertMsgReturnVoid(rv == 0,
                        ("Cannot grab semaphore, errno=%d", errno));
    mSem = s;

#elif defined(VBOX_WITH_GENERIC_SESSION_WATCHER)

    /* nothing to do */

#else
# error "Port me!"
#endif
}

bool Session::ClientTokenHolder::isReady()
{
#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
    return mSem != CTHSEMARG;
#else /* VBOX_WITH_GENERIC_SESSION_WATCHER */
    return !mToken.isNull();
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */
}

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
/** client token holder thread */
DECLCALLBACK(int) ClientTokenHolderThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    LogFlowFuncEnter();

    Assert(pvUser);

    void **data = (void **)pvUser;

# if defined(RT_OS_WINDOWS)
    Utf8Str strSessionId = (const char *)data[0];
    HANDLE initDoneSem = (HANDLE)data[1];

    Bstr bstrSessionId(strSessionId);
    HANDLE mutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, bstrSessionId.raw());

    //AssertMsg(mutex, ("cannot open token, err=%u\n", ::GetLastError()));
    AssertLogRelMsg(mutex, ("cannot open token %ls, err=%u\n", bstrSessionId.raw(), ::GetLastError()));
    if (mutex)
    {
        /* grab the token */
        DWORD wrc = ::WaitForSingleObject(mutex, 0);
        AssertMsg(wrc == WAIT_OBJECT_0, ("cannot grab token, err=%d\n", wrc));
        if (wrc == WAIT_OBJECT_0)
        {
            HANDLE finishSem = ::CreateEvent(NULL, FALSE, FALSE, NULL);
            AssertMsg(finishSem, ("cannot create event sem, err=%d\n", ::GetLastError()));
            if (finishSem)
            {
                data[2] = (void*)finishSem;
                /* signal we're done with init */
                ::SetEvent(initDoneSem);
                /* wait until we're signaled to release the token */
                ::WaitForSingleObject(finishSem, INFINITE);
                /* release the token */
                LogFlow(("ClientTokenHolderThread(): releasing token...\n"));
                BOOL fRc = ::ReleaseMutex(mutex);
                AssertMsg(fRc, ("cannot release token, err=%d\n", ::GetLastError())); NOREF(fRc);
                ::CloseHandle(mutex);
                ::CloseHandle(finishSem);
            }
        }
    }

    /* signal we're done */
    ::SetEvent(initDoneSem);
# elif defined(RT_OS_OS2)
    Utf8Str strSessionId = (const char *)data[0];
    RTSEMEVENT finishSem = (RTSEMEVENT)data[1];

    LogFlowFunc(("strSessionId='%s', finishSem=%p\n", strSessionId.c_str(), finishSem));

    HMTX mutex = NULLHANDLE;
    APIRET arc = ::DosOpenMutexSem((PSZ)strSessionId.c_str(), &mutex);
    AssertMsg(arc == NO_ERROR, ("cannot open token, arc=%ld\n", arc));

    if (arc == NO_ERROR)
    {
        /* grab the token */
        LogFlowFunc(("grabbing token...\n"));
        arc = ::DosRequestMutexSem(mutex, SEM_IMMEDIATE_RETURN);
        AssertMsg(arc == NO_ERROR, ("cannot grab token, arc=%ld\n", arc));
        if (arc == NO_ERROR)
        {
            /* store the answer */
            data[2] = (void*)true;
            /* signal we're done */
            int vrc = RTThreadUserSignal(Thread);
            AssertRC(vrc);

            /* wait until we're signaled to release the token */
            LogFlowFunc(("waiting for termination signal..\n"));
            vrc = RTSemEventWait(finishSem, RT_INDEFINITE_WAIT);
            Assert(arc == ERROR_INTERRUPT || ERROR_TIMEOUT);

            /* release the token */
            LogFlowFunc(("releasing token...\n"));
            arc = ::DosReleaseMutexSem(mutex);
            AssertMsg(arc == NO_ERROR, ("cannot release token, arc=%ld\n", arc));
        }
        ::DosCloseMutexSem(mutex);
    }

    /* store the answer */
    data[1] = (void*)false;
    /* signal we're done */
    int vrc = RTThreadUserSignal(Thread);
    AssertRC(vrc);
# else
#  error "Port me!"
# endif

    LogFlowFuncLeave();

    return 0;
}
#endif

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
