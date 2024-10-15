/* $Id: VBoxManageGuestCtrlListener.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Guest control listener implementations.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxManage.h"
#include "VBoxManageGuestCtrl.h"

#include <VBox/com/com.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <iprt/semaphore.h>
#include <iprt/time.h>

#include <map>
#include <vector>

DECLARE_TRANSLATION_CONTEXT(GuestCtrlLsnr);


/** Event semaphore we're using for notification. */
extern RTSEMEVENT g_SemEventGuestCtrlCanceled;


/*
 * GuestListenerBase
 * GuestListenerBase
 * GuestListenerBase
 */

GuestListenerBase::GuestListenerBase(void)
    : mfVerbose(false)
{
}

GuestListenerBase::~GuestListenerBase(void)
{
}

HRESULT GuestListenerBase::init(bool fVerbose)
{
    mfVerbose = fVerbose;
    return S_OK;
}



/*
 * GuestFileEventListener
 * GuestFileEventListener
 * GuestFileEventListener
 */

GuestFileEventListener::GuestFileEventListener(void)
{
}

GuestFileEventListener::~GuestFileEventListener(void)
{
}

void GuestFileEventListener::uninit(void)
{

}

STDMETHODIMP GuestFileEventListener::HandleEvent(VBoxEventType_T aType, IEvent *aEvent)
{
    switch (aType)
    {
        case VBoxEventType_OnGuestFileStateChanged:
        {
            HRESULT hrc;
            do
            {
                ComPtr<IGuestFileStateChangedEvent> pEvent = aEvent;
                Assert(!pEvent.isNull());

                ComPtr<IGuestFile> pProcess;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(File)(pProcess.asOutParam()));
                AssertBreak(!pProcess.isNull());
                FileStatus_T fileSts;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Status)(&fileSts));
                Bstr strPath;
                CHECK_ERROR_BREAK(pProcess, COMGETTER(Filename)(strPath.asOutParam()));
                ULONG uID;
                CHECK_ERROR_BREAK(pProcess, COMGETTER(Id)(&uID));

                RTPrintf(GuestCtrlLsnr::tr("File ID=%RU32 \"%s\" changed status to [%s]\n"),
                         uID, Utf8Str(strPath).c_str(), gctlFileStatusToText(fileSts));

            } while (0);
            break;
        }

        default:
            AssertFailed();
    }

    return S_OK;
}


/*
 * GuestProcessEventListener
 * GuestProcessEventListener
 * GuestProcessEventListener
 */

GuestProcessEventListener::GuestProcessEventListener(void)
{
}

GuestProcessEventListener::~GuestProcessEventListener(void)
{
}

void GuestProcessEventListener::uninit(void)
{

}

STDMETHODIMP GuestProcessEventListener::HandleEvent(VBoxEventType_T aType, IEvent *aEvent)
{
    switch (aType)
    {
        case VBoxEventType_OnGuestProcessStateChanged:
        {
            HRESULT hrc;
            do
            {
                ComPtr<IGuestProcessStateChangedEvent> pEvent = aEvent;
                Assert(!pEvent.isNull());

                ComPtr<IGuestProcess> pProcess;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Process)(pProcess.asOutParam()));
                AssertBreak(!pProcess.isNull());
                ProcessStatus_T procSts;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Status)(&procSts));
                Bstr strPath;
                CHECK_ERROR_BREAK(pProcess, COMGETTER(ExecutablePath)(strPath.asOutParam()));
                ULONG uPID;
                CHECK_ERROR_BREAK(pProcess, COMGETTER(PID)(&uPID));

                RTPrintf(GuestCtrlLsnr::tr("Process PID=%RU32 \"%s\" changed status to [%s]\n"),
                         uPID, Utf8Str(strPath).c_str(), gctlProcessStatusToText(procSts));

            } while (0);
            break;
        }

        default:
            AssertFailed();
    }

    return S_OK;
}


/*
 * GuestSessionEventListener
 * GuestSessionEventListener
 * GuestSessionEventListener
 */

GuestSessionEventListener::GuestSessionEventListener(void)
{
}

GuestSessionEventListener::~GuestSessionEventListener(void)
{
}

void GuestSessionEventListener::uninit(void)
{
    GuestEventProcs::iterator itProc = mProcs.begin();
    while (itProc != mProcs.end())
    {
        if (!itProc->first.isNull())
        {
            HRESULT hrc;
            do
            {
                /* Listener unregistration. */
                ComPtr<IEventSource> pES;
                CHECK_ERROR_BREAK(itProc->first, COMGETTER(EventSource)(pES.asOutParam()));
                if (!pES.isNull())
                    CHECK_ERROR_BREAK(pES, UnregisterListener(itProc->second.mListener));
            } while (0);
        }

        ++itProc;
    }
    mProcs.clear();

    GuestEventFiles::iterator itFile = mFiles.begin();
    while (itFile != mFiles.end())
    {
        if (!itFile->first.isNull())
        {
            HRESULT hrc;
            do
            {
                /* Listener unregistration. */
                ComPtr<IEventSource> pES;
                CHECK_ERROR_BREAK(itFile->first, COMGETTER(EventSource)(pES.asOutParam()));
                if (!pES.isNull())
                    CHECK_ERROR_BREAK(pES, UnregisterListener(itFile->second.mListener));
            } while (0);
        }

        ++itFile;
    }
    mFiles.clear();
}

STDMETHODIMP GuestSessionEventListener::HandleEvent(VBoxEventType_T aType, IEvent *aEvent)
{
    switch (aType)
    {
        case VBoxEventType_OnGuestFileRegistered:
        {
            HRESULT hrc;
            do
            {
                ComPtr<IGuestFileRegisteredEvent> pEvent = aEvent;
                Assert(!pEvent.isNull());

                ComPtr<IGuestFile> pFile;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(File)(pFile.asOutParam()));
                AssertBreak(!pFile.isNull());
                BOOL fRegistered;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Registered)(&fRegistered));
                Bstr strPath;
                CHECK_ERROR_BREAK(pFile, COMGETTER(Filename)(strPath.asOutParam()));

                RTPrintf(GuestCtrlLsnr::tr("File \"%s\" %s\n"),
                         Utf8Str(strPath).c_str(),
                         fRegistered ? GuestCtrlLsnr::tr("registered") : GuestCtrlLsnr::tr("unregistered"));
                if (fRegistered)
                {
                    if (mfVerbose)
                        RTPrintf(GuestCtrlLsnr::tr("Registering ...\n"));

                    /* Register for IGuestFile events. */
                    ComObjPtr<GuestFileEventListenerImpl> pListener;
                    pListener.createObject();
                    CHECK_ERROR_BREAK(pListener, init(new GuestFileEventListener()));

                    ComPtr<IEventSource> es;
                    CHECK_ERROR_BREAK(pFile, COMGETTER(EventSource)(es.asOutParam()));
                    com::SafeArray<VBoxEventType_T> eventTypes;
                    eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
                    CHECK_ERROR_BREAK(es, RegisterListener(pListener, ComSafeArrayAsInParam(eventTypes),
                                                           true /* Active listener */));

                    GuestFileStats fileStats(pListener);
                    mFiles[pFile] = fileStats;
                }
                else
                {
                    GuestEventFiles::iterator itFile = mFiles.find(pFile);
                    if (itFile != mFiles.end())
                    {
                        if (mfVerbose)
                            RTPrintf(GuestCtrlLsnr::tr("Unregistering file ...\n"));

                        if (!itFile->first.isNull())
                        {
                            /* Listener unregistration. */
                            ComPtr<IEventSource> pES;
                            CHECK_ERROR(itFile->first, COMGETTER(EventSource)(pES.asOutParam()));
                            if (!pES.isNull())
                                CHECK_ERROR(pES, UnregisterListener(itFile->second.mListener));
                        }

                        mFiles.erase(itFile);
                    }
                }

            } while (0);
            break;
        }

        case VBoxEventType_OnGuestProcessRegistered:
        {
            HRESULT hrc;
            do
            {
                ComPtr<IGuestProcessRegisteredEvent> pEvent = aEvent;
                Assert(!pEvent.isNull());

                ComPtr<IGuestProcess> pProcess;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Process)(pProcess.asOutParam()));
                AssertBreak(!pProcess.isNull());
                BOOL fRegistered;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Registered)(&fRegistered));
                Bstr strPath;
                CHECK_ERROR_BREAK(pProcess, COMGETTER(ExecutablePath)(strPath.asOutParam()));

                RTPrintf(GuestCtrlLsnr::tr("Process \"%s\" %s\n"),
                         Utf8Str(strPath).c_str(),
                         fRegistered ? GuestCtrlLsnr::tr("registered") : GuestCtrlLsnr::tr("unregistered"));
                if (fRegistered)
                {
                    if (mfVerbose)
                        RTPrintf(GuestCtrlLsnr::tr("Registering ...\n"));

                    /* Register for IGuestProcess events. */
                    ComObjPtr<GuestProcessEventListenerImpl> pListener;
                    pListener.createObject();
                    CHECK_ERROR_BREAK(pListener, init(new GuestProcessEventListener()));

                    ComPtr<IEventSource> es;
                    CHECK_ERROR_BREAK(pProcess, COMGETTER(EventSource)(es.asOutParam()));
                    com::SafeArray<VBoxEventType_T> eventTypes;
                    eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);
                    CHECK_ERROR_BREAK(es, RegisterListener(pListener, ComSafeArrayAsInParam(eventTypes),
                                                           true /* Active listener */));

                    GuestProcStats procStats(pListener);
                    mProcs[pProcess] = procStats;
                }
                else
                {
                    GuestEventProcs::iterator itProc = mProcs.find(pProcess);
                    if (itProc != mProcs.end())
                    {
                        if (mfVerbose)
                            RTPrintf(GuestCtrlLsnr::tr("Unregistering process ...\n"));

                        if (!itProc->first.isNull())
                        {
                            /* Listener unregistration. */
                            ComPtr<IEventSource> pES;
                            CHECK_ERROR(itProc->first, COMGETTER(EventSource)(pES.asOutParam()));
                            if (!pES.isNull())
                                CHECK_ERROR(pES, UnregisterListener(itProc->second.mListener));
                        }

                        mProcs.erase(itProc);
                    }
                }

            } while (0);
            break;
        }

        case VBoxEventType_OnGuestSessionStateChanged:
        {
            HRESULT hrc;
            do
            {
                ComPtr<IGuestSessionStateChangedEvent> pEvent = aEvent;
                Assert(!pEvent.isNull());
                ComPtr<IGuestSession> pSession;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Session)(pSession.asOutParam()));
                AssertBreak(!pSession.isNull());

                GuestSessionStatus_T sessSts;
                CHECK_ERROR_BREAK(pSession, COMGETTER(Status)(&sessSts));
                ULONG uID;
                CHECK_ERROR_BREAK(pSession, COMGETTER(Id)(&uID));
                Bstr strName;
                CHECK_ERROR_BREAK(pSession, COMGETTER(Name)(strName.asOutParam()));

                RTPrintf(GuestCtrlLsnr::tr("Session ID=%RU32 \"%s\" changed status to [%s]\n"),
                         uID, Utf8Str(strName).c_str(), gctlGuestSessionStatusToText(sessSts));

            } while (0);
            break;
        }

        default:
            AssertFailed();
    }

    return S_OK;
}


/*
 * GuestEventListener
 * GuestEventListener
 * GuestEventListener
 */

GuestEventListener::GuestEventListener(void)
{
}

GuestEventListener::~GuestEventListener(void)
{
}

void GuestEventListener::uninit(void)
{
    GuestEventSessions::iterator itSession = mSessions.begin();
    while (itSession != mSessions.end())
    {
        if (!itSession->first.isNull())
        {
            HRESULT hrc;
            do
            {
                /* Listener unregistration. */
                ComPtr<IEventSource> pES;
                CHECK_ERROR_BREAK(itSession->first, COMGETTER(EventSource)(pES.asOutParam()));
                if (!pES.isNull())
                    CHECK_ERROR_BREAK(pES, UnregisterListener(itSession->second.mListener));

            } while (0);
        }

        ++itSession;
    }
    mSessions.clear();
}

STDMETHODIMP GuestEventListener::HandleEvent(VBoxEventType_T aType, IEvent *aEvent)
{
    switch (aType)
    {
        case VBoxEventType_OnGuestSessionRegistered:
        {
            HRESULT hrc;
            do
            {
                ComPtr<IGuestSessionRegisteredEvent> pEvent = aEvent;
                Assert(!pEvent.isNull());

                ComPtr<IGuestSession> pSession;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Session)(pSession.asOutParam()));
                AssertBreak(!pSession.isNull());
                BOOL fRegistered;
                CHECK_ERROR_BREAK(pEvent, COMGETTER(Registered)(&fRegistered));
                Bstr strName;
                CHECK_ERROR_BREAK(pSession, COMGETTER(Name)(strName.asOutParam()));
                ULONG uID;
                CHECK_ERROR_BREAK(pSession, COMGETTER(Id)(&uID));

                RTPrintf(GuestCtrlLsnr::tr("Session ID=%RU32 \"%s\" %s\n"),
                         uID, Utf8Str(strName).c_str(),
                         fRegistered ? GuestCtrlLsnr::tr("registered") : GuestCtrlLsnr::tr("unregistered"));
                if (fRegistered)
                {
                    if (mfVerbose)
                        RTPrintf(GuestCtrlLsnr::tr("Registering ...\n"));

                    /* Register for IGuestSession events. */
                    ComObjPtr<GuestSessionEventListenerImpl> pListener;
                    pListener.createObject();
                    CHECK_ERROR_BREAK(pListener, init(new GuestSessionEventListener()));

                    ComPtr<IEventSource> es;
                    CHECK_ERROR_BREAK(pSession, COMGETTER(EventSource)(es.asOutParam()));
                    com::SafeArray<VBoxEventType_T> eventTypes;
                    eventTypes.push_back(VBoxEventType_OnGuestFileRegistered);
                    eventTypes.push_back(VBoxEventType_OnGuestProcessRegistered);
                    eventTypes.push_back(VBoxEventType_OnGuestSessionStateChanged);
                    CHECK_ERROR_BREAK(es, RegisterListener(pListener, ComSafeArrayAsInParam(eventTypes),
                                                           true /* Active listener */));

                    GuestSessionStats sessionStats(pListener);
                    mSessions[pSession] = sessionStats;
                }
                else
                {
                    GuestEventSessions::iterator itSession = mSessions.find(pSession);
                    if (itSession != mSessions.end())
                    {
                        if (mfVerbose)
                            RTPrintf(GuestCtrlLsnr::tr("Unregistering ...\n"));

                        if (!itSession->first.isNull())
                        {
                            /* Listener unregistration. */
                            ComPtr<IEventSource> pES;
                            CHECK_ERROR_BREAK(itSession->first, COMGETTER(EventSource)(pES.asOutParam()));
                            if (!pES.isNull())
                                CHECK_ERROR_BREAK(pES, UnregisterListener(itSession->second.mListener));
                        }

                        mSessions.erase(itSession);
                    }
                }

            } while (0);
            break;
        }

        default:
            AssertFailed();
    }

    return S_OK;
}

/*
 * GuestAdditionsRunlevelListener
 * GuestAdditionsRunlevelListener
 * GuestAdditionsRunlevelListener
 */

GuestAdditionsRunlevelListener::GuestAdditionsRunlevelListener(AdditionsRunLevelType_T enmRunLevel)
    : mRunLevelTarget(enmRunLevel)
{
}

GuestAdditionsRunlevelListener::~GuestAdditionsRunlevelListener(void)
{
}

void GuestAdditionsRunlevelListener::uninit(void)
{
}

STDMETHODIMP GuestAdditionsRunlevelListener::HandleEvent(VBoxEventType_T aType, IEvent *aEvent)
{
    Assert(mRunLevelTarget != AdditionsRunLevelType_None);

    HRESULT hrc;

    switch (aType)
    {
        case VBoxEventType_OnGuestAdditionsStatusChanged:
        {
            ComPtr<IGuestAdditionsStatusChangedEvent> pEvent = aEvent;
            Assert(!pEvent.isNull());

            AdditionsRunLevelType_T RunLevelCur = AdditionsRunLevelType_None;
            CHECK_ERROR_BREAK(pEvent, COMGETTER(RunLevel)(&RunLevelCur));

            if (mfVerbose)
                RTPrintf(GuestCtrlLsnr::tr("Reached run level %RU32\n"), RunLevelCur);

            if (RunLevelCur == mRunLevelTarget)
            {
                int vrc = RTSemEventSignal(g_SemEventGuestCtrlCanceled);
                AssertRC(vrc);
            }

            break;
        }

        default:
            AssertFailed();
    }

    return S_OK;
}
