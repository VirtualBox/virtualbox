/* $Id: ClipboardTransferManagerImpl.h 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
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
#endif

#include <vector>

class Clipboard;

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

        /** Parent clipboard object. */
        Clipboard *mParent;
        /** Clipboard event source. */
        ComPtr<IEventSource> mEventSource;
        /** Current clipboard transfer records. */
        TransferRecords mTransfers;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /** Finds a record by public Main interface while the caller owns the manager lock. */
        TransferRecords::iterator findTransferRecord(IClipboardTransfer *aTransfer)
        {
            for (TransferRecords::iterator it = mTransfers.begin(); it != mTransfers.end(); ++it)
                if (it->matches(aTransfer))
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
};

#endif /* !MAIN_INCLUDED_ClipboardTransferManagerImpl_h */
