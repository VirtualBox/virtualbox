/* $Id: ClipboardTransferDirectoryImpl.h 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer directory handle.
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

#ifndef MAIN_INCLUDED_ClipboardTransferDirectoryImpl_h
#define MAIN_INCLUDED_ClipboardTransferDirectoryImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardTransferDirectoryWrap.h"

#include <VBox/GuestHost/SharedClipboard-transfers.h>

class ClipboardTransfer;

/**
 * Clipboard transfer directory handle.
 */
class ATL_NO_VTABLE ClipboardTransferDirectory :
    public ClipboardTransferDirectoryWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ClipboardTransferDirectory)

    HRESULT FinalConstruct();
    void FinalRelease();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT init(const ComObjPtr<ClipboardTransfer> &aParent,
                 PSHCLTRANSFER aTransfer,
                 const com::Utf8Str &aPath,
                 SHCLLISTHANDLE aHandle);
#endif
    void uninit();

private:

    HRESULT getDirectoryName(com::Utf8Str &aDirectoryName);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getFilter(com::Utf8Str &aFilter);
    HRESULT getId(ULONG *aId);
    HRESULT getStatus(DirectoryStatus_T *aStatus);
    HRESULT close();
    HRESULT list(ULONG aMaxEntries,
                 std::vector<ComPtr<IFsObjInfo> > &aObjInfo);
    HRESULT read(ComPtr<IFsObjInfo> &aObjInfo);
    HRESULT rewind();
    HRESULT getPath(com::Utf8Str &aPath);
    HRESULT listEx(ULONG aMaxEntries,
                   ULONG aFlags,
                   std::vector<ComPtr<IClipboardTransferFsObjInfo> > &aEntries);

    struct Data
    {
        Data()
            : mTransfer(NULL)
            , mHandle(NIL_SHCLLISTHANDLE)
            , mStatus(DirectoryStatus_Undefined)
        { }

        /** Concrete parent transfer object keeping the borrowed transfer alive. */
        ComObjPtr<ClipboardTransfer> mParent;
        /** Parent-owned Shared Clipboard transfer owning mHandle. */
        PSHCLTRANSFER                mTransfer;
        SHCLLISTHANDLE               mHandle;
        com::Utf8Str                 mPath;
        DirectoryStatus_T            mStatus;
    } mData;
};

#endif /* !MAIN_INCLUDED_ClipboardTransferDirectoryImpl_h */
