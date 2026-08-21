/* $Id: ClipboardTransferManagerImpl.h 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
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

#ifndef MAIN_INCLUDED_ClipboardTransferManagerImpl_h
#define MAIN_INCLUDED_ClipboardTransferManagerImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardTransferManagerWrap.h"
#include "ClipboardTransferImpl.h"

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
# include <iprt/semaphore.h>
# include <iprt/thread.h>
#endif

#include <list>
#include <vector>

class Clipboard;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
struct ClipboardTransferManagerPublicationThreadCtx;
#endif

/**
 * Clipboard transfer manager object.
 */
class ATL_NO_VTABLE ClipboardTransferManager :
    public ClipboardTransferManagerWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ClipboardTransferManager)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(IEventSource *aEventSource = NULL, Clipboard *aParent = NULL);
    void uninit();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Resets the internally tracked transfer list. */
    void i_reset();
    /** Resets tracked transfers asynchronously for service disconnect. */
    void i_resetFromService();
    /** Returns whether the asynchronous service publication worker is still running. */
    bool i_isPublicationWorkerRunning();
# ifdef UNIT_TEST
    /** Suppresses worker signals so polling and exited-state fallbacks can be tested. */
    void i_setPublicationWorkerSignalsSuppressed(bool fSuppressed);
    /** Suppresses Progress cancellation polling for terminal arbitration tests. */
    void i_setProgressCancellationPollingSuppressed(bool fSuppressed);
    /** Overrides the service cancellation result for worker cancellation tests. */
    void i_setCancelServiceResult(HRESULT hrcResult);
# endif
    /**
     * Handles a Shared Clipboard transfer lifecycle status delivered by the host service.
     *
     * @returns COM status code.
     * @param   aServiceSessionId   Service session that owns the transfer.
     * @param   aTransferId         Shared Clipboard transfer identifier.
     * @param   aGeneration         Host-private transfer generation.
     * @param   aTransfer           Borrowed service transfer used to validate status metadata.
     * @param   enmShClSource       Data source recorded by the backing transfer.
     * @param   enmStatus           Transfer lifecycle status.
     * @param   vrcTransfer         Transfer result code associated with the status.
     */
    HRESULT i_handleTransferStatus(SHCLSESSIONID aServiceSessionId,
                                   SHCLTRANSFERID aTransferId,
                                   SHCLTRANSFERGEN aGeneration,
                                   PSHCLTRANSFER aTransfer,
                                   SHCLSOURCE enmShClSource,
                                   SHCLTRANSFERSTATUS enmStatus,
                                   int vrcTransfer);
    /**
     * Updates the byte progress of a Shared Clipboard transfer.
     *
     * @returns COM status code.
     * @param   aServiceSessionId   Service session that owns the transfer.
     * @param   aTransferId         Shared Clipboard transfer identifier.
     * @param   aGeneration         Host-private transfer generation.
     * @param   cbProcessed         Number of bytes processed so far.
     * @param   cbTotal             Total number of bytes to process.
     */
    HRESULT i_handleTransferProgress(SHCLSESSIONID aServiceSessionId,
                                     SHCLTRANSFERID aTransferId,
                                     SHCLTRANSFERGEN aGeneration,
                                     uint64_t cbProcessed,
                                     uint64_t cbTotal);
    HRESULT i_cancelTransferById(ULONG aTransferId);
#endif

private:

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    void i_fireTransferEvent(const ComObjPtr<ClipboardTransfer> &aTransfer,
                             ClipboardTransferState_T aState,
                             ClipboardTransferInteraction_T aInteraction,
                             const com::Utf8Str &aPath,
                             const com::Utf8Str &aMessage,
                             ClipboardError_T aError);
#endif

    /** @name Wrapped IClipboardTransferManager properties and methods
     * @{ */
    HRESULT getTransfers(ClipboardTransferDirection_T aDirection,
                          ULONG aFlags,
                          std::vector<ComPtr<IClipboardTransfer> > &aTransfers);
    /**
     * Creates and tracks a Main-owned clipboard transfer.
     *
     * @returns COM status code.
     * @param   aDirection      Transfer direction.
     * @param   aSource         Clipboard source owning the transfer.
     * @param   aAction         Clipboard transfer action.
     * @param   aTransfer       Where to return the transfer object.
     */
    HRESULT create(ClipboardTransferDirection_T aDirection,
                   ClipboardSource_T aSource,
                   ClipboardAction_T aAction,
                   ComPtr<IClipboardTransfer> &aTransfer);
    HRESULT remove(const ComPtr<IClipboardTransfer> &aTransfer);
    HRESULT cancel(const ComPtr<IClipboardTransfer> &aTransfer);
    HRESULT approve(const ComPtr<IClipboardTransfer> &aTransfer,
                    ULONG aFlags);
    HRESULT deny(const ComPtr<IClipboardTransfer> &aTransfer,
                 const com::Utf8Str &aReason);
    HRESULT respond(const ComPtr<IClipboardTransfer> &aTransfer,
                    ClipboardTransferInteraction_T aInteraction,
                    const com::Utf8Str &aPath,
                    ClipboardTransferResponse_T aResponse,
                    const com::Utf8Str &aResponsePath,
                    ULONG aFlags);
    HRESULT pause(const ComPtr<IClipboardTransfer> &aTransfer);
    HRESULT resume(const ComPtr<IClipboardTransfer> &aTransfer);
    HRESULT reset();
    /** @} */

    struct Data
    {
        Data()
            : mParent(NULL)
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            , mPublicationThread(NIL_RTTHREAD)
            , mPublicationThreadDone(NIL_RTSEMEVENTMULTI)
            , mpPublicationThreadCtx(NULL)
            , mfAcceptingPublications(false)
            , mfPublishing(false)
            , mfPublicationWorkerAssigned(false)
            , mfPublicationThreadStopping(false)
            , mfPublicationThreadDeferredCleanup(false)
            , mfPublicationThreadExited(false)
# ifdef UNIT_TEST
            , mfPublicationWorkerSignalsSuppressed(false)
            , mfProgressCancellationPollingSuppressed(false)
            , mfCancelServiceResultOverridden(false)
            , mhrcCancelServiceResult(E_FAIL)
# endif
            , mPublishingThread(NIL_RTTHREAD)
            , mNextTransferId(1)
#endif
        { }

        struct TransferRecord
        {
            TransferRecord()
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                : mServiceSessionId(NIL_SHCLSESSIONID)
                , mTransferId(0)
                , mGeneration(NIL_SHCLTRANSFERGEN)
                , mDirection(SHCLTRANSFERDIR_UNKNOWN)
                , mSource(SHCLSOURCE_INVALID)
                , mStatus(SHCLTRANSFERSTATUS_NONE)
                , mState(ClipboardTransferState_Added)
                , mcbProcessed(0)
                , mcbTotal(0)
                , muLastPercent(0)
                , mfTerminal(false)
                , mfCancelRequested(false)
#endif
            { }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            SHCLSESSIONID                    mServiceSessionId;
            ULONG                            mTransferId;
            SHCLTRANSFERGEN                  mGeneration;
            /** Shared Clipboard data-plane direction. */
            SHCLTRANSFERDIR                  mDirection;
            /** Shared Clipboard data source recorded by the backing transfer. */
            SHCLSOURCE                       mSource;
            SHCLTRANSFERSTATUS               mStatus;
            ClipboardTransferState_T         mState;
            /** Monotonic number of bytes processed by the data plane. */
            uint64_t                         mcbProcessed;
            /** Stable total number of bytes reported by the data plane. */
            uint64_t                         mcbTotal;
            /** Last integer percentage sent to the progress object. */
            ULONG                            muLastPercent;
            bool                             mfTerminal;
            bool                             mfCancelRequested;
#endif
            /** Concrete transfer owned by this manager. */
            ComObjPtr<ClipboardTransfer>     mTransfer;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            ComPtr<IProgress>                mProgress;
            ComPtr<IInternalProgressControl> mProgressControl;

            /** Returns whether the public Main interface matches this record. */
            bool matches(IClipboardTransfer *aTransfer) const
            {
                ClipboardTransfer *pTransfer = mTransfer;
                return static_cast<IClipboardTransfer *>(pTransfer) == aTransfer;
            }

            /** Returns whether the service identity matches this record. */
            bool matches(SHCLSESSIONID aServiceSessionId, ULONG aTransferId,
                         SHCLTRANSFERGEN aGeneration) const
            {
                return    mServiceSessionId == aServiceSessionId
                       && mTransferId == aTransferId
                       && mGeneration == aGeneration;
            }

            /** Returns whether the concrete Main object matches this record. */
            bool matches(ClipboardTransfer *aTransfer) const
            {
                ClipboardTransfer *pTransfer = mTransfer;
                return pTransfer == aTransfer;
            }

            /** Returns whether both the Main object and service identity match this record. */
            bool matches(ClipboardTransfer *aTransfer,
                         SHCLSESSIONID aServiceSessionId, ULONG aTransferId,
                         SHCLTRANSFERGEN aGeneration) const
            {
                return    matches(aTransfer)
                       && matches(aServiceSessionId, aTransferId, aGeneration);
            }
#endif
        };

        /** Transfer record container type. */
        typedef std::vector<TransferRecord> TransferRecords;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /** One ordered publication of transfer state and/or progress. */
        struct TransferPublication
        {
            TransferPublication()
                : mState(ClipboardTransferState_Added)
                , mError(ClipboardError_None)
                , mStatus(SHCLTRANSFERSTATUS_NONE)
                , mrcTransfer(VINF_SUCCESS)
                , muPercent(0)
                , mfSetState(false)
                , mfSetProgress(false)
                , mfCompleteProgress(false)
                , mfFireEvent(false)
            { }

            /** Transfer whose public state is being published. */
            ComObjPtr<ClipboardTransfer>     mTransfer;
            /** Progress control to update or complete. */
            ComPtr<IInternalProgressControl> mProgressControl;
            /** Public progress interface used to observe cancellation. */
            ComPtr<IProgress>                mProgress;
            /** Public state for the transfer and event. */
            ClipboardTransferState_T         mState;
            /** Public error associated with @a mState. */
            ClipboardError_T                 mError;
            /** Native terminal status used to complete the progress object. */
            SHCLTRANSFERSTATUS               mStatus;
            /** Native terminal result used to complete the progress object. */
            int                              mrcTransfer;
            /** Percentage to publish while the transfer remains active. */
            ULONG                            muPercent;
            /** Whether to update the transfer object's public state. */
            bool                             mfSetState;
            /** Whether to update the progress percentage. */
            bool                             mfSetProgress;
            /** Whether to complete the progress object. */
            bool                             mfCompleteProgress;
            /** Whether to fire a clipboard transfer event. */
            bool                             mfFireEvent;
        };

        /** Ordered publication queue type. */
        typedef std::list<TransferPublication> TransferPublications;
#endif

        /** Parent clipboard object. */
        Clipboard *mParent;
        /** Clipboard event source. */
        ComPtr<IEventSource> mEventSource;
        /** Current clipboard transfer records. */
        TransferRecords mTransfers;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /** Transfer state/progress publications waiting for the single dispatcher. */
        TransferPublications mPublications;
        /** Live transfer records awaiting ordered teardown after pending publications. */
        TransferRecords mTeardownTransfers;
        /** COM-MTA worker used to publish service-ingress work outside the service callback. */
        RTTHREAD mPublicationThread;
        /** Completion event used to wait for the non-waitable worker's last manager access. */
        RTSEMEVENTMULTI mPublicationThreadDone;
        /** Worker context, retained until the worker exits. */
        ClipboardTransferManagerPublicationThreadCtx *mpPublicationThreadCtx;
        /** Whether service ingress may add transfer publications. */
        bool mfAcceptingPublications;
        /** Whether a caller currently drains @a mPublications. */
        bool mfPublishing;
        /** Whether the publication worker owns the next FIFO drain. */
        bool mfPublicationWorkerAssigned;
        /** Whether the publication worker must stop. */
        bool mfPublicationThreadStopping;
        /** Whether the worker must clear its own handle after reentrant teardown. */
        bool mfPublicationThreadDeferredCleanup;
        /** Whether the worker has finished every manager access on the normal join path. */
        bool mfPublicationThreadExited;
# ifdef UNIT_TEST
        /** Whether worker signals are suppressed to exercise finite polling and exit observation. */
        bool mfPublicationWorkerSignalsSuppressed;
        /** Whether Progress cancellation polling is suppressed by a unit test. */
        bool mfProgressCancellationPollingSuppressed;
        /** Whether the unit test overrides backend cancellation. */
        bool mfCancelServiceResultOverridden;
        /** Unit-test backend cancellation result. */
        HRESULT mhrcCancelServiceResult;
# endif
        /** Native thread currently draining publications, or NIL_RTTHREAD. */
        RTTHREAD mPublishingThread;
        /** Finds a record by public Main interface while the caller owns the manager lock. */
        TransferRecords::iterator findTransferRecord(IClipboardTransfer *aTransfer)
        {
            for (TransferRecords::iterator it = mTransfers.begin(); it != mTransfers.end(); ++it)
                if (it->matches(aTransfer))
                    return it;
            return mTransfers.end();
        }

        /** Finds a record by exact service identity while the caller owns the manager lock. */
        TransferRecords::iterator findTransferRecord(SHCLSESSIONID aServiceSessionId, ULONG aTransferId,
                                                     SHCLTRANSFERGEN aGeneration)
        {
            for (TransferRecords::iterator it = mTransfers.begin(); it != mTransfers.end(); ++it)
                if (it->matches(aServiceSessionId, aTransferId, aGeneration))
                    return it;
            return mTransfers.end();
        }

        /** Finds an exact transfer record while the caller owns the manager lock. */
        TransferRecords::iterator findTransferRecord(ClipboardTransfer *aTransfer,
                                                     SHCLSESSIONID aServiceSessionId, ULONG aTransferId,
                                                     SHCLTRANSFERGEN aGeneration)
        {
            for (TransferRecords::iterator it = mTransfers.begin(); it != mTransfers.end(); ++it)
                if (it->matches(aTransfer, aServiceSessionId, aTransferId, aGeneration))
                    return it;
            return mTransfers.end();
        }

        /** Next Main-created transfer identifier. */
        ULONG mNextTransferId;
#endif
    } mData;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Adds prepared publications to the FIFO while the manager write lock is held. */
    void i_enqueueTransferPublications(Data::TransferPublications &aPublications, bool *pfStartPublishing);
    /** Wakes the service publication worker after assigning it under the manager lock. */
    void i_signalPublicationWorker();
    /** Drains queued publications without holding the manager lock. */
    void i_drainTransferPublications();
    /** Common reset implementation for synchronous and service-ingress callers. */
    void i_resetInternal(bool fFromService);
    /** Main procedure of the manager-owned service publication worker. */
    static DECLCALLBACK(int) i_publicationThread(RTTHREAD hThreadSelf, void *pvUser);
    /** Processes cancel requests observed on live transfer progress objects. */
    void i_processProgressCancellations();
    /** Sends a cancellation request for one exact service-backed transfer. */
    HRESULT i_cancelServiceTransfer(SHCLSESSIONID aServiceSessionId,
                                    SHCLTRANSFERID aTransferId,
                                    SHCLTRANSFERGEN aGeneration);
#endif
};

#endif /* !MAIN_INCLUDED_ClipboardTransferManagerImpl_h */
