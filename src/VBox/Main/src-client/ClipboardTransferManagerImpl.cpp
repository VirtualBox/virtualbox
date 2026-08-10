/* $Id: ClipboardTransferManagerImpl.cpp 114977 2026-08-10 19:27:36Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer manager object.
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include "LoggingNew.h"

#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "ClipboardImpl.h"
#include "ClipboardTransferImpl.h"
#include "ClipboardTransferManagerImpl.h"
#include "GuestShClHelpers.h"
#include "ProgressImpl.h"
#include "VirtualBoxErrorInfoImpl.h"
#include "VBoxEvents.h"

#include <VBox/com/ErrorInfo.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/log.h>

#include <iprt/errcore.h>
#include <iprt/string.h>

#include <new>


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(ClipboardTransferManager)


/**
 * Completes construction of a clipboard transfer manager object.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransferManager::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard transfer manager object.
 */
void ClipboardTransferManager::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts a transfer status to the corresponding public Main transfer state.
 *
 * @returns Public Main transfer state.
 * @param   enmStatus       Transfer status to convert.
 */
static ClipboardTransferState_T clipboardTransferManagerStatusToState(SHCLTRANSFERSTATUS enmStatus)
{
    switch (enmStatus)
    {
        case SHCLTRANSFERSTATUS_REQUESTED:
        case SHCLTRANSFERSTATUS_INITIALIZED:
            return ClipboardTransferState_Added;
        case SHCLTRANSFERSTATUS_STARTED:
            return ClipboardTransferState_InProgress;
        case SHCLTRANSFERSTATUS_COMPLETED:
            return ClipboardTransferState_Completed;
        case SHCLTRANSFERSTATUS_CANCELED:
            return ClipboardTransferState_Canceled;
        case SHCLTRANSFERSTATUS_UNINITIALIZED:
            return ClipboardTransferState_Removed;
        case SHCLTRANSFERSTATUS_KILLED:
        case SHCLTRANSFERSTATUS_ERROR:
            return ClipboardTransferState_Failed;
        default:
            return ClipboardTransferState_Added;
    }
}


/**
 * Converts a failed transfer status to the public Main clipboard error value.
 *
 * @returns Public Main clipboard error value.
 * @param   enmStatus       Transfer status to convert.
 * @param   vrcTransfer     Transfer result code.
 */
static ClipboardError_T clipboardTransferManagerStatusToError(SHCLTRANSFERSTATUS enmStatus, int vrcTransfer)
{
    if (enmStatus == SHCLTRANSFERSTATUS_ERROR || enmStatus == SHCLTRANSFERSTATUS_KILLED)
    {
        if (vrcTransfer == VERR_ACCESS_DENIED)
            return ClipboardError_AccessDenied;
        if (vrcTransfer == VERR_NOT_SUPPORTED)
            return ClipboardError_NotSupported;
        return ClipboardError_OperationFailed;
    }
    return ClipboardError_None;
}


/**
 * Converts a transfer status to a progress completion HRESULT.
 *
 * @returns Progress completion HRESULT.
 * @param   enmStatus       Transfer status to convert.
 * @param   vrcTransfer     Transfer result code.
 */
static HRESULT clipboardTransferManagerStatusToProgressHrc(SHCLTRANSFERSTATUS enmStatus, int vrcTransfer)
{
    switch (enmStatus)
    {
        case SHCLTRANSFERSTATUS_COMPLETED:
            return S_OK;
        case SHCLTRANSFERSTATUS_CANCELED:
        case SHCLTRANSFERSTATUS_UNINITIALIZED:
            return E_ABORT;
        case SHCLTRANSFERSTATUS_KILLED:
        case SHCLTRANSFERSTATUS_ERROR:
            if (vrcTransfer == VERR_ACCESS_DENIED)
                return VBOX_E_SHCL_ACCESS_DENIED;
            return VBOX_E_SHCL_ERROR;
        default:
            return VBOX_E_SHCL_ERROR;
    }
}


/**
 * Completes a transfer progress object from a terminal service status.
 *
 * @param   ptrProgressControl  Internal progress control to complete. Optional.
 * @param   enmStatus           Terminal service transfer status.
 * @param   vrcTransfer         Service transfer result code.
 */
static void clipboardTransferManagerCompleteProgress(const ComPtr<IInternalProgressControl> &ptrProgressControl,
                                                     SHCLTRANSFERSTATUS enmStatus, int vrcTransfer)
{
    if (ptrProgressControl.isNull())
        return;

    HRESULT const hrcProgress = clipboardTransferManagerStatusToProgressHrc(enmStatus, vrcTransfer);
    ComPtr<IVirtualBoxErrorInfo> ptrErrorInfo;
    if (FAILED(hrcProgress))
    {
        ComObjPtr<VirtualBoxErrorInfo> ptrErrorInfoImpl;
        HRESULT hrc = ptrErrorInfoImpl.createObject();
        if (SUCCEEDED(hrc))
        {
            const char *pszText;
            if (enmStatus == SHCLTRANSFERSTATUS_CANCELED)
                pszText = ClipboardTransferManager::tr("Shared Clipboard transfer was canceled");
            else if (enmStatus == SHCLTRANSFERSTATUS_UNINITIALIZED)
                pszText = ClipboardTransferManager::tr("Shared Clipboard transfer was removed before completion");
            else
                pszText = ClipboardTransferManager::tr("Shared Clipboard transfer failed");
            try
            {
                hrc = ptrErrorInfoImpl->initEx(hrcProgress, (LONG)vrcTransfer, COM_IIDOF(IClipboardTransferManager),
                                              "ClipboardTransferManager", com::Utf8Str(pszText));
            }
            catch (std::bad_alloc &)
            {
                hrc = E_OUTOFMEMORY;
            }
            if (SUCCEEDED(hrc))
                ptrErrorInfo = ptrErrorInfoImpl;
        }
        if (FAILED(hrc))
            AssertComRC(hrc);
    }

    HRESULT hrc = ptrProgressControl->NotifyComplete((LONG)hrcProgress, ptrErrorInfo);
    AssertComRC(hrc);
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Initializes a clipboard transfer manager object.
 *
 * @returns COM status code.
 * @param   aEventSource    Optional event source used to emit anonymous transfer
 *                          events when no parent clipboard object is available.
 * @param   aParent         Optional parent clipboard object used to fire transfer
 *                          events with clipboard revision/session context.
 */
HRESULT ClipboardTransferManager::init(IEventSource *aEventSource /* = NULL */, Clipboard *aParent /* = NULL */)
{
    Log2Func(("aEventSource=%p, aParent=%p\n", aEventSource, aParent));
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData.mParent = aParent;
    mData.mEventSource = aEventSource;
    mData.mTransfers.clear();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    mData.mNextTransferId = 1;
#endif

    autoInitSpan.setSucceeded();
    return S_OK;
}


/**
 * Uninitializes a clipboard transfer manager object.
 */
void ClipboardTransferManager::uninit()
{
    Log3Func(("cTransfers=%zu\n", mData.mTransfers.size()));
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    std::vector<Data::TransferRecord> DetachedTransfers;
    ComPtr<IEventSource> ptrEventSource;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        DetachedTransfers.swap(mData.mTransfers);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        mData.mNextTransferId = 1;
#endif
        ptrEventSource = mData.mEventSource;
        mData.mEventSource.setNull();
        mData.mParent = NULL;
    }
    RT_NOREF(ptrEventSource);
}


/**
 * Returns the current clipboard transfers.
 *
 * @returns COM status code.
 * @param   aDirection      Transfer direction filter.
 * @param   aFlags          Reserved flags, must be zero.
 * @param   aTransfers      Where to return the transfer list.
 */
HRESULT ClipboardTransferManager::getTransfers(ClipboardTransferDirection_T aDirection,
                                               ULONG aFlags,
                                               std::vector<ComPtr<IClipboardTransfer> > &aTransfers)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aDirection, aFlags, aTransfers);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    if (aFlags != 0)
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer manager flags %RU32"), aFlags);
    if (   aDirection != ClipboardTransferDirection_Any
        && aDirection != ClipboardTransferDirection_ToGuest
        && aDirection != ClipboardTransferDirection_ToHost)
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer direction %RU32"), (uint32_t)aDirection);

    std::vector<ComPtr<IClipboardTransfer> > Transfers;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        try
        {
            Transfers.reserve(mData.mTransfers.size());
            for (std::vector<Data::TransferRecord>::const_iterator it = mData.mTransfers.begin();
                 it != mData.mTransfers.end(); ++it)
            {
                if (aDirection != ClipboardTransferDirection_Any)
                {
                    ClipboardTransferDirection_T const enmDirection
                        = it->mDirection == SHCLTRANSFERDIR_FROM_REMOTE
                        ? ClipboardTransferDirection_ToHost : ClipboardTransferDirection_ToGuest;
                    if (enmDirection != aDirection)
                        continue;
                }
                ComPtr<IClipboardTransfer> ptrTransfer;
                HRESULT const hrc = it->mTransfer.queryInterfaceTo(ptrTransfer.asOutParam());
                AssertComRCReturn(hrc, hrc);
                Transfers.push_back(ptrTransfer);
            }
        }
        catch (std::bad_alloc &)
        {
            return setError(E_OUTOFMEMORY, tr("Allocating the clipboard transfer result list failed"));
        }
    }
    aTransfers.swap(Transfers);
    Log3Func(("cTransfers=%zu\n", aTransfers.size()));
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


HRESULT ClipboardTransferManager::create(ClipboardTransferDirection_T aDirection,
                                         ClipboardSource_T aSource,
                                         ClipboardAction_T aAction,
                                         ComPtr<IClipboardTransfer> &aTransfer)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aDirection, aSource, aAction, aTransfer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    aTransfer.setNull();
    Log2Func(("aDirection=%RU32, aSource=%RU32, aAction=%RU32\n",
              (uint32_t)aDirection, (uint32_t)aSource, (uint32_t)aAction));
    if (   aDirection != ClipboardTransferDirection_ToGuest
        && aDirection != ClipboardTransferDirection_ToHost)
    {
        LogFunc(("Rejecting invalid clipboard transfer direction %RU32\n", (uint32_t)aDirection));
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer direction %RU32"), (uint32_t)aDirection);
    }
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid clipboard transfer source %RU32\n", (uint32_t)aSource));
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer source %RU32"), (uint32_t)aSource);
    }
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid clipboard transfer action %RU32\n", (uint32_t)aAction));
        return setError(E_INVALIDARG, tr("Invalid clipboard transfer action %RU32"), (uint32_t)aAction);
    }

    ULONG idTransfer;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        idTransfer = mData.mNextTransferId++;
        if (mData.mNextTransferId == 0)
            mData.mNextTransferId = 1;
    }

    ComObjPtr<ClipboardTransfer> ptrTransferObj;
    HRESULT hrc = ptrTransferObj.createObject();
    if (FAILED(hrc))
        return setError(hrc, tr("Creating clipboard transfer object failed"));

    ComPtr<IClipboardItem> ptrItem;
    ComPtr<IProgress> ptrProgress;
    hrc = ptrTransferObj->init(idTransfer, aDirection, aSource, aAction, ptrItem, ptrProgress);
    if (FAILED(hrc))
        return setError(hrc, tr("Initializing clipboard transfer object failed"));

    ComPtr<IClipboardTransfer> ptrTransfer;
    hrc = ptrTransferObj.queryInterfaceTo(ptrTransfer.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer interface failed"));

    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Data::TransferRecord Record;
        Record.mTransferId = idTransfer;
        Record.mDirection = aDirection == ClipboardTransferDirection_ToHost
                          ? SHCLTRANSFERDIR_FROM_REMOTE : SHCLTRANSFERDIR_TO_REMOTE;
        Record.mSource = aSource == ClipboardSource_Host
                       ? SHCLSOURCE_LOCAL
                       : aSource == ClipboardSource_Guest ? SHCLSOURCE_REMOTE : SHCLSOURCE_INVALID;
        Record.mTransfer = ptrTransferObj;
        try
        {
            mData.mTransfers.push_back(Record);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    aTransfer = ptrTransfer;
    Log2Func(("Firing transfer added event: transfer=%p\n", (void *)(ClipboardTransfer *)ptrTransferObj));
    i_fireTransferEvent(ptrTransferObj, ClipboardTransferState_Added, ClipboardTransferInteraction_None,
                        com::Utf8Str(), com::Utf8Str(), ClipboardError_None);
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Removes a clipboard transfer.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer to remove.
 */
HRESULT ClipboardTransferManager::remove(const ComPtr<IClipboardTransfer> &aTransfer)
{
    Log2Func(("aTransfer=%p\n", (void *)aTransfer));
    if (aTransfer.isNull())
    {
        LogFunc(("Rejecting NULL transfer remove\n"));
        return setError(E_INVALIDARG, tr("Clipboard transfer to remove must not be NULL"));
    }

#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aTransfer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    ComObjPtr<ClipboardTransfer> ptrTransfer;
    bool fRemoved = false;
    bool fFireEvent = false;
    bool fServiceTransfer = false;
    ComPtr<IInternalProgressControl> ptrProgressControl;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Data::TransferRecords::iterator it = mData.findTransferRecord(aTransfer);
        if (it != mData.mTransfers.end())
        {
            ptrTransfer = it->mTransfer;
            if (ShClTransferKeyIsValid(it->mServiceSessionId, it->mTransferId, it->mGeneration))
                fServiceTransfer = true;
            else
            {
                ptrProgressControl = it->mProgressControl;
                mData.mTransfers.erase(it);
                Log2Func(("Removed transfer: cTransfers=%zu\n", mData.mTransfers.size()));
                fFireEvent = mData.mParent != NULL || mData.mEventSource.isNotNull();
                fRemoved = true;
            }
        }
    }

    if (fServiceTransfer)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot remove an active service transfer; cancel it first"));
    if (!fRemoved)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is no longer owned by this manager"));

    ptrTransfer->i_setState(ClipboardTransferState_Removed, com::Utf8Str(), ClipboardError_None);
    clipboardTransferManagerCompleteProgress(ptrProgressControl, SHCLTRANSFERSTATUS_UNINITIALIZED, VERR_CANCELLED);
    if (fFireEvent)
    {
        Log2Func(("Firing transfer removed event: transfer=%p\n", (void *)(ClipboardTransfer *)ptrTransfer));
        i_fireTransferEvent(ptrTransfer, ClipboardTransferState_Removed, ClipboardTransferInteraction_None,
                            com::Utf8Str(), com::Utf8Str(), ClipboardError_None);
    }
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Cancels a clipboard transfer.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer to cancel.
 */
HRESULT ClipboardTransferManager::cancel(const ComPtr<IClipboardTransfer> &aTransfer)
{
    Log2Func(("aTransfer=%p\n", (void *)aTransfer));
    if (aTransfer.isNull())
    {
        LogFunc(("Rejecting NULL transfer cancel\n"));
        return setError(E_INVALIDARG, tr("Clipboard transfer to cancel must not be NULL"));
    }

#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aTransfer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    ComObjPtr<ClipboardTransfer> ptrTransfer;
    bool fCanceled = false;
    bool fFireEvent = false;
    bool fNeedsHostCancel = false;
    bool fCancelAlreadyRequested = false;
    SHCLSESSIONID idSession = NIL_SHCLSESSIONID;
    ULONG idTransfer = 0;
    SHCLTRANSFERGEN uGeneration = NIL_SHCLTRANSFERGEN;
    ComPtr<IInternalProgressControl> ptrProgressControl;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Data::TransferRecords::iterator it = mData.findTransferRecord(aTransfer);
        if (it != mData.mTransfers.end())
        {
            ptrTransfer = it->mTransfer;
            idSession = it->mServiceSessionId;
            idTransfer = it->mTransferId;
            uGeneration = it->mGeneration;
            fNeedsHostCancel = ShClTransferKeyIsValid(idSession, idTransfer, uGeneration);
            if (!fNeedsHostCancel)
            {
                ptrProgressControl = it->mProgressControl;
                mData.mTransfers.erase(it);
                Log2Func(("Canceled transfer: cTransfers=%zu\n", mData.mTransfers.size()));
                fFireEvent = mData.mParent != NULL || mData.mEventSource.isNotNull();
                fCanceled = true;
            }
            else
            {
                if (it->mfCancelRequested)
                    fCancelAlreadyRequested = true;
                else
                    it->mfCancelRequested = true;
            }
        }
    }

    if (fCancelAlreadyRequested)
        return setError(VBOX_E_OBJECT_IN_USE, tr("Clipboard transfer cancellation is already in progress"));
    if (!fCanceled && !fNeedsHostCancel)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is no longer owned by this manager"));

    ClipboardTransfer *pTransfer = ptrTransfer;
    if (fNeedsHostCancel)
    {
        Clipboard *pParent = NULL;
        AutoCaller autoCaller;
        {
            AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
            pParent = mData.mParent;
            if (pParent)
                autoCaller.attach(pParent);
        }
        if (!pParent)
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            Data::TransferRecords::iterator it = mData.findTransferRecord(pTransfer, idSession,
                                                                          idTransfer, uGeneration);
            if (it != mData.mTransfers.end())
                it->mfCancelRequested = false;
            return setError(E_FAIL, tr("Clipboard transfer cannot be canceled because no clipboard backend is available"));
        }
        if (FAILED(autoCaller.hrc()))
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            Data::TransferRecords::iterator it = mData.findTransferRecord(pTransfer, idSession,
                                                                          idTransfer, uGeneration);
            if (it != mData.mTransfers.end())
                it->mfCancelRequested = false;
            return setError(autoCaller.hrc(), tr("Clipboard backend is not ready for canceling clipboard transfers"));
        }

        HRESULT const hrc = pParent->i_transferCancel(idSession, (SHCLTRANSFERID)idTransfer, uGeneration);
        if (FAILED(hrc))
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            Data::TransferRecords::iterator it = mData.findTransferRecord(pTransfer, idSession,
                                                                          idTransfer, uGeneration);
            if (it != mData.mTransfers.end())
                it->mfCancelRequested = false;
            return setError(hrc, tr("Canceling clipboard transfer through the backend failed"));
        }

        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            Data::TransferRecords::iterator it = mData.findTransferRecord(pTransfer, idSession,
                                                                          idTransfer, uGeneration);
            if (it != mData.mTransfers.end())
            {
                ptrProgressControl = it->mProgressControl;
                mData.mTransfers.erase(it);
                Log2Func(("Canceled transfer after host request: cTransfers=%zu\n", mData.mTransfers.size()));
                fFireEvent = mData.mParent != NULL || mData.mEventSource.isNotNull();
                fCanceled = true;
            }
        }
    }

    if (fCanceled)
    {
        ptrTransfer->i_setState(ClipboardTransferState_Canceled, com::Utf8Str(), ClipboardError_None);
        clipboardTransferManagerCompleteProgress(ptrProgressControl, SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
    }
    if (fCanceled && fFireEvent)
    {
        Log2Func(("Firing transfer canceled event: transfer=%p\n", (void *)pTransfer));
        i_fireTransferEvent(ptrTransfer, ClipboardTransferState_Canceled, ClipboardTransferInteraction_None,
                            com::Utf8Str(), com::Utf8Str(), ClipboardError_None);
    }
    else if (!fCanceled)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is no longer owned by this manager"));
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Approves a transfer waiting for client approval.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer to approve.
 * @param   aFlags          Reserved approval flags.
 */
HRESULT ClipboardTransferManager::approve(const ComPtr<IClipboardTransfer> &aTransfer, ULONG aFlags)
{
    if (aTransfer.isNull())
        return setError(E_INVALIDARG, tr("Clipboard transfer to approve must not be NULL"));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    bool fOwned;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        fOwned = mData.findTransferRecord(aTransfer) != mData.mTransfers.end();
    }
    if (!fOwned)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is not owned by this manager"));
#endif
    RT_NOREF(aFlags);
    ReturnComNotImplemented();
}


/**
 * Denies a transfer waiting for client approval.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer to deny.
 * @param   aReason         Optional denial reason.
 */
HRESULT ClipboardTransferManager::deny(const ComPtr<IClipboardTransfer> &aTransfer, const com::Utf8Str &aReason)
{
    if (aTransfer.isNull())
        return setError(E_INVALIDARG, tr("Clipboard transfer to deny must not be NULL"));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    bool fOwned;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        fOwned = mData.findTransferRecord(aTransfer) != mData.mTransfers.end();
    }
    if (!fOwned)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is not owned by this manager"));
#endif
    RT_NOREF(aReason);
    ReturnComNotImplemented();
}


/**
 * Supplies a response to a transfer interaction request.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer waiting for interaction.
 * @param   aInteraction    Interaction being answered.
 * @param   aPath           Optional transfer-relative interaction path.
 * @param   aResponse       Client response.
 * @param   aResponsePath   Optional transfer-relative path supplied by the response.
 * @param   aFlags          Reserved response flags.
 */
HRESULT ClipboardTransferManager::respond(const ComPtr<IClipboardTransfer> &aTransfer,
                                          ClipboardTransferInteraction_T aInteraction,
                                          const com::Utf8Str &aPath,
                                          ClipboardTransferResponse_T aResponse,
                                          const com::Utf8Str &aResponsePath,
                                          ULONG aFlags)
{
    if (aTransfer.isNull())
        return setError(E_INVALIDARG, tr("Clipboard transfer awaiting a response must not be NULL"));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    bool fOwned;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        fOwned = mData.findTransferRecord(aTransfer) != mData.mTransfers.end();
    }
    if (!fOwned)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is not owned by this manager"));
#endif
    RT_NOREF(aInteraction, aPath, aResponse, aResponsePath, aFlags);
    ReturnComNotImplemented();
}


/**
 * Pauses a running transfer when supported.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer to pause.
 */
HRESULT ClipboardTransferManager::pause(const ComPtr<IClipboardTransfer> &aTransfer)
{
    if (aTransfer.isNull())
        return setError(E_INVALIDARG, tr("Clipboard transfer to pause must not be NULL"));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    bool fOwned;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        fOwned = mData.findTransferRecord(aTransfer) != mData.mTransfers.end();
    }
    if (!fOwned)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is not owned by this manager"));
#endif
    ReturnComNotImplemented();
}


/**
 * Resumes a paused transfer when supported.
 *
 * @returns COM status code.
 * @param   aTransfer       Transfer to resume.
 */
HRESULT ClipboardTransferManager::resume(const ComPtr<IClipboardTransfer> &aTransfer)
{
    if (aTransfer.isNull())
        return setError(E_INVALIDARG, tr("Clipboard transfer to resume must not be NULL"));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    bool fOwned;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        fOwned = mData.findTransferRecord(aTransfer) != mData.mTransfers.end();
    }
    if (!fOwned)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is not owned by this manager"));
#endif
    ReturnComNotImplemented();
}


/**
 * Resets all clipboard transfers.
 *
 * @returns COM status code.
 */
HRESULT ClipboardTransferManager::reset()
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    LogFunc(("Resetting transfer manager via public API\n"));
    ComPtr<IEventSource> ptrEventSource;
    std::vector<Data::TransferRecord> DetachedTransfers;
    bool fHasServiceTransfers = false;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        for (std::vector<Data::TransferRecord>::const_iterator it = mData.mTransfers.begin();
             it != mData.mTransfers.end(); ++it)
            if (ShClTransferKeyIsValid(it->mServiceSessionId, it->mTransferId, it->mGeneration))
            {
                fHasServiceTransfers = true;
                break;
            }

        if (!fHasServiceTransfers)
        {
            ptrEventSource = mData.mEventSource;
            DetachedTransfers.swap(mData.mTransfers);
            Log2Func(("Detached %zu transfers during public reset\n", DetachedTransfers.size()));
        }
    }

    if (fHasServiceTransfers)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot reset clipboard transfers while a service transfer is active; cancel it first"));

    RT_NOREF(ptrEventSource);
    for (std::vector<Data::TransferRecord>::const_iterator it = DetachedTransfers.begin();
         it != DetachedTransfers.end(); ++it)
    {
        it->mTransfer->i_setState(ClipboardTransferState_Removed, com::Utf8Str(), ClipboardError_None);
        clipboardTransferManagerCompleteProgress(it->mProgressControl,
                                                 SHCLTRANSFERSTATUS_UNINITIALIZED, VERR_CANCELLED);
        Log2Func(("Firing transfer removed event during public reset: transfer=%p\n",
                  (void *)(ClipboardTransfer *)it->mTransfer));
        i_fireTransferEvent(it->mTransfer, ClipboardTransferState_Removed, ClipboardTransferInteraction_None,
                            com::Utf8Str(), com::Utf8Str(), ClipboardError_None);
    }
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Resets the internally tracked transfer list.
 */
void ClipboardTransferManager::i_reset()
{
    LogFunc(("Resetting transfer manager\n"));
    ComPtr<IEventSource> ptrEventSource;
    std::vector<Data::TransferRecord> DetachedTransfers;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        ptrEventSource = mData.mEventSource;
        DetachedTransfers.swap(mData.mTransfers);
        Log2Func(("Detached %zu transfers during reset\n", DetachedTransfers.size()));
    }

    RT_NOREF(ptrEventSource);
    for (std::vector<Data::TransferRecord>::const_iterator it = DetachedTransfers.begin();
         it != DetachedTransfers.end(); ++it)
    {
        it->mTransfer->i_setState(ClipboardTransferState_Removed, com::Utf8Str(), ClipboardError_None);
        clipboardTransferManagerCompleteProgress(it->mProgressControl,
                                                 SHCLTRANSFERSTATUS_UNINITIALIZED, VERR_CANCELLED);
    }
    for (std::vector<Data::TransferRecord>::const_iterator it = DetachedTransfers.begin();
         it != DetachedTransfers.end(); ++it)
    {
        Log2Func(("Firing transfer removed event during reset: transfer=%p\n",
                  (void *)(ClipboardTransfer *)it->mTransfer));
        i_fireTransferEvent(it->mTransfer, ClipboardTransferState_Removed, ClipboardTransferInteraction_None,
                            com::Utf8Str(), com::Utf8Str(), ClipboardError_None);
    }
}


/**
 * Fires a clipboard transfer event through the parent clipboard object when available.
 * If there is no parent clipboard object, emits an anonymous event directly on
 * the stored event source.
 *
 * @param   aTransfer       Transfer associated with the event.
 * @param   aState          Transfer state.
 * @param   aInteraction    Transfer interaction type.
 * @param   aPath           Transfer-relative path associated with the event, if any.
 * @param   aMessage        Optional event message.
 * @param   aError          Clipboard transfer error code.
 */
void ClipboardTransferManager::i_fireTransferEvent(const ComObjPtr<ClipboardTransfer> &aTransfer,
                                                   ClipboardTransferState_T aState,
                                                   ClipboardTransferInteraction_T aInteraction,
                                                   const com::Utf8Str &aPath,
                                                   const com::Utf8Str &aMessage,
                                                   ClipboardError_T aError)
{
    ComPtr<IClipboardTransfer> ptrTransfer;
    HRESULT const hrc = aTransfer.queryInterfaceTo(ptrTransfer.asOutParam());
    if (FAILED(hrc))
    {
        AssertComRC(hrc);
        return;
    }

    Clipboard *pParent = NULL;
    ComPtr<IEventSource> ptrEventSource;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
        else
            ptrEventSource = mData.mEventSource;
    }

    if (pParent)
    {
        if (SUCCEEDED(autoCaller.hrc()))
            pParent->i_fireClipboardTransferEvent(VBOX_SHCL_MAIN_CLIENT_NONE, ptrTransfer, aState, aInteraction,
                                                  aPath, aMessage, aError);
        else
            LogFunc(("Cannot fire clipboard transfer event through parent: hrc=%#x\n", autoCaller.hrc()));
        return;
    }

    if (ptrEventSource.isNotNull())
    {
        /*
         * No parent Clipboard is available to supply a live revision or session
         * fan-out context. Keep this legacy event anonymous.
         */
        ::FireClipboardTransferEvent(ptrEventSource, 0 /* anonymous revision */, VBOX_SHCL_MAIN_CLIENT_NONE,
                                     ptrTransfer, aState, aInteraction, Bstr(aPath).raw(), aMessage, aError);
    }
}


/**
 * Handles a Shared Clipboard transfer status from the host service.
 *
 * @returns COM status code.
 * @param   aServiceSessionId   Service session that owns the transfer.
 * @param   aTransferId         Shared Clipboard transfer ID.
 * @param   aGeneration         Host-private transfer generation.
 * @param   aTransfer           Borrowed service transfer used to validate status metadata.
 * @param   enmShClSource       Data source recorded by the backing transfer.
 * @param   enmStatus           Transfer lifecycle status.
 * @param   vrcTransfer         Transfer result code associated with the status.
 */
HRESULT ClipboardTransferManager::i_handleTransferStatus(SHCLSESSIONID aServiceSessionId,
                                                         SHCLTRANSFERID aTransferId,
                                                         SHCLTRANSFERGEN aGeneration,
                                                         PSHCLTRANSFER aTransfer,
                                                         SHCLSOURCE enmShClSource,
                                                         SHCLTRANSFERSTATUS enmStatus,
                                                         int vrcTransfer)
{
    SHCLSESSIONID const idSession = aServiceSessionId;
    SHCLTRANSFERID const idTransfer = aTransferId;
    SHCLTRANSFERGEN const uGeneration = aGeneration;
    if (!ShClTransferKeyIsValid(idSession, idTransfer, uGeneration))
        return E_INVALIDARG;
    if (   enmShClSource != SHCLSOURCE_LOCAL
        && enmShClSource != SHCLSOURCE_REMOTE)
        return E_INVALIDARG;
    if (!ShClTransferStatusIsValid(enmStatus))
        return E_INVALIDARG;
    if (!ShClTransferStatusResultIsValid(enmStatus, vrcTransfer))
        return E_INVALIDARG;

    SHCLTRANSFERDIR enmTransferDirection;
    SHCLSOURCE      enmTransferSource;
    if (aTransfer)
    {
        if (ShClTransferGetSource(aTransfer) != enmShClSource)
            return E_INVALIDARG;
        enmTransferDirection = ShClTransferGetDir(aTransfer);
        enmTransferSource    = ShClTransferGetSource(aTransfer);
    }
    else
    {
        enmTransferDirection = enmShClSource == SHCLSOURCE_REMOTE
                             ? SHCLTRANSFERDIR_FROM_REMOTE : SHCLTRANSFERDIR_TO_REMOTE;
        enmTransferSource    = enmShClSource;
    }
    if (   (   enmTransferDirection != SHCLTRANSFERDIR_FROM_REMOTE
            && enmTransferDirection != SHCLTRANSFERDIR_TO_REMOTE)
        || (   enmTransferSource != SHCLSOURCE_LOCAL
            && enmTransferSource != SHCLSOURCE_REMOTE)
        || (   enmTransferSource == SHCLSOURCE_LOCAL
            && enmTransferDirection != SHCLTRANSFERDIR_TO_REMOTE)
        || (   enmTransferSource == SHCLSOURCE_REMOTE
            && enmTransferDirection != SHCLTRANSFERDIR_FROM_REMOTE))
        return E_INVALIDARG;
    if (enmStatus == SHCLTRANSFERSTATUS_NONE)
        return S_OK;

    ClipboardTransferState_T const enmState = clipboardTransferManagerStatusToState(enmStatus);
    ClipboardError_T const enmError = clipboardTransferManagerStatusToError(enmStatus, vrcTransfer);
    bool const fTerminal = ShClTransferStatusIsTerminal(enmStatus);

    ComObjPtr<ClipboardTransfer> ptrTransfer;
    ComPtr<IProgress> ptrProgress;
    ComPtr<IInternalProgressControl> ptrProgressControl;
    bool fFireAdded = false;
    bool fFireState = false;

    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        size_t idxRecord = mData.mTransfers.size();
        for (size_t i = 0; i < mData.mTransfers.size(); ++i)
            if (mData.mTransfers[i].matches(idSession, idTransfer, uGeneration))
            {
                idxRecord = i;
                break;
            }

        if (idxRecord == mData.mTransfers.size())
        {
            if (fTerminal)
            {
                Log2Func(("Ignoring terminal status for unknown transfer: session=%RU16, id=%RU16, generation=%RU64, status=%RU32\n",
                          idSession, idTransfer, uGeneration, (uint32_t)enmStatus));
                return S_OK;
            }
            if (   enmStatus != SHCLTRANSFERSTATUS_REQUESTED
                && enmStatus != SHCLTRANSFERSTATUS_INITIALIZED)
                return E_INVALIDARG;

            ComObjPtr<Progress> ptrNewProgress;
            HRESULT hrc = ptrNewProgress.createObject();
            if (FAILED(hrc))
                return hrc;
            ComPtr<IEventSource> ptrProgressInitiator = mData.mEventSource;
            if (ptrProgressInitiator.isNull())
                return E_FAIL;
            hrc = ptrNewProgress->init(ptrProgressInitiator,
                                       com::Utf8Str("Shared Clipboard transfer"), FALSE /* aCancelable */);
            if (FAILED(hrc))
                return hrc;

            ComPtr<IProgress> ptrIProgress;
            hrc = ptrNewProgress.queryInterfaceTo(ptrIProgress.asOutParam());
            if (FAILED(hrc))
                return hrc;

            ComPtr<IInternalProgressControl> ptrIProgressControl(ptrIProgress);
            if (ptrIProgressControl.isNull())
                return E_FAIL;

            ComObjPtr<ClipboardTransfer> ptrNewTransfer;
            hrc = ptrNewTransfer.createObject();
            if (FAILED(hrc))
                return hrc;

            ClipboardTransferDirection_T const enmDirection = enmTransferDirection == SHCLTRANSFERDIR_FROM_REMOTE
                                                            ? ClipboardTransferDirection_ToHost
                                                            : ClipboardTransferDirection_ToGuest;
            ClipboardSource_T const enmSource = enmTransferSource == SHCLSOURCE_REMOTE
                                              ? ClipboardSource_Guest
                                              : ClipboardSource_Host;
            ComPtr<IClipboardItem> ptrItem;
            hrc = ptrNewTransfer->init(idTransfer, enmDirection, enmSource, ClipboardAction_Copy, ptrItem, ptrIProgress,
                                       NULL /* aTransfer */, false /* fOwnTransfer */);
            if (FAILED(hrc))
                return hrc;

            Data::TransferRecord Record;
            Record.mServiceSessionId = idSession;
            Record.mTransferId = idTransfer;
            Record.mGeneration = uGeneration;
            Record.mDirection = enmTransferDirection;
            Record.mSource = enmTransferSource;
            Record.mStatus = enmStatus;
            Record.mState = ClipboardTransferState_Added;
            Record.mTransfer = ptrNewTransfer;
            Record.mProgress = ptrIProgress;
            Record.mProgressControl = ptrIProgressControl;
            try
            {
                mData.mTransfers.push_back(Record);
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
            idxRecord = mData.mTransfers.size() - 1;
            fFireAdded = true;
        }

        Data::TransferRecord &Record = mData.mTransfers[idxRecord];
        if (   Record.mDirection != enmTransferDirection
            || Record.mSource != enmTransferSource
            || !ShClTransferStatusTransitionIsValid(Record.mStatus, enmStatus))
            return E_INVALIDARG;

        ptrTransfer = Record.mTransfer;
        ptrProgress = Record.mProgress;
        ptrProgressControl = Record.mProgressControl;
        if (fTerminal)
        {
            Record.mStatus = enmStatus;
            Record.mState = enmState;
            Record.mfTerminal = true;
            fFireState = true;
            mData.mTransfers.erase(mData.mTransfers.begin() + idxRecord);
        }
        else if (enmState != ClipboardTransferState_Added && Record.mState != enmState)
        {
            Record.mStatus = enmStatus;
            Record.mState = enmState;
            fFireState = true;
        }
        else
            Record.mStatus = enmStatus;
    }

    RT_NOREF(ptrProgress);

    if (fFireAdded)
    {
        ptrTransfer->i_setState(ClipboardTransferState_Added, com::Utf8Str(), ClipboardError_None);
        i_fireTransferEvent(ptrTransfer, ClipboardTransferState_Added, ClipboardTransferInteraction_None,
                            com::Utf8Str(), com::Utf8Str(), ClipboardError_None);
    }
    if (fFireState)
    {
        ptrTransfer->i_setState(enmState, com::Utf8Str(), enmError);
        if (fTerminal)
            clipboardTransferManagerCompleteProgress(ptrProgressControl, enmStatus, vrcTransfer);
        i_fireTransferEvent(ptrTransfer, enmState, ClipboardTransferInteraction_None,
                            com::Utf8Str(), com::Utf8Str(), enmError);
    }

    return S_OK;
}


/**
 * Cancels the single live transfer with the given public transfer ID.
 *
 * @returns COM status code.
 * @param   aTransferId     Public transfer ID to resolve.
 */
HRESULT ClipboardTransferManager::i_cancelTransferById(ULONG aTransferId)
{
    ComObjPtr<ClipboardTransfer> ptrTransfer;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        for (std::vector<Data::TransferRecord>::const_iterator it = mData.mTransfers.begin();
             it != mData.mTransfers.end(); ++it)
            if (it->mTransferId == aTransferId)
            {
                if (ptrTransfer.isNotNull())
                    return E_INVALIDARG;
                ptrTransfer = it->mTransfer;
            }
    }

    if (ptrTransfer.isNull())
        return E_INVALIDARG;

    ComPtr<IClipboardTransfer> ptrITransfer;
    HRESULT const hrc = ptrTransfer.queryInterfaceTo(ptrITransfer.asOutParam());
    AssertComRCReturn(hrc, hrc);
    return cancel(ptrITransfer);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
