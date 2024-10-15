/* $Id: GuestCtrlImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation: Guest
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

#define LOG_GROUP LOG_GROUP_GUEST_CONTROL
#include "LoggingNew.h"

#include "GuestImpl.h"
#ifdef VBOX_WITH_GUEST_CONTROL
# include "GuestSessionImpl.h"
# include "GuestSessionImplTasks.h"
# include "GuestCtrlImplPrivate.h"
#endif

#include "Global.h"
#include "ConsoleImpl.h"
#include "ProgressImpl.h"
#include "VBoxEvents.h"
#include "VMMDev.h"

#include "AutoCaller.h"

#include <VBox/VMMDev.h>
#ifdef VBOX_WITH_GUEST_CONTROL
# include <VBox/com/array.h>
# include <VBox/com/ErrorInfo.h>
#endif
#include <iprt/cpp/utils.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/list.h>
#include <iprt/path.h>
#include <VBox/vmm/pgm.h>
#include <VBox/AssertGuest.h>

#include <memory>


/*
 * This #ifdef goes almost to the end of the file where there are a couple of
 * IGuest method implementations.
 */
#ifdef VBOX_WITH_GUEST_CONTROL


// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Static callback function for receiving updates on guest control messages
 * from the guest. Acts as a dispatcher for the actual class instance.
 *
 * @returns VBox status code.
 * @param   pvExtension         Pointer to HGCM service extension.
 * @param   idMessage           HGCM message ID the callback was called for.
 * @param   pvData              Pointer to user-supplied callback data.
 * @param   cbData              Size (in bytes) of user-supplied callback data.
 */
/* static */
DECLCALLBACK(int) Guest::i_notifyCtrlDispatcher(void    *pvExtension,
                                                uint32_t idMessage,
                                                void    *pvData,
                                                uint32_t cbData)
{
    using namespace guestControl;

    /*
     * No locking, as this is purely a notification which does not make any
     * changes to the object state.
     */
    Log2Func(("pvExtension=%p, idMessage=%RU32, pvParms=%p, cbParms=%RU32\n", pvExtension, idMessage, pvData, cbData));

    ComObjPtr<Guest> pGuest = reinterpret_cast<Guest *>(pvExtension);
    AssertReturn(pGuest.isNotNull(), VERR_WRONG_ORDER);

    /*
     * The data packet should ever be a problem, but check to be sure.
     */
    AssertMsgReturn(cbData == sizeof(VBOXGUESTCTRLHOSTCALLBACK),
                    ("Guest control host callback data has wrong size (expected %zu, got %zu) - buggy host service!\n",
                     sizeof(VBOXGUESTCTRLHOSTCALLBACK), cbData), VERR_INVALID_PARAMETER);
    PVBOXGUESTCTRLHOSTCALLBACK pSvcCb = (PVBOXGUESTCTRLHOSTCALLBACK)pvData;
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    /*
     * Deal with GUEST_MSG_REPORT_FEATURES here as it shouldn't be handed
     * i_dispatchToSession() and has different parameters.
     */
    if (idMessage == GUEST_MSG_REPORT_FEATURES)
    {
        Assert(pSvcCb->mParms == 2);
        Assert(pSvcCb->mpaParms[0].type == VBOX_HGCM_SVC_PARM_64BIT);
        Assert(pSvcCb->mpaParms[1].type == VBOX_HGCM_SVC_PARM_64BIT);
        Assert(pSvcCb->mpaParms[1].u.uint64 & VBOX_GUESTCTRL_GF_1_MUST_BE_ONE);
        pGuest->mData.mfGuestFeatures0 = pSvcCb->mpaParms[0].u.uint64;
        pGuest->mData.mfGuestFeatures1 = pSvcCb->mpaParms[1].u.uint64;
        LogRel(("Guest Control: GUEST_MSG_REPORT_FEATURES: %#RX64, %#RX64\n",
                pGuest->mData.mfGuestFeatures0, pGuest->mData.mfGuestFeatures1));
        return VINF_SUCCESS;
    }

    /*
     * For guest control 2.0 using the legacy messages we need to do the following here:
     * - Get the callback header to access the context ID
     * - Get the context ID of the callback
     * - Extract the session ID out of the context ID
     * - Dispatch the whole stuff to the appropriate session (if still exists)
     *
     * At least context ID parameter must always be present.
     */
    ASSERT_GUEST_RETURN(pSvcCb->mParms > 0, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_MSG_RETURN(pSvcCb->mpaParms[0].type == VBOX_HGCM_SVC_PARM_32BIT,
                            ("type=%d\n", pSvcCb->mpaParms[0].type), VERR_WRONG_PARAMETER_TYPE);
    uint32_t const idContext = pSvcCb->mpaParms[0].u.uint32;

    VBOXGUESTCTRLHOSTCBCTX CtxCb = { idMessage, idContext };
    int vrc = pGuest->i_dispatchToSession(&CtxCb, pSvcCb);

    Log2Func(("CID=%#x, idSession=%RU32, uObject=%RU32, uCount=%RU32, vrc=%Rrc\n",
              idContext, VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(idContext), VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(idContext),
              VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(idContext), vrc));
    return vrc;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Dispatches a host service callback to the appropriate guest control session object.
 *
 * @returns VBox status code.
 * @param   pCtxCb              Pointer to host callback context.
 * @param   pSvcCb              Pointer to callback parameters.
 */
int Guest::i_dispatchToSession(PVBOXGUESTCTRLHOSTCBCTX pCtxCb, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    LogFlowFunc(("pCtxCb=%p, pSvcCb=%p\n", pCtxCb, pSvcCb));

    AssertPtrReturn(pCtxCb, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    Log2Func(("uMessage=%RU32, uContextID=%RU32, uProtocol=%RU32\n", pCtxCb->uMessage, pCtxCb->uContextID, pCtxCb->uProtocol));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    const uint32_t uSessionID = VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(pCtxCb->uContextID);

    Log2Func(("uSessionID=%RU32 (%zu total)\n", uSessionID, mData.mGuestSessions.size()));

    GuestSessions::const_iterator itSession = mData.mGuestSessions.find(uSessionID);

    int vrc;
    if (itSession != mData.mGuestSessions.end())
    {
        ComObjPtr<GuestSession> pSession(itSession->second);
        Assert(!pSession.isNull());

        alock.release();

#ifdef DEBUG
        /*
         * Pre-check: If we got a status message with an error and VERR_TOO_MUCH_DATA
         *            it means that that guest could not handle the entire message
         *            because of its exceeding size. This should not happen on daily
         *            use but testcases might try this. It then makes no sense to dispatch
         *            this further because we don't have a valid context ID.
         */
        bool fDispatch = true;
        vrc = VERR_INVALID_FUNCTION;
        if (   pCtxCb->uMessage == GUEST_MSG_EXEC_STATUS
            && pSvcCb->mParms    >= 5)
        {
            CALLBACKDATA_PROC_STATUS dataCb;
            /* pSvcCb->mpaParms[0] always contains the context ID. */
            HGCMSvcGetU32(&pSvcCb->mpaParms[1], &dataCb.uPID);
            HGCMSvcGetU32(&pSvcCb->mpaParms[2], &dataCb.uStatus);
            HGCMSvcGetU32(&pSvcCb->mpaParms[3], &dataCb.uFlags);
            HGCMSvcGetPv(&pSvcCb->mpaParms[4], &dataCb.pvData, &dataCb.cbData);

            if (   dataCb.uStatus == PROC_STS_ERROR
                && (int32_t)dataCb.uFlags == VERR_TOO_MUCH_DATA)
            {
                LogFlowFunc(("Requested message with too much data, skipping dispatching ...\n"));
                Assert(dataCb.uPID == 0);
                fDispatch = false;
            }
        }
        if (fDispatch)
#endif
        {
            switch (pCtxCb->uMessage)
            {
                case GUEST_MSG_DISCONNECTED:
                    vrc = pSession->i_dispatchToThis(pCtxCb, pSvcCb);
                    break;

                /* Process stuff. */
                case GUEST_MSG_EXEC_STATUS:
                case GUEST_MSG_EXEC_OUTPUT:
                case GUEST_MSG_EXEC_INPUT_STATUS:
                case GUEST_MSG_EXEC_IO_NOTIFY:
                    vrc = pSession->i_dispatchToObject(pCtxCb, pSvcCb);
                    break;

                /* File stuff. */
                case GUEST_MSG_FILE_NOTIFY:
                    vrc = pSession->i_dispatchToObject(pCtxCb, pSvcCb);
                    break;

                /* File system stuff. */
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
                case GUEST_MSG_FS_NOTIFY:
                    vrc = pSession->i_dispatchToThis(pCtxCb, pSvcCb);
                    break;
#endif
                /* Session stuff. */
                case GUEST_MSG_SESSION_NOTIFY:
                    vrc = pSession->i_dispatchToThis(pCtxCb, pSvcCb);
                    break;

                default:
                    vrc = pSession->i_dispatchToObject(pCtxCb, pSvcCb);
                    break;
            }
        }
    }
    else
        vrc = VERR_INVALID_SESSION_ID;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Creates a new guest session.
 * This will invoke VBoxService running on the guest creating a new (dedicated) guest session
 * On older Guest Additions this call has no effect on the guest, and only the credentials will be
 * used for starting/impersonating guest processes.
 *
 * @returns VBox status code.
 * @param   ssInfo              Guest session startup information.
 * @param   guestCreds          Guest OS (user) credentials to use on the guest for creating the session.
 *                              The specified user must be able to logon to the guest and able to start new processes.
 * @param   pGuestSession       Where to store the created guest session on success.
 *
 * @note    Takes the write lock.
 */
int Guest::i_sessionCreate(const GuestSessionStartupInfo &ssInfo,
                           const GuestCredentials &guestCreds, ComObjPtr<GuestSession> &pGuestSession)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VERR_MAX_PROCS_REACHED;
    if (mData.mGuestSessions.size() >= VBOX_GUESTCTRL_MAX_SESSIONS)
        return vrc;

    try
    {
        /* Create a new session ID and assign it. */
        uint32_t uNewSessionID = VBOX_GUESTCTRL_SESSION_ID_BASE;
        uint32_t uTries = 0;

        for (;;)
        {
            /* Is the context ID already used? */
            if (!i_sessionExists(uNewSessionID))
            {
                vrc = VINF_SUCCESS;
                break;
            }
            uNewSessionID++;
            if (uNewSessionID >= VBOX_GUESTCTRL_MAX_SESSIONS)
                uNewSessionID = VBOX_GUESTCTRL_SESSION_ID_BASE;

            if (++uTries == VBOX_GUESTCTRL_MAX_SESSIONS)
                break; /* Don't try too hard. */
        }
        if (RT_FAILURE(vrc)) throw vrc;

        /* Create the session object. */
        HRESULT hrc = pGuestSession.createObject();
        if (FAILED(hrc)) throw VERR_COM_UNEXPECTED;

        /** @todo Use an overloaded copy operator. Later. */
        GuestSessionStartupInfo startupInfo;
        startupInfo.mID = uNewSessionID; /* Assign new session ID. */
        startupInfo.mName = ssInfo.mName;
        startupInfo.mOpenFlags = ssInfo.mOpenFlags;
        startupInfo.mOpenTimeoutMS = ssInfo.mOpenTimeoutMS;

        GuestCredentials guestCredentials;
        if (!guestCreds.mUser.isEmpty())
        {
            /** @todo Use an overloaded copy operator. Later. */
            guestCredentials.mUser = guestCreds.mUser;
            guestCredentials.mPassword = guestCreds.mPassword;
            guestCredentials.mDomain = guestCreds.mDomain;
        }
        else
        {
            /* Internal (annonymous) session. */
            startupInfo.mIsInternal = true;
        }

        vrc = pGuestSession->init(this, startupInfo, guestCredentials);
        if (RT_FAILURE(vrc)) throw vrc;

        /*
         * Add session object to our session map. This is necessary
         * before calling openSession because the guest calls back
         * with the creation result of this session.
         */
        mData.mGuestSessions[uNewSessionID] = pGuestSession;

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestSessionRegisteredEvent(mEventSource, pGuestSession, true /* Registered */);
    }
    catch (int vrc2)
    {
        vrc = vrc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Removes a given guest session and removes it from the internal list.
 *
 * @returns VBox status code.
 * @param   uSessionID          ID of the guest control session to destroy.
 *
 * @note    Takes the read + write locks.
 */
int Guest::i_sessionRemove(uint32_t uSessionID)
{
    LogFlowThisFuncEnter();

    int vrc = VERR_NOT_FOUND;

    LogFlowThisFunc(("Removing session (ID=%RU32) ...\n", uSessionID));

    AutoReadLock arlock(this COMMA_LOCKVAL_SRC_POS);

    GuestSessions::iterator itSessions = mData.mGuestSessions.find(uSessionID);
    if (itSessions == mData.mGuestSessions.end())
        return VERR_NOT_FOUND;

    /* Make sure to consume the pointer before the one of the
     * iterator gets released. */
    ComObjPtr<GuestSession> pSession = itSessions->second;

    LogFlowThisFunc(("Removing session %RU32 (now total %ld sessions)\n",
                     uSessionID, mData.mGuestSessions.size() ? mData.mGuestSessions.size() - 1 : 0));

    vrc = pSession->i_onRemove();

    arlock.release();

    AutoWriteLock awlock(this COMMA_LOCKVAL_SRC_POS);
    mData.mGuestSessions.erase(itSessions);
    awlock.release(); /* Release write lock before firing off event. */

    ::FireGuestSessionRegisteredEvent(mEventSource, pSession, false /* Unregistered */);
    pSession.setNull();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Returns whether a guest control session with a specific ID exists or not.
 *
 * @returns Returns \c true if the session exists, \c false if not.
 * @param   uSessionID          ID to check for.
 *
 * @note    No locking done, as inline function!
 */
inline bool Guest::i_sessionExists(uint32_t uSessionID)
{
    GuestSessions::const_iterator itSessions = mData.mGuestSessions.find(uSessionID);
    return (itSessions == mData.mGuestSessions.end()) ? false : true;
}

#endif /* VBOX_WITH_GUEST_CONTROL */


// implementation of public methods
/////////////////////////////////////////////////////////////////////////////
HRESULT Guest::createSession(const com::Utf8Str &aUser, const com::Utf8Str &aPassword, const com::Utf8Str &aDomain,
                             const com::Utf8Str &aSessionName, ComPtr<IGuestSession> &aGuestSession)

{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_CONTROL */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* Do not allow anonymous sessions (with system rights) with public API. */
    if (RT_UNLIKELY(!aUser.length()))
        return setError(E_INVALIDARG, tr("No user name specified"));

    LogFlowFuncEnter();

    GuestSessionStartupInfo startupInfo;
    startupInfo.mName = aSessionName;

    GuestCredentials guestCreds;
    guestCreds.mUser = aUser;
    guestCreds.mPassword = aPassword;
    guestCreds.mDomain = aDomain;

    ComObjPtr<GuestSession> pSession;
    int vrc = i_sessionCreate(startupInfo, guestCreds, pSession);
    if (RT_SUCCESS(vrc))
    {
        /* Return guest session to the caller. */
        HRESULT hr2 = pSession.queryInterfaceTo(aGuestSession.asOutParam());
        if (FAILED(hr2))
            vrc = VERR_COM_OBJECT_NOT_FOUND;
    }

    if (RT_SUCCESS(vrc))
        /* Start (fork) the session asynchronously
         * on the guest. */
        vrc = pSession->i_startSessionAsync();

    HRESULT hrc = S_OK;
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_MAX_PROCS_REACHED:
                hrc = setErrorBoth(VBOX_E_MAXIMUM_REACHED, vrc, tr("Maximum number of concurrent guest sessions (%d) reached"),
                                   VBOX_GUESTCTRL_MAX_SESSIONS);
                break;

            case VERR_NOT_FOUND: /* Returned by i_sessionCreate(). */
                hrc = setErrorBoth(VBOX_E_GSTCTL_GUEST_ERROR, vrc, tr("Guest Additions are not installed or not ready (yet)"));
                break;

            default:
                hrc = setErrorBoth(VBOX_E_GSTCTL_GUEST_ERROR, vrc, tr("Could not create guest session: %Rrc"), vrc);
                break;
        }
    }

    LogFlowThisFunc(("Returning hrc=%Rhrc\n", hrc));
    return hrc;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

HRESULT Guest::findSession(const com::Utf8Str &aSessionName, std::vector<ComPtr<IGuestSession> > &aSessions)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_CONTROL */

    LogFlowFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    Utf8Str strName(aSessionName);
    std::list < ComObjPtr<GuestSession> > listSessions;

    GuestSessions::const_iterator itSessions = mData.mGuestSessions.begin();
    while (itSessions != mData.mGuestSessions.end())
    {
        if (strName.contains(itSessions->second->i_getName())) /** @todo Use a (simple) pattern match (IPRT?). */
            listSessions.push_back(itSessions->second);
        ++itSessions;
    }

    LogFlowFunc(("Sessions with \"%s\" = %RU32\n",
                 aSessionName.c_str(), listSessions.size()));

    aSessions.resize(listSessions.size());
    if (!listSessions.empty())
    {
        size_t i = 0;
        for (std::list < ComObjPtr<GuestSession> >::const_iterator it = listSessions.begin(); it != listSessions.end(); ++it, ++i)
            (*it).queryInterfaceTo(aSessions[i].asOutParam());

        return S_OK;

    }

    return setErrorNoLog(VBOX_E_OBJECT_NOT_FOUND,
                         tr("Could not find sessions with name '%s'"),
                         aSessionName.c_str());
#endif /* VBOX_WITH_GUEST_CONTROL */
}

HRESULT Guest::shutdown(const std::vector<GuestShutdownFlag_T> &aFlags)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_CONTROL */

    /* Validate flags. */
    uint32_t fFlags = GuestShutdownFlag_None;
    if (aFlags.size())
        for (size_t i = 0; i < aFlags.size(); ++i)
            fFlags |= aFlags[i];

    const uint32_t fValidFlags = GuestShutdownFlag_None
                               | GuestShutdownFlag_PowerOff | GuestShutdownFlag_Reboot | GuestShutdownFlag_Force;
    if (fFlags & ~fValidFlags)
        return setError(E_INVALIDARG,tr("Unknown flags: flags value %#x, invalid: %#x"), fFlags, fFlags & ~fValidFlags);

    if (   (fFlags & GuestShutdownFlag_PowerOff)
        && (fFlags & GuestShutdownFlag_Reboot))
        return setError(E_INVALIDARG, tr("Invalid combination of flags (%#x)"), fFlags);

    Utf8Str strAction = (fFlags & GuestShutdownFlag_Reboot) ? tr("Rebooting") : tr("Shutting down");

    /*
     * Create an anonymous session. This is required to run shutting down / rebooting
     * the guest with administrative rights.
     */
    GuestSessionStartupInfo startupInfo;
    startupInfo.mName = (fFlags & GuestShutdownFlag_Reboot) ? tr("Rebooting guest") : tr("Shutting down guest");

    GuestCredentials guestCreds;

    HRESULT hrc = S_OK;

    ComObjPtr<GuestSession> pSession;
    int vrc = i_sessionCreate(startupInfo, guestCreds, pSession);
    if (RT_SUCCESS(vrc))
    {
        Assert(!pSession.isNull());

        int vrcGuest = VERR_GSTCTL_GUEST_ERROR;
        vrc = pSession->i_startSession(&vrcGuest);
        if (RT_SUCCESS(vrc))
        {
            vrc = pSession->i_shutdown(fFlags, &vrcGuest);
            if (RT_FAILURE(vrc))
            {
                switch (vrc)
                {
                    case VERR_NOT_SUPPORTED:
                        hrc = setErrorBoth(VBOX_E_NOT_SUPPORTED, vrc,
                                           tr("%s not supported by installed Guest Additions"), strAction.c_str());
                        break;

                    default:
                    {
                        if (vrc == VERR_GSTCTL_GUEST_ERROR)
                            vrc = vrcGuest;
                        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Error %s guest: %Rrc"), strAction.c_str(), vrc);
                        break;
                    }
                }
            }
        }
        else
        {
            if (vrc == VERR_GSTCTL_GUEST_ERROR)
                vrc = vrcGuest;
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Could not open guest session: %Rrc"), vrc);
        }
    }
    else
    {
        switch (vrc)
        {
            case VERR_MAX_PROCS_REACHED:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Maximum number of concurrent guest sessions (%d) reached"),
                                  VBOX_GUESTCTRL_MAX_SESSIONS);
                break;

            /** @todo Add more errors here. */

           default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Could not create guest session: %Rrc"), vrc);
                break;
        }
    }

    LogFlowFunc(("Returning hrc=%Rhrc\n", hrc));
    return hrc;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

HRESULT Guest::updateGuestAdditions(const com::Utf8Str &aSource, const std::vector<com::Utf8Str> &aArguments,
                                    const std::vector<AdditionsUpdateFlag_T> &aFlags, ComPtr<IProgress> &aProgress)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_CONTROL */

    /* Validate flags. */
    uint32_t fFlags = AdditionsUpdateFlag_None;
    if (aFlags.size())
        for (size_t i = 0; i < aFlags.size(); ++i)
            fFlags |= aFlags[i];

    if (fFlags && !(fFlags & AdditionsUpdateFlag_WaitForUpdateStartOnly))
        return setError(E_INVALIDARG, tr("Unknown flags (%#x)"), fFlags);


    /* Copy arguments into aArgs: */
    ProcessArguments aArgs;
    try
    {
        aArgs.resize(0);
        for (size_t i = 0; i < aArguments.size(); ++i)
            aArgs.push_back(aArguments[i]);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }


    /*
     * Create an anonymous session. This is required to run the Guest Additions
     * update process with administrative rights.
     */
    GuestSessionStartupInfo startupInfo;
    startupInfo.mName = "Updating Guest Additions";

    GuestCredentials guestCreds;

    HRESULT hrc;
    ComObjPtr<GuestSession> pSession;
    int vrc = i_sessionCreate(startupInfo, guestCreds, pSession);
    if (RT_SUCCESS(vrc))
    {
        Assert(!pSession.isNull());

        int vrcGuest = VERR_GSTCTL_GUEST_ERROR;
        vrc = pSession->i_startSession(&vrcGuest);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Create the update task.
             */
            GuestSessionTaskUpdateAdditions *pTask = NULL;
            try
            {
                pTask = new GuestSessionTaskUpdateAdditions(pSession /* GuestSession */, aSource, aArgs, fFlags);
                hrc = S_OK;
            }
            catch (std::bad_alloc &)
            {
                hrc = setError(E_OUTOFMEMORY, tr("Failed to create SessionTaskUpdateAdditions object"));
            }
            if (SUCCEEDED(hrc))
            {
                try
                {
                    hrc = pTask->Init(Utf8StrFmt(tr("Updating Guest Additions")));
                }
                catch (std::bad_alloc &)
                {
                    hrc = E_OUTOFMEMORY;
                }
                if (SUCCEEDED(hrc))
                {
                    ComPtr<Progress> ptrProgress = pTask->GetProgressObject();

                    /*
                     * Kick off the thread.  Note! consumes pTask!
                     */
                    hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_HEAVY_WORKER);
                    pTask = NULL;
                    if (SUCCEEDED(hrc))
                        hrc = ptrProgress.queryInterfaceTo(aProgress.asOutParam());
                    else
                        hrc = setError(hrc, tr("Starting thread for updating Guest Additions on the guest failed"));
                }
                else
                {
                    hrc = setError(hrc, tr("Failed to initialize SessionTaskUpdateAdditions object"));
                    delete pTask;
                }
            }
        }
        else
        {
            if (vrc == VERR_GSTCTL_GUEST_ERROR)
                vrc = vrcGuest;
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Could not open guest session: %Rrc"), vrc);
        }
    }
    else
    {
        switch (vrc)
        {
            case VERR_MAX_PROCS_REACHED:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Maximum number of concurrent guest sessions (%d) reached"),
                                  VBOX_GUESTCTRL_MAX_SESSIONS);
                break;

            /** @todo Add more errors here. */

           default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Could not create guest session: %Rrc"), vrc);
                break;
        }
    }

    LogFlowFunc(("Returning hrc=%Rhrc\n", hrc));
    return hrc;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

