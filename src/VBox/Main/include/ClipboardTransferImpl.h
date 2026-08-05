/* $Id: ClipboardTransferImpl.h 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer object.
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

#ifndef MAIN_INCLUDED_ClipboardTransferImpl_h
#define MAIN_INCLUDED_ClipboardTransferImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardTransferWrap.h"

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#include <vector>

/**
 * Clipboard transfer object.
 */
class ATL_NO_VTABLE ClipboardTransfer :
    public ClipboardTransferWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ClipboardTransfer)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(ULONG aId,
                 ClipboardTransferDirection_T aDirection,
                 ClipboardSource_T aSource,
                 ClipboardAction_T aAction,
                 const ComPtr<IClipboardItem> &aItem,
                 const ComPtr<IProgress> &aProgress
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                 , PSHCLTRANSFER aTransfer = NULL,
                 bool fOwnTransfer = false
#endif
                 );
    void uninit();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Returns the parent-owned backing Shared Clipboard transfer. */
    PSHCLTRANSFER i_getTransfer() const;
    /**
     * Lists nodes from one parent-owned backing transfer.
     *
     * @returns COM status code.
     * @param   pTransfer       Retained backing transfer.
     * @param   aPath           Transfer-relative directory path, or empty for roots.
     * @param   aFlags          ClipboardTransferListFlag mask.
     * @param   aNodes          Where to return listed nodes.
     */
    HRESULT i_list(PSHCLTRANSFER pTransfer,
                   const com::Utf8Str &aPath,
                   ULONG aFlags,
                   std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes);
    /**
     * Queries one node from one parent-owned backing transfer.
     *
     * @returns COM status code.
     * @param   pTransfer       Retained backing transfer.
     * @param   aPath           Transfer-relative path.
     * @param   aNode           Where to return the node information.
     */
    HRESULT i_query(PSHCLTRANSFER pTransfer,
                    const com::Utf8Str &aPath,
                    ComPtr<IClipboardTransferFsObjInfo> &aNode);
#endif
    /** Updates the public transfer state. */
    void i_setState(ClipboardTransferState_T aState,
                    const com::Utf8Str &aMessage,
                    ClipboardError_T aError);

private:

    /** @name Wrapped IClipboardTransfer properties
     * @{ */
    HRESULT getId(ULONG *aId);
    HRESULT getDirection(ClipboardTransferDirection_T *aDirection);
    HRESULT getSource(ClipboardSource_T *aSource);
    HRESULT getAction(ClipboardAction_T *aAction);
    HRESULT getState(ClipboardTransferState_T *aState);
    HRESULT getItem(ComPtr<IClipboardItem> &aItem);
    HRESULT getProgress(ComPtr<IProgress> &aProgress);
    HRESULT getMessage(com::Utf8Str &aMessage);
    HRESULT getError(ClipboardError_T *aError);
    HRESULT getData(ComPtr<IClipboardTransferData> &aData);
    HRESULT getSourcePaths(std::vector<com::Utf8Str> &aSourcePaths);
    HRESULT setSourcePaths(const std::vector<com::Utf8Str> &aSourcePaths);
    HRESULT roots(std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes);
    HRESULT query(const com::Utf8Str &aPath,
                  ComPtr<IClipboardTransferFsObjInfo> &aNode);
    HRESULT list(const com::Utf8Str &aPath,
                 ULONG aFlags,
                 std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes);
    HRESULT openDirectory(const com::Utf8Str &aPath,
                          ULONG aFlags,
                          ComPtr<IClipboardTransferDirectory> &aDirectory);
    HRESULT openFile(const com::Utf8Str &aPath,
                     FileAccessMode_T aAccessMode,
                     FileOpenAction_T aOpenAction,
                     FileSharingMode_T aSharingMode,
                     ULONG aCreationMode,
                     ComPtr<IClipboardTransferFile> &aFile);
    HRESULT createDirectory(const com::Utf8Str &aPath);
    /** @} */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /**
     * Returns root nodes from one parent-owned backing transfer.
     *
     * @returns COM status code.
     * @param   pTransfer       Retained backing transfer.
     * @param   aNodes          Where to return the root nodes.
     */
    HRESULT i_roots(PSHCLTRANSFER pTransfer,
                    std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aNodes);
#endif

    struct Data
    {
        /** Unique transfer identifier. */
        ULONG mId;
        /** Clipboard transfer direction. */
        ClipboardTransferDirection_T mDirection;
        /** Clipboard source owning or advertising the transfer. */
        ClipboardSource_T mSource;
        /** Clipboard transfer action. */
        ClipboardAction_T mAction;
        /** Current public transfer state. */
        ClipboardTransferState_T mState;
        /** Clipboard item being transferred. */
        ComPtr<IClipboardItem> mItem;
        /** Progress object for the transfer. */
        ComPtr<IProgress> mProgress;
        /** Optional transfer status or interaction message. */
        com::Utf8Str mMessage;
        /** Last transfer error. */
        ClipboardError_T mError;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /** Source-side local paths explicitly configured for this transfer. */
        std::vector<com::Utf8Str> mSourcePaths;
        /** Parent-owned Shared Clipboard transfer backing data-plane operations. Optional. */
        PSHCLTRANSFER mTransfer;
        /**
         * Whether this object owns and destroys mTransfer.  Otherwise the
         * object keeps its parent alive while using this borrowed transfer.
         */
        bool mfOwnTransfer;
#endif
    } mData;
};

#endif /* !MAIN_INCLUDED_ClipboardTransferImpl_h */
