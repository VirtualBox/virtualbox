/* $Id: GuestSessionImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest session handling.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_GUESTSESSION
#include "LoggingNew.h"

#include "GuestImpl.h"
#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestSessionImpl.h"
#include "GuestSessionImplTasks.h"
#include "GuestFsInfoImpl.h"
#include "GuestCtrlImplPrivate.h"
#include "VirtualBoxErrorInfoImpl.h"

#include "Global.h"
#include "AutoCaller.h"
#include "ProgressImpl.h"
#include "VBoxEvents.h"
#include "VMMDev.h"
#include "ThreadTask.h"

#include <memory> /* For auto_ptr. */

#include <iprt/cpp/utils.h> /* For unconst(). */
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/file.h> /* For CopyTo/From. */
#include <iprt/path.h>
#include <iprt/rand.h>

#include <VBox/com/array.h>
#include <VBox/com/listeners.h>
#include <VBox/version.h>


/**
 * Base class representing an internal
 * asynchronous session task.
 */
class GuestSessionTaskInternal : public ThreadTask
{
public:

    GuestSessionTaskInternal(GuestSession *pSession)
        : ThreadTask("GenericGuestSessionTaskInternal")
        , mSession(pSession)
        , mVrc(VINF_SUCCESS) { }

    virtual ~GuestSessionTaskInternal(void) { }

    /** Returns the last set result code. */
    int vrc(void) const { return mVrc; }
    /** Returns whether the last set result code indicates success or not. */
    bool isOk(void) const { return RT_SUCCESS(mVrc); }
    /** Returns the task's guest session object. */
    const ComObjPtr<GuestSession> &Session(void) const { return mSession; }

protected:

    /** Guest session the task belongs to. */
    const ComObjPtr<GuestSession>    mSession;
    /** The last set VBox status code. */
    int                              mVrc;
};

/**
 * Class for asynchronously starting a guest session.
 */
class GuestSessionTaskInternalStart : public GuestSessionTaskInternal
{
public:

    GuestSessionTaskInternalStart(GuestSession *pSession)
        : GuestSessionTaskInternal(pSession)
    {
        m_strTaskName = "gctlSesStart";
    }

    void handler()
    {
        /* Ignore return code */
        GuestSession::i_startSessionThreadTask(this);
    }
};

/**
 * Internal listener class to serve events in an
 * active manner, e.g. without polling delays.
 */
class GuestSessionListener
{
public:

    GuestSessionListener(void)
    {
    }

    virtual ~GuestSessionListener(void)
    {
    }

    HRESULT init(GuestSession *pSession)
    {
        AssertPtrReturn(pSession, E_POINTER);
        mSession = pSession;
        return S_OK;
    }

    void uninit(void)
    {
        mSession = NULL;
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnGuestSessionStateChanged:
            {
                AssertPtrReturn(mSession, E_POINTER);
                int vrc2 = mSession->signalWaitEvent(aType, aEvent);
                RT_NOREF(vrc2);
#ifdef DEBUG_andy
                LogFlowFunc(("Signalling events of type=%RU32, session=%p resulted in vrc2=%Rrc\n", aType, mSession, vrc2));
#endif
                break;
            }

            default:
                AssertMsgFailed(("Unhandled event %RU32\n", aType));
                break;
        }

        return S_OK;
    }

private:

    GuestSession *mSession;
};
typedef ListenerImpl<GuestSessionListener, GuestSession*> GuestSessionListenerImpl;

VBOX_LISTENER_DECLARE(GuestSessionListenerImpl)

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestSession)

HRESULT GuestSession::FinalConstruct(void)
{
    LogFlowThisFuncEnter();
    return BaseFinalConstruct();
}

void GuestSession::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes a guest session but does *not* open in on the guest side
 * yet. This needs to be done via the openSession() / openSessionAsync calls.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the Guest Additions were not found (or were not reported yet).
 * @param   pGuest              Guest object the guest session belongs to.
 * @param   ssInfo              Guest session startup info to use.
 * @param   guestCreds          Guest credentials to use for starting a guest session
 *                              with a specific guest account.
 */
int GuestSession::init(Guest *pGuest, const GuestSessionStartupInfo &ssInfo,
                       const GuestCredentials &guestCreds)
{
    LogFlowThisFunc(("pGuest=%p, ssInfo=%p, guestCreds=%p\n",
                      pGuest, &ssInfo, &guestCreds));

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    AssertPtrReturn(pGuest, VERR_INVALID_POINTER);

    /*
     * Initialize our data members from the input.
     */
    mParent  = pGuest;
    mConsole = pGuest->i_getConsole();

    /* Copy over startup info. */
    /** @todo Use an overloaded copy operator. Later. */
    mData.mSession.mID = ssInfo.mID;
    mData.mSession.mIsInternal = ssInfo.mIsInternal;
    mData.mSession.mName = ssInfo.mName;
    mData.mSession.mOpenFlags = ssInfo.mOpenFlags;
    mData.mSession.mOpenTimeoutMS = ssInfo.mOpenTimeoutMS;

    /* Copy over session credentials. */
    /** @todo Use an overloaded copy operator. Later. */
    mData.mCredentials.mUser = guestCreds.mUser;
    mData.mCredentials.mPassword = guestCreds.mPassword;
    mData.mCredentials.mDomain = guestCreds.mDomain;

    /* Initialize the remainder of the data. */
    mData.mVrc = VINF_SUCCESS;
    mData.mStatus = GuestSessionStatus_Undefined;
    mData.mpBaseEnvironment = NULL;

    /*
     * Register an object for the session itself to clearly
     * distinguish callbacks which are for this session directly, or for
     * objects (like files, directories, ...) which are bound to this session.
     */
    int vrc = i_objectRegister(NULL /* pObject */, SESSIONOBJECTTYPE_SESSION, &mData.mObjectID);
    if (RT_SUCCESS(vrc))
    {
        vrc = mData.mEnvironmentChanges.initChangeRecord(pGuest->i_isGuestInWindowsNtFamily()
                                                         ? RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR : 0);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTCritSectInit(&mWaitEventCritSect);
            AssertRC(vrc);
        }
    }

    if (RT_SUCCESS(vrc))
        vrc = i_determineProtocolVersion();

    if (RT_SUCCESS(vrc))
    {
        /*
         * <Replace this if you figure out what the code is doing.>
         */
        HRESULT hrc = unconst(mEventSource).createObject();
        if (SUCCEEDED(hrc))
            hrc = mEventSource->init();
        if (SUCCEEDED(hrc))
        {
            try
            {
                GuestSessionListener *pListener = new GuestSessionListener();
                ComObjPtr<GuestSessionListenerImpl> thisListener;
                hrc = thisListener.createObject();
                if (SUCCEEDED(hrc))
                    hrc = thisListener->init(pListener, this); /* thisListener takes ownership of pListener. */
                if (SUCCEEDED(hrc))
                {
                    com::SafeArray <VBoxEventType_T> eventTypes;
                    eventTypes.push_back(VBoxEventType_OnGuestSessionStateChanged);
                    hrc = mEventSource->RegisterListener(thisListener,
                                                         ComSafeArrayAsInParam(eventTypes),
                                                         TRUE /* Active listener */);
                    if (SUCCEEDED(hrc))
                    {
                        mLocalListener = thisListener;

                        /*
                         * Mark this object as operational and return success.
                         */
                        autoInitSpan.setSucceeded();
                        LogFlowThisFunc(("mName=%s mID=%RU32 mIsInternal=%RTbool vrc=VINF_SUCCESS\n",
                                         mData.mSession.mName.c_str(), mData.mSession.mID, mData.mSession.mIsInternal));
                        return VINF_SUCCESS;
                    }
                }
            }
            catch (std::bad_alloc &)
            {
                hrc = E_OUTOFMEMORY;
            }
        }
        vrc = Global::vboxStatusCodeFromCOM(hrc);
    }

    autoInitSpan.setFailed();
    LogThisFunc(("Failed! mName=%s mID=%RU32 mIsInternal=%RTbool => vrc=%Rrc\n",
                 mData.mSession.mName.c_str(), mData.mSession.mID, mData.mSession.mIsInternal, vrc));
    return vrc;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestSession::uninit(void)
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    /* Call i_onRemove to take care of the object cleanups. */
    i_onRemove();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Unregister the session's object ID. */
    i_objectUnregister(mData.mObjectID);

    Assert(mData.mObjects.size () == 0);
    mData.mObjects.clear();

    mData.mEnvironmentChanges.reset();

    if (mData.mpBaseEnvironment)
    {
        mData.mpBaseEnvironment->releaseConst();
        mData.mpBaseEnvironment = NULL;
    }

    /* Unitialize our local listener. */
    mLocalListener.setNull();

    baseUninit();

    LogFlowFuncLeave();
}

// implementation of public getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestSession::getUser(com::Utf8Str &aUser)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aUser = mData.mCredentials.mUser;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getDomain(com::Utf8Str &aDomain)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDomain = mData.mCredentials.mDomain;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getName(com::Utf8Str &aName)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = mData.mSession.mName;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getId(ULONG *aId)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aId = mData.mSession.mID;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getStatus(GuestSessionStatus_T *aStatus)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aStatus = mData.mStatus;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getTimeout(ULONG *aTimeout)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aTimeout = mData.mTimeout;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::setTimeout(ULONG aTimeout)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData.mTimeout = aTimeout;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getProtocolVersion(ULONG *aProtocolVersion)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aProtocolVersion = mData.mProtocolVersion;

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestSession::getEnvironmentChanges(std::vector<com::Utf8Str> &aEnvironmentChanges)
{
    LogFlowThisFuncEnter();

    int vrc;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        vrc = mData.mEnvironmentChanges.queryPutEnvArray(&aEnvironmentChanges);
    }

    LogFlowFuncLeaveRC(vrc);
    return Global::vboxStatusCodeToCOM(vrc);
}

HRESULT GuestSession::setEnvironmentChanges(const std::vector<com::Utf8Str> &aEnvironmentChanges)
{
    LogFlowThisFuncEnter();

    int vrc;
    size_t idxError = ~(size_t)0;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        mData.mEnvironmentChanges.reset();
        vrc = mData.mEnvironmentChanges.applyPutEnvArray(aEnvironmentChanges, &idxError);
    }

    LogFlowFuncLeaveRC(vrc);
    if (RT_SUCCESS(vrc))
        return S_OK;
    if (vrc == VERR_ENV_INVALID_VAR_NAME)
        return setError(E_INVALIDARG, tr("Invalid environment variable name '%s', index %zu"),
                        aEnvironmentChanges[idxError].c_str(), idxError);
    return setErrorBoth(Global::vboxStatusCodeToCOM(vrc), vrc, tr("Failed to apply '%s', index %zu (%Rrc)"),
                        aEnvironmentChanges[idxError].c_str(), idxError, vrc);
}

HRESULT GuestSession::getEnvironmentBase(std::vector<com::Utf8Str> &aEnvironmentBase)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    HRESULT hrc;
    if (mData.mpBaseEnvironment)
    {
        int vrc = mData.mpBaseEnvironment->queryPutEnvArray(&aEnvironmentBase);
        hrc = Global::vboxStatusCodeToCOM(vrc);
    }
    else if (mData.mProtocolVersion < 99999)
        hrc = setError(VBOX_E_NOT_SUPPORTED, tr("The base environment feature is not supported by the Guest Additions"));
    else
        hrc = setError(VBOX_E_INVALID_OBJECT_STATE, tr("The base environment has not yet been reported by the guest"));

    LogFlowFuncLeave();
    return hrc;
}

HRESULT GuestSession::getProcesses(std::vector<ComPtr<IGuestProcess> > &aProcesses)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aProcesses.resize(mData.mProcesses.size());
    size_t i = 0;
    for (SessionProcesses::iterator it  = mData.mProcesses.begin();
                                    it != mData.mProcesses.end();
                                    ++it, ++i)
    {
        it->second.queryInterfaceTo(aProcesses[i].asOutParam());
    }

    LogFlowFunc(("mProcesses=%zu\n", aProcesses.size()));
    return S_OK;
}

HRESULT GuestSession::getPathStyle(PathStyle_T *aPathStyle)
{
    *aPathStyle = i_getGuestPathStyle();
    return S_OK;
}

HRESULT GuestSession::getCurrentDirectory(com::Utf8Str &aCurrentDirectory)
{
    RT_NOREF(aCurrentDirectory);
    ReturnComNotImplemented();
}

HRESULT GuestSession::setCurrentDirectory(const com::Utf8Str &aCurrentDirectory)
{
    RT_NOREF(aCurrentDirectory);
    ReturnComNotImplemented();
}

HRESULT GuestSession::getUserHome(com::Utf8Str &aUserHome)
{
    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_pathUserHome(aUserHome, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                switch (vrcGuest)
                {
                    case VERR_NOT_SUPPORTED:
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                          tr("Getting the user's home path is not supported by installed Guest Additions"));
                        break;

                    default:
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                          tr("Getting the user's home path failed on the guest: %Rrc"), vrcGuest);
                        break;
                }
                break;
            }

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Getting the user's home path failed: %Rrc"), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::getUserDocuments(com::Utf8Str &aUserDocuments)
{
    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_pathUserDocuments(aUserDocuments, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                switch (vrcGuest)
                {
                    case VERR_NOT_SUPPORTED:
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                          tr("Getting the user's documents path is not supported by installed Guest Additions"));
                        break;

                    default:
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                          tr("Getting the user's documents path failed on the guest: %Rrc"), vrcGuest);
                        break;
                }
                break;
            }

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Getting the user's documents path failed: %Rrc"), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::getMountPoints(std::vector<com::Utf8Str> &aMountPoints)
{
    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_getMountPoints(aMountPoints, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                switch (vrcGuest)
                {
                    case VERR_NOT_SUPPORTED:
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                          tr("Getting the mount points is not supported by installed Guest Additions"));
                        break;

                    default:
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                          tr("Getting the mount points failed on the guest: %Rrc"), vrcGuest);
                        break;
                }
                break;
            }

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Getting the mount points failed: %Rrc"), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::getDirectories(std::vector<ComPtr<IGuestDirectory> > &aDirectories)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDirectories.resize(mData.mDirectories.size());
    size_t i = 0;
    for (SessionDirectories::iterator it = mData.mDirectories.begin(); it != mData.mDirectories.end(); ++it, ++i)
    {
        it->second.queryInterfaceTo(aDirectories[i].asOutParam());
    }

    LogFlowFunc(("mDirectories=%zu\n", aDirectories.size()));
    return S_OK;
}

HRESULT GuestSession::getFiles(std::vector<ComPtr<IGuestFile> > &aFiles)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFiles.resize(mData.mFiles.size());
    size_t i = 0;
    for(SessionFiles::iterator it = mData.mFiles.begin(); it != mData.mFiles.end(); ++it, ++i)
        it->second.queryInterfaceTo(aFiles[i].asOutParam());

    LogFlowFunc(("mDirectories=%zu\n", aFiles.size()));

    return S_OK;
}

HRESULT GuestSession::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    LogFlowThisFuncEnter();

    // no need to lock - lifetime constant
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());

    LogFlowThisFuncLeave();
    return S_OK;
}

// private methods
///////////////////////////////////////////////////////////////////////////////

/**
 * Closes a guest session on the guest.
 *
 * @returns VBox status code.
 * @param   uFlags              Guest session close flags.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_closeSession(uint32_t uFlags, uint32_t uTimeoutMS, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);

    LogFlowThisFunc(("uFlags=%x, uTimeoutMS=%RU32\n", uFlags, uTimeoutMS));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Guest Additions < 4.3 don't support closing dedicated
       guest sessions, skip. */
    if (mData.mProtocolVersion < 2)
    {
        LogFlowThisFunc(("Installed Guest Additions don't support closing dedicated sessions, skipping\n"));
        return VINF_SUCCESS;
    }

    /** @todo uFlags validation. */

    if (mData.mStatus != GuestSessionStatus_Started)
    {
        LogFlowThisFunc(("Session ID=%RU32 not started (anymore), status now is: %RU32\n",
                         mData.mSession.mID, mData.mStatus));
        return VINF_SUCCESS;
    }

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestSessionStateChanged);

        vrc = registerWaitEventEx(mData.mSession.mID, mData.mObjectID, eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    LogFlowThisFunc(("Sending closing request to guest session ID=%RU32, uFlags=%x\n",
                     mData.mSession.mID, uFlags));

    alock.release();

    VBOXHGCMSVCPARM paParms[4];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], uFlags);

    vrc = i_sendMessage(HOST_MSG_SESSION_CLOSE, i, paParms, VBOX_GUESTCTRL_DST_BOTH);
    if (RT_SUCCESS(vrc))
        vrc = i_waitForStatusChange(pEvent, GuestSessionWaitForFlag_Terminate, uTimeoutMS,
                                    NULL /* Session status */, pvrcGuest);

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Internal worker function for public APIs that handle copying elements from
 * guest to the host.
 *
 * @return HRESULT
 * @param  SourceSet            Source set specifying what to copy.
 * @param  strDestination       Destination path on the host. Host path style.
 * @param  pProgress            Progress object returned to the caller.
 */
HRESULT GuestSession::i_copyFromGuest(const GuestSessionFsSourceSet &SourceSet,
                                      const com::Utf8Str &strDestination, ComPtr<IProgress> &pProgress)
{
    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    /* Validate stuff. */
    if (RT_UNLIKELY(SourceSet.size() == 0 || *(SourceSet[0].strSource.c_str()) == '\0')) /* At least one source must be present. */
        return setError(E_INVALIDARG, tr("No source(s) specified"));
    if (RT_UNLIKELY((strDestination.c_str()) == NULL || *(strDestination.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No destination specified"));

    GuestSessionFsSourceSet::const_iterator itSrc = SourceSet.begin();
    while (itSrc != SourceSet.end())
    {
        LogRel2(("Guest Control: Copying '%s' from guest to '%s' on the host (type: %s, filter: %s)\n",
                 itSrc->strSource.c_str(), strDestination.c_str(), GuestBase::fsObjTypeToStr(itSrc->enmType), itSrc->strFilter.c_str()));
        ++itSrc;
    }

    /* Create a task and return the progress obejct for it. */
    GuestSessionTaskCopyFrom *pTask = NULL;
    try
    {
        pTask = new GuestSessionTaskCopyFrom(this /* GuestSession */, SourceSet, strDestination);
    }
    catch (std::bad_alloc &)
    {
        return setError(E_OUTOFMEMORY, tr("Failed to create GuestSessionTaskCopyFrom object"));
    }

    try
    {
        hrc = pTask->Init(Utf8StrFmt(tr("Copying to \"%s\" on the host"), strDestination.c_str()));
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hrc))
    {
        ComObjPtr<Progress> ptrProgressObj = pTask->GetProgressObject();

        /* Kick off the worker thread. Note! Consumes pTask. */
        hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_HEAVY_WORKER);
        pTask = NULL;
        if (SUCCEEDED(hrc))
            hrc = ptrProgressObj.queryInterfaceTo(pProgress.asOutParam());
        else
            hrc = setError(hrc, tr("Starting thread for copying from guest to the host failed"));
    }
    else
    {
        hrc = setError(hrc, tr("Initializing GuestSessionTaskCopyFrom object failed"));
        delete pTask;
    }

    LogFlowFunc(("Returning %Rhrc\n", hrc));
    return hrc;
}

/**
 * Internal worker function for public APIs that handle copying elements from
 * host to the guest.
 *
 * @return HRESULT
 * @param  SourceSet            Source set specifying what to copy.
 * @param  strDestination       Destination path on the guest. Guest path style.
 * @param  pProgress            Progress object returned to the caller.
 */
HRESULT GuestSession::i_copyToGuest(const GuestSessionFsSourceSet &SourceSet,
                                    const com::Utf8Str &strDestination, ComPtr<IProgress> &pProgress)
{
    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    GuestSessionFsSourceSet::const_iterator itSrc = SourceSet.begin();
    while (itSrc != SourceSet.end())
    {
        LogRel2(("Guest Control: Copying '%s' from host to '%s' on the guest (type: %s, filter: %s)\n",
                 itSrc->strSource.c_str(), strDestination.c_str(), GuestBase::fsObjTypeToStr(itSrc->enmType), itSrc->strFilter.c_str()));
        ++itSrc;
    }

    /* Create a task and return the progress object for it. */
    GuestSessionTaskCopyTo *pTask = NULL;
    try
    {
        pTask = new GuestSessionTaskCopyTo(this /* GuestSession */, SourceSet, strDestination);
    }
    catch (std::bad_alloc &)
    {
        return setError(E_OUTOFMEMORY, tr("Failed to create GuestSessionTaskCopyTo object"));
    }

    try
    {
        hrc = pTask->Init(Utf8StrFmt(tr("Copying to \"%s\" on the guest"), strDestination.c_str()));
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hrc))
    {
        ComObjPtr<Progress> ptrProgressObj = pTask->GetProgressObject();

        /* Kick off the worker thread. Note! Consumes pTask. */
        hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_HEAVY_WORKER);
        pTask = NULL;
        if (SUCCEEDED(hrc))
            hrc = ptrProgressObj.queryInterfaceTo(pProgress.asOutParam());
        else
            hrc = setError(hrc, tr("Starting thread for copying from host to the guest failed"));
    }
    else
    {
        hrc = setError(hrc, tr("Initializing GuestSessionTaskCopyTo object failed"));
        delete pTask;
    }

    LogFlowFunc(("Returning %Rhrc\n", hrc));
    return hrc;
}

/**
 * Validates and extracts directory copy flags from a comma-separated string.
 *
 * @return COM status, error set on failure
 * @param  strFlags             String to extract flags from.
 * @param  fStrict              Whether to set an error when an unknown / invalid flag is detected.
 * @param  pfFlags              Where to store the extracted (and validated) flags.
 */
HRESULT GuestSession::i_directoryCopyFlagFromStr(const com::Utf8Str &strFlags, bool fStrict, DirectoryCopyFlag_T *pfFlags)
{
    unsigned fFlags = DirectoryCopyFlag_None;

    /* Validate and set flags. */
    if (strFlags.isNotEmpty())
    {
        const char *pszNext = strFlags.c_str();
        for (;;)
        {
            /* Find the next keyword, ignoring all whitespace. */
            pszNext = RTStrStripL(pszNext);

            const char * const pszComma = strchr(pszNext, ',');
            size_t cchKeyword = pszComma ? pszComma - pszNext : strlen(pszNext);
            while (cchKeyword > 0 && RT_C_IS_SPACE(pszNext[cchKeyword - 1]))
                cchKeyword--;

            if (cchKeyword > 0)
            {
                /* Convert keyword to flag. */
#define MATCH_KEYWORD(a_szKeyword) (   cchKeyword == sizeof(a_szKeyword) - 1U \
                                    && memcmp(pszNext, a_szKeyword, sizeof(a_szKeyword) - 1U) == 0)
                if (MATCH_KEYWORD("CopyIntoExisting"))
                    fFlags |= (unsigned)DirectoryCopyFlag_CopyIntoExisting;
                else if (MATCH_KEYWORD("Recursive"))
                    fFlags |= (unsigned)DirectoryCopyFlag_Recursive;
                else if (MATCH_KEYWORD("FollowLinks"))
                    fFlags |= (unsigned)DirectoryCopyFlag_FollowLinks;
                else if (fStrict)
                    return setError(E_INVALIDARG, tr("Invalid directory copy flag: %.*s"), (int)cchKeyword, pszNext);
#undef MATCH_KEYWORD
            }
            if (!pszComma)
                break;
            pszNext = pszComma + 1;
        }
    }

    if (pfFlags)
        *pfFlags = (DirectoryCopyFlag_T)fFlags;
    return S_OK;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Creates a directory on the guest.
 *
 * @returns VBox status code.
 * @param   strPath             Path on guest to directory to create.
 * @param   uMode               Creation mode to use (octal, 0777 max).
 * @param   uFlags              Directory creation flags to use.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned.
 */
int GuestSession::i_directoryCreateViaToolbox(const Utf8Str &strPath, uint32_t uMode, uint32_t uFlags, int *pvrcGuest)
{
    int vrc = VINF_SUCCESS;

    GuestProcessStartupInfo procInfo;
    procInfo.mFlags      = ProcessCreateFlag_Hidden;
    procInfo.mExecutable = VBOXSERVICE_TOOL_MKDIR;

    try
    {
        procInfo.mArguments.push_back(procInfo.mExecutable); /* Set argv0. */

        /* Construct arguments. */
        if (uFlags)
        {
            if (uFlags & DirectoryCreateFlag_Parents)
                procInfo.mArguments.push_back(Utf8Str("--parents")); /* We also want to create the parent directories. */
            else
                vrc = VERR_INVALID_PARAMETER;
        }

        if (   RT_SUCCESS(vrc)
            && uMode)
        {
            procInfo.mArguments.push_back(Utf8Str("--mode")); /* Set the creation mode. */

            char szMode[16];
            if (RTStrPrintf(szMode, sizeof(szMode), "%o", uMode))
                procInfo.mArguments.push_back(Utf8Str(szMode));
            else
                vrc = VERR_BUFFER_OVERFLOW;
        }

        procInfo.mArguments.push_back("--");    /* '--version' is a valid directory name. */
        procInfo.mArguments.push_back(strPath); /* The directory we want to create. */
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(vrc))
        vrc = GuestProcessToolbox::runTool(this, procInfo, pvrcGuest);

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Creates a directory on the guest.
 *
 * @returns VBox status code.
 * @param   strPath             Path on guest to directory to create.
 * @param   uMode               Creation mode to use (octal, 0777 max).
 * @param   uFlags              Directory creation flags to use.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned.
 */
int GuestSession::i_directoryCreate(const Utf8Str &strPath, uint32_t uMode, uint32_t uFlags, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);
    LogFlowThisFunc(("strPath=%s, uMode=%x, uFlags=%x\n", strPath.c_str(), uMode, uFlags));
    *pvrcGuest = VERR_IPE_UNINITIALIZED_STATUS;

    int vrc = VINF_SUCCESS;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        GuestWaitEvent *pEvent = NULL;
        vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
        if (RT_FAILURE(vrc))
            return vrc;

        uint32_t fFlags = GSTCTL_CREATEDIRECTORY_F_NONE;
        if (uFlags & DirectoryCreateFlag_Parents)
            fFlags |= GSTCTL_CREATEDIRECTORY_F_PARENTS;
        Assert(!(fFlags & ~GSTCTL_CREATEDIRECTORY_F_VALID_MASK));

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[4];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetPv (&paParms[i++], (void*)strPath.c_str(), (ULONG)strPath.length() + 1);
        HGCMSvcSetU32(&paParms[i++], uMode);
        HGCMSvcSetU32(&paParms[i++], fFlags);
        alock.release(); /* Drop lock before sending. */

        vrc = i_sendMessage(HOST_MSG_DIR_CREATE, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                // Nothing to do here.
            }
            else if (pEvent->HasGuestError())
                *pvrcGuest = pEvent->GuestResult();
        }
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_directoryCreateViaToolbox(strPath, uMode, uFlags, pvrcGuest);
#else
        RT_NOREF(uMode, uFlags, pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    LogFlowFunc(("LEAVE: %Rrc *pvrcGuest=%Rrc\n", vrc, pvrcGuest ? *pvrcGuest : -VERR_IPE_UNINITIALIZED_STATUS));
    return vrc;
}

/**
 * Checks if a directory on the guest exists.
 *
 * @returns \c true if directory exists on the guest, \c false if not.
 * @param   strPath             Path of directory to check.
 */
bool GuestSession::i_directoryExists(const Utf8Str &strPath)
{
    GuestFsObjData objDataIgnored;
    int vrcGuestIgnored;
    int const vrc = i_directoryQueryInfo(strPath, true /* fFollowSymlinks */, objDataIgnored, &vrcGuestIgnored);
    return RT_SUCCESS(vrc);
}

/**
 * Checks if a directory object exists and optionally returns its object.
 *
 * @returns \c true if directory object exists, or \c false if not.
 * @param   uDirID              ID of directory object to check.
 * @param   pDir                Where to return the found directory object on success.
 */
inline bool GuestSession::i_directoryExists(uint32_t uDirID, ComObjPtr<GuestDirectory> *pDir)
{
    SessionDirectories::const_iterator it = mData.mDirectories.find(uDirID);
    if (it != mData.mDirectories.end())
    {
        if (pDir)
            *pDir = it->second;
        return true;
    }
    return false;
}

/**
 * Queries information about a directory on the guest.
 *
 * @returns VBox status code, or VERR_NOT_A_DIRECTORY if the file system object exists but is not a directory.
 * @param   strPath             Path to directory to query information for.
 * @param   fFollowSymlinks     Whether to follow symlinks or not.
 * @param   objData             Where to store the information returned on success.
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR.
 */
int GuestSession::i_directoryQueryInfo(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);

    LogFlowThisFunc(("strPath=%s, fFollowSymlinks=%RTbool\n", strPath.c_str(), fFollowSymlinks));

    int vrc = i_fsObjQueryInfo(strPath, fFollowSymlinks, objData, pvrcGuest);
    if (RT_SUCCESS(vrc))
    {
        vrc = objData.mType == FsObjType_Directory
            ? VINF_SUCCESS : VERR_NOT_A_DIRECTORY;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Unregisters a directory object from a guest session.
 *
 * @returns VBox status code. VERR_NOT_FOUND if the directory is not registered (anymore).
 * @param   pDirectory          Directory object to unregister from session.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_directoryUnregister(GuestDirectory *pDirectory)
{
    AssertPtrReturn(pDirectory, VERR_INVALID_POINTER);

    LogFlowThisFunc(("pDirectory=%p\n", pDirectory));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const uint32_t idObject = pDirectory->getObjectID();

    LogFlowFunc(("Removing directory (objectID=%RU32) ...\n", idObject));

    int vrc = i_objectUnregister(idObject);
    if (RT_FAILURE(vrc))
        return vrc;

    SessionDirectories::iterator itDirs = mData.mDirectories.find(idObject);
    AssertReturn(itDirs != mData.mDirectories.end(), VERR_NOT_FOUND);

    /* Make sure to consume the pointer before the one of the iterator gets released. */
    ComObjPtr<GuestDirectory> pDirConsumed = pDirectory;

    LogFlowFunc(("Removing directory ID=%RU32 (session %RU32, now total %zu directories)\n",
                 idObject, mData.mSession.mID, mData.mDirectories.size()));

    vrc = pDirConsumed->i_onUnregister();
    AssertRCReturn(vrc, vrc);

    mData.mDirectories.erase(itDirs);

    alock.release(); /* Release lock before firing off event. */

//    ::FireGuestDirectoryRegisteredEvent(mEventSource, this /* Session */, pDirConsumed, false /* Process unregistered */);

    pDirConsumed.setNull();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Removes a directory on the guest.
 *
 * @returns VBox status code.
 * @param   strPath             Path of directory on guest to remove.
 * @param   fFlags              Directory remove flags to use.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_directoryRemove(const Utf8Str &strPath, uint32_t fFlags, int *pvrcGuest)
{
    AssertReturn(!(fFlags & ~DIRREMOVEREC_FLAG_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);

    LogFlowThisFunc(("strPath=%s, uFlags=0x%x\n", strPath.c_str(), fFlags));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    GuestWaitEvent *pEvent = NULL;
    int vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[8];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetPv(&paParms[i++], (void*)strPath.c_str(),
                            (ULONG)strPath.length() + 1);
    HGCMSvcSetU32(&paParms[i++], fFlags);

    alock.release(); /* Drop lock before sending. */

    vrc = i_sendMessage(HOST_MSG_DIR_REMOVE, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
        if (pEvent->HasGuestError())
            *pvrcGuest = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Creates a temporary directory / file on the guest (legacy version).
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   strTemplate         Name template to use.
 *                              \sa RTDirCreateTemp / RTDirCreateTempSecure.
 * @param   strPath             Path where to create the temporary directory / file.
 * @param   fDirectory          Whether to create a temporary directory or file.
 * @param   strName             Where to return the created temporary name on success.
 * @param   fMode               File mode to use for creation (octal, umask-style).
 *                              Ignored when \a fSecure is specified.
 * @param   fSecure             Whether to perform a secure creation or not.
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR.
 */
int GuestSession::i_fsCreateTempViaToolbox(const Utf8Str &strTemplate, const Utf8Str &strPath, bool fDirectory, Utf8Str &strName,
                                           uint32_t fMode, bool fSecure, int *pvrcGuest)
{
    int vrc;

    GuestProcessStartupInfo procInfo;
    procInfo.mFlags = ProcessCreateFlag_WaitForStdOut;
    try
    {
        procInfo.mExecutable = Utf8Str(VBOXSERVICE_TOOL_MKTEMP);
        procInfo.mArguments.push_back(procInfo.mExecutable); /* Set argv0. */
        procInfo.mArguments.push_back(Utf8Str("--machinereadable"));
        if (fDirectory)
            procInfo.mArguments.push_back(Utf8Str("-d"));
        if (strPath.length()) /* Otherwise use /tmp or equivalent. */
        {
            procInfo.mArguments.push_back(Utf8Str("-t"));
            procInfo.mArguments.push_back(strPath);
        }
        /* Note: Secure flag and mode cannot be specified at the same time. */
        if (fSecure)
        {
            procInfo.mArguments.push_back(Utf8Str("--secure"));
        }
        else
        {
            procInfo.mArguments.push_back(Utf8Str("--mode"));

            /* Note: Pass the mode unmodified down to the guest. See @ticketref{21394}. */
            char szMode[16];
            vrc = RTStrPrintf2(szMode, sizeof(szMode), "%d", fMode);
            AssertRCReturn(vrc, vrc);
            procInfo.mArguments.push_back(szMode);
        }
        procInfo.mArguments.push_back("--"); /* strTemplate could be '--help'. */
        procInfo.mArguments.push_back(strTemplate);
    }
    catch (std::bad_alloc &)
    {
        Log(("Out of memory!\n"));
        return VERR_NO_MEMORY;
    }

    GuestCtrlStreamObjects stdOut;
    int vrcGuest;
    vrc = GuestProcessToolbox::runTool(this, procInfo, &vrcGuest, &stdOut);
    if (!GuestProcess::i_isGuestError(vrc))
    {
        GuestFsObjData objData;
        if (!stdOut.empty())
        {
            vrc = objData.FromToolboxMkTemp(stdOut.at(0));
            if (RT_FAILURE(vrc))
            {
                vrcGuest = vrc;
                if (pvrcGuest)
                    *pvrcGuest = vrcGuest;
                vrc = VERR_GSTCTL_GUEST_ERROR;
            }
        }
        else
            vrc = VERR_BROKEN_PIPE;

        if (RT_SUCCESS(vrc))
            strName = objData.mName;
    }
    else if (pvrcGuest)
        *pvrcGuest = vrcGuest;

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Creates a temporary directory / file on the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   strTemplate         Name template to use.
 *                              \sa RTDirCreateTemp / RTDirCreateTempSecure.
 * @param   strPath             Path where to create the temporary directory / file.
 * @param   fDirectory          Whether to create a temporary directory or file.
 * @param   strName             Where to return the created temporary name on success.
 * @param   fMode               File mode to use for creation (octal, umask-style).
 *                              Ignored when \a fSecure is specified.
 * @param   fSecure             Whether to perform a secure creation or not.
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR.
 */
int GuestSession::i_fsCreateTemp(const Utf8Str &strTemplate, const Utf8Str &strPath, bool fDirectory, Utf8Str &strName,
                                 uint32_t fMode, bool fSecure, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);
    AssertReturn(fSecure || !(fMode & ~07777), VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("strTemplate=%s, strPath=%s, fDirectory=%RTbool, fMode=%o, fSecure=%RTbool\n",
                     strTemplate.c_str(), strPath.c_str(), fDirectory, fMode, fSecure));

    int vrc;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        GuestWaitEvent *pEvent = NULL;
        vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
        if (RT_FAILURE(vrc))
            return vrc;

        uint32_t fFlags = GSTCTL_CREATETEMP_F_NONE;
        if (fDirectory)
            fFlags |= GSTCTL_CREATETEMP_F_DIRECTORY;
        if (fSecure)
            fFlags |= GSTCTL_CREATETEMP_F_SECURE;
        Assert(!(fFlags & ~GSTCTL_CREATETEMP_F_VALID_MASK));

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[8];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetStr(&paParms[i++], strTemplate.c_str());
        HGCMSvcSetStr(&paParms[i++], strPath.c_str());
        HGCMSvcSetU32(&paParms[i++], fFlags);
        HGCMSvcSetU32(&paParms[i++], fMode);

        alock.release(); /* Drop lock before sending. */

        vrc = i_sendMessage(HOST_MSG_FS_CREATE_TEMP, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                PCALLBACKDATA_FS_NOTIFY const pFsNotify = (PCALLBACKDATA_FS_NOTIFY)pEvent->Payload().Raw();
                AssertPtrReturn(pFsNotify, VERR_INVALID_POINTER);
                int vrcGuest = (int)pFsNotify->rc;
                if (RT_SUCCESS(vrcGuest))
                {
                    AssertReturn(pFsNotify->uType == GUEST_FS_NOTIFYTYPE_CREATE_TEMP, VERR_INVALID_PARAMETER);
                    AssertReturn(pFsNotify->u.CreateTemp.cbPath, VERR_INVALID_PARAMETER);
                    strName = pFsNotify->u.CreateTemp.pszPath;
                    RTStrFree(pFsNotify->u.CreateTemp.pszPath);
                }
                else
                {
                    *pvrcGuest = vrcGuest;
                    vrc = VERR_GSTCTL_GUEST_ERROR;
                }
            }
            else
            {
                if (pEvent->HasGuestError() && pvrcGuest)
                    *pvrcGuest = pEvent->GuestResult();
            }
        }
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_fsCreateTempViaToolbox(strTemplate, strPath, fDirectory, strName, fMode, fSecure, pvrcGuest);
#else
        RT_NOREF(strTemplate, strPath, fDirectory, strName, fMode, fSecure, pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    LogFlowThisFunc(("Returning vrc=%Rrc, vrcGuest=%Rrc\n", vrc, pvrcGuest ? *pvrcGuest : VERR_IPE_UNINITIALIZED_STATUS));
    return vrc;
}

/**
 * Open a directory on the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   openInfo            Open information to use.
 * @param   pDirectory          Where to return the guest directory object on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_directoryOpen(const GuestDirectoryOpenInfo &openInfo, ComObjPtr<GuestDirectory> &pDirectory, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);

    LogFlowThisFunc(("strPath=%s, strPath=%s, uFlags=%x\n", openInfo.mPath.c_str(), openInfo.mFilter.c_str(), openInfo.mFlags));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Create the directory object. */
    HRESULT hrc = pDirectory.createObject();
    if (FAILED(hrc))
        return Global::vboxStatusCodeFromCOM(hrc);

    /* Register a new object ID. */
    uint32_t idObject;
    int vrc = i_objectRegister(pDirectory, SESSIONOBJECTTYPE_DIRECTORY, &idObject);
    if (RT_FAILURE(vrc))
    {
        pDirectory.setNull();
        return vrc;
    }

    /**
     * If VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT is enabled:
     *
     * We need to release the write lock first before opening the directory object below,
     * as we're starting a guest process as part of it. This in turn will try to acquire the session's
     * write lock. */
    alock.release();

    vrc = pDirectory->init(mConsole, this /* Parent */, idObject, openInfo);
    if (RT_SUCCESS(vrc))
        vrc = pDirectory->i_open(pvrcGuest);

    if (RT_FAILURE(vrc))
    {
        /* Make sure to acquire the write lock again before unregistering the object. */
        alock.acquire();

        int vrc2 = i_objectUnregister(idObject);
        AssertRC(vrc2);

        pDirectory.setNull();
    }
    else
    {
        /* Make sure to acquire the write lock again before continuing. */
        alock.acquire();

        try
        {
            /* Add the created directory to our map. */
            mData.mDirectories[idObject] = pDirectory;

            LogFlowFunc(("Added new guest directory \"%s\" (Session: %RU32) (now total %zu directories)\n",
                         openInfo.mPath.c_str(), mData.mSession.mID, mData.mDirectories.size()));

            alock.release(); /* Release lock before firing off event. */

            /** @todo Fire off a VBoxEventType_OnGuestDirectoryRegistered event? */
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        /* Nothing further to do here yet. */
        *pvrcGuest = VINF_SUCCESS;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Dispatches a host callback to its corresponding object.
 *
 * @return VBox status code. VERR_NOT_FOUND if no corresponding object was found.
 * @param  pCtxCb               Host callback context.
 * @param  pSvcCb               Service callback data.
 *
 * @note   Takes the read lock.
 */
int GuestSession::i_dispatchToObject(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    LogFlowFunc(("pCtxCb=%p, pSvcCb=%p\n", pCtxCb, pSvcCb));

    AssertPtrReturn(pCtxCb, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Find the object.
     */
    int vrc = VERR_NOT_FOUND;
    const uint32_t idObject = VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(pCtxCb->uContextID);
    SessionObjects::const_iterator itObj = mData.mObjects.find(idObject);
    if (itObj != mData.mObjects.end())
    {
        /* Set protocol version so that pSvcCb can be interpreted right. */
        pCtxCb->uProtocol = mData.mProtocolVersion;

        switch (itObj->second.enmType)
        {
            /* Note: The session object is special, as it does not inherit from GuestObject we could call
             *       its dispatcher for -- so treat this separately and call it directly. */
            case SESSIONOBJECTTYPE_SESSION:
            {
                alock.release();

                vrc = i_dispatchToThis(pCtxCb, pSvcCb);
                break;
            }
            case SESSIONOBJECTTYPE_DIRECTORY:
            {
                ComObjPtr<GuestDirectory> pObj((GuestDirectory *)itObj->second.pObject);
                AssertReturn(!pObj.isNull(), VERR_INVALID_POINTER);

                alock.release();

                vrc = pObj->i_callbackDispatcher(pCtxCb, pSvcCb);
                break;
            }
            case SESSIONOBJECTTYPE_FILE:
            {
                ComObjPtr<GuestFile> pObj((GuestFile *)itObj->second.pObject);
                AssertReturn(!pObj.isNull(), VERR_INVALID_POINTER);

                alock.release();

                vrc = pObj->i_callbackDispatcher(pCtxCb, pSvcCb);
                break;
            }
            case SESSIONOBJECTTYPE_PROCESS:
            {
                ComObjPtr<GuestProcess> pObj((GuestProcess *)itObj->second.pObject);
                AssertReturn(!pObj.isNull(), VERR_INVALID_POINTER);

                alock.release();

                vrc = pObj->i_callbackDispatcher(pCtxCb, pSvcCb);
                break;
            }
            default:
                AssertMsgFailed(("%d\n", itObj->second.enmType));
                vrc = VERR_INTERNAL_ERROR_4;
                break;
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Main handler for guest session messages from the guest.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context from HGCM service.
 * @param   pSvcCbData          HGCM service callback data.
 *
 * @note    No locking!
 */
int GuestSession::i_dispatchToThis(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    LogFlowThisFunc(("sessionID=%RU32, CID=%RU32, uMessage=%RU32, pSvcCb=%p\n",
                     mData.mSession.mID, pCbCtx->uContextID, pCbCtx->uMessage, pSvcCbData));
    int vrc;
    switch (pCbCtx->uMessage)
    {
        case GUEST_MSG_DISCONNECTED:
            /** @todo Handle closing all guest objects. */
            vrc = VERR_INTERNAL_ERROR;
            break;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        case GUEST_MSG_FS_NOTIFY:
            vrc = i_onFsNotify(pCbCtx, pSvcCbData);
            break;
#endif
        case GUEST_MSG_SESSION_NOTIFY: /* Guest Additions >= 4.3.0. */
            vrc = i_onSessionStatusChange(pCbCtx, pSvcCbData);
            break;

        default:
            vrc = dispatchGeneric(pCbCtx, pSvcCbData);
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Validates and extracts file copy flags from a comma-separated string.
 *
 * @return COM status, error set on failure
 * @param  strFlags             String to extract flags from.
 * @param  fStrict              Whether to set an error when an unknown / invalid flag is detected.
 * @param  pfFlags              Where to store the extracted (and validated) flags.
 */
HRESULT GuestSession::i_fileCopyFlagFromStr(const com::Utf8Str &strFlags, bool fStrict, FileCopyFlag_T *pfFlags)
{
    unsigned fFlags = (unsigned)FileCopyFlag_None;

    /* Validate and set flags. */
    if (strFlags.isNotEmpty())
    {
        const char *pszNext = strFlags.c_str();
        for (;;)
        {
            /* Find the next keyword, ignoring all whitespace. */
            pszNext = RTStrStripL(pszNext);

            const char * const pszComma = strchr(pszNext, ',');
            size_t cchKeyword = pszComma ? pszComma - pszNext : strlen(pszNext);
            while (cchKeyword > 0 && RT_C_IS_SPACE(pszNext[cchKeyword - 1]))
                cchKeyword--;

            if (cchKeyword > 0)
            {
                /* Convert keyword to flag. */
#define MATCH_KEYWORD(a_szKeyword) (   cchKeyword == sizeof(a_szKeyword) - 1U \
                                    && memcmp(pszNext, a_szKeyword, sizeof(a_szKeyword) - 1U) == 0)
                if (MATCH_KEYWORD("NoReplace"))
                    fFlags |= (unsigned)FileCopyFlag_NoReplace;
                else if (MATCH_KEYWORD("FollowLinks"))
                    fFlags |= (unsigned)FileCopyFlag_FollowLinks;
                else if (MATCH_KEYWORD("Update"))
                    fFlags |= (unsigned)FileCopyFlag_Update;
                else if (fStrict)
                    return setError(E_INVALIDARG, tr("Invalid file copy flag: %.*s"), (int)cchKeyword, pszNext);
#undef MATCH_KEYWORD
            }
            if (!pszComma)
                break;
            pszNext = pszComma + 1;
        }
    }

    if (pfFlags)
        *pfFlags = (FileCopyFlag_T)fFlags;
    return S_OK;
}

/**
 * Checks if a file object exists and optionally returns its object.
 *
 * @returns \c true if file object exists, or \c false if not.
 * @param   uFileID             ID of file object to check.
 * @param   pFile               Where to return the found file object on success.
 */
inline bool GuestSession::i_fileExists(uint32_t uFileID, ComObjPtr<GuestFile> *pFile)
{
    SessionFiles::const_iterator it = mData.mFiles.find(uFileID);
    if (it != mData.mFiles.end())
    {
        if (pFile)
            *pFile = it->second;
        return true;
    }
    return false;
}

/**
 * Unregisters a file object from a guest session.
 *
 * @returns VBox status code. VERR_NOT_FOUND if the file is not registered (anymore).
 * @param   pFile               File object to unregister from session.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_fileUnregister(GuestFile *pFile)
{
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);

    LogFlowThisFunc(("pFile=%p\n", pFile));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const uint32_t idObject = pFile->getObjectID();

    LogFlowFunc(("Removing file (objectID=%RU32) ...\n", idObject));

    int vrc = i_objectUnregister(idObject);
    if (RT_FAILURE(vrc))
        return vrc;

    SessionFiles::iterator itFiles = mData.mFiles.find(idObject);
    AssertReturn(itFiles != mData.mFiles.end(), VERR_NOT_FOUND);

    /* Make sure to consume the pointer before the one of the iterator gets released. */
    ComObjPtr<GuestFile> pFileConsumed = pFile;

    LogFlowFunc(("Removing file ID=%RU32 (session %RU32, now total %zu files)\n",
                 pFileConsumed->getObjectID(), mData.mSession.mID, mData.mFiles.size()));

    vrc = pFileConsumed->i_onUnregister();
    AssertRCReturn(vrc, vrc);

    mData.mFiles.erase(itFiles);

    alock.release(); /* Release lock before firing off event. */

    ::FireGuestFileRegisteredEvent(mEventSource, this, pFileConsumed, false /* Unregistered */);

    pFileConsumed.setNull();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Removes a file from the guest (legacy version).
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   strPath             Path of file on guest to remove.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestSession::i_fileRemoveViaToolbox(const Utf8Str &strPath, int *pvrcGuest)
{
    GuestProcessStartupInfo procInfo;
    GuestToolboxStream  streamOut;

    procInfo.mFlags      = ProcessCreateFlag_WaitForStdOut;
    procInfo.mExecutable = Utf8Str(VBOXSERVICE_TOOL_RM);

    try
    {
        procInfo.mArguments.push_back(procInfo.mExecutable); /* Set argv0. */
        procInfo.mArguments.push_back(Utf8Str("--machinereadable"));
        procInfo.mArguments.push_back("--"); /* strPath could be '--help', which is a valid filename. */
        procInfo.mArguments.push_back(strPath); /* The file we want to remove. */
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }

    GuestCtrlStreamObjects stdOut;
    int vrcGuest;
    int vrc = GuestProcessToolbox::runTool(this, procInfo, &vrcGuest, &stdOut);
    if (!GuestProcess::i_isGuestError(vrc))
    {
        if (!stdOut.empty())
        {
            GuestFsObjData objData;
            vrc = objData.FromToolboxRm(stdOut.at(0));
            if (RT_FAILURE(vrc))
            {
                vrcGuest = vrc;
                if (pvrcGuest)
                    *pvrcGuest = vrcGuest;
                vrc = VERR_GSTCTL_GUEST_ERROR;
            }
        }
        else
            vrc = VERR_BROKEN_PIPE;
    }
    else if (pvrcGuest)
        *pvrcGuest = vrcGuest;

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Removes a file from the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   strPath             Path of file on guest to remove.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestSession::i_fileRemove(const Utf8Str &strPath, int *pvrcGuest)
{
    LogFlowThisFunc(("strPath=%s\n", strPath.c_str()));

    int vrc;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        GuestWaitEvent *pEvent = NULL;
        vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[4];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetPv (&paParms[i++], (void*)strPath.c_str(), (ULONG)strPath.length() + 1);

        alock.release(); /* Drop lock before sending. */

        vrc = i_sendMessage(HOST_MSG_FILE_REMOVE, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                // Nothing to do here.
            }
            else if (pEvent->HasGuestError() && pvrcGuest)
                *pvrcGuest = pEvent->GuestResult();
        }
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_fileRemoveViaToolbox(strPath, pvrcGuest);
#else
        RT_NOREF(strPath, pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    LogFlowThisFunc(("Returning vrc=%Rrc, vrcGuest=%Rrc\n", vrc, pvrcGuest ? *pvrcGuest : VERR_IPE_UNINITIALIZED_STATUS));
    return vrc;
}

/**
 * Opens a file on the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   aPath               File path on guest to open.
 * @param   aAccessMode         Access mode to use.
 * @param   aOpenAction         Open action to use.
 * @param   aSharingMode        Sharing mode to use.
 * @param   aCreationMode       Creation mode to use.
 * @param   aFlags              Open flags to use.
 * @param   pFile               Where to return the file object on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_fileOpenEx(const com::Utf8Str &aPath, FileAccessMode_T aAccessMode, FileOpenAction_T aOpenAction,
                               FileSharingMode_T aSharingMode, ULONG aCreationMode, const std::vector<FileOpenExFlag_T> &aFlags,
                               ComObjPtr<GuestFile> &pFile, int *pvrcGuest)
{
    GuestFileOpenInfo openInfo;
    openInfo.mFilename     = aPath;
    openInfo.mCreationMode = aCreationMode;
    openInfo.mAccessMode   = aAccessMode;
    openInfo.mOpenAction   = aOpenAction;
    openInfo.mSharingMode  = aSharingMode;

    /* Combine and validate flags. */
    for (size_t i = 0; i < aFlags.size(); i++)
        openInfo.mfOpenEx |= aFlags[i];
    /* Validation is done in i_fileOpen(). */

    return i_fileOpen(openInfo, pFile, pvrcGuest);
}

/**
 * Opens a file on the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   openInfo            Open information to use for opening the file.
 * @param   pFile               Where to return the file object on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_fileOpen(const GuestFileOpenInfo &openInfo, ComObjPtr<GuestFile> &pFile, int *pvrcGuest)
{
    LogFlowThisFunc(("strFile=%s, enmAccessMode=0x%x, enmOpenAction=0x%x, uCreationMode=%RU32, mfOpenEx=%RU32\n",
                     openInfo.mFilename.c_str(), openInfo.mAccessMode, openInfo.mOpenAction, openInfo.mCreationMode,
                     openInfo.mfOpenEx));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Guest Additions < 4.3 don't support handling guest files, skip. */
    if (mData.mProtocolVersion < 2)
    {
        if (pvrcGuest)
            *pvrcGuest = VERR_NOT_SUPPORTED;
        return VERR_GSTCTL_GUEST_ERROR;
    }

    if (!openInfo.IsValid())
        return VERR_INVALID_PARAMETER;

    /* Create the directory object. */
    HRESULT hrc = pFile.createObject();
    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    /* Register a new object ID. */
    uint32_t idObject;
    int vrc = i_objectRegister(pFile, SESSIONOBJECTTYPE_FILE, &idObject);
    if (RT_FAILURE(vrc))
    {
        pFile.setNull();
        return vrc;
    }

    vrc = pFile->init(mConsole, this /* GuestSession */, idObject, openInfo);
    if (RT_FAILURE(vrc))
        return vrc;

    /*
     * Since this is a synchronous guest call we have to
     * register the file object first, releasing the session's
     * lock and then proceed with the actual opening command
     * -- otherwise the file's opening callback would hang
     * because the session's lock still is in place.
     */
    try
    {
        /* Add the created file to our vector. */
        mData.mFiles[idObject] = pFile;

        LogFlowFunc(("Added new guest file \"%s\" (Session: %RU32) (now total %zu files)\n",
                     openInfo.mFilename.c_str(), mData.mSession.mID, mData.mFiles.size()));

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestFileRegisteredEvent(mEventSource, this, pFile, true /* Registered */);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(vrc))
    {
        int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
        vrc = pFile->i_open(GSTCTL_DEFAULT_TIMEOUT_MS, &vrcGuest);
        if (   vrc == VERR_GSTCTL_GUEST_ERROR
            && pvrcGuest)
            *pvrcGuest = vrcGuest;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Queries information from a file on the guest.
 *
 * @returns IPRT status code. VERR_NOT_A_FILE if the queried file system object on the guest is not a file,
 *                            or VERR_GSTCTL_GUEST_ERROR if pvrcGuest contains
 *                            more error information from the guest.
 * @param   strPath           Absolute path of file to query information for.
 * @param   fFollowSymlinks   Whether or not to follow symbolic links on the guest.
 * @param   objData           Where to store the acquired information.
 * @param   pvrcGuest         Where to store the guest VBox status code.
 *                            Optional.
 */
int GuestSession::i_fileQueryInfo(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest)
{
    LogFlowThisFunc(("strPath=%s fFollowSymlinks=%RTbool\n", strPath.c_str(), fFollowSymlinks));

    int vrc = i_fsObjQueryInfo(strPath, fFollowSymlinks, objData, pvrcGuest);
    if (RT_SUCCESS(vrc))
        vrc = objData.mType == FsObjType_File ? VINF_SUCCESS : VERR_NOT_A_FILE;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Queries the size of a file on the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @retval  VERR_
 * @param   strPath             Path of file on guest to query size for.
 * @param   fFollowSymlinks     \c true when wanting to follow symbolic links, \c false if not.
 * @param   pllSize             Where to return the queried file size on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestSession::i_fileQuerySize(const Utf8Str &strPath, bool fFollowSymlinks, int64_t *pllSize, int *pvrcGuest)
{
    AssertPtrReturn(pllSize, VERR_INVALID_POINTER);

    GuestFsObjData objData;
    int vrc = i_fileQueryInfo(strPath, fFollowSymlinks, objData, pvrcGuest);
    if (RT_SUCCESS(vrc))
        *pllSize = objData.mObjectSize;

    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Queries information of a file system object (file, directory, ...). Legacy version.
 *
 * @return  IPRT status code.
 * @param   strPath             Path to file system object to query information for.
 * @param   fFollowSymlinks     Whether to follow symbolic links or not.
 * @param   objData             Where to return the file system object data, if found.
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR. Any other return code
 *                              indicates some host side error.
 */
int GuestSession::i_fsObjQueryInfoViaToolbox(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest)
{
    /** @todo Merge this with IGuestFile::queryInfo(). */
    GuestProcessStartupInfo procInfo;
    procInfo.mFlags = ProcessCreateFlag_WaitForStdOut;
    try
    {
        procInfo.mExecutable = VBOXSERVICE_TOOL_STAT;
        procInfo.mArguments.push_back(procInfo.mExecutable); /* Set argv0. */
        procInfo.mArguments.push_back(Utf8Str("--machinereadable"));
        if (fFollowSymlinks)
            procInfo.mArguments.push_back(Utf8Str("-L"));
        procInfo.mArguments.push_back("--"); /* strPath could be '--help', which is a valid filename. */
        procInfo.mArguments.push_back(strPath);
    }
    catch (std::bad_alloc &)
    {
        Log(("Out of memory!\n"));
        return VERR_NO_MEMORY;
    }

    GuestCtrlStreamObjects stdOut;
    int vrcGuest;
    int vrc = GuestProcessToolbox::runTool(this, procInfo, &vrcGuest, &stdOut);
    if (!GuestProcess::i_isGuestError(vrc))
    {
        if (!stdOut.empty())
        {
            vrc = objData.FromToolboxStat(stdOut.at(0));
            if (RT_FAILURE(vrc))
            {
                vrcGuest = vrc;
                if (pvrcGuest)
                    *pvrcGuest = vrcGuest;
                vrc = VERR_GSTCTL_GUEST_ERROR;
            }
        }
        else
            vrc = VERR_BROKEN_PIPE;
    }
    else if (pvrcGuest)
        *pvrcGuest = vrcGuest;

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Queries information of a guest file system.
 *
 * @return  IPRT status code.
 * @param   strPath             Path to file system object to query information for.
 * @param   pFsInfo             Where to return the file system information on success.
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR. Any other return code
 *                              indicates some host side error.
 */
int GuestSession::i_fsQueryInfo(const Utf8Str &strPath, PGSTCTLFSINFO pFsInfo, int *pvrcGuest)
{
    LogFlowThisFunc(("strPath=%s\n", strPath.c_str()));

    int vrc;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        GuestWaitEvent *pEvent = NULL;
        vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[2];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetStr(&paParms[i++], strPath.c_str());

        alock.release(); /* Drop lock before sending. */

        vrc = i_sendMessage(HOST_MSG_FS_QUERY_INFO, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                PCALLBACKDATA_FS_NOTIFY const pFsNotify = (PCALLBACKDATA_FS_NOTIFY)pEvent->Payload().Raw();
                AssertPtrReturn(pFsNotify, VERR_INVALID_POINTER);
                int vrcGuest = (int)pFsNotify->rc;
                if (RT_SUCCESS(vrcGuest))
                {
                    AssertReturn(pFsNotify->uType == GUEST_FS_NOTIFYTYPE_QUERY_INFO, VERR_INVALID_PARAMETER);
                    memcpy(pFsInfo, &pFsNotify->u.QueryInfo.fsInfo, sizeof(GSTCTLFSINFO));
                }
                else
                {
                    if (pvrcGuest)
                        *pvrcGuest = vrcGuest;
                    vrc = VERR_GSTCTL_GUEST_ERROR;
                }
            }
            else
            {
                if (pEvent->HasGuestError() && pvrcGuest)
                    *pvrcGuest = pEvent->GuestResult();
            }
        }
        unregisterWaitEvent(pEvent);
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    LogFlowThisFunc(("Returning vrc=%Rrc, vrcGuest=%Rrc\n", vrc, pvrcGuest ? *pvrcGuest : VERR_IPE_UNINITIALIZED_STATUS));
    return vrc;
}

/**
 * Queries information of a file system object (file, directory, ...).
 *
 * @return  IPRT status code.
 * @param   strPath             Path to file system object to query information for.
 * @param   fFollowSymlinks     Whether to follow symbolic links or not.
 * @param   objData             Where to return the file system object data, if found.
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR. Any other return code
 *                              indicates some host side error.
 */
int GuestSession::i_fsObjQueryInfo(const Utf8Str &strPath, bool fFollowSymlinks, GuestFsObjData &objData, int *pvrcGuest)
{
    LogFlowThisFunc(("strPath=%s\n", strPath.c_str()));

    int vrc;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        GuestWaitEvent *pEvent = NULL;
        vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
        if (RT_FAILURE(vrc))
            return vrc;

        uint32_t const fFlags = fFollowSymlinks ? GSTCTL_PATH_F_FOLLOW_LINK : GSTCTL_PATH_F_ON_LINK;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[4];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetStr(&paParms[i++], strPath.c_str());
        HGCMSvcSetU32(&paParms[i++], GSTCTLFSOBJATTRADD_UNIX /* Implicit */);
        HGCMSvcSetU32(&paParms[i++], fFlags);

        alock.release(); /* Drop lock before sending. */

        vrc = i_sendMessage(HOST_MSG_FS_OBJ_QUERY_INFO, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                PCALLBACKDATA_FS_NOTIFY const pFsNotify = (PCALLBACKDATA_FS_NOTIFY)pEvent->Payload().Raw();
                AssertPtrReturn(pFsNotify, VERR_INVALID_POINTER);
                int vrcGuest = (int)pFsNotify->rc;
                if (RT_SUCCESS(vrcGuest))
                {
                    AssertReturn(pFsNotify->uType == GUEST_FS_NOTIFYTYPE_QUERY_OBJ_INFO, VERR_INVALID_PARAMETER);
                    objData.Init(strPath);
                    vrc = objData.FromGuestFsObjInfo(&pFsNotify->u.QueryObjInfo.objInfo);
                    RTStrFree(pFsNotify->u.QueryObjInfo.pszUser);
                    RTStrFree(pFsNotify->u.QueryObjInfo.pszGroups);
                }
                else
                {
                    if (pvrcGuest)
                        *pvrcGuest = vrcGuest;
                    vrc = VERR_GSTCTL_GUEST_ERROR;
                }
            }
            else
            {
                if (pEvent->HasGuestError() && pvrcGuest)
                    *pvrcGuest = pEvent->GuestResult();
            }
        }
        unregisterWaitEvent(pEvent);
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_fsObjQueryInfoViaToolbox(strPath, fFollowSymlinks, objData, pvrcGuest);
#else
        RT_NOREF(strPath, fFollowSymlinks, objData, pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    LogFlowThisFunc(("Returning vrc=%Rrc, vrcGuest=%Rrc\n", vrc, pvrcGuest ? *pvrcGuest : VERR_IPE_UNINITIALIZED_STATUS));
    return vrc;
}

/**
 * Returns the guest credentials of a guest session.
 *
 * @returns Guest credentials.
 */
const GuestCredentials& GuestSession::i_getCredentials(void)
{
    return mData.mCredentials;
}

/**
 * Returns the guest session (friendly) name.
 *
 * @returns Guest session name.
 */
Utf8Str GuestSession::i_getName(void)
{
    return mData.mSession.mName;
}

/**
 * Returns a stringified error description for a given guest result code.
 *
 * @returns Stringified error description.
 */
/* static */
Utf8Str GuestSession::i_guestErrorToString(int vrcGuest)
{
    Utf8Str strError;

    /** @todo pData->u32Flags: int vs. uint32 -- IPRT errors are *negative* !!! */
    switch (vrcGuest)
    {
        case VERR_INVALID_VM_HANDLE:
            strError.printf(tr("VMM device is not available (is the VM running?)"));
            break;

        case VERR_HGCM_SERVICE_NOT_FOUND:
            strError.printf(tr("The guest execution service is not available"));
            break;

        case VERR_ACCOUNT_RESTRICTED:
            strError.printf(tr("The specified user account on the guest is restricted and can't be used to logon"));
            break;

        case VERR_AUTHENTICATION_FAILURE:
            strError.printf(tr("The specified user was not able to logon on guest"));
            break;

        case VERR_TIMEOUT:
            strError.printf(tr("The guest did not respond within time"));
            break;

        case VERR_CANCELLED:
            strError.printf(tr("The session operation was canceled"));
            break;

        case VERR_GSTCTL_MAX_CID_OBJECTS_REACHED:
            strError.printf(tr("Maximum number of concurrent guest processes has been reached"));
            break;

        case VERR_NOT_FOUND:
            strError.printf(tr("The guest execution service is not ready (yet)"));
            break;

        default:
            strError.printf("%Rrc", vrcGuest);
            break;
    }

    return strError;
}

/**
 * Returns whether the session is in a started state or not.
 *
 * @returns \c true if in a started state, or \c false if not.
 */
bool GuestSession::i_isStarted(void) const
{
    return (mData.mStatus == GuestSessionStatus_Started);
}

/**
 * Checks if this session is in a ready state where it can handle
 * all session-bound actions (like guest processes, guest files).
 *
 * Only used by official API methods.
 * Takes the read lock.
 *
 * @returns S_OK if ready, E_FAIL if not.
 * @note    Will set an external error when not ready.
 */
HRESULT GuestSession::i_isStartedExternal(void)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!i_isStarted())
        return setError(E_FAIL, tr("Session is not in started state (state is '%s')",
                                   Global::stringifyGuestSessionStatus(mData.mStatus)));
    return S_OK;
}

/**
 * Returns if this session is in a ready-to-use state or not.
 *
 * @returns \c true if ready, or \c false if not.
 */
bool GuestSession::i_isReady(void)
{
    /* Check if the VM has the right machine state we can operate with. Also make sure that
     * the VM is in an online *and* non-transient state while at it.
     *
     * This for instance is required if we want to close a guest session while the VM state is being saved or
     * is doing some other lenghtly operations we can't operate with the guest.
     */
    MachineState_T enmMachineState = MachineState_Null;
    HRESULT hrc = mConsole->COMGETTER(State)(&enmMachineState);
    ComAssertComRCRet(hrc, false);
    if (   !Global::IsOnline(enmMachineState)
        ||  Global::IsTransient(enmMachineState))
        return false;

    return true;
}

/**
 * Returns whether a guest session status implies a terminated state or not.
 *
 * @returns \c true if it's a terminated state, or \c false if not.
 */
/* static */
bool GuestSession::i_isTerminated(GuestSessionStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case GuestSessionStatus_Terminated:
            RT_FALL_THROUGH();
        case GuestSessionStatus_TimedOutKilled:
            RT_FALL_THROUGH();
        case GuestSessionStatus_TimedOutAbnormally:
            RT_FALL_THROUGH();
        case GuestSessionStatus_Down:
            RT_FALL_THROUGH();
        case GuestSessionStatus_Error:
            return true;

        default:
            break;
    }

    return false;
}

/**
 * Returns whether the session is in a terminated state or not.
 *
 * @returns \c true if in a terminated state, or \c false if not.
 */
bool GuestSession::i_isTerminated(void) const
{
    return GuestSession::i_isTerminated(mData.mStatus);
}

/**
 * Called by IGuest right before this session gets removed from
 * the public session list.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_onRemove(void)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = i_objectsUnregister();

    /*
     * Note: The event source stuff holds references to this object,
     *       so make sure that this is cleaned up *before* calling uninit.
     */
    if (!mEventSource.isNull())
    {
        mEventSource->UnregisterListener(mLocalListener);

        mLocalListener.setNull();
        unconst(mEventSource).setNull();
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Handles guest file system notifications.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context from HGCM service.
 * @param   pSvcCbData          HGCM service callback data.
 */
int GuestSession::i_onFsNotify(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    if (pSvcCbData->mParms < 4)
        return VERR_INVALID_PARAMETER;

    CALLBACKDATA_FS_NOTIFY dataCb;
    RT_ZERO(dataCb);
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    int vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[1], &dataCb.uType);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[2], &dataCb.rc);
    AssertRCReturn(vrc, vrc);

    int const vrcGuest = (int)dataCb.rc;

    if (RT_SUCCESS(vrcGuest))
    {
        switch (dataCb.uType)
        {
            case GUEST_FS_NOTIFYTYPE_CREATE_TEMP:
            {
                char    *pszPath;
                uint32_t cbPath;
                vrc = HGCMSvcGetStr(&pSvcCbData->mpaParms[3], &pszPath, &cbPath);
                AssertRCBreak(vrc);
                dataCb.u.CreateTemp.pszPath = RTStrDup(pszPath);
                AssertPtrBreakStmt(dataCb.u.CreateTemp.pszPath, vrc = VERR_NO_MEMORY);
                dataCb.u.CreateTemp.cbPath  = cbPath;
                break;
            }

            case GUEST_FS_NOTIFYTYPE_QUERY_OBJ_INFO:
            {
                AssertBreakStmt(pSvcCbData->mParms >= 6, vrc = VERR_INVALID_PARAMETER);
                PGSTCTLFSOBJINFO pObjInfo;
                uint32_t         cbObjInfo;
                vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[3], (void **)&pObjInfo, &cbObjInfo);
                AssertRCBreak(vrc);
                AssertBreakStmt(cbObjInfo == sizeof(GSTCTLFSOBJINFO), vrc = VERR_INVALID_PARAMETER);
                memcpy(&dataCb.u.QueryObjInfo.objInfo, pObjInfo, sizeof(GSTCTLFSOBJINFO));

                char    *pszUser;
                uint32_t cbUser;
                vrc = HGCMSvcGetStr(&pSvcCbData->mpaParms[4], &pszUser, &cbUser);
                AssertRCBreak(vrc);
                dataCb.u.QueryObjInfo.pszUser = RTStrDup(pszUser);
                AssertPtrBreakStmt(dataCb.u.QueryObjInfo.pszUser, vrc = VERR_NO_MEMORY);
                dataCb.u.QueryObjInfo.cbUser  = cbUser;

                char    *pszGroups;
                uint32_t cbGroups;
                vrc = HGCMSvcGetStr(&pSvcCbData->mpaParms[5], &pszGroups, &cbGroups);
                AssertRCBreak(vrc);
                dataCb.u.QueryObjInfo.pszGroups = RTStrDup(pszGroups);
                AssertPtrBreakStmt(dataCb.u.QueryObjInfo.pszGroups, vrc = VERR_NO_MEMORY);
                dataCb.u.QueryObjInfo.cbGroups  = cbGroups;
                break;
            }

            case GUEST_FS_NOTIFYTYPE_QUERY_INFO:
            {
                AssertBreakStmt(pSvcCbData->mParms >= 2, vrc = VERR_INVALID_PARAMETER);
                PGSTCTLFSINFO pFsInfo;
                uint32_t      cbFsInfo;
                vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[3], (void **)&pFsInfo, &cbFsInfo);
                AssertRCBreak(vrc);
                AssertBreakStmt(cbFsInfo == sizeof(GSTCTLFSINFO), vrc = VERR_INVALID_PARAMETER);
                memcpy(&dataCb.u.QueryInfo.fsInfo, pFsInfo, sizeof(GSTCTLFSINFO));
                break;
            }

            case GUEST_FS_NOTIFYTYPE_UNKNOWN:
                RT_FALL_THROUGH();
            default:
                vrc = VERR_NOT_SUPPORTED;
                break;
        }
    }

    try
    {
        GuestWaitEventPayload evPayload(dataCb.uType, &dataCb, sizeof(dataCb));
        vrc = signalWaitEventInternal(pCbCtx, dataCb.rc, &evPayload);
    }
    catch (int vrcEx) /* Thrown by GuestWaitEventPayload constructor. */
    {
        vrc = vrcEx;
    }

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

/**
 * Handles guest session status changes from the guest.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context from HGCM service.
 * @param   pSvcCbData          HGCM service callback data.
 *
 * @note    Takes the read lock (for session ID lookup).
 */
int GuestSession::i_onSessionStatusChange(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    if (pSvcCbData->mParms < 3)
        return VERR_INVALID_PARAMETER;

    CALLBACKDATA_SESSION_NOTIFY dataCb;
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    int vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[1], &dataCb.uType);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[2], &dataCb.uResult);
    AssertRCReturn(vrc, vrc);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("ID=%RU32, uType=%RU32, vrcGuest=%Rrc\n", mData.mSession.mID, dataCb.uType, dataCb.uResult));

    GuestSessionStatus_T sessionStatus = GuestSessionStatus_Undefined;

    int vrcGuest = dataCb.uResult; /** @todo uint32_t vs. int. */
    switch (dataCb.uType)
    {
        case GUEST_SESSION_NOTIFYTYPE_ERROR:
            sessionStatus = GuestSessionStatus_Error;
            LogRel(("Guest Control: Error starting Session '%s' (%Rrc) \n", mData.mSession.mName.c_str(), vrcGuest));
            break;

        case GUEST_SESSION_NOTIFYTYPE_STARTED:
            sessionStatus = GuestSessionStatus_Started;
#if 0 /** @todo If we get some environment stuff along with this kind notification: */
            const char *pszzEnvBlock = ...;
            uint32_t    cbEnvBlock   = ...;
            if (!mData.mpBaseEnvironment)
            {
                GuestEnvironment *pBaseEnv;
                try { pBaseEnv = new GuestEnvironment(); } catch (std::bad_alloc &) { pBaseEnv = NULL; }
                if (pBaseEnv)
                {
                    int vrc = pBaseEnv->initNormal(Guest.i_isGuestInWindowsNtFamily() ? RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR : 0);
                    if (RT_SUCCESS(vrc))
                        vrc = pBaseEnv->copyUtf8Block(pszzEnvBlock, cbEnvBlock);
                    if (RT_SUCCESS(vrc))
                        mData.mpBaseEnvironment = pBaseEnv;
                    else
                        pBaseEnv->release();
                }
            }
#endif
            LogRel(("Guest Control: Session '%s' was successfully started\n", mData.mSession.mName.c_str()));
            break;

        case GUEST_SESSION_NOTIFYTYPE_TEN:
            LogRel(("Guest Control: Session '%s' was terminated normally with exit code %#x\n",
                    mData.mSession.mName.c_str(), dataCb.uResult));
            sessionStatus = GuestSessionStatus_Terminated;
            break;

        case GUEST_SESSION_NOTIFYTYPE_TEA:
            LogRel(("Guest Control: Session '%s' was terminated abnormally\n", mData.mSession.mName.c_str()));
            sessionStatus = GuestSessionStatus_Terminated;
            /* dataCb.uResult is undefined. */
            break;

        case GUEST_SESSION_NOTIFYTYPE_TES:
            LogRel(("Guest Control: Session '%s' was terminated via signal %#x\n", mData.mSession.mName.c_str(), dataCb.uResult));
            sessionStatus = GuestSessionStatus_Terminated;
            break;

        case GUEST_SESSION_NOTIFYTYPE_TOK:
            sessionStatus = GuestSessionStatus_TimedOutKilled;
            LogRel(("Guest Control: Session '%s' timed out and was killed\n", mData.mSession.mName.c_str()));
            break;

        case GUEST_SESSION_NOTIFYTYPE_TOA:
            sessionStatus = GuestSessionStatus_TimedOutAbnormally;
            LogRel(("Guest Control: Session '%s' timed out and was not killed successfully\n", mData.mSession.mName.c_str()));
            break;

        case GUEST_SESSION_NOTIFYTYPE_DWN:
            sessionStatus = GuestSessionStatus_Down;
            LogRel(("Guest Control: Session '%s' got killed as guest service/OS is down\n", mData.mSession.mName.c_str()));
            break;

        case GUEST_SESSION_NOTIFYTYPE_UNDEFINED:
        default:
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

    /* Leave the lock, as i_setSessionStatus() below will require a write lock for actually
     * committing the session state. */
    alock.release();

    if (RT_SUCCESS(vrc))
    {
        if (RT_FAILURE(vrcGuest))
            sessionStatus = GuestSessionStatus_Error;
    }

    /* Set the session status. */
    if (RT_SUCCESS(vrc))
        vrc = i_setSessionStatus(sessionStatus, vrcGuest);

    LogFlowThisFunc(("ID=%RU32, vrcGuest=%Rrc\n", mData.mSession.mID, vrcGuest));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Returns the path separation style used on the guest.
 *
 * @returns Separation style used on the guest.
 */
PathStyle_T GuestSession::i_getGuestPathStyle(void)
{
    PathStyle_T enmPathStyle;

    VBOXOSTYPE enmOsType = mParent->i_getGuestOSType();
    if (enmOsType < VBOXOSTYPE_DOS)
    {
        LogFlowFunc(("returns PathStyle_Unknown\n"));
        enmPathStyle = PathStyle_Unknown;
    }
    else if (enmOsType < VBOXOSTYPE_Linux)
    {
        LogFlowFunc(("returns PathStyle_DOS\n"));
        enmPathStyle = PathStyle_DOS;
    }
    else
    {
        LogFlowFunc(("returns PathStyle_UNIX\n"));
        enmPathStyle = PathStyle_UNIX;
    }

    return enmPathStyle;
}

/**
 * Returns the path separation style used on the host.
 *
 * @returns Separation style used on the host.
 */
/* static */
PathStyle_T GuestSession::i_getHostPathStyle(void)
{
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    return PathStyle_DOS;
#else
    return PathStyle_UNIX;
#endif
}

/**
 * Starts the guest session on the guest.
 *
 * @returns VBox status code.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the read and write locks.
 */
int GuestSession::i_startSession(int *pvrcGuest)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("mID=%RU32, mName=%s, uProtocolVersion=%RU32, openFlags=%x, openTimeoutMS=%RU32\n",
                     mData.mSession.mID, mData.mSession.mName.c_str(), mData.mProtocolVersion,
                     mData.mSession.mOpenFlags, mData.mSession.mOpenTimeoutMS));

    /* Guest Additions < 4.3 don't support opening dedicated
       guest sessions. Simply return success here. */
    if (mData.mProtocolVersion < 2)
    {
        alock.release(); /* Release lock before changing status. */

        i_setSessionStatus(GuestSessionStatus_Started, VINF_SUCCESS); /* ignore return code*/
        LogFlowThisFunc(("Installed Guest Additions don't support opening dedicated sessions, skipping\n"));
        return VINF_SUCCESS;
    }

    if (mData.mStatus != GuestSessionStatus_Undefined)
        return VINF_SUCCESS;

    /** @todo mData.mSession.uFlags validation. */

    alock.release(); /* Release lock before changing status. */

    /* Set current session status. */
    int vrc = i_setSessionStatus(GuestSessionStatus_Starting, VINF_SUCCESS);
    if (RT_FAILURE(vrc))
        return vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestSessionStateChanged);

        vrc = registerWaitEventEx(mData.mSession.mID, mData.mObjectID, eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    alock.acquire(); /* Re-acquire lock before accessing session attributes below. */

    VBOXHGCMSVCPARM paParms[8];

    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mData.mProtocolVersion);
    HGCMSvcSetPv(&paParms[i++], (void*)mData.mCredentials.mUser.c_str(),
                            (ULONG)mData.mCredentials.mUser.length() + 1);
    HGCMSvcSetPv(&paParms[i++], (void*)mData.mCredentials.mPassword.c_str(),
                            (ULONG)mData.mCredentials.mPassword.length() + 1);
    HGCMSvcSetPv(&paParms[i++], (void*)mData.mCredentials.mDomain.c_str(),
                            (ULONG)mData.mCredentials.mDomain.length() + 1);
    HGCMSvcSetU32(&paParms[i++], mData.mSession.mOpenFlags);

    alock.release(); /* Drop lock before sending. */

    vrc = i_sendMessage(HOST_MSG_SESSION_CREATE, i, paParms, VBOX_GUESTCTRL_DST_ROOT_SVC);
    if (RT_SUCCESS(vrc))
    {
        vrc = i_waitForStatusChange(pEvent, GuestSessionWaitForFlag_Start,
                                    GSTCTL_DEFAULT_TIMEOUT_MS, NULL /* Session status */, pvrcGuest);
    }
    else
    {
        /*
         * Unable to start guest session - update its current state.
         * Since there is no (official API) way to recover a failed guest session
         * this also marks the end state. Internally just calling this
         * same function again will work though.
         */
        i_setSessionStatus(GuestSessionStatus_Error, vrc); /* ignore return code */
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Starts the guest session asynchronously in a separate worker thread.
 *
 * @returns IPRT status code.
 */
int GuestSession::i_startSessionAsync(void)
{
    LogFlowThisFuncEnter();

    /* Create task: */
    GuestSessionTaskInternalStart *pTask = NULL;
    try
    {
        pTask = new GuestSessionTaskInternalStart(this);
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    if (pTask->isOk())
    {
        /* Kick off the thread: */
        HRESULT hrc = pTask->createThread();
        pTask = NULL; /* Not valid anymore, not even on failure! */
        if (SUCCEEDED(hrc))
        {
            LogFlowFuncLeaveRC(VINF_SUCCESS);
            return VINF_SUCCESS;
        }
        LogFlow(("GuestSession: Failed to create thread for GuestSessionTaskInternalOpen task.\n"));
    }
    else
        LogFlow(("GuestSession: GuestSessionTaskInternalStart creation failed: %Rhrc.\n", pTask->vrc()));
    LogFlowFuncLeaveRC(VERR_GENERAL_FAILURE);
    return VERR_GENERAL_FAILURE;
}

/**
 * Static function to start a guest session asynchronously.
 *
 * @returns IPRT status code.
 * @param   pTask               Task object to use for starting the guest session.
 */
/* static */
int GuestSession::i_startSessionThreadTask(GuestSessionTaskInternalStart *pTask)
{
    LogFlowFunc(("pTask=%p\n", pTask));
    AssertPtr(pTask);

    const ComObjPtr<GuestSession> pSession(pTask->Session());
    Assert(!pSession.isNull());

    AutoCaller autoCaller(pSession);
    if (FAILED(autoCaller.hrc()))
        return VERR_COM_INVALID_OBJECT_STATE;

    int vrc = pSession->i_startSession(NULL /*pvrcGuest*/);
    /* Nothing to do here anymore. */

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Registers an object with the session, i.e. allocates an object ID.
 *
 * @return  VBox status code.
 * @retval  VERR_GSTCTL_MAX_OBJECTS_REACHED if the maximum of concurrent objects
 *          is reached.
 * @param   pObject     Guest object to register (weak pointer). Optional.
 * @param   enmType     Session object type to register.
 * @param   pidObject   Where to return the object ID on success. Optional.
 */
int GuestSession::i_objectRegister(GuestObject *pObject, SESSIONOBJECTTYPE enmType, uint32_t *pidObject)
{
    /* pObject can be NULL. */
    /* pidObject is optional. */

    /*
     * Pick a random bit as starting point.  If it's in use, search forward
     * for a free one, wrapping around.  We've reserved both the zero'th and
     * max-1 IDs (see Data constructor).
     */
    uint32_t idObject = RTRandU32Ex(1, VBOX_GUESTCTRL_MAX_OBJECTS - 2);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!ASMBitTestAndSet(&mData.bmObjectIds[0], idObject))
    { /* likely */ }
    else if (mData.mObjects.size() < VBOX_GUESTCTRL_MAX_OBJECTS - 2 /* First and last are not used */)
    {
        /* Forward search. */
        int iHit = ASMBitNextClear(&mData.bmObjectIds[0], VBOX_GUESTCTRL_MAX_OBJECTS, idObject);
        if (iHit < 0)
            iHit = ASMBitFirstClear(&mData.bmObjectIds[0], VBOX_GUESTCTRL_MAX_OBJECTS);
        AssertLogRelMsgReturn(iHit >= 0, ("object count: %#zu\n", mData.mObjects.size()), VERR_GSTCTL_MAX_CID_OBJECTS_REACHED);
        idObject = iHit;
        AssertLogRelMsgReturn(!ASMBitTestAndSet(&mData.bmObjectIds[0], idObject), ("idObject=%#x\n", idObject), VERR_INTERNAL_ERROR_2);
    }
    else
    {
        LogFunc(("Maximum number of objects reached (enmType=%RU32, %zu objects)\n", enmType, mData.mObjects.size()));
        return VERR_GSTCTL_MAX_CID_OBJECTS_REACHED;
    }

    Log2Func(("enmType=%RU32 -> idObject=%RU32 (%zu objects)\n", enmType, idObject, mData.mObjects.size()));

    try
    {
        mData.mObjects[idObject].pObject = pObject; /* Can be NULL. */
        mData.mObjects[idObject].enmType = enmType;
        mData.mObjects[idObject].msBirth = RTTimeMilliTS();
    }
    catch (std::bad_alloc &)
    {
        ASMBitClear(&mData.bmObjectIds[0], idObject);
        return VERR_NO_MEMORY;
    }

    if (pidObject)
        *pidObject = idObject;

    return VINF_SUCCESS;
}

/**
 * Unregisters an object from the session objects list.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_FOUND if the object ID was not found.
 * @param   idObject        Object ID to unregister.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_objectUnregister(uint32_t idObject)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;
    AssertMsgStmt(ASMBitTestAndClear(&mData.bmObjectIds, idObject), ("idObject=%#x\n", idObject), vrc = VERR_NOT_FOUND);

    SessionObjects::iterator ItObj = mData.mObjects.find(idObject);
    AssertMsgReturn(ItObj != mData.mObjects.end(), ("idObject=%#x\n", idObject), VERR_NOT_FOUND);
    mData.mObjects.erase(ItObj);

    return vrc;
}

/**
 * Unregisters all objects from the session list.
 *
 * @returns VBox status code.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_objectsUnregister(void)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("Unregistering directories (%zu total)\n", mData.mDirectories.size()));

    SessionDirectories::iterator itDirs;
    while ((itDirs = mData.mDirectories.begin()) != mData.mDirectories.end())
    {
        alock.release();
        i_directoryUnregister(itDirs->second);
        alock.acquire();
    }

    Assert(mData.mDirectories.size() == 0);
    mData.mDirectories.clear();

    LogFlowThisFunc(("Unregistering files (%zu total)\n", mData.mFiles.size()));

    SessionFiles::iterator itFiles;
    while ((itFiles = mData.mFiles.begin()) != mData.mFiles.end())
    {
        alock.release();
        i_fileUnregister(itFiles->second);
        alock.acquire();
    }

    Assert(mData.mFiles.size() == 0);
    mData.mFiles.clear();

    LogFlowThisFunc(("Unregistering processes (%zu total)\n", mData.mProcesses.size()));

    SessionProcesses::iterator itProcs;
    while ((itProcs = mData.mProcesses.begin()) != mData.mProcesses.end())
    {
        alock.release();
        i_processUnregister(itProcs->second);
        alock.acquire();
    }

    Assert(mData.mProcesses.size() == 0);
    mData.mProcesses.clear();

    return VINF_SUCCESS;
}

/**
 * Notifies all registered objects about a guest session status change.
 *
 * @returns VBox status code.
 * @param   enmSessionStatus    Session status to notify objects about.
 */
int GuestSession::i_objectsNotifyAboutStatusChange(GuestSessionStatus_T enmSessionStatus)
{
    LogFlowThisFunc(("enmSessionStatus=%RU32\n", enmSessionStatus));

    int vrc = VINF_SUCCESS;

    SessionObjects::iterator itObjs = mData.mObjects.begin();
    while (itObjs != mData.mObjects.end())
    {
        GuestObject *pObj = itObjs->second.pObject;
        if (pObj) /* pObject can be NULL (weak pointer). */
        {
            int vrc2 = pObj->i_onSessionStatusChange(enmSessionStatus);
            if (RT_SUCCESS(vrc))
                vrc = vrc2;

            /* If the session got terminated, make sure to cancel all wait events for
             * the current object. */
            if (i_isTerminated())
                pObj->cancelWaitEvents();
        }

        ++itObjs;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Renames a path on the guest.
 *
 * @returns VBox status code.
 * @returns VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @param   strSource           Source path on guest to rename.
 * @param   strDest             Destination path on guest to rename \a strSource to.
 * @param   uFlags              Renaming flags.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 * @note    Takes the read lock.
 */
int GuestSession::i_pathRename(const Utf8Str &strSource, const Utf8Str &strDest, uint32_t uFlags, int *pvrcGuest)
{
    AssertReturn(!(uFlags & ~PATHRENAME_FLAG_VALID_MASK), VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("strSource=%s, strDest=%s, uFlags=0x%x\n",
                     strSource.c_str(), strDest.c_str(), uFlags));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    GuestWaitEvent *pEvent = NULL;
    int vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[8];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetPv(&paParms[i++], (void*)strSource.c_str(),
                            (ULONG)strSource.length() + 1);
    HGCMSvcSetPv(&paParms[i++], (void*)strDest.c_str(),
                            (ULONG)strDest.length() + 1);
    HGCMSvcSetU32(&paParms[i++], uFlags);

    alock.release(); /* Drop lock before sending. */

    vrc = i_sendMessage(HOST_MSG_PATH_RENAME, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
        if (pEvent->HasGuestError() && pvrcGuest)
            *pvrcGuest = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Returns the user's absolute documents path, if any.
 *
 * @returns VBox status code.
 * @param   strPath     Where to store the user's document path.
 * @param   pvrcGuest   Guest VBox status code, when returning
 *                      VERR_GSTCTL_GUEST_ERROR. Any other return code indicates
 *                      some host side error.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_pathUserDocuments(Utf8Str &strPath, int *pvrcGuest)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo Cache the user's document path? */

    GuestWaitEvent *pEvent = NULL;
    int vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[2];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());

    alock.release(); /* Drop lock before sending. */

    vrc = i_sendMessage(HOST_MSG_PATH_USER_DOCUMENTS, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
        if (RT_SUCCESS(vrc))
        {
            strPath = pEvent->Payload().ToString();
        }
        else
        {
            if (pEvent->HasGuestError() && pvrcGuest)
                *pvrcGuest = pEvent->GuestResult();
        }
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Returns the currently accessible mount points of the guest.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the installed Guest Additions do not support this feature.
 * @param   vecMountPoints      Where to return the mount points (guest-style paths).
 * @param   pvrcGuest           Guest VBox status code, when returning
 *                              VERR_GSTCTL_GUEST_ERROR. Any other return code indicates
 *                              some host side error.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_getMountPoints(std::vector<com::Utf8Str> &vecMountPoints, int *pvrcGuest)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!(mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_MOUNT_POINTS_ENUM))
        return VERR_NOT_SUPPORTED;

    GuestWaitEvent *pEvent = NULL;
    int vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[4];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], 0 /* fFlags, unused */);

    alock.release(); /* Drop lock before sending. */

    vrc = i_sendMessage(HOST_MSG_MOUNT_POINTS, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Payload().ToStringVector(vecMountPoints);
        }
        else
        {
            if (pEvent->HasGuestError() && pvrcGuest)
                *pvrcGuest = pEvent->GuestResult();
        }
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Returns the user's absolute home path, if any.
 *
 * @returns VBox status code.
 * @param   strPath     Where to store the user's home path.
 * @param   pvrcGuest   Guest VBox status code, when returning
 *                      VERR_GSTCTL_GUEST_ERROR. Any other return code indicates
 *                      some host side error.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_pathUserHome(Utf8Str &strPath, int *pvrcGuest)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /** @todo Cache the user's home path? */

    GuestWaitEvent *pEvent = NULL;
    int vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[2];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());

    alock.release(); /* Drop lock before sending. */

    vrc = i_sendMessage(HOST_MSG_PATH_USER_HOME, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
        if (RT_SUCCESS(vrc))
        {
            strPath = pEvent->Payload().ToString();
        }
        else
        {
            if (pEvent->HasGuestError() && pvrcGuest)
                *pvrcGuest = pEvent->GuestResult();
        }
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Unregisters a process object from a guest session.
 *
 * @returns VBox status code. VERR_NOT_FOUND if the process is not registered (anymore).
 * @param   pProcess            Process object to unregister from session.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_processUnregister(GuestProcess *pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    LogFlowThisFunc(("pProcess=%p\n", pProcess));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const uint32_t idObject = pProcess->getObjectID();

    LogFlowFunc(("Removing process (objectID=%RU32) ...\n", idObject));

    int vrc = i_objectUnregister(idObject);
    if (RT_FAILURE(vrc))
        return vrc;

    SessionProcesses::iterator itProcs = mData.mProcesses.find(idObject);
    AssertReturn(itProcs != mData.mProcesses.end(), VERR_NOT_FOUND);

    /* Make sure to consume the pointer before the one of the iterator gets released. */
    ComObjPtr<GuestProcess> pProc = pProcess;

    ULONG uPID;
    HRESULT hrc = pProc->COMGETTER(PID)(&uPID);
    ComAssertComRC(hrc);

    LogFlowFunc(("Removing process ID=%RU32 (session %RU32, guest PID %RU32, now total %zu processes)\n",
                 idObject, mData.mSession.mID, uPID, mData.mProcesses.size()));

    vrc = pProcess->i_onUnregister();
    AssertRCReturn(vrc, vrc);

    mData.mProcesses.erase(itProcs);

    alock.release(); /* Release lock before firing off event. */

    ::FireGuestProcessRegisteredEvent(mEventSource, this /* Session */, pProc, uPID, false /* Process unregistered */);

    pProc.setNull();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Creates but does *not* start the process yet.
 *
 * See GuestProcess::startProcess() or GuestProcess::startProcessAsync() for
 * starting the process.
 *
 * @returns IPRT status code.
 * @param   procInfo            Process startup info to use for starting the process.
 * @param   pProcess            Where to return the created guest process object on success.
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_processCreateEx(GuestProcessStartupInfo &procInfo, ComObjPtr<GuestProcess> &pProcess)
{
    LogFlowFunc(("mExe=%s, mFlags=%x, mTimeoutMS=%RU32\n",
                 procInfo.mExecutable.c_str(), procInfo.mFlags, procInfo.mTimeoutMS));
#ifdef DEBUG
    if (procInfo.mArguments.size())
    {
        LogFlowFunc(("Arguments:"));
        ProcessArguments::const_iterator it = procInfo.mArguments.begin();
        while (it != procInfo.mArguments.end())
        {
            LogFlow((" %s", (*it).c_str()));
            ++it;
        }
        LogFlow(("\n"));
    }
#endif

    /* Validate flags. */
    if (procInfo.mFlags)
    {
        if (   !(procInfo.mFlags & ProcessCreateFlag_IgnoreOrphanedProcesses)
            && !(procInfo.mFlags & ProcessCreateFlag_WaitForProcessStartOnly)
            && !(procInfo.mFlags & ProcessCreateFlag_Hidden)
            && !(procInfo.mFlags & ProcessCreateFlag_Profile)
            && !(procInfo.mFlags & ProcessCreateFlag_WaitForStdOut)
            && !(procInfo.mFlags & ProcessCreateFlag_WaitForStdErr))
        {
            return VERR_INVALID_PARAMETER;
        }
    }

    if (   (procInfo.mFlags & ProcessCreateFlag_WaitForProcessStartOnly)
        && (   (procInfo.mFlags & ProcessCreateFlag_WaitForStdOut)
            || (procInfo.mFlags & ProcessCreateFlag_WaitForStdErr)
           )
       )
    {
        return VERR_INVALID_PARAMETER;
    }

    if (procInfo.mPriority)
    {
        if (!(procInfo.mPriority & ProcessPriority_Default))
            return VERR_INVALID_PARAMETER;
    }

    /* Adjust timeout.
     * If set to 0, we define an infinite timeout (unlimited process run time). */
    if (procInfo.mTimeoutMS == 0)
        procInfo.mTimeoutMS = UINT32_MAX;

    /** @todo Implement process priority + affinity. */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Create the process object. */
    HRESULT hrc = pProcess.createObject();
    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    /* Register a new object ID. */
    uint32_t idObject;
    int vrc = i_objectRegister(pProcess, SESSIONOBJECTTYPE_PROCESS, &idObject);
    if (RT_FAILURE(vrc))
    {
        pProcess.setNull();
        return vrc;
    }

    vrc = pProcess->init(mConsole, this /* Session */, idObject, procInfo, mData.mpBaseEnvironment);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Add the created process to our map. */
    try
    {
        mData.mProcesses[idObject] = pProcess;

        LogFlowFunc(("Added new process (Session: %RU32) with process ID=%RU32 (now total %zu processes)\n",
                     mData.mSession.mID, idObject, mData.mProcesses.size()));

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestProcessRegisteredEvent(mEventSource, this /* Session */, pProcess, 0 /* PID */, true /* Process registered */);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    return vrc;
}

/**
 * Checks if a process object exists and optionally returns its object.
 *
 * @returns \c true if process object exists, or \c false if not.
 * @param   uProcessID          ID of process object to check.
 * @param   pProcess            Where to return the found process object on success.
 *
 * @note    No locking done!
 */
inline bool GuestSession::i_processExists(uint32_t uProcessID, ComObjPtr<GuestProcess> *pProcess)
{
    SessionProcesses::const_iterator it = mData.mProcesses.find(uProcessID);
    if (it != mData.mProcesses.end())
    {
        if (pProcess)
            *pProcess = it->second;
        return true;
    }
    return false;
}

/**
 * Returns the process object from a guest PID.
 *
 * @returns VBox status code.
 * @param   uPID                Guest PID to get process object for.
 * @param   pProcess            Where to return the process object on success.
 *
 * @note    No locking done!
 */
inline int GuestSession::i_processGetByPID(ULONG uPID, ComObjPtr<GuestProcess> *pProcess)
{
    AssertReturn(uPID, false);
    /* pProcess is optional. */

    SessionProcesses::iterator itProcs = mData.mProcesses.begin();
    for (; itProcs != mData.mProcesses.end(); ++itProcs)
    {
        ComObjPtr<GuestProcess> pCurProc = itProcs->second;
        AutoCaller procCaller(pCurProc);
        if (!procCaller.isOk())
            return VERR_COM_INVALID_OBJECT_STATE;

        ULONG uCurPID;
        HRESULT hrc = pCurProc->COMGETTER(PID)(&uCurPID);
        ComAssertComRC(hrc);

        if (uCurPID == uPID)
        {
            if (pProcess)
                *pProcess = pCurProc;
            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}

/**
 * Sends a message to the HGCM host service.
 *
 * @returns VBox status code.
 * @retval  VERR_VM_INVALID_VM_STATE if the VM is in a state where can't send message to the guest (anymore).
 * @param   uMessage            Message ID to send.
 * @param   uParms              Number of parameters in \a paParms to send.
 * @param   paParms             Array of HGCM parameters to send.
 * @param   fDst                Host message destination flags of type VBOX_GUESTCTRL_DST_XXX.
 */
int GuestSession::i_sendMessage(uint32_t uMessage, uint32_t uParms, PVBOXHGCMSVCPARM paParms,
                                uint64_t fDst /*= VBOX_GUESTCTRL_DST_SESSION*/)
{
    LogFlowThisFuncEnter();

#ifndef VBOX_GUESTCTRL_TEST_CASE
    VMMDev *pVMMDev = mConsole->i_getVMMDev();
    AssertPtrReturn(pVMMDev, VERR_VM_INVALID_VM_STATE);

    /* Forward the information to the VMM device. */
    LogFlowThisFunc(("uMessage=%RU32 (%s), uParms=%RU32\n", uMessage, GstCtrlHostMsgtoStr((guestControl::eHostMsg)uMessage), uParms));

    /* HACK ALERT! We extend the first parameter to 64-bit and use the
                   two topmost bits for call destination information. */
    Assert(fDst == VBOX_GUESTCTRL_DST_SESSION || fDst == VBOX_GUESTCTRL_DST_ROOT_SVC || fDst == VBOX_GUESTCTRL_DST_BOTH);
    Assert(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT);
    paParms[0].type = VBOX_HGCM_SVC_PARM_64BIT;
    paParms[0].u.uint64 = (uint64_t)paParms[0].u.uint32 | fDst;

    /* Make the call. */
    int vrc = pVMMDev->hgcmHostCall(HGCMSERVICE_NAME, uMessage, uParms, paParms);
    if (RT_FAILURE(vrc))
    {
        /** @todo What to do here? */
    }
#else
    /* Not needed within testcases. */
    int vrc = VINF_SUCCESS;
#endif
    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sets the guest session's current status.
 *
 * @returns VBox status code.
 * @param   sessionStatus       Session status to set.
 * @param   vrcSession          Session result to set (for error handling).
 *
 * @note    Takes the write lock.
 */
int GuestSession::i_setSessionStatus(GuestSessionStatus_T sessionStatus, int vrcSession)
{
    LogFlowThisFunc(("oldStatus=%RU32, newStatus=%RU32, vrcSession=%Rrc\n", mData.mStatus, sessionStatus, vrcSession));

    if (sessionStatus == GuestSessionStatus_Error)
    {
        AssertMsg(RT_FAILURE(vrcSession), ("Guest vrcSession must be an error (%Rrc)\n", vrcSession));
        /* Do not allow overwriting an already set error. If this happens
         * this means we forgot some error checking/locking somewhere. */
        AssertMsg(RT_SUCCESS(mData.mVrc), ("Guest mVrc already set (to %Rrc)\n", mData.mVrc));
    }
    else
        AssertMsg(RT_SUCCESS(vrcSession), ("Guest vrcSession must not be an error (%Rrc)\n", vrcSession));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;

    if (mData.mStatus != sessionStatus)
    {
        mData.mStatus = sessionStatus;
        mData.mVrc    = vrcSession;

        /* Make sure to notify all underlying objects first. */
        vrc = i_objectsNotifyAboutStatusChange(sessionStatus);

        ComObjPtr<VirtualBoxErrorInfo> errorInfo;
        HRESULT hrc = errorInfo.createObject();
        ComAssertComRC(hrc);
        int vrc2 = errorInfo->initEx(VBOX_E_IPRT_ERROR, vrcSession, COM_IIDOF(IGuestSession), getComponentName(),
                                     i_guestErrorToString(vrcSession));
        AssertRC(vrc2);

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestSessionStateChangedEvent(mEventSource, this, mData.mSession.mID, sessionStatus, errorInfo);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/** @todo Unused --remove? */
int GuestSession::i_signalWaiters(GuestSessionWaitResult_T enmWaitResult, int vrc /*= VINF_SUCCESS */)
{
    RT_NOREF(enmWaitResult, vrc);

    /*LogFlowThisFunc(("enmWaitResult=%d, vrc=%Rrc, mWaitCount=%RU32, mWaitEvent=%p\n",
                     enmWaitResult, vrc, mData.mWaitCount, mData.mWaitEvent));*/

    /* Note: No write locking here -- already done in the caller. */

    int vrc2 = VINF_SUCCESS;
    /*if (mData.mWaitEvent)
        vrc2 = mData.mWaitEvent->Signal(enmWaitResult, vrc);*/
    LogFlowFuncLeaveRC(vrc2);
    return vrc2;
}

/**
 * Shuts down (and optionally powers off / reboots) the guest.
 * Needs supported Guest Additions installed.
 *
 * @returns VBox status code. VERR_NOT_SUPPORTED if not supported by Guest Additions.
 * @param   fFlags      Guest shutdown flags.
 * @param   pvrcGuest   Guest VBox status code, when returning
 *                      VERR_GSTCTL_GUEST_ERROR. Any other return code indicates
 *                      some host side error.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_shutdown(uint32_t fFlags, int *pvrcGuest)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (!(mParent->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_SHUTDOWN))
        return VERR_NOT_SUPPORTED;

    LogRel(("Guest Control: Shutting down guest (flags = %#x) ...\n", fFlags));

    GuestWaitEvent *pEvent = NULL;
    int vrc = registerWaitEvent(mData.mSession.mID, mData.mObjectID, &pEvent);
    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[2];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], fFlags);

    alock.release(); /* Drop lock before sending. */

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;

    vrc = i_sendMessage(HOST_MSG_SHUTDOWN, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
        if (RT_FAILURE(vrc))
        {
            if (pEvent->HasGuestError())
                vrcGuest = pEvent->GuestResult();
        }
    }

    if (RT_FAILURE(vrc))
    {
        LogRel(("Guest Control: Shutting down guest failed, vrc=%Rrc\n", vrc == VERR_GSTCTL_GUEST_ERROR ? vrcGuest : vrc));
        if (pEvent->HasGuestError() && pvrcGuest)
            *pvrcGuest = vrcGuest;
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Determines the protocol version (sets mData.mProtocolVersion).
 *
 * This is called from the init method prior to to establishing a guest session.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the Guest Additions were not found (or were not reported) yet.
 */
int GuestSession::i_determineProtocolVersion(void)
{
    /*
     * We currently do this based on the reported Guest Additions version,
     * ASSUMING that VBoxService and VBoxDrv are at the same version.
     */
    ComObjPtr<Guest> pGuest = mParent;
    AssertReturn(!pGuest.isNull(), VERR_NOT_SUPPORTED);
    uint32_t uGaVersion = pGuest->i_getAdditionsVersion();
    if (!uGaVersion) /* If 0, there was no Guest Additions version detected (yet), or the VM is in reset state. */
        return VERR_NOT_FOUND;

    /* Everyone supports version one, if they support anything at all. */
    mData.mProtocolVersion = 1;

    /* Guest control 2.0 was introduced with 4.3.0. */
    if (uGaVersion >= VBOX_FULL_VERSION_MAKE(4,3,0))
        mData.mProtocolVersion = 2; /* Guest control 2.0. */

    LogFlowThisFunc(("uGaVersion=%u.%u.%u => mProtocolVersion=%u\n",
                     VBOX_FULL_VERSION_GET_MAJOR(uGaVersion), VBOX_FULL_VERSION_GET_MINOR(uGaVersion),
                     VBOX_FULL_VERSION_GET_BUILD(uGaVersion), mData.mProtocolVersion));

    /*
     * Inform the user about outdated Guest Additions (VM release log).
     */
    if (mData.mProtocolVersion < 2)
        LogRelMax(3, ("Warning: Guest Additions v%u.%u.%u only supports the older guest control protocol version %u.\n"
                      "         Please upgrade GAs to the current version to get full guest control capabilities.\n",
                      VBOX_FULL_VERSION_GET_MAJOR(uGaVersion), VBOX_FULL_VERSION_GET_MINOR(uGaVersion),
                      VBOX_FULL_VERSION_GET_BUILD(uGaVersion), mData.mProtocolVersion));

    return VINF_SUCCESS;
}

/**
 * Waits for guest session events.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @retval  VERR_TIMEOUT when a timeout has occurred.
 * @param   fWaitFlags          Wait flags to use.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   waitResult          Where to return the wait result on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the read lock.
 */
int GuestSession::i_waitFor(uint32_t fWaitFlags, ULONG uTimeoutMS, GuestSessionWaitResult_T &waitResult, int *pvrcGuest)
{
    LogFlowThisFuncEnter();

    AssertReturn(fWaitFlags, VERR_INVALID_PARAMETER);

    /*LogFlowThisFunc(("fWaitFlags=0x%x, uTimeoutMS=%RU32, mStatus=%RU32, mWaitCount=%RU32, mWaitEvent=%p, pvrcGuest=%p\n",
                     fWaitFlags, uTimeoutMS, mData.mStatus, mData.mWaitCount, mData.mWaitEvent, pvrcGuest));*/

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Did some error occur before? Then skip waiting and return. */
    if (mData.mStatus == GuestSessionStatus_Error)
    {
        waitResult = GuestSessionWaitResult_Error;
        AssertMsg(RT_FAILURE(mData.mVrc), ("No error mVrc (%Rrc) set when guest session indicated an error\n", mData.mVrc));
        if (pvrcGuest)
            *pvrcGuest = mData.mVrc; /* Return last set error. */
        return VERR_GSTCTL_GUEST_ERROR;
    }

    /* Guest Additions < 4.3 don't support session handling, skip. */
    if (mData.mProtocolVersion < 2)
    {
        waitResult = GuestSessionWaitResult_WaitFlagNotSupported;

        LogFlowThisFunc(("Installed Guest Additions don't support waiting for dedicated sessions, skipping\n"));
        return VINF_SUCCESS;
    }

    waitResult = GuestSessionWaitResult_None;
    if (fWaitFlags & GuestSessionWaitForFlag_Terminate)
    {
        switch (mData.mStatus)
        {
            case GuestSessionStatus_Terminated:
            case GuestSessionStatus_Down:
                waitResult = GuestSessionWaitResult_Terminate;
                break;

            case GuestSessionStatus_TimedOutKilled:
            case GuestSessionStatus_TimedOutAbnormally:
                waitResult = GuestSessionWaitResult_Timeout;
                break;

            case GuestSessionStatus_Error:
                /* Handled above. */
                break;

            case GuestSessionStatus_Started:
                waitResult = GuestSessionWaitResult_Start;
                break;

            case GuestSessionStatus_Undefined:
            case GuestSessionStatus_Starting:
                /* Do the waiting below. */
                break;

            default:
                AssertMsgFailed(("Unhandled session status %RU32\n", mData.mStatus));
                return VERR_NOT_IMPLEMENTED;
        }
    }
    else if (fWaitFlags & GuestSessionWaitForFlag_Start)
    {
        switch (mData.mStatus)
        {
            case GuestSessionStatus_Started:
            case GuestSessionStatus_Terminating:
            case GuestSessionStatus_Terminated:
            case GuestSessionStatus_Down:
                waitResult = GuestSessionWaitResult_Start;
                break;

            case GuestSessionStatus_Error:
                waitResult = GuestSessionWaitResult_Error;
                break;

            case GuestSessionStatus_TimedOutKilled:
            case GuestSessionStatus_TimedOutAbnormally:
                waitResult = GuestSessionWaitResult_Timeout;
                break;

            case GuestSessionStatus_Undefined:
            case GuestSessionStatus_Starting:
                /* Do the waiting below. */
                break;

            default:
                AssertMsgFailed(("Unhandled session status %RU32\n", mData.mStatus));
                return VERR_NOT_IMPLEMENTED;
        }
    }

    LogFlowThisFunc(("sessionStatus=%RU32, vrcSession=%Rrc, waitResult=%RU32\n", mData.mStatus, mData.mVrc, waitResult));

    /* No waiting needed? Return immediately using the last set error. */
    if (waitResult != GuestSessionWaitResult_None)
    {
        if (pvrcGuest)
            *pvrcGuest = mData.mVrc; /* Return last set error (if any). */
        return RT_SUCCESS(mData.mVrc) ? VINF_SUCCESS : VERR_GSTCTL_GUEST_ERROR;
    }

    int vrc = VINF_SUCCESS;

    uint64_t const tsStart = RTTimeMilliTS();
    uint64_t       tsNow   = tsStart;

    while (tsNow - tsStart < uTimeoutMS)
    {
        GuestWaitEvent *pEvent = NULL;
        GuestEventTypes eventTypes;
        try
        {
            eventTypes.push_back(VBoxEventType_OnGuestSessionStateChanged);

            vrc = registerWaitEventEx(mData.mSession.mID, mData.mObjectID, eventTypes, &pEvent);
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        alock.release(); /* Release lock before waiting. */

        GuestSessionStatus_T sessionStatus;
        vrc = i_waitForStatusChange(pEvent, fWaitFlags,
                                    uTimeoutMS - (tsNow - tsStart), &sessionStatus, pvrcGuest);
        if (RT_SUCCESS(vrc))
        {
            switch (sessionStatus)
            {
                case GuestSessionStatus_Started:
                    waitResult = GuestSessionWaitResult_Start;
                    break;

                case GuestSessionStatus_Starting:
                    RT_FALL_THROUGH();
                case GuestSessionStatus_Terminating:
                    if (fWaitFlags & GuestSessionWaitForFlag_Status) /* Any status wanted? */
                        waitResult = GuestSessionWaitResult_Status;
                    /* else: Wait another round until we get the event(s) fWaitFlags defines. */
                    break;

                case GuestSessionStatus_Terminated:
                    waitResult = GuestSessionWaitResult_Terminate;
                    break;

                case GuestSessionStatus_TimedOutKilled:
                    RT_FALL_THROUGH();
                case GuestSessionStatus_TimedOutAbnormally:
                    waitResult = GuestSessionWaitResult_Timeout;
                    break;

                case GuestSessionStatus_Down:
                    waitResult = GuestSessionWaitResult_Terminate;
                    break;

                case GuestSessionStatus_Error:
                    waitResult = GuestSessionWaitResult_Error;
                    break;

                default:
                    waitResult = GuestSessionWaitResult_Status;
                    break;
            }
        }

        unregisterWaitEvent(pEvent);

        /* Wait result not None, e.g. some result acquired or a wait error occurred? Bail out. */
        if (   waitResult != GuestSessionWaitResult_None
            || RT_FAILURE(vrc))
            break;

        tsNow = RTTimeMilliTS();

        alock.acquire(); /* Re-acquire lock before waiting for the next event. */
    }

    if (tsNow - tsStart >= uTimeoutMS)
    {
        waitResult = GuestSessionWaitResult_None; /* Paranoia. */
        vrc = VERR_TIMEOUT;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Waits for guest session status changes.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR on received guest error.
 * @retval  VERR_WRONG_ORDER when an unexpected event type has been received.
 * @param   pEvent              Wait event to use for waiting.
 * @param   fWaitFlags          Wait flags to use.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pSessionStatus      Where to return the guest session status.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestSession::i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t fWaitFlags, uint32_t uTimeoutMS,
                                        GuestSessionStatus_T *pSessionStatus, int *pvrcGuest)
{
    RT_NOREF(fWaitFlags);
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS, &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        if (evtType == VBoxEventType_OnGuestSessionStateChanged)
        {
            ComPtr<IGuestSessionStateChangedEvent> pChangedEvent = pIEvent;
            Assert(!pChangedEvent.isNull());

            GuestSessionStatus_T sessionStatus;
            pChangedEvent->COMGETTER(Status)(&sessionStatus);
            if (pSessionStatus)
                *pSessionStatus = sessionStatus;

            ComPtr<IVirtualBoxErrorInfo> errorInfo;
            HRESULT hrc = pChangedEvent->COMGETTER(Error)(errorInfo.asOutParam());
            ComAssertComRC(hrc);

            LONG lGuestRc;
            hrc = errorInfo->COMGETTER(ResultDetail)(&lGuestRc);
            ComAssertComRC(hrc);
            if (RT_FAILURE((int)lGuestRc))
                vrc = VERR_GSTCTL_GUEST_ERROR;
            if (pvrcGuest)
                *pvrcGuest = (int)lGuestRc;

            LogFlowThisFunc(("Status changed event for session ID=%RU32, new status is: %RU32 (%Rrc)\n",
                             mData.mSession.mID, sessionStatus,
                             RT_SUCCESS((int)lGuestRc) ? VINF_SUCCESS : (int)lGuestRc));
        }
        else /** @todo Re-visit this. Can this happen more frequently? */
            AssertMsgFailedReturn(("Got unexpected event type %#x\n", evtType), VERR_WRONG_ORDER);
    }
    /* waitForEvent may also return VERR_GSTCTL_GUEST_ERROR like we do above, so make pvrcGuest is set. */
    else if (pEvent->HasGuestError() && pvrcGuest)
        *pvrcGuest = pEvent->GuestResult();
    Assert(vrc != VERR_GSTCTL_GUEST_ERROR || !pvrcGuest || *pvrcGuest != (int)0xcccccccc);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

// implementation of public methods
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestSession::close()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS; /* Shut up MSVC. */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* If the guest session is not in an usable state (anymore), do the cleanup stuff ourselves. */
    if (!i_isReady())
    {
        i_onRemove();
        return S_OK;
    }

    alock.release(); /* Leave lock before calling i_closeSession() below. */

    int vrcGuest = VINF_SUCCESS;

    /* If the session is in a started state, tell the guest to close it.
     *
     * Note: Don't check if the session is ready via i_isStartedExternal() here;
     *       the session (already) could be in a stopped / aborted state.
     */
    AutoCaller autoCallerParent(mParent);
    if (   SUCCEEDED(autoCallerParent.hrc())
        && i_isStarted())
    {
        uint32_t msTimeout = RT_MS_10SEC; /* 10s timeout by default */
        for (int i = 0; i < 3; i++)
        {
            if (i)
            {
                LogRel(("Guest Control: Closing session '%s' timed out (%RU32s timeout, attempt %d/10), retrying ...\n",
                        mData.mSession.mName.c_str(), msTimeout / RT_MS_1SEC, i + 1));
                msTimeout += RT_MS_5SEC; /* Slightly increase the timeout. */
            }

            /* Close session on guest. */
            vrc = i_closeSession(0 /* Flags */, msTimeout, &vrcGuest);
            if (   RT_SUCCESS(vrc)
                || vrc != VERR_TIMEOUT) /* If something else happened there is no point in retrying further. */
                break;
        }
    }

    /* On failure don't return here, instead do all the cleanup
     * work first and then return an error. */

    /* We have to make sure that our parent (IGuest) still is alive and in a working shapee.
     * If not, skip removing the session from it. */
    LogFlowThisFunc(("Removing session '%s' from parent ...", mData.mSession.mName.c_str()));

    /* Remove ourselves from the session list. */
    int vrc2 = mParent->i_sessionRemove(mData.mSession.mID);
    if (vrc2 == VERR_NOT_FOUND) /* Not finding the session anymore isn't critical. */
        vrc2 = VINF_SUCCESS;

    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    LogFlowThisFunc(("Returning vrc=%Rrc, vrcGuest=%Rrc\n", vrc, vrcGuest));

    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_GSTCTL_GUEST_ERROR)
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Session, vrcGuest, mData.mSession.mName.c_str());
            return setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Closing guest session failed: %s"),
                                GuestBase::getErrorAsString(ge).c_str());
        }
        return setError(VBOX_E_IPRT_ERROR, tr("Closing guest session \"%s\" failed with %Rrc"),
                        mData.mSession.mName.c_str(), vrc);
    }

    return S_OK;
}

HRESULT GuestSession::fileCopy(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                               const std::vector<FileCopyFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aSource, aDestination, aFlags, aProgress);
    ReturnComNotImplemented();
}

HRESULT GuestSession::fileCopyFromGuest(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                                        const std::vector<FileCopyFlag_T> &aFlags,
                                        ComPtr<IProgress> &aProgress)
{
    uint32_t fFlags = FileCopyFlag_None;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
            fFlags |= aFlags[i];

        const uint32_t fValidFlags = FileCopyFlag_None | FileCopyFlag_NoReplace | FileCopyFlag_FollowLinks | FileCopyFlag_Update;
        if (fFlags & ~fValidFlags)
            return setError(E_INVALIDARG,tr("Unknown flags: flags value %#x, invalid: %#x"), fFlags, fFlags & ~fValidFlags);
    }

    GuestSessionFsSourceSet SourceSet;

    GuestSessionFsSourceSpec source;
    source.strSource      = aSource;
    source.enmType        = FsObjType_File;
    source.enmPathStyle   = i_getGuestPathStyle();
    source.fDryRun        = false; /** @todo Implement support for a dry run. */
    source.fDirCopyFlags  = DirectoryCopyFlag_None;
    source.fFileCopyFlags = (FileCopyFlag_T)fFlags;

    SourceSet.push_back(source);

    return i_copyFromGuest(SourceSet, aDestination, aProgress);
}

HRESULT GuestSession::fileCopyToGuest(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                                      const std::vector<FileCopyFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
    uint32_t fFlags = FileCopyFlag_None;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
            fFlags |= aFlags[i];

        const uint32_t fValidFlags = FileCopyFlag_None | FileCopyFlag_NoReplace | FileCopyFlag_FollowLinks | FileCopyFlag_Update;
        if (fFlags & ~fValidFlags)
            return setError(E_INVALIDARG,tr("Unknown flags: flags value %#x, invalid: %#x"), fFlags, fFlags & ~fValidFlags);
    }

    GuestSessionFsSourceSet SourceSet;

    GuestSessionFsSourceSpec source;
    source.strSource      = aSource;
    source.enmType        = FsObjType_File;
    source.enmPathStyle   = GuestSession::i_getHostPathStyle();
    source.fDryRun        = false; /** @todo Implement support for a dry run. */
    source.fDirCopyFlags  = DirectoryCopyFlag_None;
    source.fFileCopyFlags = (FileCopyFlag_T)fFlags;

    SourceSet.push_back(source);

    return i_copyToGuest(SourceSet, aDestination, aProgress);
}

HRESULT GuestSession::copyFromGuest(const std::vector<com::Utf8Str> &aSources, const std::vector<com::Utf8Str> &aFilters,
                                    const std::vector<com::Utf8Str> &aFlags, const com::Utf8Str &aDestination,
                                    ComPtr<IProgress> &aProgress)
{
    const size_t cSources = aSources.size();
    if (   (aFilters.size() && aFilters.size() != cSources)
        || (aFlags.size()   && aFlags.size()   != cSources))
    {
        return setError(E_INVALIDARG, tr("Parameter array sizes don't match to the number of sources specified"));
    }

    GuestSessionFsSourceSet SourceSet;

    std::vector<com::Utf8Str>::const_iterator itSource = aSources.begin();
    std::vector<com::Utf8Str>::const_iterator itFilter = aFilters.begin();
    std::vector<com::Utf8Str>::const_iterator itFlags  = aFlags.begin();

    const bool fContinueOnErrors = false; /** @todo Do we want a flag for that? */
    const bool fFollowSymlinks   = true;  /** @todo Ditto. */

    while (itSource != aSources.end())
    {
        GuestFsObjData objData;
        int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
        int vrc = i_fsObjQueryInfo(*(itSource), fFollowSymlinks, objData, &vrcGuest);
        if (   RT_FAILURE(vrc)
            && !fContinueOnErrors)
        {
            if (GuestProcess::i_isGuestError(vrc))
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, (*itSource).c_str());
                return setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying type for guest source failed: %s"),
                                    GuestBase::getErrorAsString(ge).c_str());
            }
            return setError(E_FAIL, tr("Querying type for guest source \"%s\" failed: %Rrc"), (*itSource).c_str(), vrc);
        }

        Utf8Str strFlags;
        if (itFlags != aFlags.end())
        {
            strFlags = *itFlags;
            ++itFlags;
        }

        Utf8Str strFilter;
        if (itFilter != aFilters.end())
        {
            strFilter = *itFilter;
            ++itFilter;
        }

        GuestSessionFsSourceSpec source;
        source.strSource    = *itSource;
        source.strFilter    = strFilter;
        source.enmType      = objData.mType;
        source.enmPathStyle = i_getGuestPathStyle();
        source.fDryRun      = false; /** @todo Implement support for a dry run. */

        /* Check both flag groups here, as copying a directory also could mean to explicitly
         * *not* replacing any existing files (or just copy files which are newer, for instance). */
        GuestSession::i_directoryCopyFlagFromStr(strFlags, false /* fStrict */, &source.fDirCopyFlags);
        GuestSession::i_fileCopyFlagFromStr(strFlags, false /* fStrict */, &source.fFileCopyFlags);

        SourceSet.push_back(source);

        ++itSource;
    }

    return i_copyFromGuest(SourceSet, aDestination, aProgress);
}

HRESULT GuestSession::copyToGuest(const std::vector<com::Utf8Str> &aSources, const std::vector<com::Utf8Str> &aFilters,
                                  const std::vector<com::Utf8Str> &aFlags, const com::Utf8Str &aDestination,
                                  ComPtr<IProgress> &aProgress)
{
    const size_t cSources = aSources.size();
    if (   (aFilters.size() && aFilters.size() != cSources)
        || (aFlags.size()   && aFlags.size()   != cSources))
    {
        return setError(E_INVALIDARG, tr("Parameter array sizes don't match to the number of sources specified"));
    }

    GuestSessionFsSourceSet SourceSet;

    std::vector<com::Utf8Str>::const_iterator itSource = aSources.begin();
    std::vector<com::Utf8Str>::const_iterator itFilter = aFilters.begin();
    std::vector<com::Utf8Str>::const_iterator itFlags  = aFlags.begin();

    const bool fContinueOnErrors = false; /** @todo Do we want a flag for that? */

    while (itSource != aSources.end())
    {
        RTFSOBJINFO objInfo;
        int vrc = RTPathQueryInfo((*itSource).c_str(), &objInfo, RTFSOBJATTRADD_NOTHING);
        if (   RT_FAILURE(vrc)
            && !fContinueOnErrors)
        {
            return setError(E_FAIL, tr("Unable to query type for source '%s' (%Rrc)"), (*itSource).c_str(), vrc);
        }

        Utf8Str strFlags;
        if (itFlags != aFlags.end())
        {
            strFlags = *itFlags;
            ++itFlags;
        }

        Utf8Str strFilter;
        if (itFilter != aFilters.end())
        {
            strFilter = *itFilter;
            ++itFilter;
        }

        GuestSessionFsSourceSpec source;
        source.strSource    = *itSource;
        source.strFilter    = strFilter;
        source.enmType      = GuestBase::fileModeToFsObjType(objInfo.Attr.fMode);
        source.enmPathStyle = GuestSession::i_getHostPathStyle();
        source.fDryRun      = false; /** @todo Implement support for a dry run. */

        GuestSession::i_directoryCopyFlagFromStr(strFlags, false /* fStrict */, &source.fDirCopyFlags);
        GuestSession::i_fileCopyFlagFromStr(strFlags, false /* fStrict */, &source.fFileCopyFlags);

        SourceSet.push_back(source);

        ++itSource;
    }

    /* (Re-)Validate stuff. */
    if (RT_UNLIKELY(SourceSet.size() == 0)) /* At least one source must be present. */
        return setError(E_INVALIDARG, tr("No sources specified"));
    if (RT_UNLIKELY(SourceSet[0].strSource.isEmpty()))
        return setError(E_INVALIDARG, tr("First source entry is empty"));
    if (RT_UNLIKELY(aDestination.isEmpty()))
        return setError(E_INVALIDARG, tr("No destination specified"));

    return i_copyToGuest(SourceSet, aDestination, aProgress);
}

HRESULT GuestSession::directoryCopy(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                                    const std::vector<DirectoryCopyFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aSource, aDestination, aFlags, aProgress);
    ReturnComNotImplemented();
}

HRESULT GuestSession::directoryCopyFromGuest(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                                             const std::vector<DirectoryCopyFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
    uint32_t fFlags = DirectoryCopyFlag_None;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
            fFlags |= aFlags[i];

        const uint32_t fValidFlags =   DirectoryCopyFlag_None | DirectoryCopyFlag_CopyIntoExisting | DirectoryCopyFlag_Recursive
                                     | DirectoryCopyFlag_FollowLinks;
        if (fFlags & ~fValidFlags)
            return setError(E_INVALIDARG,tr("Unknown flags: flags value %#x, invalid: %#x"), fFlags, fFlags & ~fValidFlags);
    }

    GuestSessionFsSourceSet SourceSet;

    GuestSessionFsSourceSpec source;
    source.strSource      = aSource;
    source.enmType        = FsObjType_Directory;
    source.enmPathStyle   = i_getGuestPathStyle();
    source.fDryRun        = false; /** @todo Implement support for a dry run. */
    source.fDirCopyFlags  = (DirectoryCopyFlag_T)fFlags;
    source.fFileCopyFlags = FileCopyFlag_None; /* Overwrite existing files. */

    SourceSet.push_back(source);

    return i_copyFromGuest(SourceSet, aDestination, aProgress);
}

HRESULT GuestSession::directoryCopyToGuest(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                                           const std::vector<DirectoryCopyFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
    uint32_t fFlags = DirectoryCopyFlag_None;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
            fFlags |= aFlags[i];

        const uint32_t fValidFlags =   DirectoryCopyFlag_None | DirectoryCopyFlag_CopyIntoExisting | DirectoryCopyFlag_Recursive
                                     | DirectoryCopyFlag_FollowLinks;
        if (fFlags & ~fValidFlags)
            return setError(E_INVALIDARG,tr("Unknown flags: flags value %#x, invalid: %#x"), fFlags, fFlags & ~fValidFlags);
    }

    GuestSessionFsSourceSet SourceSet;

    GuestSessionFsSourceSpec source;
    source.strSource      = aSource;
    source.enmType        = FsObjType_Directory;
    source.enmPathStyle   = GuestSession::i_getHostPathStyle();
    source.fDryRun        = false; /** @todo Implement support for a dry run. */
    source.fDirCopyFlags  = (DirectoryCopyFlag_T)fFlags;
    source.fFileCopyFlags = FileCopyFlag_None; /* Overwrite existing files. */

    SourceSet.push_back(source);

    return i_copyToGuest(SourceSet, aDestination, aProgress);
}

HRESULT GuestSession::directoryCreate(const com::Utf8Str &aPath, ULONG aMode,
                                      const std::vector<DirectoryCreateFlag_T> &aFlags)
{
    if (RT_UNLIKELY((aPath.c_str()) == NULL || *(aPath.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No directory to create specified"));

    uint32_t fFlags = DirectoryCreateFlag_None;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
            fFlags |= aFlags[i];

        if (fFlags & ~DirectoryCreateFlag_Parents)
            return setError(E_INVALIDARG, tr("Unknown flags (%#x)"), fFlags);
    }

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    ComObjPtr <GuestDirectory> pDirectory;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_directoryCreate(aPath, (uint32_t)aMode, fFlags, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, aPath.c_str());
            return setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Guest directory creation failed: %s"),
                                GuestBase::getErrorAsString(ge).c_str());
        }
        switch (vrc)
        {
            case VERR_INVALID_PARAMETER:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Guest directory creation failed: Invalid parameters given"));
                break;

            case VERR_BROKEN_PIPE:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Guest directory creation failed: Unexpectedly aborted"));
                break;

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Guest directory creation failed: %Rrc"), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::directoryCreateTemp(const com::Utf8Str &aTemplateName, ULONG aMode, const com::Utf8Str &aPath,
                                          BOOL aSecure, com::Utf8Str &aDirectory)
{
    if (RT_UNLIKELY((aTemplateName.c_str()) == NULL || *(aTemplateName.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No template specified"));
    if (RT_UNLIKELY((aPath.c_str()) == NULL || *(aPath.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No directory name specified"));
    if (!aSecure) /* Ignore what mode is specified when a secure temp thing needs to be created. */
        if (RT_UNLIKELY(aMode & ~07777))
            return setError(E_INVALIDARG, tr("Mode invalid (must be specified in octal mode)"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fsCreateTemp(aTemplateName, aPath, true /* Directory */, aDirectory, aMode, RT_BOOL(aSecure), &vrcGuest);
    if (!RT_SUCCESS(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Temporary guest directory creation failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
               hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Temporary guest directory creation \"%s\" with template \"%s\" failed: %Rrc"),
                                  aPath.c_str(), aTemplateName.c_str(), vrc);
               break;
        }
    }

    return hrc;
}

HRESULT GuestSession::directoryExists(const com::Utf8Str &aPath, BOOL aFollowSymlinks, BOOL *aExists)
{
    if (RT_UNLIKELY(aPath.isEmpty()))
        return setError(E_INVALIDARG, tr("Empty path"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    GuestFsObjData objData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;

    int vrc = i_directoryQueryInfo(aPath, aFollowSymlinks != FALSE, objData, &vrcGuest);
    if (RT_SUCCESS(vrc))
        *aExists = TRUE;
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                switch (vrcGuest)
                {
                    case VERR_PATH_NOT_FOUND:
                        *aExists = FALSE;
                        break;
                    default:
                    {
                        GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying directory existence failed: %s"),
                                           GuestBase::getErrorAsString(ge).c_str());
                        break;
                    }
                }
                break;
            }

            case VERR_NOT_A_DIRECTORY:
            {
                *aExists = FALSE;
                break;
            }

            default:
               hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Querying directory existence \"%s\" failed: %Rrc"),
                                  aPath.c_str(), vrc);
               break;
        }
    }

    return hrc;
}

HRESULT GuestSession::directoryOpen(const com::Utf8Str &aPath, const com::Utf8Str &aFilter,
                                    const std::vector<DirectoryOpenFlag_T> &aFlags, ComPtr<IGuestDirectory> &aDirectory)
{
    if (RT_UNLIKELY((aPath.c_str()) == NULL || *(aPath.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No directory to open specified"));
    if (RT_UNLIKELY((aFilter.c_str()) != NULL && *(aFilter.c_str()) != '\0'))
        return setError(E_INVALIDARG, tr("Directory filters are not implemented yet"));

    uint32_t fFlags = DirectoryOpenFlag_None;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
            fFlags |= aFlags[i];

        if (fFlags)
            return setError(E_INVALIDARG, tr("Open flags (%#x) not implemented yet"), fFlags);
    }

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    GuestDirectoryOpenInfo openInfo;
    openInfo.mPath = aPath;
    openInfo.mFilter = aFilter;
    openInfo.mFlags = fFlags;

    ComObjPtr<GuestDirectory> pDirectory;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_directoryOpen(openInfo, pDirectory, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        /* Return directory object to the caller. */
        hrc = pDirectory.queryInterfaceTo(aDirectory.asOutParam());
    }
    else
    {
        switch (vrc)
        {
            case VERR_INVALID_PARAMETER:
               hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Opening guest directory \"%s\" failed; invalid parameters given"),
                                  aPath.c_str());
               break;

            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, aPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Opening guest directory failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
               hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Opening guest directory \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
               break;
        }
    }

    return hrc;
}

HRESULT GuestSession::directoryRemove(const com::Utf8Str &aPath)
{
    if (RT_UNLIKELY(aPath.c_str() == NULL || *aPath.c_str() == '\0'))
        return setError(E_INVALIDARG, tr("No directory to remove specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    /* No flags; only remove the directory when empty. */
    uint32_t fFlags = DIRREMOVEREC_FLAG_NONE;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_directoryRemove(aPath, fFlags, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NOT_SUPPORTED:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Handling removing guest directories not supported by installed Guest Additions"));
                break;

            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, aPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Removing guest directory failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Removing guest directory \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::directoryRemoveRecursive(const com::Utf8Str &aPath, const std::vector<DirectoryRemoveRecFlag_T> &aFlags,
                                               ComPtr<IProgress> &aProgress)
{
    if (RT_UNLIKELY(aPath.c_str() == NULL || *aPath.c_str() == '\0'))
        return setError(E_INVALIDARG, tr("No directory to remove recursively specified"));

    /* By defautl remove recursively as the function name implies. */
    uint32_t fFlags = DIRREMOVEREC_FLAG_RECURSIVE;
    if (aFlags.size())
    {
        for (size_t i = 0; i < aFlags.size(); i++)
        {
            switch (aFlags[i])
            {
                case DirectoryRemoveRecFlag_None: /* Skip. */
                    continue;

                case DirectoryRemoveRecFlag_ContentAndDir:
                    fFlags |= DIRREMOVEREC_FLAG_CONTENT_AND_DIR;
                    break;

                case DirectoryRemoveRecFlag_ContentOnly:
                    fFlags |= DIRREMOVEREC_FLAG_CONTENT_ONLY;
                    break;

                default:
                    return setError(E_INVALIDARG, tr("Invalid flags specified"));
            }
        }
    }

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    ComObjPtr<Progress> pProgress;
    hrc = pProgress.createObject();
    if (SUCCEEDED(hrc))
        hrc = pProgress->init(static_cast<IGuestSession *>(this),
                              Bstr(tr("Removing guest directory")).raw(),
                              TRUE /*aCancelable*/);
    if (FAILED(hrc))
        return hrc;

    /* Note: At the moment we don't supply progress information while
     *       deleting a guest directory recursively. So just complete
     *       the progress object right now. */
     /** @todo Implement progress reporting on guest directory deletion! */
    hrc = pProgress->i_notifyComplete(S_OK);
    if (FAILED(hrc))
        return hrc;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_directoryRemove(aPath, fFlags, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NOT_SUPPORTED:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Handling removing guest directories recursively not supported by installed Guest Additions"));
                break;

            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, aPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Recursively removing guest directory failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Recursively removing guest directory \"%s\" failed: %Rrc"),
                                   aPath.c_str(), vrc);
                break;
        }
    }
    else
    {
        pProgress.queryInterfaceTo(aProgress.asOutParam());
    }

    return hrc;
}

HRESULT GuestSession::environmentScheduleSet(const com::Utf8Str &aName, const com::Utf8Str &aValue)
{
    LogFlowThisFuncEnter();
    int vrc;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        vrc = mData.mEnvironmentChanges.setVariable(aName, aValue);
    }
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
        hrc = S_OK;
    else if (vrc == VERR_ENV_INVALID_VAR_NAME)
        hrc = setError(E_INVALIDARG, tr("Invalid environment variable name '%s'"), aName.c_str());
    else
        hrc = setErrorVrc(vrc, tr("Failed to schedule setting environment variable '%s' to '%s'"), aName.c_str(), aValue.c_str());

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT GuestSession::environmentScheduleUnset(const com::Utf8Str &aName)
{
    LogFlowThisFuncEnter();
    int vrc;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        vrc = mData.mEnvironmentChanges.unsetVariable(aName);
    }
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
        hrc = S_OK;
    else if (vrc == VERR_ENV_INVALID_VAR_NAME)
        hrc = setError(E_INVALIDARG, tr("Invalid environment variable name '%s'"), aName.c_str());
    else
        hrc = setErrorVrc(vrc, tr("Failed to schedule unsetting environment variable '%s'"), aName.c_str());

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT GuestSession::environmentGetBaseVariable(const com::Utf8Str &aName, com::Utf8Str &aValue)
{
    LogFlowThisFuncEnter();
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc;
    if (mData.mpBaseEnvironment)
    {
        int vrc = mData.mpBaseEnvironment->getVariable(aName, &aValue);
        if (RT_SUCCESS(vrc))
            hrc = S_OK;
        else if (vrc == VERR_ENV_INVALID_VAR_NAME)
            hrc = setError(E_INVALIDARG, tr("Invalid environment variable name '%s'"), aName.c_str());
        else
            hrc = setErrorVrc(vrc);
    }
    else if (mData.mProtocolVersion < 99999)
        hrc = setError(VBOX_E_NOT_SUPPORTED, tr("The base environment feature is not supported by the Guest Additions"));
    else
        hrc = setError(VBOX_E_INVALID_OBJECT_STATE, tr("The base environment has not yet been reported by the guest"));

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT GuestSession::environmentDoesBaseVariableExist(const com::Utf8Str &aName, BOOL *aExists)
{
    LogFlowThisFuncEnter();
    *aExists = FALSE;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc;
    if (mData.mpBaseEnvironment)
    {
        hrc = S_OK;
        *aExists = mData.mpBaseEnvironment->doesVariableExist(aName);
    }
    else if (mData.mProtocolVersion < 99999)
        hrc = setError(VBOX_E_NOT_SUPPORTED, tr("The base environment feature is not supported by the Guest Additions"));
    else
        hrc = setError(VBOX_E_INVALID_OBJECT_STATE, tr("The base environment has not yet been reported by the guest"));

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT GuestSession::fileCreateTemp(const com::Utf8Str &aTemplateName, ULONG aMode, const com::Utf8Str &aPath, BOOL aSecure,
                                     ComPtr<IGuestFile> &aFile)
{
    RT_NOREF(aTemplateName, aMode, aPath, aSecure, aFile);
    ReturnComNotImplemented();
}

HRESULT GuestSession::fileExists(const com::Utf8Str &aPath, BOOL aFollowSymlinks, BOOL *aExists)
{
    /* By default we return non-existent. */
    *aExists = FALSE;

    if (RT_UNLIKELY((aPath.c_str()) == NULL || *(aPath.c_str()) == '\0'))
        return S_OK;

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    GuestFsObjData objData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fileQueryInfo(aPath, RT_BOOL(aFollowSymlinks), objData, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        *aExists = TRUE;
        return S_OK;
    }

    switch (vrc)
    {
        case VERR_GSTCTL_GUEST_ERROR:
        {
            switch (vrcGuest)
            {
                case VERR_PATH_NOT_FOUND:
                    RT_FALL_THROUGH();
                case VERR_FILE_NOT_FOUND:
                    break;

                default:
                {
                    GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
                    hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest file existence failed: %s"),
                                       GuestBase::getErrorAsString(ge).c_str());
                    break;
                }
            }

            break;
        }

        case VERR_NOT_A_FILE:
            break;

        default:
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Querying guest file information for \"%s\" failed: %Rrc"),
                               aPath.c_str(), vrc);
            break;
    }

    return hrc;
}

HRESULT GuestSession::fileOpen(const com::Utf8Str &aPath, FileAccessMode_T aAccessMode, FileOpenAction_T aOpenAction,
                               ULONG aCreationMode, ComPtr<IGuestFile> &aFile)
{
    LogFlowThisFuncEnter();

    const std::vector<FileOpenExFlag_T> EmptyFlags;
    return fileOpenEx(aPath, aAccessMode, aOpenAction, FileSharingMode_All, aCreationMode, EmptyFlags, aFile);
}

HRESULT GuestSession::fileOpenEx(const com::Utf8Str &aPath, FileAccessMode_T aAccessMode, FileOpenAction_T aOpenAction,
                                 FileSharingMode_T aSharingMode, ULONG aCreationMode,
                                 const std::vector<FileOpenExFlag_T> &aFlags, ComPtr<IGuestFile> &aFile)
{
    if (RT_UNLIKELY((aPath.c_str()) == NULL || *(aPath.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No file to open specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFuncEnter();

    /* Validate aAccessMode. */
    switch (aAccessMode)
    {
        case FileAccessMode_ReadOnly:
            RT_FALL_THRU();
        case FileAccessMode_WriteOnly:
            RT_FALL_THRU();
        case FileAccessMode_ReadWrite:
            break;
        case FileAccessMode_AppendOnly:
            RT_FALL_THRU();
        case FileAccessMode_AppendRead:
            return setError(E_NOTIMPL, tr("Append access modes are not yet implemented"));
        default:
            return setError(E_INVALIDARG, tr("Unknown FileAccessMode value %u (%#x)"), aAccessMode, aAccessMode);
    }

    /* Validate aOpenAction to the old format. */
    switch (aOpenAction)
    {
        case FileOpenAction_OpenExisting:
            RT_FALL_THRU();
        case FileOpenAction_OpenOrCreate:
            RT_FALL_THRU();
        case FileOpenAction_CreateNew:
            RT_FALL_THRU();
        case FileOpenAction_CreateOrReplace:
            RT_FALL_THRU();
        case FileOpenAction_OpenExistingTruncated:
            RT_FALL_THRU();
        case FileOpenAction_AppendOrCreate:
            break;
        default:
            return setError(E_INVALIDARG, tr("Unknown FileOpenAction value %u (%#x)"), aAccessMode, aAccessMode);
    }

    /* Validate aSharingMode. */
    switch (aSharingMode)
    {
        case FileSharingMode_All:
            break;
        case FileSharingMode_Read:
        case FileSharingMode_Write:
        case FileSharingMode_ReadWrite:
        case FileSharingMode_Delete:
        case FileSharingMode_ReadDelete:
        case FileSharingMode_WriteDelete:
            return setError(E_NOTIMPL, tr("Only FileSharingMode_All is currently implemented"));

        default:
            return setError(E_INVALIDARG, tr("Unknown FileOpenAction value %u (%#x)"), aAccessMode, aAccessMode);
    }

    /* Combine and validate flags. */
    uint32_t fOpenEx = 0;
    for (size_t i = 0; i < aFlags.size(); i++)
        fOpenEx |= aFlags[i];
    if (fOpenEx)
        return setError(E_INVALIDARG, tr("Unsupported FileOpenExFlag value(s) in aFlags (%#x)"), fOpenEx);

    ComObjPtr <GuestFile> pFile;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fileOpenEx(aPath, aAccessMode, aOpenAction, aSharingMode, aCreationMode, aFlags, pFile, &vrcGuest);
    if (RT_SUCCESS(vrc))
        /* Return directory object to the caller. */
        hrc = pFile.queryInterfaceTo(aFile.asOutParam());
    else
    {
        switch (vrc)
        {
            case VERR_NOT_SUPPORTED:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Handling guest files not supported by installed Guest Additions"));
                break;

            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_File, vrcGuest, aPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Opening guest file failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Opening guest file \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::fileQuerySize(const com::Utf8Str &aPath, BOOL aFollowSymlinks, LONG64 *aSize)
{
    if (aPath.isEmpty())
        return setError(E_INVALIDARG, tr("No path specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    int64_t llSize;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fileQuerySize(aPath, aFollowSymlinks != FALSE,  &llSize, &vrcGuest);
    if (RT_SUCCESS(vrc))
        *aSize = llSize;
    else
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest file size failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
        }
        else
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Querying guest file size of \"%s\" failed: %Rrc"),
                               vrc, aPath.c_str());
    }

    return hrc;
}

HRESULT GuestSession::fsQueryFreeSpace(const com::Utf8Str &aPath, LONG64 *aFreeSpace)
{
    ComPtr<IGuestFsInfo> pFsInfo;
    HRESULT hrc = fsQueryInfo(aPath, pFsInfo);
    if (SUCCEEDED(hrc))
        hrc = pFsInfo->COMGETTER(FreeSize)(aFreeSpace);

    return hrc;
}

HRESULT GuestSession::fsQueryInfo(const com::Utf8Str &aPath, ComPtr<IGuestFsInfo> &aInfo)
{
    if (aPath.isEmpty())
        return setError(E_INVALIDARG, tr("No path specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    GSTCTLFSINFO fsInfo;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fsQueryInfo(aPath, &fsInfo, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        ComObjPtr<GuestFsInfo> ptrFsInfo;
        hrc = ptrFsInfo.createObject();
        if (SUCCEEDED(hrc))
        {
            vrc = ptrFsInfo->init(&fsInfo);
            if (RT_SUCCESS(vrc))
                hrc = ptrFsInfo.queryInterfaceTo(aInfo.asOutParam());
            else
                hrc = setErrorVrc(vrc);
        }
    }
    else
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest filesystem information failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
        }
        else
            hrc = setErrorVrc(vrc, tr("Querying guest filesystem information for \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
    }

    return hrc;
}

HRESULT GuestSession::fsObjExists(const com::Utf8Str &aPath, BOOL aFollowSymlinks, BOOL *aExists)
{
    if (aPath.isEmpty())
        return setError(E_INVALIDARG, tr("No path specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFunc(("aPath=%s, aFollowSymlinks=%RTbool\n", aPath.c_str(), RT_BOOL(aFollowSymlinks)));

    *aExists = false;

    GuestFsObjData objData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fsObjQueryInfo(aPath, aFollowSymlinks != FALSE, objData, &vrcGuest);
    if (RT_SUCCESS(vrc))
        *aExists = TRUE;
    else
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            if (   vrcGuest == VERR_NOT_A_FILE
                || vrcGuest == VERR_PATH_NOT_FOUND
                || vrcGuest == VERR_FILE_NOT_FOUND
                || vrcGuest == VERR_INVALID_NAME)
                hrc = S_OK; /* Ignore these vrc values. */
            else
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest file existence information failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
            }
        }
        else
            hrc = setErrorVrc(vrc, tr("Querying guest file existence information for \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
    }

    return hrc;
}

HRESULT GuestSession::fsObjQueryInfo(const com::Utf8Str &aPath, BOOL aFollowSymlinks, ComPtr<IGuestFsObjInfo> &aInfo)
{
    if (aPath.isEmpty())
        return setError(E_INVALIDARG, tr("No path specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFunc(("aPath=%s, aFollowSymlinks=%RTbool\n", aPath.c_str(), RT_BOOL(aFollowSymlinks)));

    GuestFsObjData objData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fsObjQueryInfo(aPath, aFollowSymlinks != FALSE, objData, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        ComObjPtr<GuestFsObjInfo> ptrFsObjInfo;
        hrc = ptrFsObjInfo.createObject();
        if (SUCCEEDED(hrc))
        {
            vrc = ptrFsObjInfo->init(objData);
            if (RT_SUCCESS(vrc))
                hrc = ptrFsObjInfo.queryInterfaceTo(aInfo.asOutParam());
            else
                hrc = setErrorVrc(vrc);
        }
    }
    else
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Fs, vrcGuest, aPath.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest filesystem object information failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
        }
        else
            hrc = setErrorVrc(vrc, tr("Querying guest filesystem object information for \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
    }

    return hrc;
}

HRESULT GuestSession::fsObjRemove(const com::Utf8Str &aPath)
{
    if (RT_UNLIKELY(aPath.isEmpty()))
        return setError(E_INVALIDARG, tr("No path specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    LogFlowThisFunc(("aPath=%s\n", aPath.c_str()));

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_fileRemove(aPath, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_File, vrcGuest, aPath.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Removing guest file failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
        }
        else
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Removing guest file \"%s\" failed: %Rrc"), aPath.c_str(), vrc);
    }

    return hrc;
}

HRESULT GuestSession::fsObjRemoveArray(const std::vector<com::Utf8Str> &aPaths, ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aPaths, aProgress);
    return E_NOTIMPL;
}

HRESULT GuestSession::fsObjRename(const com::Utf8Str &aSource,
                                  const com::Utf8Str &aDestination,
                                  const std::vector<FsObjRenameFlag_T> &aFlags)
{
    if (RT_UNLIKELY(aSource.isEmpty()))
        return setError(E_INVALIDARG, tr("No source path specified"));

    if (RT_UNLIKELY(aDestination.isEmpty()))
        return setError(E_INVALIDARG, tr("No destination path specified"));

    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    /* Combine, validate and convert flags. */
    uint32_t fApiFlags = 0;
    for (size_t i = 0; i < aFlags.size(); i++)
        fApiFlags |= aFlags[i];
    if (fApiFlags & ~((uint32_t)FsObjRenameFlag_NoReplace | (uint32_t)FsObjRenameFlag_Replace))
        return setError(E_INVALIDARG, tr("Unknown rename flag: %#x"), fApiFlags);

    LogFlowThisFunc(("aSource=%s, aDestination=%s\n", aSource.c_str(), aDestination.c_str()));

    AssertCompile(FsObjRenameFlag_NoReplace == 0);
    AssertCompile(FsObjRenameFlag_Replace != 0);
    uint32_t fBackend;
    if ((fApiFlags & ((uint32_t)FsObjRenameFlag_NoReplace | (uint32_t)FsObjRenameFlag_Replace)) == FsObjRenameFlag_Replace)
        fBackend = PATHRENAME_FLAG_REPLACE;
    else
        fBackend = PATHRENAME_FLAG_NO_REPLACE;

    /* Call worker to do the job. */
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_pathRename(aSource, aDestination, fBackend, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NOT_SUPPORTED:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Handling renaming guest paths not supported by installed Guest Additions"));
                break;

            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, aSource.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Renaming guest path failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Renaming guest path \"%s\" failed: %Rrc"),
                                   aSource.c_str(), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestSession::fsObjMove(const com::Utf8Str &aSource, const com::Utf8Str &aDestination,
                                const std::vector<FsObjMoveFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aSource, aDestination, aFlags, aProgress);
    ReturnComNotImplemented();
}

HRESULT GuestSession::fsObjMoveArray(const std::vector<com::Utf8Str> &aSource,
                                     const com::Utf8Str &aDestination,
                                     const std::vector<FsObjMoveFlag_T> &aFlags,
                                     ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aSource, aDestination, aFlags, aProgress);
    ReturnComNotImplemented();
}

HRESULT GuestSession::fsObjCopyArray(const std::vector<com::Utf8Str> &aSource,
                                     const com::Utf8Str &aDestination,
                                     const std::vector<FileCopyFlag_T> &aFlags,
                                     ComPtr<IProgress> &aProgress)
{
    RT_NOREF(aSource, aDestination, aFlags, aProgress);
    ReturnComNotImplemented();
}

HRESULT GuestSession::fsObjSetACL(const com::Utf8Str &aPath, BOOL aFollowSymlinks, const com::Utf8Str &aAcl, ULONG aMode)
{
    RT_NOREF(aPath, aFollowSymlinks, aAcl, aMode);
    ReturnComNotImplemented();
}


HRESULT GuestSession::processCreate(const com::Utf8Str &aExecutable, const std::vector<com::Utf8Str> &aArguments,
                                    const com::Utf8Str &aCwd,
                                    const std::vector<com::Utf8Str> &aEnvironment,
                                    const std::vector<ProcessCreateFlag_T> &aFlags,
                                    ULONG aTimeoutMS, ComPtr<IGuestProcess> &aGuestProcess)
{
    LogFlowThisFuncEnter();

    std::vector<LONG> affinityIgnored;
    return processCreateEx(aExecutable, aArguments, aCwd, aEnvironment, aFlags, aTimeoutMS,
                           ProcessPriority_Default, affinityIgnored, aGuestProcess);
}

HRESULT GuestSession::processCreateEx(const com::Utf8Str &aExecutable, const std::vector<com::Utf8Str> &aArguments,
                                      const com::Utf8Str &aCwd,
                                      const std::vector<com::Utf8Str> &aEnvironment,
                                      const std::vector<ProcessCreateFlag_T> &aFlags, ULONG aTimeoutMS,
                                      ProcessPriority_T aPriority, const std::vector<LONG> &aAffinity,
                                      ComPtr<IGuestProcess> &aGuestProcess)
{
    HRESULT hrc = i_isStartedExternal();
    if (FAILED(hrc))
        return hrc;

    /*
     * Must have an executable to execute.  If none is given, we try use the
     * zero'th argument.
     */
    const char *pszExecutable = aExecutable.c_str();
    if (RT_UNLIKELY(pszExecutable == NULL || *pszExecutable == '\0'))
    {
        if (aArguments.size() > 0)
            pszExecutable = aArguments[0].c_str();
        if (pszExecutable == NULL || *pszExecutable == '\0')
            return setError(E_INVALIDARG, tr("No command to execute specified"));
    }

    uint32_t const uProtocol              = i_getProtocolVersion();
    uint64_t const fGuestControlFeatures0 = mParent->i_getGuestControlFeatures0();

    /* If a current working directory (CWD) is set, make sure that the installed Guest Additions actually
     * support this before doing anything else. */
    if (   !aCwd.isEmpty()
        && (   uProtocol < 2
            || !(fGuestControlFeatures0 & VBOX_GUESTCTRL_GF_0_PROCESS_CWD)))
        return setError(VBOX_E_NOT_SUPPORTED,
                        tr("Setting the current working directory is not supported by the installed Guest Addtions!"));

    /* The rest of the input is being validated in i_processCreateEx(). */

    LogFlowThisFuncEnter();

    /*
     * Build the process startup info.
     */
    GuestProcessStartupInfo procInfo;

    /* Executable and arguments. */
    procInfo.mExecutable = pszExecutable;
    if (aArguments.size())
    {
        for (size_t i = 0; i < aArguments.size(); i++)
            procInfo.mArguments.push_back(aArguments[i]);
    }
    else /* If no arguments were given, add the executable as argv[0] by default. */
        procInfo.mArguments.push_back(procInfo.mExecutable);

    /* Optional working directory */
    procInfo.mCwd = aCwd;

    /* Combine the environment changes associated with the ones passed in by
       the caller, giving priority to the latter.  The changes are putenv style
       and will be applied to the standard environment for the guest user. */
    int vrc = procInfo.mEnvironmentChanges.copy(mData.mEnvironmentChanges);
    if (RT_SUCCESS(vrc))
    {
        size_t idxError = ~(size_t)0;
        vrc = procInfo.mEnvironmentChanges.applyPutEnvArray(aEnvironment, &idxError);
        if (RT_SUCCESS(vrc))
        {
            /* Convert the flag array into a mask. */
            if (aFlags.size())
                for (size_t i = 0; i < aFlags.size(); i++)
                    procInfo.mFlags |= aFlags[i];

            procInfo.mTimeoutMS = aTimeoutMS;

            /** @todo use RTCPUSET instead of archaic 64-bit variables! */
            if (aAffinity.size())
                for (size_t i = 0; i < aAffinity.size(); i++)
                    if (aAffinity[i])
                        procInfo.mAffinity |= (uint64_t)1 << i;

            procInfo.mPriority = aPriority;

            /*
             * Create a guest process object.
             */
            ComObjPtr<GuestProcess> pProcess;
            vrc = i_processCreateEx(procInfo, pProcess);
            if (RT_SUCCESS(vrc))
            {
                ComPtr<IGuestProcess> pIProcess;
                hrc = pProcess.queryInterfaceTo(pIProcess.asOutParam());
                if (SUCCEEDED(hrc))
                {
                    /*
                     * Start the process.
                     */
                    vrc = pProcess->i_startProcessAsync();
                    if (RT_SUCCESS(vrc))
                    {
                        aGuestProcess = pIProcess;

                        LogFlowFuncLeaveRC(vrc);
                        return S_OK;
                    }

                    hrc = setErrorVrc(vrc, tr("Failed to start guest process: %Rrc"), vrc);
                }
            }
            else if (vrc == VERR_GSTCTL_MAX_CID_OBJECTS_REACHED)
                hrc = setErrorVrc(vrc, tr("Maximum number of concurrent guest processes per session (%u) reached"),
                                 VBOX_GUESTCTRL_MAX_OBJECTS);
            else
                hrc = setErrorVrc(vrc, tr("Failed to create guest process object: %Rrc"), vrc);
        }
        else
            hrc = setErrorBoth(vrc == VERR_ENV_INVALID_VAR_NAME ? E_INVALIDARG : Global::vboxStatusCodeToCOM(vrc), vrc,
                              tr("Failed to apply environment variable '%s', index %u (%Rrc)'"),
                              aEnvironment[idxError].c_str(), idxError, vrc);
    }
    else
        hrc = setErrorVrc(vrc, tr("Failed to set up the environment: %Rrc"), vrc);

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestSession::processGet(ULONG aPid, ComPtr<IGuestProcess> &aGuestProcess)

{
    if (aPid == 0)
        return setError(E_INVALIDARG, tr("No valid process ID (PID) specified"));

    LogFlowThisFunc(("PID=%RU32\n", aPid));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    ComObjPtr<GuestProcess> pProcess;
    int vrc = i_processGetByPID(aPid, &pProcess);
    if (RT_FAILURE(vrc))
        hrc = setError(E_INVALIDARG, tr("No process with PID %RU32 found"), aPid);

    /* This will set (*aProcess) to NULL if pProgress is NULL. */
    HRESULT hrc2 = pProcess.queryInterfaceTo(aGuestProcess.asOutParam());
    if (SUCCEEDED(hrc))
        hrc = hrc2;

    LogFlowThisFunc(("aProcess=%p, hrc=%Rhrc\n", (IGuestProcess*)aGuestProcess, hrc));
    return hrc;
}

HRESULT GuestSession::symlinkCreate(const com::Utf8Str &aSource, const com::Utf8Str &aTarget, SymlinkType_T aType)
{
    RT_NOREF(aSource, aTarget, aType);
    ReturnComNotImplemented();
}

HRESULT GuestSession::symlinkExists(const com::Utf8Str &aSymlink, BOOL *aExists)

{
    RT_NOREF(aSymlink, aExists);
    ReturnComNotImplemented();
}

HRESULT GuestSession::symlinkRead(const com::Utf8Str &aSymlink, const std::vector<SymlinkReadFlag_T> &aFlags,
                                  com::Utf8Str &aTarget)
{
    RT_NOREF(aSymlink, aFlags, aTarget);
    ReturnComNotImplemented();
}

/* Deprecated; use GuestSession::waitForArray() instead. */
HRESULT GuestSession::waitFor(ULONG aWaitFor, ULONG aTimeoutMS, GuestSessionWaitResult_T *aReason)
{
    /* Note: No call to i_isStartedExternal() needed here, as the session might not has been started (yet). */

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    /*
     * Note: Do not hold any locks here while waiting!
     */
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS; GuestSessionWaitResult_T waitResult;
    int vrc = i_waitFor(aWaitFor, aTimeoutMS, waitResult, &vrcGuest);
    if (RT_SUCCESS(vrc))
        *aReason = waitResult;
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Session, vrcGuest, mData.mSession.mName.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Waiting for guest process failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            case VERR_TIMEOUT:
                *aReason = GuestSessionWaitResult_Timeout;
                break;

            default:
            {
                const char *pszSessionName = mData.mSession.mName.c_str();
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Waiting for guest session \"%s\" failed: %Rrc"),
                                   pszSessionName ? pszSessionName : tr("Unnamed"), vrc);
                break;
            }
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestSession::waitForArray(const std::vector<GuestSessionWaitForFlag_T> &aWaitFor, ULONG aTimeoutMS,
                                   GuestSessionWaitResult_T *aReason)
{
    /* Note: No call to i_isStartedExternal() needed here, as the session might not has been started (yet). */

    LogFlowThisFuncEnter();

    /*
     * Note: Do not hold any locks here while waiting!
     */
    uint32_t fWaitFor = GuestSessionWaitForFlag_None;
    for (size_t i = 0; i < aWaitFor.size(); i++)
        fWaitFor |= aWaitFor[i];

    return WaitFor(fWaitFor, aTimeoutMS, aReason);
}
