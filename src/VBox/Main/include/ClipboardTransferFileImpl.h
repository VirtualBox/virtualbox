/* $Id: ClipboardTransferFileImpl.h 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer file handle.
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

#ifndef MAIN_INCLUDED_ClipboardTransferFileImpl_h
#define MAIN_INCLUDED_ClipboardTransferFileImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardTransferFileWrap.h"

#include <VBox/GuestHost/SharedClipboard-transfers.h>

/**
 * Clipboard transfer file handle.
 */
class ATL_NO_VTABLE ClipboardTransferFile :
    public ClipboardTransferFileWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ClipboardTransferFile)

    HRESULT FinalConstruct();
    void FinalRelease();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT init(const ComPtr<IClipboardTransfer> &aParent,
                 PSHCLTRANSFER aTransfer,
                 SHCLOBJHANDLE aHandle,
                 const com::Utf8Str &aPath,
                 const SHCLFSOBJINFO &aInfo,
                 FileAccessMode_T aAccessMode,
                 FileOpenAction_T aOpenAction,
                 FileSharingMode_T aSharingMode,
                 ULONG aCreationMode);
#endif
    void uninit();

private:

    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getId(ULONG *aId);
    HRESULT getInitialSize(LONG64 *aInitialSize);
    HRESULT getOffset(LONG64 *aOffset);
    HRESULT getStatus(FileStatus_T *aStatus);
    HRESULT getFilename(com::Utf8Str &aFilename);
    HRESULT getCreationMode(ULONG *aCreationMode);
    HRESULT getOpenAction(FileOpenAction_T *aOpenAction);
    HRESULT getAccessMode(FileAccessMode_T *aAccessMode);
    HRESULT close();
    HRESULT queryInfo(ComPtr<IFsObjInfo> &aObjInfo);
    HRESULT querySize(LONG64 *aSize);
    HRESULT read(ULONG aToRead,
                 ULONG aTimeoutMS,
                 std::vector<BYTE> &aData);
    HRESULT readAt(LONG64 aOffset,
                   ULONG aToRead,
                   ULONG aTimeoutMS,
                   std::vector<BYTE> &aData);
    HRESULT seek(LONG64 aOffset,
                 FileSeekOrigin_T aWhence,
                 LONG64 *aNewOffset);
    HRESULT setACL(const com::Utf8Str &aAcl,
                   ULONG aMode);
    HRESULT setSize(LONG64 aSize);
    HRESULT write(const std::vector<BYTE> &aData,
                  ULONG aTimeoutMS,
                  ULONG *aWritten);
    HRESULT writeAt(LONG64 aOffset,
                    const std::vector<BYTE> &aData,
                    ULONG aTimeoutMS,
                    ULONG *aWritten);
    HRESULT getPath(com::Utf8Str &aPath);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT i_reopenAt(uint64_t offNew);
#endif

    struct Data
    {
        Data()
            : mTransfer(NULL)
            , mHandle(NIL_SHCLOBJHANDLE)
            , mOffset(0)
            , mStatus(FileStatus_Undefined)
            , mAccessMode(FileAccessMode_ReadOnly)
            , mOpenAction(FileOpenAction_OpenExisting)
            , mSharingMode(FileSharingMode_All)
            , mCreationMode(0)
        { RT_ZERO(mInfo); }

        /** Parent transfer object. */
        ComPtr<IClipboardTransfer> mParent;
        /** Retained Shared Clipboard transfer owning mHandle. */
        PSHCLTRANSFER              mTransfer;
        SHCLOBJHANDLE              mHandle;
        com::Utf8Str               mPath;
        SHCLFSOBJINFO              mInfo;
        LONG64                     mOffset;
        FileStatus_T               mStatus;
        FileAccessMode_T           mAccessMode;
        FileOpenAction_T           mOpenAction;
        FileSharingMode_T          mSharingMode;
        ULONG                      mCreationMode;
    } mData;
};

#endif /* !MAIN_INCLUDED_ClipboardTransferFileImpl_h */
