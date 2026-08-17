/* $Id: ClipboardSessionImpl.h 115056 2026-08-17 16:44:52Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Clipboard session API object.
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

#ifndef MAIN_INCLUDED_ClipboardSessionImpl_h
#define MAIN_INCLUDED_ClipboardSessionImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ClipboardSessionWrap.h"
#include "EventImpl.h"
#include "HostClipboardImpl.h"

#include <VBox/GuestHost/SharedClipboard.h>

#include <vector>

class Clipboard;

/**
 * Session-aware clipboard endpoint.
 */
class ATL_NO_VTABLE ClipboardSession :
    public ClipboardSessionWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ClipboardSession)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFlags, Clipboard *aParent);
#ifdef UNIT_TEST
    /**
     * Initializes a parentless session for testing the public session object's own state.
     *
     * @returns COM status code.
     * @param   aClientId           Main clipboard client ID represented by the session.
     * @param   fFlags              IClipboardSessionFlag mask.
     */
    HRESULT initForTesting(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFlags);
#endif
    void uninit();

    HRESULT i_onEventSourceChanged(const ComPtr<IEventListener> &aListener, BOOL fAdded);

private:

    /** @name Wrapped IClipboardSession properties and methods
     * @{ */
    HRESULT getId(ULONG *aId);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getHostClipboard(ComPtr<IHostClipboard> &aHostClipboard);
    HRESULT readFormats(std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT readDataRaw(ClipboardAction_T aAction,
                        const com::Utf8Str &aRequestedMimeType,
                        ClipboardSource_T *aSource,
                        com::Utf8Str &aMimeType,
                        std::vector<BYTE> &aBuffer);
    HRESULT writeDataRaw(ClipboardAction_T aAction,
                         ClipboardSource_T aSource,
                         const com::Utf8Str &aMimeType,
                         const std::vector<BYTE> &aBuffer,
                         ClipboardSource_T *aWrittenSource,
                         com::Utf8Str &aWrittenMimeType,
                         std::vector<BYTE> &aWrittenBuffer);
    HRESULT writeFormats(const std::vector<ComPtr<IClipboardFormat> > &aFormats);
    HRESULT close();
    /** @} */

    struct Data
    {
        Data()
            : mClientId(VBOX_SHCL_MAIN_CLIENT_NONE)
            , mfFlags(0)
            , mParent(NULL)
            , mfInitialStateDelivered(false)
        { }

        /** Main clipboard client ID this session is associated with. */
        VBOXSHCLMAINCLIENTID mClientId;
        /** IClipboardSessionFlag mask for this session. */
        uint32_t mfFlags;
        /** Session-specific event source. */
        ComObjPtr<EventSource> mEventSource;
        /** Session-specific host clipboard endpoint. */
        ComObjPtr<HostClipboard> mHostClipboard;
        /** Internal event source change listener used for IncludeInitialState. */
        ComPtr<IEventListener> mEventSourceChangedListener;
        /** Parent clipboard object. */
        Clipboard *mParent;
        /** One-shot IncludeInitialState delivery state. */
        bool mfInitialStateDelivered;
    } mData;
};

#endif /* !MAIN_INCLUDED_ClipboardSessionImpl_h */
