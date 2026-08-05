/* $Id: ClipboardTransferDataImpl.h 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard transfer data plane object.
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

#ifndef MAIN_INCLUDED_ClipboardTransferDataImpl_h
#define MAIN_INCLUDED_ClipboardTransferDataImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardTransferDataWrap.h"

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

#include <vector>

/**
 * Clipboard transfer low-level data plane object.
 */
class ATL_NO_VTABLE ClipboardTransferData :
    public ClipboardTransferDataWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ClipboardTransferData)

    HRESULT FinalConstruct();
    void FinalRelease();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HRESULT init(const ComPtr<IClipboardTransfer> &aParent,
                 PSHCLTRANSFER aTransfer);
#endif
    void uninit();

private:

    /** @name Wrapped IClipboardTransferData methods
     * @{ */
    HRESULT open(ClipboardTransferDataType_T aType,
                 const com::Utf8Str &aPath,
                 const com::Utf8Str &aFilter,
                 ULONG aFlags,
                 LONG64 *aHandle);
    HRESULT close(ClipboardTransferDataType_T aType,
                  LONG64 aHandle);
    HRESULT read(ClipboardTransferDataType_T aType,
                 LONG64 aHandle,
                 ULONG aSize,
                 ULONG aFlags,
                 com::Utf8Str &aName,
                 ULONG *aInfoFlags,
                 std::vector<BYTE> &aInfo,
                 std::vector<BYTE> &aData);
    HRESULT write(ClipboardTransferDataType_T aType,
                  LONG64 aHandle,
                  const com::Utf8Str &aName,
                  ULONG aInfoFlags,
                  const std::vector<BYTE> &aInfo,
                  const std::vector<BYTE> &aData,
                  ULONG aFlags,
                  ULONG *aWritten);
    /** @} */

    struct Data
    {
        Data()
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            : mTransfer(NULL)
#endif
        { }

        /** Parent transfer object used to preserve the public object relationship. */
        ComPtr<IClipboardTransfer> mParent;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        /** Parent-owned Shared Clipboard transfer backing data-plane operations. */
        PSHCLTRANSFER              mTransfer;
#endif
    } mData;
};

#endif /* !MAIN_INCLUDED_ClipboardTransferDataImpl_h */
