/* $Id: ClipboardTransferManagerImpl.cpp 115110 2026-08-25 09:25:30Z andreas.loeffler@oracle.com $ */
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
/** State retained by the manager-owned service publication worker. */
struct ClipboardTransferManagerPublicationThreadCtx
{
    explicit ClipboardTransferManagerPublicationThreadCtx(ClipboardTransferManager *pManager)
        : mManager(pManager)
    { }

    /** Manager whose publication FIFO the worker drains. */
    ClipboardTransferManager            *mManager;
    /** Temporary lifetime hold used when teardown reenters the active worker. */
    ComObjPtr<ClipboardTransferManager>  mManagerHold;
};


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
        case SHCLTRANSFERSTATUS_STARTED:
            return ClipboardTransferState_Added;
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

    /* Prevent a late IProgress::Cancel() from changing an already terminal
     * progress object.  E_FAIL is expected when cancellation won the race. */
    (void)ptrProgressControl->NotifyPointOfNoReturn();

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


/**
 * Calculates an active transfer percentage without overflowing 64-bit byte counters.
 *
 * @returns Percentage in the range 0 through 99.
 * @param   cbProcessed     Number of bytes processed so far. Must not exceed @a cbTotal.
 * @param   cbTotal         Total number of bytes to process. Must be non-zero.
 */
static ULONG clipboardTransferManagerCalcProgress(uint64_t cbProcessed, uint64_t cbTotal)
{
    AssertReturn(cbTotal, 0);
    AssertReturn(cbProcessed <= cbTotal, 0);

    /*
     * Binary-search floor(100 * cbProcessed / cbTotal).  Comparing against
     * ceil(cbTotal * uPercent / 100) avoids overflowing the multiplication.
     */
    uint64_t const cbHundredths = cbTotal / 100;
    uint64_t const cbRemainder  = cbTotal % 100;
    ULONG uMin = 0;
    ULONG uMax = 99;
    while (uMin < uMax)
    {
        ULONG const uPercent = (uMin + uMax + 1) / 2;
        uint64_t const cbThreshold = cbHundredths * uPercent
                                   + (cbRemainder * uPercent + 99) / 100;
        if (cbProcessed >= cbThreshold)
            uMin = uPercent;
        else
            uMax = uPercent - 1;
    }
    return uMin;
}


/**
 * Appends prepared transfer publications to the manager FIFO.
 *
 * The caller must hold the manager write lock.  The first caller which changes
 * the queue from idle to active owns its dispatcher; service ingress assigns
 * that ownership to the worker.  A reentrant caller only appends work and
 * returns, so listener callbacks cannot recursively publish a later transfer
 * state ahead of the current one.
 *
 * @param   aPublications       Prepared publications.  Consumed by this method.
 * @param   pfStartPublishing   Where to return whether the caller must drain the FIFO.
 */
void ClipboardTransferManager::i_enqueueTransferPublications(Data::TransferPublications &aPublications,
                                                              bool *pfStartPublishing)
{
    Assert(isWriteLockOnCurrentThread());
    AssertPtr(pfStartPublishing);
    Assert(mData.mfAcceptingPublications);
    *pfStartPublishing = false;
    if (aPublications.empty())
        return;

    mData.mPublications.splice(mData.mPublications.end(), aPublications);
    if (!mData.mfPublishing)
    {
        mData.mfPublishing = true;
        *pfStartPublishing = true;
    }
}


/**
 * Wakes the manager-owned service publication worker.
 */
void ClipboardTransferManager::i_signalPublicationWorker()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData.mPublicationThread != NIL_RTTHREAD
#ifdef UNIT_TEST
        && !mData.mfPublicationWorkerSignalsSuppressed
#endif
       )
    {
        int const vrc = RTThreadUserSignal(mData.mPublicationThread);
        /* The worker also polls on a finite interval, so a failed wake cannot
         * strand accepted work or prevent teardown from completing. */
        AssertRC(vrc);
    }
}


/**
 * Runs the manager-owned COM-MTA service publication worker.
 *
 * Service extension callbacks only update records, append immutable
 * publications and wake this thread.  Consequently an active API listener
 * cannot reenter service extension unregistration from within the service's
 * own notification callback.
 *
 * @returns VBox status code.
 * @param   hThreadSelf     Native handle of this worker.
 * @param   pvUser          ClipboardTransferManagerPublicationThreadCtx pointer.
 */
/* static */ DECLCALLBACK(int) ClipboardTransferManager::i_publicationThread(RTTHREAD hThreadSelf, void *pvUser)
{
    ClipboardTransferManagerPublicationThreadCtx *pCtx
        = static_cast<ClipboardTransferManagerPublicationThreadCtx *>(pvUser);
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    ClipboardTransferManager *pManager = pCtx->mManager;
    AssertPtrReturn(pManager, VERR_INVALID_POINTER);

    int vrc = VINF_SUCCESS;
    for (;;)
    {
        int const vrcWait = RTThreadUserWait(hThreadSelf, 100 /* cMillies */);
        if (   RT_FAILURE(vrcWait)
            && vrcWait != VERR_TIMEOUT
            && vrcWait != VERR_INTERRUPTED)
        {
            /* A broken user-event wakeup must not kill the sole publication
             * worker.  Continue polling so uninit can always stop it. */
            AssertRC(vrcWait);
            RTThreadSleep(10);
        }

        bool fStopping;
        {
            AutoReadLock alock(pManager COMMA_LOCKVAL_SRC_POS);
            fStopping = pManager->mData.mfPublicationThreadStopping;
        }
        if (!fStopping)
            pManager->i_processProgressCancellations();

        bool fDrain;
        {
            AutoWriteLock alock(pManager COMMA_LOCKVAL_SRC_POS);
            fDrain = pManager->mData.mfPublicationWorkerAssigned;
            if (fDrain)
            {
                Assert(pManager->mData.mfPublishing);
                Assert(pManager->mData.mPublishingThread == NIL_RTTHREAD);
                pManager->mData.mPublishingThread = hThreadSelf;
            }
            pManager->mData.mfPublicationWorkerAssigned = false;
            fStopping = pManager->mData.mfPublicationThreadStopping;
        }

        if (fDrain)
            pManager->i_drainTransferPublications();

        {
            AutoReadLock alock(pManager COMMA_LOCKVAL_SRC_POS);
            fStopping = pManager->mData.mfPublicationThreadStopping;
        }
        if (fStopping)
            break;
    }

    RTSEMEVENTMULTI hThreadDone;
    bool fDeferredCleanup;
#ifdef UNIT_TEST
    bool fSuppressThreadDoneSignal;
#endif
    {
        AutoWriteLock alock(pManager COMMA_LOCKVAL_SRC_POS);
        Assert(pManager->mData.mpPublicationThreadCtx == pCtx);
        Assert(!pManager->mData.mfPublicationWorkerAssigned);
        hThreadDone = pManager->mData.mPublicationThreadDone;
        Assert(hThreadDone != NIL_RTSEMEVENTMULTI);
        fDeferredCleanup = pManager->mData.mfPublicationThreadDeferredCleanup;
#ifdef UNIT_TEST
        fSuppressThreadDoneSignal = pManager->mData.mfPublicationWorkerSignalsSuppressed;
#endif
        pManager->mData.mpPublicationThreadCtx = NULL;
    }

    if (fDeferredCleanup)
    {
        int const vrcDestroy = RTSemEventMultiDestroy(hThreadDone);
        AssertRC(vrcDestroy);
        {
            AutoWriteLock alock(pManager COMMA_LOCKVAL_SRC_POS);
            Assert(pManager->mData.mPublicationThreadDone == hThreadDone);
            pManager->mData.mPublicationThreadDone = NIL_RTSEMEVENTMULTI;
            pManager->mData.mPublicationThread = NIL_RTTHREAD;
            pManager->mData.mfPublicationThreadExited = true;
        }
    }
    else
    {
        int vrcSignal = VINF_SUCCESS;
#ifdef UNIT_TEST
        if (!fSuppressThreadDoneSignal)
#endif
            vrcSignal = RTSemEventMultiSignal(hThreadDone);
        /* The external waiter also observes mfPublicationThreadExited, so even
         * a failed completion signal cannot strand manager teardown. */
        AssertRC(vrcSignal);

        /* Let the external uninit waiter destroy the completion semaphore only
         * after the signal call and context destruction have both finished. */
        delete pCtx;
        pCtx = NULL;
        {
            AutoWriteLock alock(pManager COMMA_LOCKVAL_SRC_POS);
            pManager->mData.mfPublicationThreadExited = true;
        }
    }

    /* This can release the last reference acquired by reentrant uninit(). */
    if (pCtx)
        delete pCtx;
    return vrc;
}


/**
 * Sends a cancellation request for an exact service-backed transfer.
 *
 * @retval  E_POINTER           if @a pKey is NULL.
 * @retval  E_FAIL              if the parent clipboard is unavailable.
 * @returns                     COM status code from acquiring or calling the parent clipboard.
 * @param   pKey                Host-side transfer key.
 */
HRESULT ClipboardTransferManager::i_cancelServiceTransfer(PCSHCLTRANSFERKEY pKey)
{
    AssertPtrReturn(pKey, E_POINTER);
#ifdef UNIT_TEST
    bool fCancelServiceResultOverridden;
    HRESULT hrcCancelServiceResult;
#endif
    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
#ifdef UNIT_TEST
        fCancelServiceResultOverridden = mData.mfCancelServiceResultOverridden;
        hrcCancelServiceResult = mData.mhrcCancelServiceResult;
#endif
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
    }

#ifdef UNIT_TEST
    if (fCancelServiceResultOverridden)
        return hrcCancelServiceResult;
#endif

    HRESULT hrc;
    if (!pParent)
        hrc = E_FAIL;
    else if (FAILED(autoCaller.hrc()))
        hrc = autoCaller.hrc();
    else
        hrc = pParent->i_transferCancel(pKey);
    return hrc;
}


/**
 * Processes cancellation requests made through live transfer progress objects.
 *
 * IProgress::Cancel() only marks the Progress as canceled.  This existing
 * manager worker observes that flag and performs local cleanup or the
 * potentially reentrant backend call without holding either the manager or
 * Progress lock.  The exact transfer record is revalidated afterwards so a
 * concurrent terminal service status always wins safely.
 */
void ClipboardTransferManager::i_processProgressCancellations()
{
    for (;;)
    {
        ComObjPtr<ClipboardTransfer> ptrTransfer;
        SHCLTRANSFERKEY Key = SHCLTRANSFERKEY_INITIALIZER;
        Data::TransferPublications Publications;
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (   !mData.mfAcceptingPublications
                || mData.mfPublicationThreadStopping)
                return;
#ifdef UNIT_TEST
            if (mData.mfProgressCancellationPollingSuppressed)
                return;
#endif

            for (Data::TransferRecords::iterator it = mData.mTransfers.begin(); it != mData.mTransfers.end(); ++it)
            {
                if (   it->mfTerminal
                    || it->mfCancelRequested
                    || it->mProgress.isNull())
                    continue;

                BOOL fCanceled = FALSE;
                HRESULT const hrc = it->mProgress->COMGETTER(Canceled)(&fCanceled);
                if (FAILED(hrc))
                {
                    AssertComRC(hrc);
                    continue;
                }
                if (!fCanceled)
                    continue;

                Data::TransferPublication Publication;
                Publication.mTransfer = it->mTransfer;
                Publication.mProgressControl = it->mProgressControl;
                Publication.mProgress = it->mProgress;
                Publication.mState = ClipboardTransferState_Canceled;
                Publication.mError = ClipboardError_None;
                Publication.mStatus = SHCLTRANSFERSTATUS_CANCELED;
                Publication.mrcTransfer = VERR_CANCELLED;
                Publication.mfSetState = true;
                Publication.mfCompleteProgress = true;
                Publication.mfFireEvent = mData.mParent != NULL || mData.mEventSource.isNotNull();
                try
                {
                    Publications.push_back(Publication);
                }
                catch (std::bad_alloc &)
                {
                    /* Retry on the worker's next finite poll without losing
                     * the irreversible IProgress cancellation request. */
                    return;
                }

                ptrTransfer = it->mTransfer;
                Key = it->mServiceKey;
                it->mfCancelRequested = true;
                break;
            }
        }

        if (ptrTransfer.isNull())
            return;

        bool const fServiceTransfer = ShClTransferKeyIsValid(&Key);
        HRESULT const hrcCancel = fServiceTransfer
                                ? i_cancelServiceTransfer(&Key)
                                : S_OK;
        bool fStartPublishing = false;
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            Data::TransferRecords::iterator it = mData.findTransferRecord((ClipboardTransfer *)ptrTransfer, &Key);
            if (it != mData.mTransfers.end())
            {
                Data::TransferPublication &Publication = Publications.front();
                if (FAILED(hrcCancel))
                {
                    Publication.mState = ClipboardTransferState_Failed;
                    Publication.mError = ClipboardError_OperationFailed;
                    Publication.mStatus = SHCLTRANSFERSTATUS_ERROR;
                    Publication.mrcTransfer = VERR_GENERAL_FAILURE;
                }

                mData.mTransfers.erase(it);
                Log2Func(("Processed progress cancellation: session=%RU16, id=%RU16, generation=%RU64, hrc=%Rhrc, cTransfers=%zu\n",
                          ShClTransferKeyGetSessionId(&Key), ShClTransferKeyGetTransferId(&Key), Key.uGeneration,
                          hrcCancel, mData.mTransfers.size()));
                i_enqueueTransferPublications(Publications, &fStartPublishing);
                if (fStartPublishing)
                    mData.mfPublicationWorkerAssigned = true;
            }
        }

        if (   fServiceTransfer
            && FAILED(hrcCancel))
            LogFunc(("Canceling transfer through the backend failed: session=%RU16, id=%RU16, generation=%RU64, hrc=%Rhrc\n",
                     ShClTransferKeyGetSessionId(&Key), ShClTransferKeyGetTransferId(&Key), Key.uGeneration, hrcCancel));
    }
}


/**
 * Drains transfer state and progress publications in FIFO order.
 *
 * No manager lock is held while updating a transfer, completing its progress
 * object or firing an event.  Listener reentry therefore remains safe; the
 * reentrant operation appends another publication which this dispatcher picks
 * up after the current callback has returned.
 */
void ClipboardTransferManager::i_drainTransferPublications()
{
    Data::TransferPublications Current;
    Data::TransferRecords TeardownTransfers;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Assert(mData.mfPublishing);
        if (mData.mPublishingThread == NIL_RTTHREAD)
            mData.mPublishingThread = RTThreadSelf();
        else
            Assert(mData.mPublishingThread == RTThreadSelf());
    }
    for (;;)
    {
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (mData.mPublications.empty())
            {
                if (!mData.mfAcceptingPublications)
                    TeardownTransfers.swap(mData.mTeardownTransfers);
                if (TeardownTransfers.empty())
                {
                    mData.mfPublishing = false;
                    mData.mPublishingThread = NIL_RTTHREAD;
                }
            }
            else
                Current.splice(Current.end(), mData.mPublications, mData.mPublications.begin());
        }

        if (!TeardownTransfers.empty())
        {
            /* Teardown is ordered after every publication which was already
             * accepted.  In particular, listener reentry cannot leave a
             * transfer InProgress after its manager has been uninitialized. */
            for (Data::TransferRecords::iterator it = TeardownTransfers.begin(); it != TeardownTransfers.end(); ++it)
            {
                if (it->mTransfer.isNotNull())
                    it->mTransfer->i_setState(ClipboardTransferState_Removed, com::Utf8Str(), ClipboardError_None);
                clipboardTransferManagerCompleteProgress(it->mProgressControl,
                                                         SHCLTRANSFERSTATUS_UNINITIALIZED, VERR_WRONG_ORDER);
            }
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                Assert(mData.mPublishingThread == RTThreadSelf());
                mData.mfPublishing = false;
                mData.mPublishingThread = NIL_RTTHREAD;
            }
            return;
        }
        if (Current.empty())
            return;

        Data::TransferPublication const &Publication = Current.front();
        if (Publication.mfSetState)
            Publication.mTransfer->i_setState(Publication.mState, com::Utf8Str(), Publication.mError);
        if (Publication.mfSetProgress)
        {
            HRESULT const hrcSetProgress = Publication.mProgressControl->SetCurrentOperationProgress(
                Publication.muPercent);
            if (FAILED(hrcSetProgress))
            {
                /* A queued update may run after IProgress::Cancel() acquired
                 * the Progress write lock.  That is an expected failure;
                 * retain assertions for every other setter failure. */
                BOOL fCanceled = FALSE;
                HRESULT const hrcCanceled = Publication.mProgress->COMGETTER(Canceled)(&fCanceled);
                if (FAILED(hrcCanceled))
                    AssertComRC(hrcCanceled);
                else if (!fCanceled)
                    AssertComRC(hrcSetProgress);
            }
        }
        if (Publication.mfCompleteProgress)
            clipboardTransferManagerCompleteProgress(Publication.mProgressControl,
                                                     Publication.mStatus, Publication.mrcTransfer);
        if (Publication.mfFireEvent)
            i_fireTransferEvent(Publication.mTransfer, Publication.mState, ClipboardTransferInteraction_None,
                                com::Utf8Str(), com::Utf8Str(), Publication.mError);

        Current.clear();
    }
}

#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Initializes a clipboard transfer manager object.
 *
 * @retval  S_OK                if initialization succeeded.
 * @retval  E_FAIL              if initialization or worker creation failed.
 * @retval  E_OUTOFMEMORY       if the worker context could not be allocated.
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
    Assert(!mData.mfPublishing);
    Assert(mData.mPublications.empty());
    Assert(mData.mTeardownTransfers.empty());
    Assert(mData.mPublicationThread == NIL_RTTHREAD);
    Assert(mData.mPublicationThreadDone == NIL_RTSEMEVENTMULTI);
    Assert(mData.mpPublicationThreadCtx == NULL);
    mData.mNextTransferId = 1;
    mData.mPublications.clear();
    mData.mTeardownTransfers.clear();
    mData.mfAcceptingPublications = true;
    mData.mfPublishing = false;
    mData.mfPublicationWorkerAssigned = false;
    mData.mfPublicationThreadStopping = false;
    mData.mfPublicationThreadDeferredCleanup = false;
    mData.mfPublicationThreadExited = false;
# ifdef UNIT_TEST
    mData.mfPublicationWorkerSignalsSuppressed = false;
# endif
    mData.mPublishingThread = NIL_RTTHREAD;

    ClipboardTransferManagerPublicationThreadCtx *pThreadCtx
        = new (std::nothrow) ClipboardTransferManagerPublicationThreadCtx(this);
    if (!pThreadCtx)
        return E_OUTOFMEMORY;

    int vrc = RTSemEventMultiCreate(&mData.mPublicationThreadDone);
    if (RT_FAILURE(vrc))
    {
        delete pThreadCtx;
        return setErrorBoth(E_FAIL, vrc, tr("Creating the Shared Clipboard publication completion event failed with %Rrc"),
                            vrc);
    }
    mData.mpPublicationThreadCtx = pThreadCtx;
    vrc = RTThreadCreate(&mData.mPublicationThread, i_publicationThread, pThreadCtx, 0,
                         RTTHREADTYPE_MAIN_WORKER, RTTHREADFLAGS_COM_MTA, "ShClMainPub");
    if (RT_FAILURE(vrc))
    {
        mData.mPublicationThread = NIL_RTTHREAD;
        mData.mpPublicationThreadCtx = NULL;
        int const vrcDestroy = RTSemEventMultiDestroy(mData.mPublicationThreadDone);
        AssertRC(vrcDestroy);
        mData.mPublicationThreadDone = NIL_RTSEMEVENTMULTI;
        delete pThreadCtx;
        return setErrorBoth(E_FAIL, vrc, tr("Creating the Shared Clipboard publication worker failed with %Rrc"), vrc);
    }
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
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RTTHREAD hPublicationThread = NIL_RTTHREAD;
    RTSEMEVENTMULTI hPublicationThreadDone = NIL_RTSEMEVENTMULTI;
    bool fDeferredThreadCleanup = false;
#endif
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        mData.mNextTransferId = 1;
        mData.mfAcceptingPublications = false;
        mData.mfPublicationThreadStopping = true;
        Assert(mData.mTeardownTransfers.empty());
        mData.mTeardownTransfers.swap(mData.mTransfers);
        if (!mData.mfPublishing)
        {
            Assert(mData.mPublications.empty());
            mData.mPublications.clear();
            DetachedTransfers.swap(mData.mTeardownTransfers);
        }

        hPublicationThread = mData.mPublicationThread;
        hPublicationThreadDone = mData.mPublicationThreadDone;
        if (hPublicationThread != NIL_RTTHREAD)
        {
            /* A listener can reenter teardown either on the worker itself or
             * through a cross-apartment call while the worker waits for it.
             * An assigned publication can enter that state as soon as this
             * lock is released.  Joining in any of these cases deadlocks.
             * Keep the manager alive and let the worker finish the accepted
             * FIFO before cleaning itself. */
            fDeferredThreadCleanup =    hPublicationThread == RTThreadSelf()
                                     || mData.mPublishingThread == hPublicationThread
                                     || mData.mfPublicationWorkerAssigned;
            if (fDeferredThreadCleanup)
            {
                AssertPtr(mData.mpPublicationThreadCtx);
                mData.mfPublicationThreadDeferredCleanup = true;
                mData.mpPublicationThreadCtx->mManagerHold = this;
            }
#ifdef UNIT_TEST
            if (!mData.mfPublicationWorkerSignalsSuppressed)
#endif
            {
                int const vrc = RTThreadUserSignal(hPublicationThread);
                AssertRC(vrc);
            }
        }
#else
        DetachedTransfers.swap(mData.mTransfers);
#endif
        ptrEventSource = mData.mEventSource;
        mData.mEventSource.setNull();
        mData.mParent = NULL;
    }
    RT_NOREF(ptrEventSource);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (hPublicationThreadDone != NIL_RTSEMEVENTMULTI && !fDeferredThreadCleanup)
    {
        for (;;)
        {
            int const vrcWait = RTSemEventMultiWait(hPublicationThreadDone, 100 /* cMillies */);
            if (   RT_FAILURE(vrcWait)
                && vrcWait != VERR_TIMEOUT
                && vrcWait != VERR_INTERRUPTED)
                AssertRC(vrcWait);

            {
                AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
                if (mData.mfPublicationThreadExited)
                    break;
            }

            if (hPublicationThread != NIL_RTTHREAD)
                i_signalPublicationWorker();
            if (RT_SUCCESS(vrcWait))
                RTThreadSleep(1);
        }

        int const vrcDestroy = RTSemEventMultiDestroy(hPublicationThreadDone);
        AssertRC(vrcDestroy);
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Assert(mData.mPublicationThread == hPublicationThread);
        Assert(mData.mPublicationThreadDone == hPublicationThreadDone);
        Assert(mData.mpPublicationThreadCtx == NULL);
        mData.mPublicationThreadDone = NIL_RTSEMEVENTMULTI;
        mData.mPublicationThread = NIL_RTTHREAD;
    }

    /* No service terminal notification is guaranteed once teardown has begun.
     * Complete every detached live Progress explicitly so GUI consumers cannot
     * remain stuck on an operation whose manager no longer exists. */
    for (Data::TransferRecords::iterator it = DetachedTransfers.begin(); it != DetachedTransfers.end(); ++it)
    {
        if (it->mTransfer.isNotNull())
            it->mTransfer->i_setState(ClipboardTransferState_Removed, com::Utf8Str(), ClipboardError_None);
        clipboardTransferManagerCompleteProgress(it->mProgressControl,
                                                 SHCLTRANSFERSTATUS_UNINITIALIZED, VERR_WRONG_ORDER);
    }
#endif
}


/**
 * Returns the current clipboard transfers.
 *
 * @retval  S_OK                if the transfer list was returned.
 * @retval  E_INVALIDARG        if the flags or direction are invalid.
 * @retval  E_OUTOFMEMORY       if the result list could not be allocated.
 * @retval  E_NOTIMPL           if transfer support is not compiled in.
 * @returns                     COM error from querying an internal transfer interface.
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
                /* Keep the exact record internally until asynchronous backend
                 * cancellation and terminal publication have finished, but do
                 * not expose it as an active manager transfer once cancellation
                 * became irreversible through IProgress::Cancel(). */
                if (it->mfCancelRequested)
                    continue;
                if (it->mProgress.isNotNull())
                {
                    BOOL fCanceled = FALSE;
                    HRESULT const hrcCanceled = it->mProgress->COMGETTER(Canceled)(&fCanceled);
                    if (SUCCEEDED(hrcCanceled) && fCanceled)
                        continue;
                }

                if (aDirection != ClipboardTransferDirection_Any)
                {
                    ClipboardTransferDirection_T const enmDirection
                        = it->mDirection == SHCLTRANSFERDIR_GUEST_TO_HOST
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
    ComPtr<IEventSource> ptrProgressInitiator;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        idTransfer = mData.mNextTransferId++;
        if (mData.mNextTransferId == 0)
            mData.mNextTransferId = 1;
        ptrProgressInitiator = mData.mEventSource;
    }

    if (ptrProgressInitiator.isNull())
        return setError(E_FAIL, tr("Clipboard transfer manager has no event source for progress reporting"));

    ComObjPtr<Progress> ptrProgressObj;
    HRESULT hrc = ptrProgressObj.createObject();
    if (FAILED(hrc))
        return setError(hrc, tr("Creating clipboard transfer progress object failed"));
    hrc = ptrProgressObj->init(ptrProgressInitiator,
                               com::Utf8Str("Shared Clipboard transfer"), TRUE /* aCancelable */);
    if (FAILED(hrc))
        return setError(hrc, tr("Initializing clipboard transfer progress object failed"));

    ComPtr<IProgress> ptrProgress;
    hrc = ptrProgressObj.queryInterfaceTo(ptrProgress.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer progress interface failed"));

    ComPtr<IInternalProgressControl> ptrProgressControl(ptrProgress);
    if (ptrProgressControl.isNull())
        return setError(E_FAIL, tr("Querying internal clipboard transfer progress interface failed"));

    ComObjPtr<ClipboardTransfer> ptrTransferObj;
    hrc = ptrTransferObj.createObject();
    if (FAILED(hrc))
        return setError(hrc, tr("Creating clipboard transfer object failed"));

    ComPtr<IClipboardItem> ptrItem;
    hrc = ptrTransferObj->init(idTransfer, aDirection, aSource, aAction, ptrItem, ptrProgress);
    if (FAILED(hrc))
        return setError(hrc, tr("Initializing clipboard transfer object failed"));

    ComPtr<IClipboardTransfer> ptrTransfer;
    hrc = ptrTransferObj.queryInterfaceTo(ptrTransfer.asOutParam());
    if (FAILED(hrc))
        return setError(hrc, tr("Querying clipboard transfer interface failed"));

    Data::TransferPublications Publications;
    Data::TransferPublication Publication;
    Publication.mTransfer = ptrTransferObj;
    Publication.mState = ClipboardTransferState_Added;
    Publication.mError = ClipboardError_None;
    Publication.mfSetState = true;
    Publication.mfFireEvent = true;
    try
    {
        Publications.push_back(Publication);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    bool fStartPublishing = false;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Data::TransferRecord Record;
        Record.mTransferId = idTransfer;
        Record.mDirection = aDirection == ClipboardTransferDirection_ToHost
                          ? SHCLTRANSFERDIR_GUEST_TO_HOST : SHCLTRANSFERDIR_HOST_TO_GUEST;
        Record.mSource = aSource == ClipboardSource_Host
                       ? SHCLSOURCE_LOCAL
                       : aSource == ClipboardSource_Guest ? SHCLSOURCE_REMOTE : SHCLSOURCE_INVALID;
        Record.mTransfer = ptrTransferObj;
        Record.mProgress = ptrProgress;
        Record.mProgressControl = ptrProgressControl;
        try
        {
            mData.mTransfers.push_back(Record);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        i_enqueueTransferPublications(Publications, &fStartPublishing);
    }

    aTransfer = ptrTransfer;
    if (fStartPublishing)
        i_drainTransferPublications();
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Removes a clipboard transfer.
 *
 * @retval  S_OK                if the transfer status was accepted or safely ignored.
 * @retval  E_INVALIDARG        if the key, source, status, result, direction, or transition is invalid.
 * @retval  E_FAIL              if required transfer state is unavailable.
 * @retval  E_OUTOFMEMORY       if a transfer record or publication cannot be allocated.
 * @returns                     A failure status from creating or initializing the transfer's Main objects.
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

    bool fRemoved = false;
    bool fServiceTransfer = false;
    bool fStartPublishing = false;
    Data::TransferPublications Publications;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Data::TransferRecords::iterator it = mData.findTransferRecord(aTransfer);
        if (it != mData.mTransfers.end())
        {
            if (ShClTransferKeyIsValid(&it->mServiceKey))
                fServiceTransfer = true;
            else
            {
                Data::TransferPublication Publication;
                Publication.mTransfer = it->mTransfer;
                Publication.mProgressControl = it->mProgressControl;
                Publication.mState = ClipboardTransferState_Removed;
                Publication.mError = ClipboardError_None;
                Publication.mStatus = SHCLTRANSFERSTATUS_UNINITIALIZED;
                Publication.mrcTransfer = VERR_CANCELLED;
                Publication.mfSetState = true;
                Publication.mfCompleteProgress = true;
                Publication.mfFireEvent = mData.mParent != NULL || mData.mEventSource.isNotNull();
                try
                {
                    Publications.push_back(Publication);
                }
                catch (std::bad_alloc &)
                {
                    return E_OUTOFMEMORY;
                }

                mData.mTransfers.erase(it);
                Log2Func(("Removed transfer: cTransfers=%zu\n", mData.mTransfers.size()));
                fRemoved = true;
                i_enqueueTransferPublications(Publications, &fStartPublishing);
            }
        }
    }

    if (fServiceTransfer)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot remove an active service transfer; cancel it first"));
    if (!fRemoved)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is no longer owned by this manager"));

    if (fStartPublishing)
        i_drainTransferPublications();
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Cancels a clipboard transfer.
 *
 * @retval  S_OK                if progress was accepted or safely ignored.
 * @retval  E_OUTOFMEMORY       if a progress publication cannot be allocated.
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

    bool fCanceled = false;
    bool fServiceTransfer = false;
    bool fStartPublishing = false;
    ComPtr<IProgress> ptrProgress;
    Data::TransferPublications Publications;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        Data::TransferRecords::iterator it = mData.findTransferRecord(aTransfer);
        if (it != mData.mTransfers.end())
        {
            fServiceTransfer = ShClTransferKeyIsValid(&it->mServiceKey);
            if (fServiceTransfer)
                ptrProgress = it->mProgress;
            else
            {
                Data::TransferPublication Publication;
                Publication.mTransfer = it->mTransfer;
                Publication.mProgressControl = it->mProgressControl;
                Publication.mState = ClipboardTransferState_Canceled;
                Publication.mError = ClipboardError_None;
                Publication.mStatus = SHCLTRANSFERSTATUS_CANCELED;
                Publication.mrcTransfer = VERR_CANCELLED;
                Publication.mfSetState = true;
                Publication.mfCompleteProgress = true;
                Publication.mfFireEvent = mData.mParent != NULL || mData.mEventSource.isNotNull();
                try
                {
                    Publications.push_back(Publication);
                }
                catch (std::bad_alloc &)
                {
                    return E_OUTOFMEMORY;
                }

                mData.mTransfers.erase(it);
                Log2Func(("Canceled transfer: cTransfers=%zu\n", mData.mTransfers.size()));
                fCanceled = true;
                i_enqueueTransferPublications(Publications, &fStartPublishing);
            }
        }
    }

    if (!fCanceled && !fServiceTransfer)
        return setError(VBOX_E_OBJECT_NOT_FOUND, tr("Clipboard transfer is no longer owned by this manager"));

    if (fCanceled)
    {
        if (fStartPublishing)
            i_drainTransferPublications();
        return S_OK;
    }

    if (ptrProgress.isNull())
        return setError(E_FAIL, tr("Clipboard transfer has no progress object to cancel"));
    return ptrProgress->Cancel();
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
    bool fHasServiceTransfers = false;
    bool fStartPublishing = false;
    Data::TransferPublications Publications;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        for (std::vector<Data::TransferRecord>::const_iterator it = mData.mTransfers.begin();
             it != mData.mTransfers.end(); ++it)
        {
            if (ShClTransferKeyIsValid(&it->mServiceKey))
            {
                fHasServiceTransfers = true;
                break;
            }
        }

        if (!fHasServiceTransfers)
        {
            try
            {
                for (std::vector<Data::TransferRecord>::const_iterator it = mData.mTransfers.begin();
                     it != mData.mTransfers.end(); ++it)
                {
                    Data::TransferPublication Publication;
                    Publication.mTransfer = it->mTransfer;
                    Publication.mProgressControl = it->mProgressControl;
                    Publication.mState = ClipboardTransferState_Removed;
                    Publication.mError = ClipboardError_None;
                    Publication.mStatus = SHCLTRANSFERSTATUS_UNINITIALIZED;
                    Publication.mrcTransfer = VERR_CANCELLED;
                    Publication.mfSetState = true;
                    Publication.mfCompleteProgress = true;
                    Publication.mfFireEvent = true;
                    Publications.push_back(Publication);
                }
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }

            size_t const cTransfers = mData.mTransfers.size();
            mData.mTransfers.clear();
            Log2Func(("Detached %zu transfers during public reset\n", cTransfers));
            RT_NOREF(cTransfers);
            i_enqueueTransferPublications(Publications, &fStartPublishing);
        }
    }

    if (fHasServiceTransfers)
        return setError(VBOX_E_OBJECT_IN_USE,
                        tr("Cannot reset clipboard transfers while a service transfer is active; cancel it first"));

    if (fStartPublishing)
        i_drainTransferPublications();
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
    i_resetInternal(false /* fFromService */);
}


/**
 * Queues an asynchronous reset before the service invalidates its backend connection.
 */
void ClipboardTransferManager::i_resetFromService()
{
    LogFunc(("Resetting transfer manager for service disconnect\n"));
    i_resetInternal(true /* fFromService */);
}


/**
 * Returns whether the manager-owned service publication worker is running.
 *
 * @returns true if the worker has not exited yet, false otherwise.
 */
bool ClipboardTransferManager::i_isPublicationWorkerRunning()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    return    mData.mPublicationThread != NIL_RTTHREAD
           || mData.mPublicationThreadDone != NIL_RTSEMEVENTMULTI;
}


# ifdef UNIT_TEST
/**
 * Suppresses publication-worker signals to exercise finite polling and exited-state fallbacks.
 *
 * @param   fSuppressed     Whether worker wake and completion signals should be suppressed.
 */
void ClipboardTransferManager::i_setPublicationWorkerSignalsSuppressed(bool fSuppressed)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.mfPublicationWorkerSignalsSuppressed = fSuppressed;
}


/**
 * Suppresses Progress cancellation polling for terminal arbitration tests.
 *
 * @param   fSuppressed     Whether cancellation polling should be suppressed.
 */
void ClipboardTransferManager::i_setProgressCancellationPollingSuppressed(bool fSuppressed)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.mfProgressCancellationPollingSuppressed = fSuppressed;
}


/**
 * Overrides the service cancellation result for publication-worker tests.
 *
 * @param   hrcResult       Result returned instead of invoking the backend.
 */
void ClipboardTransferManager::i_setCancelServiceResult(HRESULT hrcResult)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.mfCancelServiceResultOverridden = true;
    mData.mhrcCancelServiceResult = hrcResult;
}
# endif


/**
 * Resets all tracked transfers through the ordered publication FIFO.
 *
 * @param   fFromService    Whether a service callback must assign the asynchronous worker.
 */
void ClipboardTransferManager::i_resetInternal(bool fFromService)
{
    bool fStartPublishing = false;
    Data::TransferPublications Publications;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData.mfAcceptingPublications)
            return;
        try
        {
            for (std::vector<Data::TransferRecord>::const_iterator it = mData.mTransfers.begin();
                 it != mData.mTransfers.end(); ++it)
            {
                Data::TransferPublication Publication;
                Publication.mTransfer = it->mTransfer;
                Publication.mProgressControl = it->mProgressControl;
                Publication.mState = ClipboardTransferState_Removed;
                Publication.mError = ClipboardError_None;
                Publication.mStatus = SHCLTRANSFERSTATUS_UNINITIALIZED;
                Publication.mrcTransfer = VERR_CANCELLED;
                Publication.mfSetState = true;
                Publication.mfCompleteProgress = true;
                Publication.mfFireEvent = true;
                Publications.push_back(Publication);
            }
        }
        catch (std::bad_alloc &)
        {
            AssertFailed();
            return;
        }

        size_t const cTransfers = mData.mTransfers.size();
        mData.mTransfers.clear();
        Log2Func(("Detached %zu transfers during reset\n", cTransfers));
        RT_NOREF(cTransfers);
        i_enqueueTransferPublications(Publications, &fStartPublishing);
        if (fFromService && fStartPublishing)
            mData.mfPublicationWorkerAssigned = true;
    }

    if (fStartPublishing)
    {
        if (fFromService)
            i_signalPublicationWorker();
        else
            i_drainTransferPublications();
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
    ComPtr<IClipboard> ptrParentHold;
    ComPtr<IEventSource> ptrEventSource;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        pParent = mData.mParent;
        if (pParent)
        {
            /* The manager lock keeps mParent stable while AutoCaller excludes
             * FinalRelease.  Acquire a strong interface reference under that
             * short caller, then release both locks before listener delivery. */
            AutoCaller autoCaller(pParent);
            if (SUCCEEDED(autoCaller.hrc()))
                ptrParentHold = pParent;
        }
        else
            ptrEventSource = mData.mEventSource;
    }

    if (ptrParentHold.isNotNull())
    {
        /* The interface reference keeps the parent allocation alive, while the
         * parent helper retains its AutoCaller only for the target snapshot. */
        pParent->i_fireClipboardTransferEvent(VBOX_SHCL_MAIN_CLIENT_NONE, ptrTransfer, aState, aInteraction,
                                              aPath, aMessage, aError);
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
 * @param   pKey                Host-side transfer key.
 * @param   aTransfer           Borrowed service transfer used to validate status metadata.
 * @param   enmShClSource       Data source recorded by the backing transfer.
 * @param   enmStatus           Transfer lifecycle status.
 * @param   vrcTransfer         Transfer result code associated with the status.
 */
HRESULT ClipboardTransferManager::i_handleTransferStatus(PCSHCLTRANSFERKEY pKey,
                                                         PSHCLTRANSFER aTransfer,
                                                         SHCLSOURCE enmShClSource,
                                                         SHCLTRANSFERSTATUS enmStatus,
                                                         int vrcTransfer)
{
    if (!ShClTransferKeyIsValid(pKey))
        return E_INVALIDARG;
    SHCLTRANSFERID const idTransfer = ShClTransferKeyGetTransferId(pKey);
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
                             ? SHCLTRANSFERDIR_GUEST_TO_HOST : SHCLTRANSFERDIR_HOST_TO_GUEST;
        enmTransferSource    = enmShClSource;
    }
    if (   (   enmTransferDirection != SHCLTRANSFERDIR_GUEST_TO_HOST
            && enmTransferDirection != SHCLTRANSFERDIR_HOST_TO_GUEST)
        || (   enmTransferSource != SHCLSOURCE_LOCAL
            && enmTransferSource != SHCLSOURCE_REMOTE)
        || (   enmTransferSource == SHCLSOURCE_LOCAL
            && enmTransferDirection != SHCLTRANSFERDIR_HOST_TO_GUEST)
        || (   enmTransferSource == SHCLSOURCE_REMOTE
            && enmTransferDirection != SHCLTRANSFERDIR_GUEST_TO_HOST))
        return E_INVALIDARG;
    if (enmStatus == SHCLTRANSFERSTATUS_NONE)
        return S_OK;

    SHCLTRANSFERSTATUS enmPublishedStatus = enmStatus;
    int vrcPublished = vrcTransfer;
    ClipboardTransferState_T enmState = clipboardTransferManagerStatusToState(enmPublishedStatus);
    ClipboardError_T enmError = clipboardTransferManagerStatusToError(enmPublishedStatus, vrcPublished);
    bool const fTerminal = ShClTransferStatusIsTerminal(enmStatus);

    bool fStartPublishing = false;
    Data::TransferPublications Publications;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData.mfAcceptingPublications)
            return S_OK;

        size_t idxRecord = mData.mTransfers.size();
        for (size_t i = 0; i < mData.mTransfers.size(); ++i)
            if (mData.mTransfers[i].matches(pKey))
            {
                idxRecord = i;
                break;
            }

        if (idxRecord == mData.mTransfers.size())
        {
            if (fTerminal)
            {
                Log2Func(("Ignoring terminal status for unknown transfer: session=%RU16, id=%RU16, generation=%RU64, status=%RU32\n",
                          ShClTransferKeyGetSessionId(pKey), idTransfer, pKey->uGeneration, (uint32_t)enmStatus));
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
                                       com::Utf8Str("Shared Clipboard transfer"), TRUE /* aCancelable */);
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

            ClipboardTransferDirection_T const enmDirection = enmTransferDirection == SHCLTRANSFERDIR_GUEST_TO_HOST
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
            Record.mServiceKey = *pKey;
            Record.mTransferId = idTransfer;
            Record.mDirection = enmTransferDirection;
            Record.mSource = enmTransferSource;
            Record.mStatus = enmStatus;
            Record.mState = ClipboardTransferState_Added;
            Record.mTransfer = ptrNewTransfer;
            Record.mProgress = ptrIProgress;
            Record.mProgressControl = ptrIProgressControl;

            Data::TransferPublication Publication;
            Publication.mTransfer = ptrNewTransfer;
            Publication.mState = ClipboardTransferState_Added;
            Publication.mError = ClipboardError_None;
            Publication.mfSetState = true;
            Publication.mfFireEvent = true;
            try
            {
                Publications.push_back(Publication);
                mData.mTransfers.push_back(Record);
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
            i_enqueueTransferPublications(Publications, &fStartPublishing);
            if (fStartPublishing)
                mData.mfPublicationWorkerAssigned = true;
        }
        else
        {
            Data::TransferRecord &Record = mData.mTransfers[idxRecord];
            if (Record.mfCancelRequested && !fTerminal)
                return S_OK;
            if (   Record.mDirection != enmTransferDirection
                || Record.mSource != enmTransferSource
                || !ShClTransferStatusTransitionIsValid(Record.mStatus, enmStatus))
                return E_INVALIDARG;

            Data::TransferPublication Publication;
            bool fPublish = false;
            if (fTerminal)
            {
                /* Allocate the FIFO node before making cancellation
                 * irreversible for this Progress object. */
                try
                {
                    Publications.push_back(Publication);
                }
                catch (std::bad_alloc &)
                {
                    return E_OUTOFMEMORY;
                }

                HRESULT const hrcPointOfNoReturn = Record.mProgressControl->NotifyPointOfNoReturn();
                if (FAILED(hrcPointOfNoReturn))
                {
                    BOOL fCanceled = FALSE;
                    HRESULT const hrcCanceled = Record.mProgress->COMGETTER(Canceled)(&fCanceled);
                    if (FAILED(hrcCanceled))
                        AssertComRC(hrcCanceled);
                    else if (fCanceled && enmPublishedStatus == SHCLTRANSFERSTATUS_COMPLETED)
                    {
                        /* IProgress::Cancel() won its own lock before the
                         * service terminal status.  Keep public state and
                         * Progress completion consistent. */
                        enmPublishedStatus = SHCLTRANSFERSTATUS_CANCELED;
                        vrcPublished = VERR_CANCELLED;
                        enmState = clipboardTransferManagerStatusToState(enmPublishedStatus);
                        enmError = clipboardTransferManagerStatusToError(enmPublishedStatus, vrcPublished);
                    }
                }

                Data::TransferPublication &TerminalPublication = Publications.back();
                TerminalPublication.mTransfer = Record.mTransfer;
                TerminalPublication.mProgressControl = Record.mProgressControl;
                TerminalPublication.mState = enmState;
                TerminalPublication.mError = enmError;
                TerminalPublication.mStatus = enmPublishedStatus;
                TerminalPublication.mrcTransfer = vrcPublished;
                TerminalPublication.mfSetState = true;
                TerminalPublication.mfCompleteProgress = true;
                TerminalPublication.mfFireEvent = true;
                fPublish = true;
            }
            else if (   enmStatus == SHCLTRANSFERSTATUS_STARTED
                     && Record.mcbProcessed > 0
                     && Record.mState == ClipboardTransferState_Added)
            {
                /* A byte snapshot can beat STARTED at this API boundary.  The
                 * progress object already contains that snapshot; publish the
                 * deferred public state transition now. */
                Publication.mTransfer = Record.mTransfer;
                Publication.mState = ClipboardTransferState_InProgress;
                Publication.mError = ClipboardError_None;
                Publication.mfSetState = true;
                Publication.mfFireEvent = true;
                fPublish = true;
            }

            if (fPublish && !fTerminal)
            {
                try
                {
                    Publications.push_back(Publication);
                }
                catch (std::bad_alloc &)
                {
                    return E_OUTOFMEMORY;
                }
            }

            Record.mStatus = enmPublishedStatus;
            if (fTerminal)
            {
                Record.mState = enmState;
                Record.mfTerminal = true;
                mData.mTransfers.erase(mData.mTransfers.begin() + idxRecord);
            }
            else if (fPublish)
                Record.mState = ClipboardTransferState_InProgress;

            i_enqueueTransferPublications(Publications, &fStartPublishing);
            if (fStartPublishing)
                mData.mfPublicationWorkerAssigned = true;
        }
    }

    if (fStartPublishing)
        i_signalPublicationWorker();

    return S_OK;
}


/**
 * Updates a Shared Clipboard transfer progress object from byte counters supplied by the host service.
 *
 * Unknown, stale, non-monotonic and otherwise invalid updates are ignored.  A
 * live transfer is capped at 99 percent; its terminal lifecycle status is the
 * only path which completes the progress object and advances it to 100 percent.
 *
 * @returns COM status code.
 * @param   pKey                Host-side transfer key.
 * @param   cbProcessed         Number of bytes processed so far.
 * @param   cbTotal             Total number of bytes to process.
 */
HRESULT ClipboardTransferManager::i_handleTransferProgress(PCSHCLTRANSFERKEY pKey,
                                                           uint64_t cbProcessed,
                                                           uint64_t cbTotal)
{
    if (   !ShClTransferKeyIsValid(pKey)
        || !cbTotal
        || cbProcessed > cbTotal)
        return S_OK;

    ULONG uPercent = 0;
    bool fStartPublishing = false;
    Data::TransferPublications Publications;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData.mfAcceptingPublications)
            return S_OK;

        Data::TransferRecords::iterator it = mData.findTransferRecord(pKey);
        if (   it == mData.mTransfers.end()
            || it->mfTerminal
            || it->mfCancelRequested
            || it->mProgressControl.isNull())
            return S_OK;

        if (it->mcbTotal)
        {
            if (   it->mcbTotal != cbTotal
                || cbProcessed < it->mcbProcessed)
                return S_OK;
        }

        /* STARTED also covers delayed-rendering metadata probes.  Expose the
         * public InProgress state only once the data plane has transferred
         * actual payload, so a descriptor-only clipboard query cannot create
         * a permanently idle GUI progress notification. */
        bool const fSetInProgress =    cbProcessed > 0
                                    && it->mStatus == SHCLTRANSFERSTATUS_STARTED
                                    && it->mState == ClipboardTransferState_Added;

        uPercent = clipboardTransferManagerCalcProgress(cbProcessed, cbTotal);
        bool const fSetProgress = uPercent > it->muLastPercent;
        if (fSetInProgress || fSetProgress)
        {
            Data::TransferPublication Publication;
            if (fSetInProgress)
            {
                Publication.mTransfer = it->mTransfer;
                Publication.mState = ClipboardTransferState_InProgress;
                Publication.mError = ClipboardError_None;
                Publication.mfSetState = true;
                Publication.mfFireEvent = true;
            }
            if (fSetProgress)
            {
                Publication.mProgressControl = it->mProgressControl;
                Publication.mProgress = it->mProgress;
                Publication.muPercent = uPercent;
                Publication.mfSetProgress = true;
            }
            try
            {
                Publications.push_back(Publication);
            }
            catch (std::bad_alloc &)
            {
                return E_OUTOFMEMORY;
            }
        }

        if (!it->mcbTotal)
            it->mcbTotal = cbTotal;
        it->mcbProcessed = cbProcessed;
        if (fSetInProgress)
            it->mState = ClipboardTransferState_InProgress;
        if (fSetProgress)
            it->muLastPercent = uPercent;
        i_enqueueTransferPublications(Publications, &fStartPublishing);
        if (fStartPublishing)
            mData.mfPublicationWorkerAssigned = true;
    }

    if (fStartPublishing)
        i_signalPublicationWorker();
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
