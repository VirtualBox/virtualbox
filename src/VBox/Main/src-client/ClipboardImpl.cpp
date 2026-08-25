/* $Id: ClipboardImpl.cpp 115131 2026-08-25 17:30:42Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Console clipboard API.
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

#include "ClipboardImpl.h"
#include "AutoCaller.h"

#include <iprt/errcore.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD
# include "ClipboardFormatImpl.h"
# include "ClipboardItemImpl.h"
# include "ClipboardSessionImpl.h"
# include "ClipboardTransferImpl.h"
# include "ClipboardTransferManagerImpl.h"
# include "ConsoleClipboardFormats.h"
# include "ConsoleImpl.h"
# include "EventImpl.h"
# include "GuestShClHelpers.h"
# include "GuestShClPrivate.h"
# include "HostClipboardImpl.h"
# include "VMMDev.h"
# include "VBoxEvents.h"

# include <iprt/mem.h>
# include <iprt/string.h>

# include <new>
# include <iprt/utf16.h>
# include <VBox/HostServices/VBoxClipboardSvc.h>
# include <VBox/param.h>

/** Maximum buffer size the Main API live clipboard read path will request or cache. */
static uint32_t const s_cbClipboardReadMax = _64M;
/** Maximum number of non-waitable clipboard data requests kept pending. */
static size_t const s_cClipboardPendingRequestsMax = 32;

/**
 * Selects the preferred single Shared Clipboard format from a format mask.
 *
 * @returns Shared Clipboard format bit, or VBOX_SHCL_FMT_NONE if no supported
 *          format is present.
 * @param   fFormats        Shared Clipboard format mask.
 */
static SHCLFORMAT clipboardPickFormat(SHCLFORMATS fFormats)
{
    if (fFormats & VBOX_SHCL_FMT_UNICODETEXT)
        return VBOX_SHCL_FMT_UNICODETEXT;
    if (fFormats & VBOX_SHCL_FMT_HTML)
        return VBOX_SHCL_FMT_HTML;
    if (fFormats & VBOX_SHCL_FMT_BITMAP)
        return VBOX_SHCL_FMT_BITMAP;
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
        return VBOX_SHCL_FMT_URI_LIST;
# endif
    return VBOX_SHCL_FMT_NONE;
}

static const char *clipboardSourceToLogString(ClipboardSource_T enmSource)
{
    switch (enmSource)
    {
        case ClipboardSource_Host:   return "host";
        case ClipboardSource_Guest:  return "guest";
        case ClipboardSource_Remote: return "remote";
        case ClipboardSource_Custom: return "custom";
        default:                     return "unknown";
    }
}

# ifdef LOG_ENABLED
static const char *clipboardActionToLogString(ClipboardAction_T enmAction)
{
    switch (enmAction)
    {
        case ClipboardAction_Copy:   return "copy";
        case ClipboardAction_Cut:    return "cut";
        case ClipboardAction_Paste:  return "paste";
        case ClipboardAction_Custom: return "custom";
        default:                     return "unknown";
    }
}
# endif /* LOG_ENABLED */


/**
 * Converts Shared Clipboard protocol payload data to Main API MIME payload data.
 *
 * @returns VBox status code.
 * @param   uFormat         Shared Clipboard format of the protocol payload.
 * @param   pvData          Protocol payload bytes.
 * @param   cbData          Number of protocol payload bytes.
 * @param   aBuffer         Where to return the Main API payload bytes.
 */
static int clipboardProtocolToMainData(SHCLFORMAT uFormat, const void *pvData, uint32_t cbData, std::vector<BYTE> &aBuffer)
{
    Log3Func(("uFormat=%#x, pvData=%p, cbData=%RU32\n", uFormat, pvData, cbData));

    if (cbData && !pvData)
    {
        LogFunc(("Invalid protocol data pointer: uFormat=%#x, cbData=%RU32\n", uFormat, cbData));
        return VERR_INVALID_POINTER;
    }

    if (uFormat == VBOX_SHCL_FMT_UNICODETEXT)
    {
        if (!cbData)
        {
            aBuffer.clear();
            Log3Func(("Empty Unicode text payload\n"));
            return VINF_SUCCESS;
        }
        if (cbData % sizeof(RTUTF16) != 0)
        {
            LogFunc(("Invalid Unicode text payload size: cbData=%RU32\n", cbData));
            return VERR_INVALID_PARAMETER;
        }

        PCRTUTF16 const pcwszData = (PCRTUTF16)pvData;
        size_t const cwcData = cbData / sizeof(RTUTF16);
        if (pcwszData[0] == 0xfffe /* UTF-16BE BOM */)
        {
            LogFunc(("Big endian UTF-16 protocol data is not supported: cbData=%RU32\n", cbData));
            return VERR_NOT_SUPPORTED;
        }

        try
        {
            std::vector<RTUTF16> awcConverted;
            awcConverted.reserve(cwcData + 1 /* terminator */);
            for (size_t i = pcwszData[0] == 0xfeff /* UTF-16LE BOM */ ? 1 : 0; i < cwcData; i++)
            {
                RTUTF16 const wc = pcwszData[i];
                if (!wc)
                    break;
                if (   wc == '\r'
                    && i + 1 < cwcData
                    && pcwszData[i + 1] == '\n')
                {
                    awcConverted.push_back('\n');
                    i++;
                }
                else
                    awcConverted.push_back(wc);
            }
            awcConverted.push_back(0);

            char *pszUtf8 = NULL;
            int vrc = RTUtf16ToUtf8(&awcConverted[0], &pszUtf8);
            if (RT_FAILURE(vrc))
            {
                LogFunc(("RTUtf16ToUtf8 failed: uFormat=%#x, cbData=%RU32, vrc=%Rrc\n", uFormat, cbData, vrc));
                return vrc;
            }

            try
            {
                size_t const cbUtf8 = strlen(pszUtf8);
                std::vector<BYTE> abConverted(cbUtf8);
                if (cbUtf8)
                    memcpy(&abConverted[0], pszUtf8, cbUtf8);
                aBuffer.swap(abConverted);
                Log3Func(("Converted Unicode text protocol payload: cbIn=%RU32, cbOut=%zu\n", cbData, aBuffer.size()));
            }
            catch (std::bad_alloc &)
            {
                RTStrFree(pszUtf8);
                return VERR_NO_MEMORY;
            }
            RTStrFree(pszUtf8);
        }
        catch (std::bad_alloc &)
        {
            return VERR_NO_MEMORY;
        }
        return VINF_SUCCESS;
    }

    try
    {
        aBuffer.resize(cbData);
        if (cbData)
            memcpy(&aBuffer[0], pvData, cbData);
        Log3Func(("Copied protocol payload without conversion: uFormat=%#x, cbData=%RU32\n", uFormat, cbData));
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


/**
 * Converts Main API MIME payload data to Shared Clipboard protocol payload data.
 *
 * @returns VBox status code.
 * @param   uFormat         Shared Clipboard format to produce.
 * @param   aBuffer         Main API payload bytes.
 * @param   aProtocolBuffer Where to return the protocol payload bytes.
 */
static int clipboardMainToProtocolData(SHCLFORMAT uFormat, const std::vector<BYTE> &aBuffer, std::vector<BYTE> &aProtocolBuffer)
{
    Log3Func(("uFormat=%#x, cbMain=%zu\n", uFormat, aBuffer.size()));

    if (uFormat == VBOX_SHCL_FMT_UNICODETEXT)
    {
        if (aBuffer.empty())
        {
            aProtocolBuffer.clear();
            Log3Func(("Empty Main Unicode text payload\n"));
            return VINF_SUCCESS;
        }

        PRTUTF16 pwszData = NULL;
        size_t cwcData = 0;
        int vrc = RTStrToUtf16Ex((const char *)&aBuffer[0], aBuffer.size(), &pwszData, 0, &cwcData);
        if (RT_FAILURE(vrc))
        {
            LogFunc(("RTStrToUtf16Ex failed: uFormat=%#x, cbMain=%zu, vrc=%Rrc\n", uFormat, aBuffer.size(), vrc));
            return vrc;
        }

        try
        {
            std::vector<RTUTF16> awcConverted;
            awcConverted.reserve(cwcData * 2 + 1 /* terminator */);
            for (size_t i = pwszData[0] == 0xfeff /* UTF-16LE BOM */ ? 1 : 0; i < cwcData; i++)
            {
                RTUTF16 const wc = pwszData[i];
                if (!wc)
                    break;
                if (   wc == '\n'
                    && (i == 0 || pwszData[i - 1] != '\r'))
                    awcConverted.push_back('\r');
                awcConverted.push_back(wc);
            }
            awcConverted.push_back(0);

            size_t const cbData = awcConverted.size() * sizeof(RTUTF16);
            if (cbData > UINT32_MAX)
            {
                LogFunc(("Converted Unicode text protocol payload is too large: cbData=%zu\n", cbData));
                vrc = VERR_TOO_MUCH_DATA;
            }
            if (RT_SUCCESS(vrc))
            {
                std::vector<BYTE> abConverted(cbData);
                memcpy(&abConverted[0], &awcConverted[0], cbData);
                aProtocolBuffer.swap(abConverted);
                Log3Func(("Converted Main Unicode text payload: cbIn=%zu, cbOut=%zu\n", aBuffer.size(), aProtocolBuffer.size()));
            }
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        RTUtf16Free(pwszData);
        return vrc;
    }

    try
    {
        aProtocolBuffer = aBuffer;
        Log3Func(("Copied Main payload without conversion: uFormat=%#x, cbData=%zu\n", uFormat, aBuffer.size()));
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD */


struct Clipboard::Data
{
    Data()
        : mParent(NULL)
#ifdef VBOX_WITH_SHARED_CLIPBOARD
        , mSource(ClipboardSource_Custom)
        , mCurrentClientId(VBOX_SHCL_MAIN_CLIENT_NONE)
        , mfHaveLastItem(false)
        , mLastItemSource(ClipboardSource_Custom)
        , mLastItemAction(ClipboardAction_Invalid)
        , mLastItemSerial(0)
        , mNextItemId(0)
        , mNextRequestId(1)
        , mNextClientId(1)
        , mNextEventRevision(1)
#endif
    { }

    /** Parent console. */
    Console *mParent;
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    /** Transfer manager object. */
    ComObjPtr<ClipboardTransferManager> mTransfers;
    /** Clipboard event source. */
    ComObjPtr<EventSource> mEventSource;
    /** Native host clipboard endpoint object. */
    ComObjPtr<HostClipboard> mHostClipboard;
    /** Last known active clipboard source. */
    ClipboardSource_T mSource;
    /** Client identifier which owns the current format offer. */
    VBOXSHCLMAINCLIENTID mCurrentClientId;
    /** Whether last clipboard item data is available. */
    bool mfHaveLastItem;
    /** Source of the last clipboard item data. */
    ClipboardSource_T mLastItemSource;
    /** Action associated with the last clipboard item data. */
    ClipboardAction_T mLastItemAction;
    /** MIME type of the last clipboard item data. */
    com::Utf8Str mLastItemMimeType;
    /** Payload bytes of the last clipboard item data. */
    std::vector<BYTE> mLastItemBuffer;
    /** Serial number incremented whenever clipboard item data is stored or invalidated. */
    uint64_t mLastItemSerial;
    /** Pending non-waitable clipboard data request. */
    struct PendingDataRequest
    {
        ULONG                 mId;
        VBOXSHCLMAINCLIENTID  mClientId;
        ClipboardAction_T     mAction;
        ClipboardSource_T     mSource;
        SHCLFORMAT            mFormat;
    };
    /** Pending non-waitable clipboard data requests. */
    std::vector<PendingDataRequest> mPendingDataRequests;
    /** Active clipboard session event forwarding record. */
    struct SessionRecord
    {
        SessionRecord(VBOXSHCLMAINCLIENTID aClientId,
                      ClipboardSession *aSession,
                      uint32_t fFlags,
                      const ComPtr<IEventSource> &aEventSource)
            : mClientId(aClientId)
            , mSession(aSession)
            , mfFlags(fFlags)
            , mEventSource(aEventSource)
            , mfInitialStateDelivered(false)
            , mfHaveReflectionFormats(false)
            , mfReflectionFormats(VBOX_SHCL_FMT_NONE)
            , mReflectionSource(ClipboardSource_Custom)
        { }

        /** Session client identifier. */
        VBOXSHCLMAINCLIENTID mClientId;
        /** Session object owned by the parent clipboard until close or parent teardown. */
        ComObjPtr<ClipboardSession> mSession;
        /** IClipboardSessionFlag mask. */
        uint32_t mfFlags;
        /** Session-specific event source. */
        ComPtr<IEventSource> mEventSource;
        /** Whether IncludeInitialState has been delivered for this session. */
        bool mfInitialStateDelivered;
        /** Whether a one-shot anonymous format reflection candidate is available. */
        bool mfHaveReflectionFormats;
        /** Format mask for the reflection candidate. */
        uint32_t mfReflectionFormats;
        /** Source for the reflection candidate. */
        ClipboardSource_T mReflectionSource;
    };
    /** Active clipboard sessions. */
    std::vector<SessionRecord> mSessions;
    /** Next clipboard item identifier to assign. */
    ULONG mNextItemId;
    /** Next clipboard data request identifier to assign. */
    ULONG mNextRequestId;
    /** Next Main clipboard client identifier to assign. */
    VBOXSHCLMAINCLIENTID mNextClientId;
    /** Next clipboard event revision to assign. */
    LONG64 mNextEventRevision;
#endif
};


/**
 * Constructs the console clipboard object.
 */
Clipboard::Clipboard()
    : mData(NULL)
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    , m_fFormats(VBOX_SHCL_FMT_NONE)
#endif
{
}


/**
 * Destroys the console clipboard object.
 */
Clipboard::~Clipboard()
{
}


/**
 * Completes construction of the console clipboard object.
 *
 * @retval  S_OK                if the transfer status was accepted or safely ignored.
 * @retval  E_INVALIDARG        if the key, source, status, result, direction, or transition is invalid.
 * @retval  E_FAIL              if clipboard state or required transfer state is unavailable.
 * @retval  E_OUTOFMEMORY       if a transfer record or publication cannot be allocated.
 * @returns                     A failure status from creating or initializing the transfer's Main objects.
 */
HRESULT Clipboard::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases the console clipboard object.
 */
void Clipboard::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


/**
 * Initializes the console clipboard object.
 *
 * @retval  S_OK                if progress was accepted or safely ignored.
 * @retval  E_FAIL              if clipboard state or its transfer manager is unavailable.
 * @retval  E_OUTOFMEMORY       if a progress publication cannot be allocated.
 * @param   aParent         Parent console.
 */
HRESULT Clipboard::init(Console *aParent)
{
    Log2Func(("aParent=%p\n", aParent));
    AssertPtrReturn(aParent, E_INVALIDARG);

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData = new Data;
    mData->mParent = aParent;

#ifdef VBOX_WITH_SHARED_CLIPBOARD
    mData->mEventSource.createObject();
    HRESULT hrc = mData->mEventSource->init();
    AssertComRCReturnRC(hrc);

    mData->mTransfers.createObject();
    hrc = mData->mTransfers->init(mData->mEventSource, this);
    AssertComRCReturnRC(hrc);

    mData->mHostClipboard.createObject();
    hrc = mData->mHostClipboard->init(VBOX_SHCL_MAIN_CLIENT_NONE, this);
    AssertComRCReturnRC(hrc);

    m_fFormats = VBOX_SHCL_FMT_NONE;

    Log2Func(("Initialized clipboard object: eventSource=%p, transfers=%p, hostClipboard=%p\n",
              (void *)mData->mEventSource, (void *)mData->mTransfers, (void *)mData->mHostClipboard));
#endif
    autoInitSpan.setSucceeded();
    return S_OK;
}


/**
 * Uninitializes the console clipboard object.
 */
void Clipboard::uninit()
{
    Log2Func(("mData=%p\n", mData));
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    Data *pData = NULL;
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    std::vector<Data::SessionRecord> vecSessions;
    ComObjPtr<HostClipboard> ptrHostClipboard;
    ComObjPtr<ClipboardTransferManager> ptrTransfers;
    ComObjPtr<EventSource> ptrEventSource;
#endif

    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mData)
        {
            pData = mData;
            mData = NULL;
            pData->mParent = NULL;
#ifdef VBOX_WITH_SHARED_CLIPBOARD
            pData->mLastItemBuffer.clear();
            pData->mPendingDataRequests.clear();
            vecSessions.swap(pData->mSessions);
            pData->mfHaveLastItem = false;
            ptrHostClipboard = pData->mHostClipboard;
            pData->mHostClipboard.setNull();
            ptrTransfers = pData->mTransfers;
            pData->mTransfers.setNull();
            ptrEventSource = pData->mEventSource;
            pData->mEventSource.setNull();
#endif
        }
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD
    for (std::vector<Data::SessionRecord>::iterator it = vecSessions.begin(); it != vecSessions.end(); ++it)
        if (it->mSession.isNotNull())
            it->mSession->uninit();

    if (!ptrHostClipboard.isNull())
        ptrHostClipboard->uninit();
    if (!ptrTransfers.isNull())
        ptrTransfers->uninit();
    if (!ptrEventSource.isNull())
        ptrEventSource->uninit();
#endif

    delete pData;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD
/**
 * Creates a clipboard format object.
 *
 * @returns COM status code.
 * @param   aMimeType       MIME type for the format object.
 * @param   aFormat         Where to return the created format object.
 */
HRESULT Clipboard::i_createFormat(const com::Utf8Str &aMimeType, ComPtr<IClipboardFormat> &aFormat)
{
    Log3Func(("aMimeType=%s\n", aMimeType.c_str()));
    ComObjPtr<ClipboardFormat> ptrFormatObj;
    HRESULT hrc = ptrFormatObj.createObject();
    if (FAILED(hrc))
        return hrc;
    hrc = ptrFormatObj->init(aMimeType);
    if (FAILED(hrc))
        return hrc;
    return ptrFormatObj.queryInterfaceTo(aFormat.asOutParam());
}


/**
 * Creates a clipboard item object.
 *
 * @returns COM status code.
 * @param   aSource         Clipboard item source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 * @param   aItem           Where to return the created item object.
 */
HRESULT Clipboard::i_createItem(ClipboardSource_T aSource,
                                const com::Utf8Str &aMimeType,
                                const std::vector<BYTE> &aBuffer,
                                ComPtr<IClipboardItem> &aItem)
{
    ComPtr<IClipboardFormat> ptrFormat;
    HRESULT hrc = i_createFormat(aMimeType, ptrFormat);
    if (FAILED(hrc))
        return hrc;

    ULONG idItem;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        idItem = mData->mNextItemId++;
    }

    ComObjPtr<ClipboardItem> ptrItemObj;
    hrc = ptrItemObj.createObject();
    if (FAILED(hrc))
        return hrc;
    hrc = ptrItemObj->init(idItem, aSource, ptrFormat, aBuffer);
    if (FAILED(hrc))
        return hrc;
    Log2Func(("Created item id=%RU32, source=%RU32, mime=%s, cb=%zu\n",
              (uint32_t)idItem, (uint32_t)aSource, aMimeType.c_str(), aBuffer.size()));
    return ptrItemObj.queryInterfaceTo(aItem.asOutParam());
}


/**
 * Stores the most recent clipboard item payload for later reads and events.
 *
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard data source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 */
void Clipboard::i_storeData(ClipboardAction_T aAction,
                            ClipboardSource_T aSource,
                            const com::Utf8Str &aMimeType,
                            const std::vector<BYTE> &aBuffer)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertPtrReturnVoid(mData);

    i_storeDataLocked(aAction, aSource, aMimeType, aBuffer);
}


/**
 * Stores the most recent clipboard item payload if the expected client still owns it.
 *
 * @returns true if the data was stored, false if the client is stale.
 * @param   aClientId       Expected owning client identifier.
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard data source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 * @param   fFormats        Shared Clipboard format mask expected to be owned.
 */
bool Clipboard::i_storeDataIfCurrentClient(VBOXSHCLMAINCLIENTID aClientId,
                                           ClipboardAction_T aAction,
                                           ClipboardSource_T aSource,
                                           const com::Utf8Str &aMimeType,
                                           const std::vector<BYTE> &aBuffer,
                                           uint32_t fFormats)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertPtrReturn(mData, false);

    if (!i_isClientFormatOwnerLocked(aClientId, fFormats, aSource))
    {
        LogFunc(("Rejecting stale clipboard data store: clientId=%RU32, currentClientId=%RU32, source=%s, formats=%#x\n",
                 aClientId, mData->mCurrentClientId, clipboardSourceToLogString(aSource), fFormats));
        return false;
    }

    i_storeDataLocked(aAction, aSource, aMimeType, aBuffer);
    return true;
}


/**
 * Stores the most recent clipboard item payload while the caller owns the write lock.
 *
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard data source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 */
void Clipboard::i_storeDataLocked(ClipboardAction_T aAction,
                                  ClipboardSource_T aSource,
                                  const com::Utf8Str &aMimeType,
                                  const std::vector<BYTE> &aBuffer)
{
    AssertPtrReturnVoid(mData);

    i_clearPendingDataRequestsLocked();
    mData->mfHaveLastItem = true;
    mData->mLastItemSource = aSource;
    mData->mLastItemAction = aAction;
    mData->mLastItemMimeType = aMimeType;
    mData->mLastItemBuffer = aBuffer;
    mData->mLastItemSerial++;
    Log2Func(("Stored clipboard data: action=%RU32, source=%RU32, mime=%s, cb=%zu, serial=%RU64\n",
              (uint32_t)aAction, (uint32_t)aSource, aMimeType.c_str(), aBuffer.size(),
              mData->mLastItemSerial));
}


/**
 * Clears pending non-waitable clipboard data requests while the caller owns the write lock.
 */
void Clipboard::i_clearPendingDataRequestsLocked()
{
    AssertPtrReturnVoid(mData);
    if (!mData->mPendingDataRequests.empty())
    {
        Log2Func(("Clearing %zu pending clipboard data requests\n", mData->mPendingDataRequests.size()));
        mData->mPendingDataRequests.clear();
    }
}


/**
 * Removes a pending non-waitable clipboard data request while the caller owns the write lock.
 *
 * @param   aRequestId      Request identifier to remove.
 */
void Clipboard::i_removePendingDataRequestLocked(ULONG aRequestId)
{
    AssertPtrReturnVoid(mData);
    for (std::vector<Data::PendingDataRequest>::iterator it = mData->mPendingDataRequests.begin();
         it != mData->mPendingDataRequests.end(); ++it)
    {
        if (it->mId == aRequestId)
        {
            mData->mPendingDataRequests.erase(it);
            return;
        }
    }
}


/**
 * Removes a pending non-waitable clipboard data request.
 *
 * @param   aRequestId      Request identifier to remove.
 */
void Clipboard::i_removePendingDataRequest(ULONG aRequestId)
{
    if (aRequestId == 0)
        return;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData)
        i_removePendingDataRequestLocked(aRequestId);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD */


/**
 * Creates a public clipboard format object.
 *
 * @returns COM status code.
 * @param   aMimeType       MIME type for the format object.
 * @param   aFormat         Where to return the created format object.
 */
HRESULT Clipboard::createFormat(const com::Utf8Str &aMimeType,
                                ComPtr<IClipboardFormat> &aFormat)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aMimeType, aFormat);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aMimeType=%s\n", aMimeType.c_str()));
    if (aMimeType.isEmpty())
    {
        LogFunc(("Rejecting empty clipboard MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }

    HRESULT hrc = i_createFormat(aMimeType, aFormat);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Creating the clipboard format object failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Creates a client-specific clipboard session.
 *
 * @returns COM status code.
 * @param   aFlags          Session flags.
 * @param   aSession        Where to return the created session object.
 */
HRESULT Clipboard::createSession(const std::vector<IClipboardSessionFlag_T> &aFlags,
                                 ComPtr<IClipboardSession> &aSession)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aFlags, aSession);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    uint32_t fFlags = 0;
    for (std::vector<IClipboardSessionFlag_T>::const_iterator it = aFlags.begin(); it != aFlags.end(); ++it)
    {
        switch (*it)
        {
            case IClipboardSessionFlag_ExcludeOwnChanges:
            case IClipboardSessionFlag_ExcludeReflections:
            case IClipboardSessionFlag_IncludeInitialState:
            case IClipboardSessionFlag_IncludePayload:
                fFlags |= (uint32_t)*it;
                break;
            default:
                LogFunc(("Rejecting invalid clipboard session flag %RU32\n", (uint32_t)*it));
                return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard session flag %RU32"), (uint32_t)*it);
        }
    }

    VBOXSHCLMAINCLIENTID const idClient = i_allocateClientId();
    if (idClient == VBOX_SHCL_MAIN_CLIENT_NONE)
        return setError(E_OUTOFMEMORY, tr("No clipboard session client identifiers are available"));

    ComObjPtr<ClipboardSession> ptrSessionObj;
    HRESULT hrc = ptrSessionObj.createObject();
    if (FAILED(hrc))
        return hrc;
    hrc = ptrSessionObj->init(idClient, fFlags, this);
    if (FAILED(hrc))
        return hrc;
    hrc = ptrSessionObj.queryInterfaceTo(aSession.asOutParam());
    if (FAILED(hrc))
    {
        ptrSessionObj->uninit();
        return hrc;
    }
    Log2Func(("Created clipboard session: idClient=%RU32, fFlags=%#x\n", idClient, fFlags));
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Creates a public clipboard item object.
 *
 * @returns COM status code.
 * @param   aSource         Clipboard item source.
 * @param   aFormat         Clipboard format object.
 * @param   aBuffer         Payload bytes.
 * @param   aItem           Where to return the created item object.
 */
HRESULT Clipboard::createItem(ClipboardSource_T aSource,
                              const ComPtr<IClipboardFormat> &aFormat,
                              const std::vector<BYTE> &aBuffer,
                              ComPtr<IClipboardItem> &aItem)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aSource, aFormat, aBuffer, aItem);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aSource=%RU32, aFormat=%p, cb=%zu\n", (uint32_t)aSource, (void *)aFormat, aBuffer.size()));

    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }

    if (aFormat.isNull())
    {
        LogFunc(("Rejecting NULL clipboard format\n"));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard format must not be NULL"));
    }

    com::Bstr bstrMimeType;
    HRESULT hrc = aFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard format MIME type failed (%Rhrc)"), hrc);

    com::Utf8Str strMimeType(bstrMimeType);
    if (strMimeType.isEmpty())
    {
        LogFunc(("Rejecting clipboard item with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }

    hrc = i_createItem(aSource, strMimeType, aBuffer, aItem);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Creating the clipboard item object failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Returns the clipboard transfer manager.
 *
 * @returns COM status code.
 * @param   aTransfers      Where to return the transfer manager.
 */
HRESULT Clipboard::getTransfers(ComPtr<IClipboardTransferManager> &aTransfers)
{
#if !defined(VBOX_WITH_SHARED_CLIPBOARD) || !defined(VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS)
    RT_NOREF(aTransfers);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD && VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
    HRESULT hrc = mData->mTransfers.queryInterfaceTo(aTransfers.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the clipboard transfer manager failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD && VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/**
 * Returns the clipboard event source.
 *
 * @returns COM status code.
 * @param   aEventSource    Where to return the event source.
 */
HRESULT Clipboard::getEventSource(ComPtr<IEventSource> &aEventSource)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aEventSource);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
    HRESULT hrc = mData->mEventSource.queryInterfaceTo(aEventSource.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the clipboard event source failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Returns the native host clipboard endpoint.
 *
 * @returns COM status code.
 * @param   aHostClipboard  Where to return the host clipboard endpoint.
 */
HRESULT Clipboard::getHostClipboard(ComPtr<IHostClipboard> &aHostClipboard)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aHostClipboard);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
    HRESULT hrc = mData->mHostClipboard.queryInterfaceTo(aHostClipboard.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the host clipboard endpoint failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}




/**
 * Reads clipboard data as a clipboard item.
 *
 * @returns COM status code.
 * @param   aAction         Clipboard action to read for.
 * @param   aItem           Where to return the clipboard item.
 */
HRESULT Clipboard::readData(ClipboardAction_T aAction, ComPtr<IClipboardItem> &aItem)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aAction, aItem);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aAction=%RU32\n", (uint32_t)aAction));
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }

    HRESULT hrc = S_OK;
    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    ClipboardSource_T enmReportedSource = ClipboardSource_Custom;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        enmReportedSource = mData->mSource;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsRead(enmMode, enmReportedSource))
    {
        LogFunc(("Rejecting read for reported source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)enmReportedSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)enmReportedSource);
    }

    ClipboardSource_T enmSource = ClipboardSource_Host;
    com::Utf8Str strMimeType;
    std::vector<BYTE> abBuffer;
    hrc = i_readData(aAction, &enmSource, strMimeType, abBuffer);
    if (FAILED(hrc))
    {
        LogFunc(("i_readData failed: action=%RU32, hrc=%#x\n", (uint32_t)aAction, hrc));
        return setError(hrc, tr("Reading shared clipboard data failed (%Rhrc)"), hrc);
    }
    if (!ShClMainIsValidSource(enmSource))
    {
        LogFunc(("Read returned invalid clipboard source %RU32\n", (uint32_t)enmSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)enmSource);
    }

    if (!ShClMainModeAllowsRead(enmMode, enmSource))
    {
        LogFunc(("Rejecting read from source %RU32 in clipboard mode %RU32\n", (uint32_t)enmSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)enmSource);
    }
    Log2Func(("Read data: action=%RU32, source=%RU32, mime=%s, cb=%zu\n",
              (uint32_t)aAction, (uint32_t)enmSource, strMimeType.c_str(), abBuffer.size()));
    hrc = i_createItem(enmSource, strMimeType, abBuffer, aItem);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Creating the clipboard item object failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Reads the currently available clipboard formats.
 *
 * @returns COM status code.
 * @param   aFormats        Where to return the available formats.
 */
HRESULT Clipboard::readFormats(std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aFormats);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log3Func(("\n"));
    HRESULT hrc = S_OK;
    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        enmSource = mData->mSource;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsRead(enmMode, enmSource))
    {
        LogFunc(("Rejecting format read for source %RU32 in clipboard mode %RU32\n", (uint32_t)enmSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)enmSource);
    }

    std::vector<com::Utf8Str> aMimeTypes;
    hrc = i_readFormats(aMimeTypes);
    if (FAILED(hrc))
    {
        LogFunc(("i_readFormats failed: hrc=%#x\n", hrc));
        return setError(hrc, tr("Reading shared clipboard formats failed (%Rhrc)"), hrc);
    }

    Log2Func(("Read %zu MIME formats\n", aMimeTypes.size()));
    aFormats.clear();
    for (std::vector<com::Utf8Str>::const_iterator it = aMimeTypes.begin(); it != aMimeTypes.end(); ++it)
    {
        ComPtr<IClipboardFormat> ptrFormat;
        hrc = i_createFormat(*it, ptrFormat);
        if (FAILED(hrc))
            return setError(VBOX_E_SHCL_ERROR, tr("Creating the clipboard format object failed (%Rhrc)"), hrc);
        aFormats.push_back(ptrFormat);
    }
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Reads the currently available clipboard formats for internal session use.
 *
 * @returns COM status code.
 * @param   aFormats        Where to return the available formats.
 */
HRESULT Clipboard::i_readFormatObjects(std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
    return readFormats(aFormats);
}


/**
 * Reads raw clipboard data for internal session use.
 *
 * @returns COM status code.
 * @param   aAction             Clipboard action to read for.
 * @param   aRequestedMimeType  MIME type to read, or empty to auto-select.
 * @param   aSource             Where to return the clipboard source.
 * @param   aMimeType           Where to return the MIME type.
 * @param   aBuffer             Where to return the payload bytes.
 */
HRESULT Clipboard::i_readDataRaw(ClipboardAction_T aAction,
                                 const com::Utf8Str &aRequestedMimeType,
                                 ClipboardSource_T *aSource,
                                 com::Utf8Str &aMimeType,
                                 std::vector<BYTE> &aBuffer)
{
    return readDataRaw(aAction, aRequestedMimeType, aSource, aMimeType, aBuffer);
}


/**
 * Writes clipboard data from a clipboard item.
 *
 * @returns COM status code.
 * @param   aAction         Clipboard action to write for.
 * @param   aItem           Clipboard item to write.
 * @param   aWrittenItem    Where to return the written clipboard item.
 */
HRESULT Clipboard::writeData(ClipboardAction_T aAction,
                             const ComPtr<IClipboardItem> &aItem,
                             ComPtr<IClipboardItem> &aWrittenItem)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aAction, aItem, aWrittenItem);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aAction=%RU32, aItem=%p\n", (uint32_t)aAction, (void *)aItem));
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (aItem.isNull())
    {
        LogFunc(("Invalid NULL clipboard item for writeData\n"));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard item must not be NULL"));
    }

    ClipboardSource_T enmSource = ClipboardSource_Host;
    HRESULT hrc = aItem->COMGETTER(Source)(&enmSource);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard item source failed (%Rhrc)"), hrc);
    if (!ShClMainIsValidSource(enmSource))
    {
        LogFunc(("Rejecting invalid clipboard source %RU32\n", (uint32_t)enmSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)enmSource);
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, enmSource))
    {
        LogFunc(("Rejecting write from source %RU32 in clipboard mode %RU32\n", (uint32_t)enmSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)enmSource);
    }

    ComPtr<IClipboardFormat> ptrFormat;
    hrc = aItem->COMGETTER(Format)(ptrFormat.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard item format failed (%Rhrc)"), hrc);
    if (ptrFormat.isNull())
    {
        LogFunc(("Clipboard item has no format\n"));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard item format must not be NULL"));
    }

    com::Bstr bstrMimeType;
    hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard format MIME type failed (%Rhrc)"), hrc);
    com::Utf8Str strMimeType(bstrMimeType);
    if (strMimeType.isEmpty())
    {
        LogFunc(("Rejecting clipboard write with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }
    if (consoleClipboardMimeTypeToFormat(strMimeType) == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Rejecting unsupported clipboard MIME type for write: %s\n", strMimeType.c_str()));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                        tr("Clipboard MIME type '%s' is not supported"), strMimeType.c_str());
    }

    com::SafeArray<BYTE> aSafeBuffer;
    hrc = aItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aSafeBuffer));
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard item buffer failed (%Rhrc)"), hrc);
    std::vector<BYTE> abBuffer(aSafeBuffer.size());
    if (!abBuffer.empty())
        memcpy(&abBuffer[0], aSafeBuffer.raw(), abBuffer.size());
    if (abBuffer.empty())
    {
        LogFunc(("Rejecting empty clipboard write: mime=%s\n", strMimeType.c_str()));
        return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard write data must not be empty"));
    }
    if (abBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Rejecting oversized clipboard write: mime=%s, cb=%zu, max=%RU32\n",
                 strMimeType.c_str(), abBuffer.size(), s_cbClipboardReadMax));
        return setError(VBOX_E_SHCL_TOO_MUCH_DATA,
                        tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                        s_cbClipboardReadMax);
    }

    ClipboardSource_T enmWrittenSource = ClipboardSource_Host;
    com::Utf8Str strWrittenMimeType;
    std::vector<BYTE> abWrittenBuffer;
    hrc = i_writeData(VBOX_SHCL_MAIN_CLIENT_NONE, aAction, enmSource, strMimeType, abBuffer,
                      &enmWrittenSource, strWrittenMimeType, abWrittenBuffer);
    if (FAILED(hrc))
    {
        LogFunc(("i_writeData failed: action=%RU32, source=%RU32, mime=%ls, cb=%zu, hrc=%#x\n",
                 (uint32_t)aAction, (uint32_t)enmSource, bstrMimeType.raw(), abBuffer.size(), hrc));
        return setError(hrc, tr("Writing shared clipboard data failed (%Rhrc)"), hrc);
    }
    Log2Func(("Wrote data: action=%RU32, source=%RU32->%RU32, mime=%s, cb=%zu\n",
              (uint32_t)aAction, (uint32_t)enmSource, (uint32_t)enmWrittenSource,
              strWrittenMimeType.c_str(), abWrittenBuffer.size()));
    hrc = i_createItem(enmWrittenSource, strWrittenMimeType, abWrittenBuffer, aWrittenItem);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Creating the written clipboard item object failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Reads raw clipboard data.
 *
 * @returns COM status code.
 * @param   aAction             Clipboard action to read for.
 * @param   aRequestedMimeType  MIME type to read, or empty to auto-select.
 * @param   aSource             Where to return the clipboard source.
 * @param   aMimeType           Where to return the MIME type.
 * @param   aBuffer             Where to return the payload bytes.
 */
HRESULT Clipboard::readDataRaw(ClipboardAction_T aAction,
                               const com::Utf8Str &aRequestedMimeType,
                               ClipboardSource_T *aSource,
                               com::Utf8Str &aMimeType,
                               std::vector<BYTE> &aBuffer)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aAction, aRequestedMimeType, aSource, aMimeType, aBuffer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aAction=%RU32, aRequestedMimeType=%s\n", (uint32_t)aAction, aRequestedMimeType.c_str()));
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (!aSource)
        return setError(VBOX_E_SHCL_ERROR, tr("The clipboard source output argument must not be NULL"));

    SHCLFORMAT uRequestedFormat = VBOX_SHCL_FMT_NONE;
    if (!aRequestedMimeType.isEmpty())
    {
        uRequestedFormat = consoleClipboardMimeTypeToFormat(aRequestedMimeType);
        if (uRequestedFormat == VBOX_SHCL_FMT_NONE)
        {
            LogFunc(("Rejecting raw read for unsupported MIME type: %s\n", aRequestedMimeType.c_str()));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                            tr("Clipboard MIME type '%s' is not supported"), aRequestedMimeType.c_str());
        }
    }

    HRESULT hrc = S_OK;
    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    ClipboardSource_T enmReportedSource = ClipboardSource_Custom;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        enmReportedSource = mData->mSource;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsRead(enmMode, enmReportedSource))
    {
        LogFunc(("Rejecting raw read for reported source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)enmReportedSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)enmReportedSource);
    }

    if (uRequestedFormat != VBOX_SHCL_FMT_NONE)
        hrc = i_readDataForFormat(aAction, uRequestedFormat, aSource, aMimeType, aBuffer);
    else
        hrc = i_readData(aAction, aSource, aMimeType, aBuffer);
    if (FAILED(hrc))
        return setError(hrc, tr("Reading shared clipboard data failed (%Rhrc)"), hrc);
    if (!ShClMainIsValidSource(*aSource))
    {
        LogFunc(("Read returned invalid clipboard source %RU32\n", (uint32_t)*aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)*aSource);
    }

    if (!ShClMainModeAllowsRead(enmMode, *aSource))
    {
        LogFunc(("Rejecting read from source %RU32 in clipboard mode %RU32\n", (uint32_t)*aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)*aSource);
    }
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Writes raw clipboard data.
 *
 * @returns COM status code.
 * @param   aAction         Clipboard action to write for.
 * @param   aSource         Clipboard source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 * @param   aWrittenSource  Where to return the written clipboard source.
 * @param   aWrittenMimeType  Where to return the written MIME type.
 * @param   aWrittenBuffer  Where to return the written payload bytes.
 */
HRESULT Clipboard::writeDataRaw(ClipboardAction_T aAction,
                                ClipboardSource_T aSource,
                                const com::Utf8Str &aMimeType,
                                const std::vector<BYTE> &aBuffer,
                                ClipboardSource_T *aWrittenSource,
                                com::Utf8Str &aWrittenMimeType,
                                std::vector<BYTE> &aWrittenBuffer)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aAction, aSource, aMimeType, aBuffer,
             aWrittenSource, aWrittenMimeType, aWrittenBuffer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aAction=%RU32, aSource=%RU32, aMimeType=%s, cb=%zu\n",
              (uint32_t)aAction, (uint32_t)aSource, aMimeType.c_str(), aBuffer.size()));
    HRESULT hrc = S_OK;
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }
    if (!aWrittenSource)
        return setError(VBOX_E_SHCL_ERROR, tr("The written clipboard source output argument must not be NULL"));
    if (aMimeType.isEmpty())
    {
        LogFunc(("Rejecting clipboard write with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }
    if (consoleClipboardMimeTypeToFormat(aMimeType) == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Rejecting unsupported clipboard MIME type for write: %s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                        tr("Clipboard MIME type '%s' is not supported"), aMimeType.c_str());
    }
    if (aBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Rejecting oversized clipboard write: mime=%s, cb=%zu, max=%RU32\n",
                 aMimeType.c_str(), aBuffer.size(), s_cbClipboardReadMax));
        return setError(VBOX_E_SHCL_TOO_MUCH_DATA,
                        tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                        s_cbClipboardReadMax);
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, aSource))
    {
        LogFunc(("Rejecting write from source %RU32 in clipboard mode %RU32\n", (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }
    if (aBuffer.empty())
    {
        LogFunc(("Rejecting empty clipboard write: mime=%s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard write data must not be empty"));
    }

    hrc = i_writeDataRaw(VBOX_SHCL_MAIN_CLIENT_NONE, aAction, aSource, aMimeType, aBuffer,
                         aWrittenSource, aWrittenMimeType, aWrittenBuffer);
    if (FAILED(hrc))
        return setError(hrc, tr("Writing shared clipboard data failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Writes raw clipboard data for internal session use.
 *
 * @returns COM status code.
 * @param   aClientId       Main clipboard client ID associated with the write.
 * @param   aAction         Clipboard action to write for.
 * @param   aSource         Clipboard source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 * @param   aWrittenSource  Where to return the written clipboard source.
 * @param   aWrittenMimeType  Where to return the written MIME type.
 * @param   aWrittenBuffer  Where to return the written payload bytes.
 */
HRESULT Clipboard::i_writeDataRaw(VBOXSHCLMAINCLIENTID aClientId,
                                  ClipboardAction_T aAction,
                                  ClipboardSource_T aSource,
                                  const com::Utf8Str &aMimeType,
                                  const std::vector<BYTE> &aBuffer,
                                  ClipboardSource_T *aWrittenSource,
                                  com::Utf8Str &aWrittenMimeType,
                                  std::vector<BYTE> &aWrittenBuffer)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aClientId, aAction, aSource, aMimeType, aBuffer,
             aWrittenSource, aWrittenMimeType, aWrittenBuffer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aClientId=%RU32, aAction=%RU32, aSource=%RU32, aMimeType=%s, cb=%zu\n",
              aClientId, (uint32_t)aAction, (uint32_t)aSource, aMimeType.c_str(), aBuffer.size()));
    HRESULT hrc = S_OK;
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }
    if (!aWrittenSource)
        return setError(VBOX_E_SHCL_ERROR, tr("The written clipboard source output argument must not be NULL"));
    if (aMimeType.isEmpty())
    {
        LogFunc(("Rejecting clipboard write with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }
    if (consoleClipboardMimeTypeToFormat(aMimeType) == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Rejecting unsupported clipboard MIME type for write: %s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                        tr("Clipboard MIME type '%s' is not supported"), aMimeType.c_str());
    }
    if (aBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Rejecting oversized clipboard write: mime=%s, cb=%zu, max=%RU32\n",
                 aMimeType.c_str(), aBuffer.size(), s_cbClipboardReadMax));
        return setError(VBOX_E_SHCL_TOO_MUCH_DATA,
                        tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                        s_cbClipboardReadMax);
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, aSource))
    {
        LogFunc(("Rejecting write from source %RU32 in clipboard mode %RU32\n", (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }
    if (aBuffer.empty())
    {
        LogFunc(("Rejecting empty clipboard write: mime=%s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard write data must not be empty"));
    }

    hrc = i_writeData(aClientId, aAction, aSource, aMimeType, aBuffer,
                      aWrittenSource, aWrittenMimeType, aWrittenBuffer);
    if (FAILED(hrc))
        return setError(hrc, tr("Writing shared clipboard data failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Writes the available clipboard formats.
 *
 * @returns COM status code.
 * @param   aFormats        Clipboard formats to write.
 */
HRESULT Clipboard::writeFormats(const std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aFormats);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("cFormats=%zu\n", aFormats.size()));
    HRESULT hrc = S_OK;
    std::vector<com::Utf8Str> aMimeTypes;
    for (std::vector<ComPtr<IClipboardFormat> >::const_iterator it = aFormats.begin(); it != aFormats.end(); ++it)
    {
        if (it->isNull())
        {
            LogFunc(("Invalid NULL clipboard format in writeFormats\n"));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard format must not be NULL"));
        }

        com::Bstr bstrMimeType;
        hrc = (*it)->COMGETTER(MimeType)(bstrMimeType.asOutParam());
        if (FAILED(hrc))
            return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard format MIME type failed (%Rhrc)"), hrc);
        com::Utf8Str strMimeType(bstrMimeType);
        if (strMimeType.isEmpty())
        {
            LogFunc(("Rejecting clipboard format with empty MIME type\n"));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
        }
        if (consoleClipboardMimeTypeToFormat(strMimeType) == VBOX_SHCL_FMT_NONE)
        {
            LogFunc(("Rejecting unsupported clipboard MIME type for writeFormats: %s\n", strMimeType.c_str()));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                            tr("Clipboard MIME type '%s' is not supported"), strMimeType.c_str());
        }
        aMimeTypes.push_back(strMimeType);
    }
    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, ClipboardSource_Host))
    {
        LogFunc(("Rejecting format write from host in clipboard mode %RU32\n", (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing host clipboard formats"),
                        (uint32_t)enmMode);
    }
    hrc = i_writeFormats(VBOX_SHCL_MAIN_CLIENT_NONE, aMimeTypes);
    if (FAILED(hrc))
        return setError(hrc, tr("Writing shared clipboard formats failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Writes the available clipboard formats for internal session use.
 *
 * @returns COM status code.
 * @param   aClientId       Main clipboard client ID associated with the write.
 * @param   aFormats        Clipboard formats to write.
 */
HRESULT Clipboard::i_writeFormatObjects(VBOXSHCLMAINCLIENTID aClientId,
                                        const std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aClientId, aFormats);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aClientId=%RU32, cFormats=%zu\n", aClientId, aFormats.size()));
    HRESULT hrc = S_OK;
    std::vector<com::Utf8Str> aMimeTypes;
    for (std::vector<ComPtr<IClipboardFormat> >::const_iterator it = aFormats.begin(); it != aFormats.end(); ++it)
    {
        if (it->isNull())
        {
            LogFunc(("Invalid NULL clipboard format in writeFormats\n"));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard format must not be NULL"));
        }

        com::Bstr bstrMimeType;
        hrc = (*it)->COMGETTER(MimeType)(bstrMimeType.asOutParam());
        if (FAILED(hrc))
            return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard format MIME type failed (%Rhrc)"), hrc);
        com::Utf8Str strMimeType(bstrMimeType);
        if (strMimeType.isEmpty())
        {
            LogFunc(("Rejecting clipboard format with empty MIME type\n"));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
        }
        if (consoleClipboardMimeTypeToFormat(strMimeType) == VBOX_SHCL_FMT_NONE)
        {
            LogFunc(("Rejecting unsupported clipboard MIME type for writeFormats: %s\n", strMimeType.c_str()));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                            tr("Clipboard MIME type '%s' is not supported"), strMimeType.c_str());
        }
        aMimeTypes.push_back(strMimeType);
    }
    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, ClipboardSource_Host))
    {
        LogFunc(("Rejecting format write from host in clipboard mode %RU32\n", (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing host clipboard formats"),
                        (uint32_t)enmMode);
    }
    hrc = i_writeFormats(aClientId, aMimeTypes);
    if (FAILED(hrc))
        return setError(hrc, tr("Writing shared clipboard formats failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Resets the clipboard state.
 *
 * @returns COM status code.
 */
HRESULT Clipboard::reset()
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    LogFunc(("Resetting clipboard object state\n"));
    HRESULT hrc = i_reset();
    if (FAILED(hrc))
        return setError(hrc, tr("Resetting shared clipboard state failed (%Rhrc)"), hrc);

    ComObjPtr<ClipboardTransferManager> ptrTransfers;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        Log2Func(("Clearing cached clipboard state: fHaveLastItem=%RTbool, cbLast=%zu\n",
                  mData->mfHaveLastItem, mData->mLastItemBuffer.size()));
        mData->mLastItemBuffer.clear();
        mData->mfHaveLastItem = false;
        mData->mLastItemSerial++;
        mData->mCurrentClientId = VBOX_SHCL_MAIN_CLIENT_NONE;
        i_clearPendingDataRequestsLocked();
        ptrTransfers = mData->mTransfers;
    }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (!ptrTransfers.isNull())
        ptrTransfers->i_reset();
#endif
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Checks whether a clipboard format is currently available.
 *
 * @returns COM status code.
 * @param   aSource         Clipboard source to query.
 * @param   aFormat         Clipboard format to check.
 * @param   aAvailable      Where to return the availability state.
 */
HRESULT Clipboard::isFormatAvailable(ClipboardSource_T aSource,
                                     const ComPtr<IClipboardFormat> &aFormat,
                                     BOOL *aAvailable)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aSource, aFormat, aAvailable);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aSource=%RU32, aFormat=%p\n", (uint32_t)aSource, (void *)aFormat));
    if (!aAvailable)
        return setError(VBOX_E_SHCL_ERROR, tr("The availability output argument must not be NULL"));
    *aAvailable = FALSE;
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }
    if (aFormat.isNull())
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard format must not be NULL"));

    com::Bstr bstrMimeType;
    HRESULT hrc = aFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard format MIME type failed (%Rhrc)"), hrc);
    com::Utf8Str strMimeType(bstrMimeType);
    if (strMimeType.isEmpty())
    {
        LogFunc(("Rejecting clipboard availability query with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsRead(enmMode, aSource))
    {
        LogFunc(("Rejecting format availability query for source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }

    SHCLFORMAT const uFormat = consoleClipboardMimeTypeToFormat(strMimeType);
    if (uFormat != VBOX_SHCL_FMT_NONE)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        *aAvailable = RT_BOOL(m_fFormats & uFormat);
    }
    return S_OK;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Returns the supported clipboard formats for a source.
 *
 * @returns COM status code.
 * @param   aSource         Clipboard source to query.
 * @param   aFormats        Where to return the supported formats.
 */
HRESULT Clipboard::getSupportedFormats(ClipboardSource_T aSource,
                                       std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aSource, aFormats);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Log2Func(("aSource=%RU32\n", (uint32_t)aSource));
    HRESULT hrc = S_OK;
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsRead(enmMode, aSource))
    {
        LogFunc(("Rejecting supported format query for source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow reading from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }
    hrc = readFormats(aFormats);
    if (FAILED(hrc))
        return setError(hrc, tr("Reading supported shared clipboard formats failed (%Rhrc)"), hrc);
    return hrc;
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD
/**
 * Reads raw data from the Shared Clipboard service.
 *
 * @returns COM status code.
 * @param   aAction         Clipboard action to read for.
 * @param   aSource         Where to return the clipboard source.
 * @param   aMimeType       Where to return the MIME type.
 * @param   aBuffer         Where to return the payload bytes.
 */
HRESULT Clipboard::i_readData(ClipboardAction_T aAction,
                              ClipboardSource_T *aSource,
                              com::Utf8Str &aMimeType,
                              std::vector<BYTE> &aBuffer)
{
    Log2Func(("aAction=%s\n", clipboardActionToLogString(aAction)));
    AssertPtrReturn(aSource, E_POINTER);
    AssertPtrReturn(mData, E_FAIL);

    SHCLFORMATS fFormats;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        fFormats = m_fFormats;
    }

    SHCLFORMAT const uFormat = clipboardPickFormat(fFormats);
    Log2Func(("Available formats=%#x, picked=%#x\n", fFormats, uFormat));
    if (uFormat == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("No clipboard formats available for read\n"));
        return mData->mParent->setError(VBOX_E_SHCL_NO_DATA, Console::tr("No clipboard formats are currently available"));
    }

    return i_readDataForFormat(aAction, uFormat, aSource, aMimeType, aBuffer);
}


/**
 * Reads raw data in the requested Shared Clipboard format.
 *
 * @retval  S_OK if the requested data was returned from the cache or guest.
 * @retval  E_POINTER if @a aSource is invalid.
 * @retval  E_FAIL if this clipboard object is not initialized or @a uFormat
 *          has no supported MIME type.
 * @retval  VBOX_E_SHCL_NO_DATA if the format, Shared Clipboard service, or
 *          connected guest clipboard client is unavailable.
 * @retval  VBOX_E_SHCL_TOO_MUCH_DATA if the guest payload exceeds the supported size.
 * @retval  VBOX_E_SHCL_GUEST_ERROR if reading or converting guest data fails.
 * @param   aAction         Clipboard action to read for.
 * @param   uFormat         Shared Clipboard format to read.
 * @param   aSource         Where to return the clipboard source.
 * @param   aMimeType       Where to return the MIME type.
 * @param   aBuffer         Where to return the payload bytes.
 */
HRESULT Clipboard::i_readDataForFormat(ClipboardAction_T aAction,
                                       uint32_t uFormat,
                                       ClipboardSource_T *aSource,
                                       com::Utf8Str &aMimeType,
                                       std::vector<BYTE> &aBuffer)
{
    Log2Func(("aAction=%s, uFormat=%#x\n", clipboardActionToLogString(aAction), uFormat));
    RT_NOREF(aAction);
    AssertPtrReturn(aSource, E_POINTER);
    AssertPtrReturn(mData, E_FAIL);

    AutoCaller autoCaller(mData->mParent);
    AssertComRCReturnRC(autoCaller.hrc());

    const char *pszMimeType = consoleClipboardFormatToMimeType((SHCLFORMAT)uFormat);
    AssertReturn(pszMimeType, E_FAIL);

    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!(m_fFormats & uFormat))
        {
            LogFunc(("Requested clipboard format is not available: requested=%#x, available=%#x\n", uFormat, m_fFormats));
            return mData->mParent->setError(VBOX_E_SHCL_NO_DATA,
                                            Console::tr("The requested clipboard format is not currently available"));
        }
        if (   mData->mfHaveLastItem
            && mData->mLastItemSource == mData->mSource
            && consoleClipboardMimeTypeToFormat(mData->mLastItemMimeType) == uFormat)
        {
            *aSource = mData->mLastItemSource;
            aMimeType = mData->mLastItemMimeType;
            aBuffer = mData->mLastItemBuffer;
            Log2Func(("Cache hit: source=%s, mime=%s, cb=%zu\n",
                      clipboardSourceToLogString(*aSource), aMimeType.c_str(), aBuffer.size()));
            return S_OK;
        }
    }

    GuestShCl *pShCl = GuestShCl::GetInst();
    if (!pShCl)
    {
        LogFunc(("Cannot read guest clipboard data without Shared Clipboard service\n"));
        return mData->mParent->setError(VBOX_E_SHCL_NO_DATA,
                                        Console::tr("The Shared Clipboard service is not available"));
    }

    void *pvData = NULL;
    uint32_t cbData = 0;
    int vrc = pShCl->ReadDataFromGuest((SHCLFORMAT)uFormat, &pvData, &cbData);
    if (RT_SUCCESS(vrc))
    {
        Log2Func(("Read protocol data from guest: format=%#x, cb=%RU32\n", uFormat, cbData));
        if (cbData > s_cbClipboardReadMax)
        {
            LogFunc(("Guest clipboard payload too large: format=%#x, cb=%RU32, max=%RU32\n",
                     uFormat, cbData, s_cbClipboardReadMax));
            LogRelMax(16, ("Shared Clipboard: Guest clipboard data is too large: format %#x, %RU32 bytes (limit %RU32 bytes)\n",
                           uFormat, cbData, s_cbClipboardReadMax));
            RTMemFree(pvData);
            return mData->mParent->setErrorBoth(VBOX_E_SHCL_TOO_MUCH_DATA, VERR_TOO_MUCH_DATA,
                                                Console::tr("Reading shared clipboard data exceeded the supported size (%RU32 bytes)"),
                                                s_cbClipboardReadMax);
        }
        int vrc2 = clipboardProtocolToMainData((SHCLFORMAT)uFormat, pvData, cbData, aBuffer);
        RTMemFree(pvData);
        if (RT_FAILURE(vrc2))
        {
            LogFunc(("Converting guest clipboard data failed: format=%#x, cb=%RU32, vrc=%Rrc\n", uFormat, cbData, vrc2));
            LogRelMax(16, ("Shared Clipboard: Failed to convert guest clipboard data: format %#x, %RU32 bytes, vrc=%Rrc\n",
                           uFormat, cbData, vrc2));
            return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, vrc2,
                                                Console::tr("Converting shared clipboard data failed with %Rrc"), vrc2);
        }
        *aSource = ClipboardSource_Guest;
        aMimeType = pszMimeType;
        Log2Func(("Read Main data from guest: format=%#x, mime=%s, cb=%zu\n", uFormat, aMimeType.c_str(), aBuffer.size()));
        return S_OK;
    }

    if (vrc == VERR_NOT_AVAILABLE)
    {
        LogFunc(("No guest clipboard client connected for read: format=%#x\n", uFormat));
        LogRelMax(16, ("Shared Clipboard: Cannot read guest clipboard data, no guest clipboard client is connected (format %#x)\n",
                       uFormat));
        return mData->mParent->setError(VBOX_E_SHCL_NO_DATA, Console::tr("No guest clipboard client is currently connected"));
    }

    LogFunc(("Reading guest clipboard data failed: format=%#x, vrc=%Rrc\n", uFormat, vrc));
    LogRelMax(16, ("Shared Clipboard: Reading guest clipboard data failed: format %#x, vrc=%Rrc\n", uFormat, vrc));
    aBuffer.clear();
    return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, vrc,
                                        Console::tr("Reading shared clipboard data failed with %Rrc"), vrc);
}


/**
 * Reads raw format names from the Shared Clipboard service state.
 *
 * @returns COM status code.
 * @param   aFormats        Where to return the MIME formats.
 */
HRESULT Clipboard::i_readFormats(std::vector<com::Utf8Str> &aFormats)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    consoleClipboardFormatsToMimeTypes(m_fFormats, aFormats);
    Log3Func(("fFormats=%#x, cMimeTypes=%zu\n", m_fFormats, aFormats.size()));
    return S_OK;
}


/**
 * Returns the current clipboard state as event objects.
 *
 * @returns COM status code.
 * @param   aSource         Where to return the current clipboard source.
 * @param   aFormats        Where to return the current format objects.
 */
HRESULT Clipboard::i_getCurrentStateForEvent(ClipboardSource_T *aSource,
                                             std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
    AssertPtrReturn(aSource, E_POINTER);

    std::vector<com::Utf8Str> aMimeTypes;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        *aSource = mData->mSource;
        consoleClipboardFormatsToMimeTypes(m_fFormats, aMimeTypes);
    }

    aFormats.clear();
    for (std::vector<com::Utf8Str>::const_iterator it = aMimeTypes.begin(); it != aMimeTypes.end(); ++it)
    {
        ComPtr<IClipboardFormat> ptrFormat;
        HRESULT hrc = i_createFormat(*it, ptrFormat);
        if (FAILED(hrc))
            return hrc;
        aFormats.push_back(ptrFormat);
    }
    return S_OK;
}


/**
 * Returns the current clipboard source.
 *
 * @returns COM status code.
 * @param   aSource         Where to return the current clipboard source.
 */
HRESULT Clipboard::i_getCurrentSource(ClipboardSource_T *aSource)
{
    AssertPtrReturn(aSource, E_POINTER);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertPtrReturn(mData, E_FAIL);

    *aSource = mData->mSource;
    return S_OK;
}


/**
 * Allocates the next clipboard event revision.
 *
 * @returns Clipboard event revision, never zero.
 */
LONG64 Clipboard::i_nextEventRevision()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertPtrReturn(mData, 0);

    if (mData->mNextEventRevision <= 0)
        mData->mNextEventRevision = 1;
    LONG64 const i64Revision = mData->mNextEventRevision;
    if (mData->mNextEventRevision == INT64_MAX)
        mData->mNextEventRevision = 1;
    else
        mData->mNextEventRevision++;
    return i64Revision;
}


/**
 * Checks whether a Main clipboard client ID still names a live session.
 *
 * The anonymous client ID is the console-global endpoint and is always accepted.
 * The caller must hold the clipboard lock.
 *
 * @returns true if the client ID may still publish through this clipboard object.
 * @param   aClientId      Main clipboard client ID to validate.
 */
bool Clipboard::i_isClientIdRegisteredLocked(VBOXSHCLMAINCLIENTID aClientId) const
{
    AssertPtrReturn(mData, false);
    if (aClientId == VBOX_SHCL_MAIN_CLIENT_NONE)
        return true;

    for (std::vector<Data::SessionRecord>::const_iterator it = mData->mSessions.begin(); it != mData->mSessions.end(); ++it)
        if (it->mClientId == aClientId)
            return true;
    return false;
}


/**
 * Checks whether a live client currently owns the advertised clipboard formats.
 *
 * The caller must hold the clipboard lock.
 *
 * @returns true if the client is live and still owns the requested source/formats.
 * @param   aClientId      Main clipboard client ID to validate.
 * @param   fFormats       Format mask that must still be advertised.
 * @param   aSource        Clipboard source that must still be current.
 */
bool Clipboard::i_isClientFormatOwnerLocked(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFormats, ClipboardSource_T aSource) const
{
    AssertPtrReturn(mData, false);
    return    i_isClientIdRegisteredLocked(aClientId)
           && mData->mCurrentClientId == aClientId
           && mData->mSource == aSource
           && (m_fFormats & fFormats) == fFormats;
}


/**
 * Allocates the next Main clipboard client ID.
 *
 * @returns Main clipboard client ID, never VBOX_SHCL_MAIN_CLIENT_NONE.
 */
VBOXSHCLMAINCLIENTID Clipboard::i_allocateClientId()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertPtrReturn(mData, VBOX_SHCL_MAIN_CLIENT_NONE);

    uint32_t cTries = UINT32_MAX;
    while (cTries-- > 0)
    {
        VBOXSHCLMAINCLIENTID const idClient = mData->mNextClientId++;
        if (mData->mNextClientId == VBOX_SHCL_MAIN_CLIENT_NONE)
            mData->mNextClientId = 1;
        if (idClient == VBOX_SHCL_MAIN_CLIENT_NONE)
            continue;

        bool fInUse = false;
        for (std::vector<Data::SessionRecord>::const_iterator it = mData->mSessions.begin();
             it != mData->mSessions.end(); ++it)
            if (it->mClientId == idClient)
            {
                fInUse = true;
                break;
            }
        if (!fInUse)
            return idClient;
    }

    LogFunc(("No free clipboard session client identifiers are available\n"));
    return VBOX_SHCL_MAIN_CLIENT_NONE;
}


/**
 * Registers a clipboard session event target.
 *
 * @returns COM status code.
 * @param   aClientId       Session client identifier.
 * @param   aSession        Session object to retain while registered.
 * @param   fFlags          IClipboardSessionFlag mask.
 * @param   aEventSource    Session-specific event source.
 */
HRESULT Clipboard::i_registerSession(VBOXSHCLMAINCLIENTID aClientId,
                                     ClipboardSession *aSession,
                                     uint32_t fFlags,
                                     const ComPtr<IEventSource> &aEventSource)
{
    AssertReturn(aClientId != VBOX_SHCL_MAIN_CLIENT_NONE, E_INVALIDARG);
    AssertPtrReturn(aSession, E_INVALIDARG);
    AssertReturn(aEventSource.isNotNull(), E_INVALIDARG);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertPtrReturn(mData, E_FAIL);

    for (std::vector<Data::SessionRecord>::const_iterator it = mData->mSessions.begin(); it != mData->mSessions.end(); ++it)
        if (it->mClientId == aClientId)
            return setError(E_INVALIDARG, tr("Clipboard session client id %RU32 is already registered"), aClientId);

    try
    {
        mData->mSessions.push_back(Data::SessionRecord(aClientId, aSession, fFlags, aEventSource));
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    Log2Func(("Registered clipboard session: clientId=%RU32, fFlags=%#x, cSessions=%zu\n",
              aClientId, fFlags, mData->mSessions.size()));
    return S_OK;
}


/**
 * Unregisters a clipboard session event target.
 *
 * @param   aClientId       Session client identifier.
 */
void Clipboard::i_unregisterSession(VBOXSHCLMAINCLIENTID aClientId)
{
    if (aClientId == VBOX_SHCL_MAIN_CLIENT_NONE)
        return;

    bool fClearCurrentOwner = false;
    ComObjPtr<ClipboardSession> ptrKeepAlive;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return;

        for (std::vector<Data::SessionRecord>::iterator it = mData->mSessions.begin(); it != mData->mSessions.end(); ++it)
            if (it->mClientId == aClientId)
            {
                ptrKeepAlive = it->mSession;
                mData->mSessions.erase(it);
                fClearCurrentOwner = mData->mCurrentClientId == aClientId;
                Log2Func(("Unregistered clipboard session: clientId=%RU32, cSessions=%zu, fClearCurrentOwner=%RTbool\n",
                          aClientId, mData->mSessions.size(), fClearCurrentOwner));
                break;
            }
    }

    if (ptrKeepAlive.isNull())
    {
        Log3Func(("Clipboard session clientId=%RU32 was not registered\n", aClientId));
        return;
    }

    if (fClearCurrentOwner)
        i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE, VBOX_SHCL_FMT_NONE, ClipboardSource_Custom,
                        true /* fForceNotify */, true /* fRequireCurrentClientId */, aClientId);
}


/**
 * Collects session event targets after applying session filters.
 *
 * @param   aTargets            Where to append matching session targets.
 * @param   aClientId           Originating client identifier for the event.
 * @param   fPassive            Whether ExcludeOwnChanges applies to this event.
 * @param   fCheckReflection    Whether ExcludeReflections can be evaluated from source/formats.
 * @param   fFormats            Current format mask for reflection checks.
 * @param   aSource             Current source for reflection checks.
 */
void Clipboard::i_getSessionEventTargets(std::vector<SessionEventTarget> &aTargets,
                                         VBOXSHCLMAINCLIENTID aClientId,
                                         bool fPassive,
                                         bool fCheckReflection,
                                         uint32_t fFormats,
                                         ClipboardSource_T aSource)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (!mData)
        return;

    for (std::vector<Data::SessionRecord>::iterator it = mData->mSessions.begin(); it != mData->mSessions.end(); ++it)
    {
        if (it->mEventSource.isNull())
            continue;
        if (   fPassive
            && aClientId != VBOX_SHCL_MAIN_CLIENT_NONE
            && aClientId == it->mClientId
            && (it->mfFlags & IClipboardSessionFlag_ExcludeOwnChanges))
        {
            Log3Func(("Suppressing own clipboard event for session %RU32\n", it->mClientId));
            continue;
        }
        if (   fCheckReflection
            && aClientId == VBOX_SHCL_MAIN_CLIENT_NONE
            && (it->mfFlags & IClipboardSessionFlag_ExcludeReflections)
            && it->mfHaveReflectionFormats)
        {
            bool const fSuppressReflection =    it->mfReflectionFormats == fFormats
                                             && it->mReflectionSource == aSource;
            it->mfHaveReflectionFormats = false;
            it->mfReflectionFormats = VBOX_SHCL_FMT_NONE;
            it->mReflectionSource = ClipboardSource_Custom;
            if (fSuppressReflection)
            {
                Log3Func(("Suppressing reflected clipboard event for session %RU32: source=%s, formats=%#x\n",
                          it->mClientId, clipboardSourceToLogString(aSource), fFormats));
                continue;
            }
        }

        SessionEventTarget Target;
        Target.mClientId = it->mClientId;
        Target.mfFlags = it->mfFlags;
        Target.mEventSource = it->mEventSource;
        try
        {
            aTargets.push_back(Target);
        }
        catch (std::bad_alloc &)
        {
            LogFunc(("Out of memory collecting clipboard session event targets\n"));
            break;
        }
    }
}


/**
 * Fires the current clipboard state to a session event source once.
 *
 * @returns COM status code.
 * @param   aClientId       Session client identifier.
 */
HRESULT Clipboard::i_fireSessionInitialState(VBOXSHCLMAINCLIENTID aClientId)
{
    ComPtr<IEventSource> ptrEventSource;
    uint32_t fFlags = 0;
    uint32_t fFormats = VBOX_SHCL_FMT_NONE;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    bool fHaveLastItem = false;
    ClipboardAction_T enmLastAction = ClipboardAction_Copy;
    ClipboardSource_T enmLastSource = ClipboardSource_Custom;
    com::Utf8Str strLastMimeType;
    std::vector<BYTE> abLastBuffer;

    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);

        Data::SessionRecord *pSession = NULL;
        for (std::vector<Data::SessionRecord>::iterator it = mData->mSessions.begin(); it != mData->mSessions.end(); ++it)
            if (it->mClientId == aClientId)
            {
                pSession = &*it;
                break;
            }
        if (!pSession)
            return S_OK;
        if (!(pSession->mfFlags & IClipboardSessionFlag_IncludeInitialState))
            return S_OK;
        if (pSession->mfInitialStateDelivered)
            return S_OK;

        pSession->mfInitialStateDelivered = true;
        ptrEventSource = pSession->mEventSource;
        fFlags = pSession->mfFlags;
        fFormats = m_fFormats;
        enmSource = mData->mSource;
        fHaveLastItem = mData->mfHaveLastItem;
        enmLastAction = mData->mLastItemAction != ClipboardAction_Invalid ? mData->mLastItemAction : ClipboardAction_Copy;
        enmLastSource = mData->mLastItemSource;
        strLastMimeType = mData->mLastItemMimeType;
        abLastBuffer = mData->mLastItemBuffer;
    }

    if (ptrEventSource.isNull())
        return S_OK;

    std::vector<ComPtr<IClipboardFormat> > vecFormats;
    std::vector<com::Utf8Str> vecMimeTypes;
    consoleClipboardFormatsToMimeTypes(fFormats, vecMimeTypes);
    for (std::vector<com::Utf8Str>::const_iterator it = vecMimeTypes.begin(); it != vecMimeTypes.end(); ++it)
    {
        ComPtr<IClipboardFormat> ptrFormat;
        HRESULT hrc = i_createFormat(*it, ptrFormat);
        if (FAILED(hrc))
            return hrc;
        try
        {
            vecFormats.push_back(ptrFormat);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    LONG64 const i64Revision = i_nextEventRevision();
    Log2Func(("Firing initial session clipboard state: clientId=%RU32, source=%s, formats=%#x, revision=%RI64\n",
              aClientId, clipboardSourceToLogString(enmSource), fFormats, i64Revision));
    HRESULT hrc = ::FireClipboardSourceChangedEvent(ptrEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE, enmSource);
    AssertComRC(hrc);

    com::SafeIfaceArray<IClipboardFormat> aEventFormats(vecFormats);
    ComPtr<IClipboardItem> ptrEventItem;
    hrc = ::FireClipboardFormatChangedEvent(ptrEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE,
                                            com::Utf8Str(), ptrEventItem, FALSE /* aVeto */, enmSource,
                                            ComSafeArrayAsInParam(aEventFormats));
    AssertComRC(hrc);

    if (fHaveLastItem)
    {
        if (fFlags & IClipboardSessionFlag_IncludePayload)
        {
            hrc = i_createItem(enmLastSource, strLastMimeType, abLastBuffer, ptrEventItem);
            if (FAILED(hrc))
                return hrc;
        }
        else
            ptrEventItem.setNull();
        hrc = ::FireClipboardDataChangedEvent(ptrEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE,
                                              com::Utf8Str(), ptrEventItem, FALSE /* aVeto */, enmLastAction);
        AssertComRC(hrc);
    }

    return S_OK;
}


/**
 * Writes raw data to the Shared Clipboard service.
 *
 * @retval  S_OK if the data was written.
 * @retval  E_POINTER if @a aWrittenSource is invalid.
 * @retval  E_FAIL if this clipboard object is not initialized.
 * @retval  VBOX_E_SHCL_NO_DATA if @a aBuffer is empty.
 * @retval  VBOX_E_SHCL_TOO_MUCH_DATA if the source or converted payload is too large.
 * @retval  VBOX_E_SHCL_FORMAT_NOT_SUPPORTED if @a aMimeType is unsupported.
 * @retval  VBOX_E_SHCL_GUEST_ERROR if VRDE owns the clipboard route, the
 *          Shared Clipboard service is unavailable, or guest format publication fails.
 * @retval  VBOX_E_SHCL_ERROR if the client is inactive, loses clipboard
 *          ownership, or payload conversion fails.
 * @param   aClientId       Main clipboard client ID associated with the write.
 * @param   aAction         Clipboard action to write for.
 * @param   aSource         Clipboard source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 * @param   aWrittenSource  Where to return the written clipboard source.
 * @param   aWrittenMimeType  Where to return the written MIME type.
 * @param   aWrittenBuffer  Where to return the written payload bytes.
 */
HRESULT Clipboard::i_writeData(VBOXSHCLMAINCLIENTID aClientId,
                               ClipboardAction_T aAction,
                               ClipboardSource_T aSource,
                               const com::Utf8Str &aMimeType,
                               const std::vector<BYTE> &aBuffer,
                               ClipboardSource_T *aWrittenSource,
                               com::Utf8Str &aWrittenMimeType,
                               std::vector<BYTE> &aWrittenBuffer)
{
    Log2Func(("clientId=%RU32, action=%s, source=%s, mime=%s, cb=%zu\n",
              aClientId, clipboardActionToLogString(aAction), clipboardSourceToLogString(aSource),
              aMimeType.c_str(), aBuffer.size()));
    AssertPtrReturn(aWrittenSource, E_POINTER);
    AssertPtrReturn(mData, E_FAIL);

    bool fClientActive = false;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        fClientActive = i_isClientIdRegisteredLocked(aClientId);
    }
    if (!fClientActive)
    {
        LogFunc(("Rejecting clipboard write from inactive session clientId=%RU32\n", aClientId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is not active"), aClientId);
    }

    if (aBuffer.empty())
    {
        LogFunc(("Rejecting empty clipboard write: mime=%s\n", aMimeType.c_str()));
        return mData->mParent->setError(VBOX_E_SHCL_NO_DATA, Console::tr("Clipboard write data must not be empty"));
    }
    if (aBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Rejecting oversized clipboard write: mime=%s, cb=%zu, max=%RU32\n",
                 aMimeType.c_str(), aBuffer.size(), s_cbClipboardReadMax));
        LogRelMax(16, ("Shared Clipboard: Refusing to write too much clipboard data: MIME '%.*s', %zu bytes (limit %RU32 bytes)\n",
                       128, aMimeType.c_str(), aBuffer.size(), s_cbClipboardReadMax));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_TOO_MUCH_DATA, VERR_TOO_MUCH_DATA,
                                            Console::tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                                            s_cbClipboardReadMax);
    }

    SHCLFORMAT uFormat = consoleClipboardMimeTypeToFormat(aMimeType);
    if (uFormat == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Unsupported clipboard MIME type for write: %s\n", aMimeType.c_str()));
        return mData->mParent->setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                                        Console::tr("Clipboard MIME type '%s' is not supported"), aMimeType.c_str());
    }

    std::vector<BYTE> abProtocolBuffer;
    int vrc = clipboardMainToProtocolData(uFormat, aBuffer, abProtocolBuffer);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Converting Main clipboard data failed: format=%#x, mime=%s, cb=%zu, vrc=%Rrc\n",
                 uFormat, aMimeType.c_str(), aBuffer.size(), vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to convert clipboard data for guest: MIME '%.*s', format %#x, %zu bytes, vrc=%Rrc\n",
                       128, aMimeType.c_str(), uFormat, aBuffer.size(), vrc));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                                            Console::tr("Converting shared clipboard data failed with %Rrc"), vrc);
    }
    Log3Func(("Protocol write payload: format=%#x, cbMain=%zu, cbProtocol=%zu\n",
              uFormat, aBuffer.size(), abProtocolBuffer.size()));
    if (abProtocolBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Converted clipboard write is too large: format=%#x, cb=%zu, max=%RU32\n",
                 uFormat, abProtocolBuffer.size(), s_cbClipboardReadMax));
        LogRelMax(16, ("Shared Clipboard: Converted clipboard data is too large for the guest: format %#x, %zu bytes (limit %RU32 bytes)\n",
                       uFormat, abProtocolBuffer.size(), s_cbClipboardReadMax));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_TOO_MUCH_DATA, VERR_TOO_MUCH_DATA,
                                            Console::tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                                            s_cbClipboardReadMax);
    }

    AutoCaller autoCaller(mData->mParent);
    AssertComRCReturnRC(autoCaller.hrc());

    GuestShCl *pRouteShCl = GuestShCl::GetInst();
    if (pRouteShCl && !pRouteShCl->IsNativeBackendActive())
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, VERR_RESOURCE_BUSY,
                                            Console::tr("Writing shared clipboard data is unavailable while the VRDE clipboard is active"));

    if (!i_reportFormats(aClientId, uFormat, aSource))
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer active"), aClientId);
    if (!i_storeDataIfCurrentClient(aClientId, aAction, aSource, aMimeType, aBuffer, uFormat))
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer the clipboard owner"), aClientId);

    *aWrittenSource = aSource;
    aWrittenMimeType = aMimeType;
    aWrittenBuffer = aBuffer;

    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!i_isClientFormatOwnerLocked(aClientId, uFormat, aSource))
        {
            LogFunc(("Rejecting stale clipboard write backend publication: clientId=%RU32\n", aClientId));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer the clipboard owner"), aClientId);
        }
    }

    GuestShCl *pShCl = GuestShCl::GetInst();
    if (!pShCl)
    {
        LogFunc(("Cannot report write format to guest without Shared Clipboard service: format=%#x\n", uFormat));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, VERR_INVALID_STATE,
                                            Console::tr("Writing shared clipboard data failed because the "
                                                        "Shared Clipboard service is not available"));
    }
    vrc = pShCl->ReportFormatsToGuest(uFormat);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Reporting write format to guest failed: format=%#x, vrc=%Rrc\n", uFormat, vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to report clipboard format %#x to the guest, vrc=%Rrc\n", uFormat, vrc));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, vrc,
                                            Console::tr("Writing shared clipboard data failed with %Rrc"), vrc);
    }
    Log2Func(("Reported write format to guest: format=%#x\n", uFormat));

    i_fireDataChanged(aClientId, aAction, aSource, aMimeType, aBuffer);
    Log2Func(("Write completed: source=%s, format=%#x, mime=%s, cb=%zu\n",
              clipboardSourceToLogString(aSource), uFormat, aMimeType.c_str(), aBuffer.size()));
    return S_OK;
}


/**
 * Writes raw format names to the Shared Clipboard service.
 *
 * @retval  S_OK if the formats were written.
 * @retval  E_FAIL if this clipboard object is not initialized.
 * @retval  VBOX_E_SHCL_FORMAT_NOT_SUPPORTED if one or more MIME types are unsupported.
 * @retval  VBOX_E_SHCL_GUEST_ERROR if VRDE owns the clipboard route, the
 *          Shared Clipboard service is unavailable, or guest format publication fails.
 * @retval  VBOX_E_SHCL_ERROR if the client is inactive or loses clipboard ownership.
 * @param   aClientId       Main clipboard client ID associated with the write.
 * @param   aFormats        MIME formats to write.
 */
HRESULT Clipboard::i_writeFormats(VBOXSHCLMAINCLIENTID aClientId,
                                  const std::vector<com::Utf8Str> &aFormats)
{
    Log2Func(("clientId=%RU32, cMimeTypes=%zu\n", aClientId, aFormats.size()));
    bool fClientActive = false;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        fClientActive = i_isClientIdRegisteredLocked(aClientId);
    }
    if (!fClientActive)
    {
        LogFunc(("Rejecting clipboard format write from inactive session clientId=%RU32\n", aClientId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is not active"), aClientId);
    }

    SHCLFORMATS fFormats;
    HRESULT hrc = consoleClipboardMimeTypesToFormats(aFormats, &fFormats);
    if (FAILED(hrc))
    {
        LogFunc(("Converting MIME types to shared clipboard formats failed: cMimeTypes=%zu, hrc=%#x\n",
                 aFormats.size(), hrc));
        return mData && mData->mParent
             ? mData->mParent->setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                                        Console::tr("One or more clipboard MIME types are not supported"))
             : VBOX_E_SHCL_FORMAT_NOT_SUPPORTED;
    }
    Log2Func(("Converted MIME types to formats: fFormats=%#x\n", fFormats));

    AssertPtrReturn(mData, E_FAIL);
    AutoCaller autoCaller(mData->mParent);
    AssertComRCReturnRC(autoCaller.hrc());

    GuestShCl *pRouteShCl = GuestShCl::GetInst();
    if (pRouteShCl && !pRouteShCl->IsNativeBackendActive())
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, VERR_RESOURCE_BUSY,
                                            Console::tr("Writing shared clipboard formats is unavailable while the VRDE clipboard is active"));

    if (!i_reportFormats(aClientId, fFormats, ClipboardSource_Host, true /* fForceNotify */))
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer active"), aClientId);

    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!i_isClientFormatOwnerLocked(aClientId, fFormats, ClipboardSource_Host))
        {
            LogFunc(("Rejecting stale clipboard format backend publication: clientId=%RU32\n", aClientId));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer the clipboard owner"), aClientId);
        }
    }

    GuestShCl *pShCl = GuestShCl::GetInst();
    if (!pShCl)
    {
        LogFunc(("Cannot report formats to guest without Shared Clipboard service: fFormats=%#x\n", fFormats));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, VERR_INVALID_STATE,
                                            Console::tr("Writing shared clipboard formats failed because the "
                                                        "Shared Clipboard service is not available"));
    }
    int vrc = pShCl->ReportFormatsToGuest(fFormats);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Reporting formats to guest failed: fFormats=%#x, vrc=%Rrc\n", fFormats, vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to report clipboard formats %#x to the guest, vrc=%Rrc\n", fFormats, vrc));
        return mData->mParent->setErrorBoth(VBOX_E_SHCL_GUEST_ERROR, vrc,
                                            Console::tr("Writing shared clipboard formats failed with %Rrc"), vrc);
    }
    Log2Func(("Reported formats to guest: fFormats=%#x\n", fFormats));

    return S_OK;
}


/**
 * Reports guest-owned clipboard formats to the native host clipboard endpoint.
 *
 * @retval  S_OK if the formats were published to the native host clipboard.
 * @retval  E_FAIL if clipboard state becomes unavailable while publishing.
 * @retval  VBOX_E_SHCL_ERROR if an argument, format object, session, service,
 *          mode query, ownership check, or native backend operation fails.
 * @retval  VBOX_E_SHCL_ACCESS_DENIED if the source or Shared Clipboard mode
 *          does not permit publication to the host.
 * @retval  VBOX_E_SHCL_NO_DATA if no formats were supplied.
 * @retval  VBOX_E_SHCL_FORMAT_NOT_SUPPORTED if a MIME type is empty or unsupported.
 * @param   aClientId       Main clipboard client ID associated with the report.
 * @param   aAction         Clipboard action associated with the formats.
 * @param   aSource         Clipboard source. Only ClipboardSource_Guest is accepted.
 * @param   aFormats        Clipboard formats to publish.
 */
HRESULT Clipboard::i_hostClipboardReportFormats(VBOXSHCLMAINCLIENTID aClientId,
                                                ClipboardAction_T aAction,
                                                ClipboardSource_T aSource,
                                                const std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
    Log2Func(("clientId=%RU32, action=%s, source=%s, cFormats=%zu\n",
              aClientId, clipboardActionToLogString(aAction), clipboardSourceToLogString(aSource), aFormats.size()));
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid host clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid host clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }
    if (aSource != ClipboardSource_Guest)
    {
        LogFunc(("Rejecting host clipboard publication from unsupported source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Host clipboard publication currently accepts only guest clipboard data"));
    }
    if (aFormats.empty())
    {
        LogFunc(("Rejecting host clipboard format report without formats\n"));
        return setError(VBOX_E_SHCL_NO_DATA, tr("At least one clipboard format must be supplied"));
    }

    std::vector<com::Utf8Str> aMimeTypes;
    for (std::vector<ComPtr<IClipboardFormat> >::const_iterator it = aFormats.begin(); it != aFormats.end(); ++it)
    {
        if (it->isNull())
        {
            LogFunc(("Invalid NULL clipboard format in host clipboard reportFormats\n"));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard format must not be NULL"));
        }

        com::Bstr bstrMimeType;
        HRESULT hrc = (*it)->COMGETTER(MimeType)(bstrMimeType.asOutParam());
        if (FAILED(hrc))
            return setError(VBOX_E_SHCL_ERROR, tr("Reading the clipboard format MIME type failed (%Rhrc)"), hrc);
        com::Utf8Str strMimeType(bstrMimeType);
        if (strMimeType.isEmpty())
        {
            LogFunc(("Rejecting host clipboard format with empty MIME type\n"));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
        }
        if (consoleClipboardMimeTypeToFormat(strMimeType) == VBOX_SHCL_FMT_NONE)
        {
            LogFunc(("Rejecting unsupported host clipboard MIME type for reportFormats: %s\n", strMimeType.c_str()));
            return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                            tr("Clipboard MIME type '%s' is not supported"), strMimeType.c_str());
        }
        aMimeTypes.push_back(strMimeType);
    }

    SHCLFORMATS fFormats;
    HRESULT hrc = consoleClipboardMimeTypesToFormats(aMimeTypes, &fFormats);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("One or more clipboard MIME types are not supported"));

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    bool fClientActive = false;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        fClientActive = i_isClientIdRegisteredLocked(aClientId);
    }
    if (!fClientActive)
    {
        LogFunc(("Rejecting host clipboard format publication from inactive session clientId=%RU32\n", aClientId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is not active"), aClientId);
    }
    hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, aSource))
    {
        LogFunc(("Rejecting host clipboard format publication from source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }

    AutoCaller autoCaller(pParent);
    AssertComRCReturnRC(autoCaller.hrc());

    /* Native backends may request lazily rendered data immediately after taking ownership. */
    if (!i_reportFormats(aClientId, fFormats, aSource, true /* fForceNotify */))
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer active"), aClientId);

    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!i_isClientFormatOwnerLocked(aClientId, fFormats, aSource))
        {
            LogFunc(("Rejecting stale host clipboard format backend publication: clientId=%RU32\n", aClientId));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer the clipboard owner"), aClientId);
        }
    }

    GuestShCl *pShCl = GuestShCl::GetInst();
    if (!pShCl)
    {
        LogFunc(("Cannot report formats to native host clipboard without Shared Clipboard service: fFormats=%#x\n",
                 fFormats));
        return setErrorBoth(VBOX_E_SHCL_ERROR, VERR_INVALID_STATE,
                            tr("Reporting shared clipboard formats to the host failed because the "
                               "Shared Clipboard service is not available"));
    }
    int vrc = pShCl->ReportFormatsToHost(fFormats);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Reporting formats to native host clipboard failed: fFormats=%#x, vrc=%Rrc\n", fFormats, vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to report clipboard formats %#x to the native host clipboard, vrc=%Rrc\n",
                        fFormats, vrc));
        return setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                            tr("Reporting shared clipboard formats to the host failed with %Rrc"), vrc);
    }
    Log2Func(("Reported formats to native host clipboard: fFormats=%#x\n", fFormats));

    return S_OK;
}


/**
 * Provides guest-owned clipboard data for a host clipboard request.
 *
 * @returns COM status code.
 * @param   aClientId       Main clipboard client ID associated with the pending request.
 * @param   aRequestId      Clipboard request identifier.
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard source. Only ClipboardSource_Guest is accepted.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 */
HRESULT Clipboard::i_hostClipboardProvideData(VBOXSHCLMAINCLIENTID aClientId,
                                              ULONG aRequestId,
                                              ClipboardAction_T aAction,
                                              ClipboardSource_T aSource,
                                              const com::Utf8Str &aMimeType,
                                              const std::vector<BYTE> &aBuffer)
{
    Log2Func(("clientId=%RU32, requestId=%RU32, action=%s, source=%s, mime=%s, cb=%zu\n",
              aClientId, (uint32_t)aRequestId, clipboardActionToLogString(aAction), clipboardSourceToLogString(aSource),
              aMimeType.c_str(), aBuffer.size()));
    if (aRequestId == 0)
    {
        LogFunc(("Rejecting host clipboard data with zero request ID\n"));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard request ID must not be zero"));
    }
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid host clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid host clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }
    if (aSource != ClipboardSource_Guest)
    {
        LogFunc(("Rejecting host clipboard data from unsupported source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Host clipboard publication currently accepts only guest clipboard data"));
    }
    if (aMimeType.isEmpty())
    {
        LogFunc(("Rejecting host clipboard data with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }
    SHCLFORMAT const uFormat = consoleClipboardMimeTypeToFormat(aMimeType);
    if (uFormat == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Rejecting unsupported host clipboard MIME type for provideData: %s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                        tr("Clipboard MIME type '%s' is not supported"), aMimeType.c_str());
    }
    if (aBuffer.empty())
    {
        LogFunc(("Rejecting empty host clipboard data: mime=%s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard data must not be empty"));
    }
    if (aBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Rejecting oversized host clipboard data: mime=%s, cb=%zu, max=%RU32\n",
                 aMimeType.c_str(), aBuffer.size(), s_cbClipboardReadMax));
        return setError(VBOX_E_SHCL_TOO_MUCH_DATA,
                        tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                        s_cbClipboardReadMax);
    }

    std::vector<BYTE> abProtocolBuffer;
    int vrc = clipboardMainToProtocolData(uFormat, aBuffer, abProtocolBuffer);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Converting host clipboard data failed: format=%#x, mime=%s, cb=%zu, vrc=%Rrc\n",
                 uFormat, aMimeType.c_str(), aBuffer.size(), vrc));
        return setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                            tr("Converting shared clipboard data failed with %Rrc"), vrc);
    }
    if (abProtocolBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Converted host clipboard data is too large: format=%#x, cb=%zu, max=%RU32\n",
                 uFormat, abProtocolBuffer.size(), s_cbClipboardReadMax));
        return setErrorBoth(VBOX_E_SHCL_TOO_MUCH_DATA, VERR_TOO_MUCH_DATA,
                            tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                            s_cbClipboardReadMax);
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    uint32_t fCurrentFormats = VBOX_SHCL_FMT_NONE;
    ClipboardSource_T enmCurrentSource = ClipboardSource_Custom;
    bool fClientActive = false;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        fCurrentFormats = m_fFormats;
        enmCurrentSource = mData->mSource;
        fClientActive = i_isClientIdRegisteredLocked(aClientId);
    }
    if (!fClientActive)
    {
        LogFunc(("Rejecting host clipboard data from inactive session clientId=%RU32\n", aClientId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is not active"), aClientId);
    }
    HRESULT hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, aSource))
    {
        LogFunc(("Rejecting host clipboard data from source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }
    if (   enmCurrentSource != aSource
        || !(fCurrentFormats & uFormat))
    {
        LogFunc(("Rejecting host clipboard data for unadvertised format: currentSource=%s, requestedSource=%s, currentFormats=%#x, format=%#x\n",
                 clipboardSourceToLogString(enmCurrentSource), clipboardSourceToLogString(aSource), fCurrentFormats, uFormat));
        return setError(VBOX_E_SHCL_NO_DATA,
                        tr("The requested host clipboard data format is not currently advertised"));
    }

    AutoCaller autoCaller(pParent);
    AssertComRCReturnRC(autoCaller.hrc());

    bool fFoundRequest = false;
    bool fMatchedRequest = false;
    bool fCurrentRequest = false;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));

        for (std::vector<Data::PendingDataRequest>::iterator it = mData->mPendingDataRequests.begin();
             it != mData->mPendingDataRequests.end(); ++it)
        {
            if (it->mId == aRequestId)
            {
                fFoundRequest = true;
                bool const fClientStillActive = i_isClientIdRegisteredLocked(aClientId);
                fMatchedRequest =    it->mClientId == aClientId
                                  && it->mAction == aAction
                                  && it->mSource == aSource
                                  && it->mFormat == uFormat;
                fCurrentRequest =    fMatchedRequest
                                  && fClientStillActive
                                  && mData->mCurrentClientId == aClientId
                                  && mData->mSource == aSource
                                  && (m_fFormats & uFormat);
                mData->mPendingDataRequests.erase(it);
                if (fCurrentRequest)
                    i_storeDataLocked(aAction, aSource, aMimeType, aBuffer);
                break;
            }
        }
    }
    if (!fFoundRequest)
    {
        LogFunc(("Rejecting host clipboard data for unknown request ID %RU32\n", (uint32_t)aRequestId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard request ID %RU32 is not pending"), (uint32_t)aRequestId);
    }
    if (!fMatchedRequest)
    {
        LogFunc(("Rejecting host clipboard data for mismatched request ID %RU32: clientId=%RU32, action=%s, source=%s, format=%#x\n",
                 (uint32_t)aRequestId, aClientId, clipboardActionToLogString(aAction), clipboardSourceToLogString(aSource), uFormat));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard request ID %RU32 does not match the supplied data"),
                        (uint32_t)aRequestId);
    }
    if (!fCurrentRequest)
    {
        LogFunc(("Rejecting host clipboard data for stale request ID %RU32: source=%s, format=%#x\n",
                 (uint32_t)aRequestId, clipboardSourceToLogString(aSource), uFormat));
        return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard request ID %RU32 is no longer current"),
                        (uint32_t)aRequestId);
    }

    i_fireDataChanged(aClientId, aAction, aSource, aMimeType, aBuffer);
    Log2Func(("Provided host clipboard data: requestId=%RU32, format=%#x, cb=%zu\n",
              (uint32_t)aRequestId, uFormat, aBuffer.size()));
    return S_OK;
}


/**
 * Sets guest-owned clipboard data on the native host clipboard endpoint.
 *
 * @retval  S_OK if the data was published to the native host clipboard.
 * @retval  E_FAIL if clipboard state becomes unavailable while publishing.
 * @retval  VBOX_E_SHCL_ERROR if an argument, session, service, mode query,
 *          conversion, or native backend operation fails.
 * @retval  VBOX_E_SHCL_ACCESS_DENIED if the source or Shared Clipboard mode
 *          does not permit publication to the host.
 * @retval  VBOX_E_SHCL_FORMAT_NOT_SUPPORTED if the MIME type is empty or unsupported.
 * @retval  VBOX_E_SHCL_NO_DATA if the payload is empty.
 * @retval  VBOX_E_SHCL_TOO_MUCH_DATA if the source or converted payload is too large.
 * @param   aClientId       Main clipboard client ID associated with the data.
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard source. Only ClipboardSource_Guest is accepted.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 */
HRESULT Clipboard::i_hostClipboardSetData(VBOXSHCLMAINCLIENTID aClientId,
                                          ClipboardAction_T aAction,
                                          ClipboardSource_T aSource,
                                          const com::Utf8Str &aMimeType,
                                          const std::vector<BYTE> &aBuffer)
{
    Log2Func(("clientId=%RU32, action=%s, source=%s, mime=%s, cb=%zu\n",
              aClientId, clipboardActionToLogString(aAction), clipboardSourceToLogString(aSource),
              aMimeType.c_str(), aBuffer.size()));
    if (!ShClMainIsValidAction(aAction))
    {
        LogFunc(("Rejecting invalid host clipboard action %RU32\n", (uint32_t)aAction));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard action %RU32"), (uint32_t)aAction);
    }
    if (!ShClMainIsValidSource(aSource))
    {
        LogFunc(("Rejecting invalid host clipboard source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ERROR, tr("Invalid clipboard source %RU32"), (uint32_t)aSource);
    }
    if (aSource != ClipboardSource_Guest)
    {
        LogFunc(("Rejecting host clipboard setData from unsupported source %RU32\n", (uint32_t)aSource));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Host clipboard publication currently accepts only guest clipboard data"));
    }
    if (aMimeType.isEmpty())
    {
        LogFunc(("Rejecting host clipboard setData with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }
    SHCLFORMAT const uFormat = consoleClipboardMimeTypeToFormat(aMimeType);
    if (uFormat == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Rejecting unsupported host clipboard MIME type for setData: %s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                        tr("Clipboard MIME type '%s' is not supported"), aMimeType.c_str());
    }
    if (aBuffer.empty())
    {
        LogFunc(("Rejecting empty host clipboard setData: mime=%s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_NO_DATA, tr("Clipboard data must not be empty"));
    }
    if (aBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Rejecting oversized host clipboard setData: mime=%s, cb=%zu, max=%RU32\n",
                 aMimeType.c_str(), aBuffer.size(), s_cbClipboardReadMax));
        return setError(VBOX_E_SHCL_TOO_MUCH_DATA,
                        tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                        s_cbClipboardReadMax);
    }

    std::vector<BYTE> abProtocolBuffer;
    int vrc = clipboardMainToProtocolData(uFormat, aBuffer, abProtocolBuffer);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Converting host clipboard data failed: format=%#x, mime=%s, cb=%zu, vrc=%Rrc\n",
                 uFormat, aMimeType.c_str(), aBuffer.size(), vrc));
        return setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                            tr("Converting shared clipboard data failed with %Rrc"), vrc);
    }
    if (abProtocolBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Converted host clipboard data is too large: format=%#x, cb=%zu, max=%RU32\n",
                 uFormat, abProtocolBuffer.size(), s_cbClipboardReadMax));
        return setErrorBoth(VBOX_E_SHCL_TOO_MUCH_DATA, VERR_TOO_MUCH_DATA,
                            tr("Writing shared clipboard data exceeded the supported size (%RU32 bytes)"),
                            s_cbClipboardReadMax);
    }

    ClipboardMode_T enmMode = ClipboardMode_Disabled;
    Console *pParent = NULL;
    bool fClientActive = false;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        fClientActive = i_isClientIdRegisteredLocked(aClientId);
    }
    if (!fClientActive)
    {
        LogFunc(("Rejecting host clipboard setData from inactive session clientId=%RU32\n", aClientId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is not active"), aClientId);
    }
    HRESULT hrc = ShClMainGetMode(pParent, &enmMode);
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the Shared Clipboard mode failed (%Rhrc)"), hrc);
    if (!ShClMainModeAllowsWrite(enmMode, aSource))
    {
        LogFunc(("Rejecting host clipboard setData from source %RU32 in clipboard mode %RU32\n",
                 (uint32_t)aSource, (uint32_t)enmMode));
        return setError(VBOX_E_SHCL_ACCESS_DENIED,
                        tr("Shared Clipboard mode %RU32 does not allow writing from source %RU32"),
                        (uint32_t)enmMode, (uint32_t)aSource);
    }

    AutoCaller autoCaller(pParent);
    AssertComRCReturnRC(autoCaller.hrc());

    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!i_isClientIdRegisteredLocked(aClientId))
        {
            LogFunc(("Rejecting stale host clipboard setData backend publication: clientId=%RU32\n", aClientId));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer active"), aClientId);
        }
    }

    GuestShCl *pShCl = GuestShCl::GetInst();
    if (!pShCl)
    {
        LogFunc(("Cannot set native host clipboard data without Shared Clipboard service: format=%#x\n", uFormat));
        return setErrorBoth(VBOX_E_SHCL_ERROR, VERR_INVALID_STATE,
                            tr("Writing shared clipboard data to the host failed because the "
                               "Shared Clipboard service is not available"));
    }
    vrc = pShCl->ReportFormatsToHost(uFormat);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Reporting setData format to native host clipboard failed: format=%#x, vrc=%Rrc\n", uFormat, vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to report clipboard format %#x to the native host clipboard, vrc=%Rrc\n",
                        uFormat, vrc));
        return setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                            tr("Reporting shared clipboard formats to the host failed with %Rrc"), vrc);
    }

    vrc = pShCl->WriteDataToHost(uFormat, abProtocolBuffer.empty() ? NULL : &abProtocolBuffer[0],
                                 (uint32_t)abProtocolBuffer.size());
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Writing setData payload to native host clipboard failed: format=%#x, cb=%zu, vrc=%Rrc\n",
                 uFormat, abProtocolBuffer.size(), vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to write clipboard data to the native host clipboard, format %#x, vrc=%Rrc\n",
                        uFormat, vrc));
        return setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                            tr("Writing shared clipboard data to the host failed with %Rrc"), vrc);
    }
    Log2Func(("Set native host clipboard data: format=%#x, cbMain=%zu, cbProtocol=%zu\n",
              uFormat, aBuffer.size(), abProtocolBuffer.size()));

    return S_OK;
}


/**
 * Clears the native host clipboard endpoint.
 *
 * @retval  S_OK if the native host clipboard was cleared or its service is unavailable.
 * @retval  E_FAIL if clipboard state becomes unavailable while clearing.
 * @retval  VBOX_E_SHCL_ERROR if the object, session, or native backend operation fails.
 * @param   aClientId       Main clipboard client ID associated with the endpoint.
 */
HRESULT Clipboard::i_hostClipboardClear(VBOXSHCLMAINCLIENTID aClientId)
{
    Log2Func(("clientId=%RU32\n", aClientId));

    Console *pParent = NULL;
    bool fClientActive = false;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard object is not initialized"));
        pParent = mData->mParent;
        fClientActive = i_isClientIdRegisteredLocked(aClientId);
    }
    if (!fClientActive)
    {
        LogFunc(("Rejecting host clipboard clear from inactive session clientId=%RU32\n", aClientId));
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is not active"), aClientId);
    }

    AutoCaller autoCaller(pParent);
    AssertComRCReturnRC(autoCaller.hrc());

    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!i_isClientIdRegisteredLocked(aClientId))
        {
            LogFunc(("Rejecting stale host clipboard clear backend publication: clientId=%RU32\n", aClientId));
            return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session client ID %RU32 is no longer active"), aClientId);
        }
    }

    GuestShCl *pShCl = GuestShCl::GetInst();
    if (!pShCl)
    {
        Log2Func(("Skipping native host clipboard clear because Shared Clipboard service is no longer available\n"));
        return S_OK;
    }
    int vrc = pShCl->ReportFormatsToHost(VBOX_SHCL_FMT_NONE);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Clearing native host clipboard failed: vrc=%Rrc\n", vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to clear the native host clipboard, vrc=%Rrc\n", vrc));
        return setErrorBoth(VBOX_E_SHCL_ERROR, vrc,
                            tr("Clearing the shared clipboard on the host failed with %Rrc"), vrc);
    }
    Log2Func(("Cleared native host clipboard endpoint\n"));

    return S_OK;
}


/**
 * Resets the Shared Clipboard service state.
 *
 * @returns COM status code.
 */
HRESULT Clipboard::i_reset()
{
    LogFunc(("Resetting shared clipboard service state\n"));
    AssertPtrReturn(mData, E_FAIL);
    AutoCaller autoCaller(mData->mParent);
    AssertComRCReturnRC(autoCaller.hrc());

    Console::SafeVMPtrQuiet ptrVM(mData->mParent);
    if (ptrVM.isOk())
    {
        VMMDev *pVMMDev = mData->mParent->i_getVMMDev();
        ptrVM.release();
        if (pVMMDev)
        {
            int vrc = pVMMDev->hgcmHostCall("VBoxSharedClipboard", VBOX_SHCL_HOST_FN_CANCEL, 0, NULL);
            if (RT_FAILURE(vrc))
            {
                LogFunc(("Reset HGCM host call failed: vrc=%Rrc\n", vrc));
                LogRelMax(16, ("Shared Clipboard: Failed to reset service state, vrc=%Rrc\n", vrc));
                return mData->mParent->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                    Console::tr("Resetting shared clipboard state failed with %Rrc"), vrc);
            }
            Log2Func(("Reset HGCM host call completed\n"));
        }
    }

    i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE, VBOX_SHCL_FMT_NONE, ClipboardSource_Custom);
    return S_OK;
}




# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Cancels a Shared Clipboard transfer by public ID after resolving it to one live Main record.
 *
 * @returns COM status code.
 * @param   aTransferId     Transfer identifier to cancel.
 */
HRESULT Clipboard::i_transferCancel(ULONG aTransferId)
{
    LogFunc(("Canceling transfer id=%RU32\n", (uint32_t)aTransferId));

    ComObjPtr<ClipboardTransferManager> ptrTransfers;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return E_FAIL;
        ptrTransfers = mData->mTransfers;
    }

    if (ptrTransfers.isNull())
        return E_FAIL;

    return ptrTransfers->i_cancelTransferById(aTransferId);
}


/**
 * Cancels a Shared Clipboard transfer using its private service key.
 *
 * @retval  E_POINTER           if @a pKey is NULL.
 * @retval  E_INVALIDARG        if @a pKey is invalid.
 * @retval  E_FAIL              if clipboard state is unavailable.
 * @retval  VBOX_E_IPRT_ERROR   if the HGCM cancellation request fails.
 * @returns                     COM status code from acquiring the parent console or VM.
 * @param   pKey                Host-side transfer key to cancel.
 */
HRESULT Clipboard::i_transferCancel(PCSHCLTRANSFERKEY pKey)
{
    AssertPtrReturn(pKey, E_POINTER);
    AssertReturn(ShClTransferKeyIsValid(pKey), E_INVALIDARG);
    AssertPtrReturn(mData, E_FAIL);

    LogFunc(("Canceling transfer session=%RU16 id=%RU16 generation=%RU64\n", ShClTransferKeyGetSessionId(pKey), ShClTransferKeyGetTransferId(pKey), pKey->uGeneration));

    AutoCaller autoCaller(mData->mParent);
    AssertComRCReturnRC(autoCaller.hrc());

    Console::SafeVMPtrQuiet ptrVM(mData->mParent);
    if (!ptrVM.isOk())
        return S_OK;
    VMMDev *pVMMDev = mData->mParent->i_getVMMDev();
    ptrVM.release();
    if (!pVMMDev)
        return S_OK;

    VBOXHGCMSVCPARM aParms[2];
    HGCMSvcSetU64(&aParms[0], pKey->uContextId);
    HGCMSvcSetU64(&aParms[1], pKey->uGeneration);

    int vrc = pVMMDev->hgcmHostCall("VBoxSharedClipboard", VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aParms), aParms);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Cancel transfer HGCM host call failed: session=%RU16, id=%RU16, generation=%RU64, vrc=%Rrc\n", ShClTransferKeyGetSessionId(pKey), ShClTransferKeyGetTransferId(pKey), pKey->uGeneration, vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to cancel transfer %RU16/%RU64 in session %RU16, vrc=%Rrc\n",
                       ShClTransferKeyGetTransferId(pKey), pKey->uGeneration,
                       ShClTransferKeyGetSessionId(pKey), vrc));
        return mData->mParent->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                            Console::tr("Canceling shared clipboard transfer failed with %Rrc"), vrc);
    }
    Log2Func(("Canceled transfer session=%RU16 id=%RU16 generation=%RU64\n", ShClTransferKeyGetSessionId(pKey), ShClTransferKeyGetTransferId(pKey), pKey->uGeneration));
    return S_OK;
}


/**
 * Queues local transfer teardown after a service reset or backend teardown.
 *
 * This deliberately does not make an HGCM host call; it is itself invoked from
 * the service extension callback.  Publications are assigned to the manager's
 * COM-MTA worker so active API listeners never run on the callback thread.
 */
void Clipboard::i_resetTransfersFromService()
{
    ComObjPtr<ClipboardTransferManager> ptrTransfers;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mData)
            ptrTransfers = mData->mTransfers;
    }

    if (ptrTransfers.isNotNull())
        ptrTransfers->i_resetFromService();
}


/**
 * Handles a Shared Clipboard transfer lifecycle status delivered by the host service.
 *
 * @returns COM status code.
 * @param   pKey                Host-side transfer key.
 * @param   aTransfer           Borrowed service transfer backing the data plane.
 * @param   enmShClSource       Data source recorded by the backing transfer.
 * @param   enmStatus           Transfer lifecycle status.
 * @param   vrcTransfer         Transfer status result code.
 */
HRESULT Clipboard::i_handleTransferStatus(PCSHCLTRANSFERKEY pKey,
                                          PSHCLTRANSFER aTransfer,
                                          SHCLSOURCE enmShClSource,
                                          SHCLTRANSFERSTATUS enmStatus,
                                          int vrcTransfer)
{
    ComObjPtr<ClipboardTransferManager> ptrTransfers;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return E_FAIL;
        ptrTransfers = mData->mTransfers;
    }

    if (ptrTransfers.isNull())
        return E_FAIL;

    return ptrTransfers->i_handleTransferStatus(pKey, aTransfer, enmShClSource, enmStatus, vrcTransfer);
}


/**
 * Updates a Shared Clipboard transfer's byte progress.
 *
 * @returns COM status code.
 * @param   pKey                Host-side transfer key.
 * @param   cbProcessed         Number of bytes processed so far.
 * @param   cbTotal             Total number of bytes to process.
 */
HRESULT Clipboard::i_handleTransferProgress(PCSHCLTRANSFERKEY pKey,
                                            uint64_t cbProcessed,
                                            uint64_t cbTotal)
{
    ComObjPtr<ClipboardTransferManager> ptrTransfers;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (!mData)
            return E_FAIL;
        ptrTransfers = mData->mTransfers;
    }

    if (ptrTransfers.isNull())
        return E_FAIL;

    return ptrTransfers->i_handleTransferProgress(pKey, cbProcessed, cbTotal);
}
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Reads cached Main API clipboard data for a service data request.
 *
 * @retval  S_OK if matching cached or native host data was returned.
 * @retval  E_POINTER if @a pcbActual is invalid, or @a pvData is invalid for
 *          a non-empty destination buffer.
 * @retval  E_INVALIDARG if @a uFormat is unsupported.
 * @retval  E_FAIL if clipboard state, advertised data, the native host
 *          provider, conversion, or the resulting data size is unsuitable.
 * @param   uFormat         Requested Shared Clipboard format.
 * @param   pvData          Destination buffer.
 * @param   cbData          Size of destination buffer.
 * @param   pcbActual       Where to return the required data size.
 */
HRESULT Clipboard::i_readDataForGuest(uint32_t uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    Log2Func(("uFormat=%#x, pvData=%p, cbData=%RU32\n", uFormat, pvData, cbData));
    AssertPtrReturn(pcbActual, E_POINTER);
    *pcbActual = 0;
    if (cbData)
        AssertPtrReturn(pvData, E_POINTER);

    const char *pszMimeType = consoleClipboardFormatToMimeType((SHCLFORMAT)uFormat);
    if (!pszMimeType)
    {
        LogFunc(("Service requested unsupported format: uFormat=%#x\n", uFormat));
        LogRelMax(16, ("Shared Clipboard: Service requested unsupported clipboard format %#x\n", uFormat));
        return E_INVALIDARG;
    }

    ClipboardAction_T const enmRequestAction = ClipboardAction_Copy;
    ClipboardSource_T enmRequestSource;
    VBOXSHCLMAINCLIENTID idRequestClient;
    uint32_t fCurrentFormats;
    uint64_t uLastItemSerial;
#ifdef RT_OS_DARWIN
    bool fHadMatchingHostData;
    bool fWantRequestPayload;
#endif
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        enmRequestSource = mData->mSource;
        idRequestClient = mData->mCurrentClientId;
        fCurrentFormats = m_fFormats;
        uLastItemSerial = mData->mLastItemSerial;
#ifdef RT_OS_DARWIN
        fHadMatchingHostData =    enmRequestSource == ClipboardSource_Host
                               && mData->mfHaveLastItem
                               && mData->mLastItemSource == enmRequestSource
                               && mData->mLastItemAction == enmRequestAction
                               && consoleClipboardMimeTypeToFormat(mData->mLastItemMimeType) == uFormat;
        fWantRequestPayload = false;
        for (std::vector<Data::SessionRecord>::const_iterator it = mData->mSessions.begin();
             it != mData->mSessions.end(); ++it)
            if (it->mEventSource.isNotNull() && (it->mfFlags & IClipboardSessionFlag_IncludePayload))
            {
                fWantRequestPayload = true;
                break;
            }
#endif
    }
    if (   enmRequestSource != ClipboardSource_Host
        && enmRequestSource != ClipboardSource_Guest)
    {
        Log2Func(("No clipboard data advertised for service request: format=%#x, source=%s\n",
                  uFormat, clipboardSourceToLogString(enmRequestSource)));
        return E_FAIL;
    }
    if (!(fCurrentFormats & uFormat))
    {
        Log2Func(("Service requested format not currently advertised: requested=%#x, advertised=%#x\n",
                  uFormat, fCurrentFormats));
        return E_FAIL;
    }
    GuestShCl *pShCl = NULL;
    bool fHostReadAttempted = false;
    int vrcHostRead = VERR_NO_DATA;
    const std::vector<BYTE> *pResolvedHostData = NULL;
#ifdef RT_OS_DARWIN
    std::vector<BYTE> abResolvedHostData;
    /* Keep the established provider-first order unless a macOS session explicitly requested the payload. */
    if (   enmRequestSource == ClipboardSource_Host
        && uFormat == VBOX_SHCL_FMT_UNICODETEXT
        && !fHadMatchingHostData
        && fWantRequestPayload
        && cbData)
    {
        pShCl = GuestShCl::GetInst();
        if (pShCl)
        {
            fHostReadAttempted = true;
            vrcHostRead = pShCl->ReadDataFromHost((SHCLFORMAT)uFormat, pvData, cbData, pcbActual);
            if (   RT_SUCCESS(vrcHostRead)
                && *pcbActual > 0
                && *pcbActual <= cbData
                && *pcbActual <= s_cbClipboardReadMax)
            {
                int const vrc2 = clipboardProtocolToMainData((SHCLFORMAT)uFormat, pvData, *pcbActual,
                                                             abResolvedHostData);
                if (RT_SUCCESS(vrc2) && abResolvedHostData.size() <= s_cbClipboardReadMax)
                    pResolvedHostData = &abResolvedHostData;
                else
                    Log2Func(("Not attaching native host text to request event: format=%#x, cbActual=%RU32, vrc=%Rrc\n",
                              uFormat, *pcbActual, vrc2));
            }
        }
    }
#endif

    ULONG const idRequest = i_fireDataRequested(idRequestClient, enmRequestAction, enmRequestSource, uFormat,
                                                pResolvedHostData);
    if (idRequest == 0)
        Log2Func(("No pending data request was registered for service request: format=%#x\n", uFormat));

    bool fHaveMainBuffer = false;
    bool fSuppliedForRequest = false;
    std::vector<BYTE> abMainBuffer;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        if (!mData->mfHaveLastItem)
            Log2Func(("No cached clipboard data for service request: format=%#x\n", uFormat));
        else if (mData->mLastItemSource != enmRequestSource)
            Log2Func(("Cached data source mismatch for service request: requested=%s, cached=%s\n",
                      clipboardSourceToLogString(enmRequestSource), clipboardSourceToLogString(mData->mLastItemSource)));
        else if (mData->mLastItemAction != enmRequestAction)
            Log2Func(("Cached data action mismatch for service request: requested=%s, cached=%s\n",
                      clipboardActionToLogString(enmRequestAction), clipboardActionToLogString(mData->mLastItemAction)));
        else if (consoleClipboardMimeTypeToFormat(mData->mLastItemMimeType) != uFormat)
            Log2Func(("Cached MIME type mismatch for service request: requested=%s, cached=%s\n",
                      pszMimeType, mData->mLastItemMimeType.c_str()));
        else
        {
            fSuppliedForRequest = mData->mLastItemSerial != uLastItemSerial;
            abMainBuffer = mData->mLastItemBuffer;
            fHaveMainBuffer = true;
            Log2Func(("Using %s clipboard data for service request: format=%#x, serial=%RU64\n",
                      fSuppliedForRequest ? "newly supplied" : "cached", uFormat, mData->mLastItemSerial));
        }
    }

    if (!fHaveMainBuffer)
    {
        if (enmRequestSource != ClipboardSource_Host)
        {
            i_removePendingDataRequest(idRequest);
            return E_FAIL;
        }
        if (!cbData)
        {
            Log2Func(("Cannot read host backend data without a destination buffer: format=%#x\n", uFormat));
            i_removePendingDataRequest(idRequest);
            return E_FAIL;
        }

        if (!pShCl)
            pShCl = GuestShCl::GetInst();
        if (!pShCl)
        {
            LogFunc(("Cannot read native host data for guest request without Shared Clipboard service: format=%#x\n",
                     uFormat));
            i_removePendingDataRequest(idRequest);
            return E_FAIL;
        }
        if (!fHostReadAttempted)
        {
            fHostReadAttempted = true;
            vrcHostRead = pShCl->ReadDataFromHost((SHCLFORMAT)uFormat, pvData, cbData, pcbActual);
        }
        if (RT_FAILURE(vrcHostRead))
        {
            Log2Func(("Reading native host data for guest request failed: format=%#x, vrc=%Rrc\n",
                      uFormat, vrcHostRead));
            i_removePendingDataRequest(idRequest);
            return E_FAIL;
        }
        i_removePendingDataRequest(idRequest);
        Log2Func(("Read native host data for guest request: format=%#x, cbActual=%RU32, cbProvided=%RU32\n",
                  uFormat, *pcbActual, cbData));
        return S_OK;
    }

    RT_NOREF(fSuppliedForRequest);
    i_removePendingDataRequest(idRequest);

    std::vector<BYTE> abProtocolBuffer;
    int vrc = clipboardMainToProtocolData((SHCLFORMAT)uFormat, abMainBuffer, abProtocolBuffer);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Converting cached data for service request failed: format=%#x, cbMain=%zu, vrc=%Rrc\n",
                 uFormat, abMainBuffer.size(), vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to convert cached clipboard data for service request: format %#x, %zu bytes, vrc=%Rrc\n",
                       uFormat, abMainBuffer.size(), vrc));
        return E_FAIL;
    }

    size_t const cbActual = abProtocolBuffer.size();
    if (cbActual > UINT32_MAX)
    {
        LogFunc(("Cached data for service request is too large: format=%#x, cb=%zu\n", uFormat, cbActual));
        LogRelMax(16, ("Shared Clipboard: Cached clipboard data is too large for service request: format %#x, %zu bytes\n",
                       uFormat, cbActual));
        return E_FAIL;
    }
    *pcbActual = (uint32_t)cbActual;
    if (cbData && cbActual)
        memcpy(pvData, &abProtocolBuffer[0], RT_MIN((size_t)cbData, cbActual));
    Log2Func(("Read cached data for service request: format=%#x, cbActual=%zu, cbProvided=%RU32\n", uFormat, cbActual, cbData));
    return S_OK;
}


// IInternalClipboardControl methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Requests clipboard data from the current source and registers a pending request.
 *
 * @returns COM status code.
 * @param   aMimeType       MIME type to request.
 * @param   aRequestId      Where to return the pending request identifier.
 */
HRESULT Clipboard::requestData(const com::Utf8Str &aMimeType, ULONG *aRequestId)
{
    return i_requestData(aMimeType, aRequestId);
}


/**
 * Sets a Shared Clipboard transfer status for Main testcase coverage.
 *
 * @returns COM status code.
 * @param   aServiceSessionId   Shared Clipboard service session identifier.
 * @param   aTransferId         Shared Clipboard transfer identifier.
 * @param   aGeneration         Host-private transfer generation.
 * @param   aSource             Data source associated with the status.
 * @param   aStatus             Internal Shared Clipboard transfer status value.
 * @param   aResult             IPRT result associated with the status.
 */
HRESULT Clipboard::setTransferStatus(ULONG aServiceSessionId,
                                     ULONG aTransferId,
                                     LONG64 aGeneration,
                                     ClipboardSource_T aSource,
                                     ULONG aStatus,
                                     LONG aResult)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RT_NOREF(aServiceSessionId, aTransferId, aGeneration, aSource, aStatus, aResult);
    ReturnComNotImplemented();
#else
    if (   aServiceSessionId == 0
        || aServiceSessionId >= NIL_SHCLSESSIONID
        || aTransferId == 0
        || aTransferId >= NIL_SHCLTRANSFERID
        || aGeneration <= 0)
        return E_INVALIDARG;

    SHCLSOURCE enmShClSource;
    switch (aSource)
    {
        case ClipboardSource_Host:
            enmShClSource = SHCLSOURCE_LOCAL;
            break;
        case ClipboardSource_Guest:
            enmShClSource = SHCLSOURCE_REMOTE;
            break;
        default:
            return E_INVALIDARG;
    }

    SHCLTRANSFERKEY Key;
    ShClTransferKeyInit(&Key, (SHCLSESSIONID)aServiceSessionId, (SHCLTRANSFERID)aTransferId,
                        (SHCLTRANSFERGEN)aGeneration);
    HRESULT const hrc = i_handleTransferStatus(&Key,
                                               NULL /* pTransfer */,
                                               enmShClSource,
                                               (SHCLTRANSFERSTATUS)aStatus,
                                               (int)aResult);
    return hrc;
#endif
}


/**
 * Requests clipboard data from the current source and registers a pending request.
 *
 * @returns COM status code.
 * @param   aMimeType       MIME type to request.
 * @param   aRequestId      Where to return the pending request identifier.
 */
HRESULT Clipboard::i_requestData(const com::Utf8Str &aMimeType, ULONG *aRequestId)
{
    Log2Func(("mime=%s\n", aMimeType.c_str()));
    AssertPtrReturn(aRequestId, E_POINTER);
    *aRequestId = 0;

    if (aMimeType.isEmpty())
    {
        LogFunc(("Rejecting data request with empty MIME type\n"));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED, tr("Clipboard MIME type must not be empty"));
    }

    SHCLFORMAT const uFormat = consoleClipboardMimeTypeToFormat(aMimeType);
    if (uFormat == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Rejecting data request for unsupported MIME type: %s\n", aMimeType.c_str()));
        return setError(VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                        tr("Clipboard MIME type '%s' is not supported"), aMimeType.c_str());
    }

    ClipboardSource_T enmRequestSource;
    VBOXSHCLMAINCLIENTID idRequestClient;
    uint32_t fCurrentFormats;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, E_FAIL);
        enmRequestSource = mData->mSource;
        idRequestClient = mData->mCurrentClientId;
        fCurrentFormats = m_fFormats;
    }

    if (   enmRequestSource != ClipboardSource_Host
        && enmRequestSource != ClipboardSource_Guest)
    {
        Log2Func(("No clipboard source available for data request: mime=%s, source=%s\n",
                  aMimeType.c_str(), clipboardSourceToLogString(enmRequestSource)));
        return setError(VBOX_E_SHCL_NO_DATA, tr("No clipboard source is currently available"));
    }
    if (!(fCurrentFormats & uFormat))
    {
        Log2Func(("Requested MIME type is not currently advertised: mime=%s, format=%#x, advertised=%#x\n",
                  aMimeType.c_str(), uFormat, fCurrentFormats));
        return setError(VBOX_E_SHCL_NO_DATA, tr("The requested clipboard format is not currently advertised"));
    }

    ULONG const idRequest = i_fireDataRequested(idRequestClient, ClipboardAction_Copy, enmRequestSource, uFormat);
    if (idRequest == 0)
    {
        LogFunc(("Registering data request failed: source=%s, format=%#x\n",
                 clipboardSourceToLogString(enmRequestSource), uFormat));
        return setError(VBOX_E_SHCL_ERROR, tr("Creating the clipboard data request failed"));
    }

    *aRequestId = idRequest;
    return S_OK;
}


/**
 * Records clipboard data reported by the Shared Clipboard service and notifies listeners.
 *
 * @returns COM status code.
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard source.
 * @param   fFormat         Shared Clipboard format mask or single format bit.
 * @param   pvData          Payload bytes.
 * @param   cbData          Payload size.
 */
HRESULT Clipboard::i_reportData(ClipboardAction_T aAction, ClipboardSource_T aSource, uint32_t fFormat,
                                const void *pvData, uint32_t cbData)
{
    Log2Func(("action=%s, source=%s, fFormat=%#x, pvData=%p, cbData=%RU32\n",
              clipboardActionToLogString(aAction), clipboardSourceToLogString(aSource), fFormat, pvData, cbData));
    if (cbData && !pvData)
    {
        LogFunc(("Reported data has invalid pointer: fFormat=%#x, cbData=%RU32\n", fFormat, cbData));
        LogRelMax(16, ("Shared Clipboard: Service reported clipboard data without a buffer: formats %#x, %RU32 bytes\n",
                       fFormat, cbData));
        return E_POINTER;
    }
    if (cbData > s_cbClipboardReadMax)
    {
        LogFunc(("Reported data is too large: fFormat=%#x, cbData=%RU32, max=%RU32\n",
                 fFormat, cbData, s_cbClipboardReadMax));
        LogRelMax(16, ("Shared Clipboard: Service reported too much clipboard data: formats %#x, %RU32 bytes (limit %RU32 bytes)\n",
                       fFormat, cbData, s_cbClipboardReadMax));
        return E_FAIL;
    }

    SHCLFORMAT const uFormat = clipboardPickFormat(fFormat);
    if (uFormat == VBOX_SHCL_FMT_NONE)
    {
        LogFunc(("Reported data has no supported format: fFormat=%#x\n", fFormat));
        LogRelMax(16, ("Shared Clipboard: Service reported unsupported clipboard formats %#x\n", fFormat));
        return E_INVALIDARG;
    }

    const char *pszMimeType = consoleClipboardFormatToMimeType(uFormat);
    AssertReturn(pszMimeType, E_FAIL);

    std::vector<BYTE> abBuffer;
    int vrc = clipboardProtocolToMainData(uFormat, pvData, cbData, abBuffer);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Converting reported data failed: uFormat=%#x, cbData=%RU32, vrc=%Rrc\n", uFormat, cbData, vrc));
        LogRelMax(16, ("Shared Clipboard: Failed to convert reported clipboard data: format %#x, %RU32 bytes, vrc=%Rrc\n",
                       uFormat, cbData, vrc));
        return E_FAIL;
    }
    if (abBuffer.size() > s_cbClipboardReadMax)
    {
        LogFunc(("Converted reported data is too large: uFormat=%#x, cb=%zu, max=%RU32\n",
                 uFormat, abBuffer.size(), s_cbClipboardReadMax));
        LogRelMax(16, ("Shared Clipboard: Converted reported clipboard data is too large: format %#x, %zu bytes (limit %RU32 bytes)\n",
                       uFormat, abBuffer.size(), s_cbClipboardReadMax));
        return E_FAIL;
    }

    com::Utf8Str strMimeType(pszMimeType);
    i_storeData(aAction, aSource, strMimeType, abBuffer);
    i_reportFormats(VBOX_SHCL_MAIN_CLIENT_NONE, uFormat, aSource);
    i_fireDataChanged(VBOX_SHCL_MAIN_CLIENT_NONE, aAction, aSource, strMimeType, abBuffer);
    Log2Func(("Reported data accepted: source=%s, format=%#x, mime=%s, cb=%zu\n",
              clipboardSourceToLogString(aSource), uFormat, strMimeType.c_str(), abBuffer.size()));
    return S_OK;
}


/**
 * Records the currently available Shared Clipboard formats and notifies listeners.
 *
 * @param   aClientId       Originating client identifier for the format report.
 * @param   fFormats        Shared Clipboard format mask.
 * @param   aSource         Clipboard source reporting the formats.
 * @param   fForceNotify    Whether to force notification regardless whether the formats or source
 *                          have been changed or not.
 */
bool Clipboard::i_reportFormats(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFormats, ClipboardSource_T aSource,
                                bool fForceNotify, bool fRequireCurrentClientId,
                                VBOXSHCLMAINCLIENTID aRequiredCurrentClientId)
{
    Log2Func(("clientId=%RU32, fFormats=%#x, source=%s, fForceNotify=%RTbool, fRequireCurrentClientId=%RTbool, requiredClientId=%RU32\n",
              aClientId, fFormats, clipboardSourceToLogString(aSource), fForceNotify,
              fRequireCurrentClientId, aRequiredCurrentClientId));
    ComPtr<IEventSource> ptrEventSource;
    uint32_t fOldFormats = VBOX_SHCL_FMT_NONE;
    ClipboardSource_T enmOldSource = ClipboardSource_Custom;
    bool fFormatsChanged = false;
    bool fSourceChanged = false;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, false);

        if (   fRequireCurrentClientId
            && mData->mCurrentClientId != aRequiredCurrentClientId)
        {
            LogFunc(("Ignoring conditional format report: requiredClientId=%RU32, currentClientId=%RU32\n",
                     aRequiredCurrentClientId, mData->mCurrentClientId));
            return false;
        }
        if (   aClientId != VBOX_SHCL_MAIN_CLIENT_NONE
            && !i_isClientIdRegisteredLocked(aClientId))
        {
            LogFunc(("Ignoring format report from inactive session clientId=%RU32\n", aClientId));
            return false;
        }

        fOldFormats = m_fFormats;
        enmOldSource = mData->mSource;
        fFormatsChanged = fOldFormats != fFormats;
        fSourceChanged = enmOldSource != aSource;
        m_fFormats = fFormats;
        mData->mSource = aSource;
        mData->mCurrentClientId = fFormats != VBOX_SHCL_FMT_NONE ? aClientId : VBOX_SHCL_MAIN_CLIENT_NONE;
        if (fFormatsChanged || fSourceChanged || fForceNotify)
            i_clearPendingDataRequestsLocked();

        if (aClientId != VBOX_SHCL_MAIN_CLIENT_NONE)
            for (std::vector<Data::SessionRecord>::iterator it = mData->mSessions.begin(); it != mData->mSessions.end(); ++it)
                if (it->mClientId == aClientId)
                {
                    it->mfHaveReflectionFormats = true;
                    it->mfReflectionFormats = fFormats;
                    it->mReflectionSource = aSource;
                    break;
                }

        SHCLFORMAT const uLastFormat = consoleClipboardMimeTypeToFormat(mData->mLastItemMimeType);
        if (   mData->mfHaveLastItem
            && (   (fForceNotify && mData->mLastItemSource == aSource)
                || mData->mLastItemSource != aSource
                || uLastFormat == VBOX_SHCL_FMT_NONE
                || !(fFormats & uLastFormat)))
        {
            Log2Func(("Dropping cached item: cacheSource=%s, newSource=%s, cacheFormat=%#x, newFormats=%#x, fForceNotify=%RTbool\n",
                      clipboardSourceToLogString(mData->mLastItemSource), clipboardSourceToLogString(aSource),
                      uLastFormat, fFormats, fForceNotify));
            mData->mLastItemBuffer.clear();
            mData->mfHaveLastItem = false;
            mData->mLastItemSerial++;
        }

        mData->mEventSource.queryInterfaceTo(ptrEventSource.asOutParam());
    }

    if (fFormatsChanged || fSourceChanged || fForceNotify)
    {
        if (fFormatsChanged || fSourceChanged)
            LogRel2(("Shared Clipboard: %s clipboard formats changed from %#x (%s) to %#x (%s)\n",
                     clipboardSourceToLogString(aSource), fOldFormats, clipboardSourceToLogString(enmOldSource),
                     fFormats, clipboardSourceToLogString(aSource)));
        else
            LogRel2(("Shared Clipboard: %s clipboard formats reported as %#x\n",
                     clipboardSourceToLogString(aSource), fFormats));
    }

    const bool fNotifyFormats = fFormatsChanged || fForceNotify;

    std::vector<ComPtr<IClipboardFormat> > vecFormats;
    if (fNotifyFormats)
    {
        std::vector<com::Utf8Str> vecMimeTypes;
        consoleClipboardFormatsToMimeTypes(fFormats, vecMimeTypes);
        for (std::vector<com::Utf8Str>::const_iterator it = vecMimeTypes.begin(); it != vecMimeTypes.end(); ++it)
        {
            ComPtr<IClipboardFormat> ptrFormat;
            HRESULT hrc = i_createFormat(*it, ptrFormat);
            if (FAILED(hrc))
            {
                LogFunc(("Creating format-change format object failed: source=%s, mime=%s, hrc=%#x\n",
                         clipboardSourceToLogString(aSource), it->c_str(), hrc));
                vecFormats.clear();
                break;
            }
            vecFormats.push_back(ptrFormat);
        }
    }

    LONG64 const i64Revision = fSourceChanged || fNotifyFormats ? i_nextEventRevision() : 0;
    com::SafeIfaceArray<IClipboardFormat> aEventFormats(vecFormats);
    ComPtr<IClipboardItem> ptrEventItem;
    if (ptrEventSource.isNotNull())
    {
        if (fSourceChanged)
        {
            Log2Func(("Firing source changed event: source=%s, revision=%RI64, clientId=%RU32\n",
                      clipboardSourceToLogString(aSource), i64Revision, aClientId));
            ::FireClipboardSourceChangedEvent(ptrEventSource, i64Revision, aClientId, aSource);
        }
        if (fNotifyFormats)
        {
            Log2Func(("Firing format changed event: source=%s, fFormats=%#x, cFormats=%zu, revision=%RI64, clientId=%RU32\n",
                      clipboardSourceToLogString(aSource), fFormats, vecFormats.size(), i64Revision, aClientId));
            ::FireClipboardFormatChangedEvent(ptrEventSource, i64Revision, aClientId, com::Utf8Str(), ptrEventItem,
                                              FALSE /* aVeto */, aSource, ComSafeArrayAsInParam(aEventFormats));
        }
    }
    else
        Log3Func(("No event source for reported formats: fFormats=%#x\n", fFormats));

    if (fSourceChanged || fNotifyFormats)
    {
        std::vector<SessionEventTarget> vecTargets;
        i_getSessionEventTargets(vecTargets, aClientId, true /* fPassive */, true /* fCheckReflection */, fFormats, aSource);
        for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
        {
            if (fSourceChanged)
                ::FireClipboardSourceChangedEvent(it->mEventSource, i64Revision, aClientId, aSource);
            if (fNotifyFormats)
                ::FireClipboardFormatChangedEvent(it->mEventSource, i64Revision, aClientId, com::Utf8Str(), ptrEventItem,
                                                  FALSE /* aVeto */, aSource, ComSafeArrayAsInParam(aEventFormats));
        }
    }

    return true;
}


/**
 * Fires a clipboard data changed event.
 *
 * @param   aClientId       Originating client identifier for the event.
 * @param   aAction         Clipboard action associated with the data.
 * @param   aSource         Clipboard source.
 * @param   aMimeType       MIME type of the payload.
 * @param   aBuffer         Payload bytes.
 */
void Clipboard::i_fireDataChanged(VBOXSHCLMAINCLIENTID aClientId,
                                  ClipboardAction_T aAction,
                                  ClipboardSource_T aSource,
                                  const com::Utf8Str &aMimeType,
                                  const std::vector<BYTE> &aBuffer)
{
    ComPtr<IEventSource> ptrEventSource;
    HRESULT hrc = getEventSource(ptrEventSource);
    if (FAILED(hrc) || ptrEventSource.isNull())
    {
        Log3Func(("No event source for data changed event: hrc=%#x\n", hrc));
        return;
    }

    ComPtr<IClipboardItem> ptrItem;
    hrc = i_createItem(aSource, aMimeType, aBuffer, ptrItem);
    if (FAILED(hrc))
    {
        LogFunc(("Creating data-changed item failed: action=%RU32, source=%RU32, mime=%s, cb=%zu, hrc=%#x\n",
                 (uint32_t)aAction, (uint32_t)aSource, aMimeType.c_str(), aBuffer.size(), hrc));
        return;
    }

    LONG64 const i64Revision = i_nextEventRevision();
    Log2Func(("Firing data changed event: action=%RU32, source=%RU32, mime=%s, cb=%zu, revision=%RI64, clientId=%RU32\n",
              (uint32_t)aAction, (uint32_t)aSource, aMimeType.c_str(), aBuffer.size(), i64Revision, aClientId));
    ::FireClipboardDataChangedEvent(ptrEventSource, i64Revision, aClientId, com::Utf8Str(), ptrItem,
                                    FALSE /* aVeto */, aAction);

    SHCLFORMAT const uFormat = consoleClipboardMimeTypeToFormat(aMimeType);
    std::vector<SessionEventTarget> vecTargets;
    i_getSessionEventTargets(vecTargets, aClientId, true /* fPassive */, uFormat != VBOX_SHCL_FMT_NONE /* fCheckReflection */,
                             uFormat, aSource);
    for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
    {
        ComPtr<IClipboardItem> ptrSessionItem;
        if (it->mfFlags & IClipboardSessionFlag_IncludePayload)
            ptrSessionItem = ptrItem;
        ::FireClipboardDataChangedEvent(it->mEventSource, i64Revision, aClientId, com::Utf8Str(), ptrSessionItem,
                                        FALSE /* aVeto */, aAction);
    }
}


/**
 * Fires a clipboard data requested event.
 *
 * @param   aClientId       Originating client identifier for the request.
 * @param   aAction         Clipboard action associated with the request.
 * @param   aSource         Clipboard source from which data is requested.
 * @param   uFormat         Shared Clipboard format requested.
 * @param   pResolvedBuffer Optional canonical Main payload already read for the request.
 */
ULONG Clipboard::i_fireDataRequested(VBOXSHCLMAINCLIENTID aClientId,
                                     ClipboardAction_T aAction,
                                     ClipboardSource_T aSource,
                                     uint32_t uFormat,
                                     const std::vector<BYTE> *pResolvedBuffer)
{
    const char *pszMimeType = consoleClipboardFormatToMimeType((SHCLFORMAT)uFormat);
    if (!pszMimeType)
    {
        LogFunc(("Cannot fire data-requested event for unsupported format %#x\n", uFormat));
        return 0;
    }
    com::Utf8Str strMimeType(pszMimeType);

    ComPtr<IEventSource> ptrEventSource;
    HRESULT hrc = getEventSource(ptrEventSource);
    if (FAILED(hrc) || ptrEventSource.isNull())
    {
        Log3Func(("No event source for data requested event: hrc=%#x\n", hrc));
        return 0;
    }

    ComPtr<IClipboardFormat> ptrFormat;
    hrc = i_createFormat(strMimeType, ptrFormat);
    if (FAILED(hrc))
    {
        LogFunc(("Creating data-requested format failed: action=%RU32, source=%RU32, mime=%s, hrc=%#x\n",
                 (uint32_t)aAction, (uint32_t)aSource, strMimeType.c_str(), hrc));
        return 0;
    }

    ULONG idRequest;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        AssertPtrReturn(mData, 0);
        if (   mData->mSource != aSource
            || !(m_fFormats & uFormat))
        {
            Log2Func(("Not registering stale data request: source=%s, currentSource=%s, format=%#x, currentFormats=%#x\n",
                      clipboardSourceToLogString(aSource), clipboardSourceToLogString(mData->mSource), uFormat, m_fFormats));
            return 0;
        }

        idRequest = mData->mNextRequestId++;
        if (idRequest == 0)
            idRequest = mData->mNextRequestId++;
        if (mData->mNextRequestId == 0)
            mData->mNextRequestId = 1;

        if (mData->mPendingDataRequests.size() >= s_cClipboardPendingRequestsMax)
        {
            Log2Func(("Dropping oldest pending clipboard data request %RU32\n",
                      (uint32_t)mData->mPendingDataRequests.front().mId));
            mData->mPendingDataRequests.erase(mData->mPendingDataRequests.begin());
        }

        Data::PendingDataRequest req;
        req.mId = idRequest;
        req.mClientId = aClientId;
        req.mAction = aAction;
        req.mSource = aSource;
        req.mFormat = (SHCLFORMAT)uFormat;
        try
        {
            mData->mPendingDataRequests.push_back(req);
        }
        catch (std::bad_alloc &)
        {
            LogFunc(("Out of memory registering pending clipboard data request\n"));
            return 0;
        }
    }

    LONG64 const i64Revision = i_nextEventRevision();
    Log2Func(("Firing data requested event: requestId=%RU32, action=%RU32, source=%RU32, mime=%s, revision=%RI64, clientId=%RU32\n",
              (uint32_t)idRequest, (uint32_t)aAction, (uint32_t)aSource, strMimeType.c_str(), i64Revision, aClientId));
    ComPtr<IEvent> ptrEvent;
    hrc = ::CreateClipboardDataRequestedEvent(ptrEvent.asOutParam(), ptrEventSource, i64Revision, aClientId,
                                              com::Utf8Str(), NULL /* aItem */, FALSE /* aVeto */,
                                              idRequest, aAction, aSource, ptrFormat);
    if (FAILED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        i_removePendingDataRequestLocked(idRequest);
        LogFunc(("Creating data requested event failed: requestId=%RU32, hrc=%#x\n", (uint32_t)idRequest, hrc));
        return 0;
    }

    BOOL fDelivered = FALSE;
    hrc = ptrEventSource->FireEvent(ptrEvent, 0 /* aTimeout */, &fDelivered);
    if (FAILED(hrc))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        i_removePendingDataRequestLocked(idRequest);
        LogFunc(("Firing data requested event failed: requestId=%RU32, hrc=%#x\n", (uint32_t)idRequest, hrc));
        return 0;
    }

    std::vector<SessionEventTarget> vecTargets;
    i_getSessionEventTargets(vecTargets, aClientId, false /* fPassive */, false /* fCheckReflection */,
                             VBOX_SHCL_FMT_NONE, aSource);
    bool fIncludePayload = false;
    for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
        if (it->mfFlags & IClipboardSessionFlag_IncludePayload)
        {
            fIncludePayload = true;
            break;
        }

    std::vector<BYTE> abRequestBuffer;
    const std::vector<BYTE> *pRequestBuffer = NULL;
    if (fIncludePayload)
    {
        if (pResolvedBuffer)
            pRequestBuffer = pResolvedBuffer;
        else if (   aSource == ClipboardSource_Host
                 && uFormat == VBOX_SHCL_FMT_UNICODETEXT)
        {
            AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (   mData
                && mData->mfHaveLastItem
                && mData->mLastItemSource == aSource
                && mData->mLastItemAction == aAction
                && consoleClipboardMimeTypeToFormat(mData->mLastItemMimeType) == uFormat)
            {
                try
                {
                    abRequestBuffer = mData->mLastItemBuffer;
                    pRequestBuffer = &abRequestBuffer;
                }
                catch (std::bad_alloc &)
                {
                    LogFunc(("Out of memory copying cached data-requested payload\n"));
                }
            }
        }
    }

    ComPtr<IClipboardItem> ptrItem;
    if (pRequestBuffer)
    {
        try
        {
            hrc = i_createItem(aSource, strMimeType, *pRequestBuffer, ptrItem);
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
        if (FAILED(hrc))
        {
            LogFunc(("Creating data-requested item failed: requestId=%RU32, source=%RU32, mime=%s, cb=%zu, hrc=%#x\n",
                     (uint32_t)idRequest, (uint32_t)aSource, strMimeType.c_str(), pRequestBuffer->size(), hrc));
            ptrItem.setNull();
        }
    }
    for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
    {
        ComPtr<IClipboardItem> ptrSessionItem;
        if (it->mfFlags & IClipboardSessionFlag_IncludePayload)
            ptrSessionItem = ptrItem;
        ::FireClipboardDataRequestedEvent(it->mEventSource, i64Revision, aClientId, com::Utf8Str(), ptrSessionItem,
                                          FALSE /* aVeto */, idRequest, aAction, aSource, ptrFormat);
    }

    return idRequest;
}


/**
 * Fires a clipboard error event.
 *
 * @param   aId             Clipboard identifier.
 * @param   aErrMsg         Error message.
 * @param   aRc             IPRT-style error code.
 */
void Clipboard::i_fireClipboardError(const com::Utf8Str &aId, const com::Utf8Str &aErrMsg, LONG aRc)
{
    ComPtr<IEventSource> ptrEventSource;
    HRESULT hrc = getEventSource(ptrEventSource);
    if (SUCCEEDED(hrc) && ptrEventSource.isNotNull())
    {
        LONG64 const i64Revision = i_nextEventRevision();
        LogFunc(("Firing clipboard error event: id=%s, rc=%RI32, msg=%s, revision=%RI64\n",
                 aId.c_str(), aRc, aErrMsg.c_str(), i64Revision));
        ::FireClipboardErrorEvent(ptrEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE, aId, NULL /* aItem */,
                                  FALSE /* aVeto */, aErrMsg, aRc);

        std::vector<SessionEventTarget> vecTargets;
        i_getSessionEventTargets(vecTargets, VBOX_SHCL_MAIN_CLIENT_NONE, true /* fPassive */, false /* fCheckReflection */,
                                 VBOX_SHCL_FMT_NONE, ClipboardSource_Custom);
        for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
            ::FireClipboardErrorEvent(it->mEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE, aId, NULL /* aItem */,
                                      FALSE /* aVeto */, aErrMsg, aRc);
    }
}


/**
 * Fires a clipboard mode changed event.
 *
 * @param   aClipboardMode  New clipboard mode.
 */
void Clipboard::i_fireClipboardModeChanged(ClipboardMode_T aClipboardMode)
{
    ComPtr<IEventSource> ptrEventSource;
    HRESULT hrc = getEventSource(ptrEventSource);
    if (SUCCEEDED(hrc) && ptrEventSource.isNotNull())
    {
        LONG64 const i64Revision = i_nextEventRevision();
        Log2Func(("Firing mode changed event: mode=%RU32, revision=%RI64\n", (uint32_t)aClipboardMode, i64Revision));
        ::FireClipboardModeChangedEvent(ptrEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE, aClipboardMode);

        std::vector<SessionEventTarget> vecTargets;
        i_getSessionEventTargets(vecTargets, VBOX_SHCL_MAIN_CLIENT_NONE, true /* fPassive */, false /* fCheckReflection */,
                                 VBOX_SHCL_FMT_NONE, ClipboardSource_Custom);
        for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
            ::FireClipboardModeChangedEvent(it->mEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE, aClipboardMode);
    }
}


/**
 * Fires a clipboard file transfer mode changed event.
 *
 * @param   fEnabled        Whether file transfers are enabled.
 */
void Clipboard::i_fireClipboardFileTransferModeChanged(bool fEnabled)
{
    ComPtr<IEventSource> ptrEventSource;
    HRESULT hrc = getEventSource(ptrEventSource);
    if (SUCCEEDED(hrc) && ptrEventSource.isNotNull())
    {
        LONG64 const i64Revision = i_nextEventRevision();
        Log2Func(("Firing file transfer mode changed event: fEnabled=%RTbool, revision=%RI64\n", fEnabled, i64Revision));
        ::FireClipboardFileTransferModeChangedEvent(ptrEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE,
                                                    fEnabled ? TRUE : FALSE);

        std::vector<SessionEventTarget> vecTargets;
        i_getSessionEventTargets(vecTargets, VBOX_SHCL_MAIN_CLIENT_NONE, true /* fPassive */, false /* fCheckReflection */,
                                 VBOX_SHCL_FMT_NONE, ClipboardSource_Custom);
        for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
            ::FireClipboardFileTransferModeChangedEvent(it->mEventSource, i64Revision, VBOX_SHCL_MAIN_CLIENT_NONE,
                                                        fEnabled ? TRUE : FALSE);
    }
}


/**
 * Fires a clipboard transfer event.
 *
 * @param   aClientId       Originating client identifier.
 * @param   aTransfer       Transfer associated with the event.
 * @param   aState          Transfer state.
 * @param   aInteraction    Requested transfer interaction.
 * @param   aPath           Optional transfer-relative event path.
 * @param   aMessage        Optional event message.
 * @param   aError          Clipboard transfer error code.
 */
void Clipboard::i_fireClipboardTransferEvent(VBOXSHCLMAINCLIENTID aClientId,
                                             IClipboardTransfer *aTransfer,
                                             ClipboardTransferState_T aState,
                                             ClipboardTransferInteraction_T aInteraction,
                                             const com::Utf8Str &aPath,
                                             const com::Utf8Str &aMessage,
                                             ClipboardError_T aError)
{
    ComPtr<IEventSource> ptrEventSource;
    LONG64 i64Revision;
    std::vector<SessionEventTarget> vecTargets;
    {
        /* Only retain the parent call while snapshotting COM-owned event
         * targets.  Active listener delivery may synchronously tear down the
         * Clipboard and therefore must not hold an AutoCaller on it. */
        AutoCaller autoCaller(this);
        if (FAILED(autoCaller.hrc()))
        {
            Log3Func(("Cannot snapshot transfer event targets: hrc=%#x\n", autoCaller.hrc()));
            return;
        }

        HRESULT const hrc = getEventSource(ptrEventSource);
        if (FAILED(hrc) || ptrEventSource.isNull())
        {
            Log3Func(("No event source for transfer event: hrc=%#x\n", hrc));
            return;
        }

        i64Revision = i_nextEventRevision();
        i_getSessionEventTargets(vecTargets, aClientId, true /* fPassive */, false /* fCheckReflection */,
                                 VBOX_SHCL_FMT_NONE, ClipboardSource_Custom);
    }

    Log2Func(("Firing transfer event: transfer=%p, state=%RU32, revision=%RI64, clientId=%RU32\n",
              (void *)aTransfer, (uint32_t)aState, i64Revision, aClientId));
    ::FireClipboardTransferEvent(ptrEventSource, i64Revision, aClientId, aTransfer, aState, aInteraction,
                                 Bstr(aPath).raw(), aMessage, aError);

    for (std::vector<SessionEventTarget>::const_iterator it = vecTargets.begin(); it != vecTargets.end(); ++it)
        ::FireClipboardTransferEvent(it->mEventSource, i64Revision, aClientId, aTransfer, aState, aInteraction,
                                     Bstr(aPath).raw(), aMessage, aError);
}


/**
 * Changes the clipboard mode.
 *
 * @returns VBox status code.
 * @param   aClipboardMode  new clipboard mode.
 */
int Clipboard::i_changeMode(ClipboardMode_T aClipboardMode)
{
    LogFunc(("aClipboardMode=%RU32\n", (uint32_t)aClipboardMode));
    AssertPtrReturn(mData, VERR_INVALID_POINTER);
    VMMDev *pVMMDev = mData->mParent->m_pVMMDev;
    AssertPtrReturn(pVMMDev, VERR_INVALID_POINTER);

    VBOXHGCMSVCPARM parm;
    parm.type = VBOX_HGCM_SVC_PARM_32BIT;

    switch (aClipboardMode)
    {
        default:
        case ClipboardMode_Disabled:
            LogRel(("Shared Clipboard: Mode: Off\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_OFF;
            break;
        case ClipboardMode_GuestToHost:
            LogRel(("Shared Clipboard: Mode: Guest to Host\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_GUEST_TO_HOST;
            break;
        case ClipboardMode_HostToGuest:
            LogRel(("Shared Clipboard: Mode: Host to Guest\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_HOST_TO_GUEST;
            break;
        case ClipboardMode_Bidirectional:
            LogRel(("Shared Clipboard: Mode: Bidirectional\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_BIDIRECTIONAL;
            break;
    }

    int vrc = pVMMDev->hgcmHostCall("VBoxSharedClipboard", VBOX_SHCL_HOST_FN_SET_MODE, 1, &parm);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Changing mode failed: mode=%RU32, serviceMode=%RU32, vrc=%Rrc\n",
                 (uint32_t)aClipboardMode, parm.u.uint32, vrc));
        LogRel(("Shared Clipboard: Error changing mode: %Rrc\n", vrc));
    }
    else
        Log2Func(("Changed mode: mode=%RU32, serviceMode=%RU32\n", (uint32_t)aClipboardMode, parm.u.uint32));

    return vrc;
}


# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Changes the clipboard file transfer mode.
 *
 * @returns VBox status code.
 * @param   fEnabled    Whether clipboard file transfers are enabled or not.
 */
int Clipboard::i_changeFileTransferMode(bool fEnabled)
{
    LogFunc(("fEnabled=%RTbool\n", fEnabled));
    AssertPtrReturn(mData, VERR_INVALID_POINTER);
    VMMDev *pVMMDev = mData->mParent->m_pVMMDev;
    AssertPtrReturn(pVMMDev, VERR_INVALID_POINTER);

    VBOXHGCMSVCPARM parm;
    RT_ZERO(parm);

    parm.type     = VBOX_HGCM_SVC_PARM_32BIT;
    parm.u.uint32 = fEnabled ? VBOX_SHCL_TRANSFER_MODE_F_ENABLED : VBOX_SHCL_TRANSFER_MODE_F_NONE;

    int vrc = pVMMDev->hgcmHostCall("VBoxSharedClipboard", VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1 /* cParms */, &parm);
    if (RT_FAILURE(vrc))
    {
        LogFunc(("Changing file transfer mode failed: fEnabled=%RTbool, serviceMode=%RU32, vrc=%Rrc\n",
                 fEnabled, parm.u.uint32, vrc));
        LogRel(("Shared Clipboard: Error changing file transfer mode: %Rrc\n", vrc));
    }
    else
        Log2Func(("Changed file transfer mode: fEnabled=%RTbool, serviceMode=%RU32\n", fEnabled, parm.u.uint32));

    return vrc;
}
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
