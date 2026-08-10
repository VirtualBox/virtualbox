/* $Id: tstClipboard.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Main API Testcase - Clipboard.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/array.h>
#include <VBox/com/com.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/GuestHost/SharedClipboard.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#include <VBox/log.h>

#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/utf16.h>
#include <iprt/uuid.h>

#include <string.h>
#include <vector>

using namespace com;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The testcase logger. */
static PRTLOGGER    g_pLogger;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * Creates a clipboard format object through the public Main API.
 *
 * @returns COM status code.
 * @param   pClipboard      Clipboard API object to use.
 * @param   pszMimeType     MIME type for the format object.
 * @param   ptrFormat       Where to return the created format object.
 */
static HRESULT tstCreateFormat(IClipboard *pClipboard, const char *pszMimeType, ComPtr<IClipboardFormat> &ptrFormat)
{
    AssertPtrReturn(pClipboard, E_POINTER);
    return pClipboard->CreateFormat(Bstr(pszMimeType).raw(), ptrFormat.asOutParam());
}


/**
 * Creates a clipboard item object through the public Main API.
 *
 * @returns COM status code.
 * @param   pClipboard      Clipboard API object to use.
 * @param   enmSource       Clipboard source.
 * @param   ptrFormat       Clipboard format object.
 * @param   abData          Payload bytes.
 * @param   ptrItem         Where to return the created item object.
 */
static HRESULT tstCreateItem(IClipboard *pClipboard, ClipboardSource_T enmSource, const ComPtr<IClipboardFormat> &ptrFormat,
                             const std::vector<BYTE> &abData, ComPtr<IClipboardItem> &ptrItem)
{
    AssertPtrReturn(pClipboard, E_POINTER);

    SafeArray<BYTE> aBuffer;
    if (!abData.empty())
    {
        HRESULT hrc = aBuffer.initFrom(&abData[0], abData.size());
        if (FAILED(hrc))
            return hrc;
    }

    return pClipboard->CreateItem(enmSource, ptrFormat, ComSafeArrayAsInParam(aBuffer), ptrItem.asOutParam());
}



/**
 * Checks whether a byte SafeArray contains the expected bytes.
 *
 * @returns true if the buffers match, false otherwise.
 * @param   aBuffer         Buffer to check.
 * @param   abExpected      Expected payload bytes.
 */
static bool tstByteArrayEquals(const SafeArray<BYTE> &aBuffer, const std::vector<BYTE> &abExpected)
{
    if (aBuffer.size() != abExpected.size())
        return false;
    if (abExpected.empty())
        return true;
    return !memcmp(aBuffer.raw(), &abExpected[0], abExpected.size());
}


/**
 * Creates deterministic byte test data from a NUL-terminated string, excluding the terminator.
 *
 * @returns Byte vector containing the string bytes.
 * @param   pszData         Test data string.
 */
static std::vector<BYTE> tstBytesFromString(const char *pszData)
{
    AssertPtrReturn(pszData, std::vector<BYTE>());

    size_t const cbData = strlen(pszData);
    std::vector<BYTE> abData(cbData);
    if (cbData)
        memcpy(&abData[0], pszData, cbData);
    return abData;
}


/**
 * Initializes a byte SafeArray from a vector.
 *
 * @returns COM status code.
 * @param   abData          Source bytes.
 * @param   aData           SafeArray to initialize.
 */
static HRESULT tstSafeArrayFromBytes(const std::vector<BYTE> &abData, SafeArray<BYTE> &aData)
{
    if (abData.empty())
        return S_OK;
    return aData.initFrom(&abData[0], abData.size());
}


/**
 * Reads raw clipboard data and checks the source, MIME type, and payload.
 *
 * @returns true if the read matched the expected data, false otherwise.
 * @param   pClipboard          Clipboard API object to use.
 * @param   pszWhat             Description used in failure messages.
 * @param   enmExpectedSource   Expected clipboard source.
 * @param   pszExpectedMimeType Expected MIME type.
 * @param   abExpected          Expected payload bytes.
 */
static bool tstReadDataRawEquals(IClipboard *pClipboard, const char *pszWhat, ClipboardSource_T enmExpectedSource,
                                 const char *pszExpectedMimeType, const std::vector<BYTE> &abExpected)
{
    AssertPtrReturn(pClipboard, false);
    AssertPtrReturn(pszWhat, false);
    AssertPtrReturn(pszExpectedMimeType, false);

    ClipboardSource_T enmReadSource = ClipboardSource_Custom;
    Bstr bstrRequestedMimeType("");
    Bstr bstrReadMimeType;
    SafeArray<BYTE> aReadBuffer;
    HRESULT hrc = pClipboard->ReadDataRaw(ClipboardAction_Copy, bstrRequestedMimeType.raw(), &enmReadSource,
                                          bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aReadBuffer));
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: ReadDataRaw failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }

    bool fRc = true;
    if (enmReadSource != enmExpectedSource)
    {
        RTTestIFailed("%s: ReadDataRaw source %d, expected %d\n", pszWhat, enmReadSource, enmExpectedSource);
        fRc = false;
    }

    Utf8Str strReadMimeType(bstrReadMimeType);
    if (RTStrCmp(strReadMimeType.c_str(), pszExpectedMimeType))
    {
        RTTestIFailed("%s: ReadDataRaw MIME '%s', expected '%s'\n", pszWhat, strReadMimeType.c_str(), pszExpectedMimeType);
        fRc = false;
    }

    if (!tstByteArrayEquals(aReadBuffer, abExpected))
    {
        RTTestIFailed("%s: ReadDataRaw returned %zu bytes, expected %zu\n", pszWhat, aReadBuffer.size(), abExpected.size());
        fRc = false;
    }

    return fRc;
}


/**
 * Creates a clipboard session through the public Main API.
 */
static HRESULT tstCreateSession(IClipboard *pClipboard, const IClipboardSessionFlag_T *paFlags, size_t cFlags,
                                ComPtr<IClipboardSession> &ptrSession)
{
    AssertPtrReturn(pClipboard, E_POINTER);
    if (cFlags && !paFlags)
        return E_POINTER;

    SafeArray<IClipboardSessionFlag_T> aFlags;
    for (size_t i = 0; i < cFlags; i++)
        if (!aFlags.push_back(paFlags[i]))
            return E_OUTOFMEMORY;

    return pClipboard->CreateSession(ComSafeArrayAsInParam(aFlags), ptrSession.asOutParam());
}



/**
 * Registers a passive listener for the specified event types.
 */
static HRESULT tstRegisterClipboardListener(IEventSource *pEventSource, const VBoxEventType_T *paEventTypes, size_t cEventTypes,
                                            ComPtr<IEventListener> &ptrListener)
{
    AssertPtrReturn(pEventSource, E_POINTER);
    AssertPtrReturn(paEventTypes, E_POINTER);
    AssertReturn(cEventTypes > 0, E_INVALIDARG);

    ptrListener.setNull();
    HRESULT hrc = pEventSource->CreateListener(ptrListener.asOutParam());
    if (FAILED(hrc))
        return hrc;

    SafeArray<VBoxEventType_T> aEventTypes;
    for (size_t i = 0; i < cEventTypes; i++)
        if (!aEventTypes.push_back(paEventTypes[i]))
            return E_OUTOFMEMORY;

    hrc = pEventSource->RegisterListener(ptrListener, ComSafeArrayAsInParam(aEventTypes), FALSE /* aActive */);
    if (FAILED(hrc))
        ptrListener.setNull();
    return hrc;
}


/**
 * Marks a waitable event as processed.  Passive clipboard listeners should normally see non-waitable events.
 */
static void tstClipboardMaybeProcessEvent(IEventSource *pEventSource, IEventListener *pListener, IEvent *pEvent)
{
    AssertPtrReturnVoid(pEventSource);
    AssertPtrReturnVoid(pListener);
    AssertPtrReturnVoid(pEvent);

    BOOL fWaitable = FALSE;
    HRESULT hrc = pEvent->COMGETTER(Waitable)(&fWaitable);
    if (SUCCEEDED(hrc) && fWaitable)
        pEventSource->EventProcessed(pListener, pEvent);
}


static bool tstClipboardIsEventTypeExpected(VBoxEventType_T enmType, const VBoxEventType_T *paExpectedTypes, size_t cExpectedTypes)
{
    for (size_t i = 0; i < cExpectedTypes; i++)
        if (paExpectedTypes[i] == enmType)
            return true;
    return false;
}


/**
 * Waits for the next event and verifies that it has one of the expected types.
 */
static bool tstClipboardWaitForAnyEvent(IEventSource *pEventSource, IEventListener *pListener,
                                        const VBoxEventType_T *paExpectedTypes, size_t cExpectedTypes,
                                        uint32_t cMsTimeout, const char *pszWhat,
                                        ComPtr<IEvent> &ptrEvent, VBoxEventType_T *penmType)
{
    AssertPtrReturn(pEventSource, false);
    AssertPtrReturn(pListener, false);
    AssertPtrReturn(paExpectedTypes, false);
    AssertPtrReturn(pszWhat, false);
    AssertPtrReturn(penmType, false);

    ptrEvent.setNull();
    *penmType = VBoxEventType_Invalid;

    HRESULT hrc = pEventSource->GetEvent(pListener, cMsTimeout, ptrEvent.asOutParam());
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: GetEvent failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }
    if (ptrEvent.isNull())
    {
        RTTestIFailed("%s: GetEvent returned no event\n", pszWhat);
        return false;
    }

    hrc = ptrEvent->COMGETTER(Type)(penmType);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(Type) failed, hrc=%Rhrc\n", pszWhat, hrc);
        tstClipboardMaybeProcessEvent(pEventSource, pListener, ptrEvent);
        return false;
    }

    if (!tstClipboardIsEventTypeExpected(*penmType, paExpectedTypes, cExpectedTypes))
    {
        RTTestIFailed("%s: GetEvent returned type %d\n", pszWhat, *penmType);
        tstClipboardMaybeProcessEvent(pEventSource, pListener, ptrEvent);
        return false;
    }

    return true;
}


/**
 * Verifies that a listener has no queued event.
 */
static bool tstClipboardExpectNoEvent(IEventSource *pEventSource, IEventListener *pListener, uint32_t cMsTimeout,
                                      const char *pszWhat)
{
    AssertPtrReturn(pEventSource, false);
    AssertPtrReturn(pListener, false);
    AssertPtrReturn(pszWhat, false);

    ComPtr<IEvent> ptrEvent;
    HRESULT hrc = pEventSource->GetEvent(pListener, cMsTimeout, ptrEvent.asOutParam());
    if (   hrc == VBOX_E_OBJECT_NOT_FOUND
        || (SUCCEEDED(hrc) && ptrEvent.isNull()))
        return true;

    VBoxEventType_T enmType = VBoxEventType_Invalid;
    if (SUCCEEDED(hrc))
        ptrEvent->COMGETTER(Type)(&enmType);
    RTTestIFailed("%s: unexpected event, hrc=%Rhrc, type=%d\n", pszWhat, hrc, enmType);
    if (ptrEvent.isNotNull())
        tstClipboardMaybeProcessEvent(pEventSource, pListener, ptrEvent);
    return false;
}


/**
 * Verifies the metadata common to clipboard event interfaces.
 */
template <typename EventT>
static bool tstClipboardCheckEventMetadata(EventT *pEvent, const char *pszWhat, ULONG idExpectedClient,
                                           LONG64 *pi64Revision = NULL)
{
    AssertPtrReturn(pEvent, false);
    AssertPtrReturn(pszWhat, false);

    bool fRc = true;
    LONG64 i64Revision = 0;
    HRESULT hrc = pEvent->COMGETTER(Revision)(&i64Revision);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(Revision) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (i64Revision <= 0)
    {
        RTTestIFailed("%s: revision is %RI64\n", pszWhat, i64Revision);
        fRc = false;
    }

    ULONG idClient = UINT32_MAX;
    hrc = pEvent->COMGETTER(ClientId)(&idClient);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(ClientId) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (idClient != idExpectedClient)
    {
        RTTestIFailed("%s: client ID %RU32, expected %RU32\n", pszWhat, (uint32_t)idClient, (uint32_t)idExpectedClient);
        fRc = false;
    }

    if (pi64Revision)
        *pi64Revision = i64Revision;
    return fRc;
}


template <typename EventT>
static bool tstClipboardCheckEventMetadata(const ComPtr<EventT> &ptrEvent, const char *pszWhat, ULONG idExpectedClient,
                                           LONG64 *pi64Revision = NULL)
{
    return tstClipboardCheckEventMetadata((EventT *)ptrEvent, pszWhat, idExpectedClient, pi64Revision);
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Verifies a service-originated clipboard transfer event.
 */
static bool tstClipboardCheckTransferEvent(IEvent *pEvent, const char *pszWhat,
                                           ClipboardTransferState_T enmExpectedState,
                                           ClipboardTransferDirection_T enmExpectedDirection,
                                           ClipboardSource_T enmExpectedSource,
                                           ULONG idExpectedTransfer,
                                           ComPtr<IClipboardTransfer> &ptrTransfer,
                                           ClipboardError_T enmExpectedError = ClipboardError_None)
{
    AssertPtrReturn(pEvent, false);
    AssertPtrReturn(pszWhat, false);

    ptrTransfer.setNull();
    ComPtr<IClipboardTransferEvent> ptrTransferEvent(pEvent);
    if (ptrTransferEvent.isNull())
    {
        RTTestIFailed("%s: event does not implement IClipboardTransferEvent\n", pszWhat);
        return false;
    }

    bool fRc = tstClipboardCheckEventMetadata(ptrTransferEvent, pszWhat, VBOX_SHCL_MAIN_CLIENT_NONE);

    ClipboardTransferState_T enmState = ClipboardTransferState_Removed;
    HRESULT hrc = ptrTransferEvent->COMGETTER(State)(&enmState);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(State) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmState != enmExpectedState)
    {
        RTTestIFailed("%s: state %d, expected %d\n", pszWhat, enmState, enmExpectedState);
        fRc = false;
    }

    ClipboardError_T enmError = ClipboardError_OperationFailed;
    hrc = ptrTransferEvent->COMGETTER(Error)(&enmError);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(Error) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmError != enmExpectedError)
    {
        RTTestIFailed("%s: error %d, expected %d\n", pszWhat, enmError, enmExpectedError);
        fRc = false;
    }

    hrc = ptrTransferEvent->COMGETTER(Transfer)(ptrTransfer.asOutParam());
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(Transfer) failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }
    if (ptrTransfer.isNull())
    {
        RTTestIFailed("%s: transfer event has no transfer\n", pszWhat);
        return false;
    }

    ULONG idTransfer = 0;
    hrc = ptrTransfer->COMGETTER(Id)(&idTransfer);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardTransfer::COMGETTER(Id) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (idTransfer != idExpectedTransfer)
    {
        RTTestIFailed("%s: transfer ID %RU32, expected %RU32\n", pszWhat, idTransfer, idExpectedTransfer);
        fRc = false;
    }

    ClipboardTransferDirection_T enmDirection = ClipboardTransferDirection_Any;
    hrc = ptrTransfer->COMGETTER(Direction)(&enmDirection);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardTransfer::COMGETTER(Direction) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmDirection != enmExpectedDirection)
    {
        RTTestIFailed("%s: transfer direction %d, expected %d\n", pszWhat, enmDirection, enmExpectedDirection);
        fRc = false;
    }

    ClipboardSource_T enmSource = ClipboardSource_Custom;
    hrc = ptrTransfer->COMGETTER(Source)(&enmSource);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardTransfer::COMGETTER(Source) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmSource != enmExpectedSource)
    {
        RTTestIFailed("%s: transfer source %d, expected %d\n", pszWhat, enmSource, enmExpectedSource);
        fRc = false;
    }

    ClipboardTransferState_T enmTransferState = ClipboardTransferState_Removed;
    hrc = ptrTransfer->COMGETTER(State)(&enmTransferState);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardTransfer::COMGETTER(State) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmTransferState != enmExpectedState)
    {
        RTTestIFailed("%s: transfer state %d, expected %d\n", pszWhat, enmTransferState, enmExpectedState);
        fRc = false;
    }

    ClipboardError_T enmTransferError = ClipboardError_OperationFailed;
    hrc = ptrTransfer->COMGETTER(Error)(&enmTransferError);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardTransfer::COMGETTER(Error) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmTransferError != enmExpectedError)
    {
        RTTestIFailed("%s: transfer error %d, expected %d\n", pszWhat, enmTransferError, enmExpectedError);
        fRc = false;
    }

    return fRc;
}
#endif


/**
 * Checks whether a format array contains exactly the expected MIME type.
 */
static bool tstClipboardCheckSingleFormat(SafeIfaceArray<IClipboardFormat> &aFormats, const char *pszExpectedMimeType,
                                          const char *pszWhat)
{
    AssertPtrReturn(pszExpectedMimeType, false);
    AssertPtrReturn(pszWhat, false);

    if (aFormats.size() != 1)
    {
        RTTestIFailed("%s: got %zu formats, expected 1\n", pszWhat, aFormats.size());
        return false;
    }

    Bstr bstrMimeType;
    HRESULT hrc = aFormats[0]->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(MimeType) failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }

    Utf8Str strMimeType(bstrMimeType);
    if (!RTStrCmp(strMimeType.c_str(), pszExpectedMimeType))
        return true;

    RTTestIFailed("%s: format MIME '%s', expected '%s'\n", pszWhat, strMimeType.c_str(), pszExpectedMimeType);
    return false;
}


/**
 * Verifies a clipboard item payload.
 */
static bool tstClipboardCheckItemPayload(IClipboardItem *pItem, const char *pszWhat, ClipboardSource_T enmExpectedSource,
                                         const char *pszExpectedMimeType, const std::vector<BYTE> &abExpected)
{
    AssertPtrReturn(pItem, false);
    AssertPtrReturn(pszWhat, false);
    AssertPtrReturn(pszExpectedMimeType, false);

    bool fRc = true;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    HRESULT hrc = pItem->COMGETTER(Source)(&enmSource);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardItem::COMGETTER(Source) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmSource != enmExpectedSource)
    {
        RTTestIFailed("%s: item source %d, expected %d\n", pszWhat, enmSource, enmExpectedSource);
        fRc = false;
    }

    ComPtr<IClipboardFormat> ptrFormat;
    hrc = pItem->COMGETTER(Format)(ptrFormat.asOutParam());
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardItem::COMGETTER(Format) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (ptrFormat.isNull())
    {
        RTTestIFailed("%s: item has no format\n", pszWhat);
        fRc = false;
    }
    else
    {
        Bstr bstrMimeType;
        hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
        if (FAILED(hrc))
        {
            RTTestIFailed("%s: IClipboardFormat::COMGETTER(MimeType) failed, hrc=%Rhrc\n", pszWhat, hrc);
            fRc = false;
        }
        else
        {
            Utf8Str strMimeType(bstrMimeType);
            if (RTStrCmp(strMimeType.c_str(), pszExpectedMimeType))
            {
                RTTestIFailed("%s: item MIME '%s', expected '%s'\n", pszWhat, strMimeType.c_str(), pszExpectedMimeType);
                fRc = false;
            }
        }
    }

    SafeArray<BYTE> aBuffer;
    hrc = pItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aBuffer));
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardItem::COMGETTER(Buffer) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (!tstByteArrayEquals(aBuffer, abExpected))
    {
        RTTestIFailed("%s: item payload has %zu bytes, expected %zu\n", pszWhat, aBuffer.size(), abExpected.size());
        fRc = false;
    }

    return fRc;
}


/**
 * Reads the possibly-null IClipboardEvent::item payload from a concrete clipboard event.
 */
static bool tstClipboardGetEventItem(IEvent *pEvent, const char *pszWhat, ComPtr<IClipboardItem> &ptrItem)
{
    AssertPtrReturn(pEvent, false);
    AssertPtrReturn(pszWhat, false);

    ptrItem.setNull();
    ComPtr<IClipboardEvent> ptrClipboardEvent(pEvent);
    if (ptrClipboardEvent.isNull())
    {
        RTTestIFailed("%s: event does not implement IClipboardEvent\n", pszWhat);
        return false;
    }

    HRESULT hrc = ptrClipboardEvent->COMGETTER(Item)(ptrItem.asOutParam());
    if (SUCCEEDED(hrc))
        return true;

    RTTestIFailed("%s: IClipboardEvent::COMGETTER(Item) failed, hrc=%Rhrc\n", pszWhat, hrc);
    return false;
}


/**
 * Verifies an OnClipboardDataChanged event and optionally returns its inherited item.
 */
static bool tstClipboardCheckDataChangedEvent(IEvent *pEvent, const char *pszWhat, ULONG idExpectedClient,
                                              ClipboardAction_T enmExpectedAction, ComPtr<IClipboardItem> *pptrItem,
                                              LONG64 *pi64Revision = NULL)
{
    AssertPtrReturn(pEvent, false);
    AssertPtrReturn(pszWhat, false);

    ComPtr<IClipboardDataChangedEvent> ptrDataEvent(pEvent);
    if (ptrDataEvent.isNull())
    {
        RTTestIFailed("%s: event does not implement IClipboardDataChangedEvent\n", pszWhat);
        return false;
    }

    bool fRc = tstClipboardCheckEventMetadata(ptrDataEvent, pszWhat, idExpectedClient, pi64Revision);

    ClipboardAction_T enmAction = ClipboardAction_Custom;
    HRESULT hrc = ptrDataEvent->COMGETTER(Action)(&enmAction);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(Action) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmAction != enmExpectedAction)
    {
        RTTestIFailed("%s: action %d, expected %d\n", pszWhat, enmAction, enmExpectedAction);
        fRc = false;
    }

    if (pptrItem && !tstClipboardGetEventItem(pEvent, pszWhat, *pptrItem))
        fRc = false;
    return fRc;
}


/**
 * Verifies an OnClipboardFormatChanged event.
 */
static bool tstClipboardCheckFormatChangedEvent(IEvent *pEvent, const char *pszWhat, ULONG idExpectedClient,
                                                ClipboardSource_T enmExpectedSource, const char *pszExpectedMimeType,
                                                LONG64 *pi64Revision = NULL)
{
    AssertPtrReturn(pEvent, false);
    AssertPtrReturn(pszWhat, false);
    AssertPtrReturn(pszExpectedMimeType, false);

    ComPtr<IClipboardFormatChangedEvent> ptrFormatEvent(pEvent);
    if (ptrFormatEvent.isNull())
    {
        RTTestIFailed("%s: event does not implement IClipboardFormatChangedEvent\n", pszWhat);
        return false;
    }

    bool fRc = tstClipboardCheckEventMetadata(ptrFormatEvent, pszWhat, idExpectedClient, pi64Revision);

    ClipboardSource_T enmSource = ClipboardSource_Custom;
    HRESULT hrc = ptrFormatEvent->COMGETTER(ClipboardSource)(&enmSource);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(ClipboardSource) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (enmSource != enmExpectedSource)
    {
        RTTestIFailed("%s: source %d, expected %d\n", pszWhat, enmSource, enmExpectedSource);
        fRc = false;
    }

    SafeIfaceArray<IClipboardFormat> aFormats;
    hrc = ptrFormatEvent->COMGETTER(Formats)(ComSafeArrayAsOutParam(aFormats));
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: COMGETTER(Formats) failed, hrc=%Rhrc\n", pszWhat, hrc);
        fRc = false;
    }
    else if (!tstClipboardCheckSingleFormat(aFormats, pszExpectedMimeType, pszWhat))
        fRc = false;

    return fRc;
}



/**
 * Verifies a clipboard item payload.
 * Reads raw clipboard data through a clipboard session and checks the source, MIME type, and payload.
 */
static bool tstReadSessionDataRawEquals(IClipboardSession *pSession, const char *pszWhat,
                                        ClipboardSource_T enmExpectedSource, const char *pszExpectedMimeType,
                                        const std::vector<BYTE> &abExpected)
{
    AssertPtrReturn(pSession, false);
    AssertPtrReturn(pszWhat, false);
    AssertPtrReturn(pszExpectedMimeType, false);

    ClipboardSource_T enmReadSource = ClipboardSource_Custom;
    Bstr bstrRequestedMimeType("");
    Bstr bstrReadMimeType;
    SafeArray<BYTE> aReadBuffer;
    HRESULT hrc = pSession->ReadDataRaw(ClipboardAction_Copy, bstrRequestedMimeType.raw(), &enmReadSource,
                                        bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aReadBuffer));
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IClipboardSession::ReadDataRaw failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }

    bool fRc = true;
    if (enmReadSource != enmExpectedSource)
    {
        RTTestIFailed("%s: session ReadDataRaw source %d, expected %d\n", pszWhat, enmReadSource, enmExpectedSource);
        fRc = false;
    }

    Utf8Str strReadMimeType(bstrReadMimeType);
    if (RTStrCmp(strReadMimeType.c_str(), pszExpectedMimeType))
    {
        RTTestIFailed("%s: session ReadDataRaw MIME '%s', expected '%s'\n", pszWhat, strReadMimeType.c_str(),
                      pszExpectedMimeType);
        fRc = false;
    }

    if (!tstByteArrayEquals(aReadBuffer, abExpected))
    {
        RTTestIFailed("%s: session ReadDataRaw returned %zu bytes, expected %zu\n", pszWhat, aReadBuffer.size(),
                      abExpected.size());
        fRc = false;
    }

    return fRc;
}


/**
 * Closes and releases a clipboard session.
 */
static void tstClipboardCloseSession(ComPtr<IClipboardSession> &ptrSession, const char *pszWhat)
{
    AssertPtrReturnVoid(pszWhat);

    if (ptrSession.isNull())
        return;

    HRESULT hrc = ptrSession->Close();
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("%s: IClipboardSession::Close failed, hrc=%Rhrc\n", pszWhat, hrc));
    ptrSession.setNull();
}



/**
 * Writes data through IHostClipboard::SetData and verifies regular Main state is unchanged.
 *
 * @returns true if all readbacks matched, false otherwise.
 * @param   pClipboard          Clipboard API object to use for readback.
 * @param   pHostClipboard      Host clipboard endpoint to test.
 * @param   pszMimeType         MIME type to publish to the native host clipboard.
 * @param   abData              Deterministic payload bytes to publish.
 * @param   enmExpectedSource   Expected regular Main clipboard source after publication.
 * @param   pszExpectedMimeType Expected regular Main clipboard MIME type after publication.
 * @param   abExpected          Expected regular Main clipboard payload after publication.
 * @param   cReads              Number of repeated readbacks to perform.
 * @param   pszWhat             Description used in failure messages.
 */
static bool tstHostClipboardSetDataAndKeepReadBack(IClipboard *pClipboard, IHostClipboard *pHostClipboard,
                                                   const char *pszMimeType, const std::vector<BYTE> &abData,
                                                   ClipboardSource_T enmExpectedSource, const char *pszExpectedMimeType,
                                                   const std::vector<BYTE> &abExpected, unsigned cReads,
                                                   const char *pszWhat)
{
    AssertPtrReturn(pClipboard, false);
    AssertPtrReturn(pHostClipboard, false);
    AssertPtrReturn(pszMimeType, false);
    AssertPtrReturn(pszExpectedMimeType, false);
    AssertPtrReturn(pszWhat, false);

    SafeArray<BYTE> aData;
    HRESULT hrc = tstSafeArrayFromBytes(abData, aData);
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: SafeArray initFrom failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }

    hrc = pHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Guest, Bstr(pszMimeType).raw(),
                                  ComSafeArrayAsInParam(aData));
    if (FAILED(hrc))
    {
        RTTestIFailed("%s: IHostClipboard::SetData failed, hrc=%Rhrc\n", pszWhat, hrc);
        return false;
    }

    bool fRc = true;
    for (unsigned i = 0; i < cReads; i++)
    {
        char szWhat[128];
        RTStrPrintf(szWhat, sizeof(szWhat), "%s preserved readback %u", pszWhat, i);
        if (!tstReadDataRawEquals(pClipboard, szWhat, enmExpectedSource, pszExpectedMimeType, abExpected))
            fRc = false;
    }
    return fRc;
}


/**
 * Tries to observe a native host-source readback after IHostClipboard publication.
 *
 * This is intentionally guarded: headless or no-backend environments may never
 * reflect the native clipboard back into Main as ClipboardSource_Host.
 *
 * @param   hTest               Test handle.
 * @param   pClipboard          Clipboard API object to use.
 * @param   pszMimeType         Expected MIME type.
 * @param   abExpected          Expected payload bytes.
 */
static void tstHostClipboardTryNativeReadBack(RTTEST hTest, IClipboard *pClipboard, const char *pszMimeType,
                                              const std::vector<BYTE> &abExpected)
{
    RTTestSub(hTest, "IHostClipboard native host clipboard observation");

    for (unsigned i = 0; i < 20; i++)
    {
        ClipboardSource_T enmReadSource = ClipboardSource_Custom;
        Bstr bstrRequestedMimeType("");
        Bstr bstrReadMimeType;
        SafeArray<BYTE> aReadBuffer;
        HRESULT hrc = pClipboard->ReadDataRaw(ClipboardAction_Copy, bstrRequestedMimeType.raw(), &enmReadSource,
                                              bstrReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aReadBuffer));
        if (SUCCEEDED(hrc) && enmReadSource == ClipboardSource_Host)
        {
            Utf8Str strReadMimeType(bstrReadMimeType);
            if (   !RTStrCmp(strReadMimeType.c_str(), pszMimeType)
                && tstByteArrayEquals(aReadBuffer, abExpected))
                return;
        }
        RTThreadSleep(100);
    }

    RTTestSkipped(hTest, "Native host clipboard did not become observable as host-owned data; this is expected on "
                         "headless/no-backend or lazy/no-op backend runs");
}


/**
 * Tests the sole host clipboard endpoint implementation exposed by IClipboard.
 *
 * @param   hTest                   Test handle.
 * @param   pClipboard              Clipboard API object to use.
 * @param   pClipboardSettings      Clipboard settings for mode validation.
 * @param   pEventSource            Clipboard event source for data request events.
 * @param   ptrTextFormat           Supported text format object.
 * @param   bstrGuestReadMimeType   MIME type from the regular readback path.
 * @param   aGuestReadBuffer        Payload from the regular readback path.
 * @param   abRoundTripBuffer       Expected regular readback payload bytes.
 */
static void tstHostClipboard(RTTEST hTest, IClipboard *pClipboard, IClipboardSettings *pClipboardSettings,
                             IEventSource *pEventSource, const ComPtr<IClipboardFormat> &ptrTextFormat,
                             const Bstr &bstrGuestReadMimeType, const SafeArray<BYTE> &aGuestReadBuffer,
                             const std::vector<BYTE> &abRoundTripBuffer)
{
    RTTestSub(hTest, "IHostClipboard public endpoint");

    HRESULT hrc = S_OK;
    bool fListenerRegistered = false;
    ComPtr<IEventListener> ptrListener;
    ComPtr<IHostClipboard> ptrHostClipboard;

    do
    {
        /* Verify the endpoint object is reachable from the public clipboard API. */
        hrc = pClipboard->COMGETTER(HostClipboard)(ptrHostClipboard.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(HostClipboard) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrHostClipboard.isNull(), ("COMGETTER(HostClipboard) returned NULL\n"));

        /* Verify the native host endpoint can be explicitly reset. */
        hrc = ptrHostClipboard->Clear();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("HostClipboard Clear failed, hrc=%Rhrc\n", hrc));

        std::vector<ComPtr<IClipboardFormat> > vecHostFormats;
        vecHostFormats.push_back(ptrTextFormat);
        SafeIfaceArray<IClipboardFormat> aHostTextFormats(vecHostFormats);

        /* Verify guest-origin offers are blocked when mode disallows guest-to-host publication. */
        hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_HostToGuest);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMSETTER(Mode) HostToGuest before IHostClipboard failed, hrc=%Rhrc\n", hrc));

        hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest,
                                              ComSafeArrayAsInParam(aHostTextFormats));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ACCESS_DENIED,
                          ("HostClipboard ReportFormats in HostToGuest mode returned hrc=%Rhrc\n", hrc));

        hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Host,
                                              ComSafeArrayAsInParam(aHostTextFormats));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ACCESS_DENIED,
                          ("HostClipboard ReportFormats with host source returned hrc=%Rhrc\n", hrc));

        ComPtr<IClipboardFormat> ptrOctetStreamFormat;
        hrc = tstCreateFormat(pClipboard, "application/octet-stream", ptrOctetStreamFormat);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateFormat(octet-stream) failed, hrc=%Rhrc\n", hrc));
        vecHostFormats.clear();
        vecHostFormats.push_back(ptrOctetStreamFormat);
        SafeIfaceArray<IClipboardFormat> aHostOctetFormats(vecHostFormats);
        hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest,
                                              ComSafeArrayAsInParam(aHostOctetFormats));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                          ("HostClipboard ReportFormats with application/octet-stream returned hrc=%Rhrc\n", hrc));

        /* Restore bidirectional mode before exercising successful IHostClipboard guest publication. */
        hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMSETTER(Mode) Bidirectional before IHostClipboard failed, hrc=%Rhrc\n", hrc));

        /* Verify guest format offers and early ProvideData rejection paths. */
        vecHostFormats.clear();
        vecHostFormats.push_back(ptrTextFormat);
        SafeIfaceArray<IClipboardFormat> aHostGuestTextFormats(vecHostFormats);
        hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest,
                                              ComSafeArrayAsInParam(aHostGuestTextFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("HostClipboard ReportFormats(guest) failed, hrc=%Rhrc\n", hrc));

        hrc = ptrHostClipboard->ProvideData(0 /* requestId */, ClipboardAction_Copy, ClipboardSource_Guest,
                                            bstrGuestReadMimeType.raw(), ComSafeArrayAsInParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ERROR,
                          ("HostClipboard ProvideData with zero request ID returned hrc=%Rhrc\n", hrc));

        hrc = ptrHostClipboard->ProvideData(1 /* requestId */, ClipboardAction_Copy, ClipboardSource_Guest,
                                            Bstr("application/octet-stream").raw(), ComSafeArrayAsInParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                          ("HostClipboard ProvideData with application/octet-stream returned hrc=%Rhrc\n", hrc));

        hrc = ptrHostClipboard->ProvideData(1 /* requestId */, ClipboardAction_Copy, ClipboardSource_Guest,
                                            bstrGuestReadMimeType.raw(), ComSafeArrayAsInParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ERROR,
                          ("HostClipboard ProvideData without pending request returned hrc=%Rhrc\n", hrc));

        /* Validate ProvideData against a real pending request ID generated by Main. */
        hrc = pEventSource->CreateListener(ptrListener.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateListener(data requested) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrListener.isNull());
        SafeArray<VBoxEventType_T> aRequestEventTypes;
        aRequestEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
        hrc = pEventSource->RegisterListener(ptrListener, ComSafeArrayAsInParam(aRequestEventTypes), FALSE /* aActive */);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(data requested) failed, hrc=%Rhrc\n", hrc));
        fListenerRegistered = SUCCEEDED(hrc);

        ComPtr<IInternalClipboardControl> ptrInternalClipboardControl(pClipboard);
        RTTESTI_CHECK_MSG_BREAK(!ptrInternalClipboardControl.isNull(), ("Query IInternalClipboardControl returned NULL\n"));

        ULONG idRequest = 0;
        hrc = ptrInternalClipboardControl->RequestData(bstrGuestReadMimeType.raw(), &idRequest);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("IInternalClipboardControl::RequestData failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(idRequest != 0, ("IInternalClipboardControl::RequestData returned zero request ID\n"));

        ComPtr<IEvent> ptrRequestEvent;
        hrc = pEventSource->GetEvent(ptrListener, 1000 /* aTimeout */, ptrRequestEvent.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("GetEvent(data requested) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrRequestEvent.isNull(), ("GetEvent(data requested) returned no event\n"));

        VBoxEventType_T enmRequestEventType = VBoxEventType_Invalid;
        hrc = ptrRequestEvent->COMGETTER(Type)(&enmRequestEventType);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Type)(data requested) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(enmRequestEventType == VBoxEventType_OnClipboardDataRequested,
                          ("GetEvent(data requested) returned type %d, expected %d\n",
                           enmRequestEventType, VBoxEventType_OnClipboardDataRequested));

        BOOL fRequestEventWaitable = TRUE;
        hrc = ptrRequestEvent->COMGETTER(Waitable)(&fRequestEventWaitable);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Waitable)(data requested) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(!fRequestEventWaitable, ("OnClipboardDataRequested unexpectedly became waitable\n"));

        ComPtr<IClipboardDataRequestedEvent> ptrDataRequestedEvent = ptrRequestEvent;
        RTTESTI_CHECK(!ptrDataRequestedEvent.isNull());
        if (ptrDataRequestedEvent.isNotNull())
        {
            RTTESTI_CHECK(tstClipboardCheckEventMetadata(ptrDataRequestedEvent, "data requested event",
                                                        VBOX_SHCL_MAIN_CLIENT_NONE));

            ULONG idEventRequest = 0;
            hrc = ptrDataRequestedEvent->COMGETTER(RequestId)(&idEventRequest);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(RequestId) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idEventRequest == idRequest,
                              ("Request event ID %RU32, expected %RU32\n", (uint32_t)idEventRequest, (uint32_t)idRequest));

            ClipboardAction_T enmRequestAction = ClipboardAction_Custom;
            hrc = ptrDataRequestedEvent->COMGETTER(Action)(&enmRequestAction);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Action) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(enmRequestAction == ClipboardAction_Copy);

            ClipboardSource_T enmRequestSource = ClipboardSource_Custom;
            hrc = ptrDataRequestedEvent->COMGETTER(ClipboardSource)(&enmRequestSource);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(ClipboardSource) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(enmRequestSource == ClipboardSource_Guest);

            ComPtr<IClipboardFormat> ptrRequestFormat;
            hrc = ptrDataRequestedEvent->COMGETTER(Format)(ptrRequestFormat.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Format) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(!ptrRequestFormat.isNull());
            if (ptrRequestFormat.isNotNull())
            {
                Bstr bstrRequestMimeType;
                hrc = ptrRequestFormat->COMGETTER(MimeType)(bstrRequestMimeType.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(MimeType)(request format) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrRequestMimeType).c_str(), "text/plain;charset=utf-8"));
            }
        }

        hrc = ptrHostClipboard->ProvideData(idRequest, ClipboardAction_Copy, ClipboardSource_Guest,
                                            bstrGuestReadMimeType.raw(), ComSafeArrayAsInParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("HostClipboard ProvideData with pending request failed, hrc=%Rhrc\n", hrc));

        hrc = ptrHostClipboard->ProvideData(idRequest, ClipboardAction_Copy, ClipboardSource_Guest,
                                            bstrGuestReadMimeType.raw(), ComSafeArrayAsInParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ERROR,
                          ("HostClipboard ProvideData with consumed request ID returned hrc=%Rhrc\n", hrc));

        if (ptrListener.isNotNull())
            pEventSource->UnregisterListener(ptrListener);
        fListenerRegistered = false;
        ptrListener.setNull();

        /* Ensure data supplied for the pending request became the guest-owned clipboard content. */
        ClipboardSource_T enmProvidedReadSource = ClipboardSource_Custom;
        Bstr bstrProvidedRequestedMimeType("");
        Bstr bstrProvidedReadMimeType;
        SafeArray<BYTE> aProvidedReadBuffer;
        hrc = pClipboard->ReadDataRaw(ClipboardAction_Copy, bstrProvidedRequestedMimeType.raw(), &enmProvidedReadSource,
                                      bstrProvidedReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aProvidedReadBuffer));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("ReadDataRaw after HostClipboard ProvideData failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmProvidedReadSource == ClipboardSource_Guest);
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrProvidedReadMimeType).c_str(), "text/plain;charset=utf-8"));
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aProvidedReadBuffer, abRoundTripBuffer),
                          ("ReadDataRaw after HostClipboard ProvideData returned %zu bytes, expected %zu\n",
                           aProvidedReadBuffer.size(), abRoundTripBuffer.size()));

        /* Seed host-owned Main state before lazy guest publication. */
        static const char s_szHostStateText[] = "tstClipboard host state preserved across IHostClipboard";
        std::vector<BYTE> abHostStateData = tstBytesFromString(s_szHostStateText);
        SafeArray<BYTE> aHostStateData;
        hrc = tstSafeArrayFromBytes(abHostStateData, aHostStateData);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SafeArray initFrom(host state data) failed, hrc=%Rhrc\n", hrc));

        ClipboardSource_T enmHostStateWrittenSource = ClipboardSource_Custom;
        Bstr bstrHostStateWrittenMimeType;
        SafeArray<BYTE> aHostStateWrittenBuffer;
        hrc = pClipboard->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host, bstrGuestReadMimeType.raw(),
                                       ComSafeArrayAsInParam(aHostStateData), &enmHostStateWrittenSource,
                                       bstrHostStateWrittenMimeType.asOutParam(),
                                       ComSafeArrayAsOutParam(aHostStateWrittenBuffer));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WriteDataRaw(host state before IHostClipboard) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmHostStateWrittenSource == ClipboardSource_Host);
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aHostStateWrittenBuffer, abHostStateData),
                          ("WriteDataRaw(host state before IHostClipboard) returned %zu bytes, expected %zu\n",
                           aHostStateWrittenBuffer.size(), abHostStateData.size()));
        Utf8Str strHostStateMimeType(bstrHostStateWrittenMimeType);
        tstReadDataRawEquals(pClipboard, "host state before IHostClipboard publication", ClipboardSource_Host,
                             strHostStateMimeType.c_str(), abHostStateData);

        hrc = ptrHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest,
                                              ComSafeArrayAsInParam(aHostGuestTextFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("HostClipboard ReportFormats(guest) over host state failed, hrc=%Rhrc\n", hrc));

        hrc = pEventSource->CreateListener(ptrListener.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateListener(lazy host request) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrListener.isNull());
        hrc = pEventSource->RegisterListener(ptrListener, ComSafeArrayAsInParam(aRequestEventTypes), FALSE /* aActive */);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(lazy host request) failed, hrc=%Rhrc\n", hrc));
        fListenerRegistered = SUCCEEDED(hrc);

        ULONG idLazyRequest = 0;
        hrc = ptrInternalClipboardControl->RequestData(bstrGuestReadMimeType.raw(), &idLazyRequest);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("IInternalClipboardControl::RequestData after HostClipboard ReportFormats failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(idLazyRequest != 0, ("IInternalClipboardControl::RequestData after HostClipboard ReportFormats returned zero request ID\n"));

        ComPtr<IEvent> ptrLazyRequestEvent;
        hrc = pEventSource->GetEvent(ptrListener, 1000 /* aTimeout */, ptrLazyRequestEvent.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("GetEvent(lazy host request) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrLazyRequestEvent.isNull(), ("GetEvent(lazy host request) returned no event\n"));

        VBoxEventType_T enmLazyRequestEventType = VBoxEventType_Invalid;
        hrc = ptrLazyRequestEvent->COMGETTER(Type)(&enmLazyRequestEventType);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Type)(lazy host request) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(enmLazyRequestEventType == VBoxEventType_OnClipboardDataRequested,
                          ("GetEvent(lazy host request) returned type %d, expected %d\n",
                           enmLazyRequestEventType, VBoxEventType_OnClipboardDataRequested));

        ComPtr<IClipboardDataRequestedEvent> ptrLazyDataRequestedEvent = ptrLazyRequestEvent;
        RTTESTI_CHECK(!ptrLazyDataRequestedEvent.isNull());
        if (ptrLazyDataRequestedEvent.isNotNull())
        {
            RTTESTI_CHECK(tstClipboardCheckEventMetadata(ptrLazyDataRequestedEvent, "lazy data requested event",
                                                        VBOX_SHCL_MAIN_CLIENT_NONE));

            ULONG idLazyEventRequest = 0;
            hrc = ptrLazyDataRequestedEvent->COMGETTER(RequestId)(&idLazyEventRequest);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(RequestId)(lazy host request) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idLazyEventRequest == idLazyRequest,
                              ("Lazy request event ID %RU32, expected %RU32\n",
                               (uint32_t)idLazyEventRequest, (uint32_t)idLazyRequest));

            ClipboardAction_T enmLazyRequestAction = ClipboardAction_Custom;
            hrc = ptrLazyDataRequestedEvent->COMGETTER(Action)(&enmLazyRequestAction);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Action)(lazy host request) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(enmLazyRequestAction == ClipboardAction_Copy);

            ClipboardSource_T enmLazyRequestSource = ClipboardSource_Custom;
            hrc = ptrLazyDataRequestedEvent->COMGETTER(ClipboardSource)(&enmLazyRequestSource);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(ClipboardSource)(lazy host request) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(enmLazyRequestSource == ClipboardSource_Guest);

            ComPtr<IClipboardFormat> ptrLazyRequestFormat;
            hrc = ptrLazyDataRequestedEvent->COMGETTER(Format)(ptrLazyRequestFormat.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Format)(lazy host request) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(!ptrLazyRequestFormat.isNull());
            if (ptrLazyRequestFormat.isNotNull())
            {
                Bstr bstrLazyRequestMimeType;
                hrc = ptrLazyRequestFormat->COMGETTER(MimeType)(bstrLazyRequestMimeType.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(MimeType)(lazy host request format) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrLazyRequestMimeType).c_str(), "text/plain;charset=utf-8"));
            }
        }

        hrc = ptrHostClipboard->ProvideData(idLazyRequest, ClipboardAction_Copy, ClipboardSource_Guest,
                                            bstrGuestReadMimeType.raw(), ComSafeArrayAsInParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("HostClipboard ProvideData for lazy host request failed, hrc=%Rhrc\n", hrc));

        if (ptrListener.isNotNull())
            pEventSource->UnregisterListener(ptrListener);
        fListenerRegistered = false;
        ptrListener.setNull();

        tstReadDataRawEquals(pClipboard, "IHostClipboard ReportFormats lazy guest data over host state", ClipboardSource_Guest,
                             "text/plain;charset=utf-8", abRoundTripBuffer);

        /* Reseed host-owned Main state before verifying SetData does not replace it. */
        aHostStateWrittenBuffer.setNull();
        hrc = pClipboard->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host, bstrGuestReadMimeType.raw(),
                                       ComSafeArrayAsInParam(aHostStateData), &enmHostStateWrittenSource,
                                       bstrHostStateWrittenMimeType.asOutParam(),
                                       ComSafeArrayAsOutParam(aHostStateWrittenBuffer));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WriteDataRaw(host state after lazy IHostClipboard) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmHostStateWrittenSource == ClipboardSource_Host);
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aHostStateWrittenBuffer, abHostStateData),
                          ("WriteDataRaw(host state after lazy IHostClipboard) returned %zu bytes, expected %zu\n",
                           aHostStateWrittenBuffer.size(), abHostStateData.size()));
        tstReadDataRawEquals(pClipboard, "host state after lazy IHostClipboard publication", ClipboardSource_Host,
                             strHostStateMimeType.c_str(), abHostStateData);

        RTTestSub(hTest, "IHostClipboard SetData preserves Main state");
        /* Verify deterministic SetData payloads do not overwrite host-owned Main state. */
        struct TSTHOSTCLIPBOARDSETDATA
        {
            const char *pszMimeType;
            const char *pszData;
            const char *pszWhat;
        };
        static const TSTHOSTCLIPBOARDSETDATA s_aHostClipboardSetData[] =
        {
            { "text/plain;charset=utf-8", "tstClipboard IHostClipboard text publish #1\n", "text/plain #1" },
            { "text/html", "<html><body><p>tstClipboard IHostClipboard html publish</p></body></html>", "text/html" },
            { "text/plain;charset=utf-8", "tstClipboard IHostClipboard text publish #2\n", "text/plain #2" }
        };

        bool fHaveLastHostClipboardSetData = false;
        const char *pszLastHostClipboardMimeType = NULL;
        std::vector<BYTE> abLastHostClipboardData;
        for (unsigned i = 0; i < RT_ELEMENTS(s_aHostClipboardSetData); i++)
        {
            std::vector<BYTE> abSetData = tstBytesFromString(s_aHostClipboardSetData[i].pszData);
            char szWhat[128];
            RTStrPrintf(szWhat, sizeof(szWhat), "IHostClipboard SetData %s", s_aHostClipboardSetData[i].pszWhat);
            if (tstHostClipboardSetDataAndKeepReadBack(pClipboard, ptrHostClipboard,
                                                       s_aHostClipboardSetData[i].pszMimeType, abSetData,
                                                       ClipboardSource_Host, strHostStateMimeType.c_str(),
                                                       abHostStateData, 3 /* cReads */, szWhat))
            {
                fHaveLastHostClipboardSetData = true;
                pszLastHostClipboardMimeType = s_aHostClipboardSetData[i].pszMimeType;
                abLastHostClipboardData = abSetData;
            }
        }

        if (fHaveLastHostClipboardSetData)
        {
            SafeArray<BYTE> aLastHostClipboardData;
            hrc = tstSafeArrayFromBytes(abLastHostClipboardData, aLastHostClipboardData);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SafeArray initFrom(last host clipboard data) failed, hrc=%Rhrc\n", hrc));

            /* Verify SetData rejects wrong source, empty data, unsupported format, and disallowed mode. */
            hrc = ptrHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Host, Bstr(pszLastHostClipboardMimeType).raw(),
                                            ComSafeArrayAsInParam(aLastHostClipboardData));
            RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ACCESS_DENIED,
                              ("HostClipboard SetData with host source returned hrc=%Rhrc\n", hrc));

            SafeArray<BYTE> aEmptyHostClipboardData;
            hrc = ptrHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Guest, Bstr(pszLastHostClipboardMimeType).raw(),
                                            ComSafeArrayAsInParam(aEmptyHostClipboardData));
            RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_NO_DATA,
                              ("HostClipboard SetData with empty data returned hrc=%Rhrc\n", hrc));

            hrc = ptrHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Guest, Bstr("application/octet-stream").raw(),
                                            ComSafeArrayAsInParam(aLastHostClipboardData));
            RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                              ("HostClipboard SetData with application/octet-stream returned hrc=%Rhrc\n", hrc));

            hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_HostToGuest);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                              ("COMSETTER(Mode) HostToGuest before HostClipboard SetData denial failed, hrc=%Rhrc\n", hrc));
            if (SUCCEEDED(hrc))
            {
                hrc = ptrHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Guest,
                                                Bstr(pszLastHostClipboardMimeType).raw(),
                                                ComSafeArrayAsInParam(aLastHostClipboardData));
                RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_ACCESS_DENIED,
                                  ("HostClipboard SetData in HostToGuest mode returned hrc=%Rhrc\n", hrc));

                hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
                RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc),
                                        ("COMSETTER(Mode) Bidirectional after HostClipboard SetData denial failed, "
                                         "hrc=%Rhrc\n", hrc));
            }

            /* Ensure rejection paths did not disturb the preserved host-owned Main payload. */
            tstReadDataRawEquals(pClipboard, "IHostClipboard SetData after failure cases", ClipboardSource_Host,
                                 strHostStateMimeType.c_str(), abHostStateData);
            tstHostClipboardTryNativeReadBack(hTest, pClipboard, pszLastHostClipboardMimeType, abLastHostClipboardData);
        }
    } while (0);

    if (fListenerRegistered && pEventSource && ptrListener.isNotNull())
        pEventSource->UnregisterListener(ptrListener);
    ptrListener.setNull();

    hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMSETTER(Mode) Bidirectional after IHostClipboard failed, hrc=%Rhrc\n", hrc));
}


/**
 * Configures Shared Clipboard logging to go to stdout.
 */
static void tstInitLogging(void)
{
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    static const char * const s_apszLogGroups[] = VBOX_LOGGROUP_NAMES;
    int vrc = RTLogCreate(&g_pLogger, fFlags, "all.e.l.f", "TST_CLIPBOARD_LOG",
                          RT_ELEMENTS(s_apszLogGroups), s_apszLogGroups, RTLOGDEST_STDOUT, NULL);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTLogGroupSettings(g_pLogger, "main.e.l+shared_clipboard.e.l.l2.f");
        if (RT_SUCCESS(vrc))
            RTLogRelSetDefaultInstance(g_pLogger);
    }
    else
        RTMsgWarning("Failed to create shared clipboard logger: %Rrc", vrc);
}


/**
 * Logs COM error information for a testcase failure.
 *
 * @param   hTest           Test handle.
 * @param   pszWhat         Operation that failed.
 * @param   errorInfo       Error information to log.
 */
static void tstLogErrorInfo(RTTEST hTest, const char *pszWhat, const ErrorInfo &errorInfo)
{
    if (!errorInfo.isBasicAvailable())
    {
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s: no error info available\n", pszWhat);
        return;
    }

    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s: hrc=%Rhrc, text='%ls'\n",
                 pszWhat, errorInfo.getResultCode(), errorInfo.getText().raw());
    if (errorInfo.getComponent().raw())
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s: component='%ls'\n", pszWhat, errorInfo.getComponent().raw());
    if (errorInfo.getInterfaceName().raw())
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "%s: interface='%ls'\n", pszWhat, errorInfo.getInterfaceName().raw());
}


/**
 * Waits for a temporary testcase machine to become unlocked before unregistering it.
 *
 * @returns COM status code from the last SessionState query.
 * @param   hTest           Test handle.
 * @param   pMachine        Machine to query.
 */
static HRESULT tstWaitMachineUnlocked(RTTEST hTest, IMachine *pMachine)
{
    AssertPtrReturn(pMachine, E_POINTER);

    HRESULT hrc = S_OK;
    SessionState_T enmSessionState = SessionState_Unlocked;
    for (uint32_t i = 0; i < 100; ++i)
    {
        hrc = pMachine->COMGETTER(SessionState)(&enmSessionState);
        if (FAILED(hrc))
            return hrc;
        if (enmSessionState == SessionState_Unlocked)
            return S_OK;
        RTThreadSleep(100);
    }

    RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Machine still not unlocked; SessionState=%d\n", enmSessionState);
    return VBOX_E_INVALID_OBJECT_STATE;
}


/**
 * Tests clipboard object factory methods and the returned public objects.
 *
 * @param   hTest           Test handle.
 * @param   pClipboard      Clipboard API object to use.
 */
static void tstClipboardPublicObjects(RTTEST hTest, IClipboard *pClipboard)
{
    RTTestSub(hTest, "Clipboard public objects");

    ComPtr<IClipboardFormat> ptrFormat;
    HRESULT hrc = tstCreateFormat(pClipboard, "text/plain;charset=utf-8", ptrFormat);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("CreateFormat failed, hrc=%Rhrc\n", hrc));

    Bstr bstrMimeType;
    hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardFormat::COMGETTER(MimeType) failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrMimeType).c_str(), "text/plain;charset=utf-8"));

    hrc = ptrFormat->COMSETTER(MimeType)(Bstr("text/html").raw());
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardFormat::COMSETTER(MimeType) failed, hrc=%Rhrc\n", hrc));
    bstrMimeType.setNull();
    hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardFormat::COMGETTER(MimeType) after set failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrMimeType).c_str(), "text/html"));

    ComPtr<IClipboardFormat> ptrTextFormat;
    hrc = tstCreateFormat(pClipboard, "text/plain;charset=utf-8", ptrTextFormat);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("CreateFormat(text) failed, hrc=%Rhrc\n", hrc));

    static const char s_szText[] = "Hello from the Main clipboard testcase";
    std::vector<BYTE> abText(sizeof(s_szText));
    memcpy(&abText[0], s_szText, sizeof(s_szText));

    ComPtr<IClipboardItem> ptrItem;
    hrc = tstCreateItem(pClipboard, ClipboardSource_Host, ptrTextFormat, abText, ptrItem);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("CreateItem failed, hrc=%Rhrc\n", hrc));

    ClipboardSource_T enmSource = ClipboardSource_Guest;
    hrc = ptrItem->COMGETTER(Source)(&enmSource);
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Source) failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(enmSource == ClipboardSource_Host);

    ComPtr<IClipboardFormat> ptrItemFormat;
    hrc = ptrItem->COMGETTER(Format)(ptrItemFormat.asOutParam());
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Format) failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(!ptrItemFormat.isNull());
    if (ptrItemFormat.isNotNull())
    {
        Bstr bstrItemMimeType;
        hrc = ptrItemFormat->COMGETTER(MimeType)(bstrItemMimeType.asOutParam());
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::Format::COMGETTER(MimeType) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrItemMimeType).c_str(), "text/plain;charset=utf-8"));
    }

    SafeArray<BYTE> aReadText;
    hrc = ptrItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aReadText));
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Buffer) failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK_MSG(tstByteArrayEquals(aReadText, abText),
                      ("IClipboardItem::COMGETTER(Buffer) returned %zu bytes, expected %zu\n", aReadText.size(), abText.size()));

    ULONG cbItem = 0;
    hrc = ptrItem->COMGETTER(Size)(&cbItem);
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Size) failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(cbItem == sizeof(s_szText));

    ComPtr<IClipboardFormat> ptrHtmlFormat;
    hrc = tstCreateFormat(pClipboard, "text/html", ptrHtmlFormat);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("CreateFormat(html) failed, hrc=%Rhrc\n", hrc));
    hrc = ptrItem->COMSETTER(Format)(ptrHtmlFormat);
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMSETTER(Format) failed, hrc=%Rhrc\n", hrc));
    ptrItemFormat.setNull();
    hrc = ptrItem->COMGETTER(Format)(ptrItemFormat.asOutParam());
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Format) after set failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(!ptrItemFormat.isNull());
    if (ptrItemFormat.isNotNull())
    {
        Bstr bstrItemMimeType;
        hrc = ptrItemFormat->COMGETTER(MimeType)(bstrItemMimeType.asOutParam());
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::Format::COMGETTER(MimeType) after set failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrItemMimeType).c_str(), "text/html"));
    }

    static const char s_szHtml[] = "<p>Hello from the Main clipboard testcase</p>";
    SafeArray<BYTE> aHtml;
    hrc = aHtml.initFrom(reinterpret_cast<const BYTE *>(s_szHtml), sizeof(s_szHtml));
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("SafeArray initFrom failed, hrc=%Rhrc\n", hrc));
    hrc = ptrItem->COMSETTER(Buffer)(ComSafeArrayAsInParam(aHtml));
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMSETTER(Buffer) failed, hrc=%Rhrc\n", hrc));

    SafeArray<BYTE> aReadHtml;
    hrc = ptrItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aReadHtml));
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Buffer) after set failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(aReadHtml.size() == sizeof(s_szHtml));
    if (aReadHtml.size() == sizeof(s_szHtml))
        RTTESTI_CHECK(!memcmp(aReadHtml.raw(), s_szHtml, sizeof(s_szHtml)));

    hrc = ptrItem->COMGETTER(Size)(&cbItem);
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardItem::COMGETTER(Size) after set failed, hrc=%Rhrc\n", hrc));
    RTTESTI_CHECK(cbItem == sizeof(s_szHtml));
}



/**
 * Tests public clipboard session creation, event routing and close semantics.
 * Tests the public clipboard session API.
 *
 * @param   hTest               Test handle.
 * @param   pClipboard          Live console clipboard object.
 * @param   pClipboardSettings  Live clipboard settings object.
 * @param   pEventSource        Live console clipboard event source.
 */
static void tstClipboardPublicSessionApi(RTTEST hTest, IClipboard *pClipboard, IClipboardSettings *pClipboardSettings,
                                         IEventSource *pEventSource)
{
    RTTestSub(hTest, "Clipboard public session API");

    RTTESTI_CHECK_RETV(pClipboard != NULL);
    RTTESTI_CHECK_RETV(pClipboardSettings != NULL);
    RTTESTI_CHECK_RETV(pEventSource != NULL);

    RTTESTI_CHECK_MSG(VBOX_SHCL_MAIN_CLIENT_NONE == 0,
                      ("VBOX_SHCL_MAIN_CLIENT_NONE is %RU32, expected 0\n", (uint32_t)VBOX_SHCL_MAIN_CLIENT_NONE));

    HRESULT hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("COMSETTER(Mode)(Bidirectional) before sessions failed, hrc=%Rhrc\n", hrc));

    hrc = pClipboard->Reset();
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("Reset before sessions failed, hrc=%Rhrc\n", hrc));

    ComPtr<IClipboardFormat> ptrTextFormat;
    hrc = tstCreateFormat(pClipboard, "text/plain;charset=utf-8", ptrTextFormat);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("CreateFormat(session text) failed, hrc=%Rhrc\n", hrc));

    ComPtr<IClipboardFormat> ptrHtmlFormat;
    hrc = tstCreateFormat(pClipboard, "text/html", ptrHtmlFormat);
    RTTESTI_CHECK_MSG_RETV(SUCCEEDED(hrc), ("CreateFormat(session html) failed, hrc=%Rhrc\n", hrc));

    std::vector<ComPtr<IClipboardFormat> > vecTextFormats;
    vecTextFormats.push_back(ptrTextFormat);
    SafeIfaceArray<IClipboardFormat> aTextFormats(vecTextFormats);

    std::vector<ComPtr<IClipboardFormat> > vecHtmlFormats;
    vecHtmlFormats.push_back(ptrHtmlFormat);
    SafeIfaceArray<IClipboardFormat> aHtmlFormats(vecHtmlFormats);


    /* Basic session construction, identity, accessors, raw data, format offer, and close. */
    {
        ComPtr<IClipboardSession> ptrSessionA;
        ComPtr<IClipboardSession> ptrSessionB;
        ComPtr<IEventSource> ptrObserverEventSource;
        ComPtr<IEventListener> ptrObserverListener;
        bool fObserverListenerRegistered = false;
        do
        {
            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrSessionA);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(empty A) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrSessionA.isNull(), ("CreateSession(empty A) returned NULL\n"));

            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrSessionB);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(empty B) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrSessionB.isNull(), ("CreateSession(empty B) returned NULL\n"));

            ULONG idSessionA = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrSessionA->COMGETTER(Id)(&idSessionA);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::COMGETTER(Id)(A) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idSessionA != VBOX_SHCL_MAIN_CLIENT_NONE,
                              ("Session A returned anonymous/zero client ID\n"));

            ULONG idSessionB = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrSessionB->COMGETTER(Id)(&idSessionB);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::COMGETTER(Id)(B) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idSessionB != VBOX_SHCL_MAIN_CLIENT_NONE,
                              ("Session B returned anonymous/zero client ID\n"));
            RTTESTI_CHECK_MSG(idSessionA != idSessionB,
                              ("Session IDs are not distinct: %RU32\n", (uint32_t)idSessionA));

            ComPtr<IEventSource> ptrSessionEventSource;
            hrc = ptrSessionA->COMGETTER(EventSource)(ptrSessionEventSource.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::COMGETTER(EventSource) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(!ptrSessionEventSource.isNull());

            ComPtr<IHostClipboard> ptrSessionHostClipboard;
            hrc = ptrSessionA->COMGETTER(HostClipboard)(ptrSessionHostClipboard.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::COMGETTER(HostClipboard) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(!ptrSessionHostClipboard.isNull());

            hrc = ptrSessionB->COMGETTER(EventSource)(ptrObserverEventSource.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("IClipboardSession::COMGETTER(EventSource)(observer) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrObserverEventSource.isNull(), ("IClipboardSession::COMGETTER(EventSource)(observer) returned NULL\n"));

            static VBoxEventType_T const s_aBasicEventTypes[] =
            {
                VBoxEventType_OnClipboardFormatChanged,
                VBoxEventType_OnClipboardDataChanged
            };
            hrc = tstRegisterClipboardListener(ptrObserverEventSource, s_aBasicEventTypes, RT_ELEMENTS(s_aBasicEventTypes),
                                               ptrObserverListener);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(session observer) failed, hrc=%Rhrc\n", hrc));
            fObserverListenerRegistered = true;

            SafeIfaceArray<IClipboardFormat> aReadFormats;
            hrc = ptrSessionA->ReadFormats(ComSafeArrayAsOutParam(aReadFormats));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::ReadFormats(empty) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(aReadFormats.size() == 0);

            hrc = ptrSessionA->WriteFormats(ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::WriteFormats failed, hrc=%Rhrc\n", hrc));

            LONG64 i64FormatRevision = 0;
            ComPtr<IEvent> ptrFormatEvent;
            VBoxEventType_T enmFormatEventType = VBoxEventType_Invalid;
            bool fRc = tstClipboardWaitForAnyEvent(ptrObserverEventSource, ptrObserverListener, s_aBasicEventTypes,
                                                   RT_ELEMENTS(s_aBasicEventTypes), 1000 /* cMsTimeout */,
                                                   "session observer format", ptrFormatEvent, &enmFormatEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
            {
                RTTESTI_CHECK(enmFormatEventType == VBoxEventType_OnClipboardFormatChanged);
                if (enmFormatEventType == VBoxEventType_OnClipboardFormatChanged)
                    RTTESTI_CHECK(tstClipboardCheckFormatChangedEvent(ptrFormatEvent, "session observer format event",
                                                                      idSessionA, ClipboardSource_Host,
                                                                      "text/plain;charset=utf-8", &i64FormatRevision));
            }

            aReadFormats.setNull();
            hrc = ptrSessionA->ReadFormats(ComSafeArrayAsOutParam(aReadFormats));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::ReadFormats(after WriteFormats) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(tstClipboardCheckSingleFormat(aReadFormats, "text/plain;charset=utf-8",
                                                        "session ReadFormats after WriteFormats"));

            static const char s_szSessionRawText[] = "tstClipboard session raw data";
            std::vector<BYTE> abSessionRawData = tstBytesFromString(s_szSessionRawText);
            SafeArray<BYTE> aSessionRawData;
            hrc = tstSafeArrayFromBytes(abSessionRawData, aSessionRawData);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SafeArray initFrom(session raw data) failed, hrc=%Rhrc\n", hrc));

            ClipboardSource_T enmWrittenSource = ClipboardSource_Custom;
            Bstr bstrWrittenMimeType;
            SafeArray<BYTE> aWrittenBuffer;
            hrc = ptrSessionA->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host,
                                            Bstr("text/plain;charset=utf-8").raw(),
                                            ComSafeArrayAsInParam(aSessionRawData), &enmWrittenSource,
                                            bstrWrittenMimeType.asOutParam(), ComSafeArrayAsOutParam(aWrittenBuffer));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("IClipboardSession::WriteDataRaw failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(enmWrittenSource == ClipboardSource_Host);
            RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrWrittenMimeType).c_str(), "text/plain;charset=utf-8"));
            RTTESTI_CHECK(tstByteArrayEquals(aWrittenBuffer, abSessionRawData));

            LONG64 i64DataRevision = 0;
            {
                ComPtr<IEvent> ptrDataEvent;
                VBoxEventType_T enmDataEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrObserverEventSource, ptrObserverListener, s_aBasicEventTypes,
                                                  RT_ELEMENTS(s_aBasicEventTypes), 1000 /* cMsTimeout */,
                                                  "session observer data", ptrDataEvent, &enmDataEventType);
                RTTESTI_CHECK(fRc);
                if (fRc)
                {
                    RTTESTI_CHECK(enmDataEventType == VBoxEventType_OnClipboardDataChanged);
                    if (enmDataEventType == VBoxEventType_OnClipboardDataChanged)
                        RTTESTI_CHECK(tstClipboardCheckDataChangedEvent(ptrDataEvent, "session observer data event",
                                                                        idSessionA, ClipboardAction_Copy,
                                                                        NULL /* pptrItem */, &i64DataRevision));
                }
            }
            if (i64FormatRevision > 0 && i64DataRevision > 0)
                RTTESTI_CHECK_MSG(i64DataRevision > i64FormatRevision,
                                  ("session data revision %RI64, expected greater than format revision %RI64\n",
                                   i64DataRevision, i64FormatRevision));
            RTTESTI_CHECK(tstReadSessionDataRawEquals(ptrSessionA, "session raw round-trip", ClipboardSource_Host,
                                                      "text/plain;charset=utf-8", abSessionRawData));

            hrc = ptrSessionA->Close();
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::Close(basic session A) failed, hrc=%Rhrc\n", hrc));
            ULONG idSessionAAfterClose = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrSessionA->COMGETTER(Id)(&idSessionAAfterClose);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Id)(basic session A after Close) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idSessionAAfterClose == idSessionA,
                              ("Closed session ID changed from %RU32 to %RU32\n",
                               (uint32_t)idSessionA, (uint32_t)idSessionAAfterClose));

            ComPtr<IEventSource> ptrClosedEventSource;
            hrc = ptrSessionA->COMGETTER(EventSource)(ptrClosedEventSource.asOutParam());
            RTTESTI_CHECK_MSG(FAILED(hrc), ("COMGETTER(EventSource)(closed session A) unexpectedly succeeded\n"));

            ComPtr<IHostClipboard> ptrClosedHostClipboard;
            hrc = ptrSessionA->COMGETTER(HostClipboard)(ptrClosedHostClipboard.asOutParam());
            RTTESTI_CHECK_MSG(FAILED(hrc), ("COMGETTER(HostClipboard)(closed session A) unexpectedly succeeded\n"));

            SafeIfaceArray<IClipboardFormat> aClosedFormats;
            hrc = ptrSessionA->ReadFormats(ComSafeArrayAsOutParam(aClosedFormats));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("ReadFormats(closed session A) unexpectedly succeeded\n"));

            ClipboardSource_T enmClosedReadSource = ClipboardSource_Custom;
            Bstr bstrClosedRequestedMimeType("");
            Bstr bstrClosedReadMimeType;
            SafeArray<BYTE> aClosedReadBuffer;
            hrc = ptrSessionA->ReadDataRaw(ClipboardAction_Copy, bstrClosedRequestedMimeType.raw(), &enmClosedReadSource,
                                           bstrClosedReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aClosedReadBuffer));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("ReadDataRaw(closed session A) unexpectedly succeeded\n"));

            ClipboardSource_T enmClosedWrittenSource = ClipboardSource_Custom;
            Bstr bstrClosedWrittenMimeType;
            SafeArray<BYTE> aClosedWrittenBuffer;
            hrc = ptrSessionA->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host,
                                            Bstr("text/plain;charset=utf-8").raw(), ComSafeArrayAsInParam(aSessionRawData),
                                            &enmClosedWrittenSource, bstrClosedWrittenMimeType.asOutParam(),
                                            ComSafeArrayAsOutParam(aClosedWrittenBuffer));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("WriteDataRaw(closed session A) unexpectedly succeeded\n"));

            hrc = ptrSessionA->WriteFormats(ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("WriteFormats(closed session A) unexpectedly succeeded\n"));

            hrc = ptrSessionA->Close();
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::Close(closed session A) failed, hrc=%Rhrc\n", hrc));

            hrc = ptrSessionHostClipboard->Clear();
            RTTESTI_CHECK_MSG(FAILED(hrc), ("HostClipboard::Clear(closed session A endpoint) unexpectedly succeeded\n"));
            hrc = ptrSessionHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest,
                                                         ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("HostClipboard::ReportFormats(closed session A endpoint) unexpectedly succeeded\n"));
            hrc = ptrSessionHostClipboard->SetData(ClipboardAction_Copy, ClipboardSource_Guest,
                                                   Bstr("text/plain;charset=utf-8").raw(),
                                                   ComSafeArrayAsInParam(aSessionRawData));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("HostClipboard::SetData(closed session A endpoint) unexpectedly succeeded\n"));
            hrc = ptrSessionHostClipboard->ProvideData(1 /* aRequestId */, ClipboardAction_Copy, ClipboardSource_Guest,
                                                       Bstr("text/plain;charset=utf-8").raw(),
                                                       ComSafeArrayAsInParam(aSessionRawData));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("HostClipboard::ProvideData(closed session A endpoint) unexpectedly succeeded\n"));

            SafeIfaceArray<IClipboardFormat> aOwnerClearedFormats;
            hrc = pClipboard->ReadFormats(ComSafeArrayAsOutParam(aOwnerClearedFormats));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("ReadFormats after closing owning session failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(aOwnerClearedFormats.size() == 0,
                              ("Closing owning session left %zu clipboard formats advertised\n", aOwnerClearedFormats.size()));
        } while (0);

        if (fObserverListenerRegistered && ptrObserverEventSource.isNotNull() && ptrObserverListener.isNotNull())
            ptrObserverEventSource->UnregisterListener(ptrObserverListener);
        ptrObserverListener.setNull();

        tstClipboardCloseSession(ptrSessionA, "basic session A");
        tstClipboardCloseSession(ptrSessionB, "basic session B");
        hrc = pClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after basic session test failed, hrc=%Rhrc\n", hrc));
    }


    /* Session host clipboard endpoints tag lazy native-host requests with the session client ID. */
    {
        ComPtr<IClipboardSession> ptrHostSession;
        ComPtr<IHostClipboard> ptrSessionHostClipboard;
        ComPtr<IEventListener> ptrRequestListener;
        bool fRequestListenerRegistered = false;
        do
        {
            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrHostSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(host clipboard) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrHostSession.isNull(), ("CreateSession(host clipboard) returned NULL\n"));

            ULONG idHostSession = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrHostSession->COMGETTER(Id)(&idHostSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Id)(host clipboard session) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idHostSession != VBOX_SHCL_MAIN_CLIENT_NONE,
                              ("Host clipboard session returned zero client ID\n"));

            hrc = ptrHostSession->COMGETTER(HostClipboard)(ptrSessionHostClipboard.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(HostClipboard)(session) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrSessionHostClipboard.isNull(), ("COMGETTER(HostClipboard)(session) returned NULL\n"));

            hrc = ptrSessionHostClipboard->ReportFormats(ClipboardAction_Copy, ClipboardSource_Guest,
                                                         ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("Session HostClipboard::ReportFormats failed, hrc=%Rhrc\n", hrc));

            hrc = pEventSource->CreateListener(ptrRequestListener.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateListener(session host request) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrRequestListener.isNull(), ("CreateListener(session host request) returned NULL\n"));
            SafeArray<VBoxEventType_T> aRequestEventTypes;
            aRequestEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
            hrc = pEventSource->RegisterListener(ptrRequestListener, ComSafeArrayAsInParam(aRequestEventTypes), FALSE /* aActive */);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(session host request) failed, hrc=%Rhrc\n", hrc));
            fRequestListenerRegistered = true;

            ComPtr<IInternalClipboardControl> ptrInternalClipboardControl(pClipboard);
            RTTESTI_CHECK_MSG_BREAK(!ptrInternalClipboardControl.isNull(), ("Query IInternalClipboardControl(session host) returned NULL\n"));

            ULONG idRequest = 0;
            Bstr bstrMimeType("text/plain;charset=utf-8");
            hrc = ptrInternalClipboardControl->RequestData(bstrMimeType.raw(), &idRequest);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("IInternalClipboardControl::RequestData(session host) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idRequest != 0, ("IInternalClipboardControl::RequestData(session host) returned zero request ID\n"));

            ComPtr<IEvent> ptrRequestEvent;
            hrc = pEventSource->GetEvent(ptrRequestListener, 1000 /* aTimeout */, ptrRequestEvent.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("GetEvent(session host request) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrRequestEvent.isNull(), ("GetEvent(session host request) returned no event\n"));

            ComPtr<IClipboardDataRequestedEvent> ptrDataRequestedEvent = ptrRequestEvent;
            RTTESTI_CHECK(!ptrDataRequestedEvent.isNull());
            if (ptrDataRequestedEvent.isNotNull())
            {
                RTTESTI_CHECK(tstClipboardCheckEventMetadata(ptrDataRequestedEvent, "session host data requested event",
                                                            idHostSession));

                ULONG idEventRequest = 0;
                hrc = ptrDataRequestedEvent->COMGETTER(RequestId)(&idEventRequest);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(RequestId)(session host) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK_MSG(idEventRequest == idRequest,
                                  ("Session host request event ID %RU32, expected %RU32\n",
                                   (uint32_t)idEventRequest, (uint32_t)idRequest));

                ClipboardSource_T enmRequestSource = ClipboardSource_Custom;
                hrc = ptrDataRequestedEvent->COMGETTER(ClipboardSource)(&enmRequestSource);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(ClipboardSource)(session host) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(enmRequestSource == ClipboardSource_Guest);
            }

            static const char s_szSessionHostText[] = "tstClipboard session host clipboard data";
            std::vector<BYTE> abSessionHostData = tstBytesFromString(s_szSessionHostText);
            SafeArray<BYTE> aSessionHostData;
            hrc = tstSafeArrayFromBytes(abSessionHostData, aSessionHostData);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SafeArray initFrom(session host data) failed, hrc=%Rhrc\n", hrc));

            hrc = ptrSessionHostClipboard->ProvideData(idRequest, ClipboardAction_Copy, ClipboardSource_Guest,
                                                       bstrMimeType.raw(), ComSafeArrayAsInParam(aSessionHostData));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Session HostClipboard::ProvideData failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(tstReadSessionDataRawEquals(ptrHostSession, "session HostClipboard ProvideData readback",
                                                      ClipboardSource_Guest, "text/plain;charset=utf-8", abSessionHostData));
        } while (0);

        if (fRequestListenerRegistered && ptrRequestListener.isNotNull())
            pEventSource->UnregisterListener(ptrRequestListener);
        ptrRequestListener.setNull();

        tstClipboardCloseSession(ptrHostSession, "session HostClipboard session");
        hrc = pClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after session HostClipboard test failed, hrc=%Rhrc\n", hrc));
    }


    /* IncludeInitialState replays the state current when a session listener registers. */
    {
        ComPtr<IClipboardSession> ptrInitialSession;
        ComPtr<IEventSource> ptrInitialEventSource;
        ComPtr<IEventListener> ptrInitialListener;
        bool fInitialListenerRegistered = false;
        do
        {
            hrc = pClipboard->WriteFormats(ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WriteFormats(seed initial state) failed, hrc=%Rhrc\n", hrc));

            static IClipboardSessionFlag_T const s_aInitialFlags[] =
            {
                IClipboardSessionFlag_IncludeInitialState
            };
            hrc = tstCreateSession(pClipboard, s_aInitialFlags, RT_ELEMENTS(s_aInitialFlags), ptrInitialSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(IncludeInitialState) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrInitialSession.isNull(), ("CreateSession(IncludeInitialState) returned NULL\n"));

            hrc = ptrInitialSession->COMGETTER(EventSource)(ptrInitialEventSource.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(initial) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrInitialEventSource.isNull(), ("COMGETTER(EventSource)(initial) returned NULL\n"));

            hrc = pClipboard->WriteFormats(ComSafeArrayAsInParam(aHtmlFormats));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WriteFormats(update initial state before listener) failed, hrc=%Rhrc\n", hrc));

            static VBoxEventType_T const s_aInitialEventTypes[] =
            {
                VBoxEventType_OnClipboardFormatChanged
            };
            hrc = tstRegisterClipboardListener(ptrInitialEventSource, s_aInitialEventTypes, RT_ELEMENTS(s_aInitialEventTypes),
                                               ptrInitialListener);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(initial state) failed, hrc=%Rhrc\n", hrc));
            fInitialListenerRegistered = true;

            ComPtr<IEvent> ptrInitialEvent;
            VBoxEventType_T enmInitialEventType = VBoxEventType_Invalid;
            bool fRc = tstClipboardWaitForAnyEvent(ptrInitialEventSource, ptrInitialListener, s_aInitialEventTypes,
                                                   RT_ELEMENTS(s_aInitialEventTypes), 1000 /* cMsTimeout */,
                                                   "IncludeInitialState", ptrInitialEvent, &enmInitialEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
            {
                RTTESTI_CHECK(enmInitialEventType == VBoxEventType_OnClipboardFormatChanged);
                RTTESTI_CHECK(tstClipboardCheckFormatChangedEvent(ptrInitialEvent, "IncludeInitialState format event",
                                                                  VBOX_SHCL_MAIN_CLIENT_NONE, ClipboardSource_Host,
                                                                  "text/html"));
            }

            SafeIfaceArray<IClipboardFormat> aInitialReadFormats;
            hrc = ptrInitialSession->ReadFormats(ComSafeArrayAsOutParam(aInitialReadFormats));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardSession::ReadFormats(IncludeInitialState) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(tstClipboardCheckSingleFormat(aInitialReadFormats, "text/html",
                                                        "IncludeInitialState current formats"));
        } while (0);

        if (fInitialListenerRegistered && ptrInitialEventSource.isNotNull() && ptrInitialListener.isNotNull())
            ptrInitialEventSource->UnregisterListener(ptrInitialListener);
        ptrInitialListener.setNull();

        tstClipboardCloseSession(ptrInitialSession, "IncludeInitialState session");
        hrc = pClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after IncludeInitialState test failed, hrc=%Rhrc\n", hrc));
    }


    /* ExcludeOwnChanges suppresses events for the writing session while other sessions see its client ID. */
    {
        ComPtr<IClipboardSession> ptrSessionA;
        ComPtr<IClipboardSession> ptrSessionB;
        ComPtr<IEventSource> ptrEventSourceA;
        ComPtr<IEventSource> ptrEventSourceB;
        ComPtr<IEventListener> ptrListenerA;
        ComPtr<IEventListener> ptrListenerB;
        bool fListenerARegistered = false;
        bool fListenerBRegistered = false;
        do
        {
            static IClipboardSessionFlag_T const s_aExcludeOwnFlags[] =
            {
                IClipboardSessionFlag_ExcludeOwnChanges
            };
            hrc = tstCreateSession(pClipboard, s_aExcludeOwnFlags, RT_ELEMENTS(s_aExcludeOwnFlags), ptrSessionA);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(ExcludeOwnChanges) failed, hrc=%Rhrc\n", hrc));
            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrSessionB);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(observer) failed, hrc=%Rhrc\n", hrc));

            ULONG idSessionA = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrSessionA->COMGETTER(Id)(&idSessionA);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Id)(ExcludeOwnChanges) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idSessionA != VBOX_SHCL_MAIN_CLIENT_NONE,
                              ("ExcludeOwnChanges session returned zero client ID\n"));

            hrc = ptrSessionA->COMGETTER(EventSource)(ptrEventSourceA.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(A) failed, hrc=%Rhrc\n", hrc));
            hrc = ptrSessionB->COMGETTER(EventSource)(ptrEventSourceB.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(B) failed, hrc=%Rhrc\n", hrc));

            static VBoxEventType_T const s_aOwnEventTypes[] =
            {
                VBoxEventType_OnClipboardFormatChanged,
                VBoxEventType_OnClipboardDataChanged
            };
            hrc = tstRegisterClipboardListener(ptrEventSourceA, s_aOwnEventTypes, RT_ELEMENTS(s_aOwnEventTypes), ptrListenerA);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(A own changes) failed, hrc=%Rhrc\n", hrc));
            fListenerARegistered = true;

            static VBoxEventType_T const s_aDataEventTypes[] =
            {
                VBoxEventType_OnClipboardDataChanged
            };
            hrc = tstRegisterClipboardListener(ptrEventSourceB, s_aDataEventTypes, RT_ELEMENTS(s_aDataEventTypes), ptrListenerB);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(B observer) failed, hrc=%Rhrc\n", hrc));
            fListenerBRegistered = true;

            static const char s_szSessionAText[] = "tstClipboard session A writes";
            std::vector<BYTE> abSessionAData = tstBytesFromString(s_szSessionAText);
            SafeArray<BYTE> aSessionAData;
            hrc = tstSafeArrayFromBytes(abSessionAData, aSessionAData);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SafeArray initFrom(session A data) failed, hrc=%Rhrc\n", hrc));

            ClipboardSource_T enmWrittenSource = ClipboardSource_Custom;
            Bstr bstrWrittenMimeType;
            SafeArray<BYTE> aWrittenBuffer;
            hrc = ptrSessionA->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host,
                                            Bstr("text/plain;charset=utf-8").raw(),
                                            ComSafeArrayAsInParam(aSessionAData), &enmWrittenSource,
                                            bstrWrittenMimeType.asOutParam(), ComSafeArrayAsOutParam(aWrittenBuffer));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("Session A WriteDataRaw failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(tstReadSessionDataRawEquals(ptrSessionA, "ExcludeOwnChanges committed state",
                                                      ClipboardSource_Host, "text/plain;charset=utf-8", abSessionAData));

            ComPtr<IEvent> ptrObservedEvent;
            VBoxEventType_T enmObservedEventType = VBoxEventType_Invalid;
            bool const fRc = tstClipboardWaitForAnyEvent(ptrEventSourceB, ptrListenerB, s_aDataEventTypes,
                                                         RT_ELEMENTS(s_aDataEventTypes), 1000 /* cMsTimeout */,
                                                         "ExcludeOwnChanges observer", ptrObservedEvent,
                                                         &enmObservedEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
            {
                RTTESTI_CHECK(tstClipboardCheckDataChangedEvent(ptrObservedEvent,
                                                                "ExcludeOwnChanges observer data event",
                                                                idSessionA, ClipboardAction_Copy,
                                                                NULL /* pptrItem */));
            }
            RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSourceA, ptrListenerA, 250 /* cMsTimeout */,
                                                    "ExcludeOwnChanges writer"));
        } while (0);

        if (fListenerARegistered && ptrEventSourceA.isNotNull() && ptrListenerA.isNotNull())
            ptrEventSourceA->UnregisterListener(ptrListenerA);
        if (fListenerBRegistered && ptrEventSourceB.isNotNull() && ptrListenerB.isNotNull())
            ptrEventSourceB->UnregisterListener(ptrListenerB);
        ptrListenerA.setNull();
        ptrListenerB.setNull();

        tstClipboardCloseSession(ptrSessionA, "ExcludeOwnChanges session A");
        tstClipboardCloseSession(ptrSessionB, "ExcludeOwnChanges session B");
        hrc = pClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after ExcludeOwnChanges test failed, hrc=%Rhrc\n", hrc));
    }


    /* IncludePayload attaches the item payload to data events for sessions that request it. */
    {
        ComPtr<IClipboardSession> ptrWriterSession;
        ComPtr<IClipboardSession> ptrPayloadSession;
        ComPtr<IClipboardSession> ptrNoPayloadSession;
        ComPtr<IEventSource> ptrPayloadEventSource;
        ComPtr<IEventSource> ptrNoPayloadEventSource;
        ComPtr<IEventListener> ptrPayloadListener;
        ComPtr<IEventListener> ptrNoPayloadListener;
        bool fPayloadListenerRegistered = false;
        bool fNoPayloadListenerRegistered = false;
        do
        {
            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrWriterSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(payload writer) failed, hrc=%Rhrc\n", hrc));
            ULONG idWriterSession = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrWriterSession->COMGETTER(Id)(&idWriterSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Id)(payload writer) failed, hrc=%Rhrc\n", hrc));

            static IClipboardSessionFlag_T const s_aPayloadFlags[] =
            {
                IClipboardSessionFlag_IncludePayload
            };
            hrc = tstCreateSession(pClipboard, s_aPayloadFlags, RT_ELEMENTS(s_aPayloadFlags), ptrPayloadSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(IncludePayload) failed, hrc=%Rhrc\n", hrc));
            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrNoPayloadSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(no payload observer) failed, hrc=%Rhrc\n", hrc));

            hrc = ptrPayloadSession->COMGETTER(EventSource)(ptrPayloadEventSource.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(IncludePayload) failed, hrc=%Rhrc\n", hrc));
            hrc = ptrNoPayloadSession->COMGETTER(EventSource)(ptrNoPayloadEventSource.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(no payload) failed, hrc=%Rhrc\n", hrc));

            static VBoxEventType_T const s_aPayloadEventTypes[] =
            {
                VBoxEventType_OnClipboardDataChanged,
                VBoxEventType_OnClipboardDataRequested
            };
            hrc = tstRegisterClipboardListener(ptrPayloadEventSource, s_aPayloadEventTypes,
                                               RT_ELEMENTS(s_aPayloadEventTypes),
                                               ptrPayloadListener);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(IncludePayload) failed, hrc=%Rhrc\n", hrc));
            fPayloadListenerRegistered = true;
            hrc = tstRegisterClipboardListener(ptrNoPayloadEventSource, s_aPayloadEventTypes,
                                               RT_ELEMENTS(s_aPayloadEventTypes),
                                               ptrNoPayloadListener);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(no payload) failed, hrc=%Rhrc\n", hrc));
            fNoPayloadListenerRegistered = true;

            static const char s_szPayloadText[] = "tstClipboard session payload data";
            std::vector<BYTE> abPayloadData = tstBytesFromString(s_szPayloadText);
            SafeArray<BYTE> aPayloadData;
            hrc = tstSafeArrayFromBytes(abPayloadData, aPayloadData);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SafeArray initFrom(payload data) failed, hrc=%Rhrc\n", hrc));

            ClipboardSource_T enmWrittenSource = ClipboardSource_Custom;
            Bstr bstrWrittenMimeType;
            SafeArray<BYTE> aWrittenBuffer;
            hrc = ptrWriterSession->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Host,
                                                 Bstr("text/plain;charset=utf-8").raw(),
                                                 ComSafeArrayAsInParam(aPayloadData), &enmWrittenSource,
                                                 bstrWrittenMimeType.asOutParam(), ComSafeArrayAsOutParam(aWrittenBuffer));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("Payload writer WriteDataRaw failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(tstReadSessionDataRawEquals(ptrWriterSession, "IncludePayload committed state",
                                                      ClipboardSource_Host, "text/plain;charset=utf-8", abPayloadData));

            ComPtr<IEvent> ptrPayloadEvent;
            VBoxEventType_T enmPayloadEventType = VBoxEventType_Invalid;
            bool fRc = tstClipboardWaitForAnyEvent(ptrPayloadEventSource, ptrPayloadListener, s_aPayloadEventTypes,
                                                   RT_ELEMENTS(s_aPayloadEventTypes), 1000 /* cMsTimeout */,
                                                   "IncludePayload listener", ptrPayloadEvent, &enmPayloadEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
            {
                ComPtr<IClipboardItem> ptrPayloadItem;
                RTTESTI_CHECK(tstClipboardCheckDataChangedEvent(ptrPayloadEvent, "IncludePayload data event",
                                                                idWriterSession, ClipboardAction_Copy,
                                                                &ptrPayloadItem));
                RTTESTI_CHECK_MSG(!ptrPayloadItem.isNull(), ("IncludePayload data event did not include an item\n"));
                if (ptrPayloadItem.isNotNull())
                    RTTESTI_CHECK(tstClipboardCheckItemPayload(ptrPayloadItem, "IncludePayload event item",
                                                               ClipboardSource_Host, "text/plain;charset=utf-8",
                                                               abPayloadData));
            }

            ComPtr<IEvent> ptrNoPayloadEvent;
            VBoxEventType_T enmNoPayloadEventType = VBoxEventType_Invalid;
            fRc = tstClipboardWaitForAnyEvent(ptrNoPayloadEventSource, ptrNoPayloadListener, s_aPayloadEventTypes,
                                              RT_ELEMENTS(s_aPayloadEventTypes), 1000 /* cMsTimeout */,
                                              "No IncludePayload listener", ptrNoPayloadEvent,
                                              &enmNoPayloadEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
            {
                ComPtr<IClipboardItem> ptrNoPayloadItem;
                RTTESTI_CHECK(tstClipboardCheckDataChangedEvent(ptrNoPayloadEvent,
                                                                "No IncludePayload data event",
                                                                idWriterSession, ClipboardAction_Copy,
                                                                &ptrNoPayloadItem));
                RTTESTI_CHECK_MSG(ptrNoPayloadItem.isNull(),
                                  ("No IncludePayload data event unexpectedly included an item\n"));
            }

            ComPtr<IInternalClipboardControl> ptrInternalClipboardControl(pClipboard);
            RTTESTI_CHECK_MSG_BREAK(!ptrInternalClipboardControl.isNull(),
                                    ("Query IInternalClipboardControl(IncludePayload) returned NULL\n"));
            ULONG idRequest = 0;
            hrc = ptrInternalClipboardControl->RequestData(Bstr("text/plain;charset=utf-8").raw(), &idRequest);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc),
                                    ("IInternalClipboardControl::RequestData(IncludePayload) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idRequest != 0, ("IncludePayload request returned zero request ID\n"));

            ComPtr<IEvent> ptrPayloadRequestEvent;
            fRc = tstClipboardWaitForAnyEvent(ptrPayloadEventSource, ptrPayloadListener, s_aPayloadEventTypes,
                                              RT_ELEMENTS(s_aPayloadEventTypes), 1000 /* cMsTimeout */,
                                              "IncludePayload request listener", ptrPayloadRequestEvent,
                                              &enmPayloadEventType);
            RTTESTI_CHECK(fRc);
            RTTESTI_CHECK(enmPayloadEventType == VBoxEventType_OnClipboardDataRequested);
            if (fRc && enmPayloadEventType == VBoxEventType_OnClipboardDataRequested)
            {
                ComPtr<IClipboardDataRequestedEvent> ptrPayloadRequest = ptrPayloadRequestEvent;
                RTTESTI_CHECK(!ptrPayloadRequest.isNull());
                ComPtr<IClipboardItem> ptrPayloadRequestItem;
                if (ptrPayloadRequest.isNotNull())
                    hrc = ptrPayloadRequest->COMGETTER(Item)(ptrPayloadRequestItem.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("COMGETTER(Item)(IncludePayload request) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK_MSG(!ptrPayloadRequestItem.isNull(),
                                  ("IncludePayload request event did not include an item\n"));
                if (ptrPayloadRequestItem.isNotNull())
                    RTTESTI_CHECK(tstClipboardCheckItemPayload(ptrPayloadRequestItem,
                                                               "IncludePayload request item",
                                                               ClipboardSource_Host,
                                                               "text/plain;charset=utf-8", abPayloadData));
            }

            ComPtr<IEvent> ptrNoPayloadRequestEvent;
            fRc = tstClipboardWaitForAnyEvent(ptrNoPayloadEventSource, ptrNoPayloadListener, s_aPayloadEventTypes,
                                              RT_ELEMENTS(s_aPayloadEventTypes), 1000 /* cMsTimeout */,
                                              "No IncludePayload request listener", ptrNoPayloadRequestEvent,
                                              &enmNoPayloadEventType);
            RTTESTI_CHECK(fRc);
            RTTESTI_CHECK(enmNoPayloadEventType == VBoxEventType_OnClipboardDataRequested);
            if (fRc && enmNoPayloadEventType == VBoxEventType_OnClipboardDataRequested)
            {
                ComPtr<IClipboardDataRequestedEvent> ptrNoPayloadRequest = ptrNoPayloadRequestEvent;
                RTTESTI_CHECK(!ptrNoPayloadRequest.isNull());
                ComPtr<IClipboardItem> ptrNoPayloadRequestItem;
                if (ptrNoPayloadRequest.isNotNull())
                    hrc = ptrNoPayloadRequest->COMGETTER(Item)(ptrNoPayloadRequestItem.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("COMGETTER(Item)(no payload request) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK_MSG(ptrNoPayloadRequestItem.isNull(),
                                  ("No IncludePayload request event unexpectedly included an item\n"));
            }
        } while (0);

        if (fPayloadListenerRegistered && ptrPayloadEventSource.isNotNull() && ptrPayloadListener.isNotNull())
            ptrPayloadEventSource->UnregisterListener(ptrPayloadListener);
        if (fNoPayloadListenerRegistered && ptrNoPayloadEventSource.isNotNull() && ptrNoPayloadListener.isNotNull())
            ptrNoPayloadEventSource->UnregisterListener(ptrNoPayloadListener);
        ptrPayloadListener.setNull();
        ptrNoPayloadListener.setNull();

        tstClipboardCloseSession(ptrWriterSession, "IncludePayload writer session");
        tstClipboardCloseSession(ptrPayloadSession, "IncludePayload observer session");
        tstClipboardCloseSession(ptrNoPayloadSession, "No IncludePayload observer session");
        hrc = pClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after IncludePayload test failed, hrc=%Rhrc\n", hrc));
    }


    /* ExcludeReflections suppresses matching anonymous format reflections, not direct session events. */
    {
        ComPtr<IClipboardSession> ptrReflectSession;
        ComPtr<IClipboardSession> ptrObserverSession;
        ComPtr<IEventSource> ptrReflectEventSource;
        ComPtr<IEventSource> ptrObserverEventSource;
        ComPtr<IEventListener> ptrReflectListener;
        ComPtr<IEventListener> ptrObserverListener;
        bool fReflectListenerRegistered = false;
        bool fObserverListenerRegistered = false;
        do
        {
            static IClipboardSessionFlag_T const s_aExcludeReflectionFlags[] =
            {
                IClipboardSessionFlag_ExcludeReflections
            };
            hrc = tstCreateSession(pClipboard, s_aExcludeReflectionFlags, RT_ELEMENTS(s_aExcludeReflectionFlags),
                                   ptrReflectSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(ExcludeReflections) failed, hrc=%Rhrc\n", hrc));
            hrc = tstCreateSession(pClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrObserverSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(reflection observer) failed, hrc=%Rhrc\n", hrc));

            ULONG idReflectSession = VBOX_SHCL_MAIN_CLIENT_NONE;
            hrc = ptrReflectSession->COMGETTER(Id)(&idReflectSession);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Id)(ExcludeReflections) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(idReflectSession != VBOX_SHCL_MAIN_CLIENT_NONE,
                              ("ExcludeReflections session returned zero client ID\n"));

            hrc = ptrReflectSession->COMGETTER(EventSource)(ptrReflectEventSource.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(ExcludeReflections) failed, hrc=%Rhrc\n", hrc));
            hrc = ptrObserverSession->COMGETTER(EventSource)(ptrObserverEventSource.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(EventSource)(reflection observer) failed, hrc=%Rhrc\n", hrc));

            static VBoxEventType_T const s_aFormatEventTypes[] =
            {
                VBoxEventType_OnClipboardFormatChanged
            };
            hrc = tstRegisterClipboardListener(ptrReflectEventSource, s_aFormatEventTypes, RT_ELEMENTS(s_aFormatEventTypes),
                                               ptrReflectListener);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(reflection writer) failed, hrc=%Rhrc\n", hrc));
            fReflectListenerRegistered = true;
            hrc = tstRegisterClipboardListener(ptrObserverEventSource, s_aFormatEventTypes, RT_ELEMENTS(s_aFormatEventTypes),
                                               ptrObserverListener);
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterListener(reflection observer) failed, hrc=%Rhrc\n", hrc));
            fObserverListenerRegistered = true;

            hrc = ptrReflectSession->WriteFormats(ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("Session WriteFormats(ExcludeReflections) failed, hrc=%Rhrc\n", hrc));

            ComPtr<IEvent> ptrReflectEvent;
            VBoxEventType_T enmReflectEventType = VBoxEventType_Invalid;
            bool fRc = tstClipboardWaitForAnyEvent(ptrReflectEventSource, ptrReflectListener, s_aFormatEventTypes,
                                                   RT_ELEMENTS(s_aFormatEventTypes), 1000 /* cMsTimeout */,
                                                   "ExcludeReflections direct writer", ptrReflectEvent, &enmReflectEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
                RTTESTI_CHECK(tstClipboardCheckFormatChangedEvent(ptrReflectEvent, "ExcludeReflections direct writer format event",
                                                                  idReflectSession, ClipboardSource_Host,
                                                                  "text/plain;charset=utf-8"));

            ComPtr<IEvent> ptrObserverEvent;
            VBoxEventType_T enmObserverEventType = VBoxEventType_Invalid;
            fRc = tstClipboardWaitForAnyEvent(ptrObserverEventSource, ptrObserverListener, s_aFormatEventTypes,
                                              RT_ELEMENTS(s_aFormatEventTypes), 1000 /* cMsTimeout */,
                                              "ExcludeReflections observer", ptrObserverEvent, &enmObserverEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
                RTTESTI_CHECK(tstClipboardCheckFormatChangedEvent(ptrObserverEvent, "ExcludeReflections observer format event",
                                                                  idReflectSession, ClipboardSource_Host,
                                                                  "text/plain;charset=utf-8"));

            hrc = pClipboard->WriteFormats(ComSafeArrayAsInParam(aTextFormats));
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("Anonymous WriteFormats(ExcludeReflections echo) failed, hrc=%Rhrc\n", hrc));

            ComPtr<IEvent> ptrObserverEchoEvent;
            VBoxEventType_T enmObserverEchoEventType = VBoxEventType_Invalid;
            fRc = tstClipboardWaitForAnyEvent(ptrObserverEventSource, ptrObserverListener, s_aFormatEventTypes,
                                              RT_ELEMENTS(s_aFormatEventTypes), 1000 /* cMsTimeout */,
                                              "ExcludeReflections observer echo", ptrObserverEchoEvent, &enmObserverEchoEventType);
            RTTESTI_CHECK(fRc);
            if (fRc)
                RTTESTI_CHECK(tstClipboardCheckFormatChangedEvent(ptrObserverEchoEvent,
                                                                  "ExcludeReflections observer anonymous echo event",
                                                                  VBOX_SHCL_MAIN_CLIENT_NONE, ClipboardSource_Host,
                                                                  "text/plain;charset=utf-8"));
            RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrReflectEventSource, ptrReflectListener, 250 /* cMsTimeout */,
                                                    "ExcludeReflections anonymous echo"));
        } while (0);

        if (fReflectListenerRegistered && ptrReflectEventSource.isNotNull() && ptrReflectListener.isNotNull())
            ptrReflectEventSource->UnregisterListener(ptrReflectListener);
        if (fObserverListenerRegistered && ptrObserverEventSource.isNotNull() && ptrObserverListener.isNotNull())
            ptrObserverEventSource->UnregisterListener(ptrObserverListener);
        ptrReflectListener.setNull();
        ptrObserverListener.setNull();

        tstClipboardCloseSession(ptrReflectSession, "ExcludeReflections session");
        tstClipboardCloseSession(ptrObserverSession, "ExcludeReflections observer session");
        hrc = pClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after ExcludeReflections test failed, hrc=%Rhrc\n", hrc));
    }

    hrc = pClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMSETTER(Mode)(Bidirectional) after sessions failed, hrc=%Rhrc\n", hrc));
    hrc = pClipboard->Reset();
    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset after session API subtest failed, hrc=%Rhrc\n", hrc));
}



/**
 * Tests the public console clipboard API.
 *
 * @param   hTest           Test handle.
 */
static void tstClipboardPublicApi(RTTEST hTest)
{
    RTTestSub(hTest, "Clipboard public API");

    HRESULT hrc = S_OK;
    bool fMachineRegistered = false;
    bool fMachineLocked = false;
    bool fMachinePoweredOn = false;
    bool fListenerRegistered = false;
    ComPtr<IVirtualBoxClient> ptrVirtualBoxClient;
    ComPtr<IVirtualBox> ptrVirtualBox;
    ComPtr<ISession> ptrSession;
    ComPtr<IMachine> ptrMachine;
    ComPtr<IConsole> ptrConsole;
    ComPtr<IClipboard> ptrClipboard;
    ComPtr<IEventSource> ptrEventSource;
    ComPtr<IEventListener> ptrListener;
    ComPtr<IClipboardSession> ptrSurvivingSession;
    ULONG idSurvivingSession = VBOX_SHCL_MAIN_CLIENT_NONE;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    char szFile1[RTPATH_MAX] = "";
    char szDir1[RTPATH_MAX] = "";
    char szDirFile1[RTPATH_MAX] = "";
    bool fFile1Created = false;
    bool fDir1Created = false;
    bool fDirFile1Created = false;
#endif

    do
    {
        /* Create the frontend objects and a throwaway VM used for live console clipboard testing. */
        hrc = ptrVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("createInprocObject(CLSID_VirtualBoxClient) failed, hrc=%Rhrc\n", hrc));
        hrc = ptrVirtualBoxClient->COMGETTER(VirtualBox)(ptrVirtualBox.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(VirtualBox) failed, hrc=%Rhrc\n", hrc));

        ComPtr<IHost> ptrHost;
        hrc = ptrVirtualBox->COMGETTER(Host)(ptrHost.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Host) failed, hrc=%Rhrc\n", hrc));
        PlatformArchitecture_T enmPlatformArch = PlatformArchitecture_x86;
        hrc = ptrHost->COMGETTER(Architecture)(&enmPlatformArch);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Architecture) failed, hrc=%Rhrc\n", hrc));

        RTUUID uuid;
        int vrc = RTUuidCreate(&uuid);
        RTTESTI_CHECK_RC_BREAK(vrc, VINF_SUCCESS);
        char szMachineName[64];
        RTStrPrintf(szMachineName, sizeof(szMachineName), "tstClipboard-%RTuuid", &uuid);

        SafeArray<BSTR> aGroups;
        hrc = ptrVirtualBox->CreateMachine(NULL,                          /* Settings file */
                                           Bstr(szMachineName).raw(),      /* Name */
                                           enmPlatformArch,
                                           ComSafeArrayAsInParam(aGroups), /* Groups */
                                           NULL,                          /* OS type */
                                           NULL,                          /* Flags */
                                           NULL,                          /* Cipher */
                                           NULL,                          /* Password ID */
                                           NULL,                          /* Password */
                                           ptrMachine.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateMachine failed, hrc=%Rhrc\n", hrc));

        /* Verify default clipboard settings before enabling the directions this testcase exercises. */
        ComPtr<IClipboardSettings> ptrClipboardSettings;
        hrc = ptrMachine->COMGETTER(Clipboard)(ptrClipboardSettings.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(IMachine::Clipboard) failed, hrc=%Rhrc\n", hrc));

        ClipboardMode_T enmInitialMode = ClipboardMode_Bidirectional;
        hrc = ptrClipboardSettings->COMGETTER(Mode)(&enmInitialMode);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(IClipboardSettings::Mode) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmInitialMode == ClipboardMode_Disabled);

        BOOL fFileTransfersEnabled = TRUE;
        hrc = ptrClipboardSettings->COMGETTER(FileTransfersEnabled)(&fFileTransfersEnabled);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(IClipboardSettings::FileTransfersEnabled) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!fFileTransfersEnabled);

        hrc = ptrClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMSETTER(Mode) failed, hrc=%Rhrc\n", hrc));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        hrc = ptrClipboardSettings->COMSETTER(FileTransfersEnabled)(TRUE);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMSETTER(FileTransfersEnabled) failed, hrc=%Rhrc\n", hrc));
#endif

        /* Register and launch the VM so the console-scoped clipboard service is available. */
        hrc = ptrVirtualBox->RegisterMachine(ptrMachine);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("RegisterMachine failed, hrc=%Rhrc\n", hrc));
        fMachineRegistered = true;

        hrc = ptrSession.createInprocObject(CLSID_Session);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("createInprocObject(CLSID_Session) failed, hrc=%Rhrc\n", hrc));

        ComPtr<IProgress> ptrLaunchProgress;
        hrc = ptrMachine->LaunchVMProcess(ptrSession, Bstr("headless").raw(),
                                          ComSafeArrayNullInParam(), ptrLaunchProgress.asOutParam());
        if (FAILED(hrc))
        {
            tstLogErrorInfo(hTest, "LaunchVMProcess", ErrorInfo(ptrMachine, COM_IIDOF(IMachine)));
            RTTestSkipped(hTest, "LaunchVMProcess failed, hrc=%Rhrc", hrc);
            break;
        }
        RTTESTI_CHECK_MSG_BREAK(!ptrLaunchProgress.isNull(), ("LaunchVMProcess returned no progress object\n"));
        hrc = ptrLaunchProgress->WaitForCompletion(-1);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WaitForCompletion(LaunchVMProcess) failed, hrc=%Rhrc\n", hrc));
        LONG lrcLaunchResult = S_OK;
        hrc = ptrLaunchProgress->COMGETTER(ResultCode)(&lrcLaunchResult);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(ResultCode) failed, hrc=%Rhrc\n", hrc));
        if (FAILED((HRESULT)lrcLaunchResult))
        {
            tstLogErrorInfo(hTest, "LaunchVMProcess progress", ProgressErrorInfo(ptrLaunchProgress));
            RTTestSkipped(hTest, "LaunchVMProcess result code is %Rhrc", lrcLaunchResult);
            break;
        }
        fMachineLocked = true;
        fMachinePoweredOn = true;

        /* Resolve the live console and clipboard objects under test. */
        hrc = ptrSession->COMGETTER(Console)(ptrConsole.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Console) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrConsole.isNull(), ("COMGETTER(Console) returned NULL\n"));

        ComPtr<IMachine> ptrSessionMachine;
        hrc = ptrSession->COMGETTER(Machine)(ptrSessionMachine.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Machine) failed, hrc=%Rhrc\n", hrc));
        hrc = ptrSessionMachine->COMGETTER(Clipboard)(ptrClipboardSettings.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(IInternalSession::Machine::Clipboard) failed, hrc=%Rhrc\n", hrc));

        hrc = ptrConsole->COMGETTER(Clipboard)(ptrClipboard.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Clipboard) failed, hrc=%Rhrc\n", hrc));

        /* Verify object construction helpers before exercising live clipboard state. */
        tstClipboardPublicObjects(hTest, ptrClipboard);
        RTTestSub(hTest, "Clipboard public API setup");

        /* Verify transfer source-path storage and the auxiliary transfer/event-source getters. */
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        ComPtr<IClipboardTransferManager> ptrTransfers;
        hrc = ptrClipboard->COMGETTER(Transfers)(ptrTransfers.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Transfers) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrTransfers.isNull(), ("COMGETTER(Transfers) returned NULL\n"));

        SafeIfaceArray<IClipboardTransfer> aTransfers;
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(Any initial) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aTransfers.size() == 0);
        aTransfers.setNull();
        hrc = ptrTransfers->GetTransfers((ClipboardTransferDirection_T)999, 0, ComSafeArrayAsOutParam(aTransfers));
        RTTESTI_CHECK_MSG(hrc == E_INVALIDARG, ("GetTransfers(invalid direction) returned hrc=%Rhrc, expected E_INVALIDARG\n", hrc));

        char szTmpDir[RTPATH_MAX];
        vrc = RTPathTemp(szTmpDir, sizeof(szTmpDir));
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTPathTemp failed, vrc=%Rrc\n", vrc));
        char szTmpName[64];
        RTStrPrintf(szTmpName, sizeof(szTmpName), "tstClipboard-%RU64-1.txt", RTTimeNanoTS());
        vrc = RTPathJoin(szFile1, sizeof(szFile1), szTmpDir, szTmpName);
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTPathJoin(%s, %s) failed, vrc=%Rrc\n", szTmpDir, szTmpName, vrc));

        static const char s_szFile1Data[] = "clipboard transfer data one";
        RTFILE hFile = NIL_RTFILE;
        vrc = RTFileOpen(&hFile, szFile1, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTFileOpen(%s) failed, vrc=%Rrc\n", szFile1, vrc));
        fFile1Created = true;
        vrc = RTFileWrite(hFile, s_szFile1Data, sizeof(s_szFile1Data) - 1, NULL /* pcbWritten */);
        RTTESTI_CHECK_MSG(RT_SUCCESS(vrc), ("RTFileWrite(%s) failed, vrc=%Rrc\n", szFile1, vrc));
        RTFileClose(hFile);

        ComPtr<IEventSource> ptrMainTransferEventSource;
        hrc = ptrClipboard->COMGETTER(EventSource)(ptrMainTransferEventSource.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc) && !ptrMainTransferEventSource.isNull(),
                                ("COMGETTER(EventSource for Create) failed, hrc=%Rhrc\n", hrc));
        ComPtr<IEventListener> ptrMainTransferListener;
        static VBoxEventType_T const s_aMainTransferEventTypes[] =
        {
            VBoxEventType_OnClipboardTransfer
        };
        hrc = tstRegisterClipboardListener(ptrMainTransferEventSource, s_aMainTransferEventTypes,
                                           RT_ELEMENTS(s_aMainTransferEventTypes), ptrMainTransferListener);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc) && !ptrMainTransferListener.isNull(),
                                ("RegisterListener(Create) failed, hrc=%Rhrc\n", hrc));

        ComPtr<IClipboardTransfer> ptrTransfer;
        hrc = ptrTransfers->Create(ClipboardTransferDirection_ToGuest, ClipboardSource_Host,
                                   ClipboardAction_Copy, ptrTransfer.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("Create(ToGuest, Host, Copy) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrTransfer.isNull(), ("Create(ToGuest, Host, Copy) returned NULL transfer\n"));

        ULONG idMainTransfer = 0;
        hrc = ptrTransfer->COMGETTER(Id)(&idMainTransfer);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Id Main-created transfer) failed, hrc=%Rhrc\n", hrc));

        ComPtr<IEvent> ptrMainAddedEvent;
        VBoxEventType_T enmMainAddedEventType = VBoxEventType_Invalid;
        bool fMainTransferEvent = tstClipboardWaitForAnyEvent(ptrMainTransferEventSource, ptrMainTransferListener,
                                                              s_aMainTransferEventTypes,
                                                              RT_ELEMENTS(s_aMainTransferEventTypes),
                                                              1000 /* cMsTimeout */, "Main-created transfer added",
                                                              ptrMainAddedEvent, &enmMainAddedEventType);
        RTTESTI_CHECK(fMainTransferEvent);
        if (fMainTransferEvent)
        {
            ComPtr<IClipboardTransfer> ptrAddedTransfer;
            RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrMainAddedEvent, "Main-created transfer added",
                                                         ClipboardTransferState_Added,
                                                         ClipboardTransferDirection_ToGuest,
                                                         ClipboardSource_Host, idMainTransfer, ptrAddedTransfer));
            RTTESTI_CHECK(ptrAddedTransfer == ptrTransfer);
        }

        aTransfers.setNull();
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(Any after Create) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aTransfers.size() == 1);
        if (aTransfers.size() == 1)
            RTTESTI_CHECK(aTransfers[0] == ptrTransfer);
        SafeIfaceArray<IClipboardTransfer> aGuestTransfers;
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_ToGuest, 0,
                                         ComSafeArrayAsOutParam(aGuestTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(ToGuest after Create) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aGuestTransfers.size() == 1);
        SafeIfaceArray<IClipboardTransfer> aHostTransfers;
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_ToHost, 0,
                                         ComSafeArrayAsOutParam(aHostTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(ToHost after Create) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aHostTransfers.size() == 0);

        SafeArray<BSTR> aSourcePaths;
        hrc = ptrTransfer->GetSourcePaths(ComSafeArrayAsOutParam(aSourcePaths));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetSourcePaths initial failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aSourcePaths.size() == 0);

        SafeArray<IN_BSTR> aNewSourcePaths;
        RTTESTI_CHECK(aNewSourcePaths.push_back(Bstr(szFile1).raw()));
        hrc = ptrTransfer->SetSourcePaths(ComSafeArrayAsInParam(aNewSourcePaths));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SetSourcePaths failed, hrc=%Rhrc\n", hrc));
        aSourcePaths.setNull();
        hrc = ptrTransfer->GetSourcePaths(ComSafeArrayAsOutParam(aSourcePaths));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetSourcePaths after set failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aSourcePaths.size() == 1);
        if (aSourcePaths.size() == 1)
            RTTESTI_CHECK(!RTUtf16Cmp(aSourcePaths[0], Bstr(szFile1).raw()));

        hrc = ptrTransfers->Pause(ptrTransfer);
        RTTESTI_CHECK_MSG(hrc == E_NOTIMPL, ("IClipboardTransferManager::Pause returned hrc=%Rhrc, expected E_NOTIMPL\n", hrc));
        hrc = ptrTransfers->Resume(ptrTransfer);
        RTTESTI_CHECK_MSG(hrc == E_NOTIMPL, ("IClipboardTransferManager::Resume returned hrc=%Rhrc, expected E_NOTIMPL\n", hrc));
        hrc = ptrTransfers->Approve(ptrTransfer, 0 /* aFlags */);
        RTTESTI_CHECK_MSG(hrc == E_NOTIMPL, ("IClipboardTransferManager::Approve returned hrc=%Rhrc, expected E_NOTIMPL\n", hrc));
        hrc = ptrTransfers->Deny(ptrTransfer, Bstr("").raw());
        RTTESTI_CHECK_MSG(hrc == E_NOTIMPL, ("IClipboardTransferManager::Deny returned hrc=%Rhrc, expected E_NOTIMPL\n", hrc));
        hrc = ptrTransfers->Respond(ptrTransfer, ClipboardTransferInteraction_Approval, Bstr("").raw(),
                                    ClipboardTransferResponse_Accept, Bstr("").raw(), 0 /* aFlags */);
        RTTESTI_CHECK_MSG(hrc == E_NOTIMPL, ("IClipboardTransferManager::Respond returned hrc=%Rhrc, expected E_NOTIMPL\n", hrc));

        aTransfers.setNull();
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(Any after SourcePaths) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aTransfers.size() == 1);
        if (aTransfers.size() == 1)
            RTTESTI_CHECK(aTransfers[0] == ptrTransfer);
        aGuestTransfers.setNull();
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_ToGuest, 0, ComSafeArrayAsOutParam(aGuestTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(ToGuest after SourcePaths) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aGuestTransfers.size() == 1);
        aHostTransfers.setNull();
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_ToHost, 0, ComSafeArrayAsOutParam(aHostTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(ToHost after SourcePaths) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aHostTransfers.size() == 0);
        RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrMainTransferEventSource, ptrMainTransferListener,
                                                100 /* cMsTimeout */, "Main-created transfer source-path update"));
        if (ptrTransfer.isNotNull())
        {
            SafeIfaceArray<IClipboardTransferFsObjInfo> aRootNodes;
            hrc = ptrTransfer->Roots(ComSafeArrayAsOutParam(aRootNodes));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransfer::Roots failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(aRootNodes.size() == 1);
            if (aRootNodes.size() == 1)
            {
                Bstr bstrPath;
                hrc = aRootNodes[0]->COMGETTER(Path)(bstrPath.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferFsObjInfo::COMGETTER(Path) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrPath).c_str(), RTPathFilename(szFile1)));
            }

            ComPtr<IClipboardTransferFsObjInfo> ptrInvalidNode;
            hrc = ptrTransfer->Query(Bstr("../host-file").raw(), ptrInvalidNode.asOutParam());
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransfer::Query('../host-file') unexpectedly succeeded\n"));
            hrc = ptrTransfer->Query(Bstr("host-file/").raw(), ptrInvalidNode.asOutParam());
            RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                              ("IClipboardTransfer::Query('host-file/') returned hrc=%Rhrc\n", hrc));
            SafeIfaceArray<IClipboardTransferFsObjInfo> aInvalidNodes;
            hrc = ptrTransfer->List(Bstr("/absolute").raw(), ClipboardTransferListFlag_None, ComSafeArrayAsOutParam(aInvalidNodes));
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransfer::List('/absolute') unexpectedly succeeded\n"));

            ComPtr<IClipboardTransferFile> ptrUnsupportedFile;
            hrc = ptrTransfer->OpenFile(Bstr(RTPathFilename(szFile1)).raw(), FileAccessMode_ReadWrite,
                                        FileOpenAction_OpenExisting, FileSharingMode_Read, 0 /* creationMode */,
                                        ptrUnsupportedFile.asOutParam());
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransfer::OpenFile(ReadWrite) unexpectedly succeeded\n"));
            hrc = ptrTransfer->OpenFile(Bstr(RTPathFilename(szFile1)).raw(), FileAccessMode_ReadOnly,
                                        FileOpenAction_CreateOrReplace, FileSharingMode_Read, 0 /* creationMode */,
                                        ptrUnsupportedFile.asOutParam());
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransfer::OpenFile(CreateOrReplace) unexpectedly succeeded\n"));

            ComPtr<IClipboardTransferFile> ptrFile;
            hrc = ptrTransfer->OpenFile(Bstr(RTPathFilename(szFile1)).raw(), FileAccessMode_ReadOnly,
                                        FileOpenAction_OpenExisting, FileSharingMode_Read, 0 /* creationMode */,
                                        ptrFile.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransfer::OpenFile failed, hrc=%Rhrc\n", hrc));
            if (SUCCEEDED(hrc) && ptrFile.isNotNull())
            {
                LONG64 cbInitial = -1;
                hrc = ptrFile->COMGETTER(InitialSize)(&cbInitial);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferFile::COMGETTER(InitialSize) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(cbInitial == (LONG64)sizeof(s_szFile1Data) - 1);
                LONG64 cbSize = -1;
                hrc = ptrFile->QuerySize(&cbSize);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferFile::QuerySize failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(cbSize == (LONG64)sizeof(s_szFile1Data) - 1);
                ComPtr<IFsObjInfo> ptrFileInfo;
                hrc = ptrFile->QueryInfo(ptrFileInfo.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferFile::QueryInfo failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(!ptrFileInfo.isNull());
                SafeArray<BYTE> aWriteData;
                RTTESTI_CHECK(aWriteData.push_back('x'));
                ULONG cbWritten = 0;
                hrc = ptrFile->Write(ComSafeArrayAsInParam(aWriteData), 0 /* timeoutMS */, &cbWritten);
                RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransferFile::Write unexpectedly succeeded\n"));

                SafeArray<BYTE> aOversizedFileData;
                hrc = ptrFile->Read(SHCL_TRANSFER_DEFAULT_MAX_CHUNK_SIZE + 1, 0 /* timeoutMS */,
                                    ComSafeArrayAsOutParam(aOversizedFileData));
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("IClipboardTransferFile::Read(oversized) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aOversizedFileData.size() == 0);

                SafeArray<BYTE> aFileData;
                hrc = ptrFile->Read(sizeof(s_szFile1Data) - 1, 0 /* timeoutMS */, ComSafeArrayAsOutParam(aFileData));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferFile::Read failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aFileData.size() == sizeof(s_szFile1Data) - 1);
                if (aFileData.size() == sizeof(s_szFile1Data) - 1)
                    RTTESTI_CHECK(!memcmp(aFileData.raw(), s_szFile1Data, sizeof(s_szFile1Data) - 1));
                LONG64 offNew = -1;
                hrc = ptrFile->Seek(INT64_MAX, FileSeekOrigin_Current, &offNew);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("IClipboardTransferFile::Seek(overflow) returned hrc=%Rhrc\n", hrc));
                hrc = ptrFile->Close();
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferFile::Close failed, hrc=%Rhrc\n", hrc));
            }

            ComPtr<IClipboardTransferData> ptrTransferData;
            hrc = ptrTransfer->COMGETTER(Data)(ptrTransferData.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("IClipboardTransfer::COMGETTER(Data) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrTransferData.isNull(),
                                    ("IClipboardTransfer::COMGETTER(Data) returned NULL\n"));

            LONG64 cRoots = 0;
            hrc = ptrTransferData->Open(ClipboardTransferDataType_RootList, Bstr("").raw(), Bstr("").raw(),
                                        0 /* aFlags */, &cRoots);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Open(RootList) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(cRoots == 1);

            Bstr bstrRootName;
            ULONG fInfo = 0;
            SafeArray<BYTE> aInfo;
            SafeArray<BYTE> aData;
            hrc = ptrTransferData->Read(ClipboardTransferDataType_RootList, 0 /* aHandle */, 0 /* aSize */,
                                        0 /* aFlags */, bstrRootName.asOutParam(), &fInfo,
                                        ComSafeArrayAsOutParam(aInfo), ComSafeArrayAsOutParam(aData));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Read(RootList) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(fInfo & VBOX_SHCL_INFO_F_FSOBJINFO);
            RTTESTI_CHECK(aInfo.size() == sizeof(SHCLFSOBJINFO));

            LONG64 hInvalid = 0;
            hrc = ptrTransferData->Open(ClipboardTransferDataType_List, Bstr("../parent").raw(), Bstr("").raw(),
                                        0 /* aFlags */, &hInvalid);
            RTTESTI_CHECK_MSG(hrc == E_INVALIDARG, ("IClipboardTransferData::Open(List '../parent') returned hrc=%Rhrc\n", hrc));
            hrc = ptrTransferData->Open(ClipboardTransferDataType_List, Bstr("/absolute").raw(), Bstr("").raw(),
                                        0 /* aFlags */, &hInvalid);
            RTTESTI_CHECK_MSG(hrc == E_INVALIDARG, ("IClipboardTransferData::Open(List '/absolute') returned hrc=%Rhrc\n", hrc));
            hrc = ptrTransferData->Open(ClipboardTransferDataType_Object, Bstr("dir\\file").raw(), Bstr("").raw(),
                                        SHCL_OBJ_CF_ACCESS_READ | SHCL_OBJ_CF_ACCESS_DENYWRITE, &hInvalid);
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransferData::Open(Object 'dir\\file') unexpectedly succeeded\n"));
            hrc = ptrTransferData->Open(ClipboardTransferDataType_Object, Bstr("C:file").raw(), Bstr("").raw(),
                                        SHCL_OBJ_CF_ACCESS_READ | SHCL_OBJ_CF_ACCESS_DENYWRITE, &hInvalid);
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransferData::Open(Object 'C:file') unexpectedly succeeded\n"));

            LONG64 hObj = 0;
            hrc = ptrTransferData->Open(ClipboardTransferDataType_Object, bstrRootName.raw(), Bstr("").raw(),
                                        SHCL_OBJ_CF_ACCESS_READ | SHCL_OBJ_CF_ACCESS_DENYWRITE, &hObj);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Open(Object) failed, hrc=%Rhrc\n", hrc));
            SafeArray<BYTE> aOversizedObjData;
            Bstr bstrOversizedObjName;
            ULONG fOversizedObjInfo = 0;
            SafeArray<BYTE> aOversizedObjInfo;
            hrc = ptrTransferData->Read(ClipboardTransferDataType_Object, hObj,
                                        SHCL_TRANSFER_DEFAULT_MAX_CHUNK_SIZE + 1, 0 /* aFlags */,
                                        bstrOversizedObjName.asOutParam(), &fOversizedObjInfo,
                                        ComSafeArrayAsOutParam(aOversizedObjInfo),
                                        ComSafeArrayAsOutParam(aOversizedObjData));
            RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                              ("IClipboardTransferData::Read(Object oversized) returned hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(aOversizedObjData.size() == 0);
            SafeArray<BYTE> aObjData;
            Bstr bstrObjName;
            ULONG fObjInfo = 0;
            SafeArray<BYTE> aObjInfo;
            hrc = ptrTransferData->Read(ClipboardTransferDataType_Object, hObj, sizeof(s_szFile1Data) - 1,
                                        0 /* aFlags */, bstrObjName.asOutParam(), &fObjInfo,
                                        ComSafeArrayAsOutParam(aObjInfo), ComSafeArrayAsOutParam(aObjData));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Read(Object) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(aObjData.size() == sizeof(s_szFile1Data) - 1);
            if (aObjData.size() == sizeof(s_szFile1Data) - 1)
                RTTESTI_CHECK(!memcmp(aObjData.raw(), s_szFile1Data, sizeof(s_szFile1Data) - 1));
            ULONG cbObjWritten = 0;
            hrc = ptrTransferData->Write(ClipboardTransferDataType_Object, hObj, Bstr("").raw(), 0 /* aInfoFlags */,
                                         ComSafeArrayAsInParam(aObjInfo), ComSafeArrayAsInParam(aObjData),
                                         1 /* aFlags */, &cbObjWritten);
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransferData::Write(Object with flags) unexpectedly succeeded\n"));
            RTTESTI_CHECK(cbObjWritten == 0);
            hrc = ptrTransferData->Close(ClipboardTransferDataType_Object, hObj);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Close(Object) failed, hrc=%Rhrc\n", hrc));
        }

        RTStrPrintf(szTmpName, sizeof(szTmpName), "tstClipboard-%RU64-dir", RTTimeNanoTS());
        vrc = RTPathJoin(szDir1, sizeof(szDir1), szTmpDir, szTmpName);
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTPathJoin(%s, %s) failed, vrc=%Rrc\n", szTmpDir, szTmpName, vrc));
        vrc = RTDirCreate(szDir1, 0700 /* fMode */, 0 /* fCreate */);
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTDirCreate(%s) failed, vrc=%Rrc\n", szDir1, vrc));
        fDir1Created = true;
        vrc = RTPathJoin(szDirFile1, sizeof(szDirFile1), szDir1, "tstClipboard-list-entry.txt");
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTPathJoin(%s, tstClipboard-list-entry.txt) failed, vrc=%Rrc\n",
                                                 szDir1, vrc));
        hFile = NIL_RTFILE;
        vrc = RTFileOpen(&hFile, szDirFile1, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE);
        RTTESTI_CHECK_MSG_BREAK(RT_SUCCESS(vrc), ("RTFileOpen(%s) failed, vrc=%Rrc\n", szDirFile1, vrc));
        fDirFile1Created = true;
        static const char s_szDirFile1Data[] = "clipboard transfer list data";
        vrc = RTFileWrite(hFile, s_szDirFile1Data, sizeof(s_szDirFile1Data) - 1, NULL /* pcbWritten */);
        RTTESTI_CHECK_MSG(RT_SUCCESS(vrc), ("RTFileWrite(%s) failed, vrc=%Rrc\n", szDirFile1, vrc));
        RTFileClose(hFile);

        SafeArray<IN_BSTR> aDirSourcePaths;
        RTTESTI_CHECK(aDirSourcePaths.push_back(Bstr(szDir1).raw()));
        hrc = ptrTransfer->SetSourcePaths(ComSafeArrayAsInParam(aDirSourcePaths));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("SetSourcePaths directory failed, hrc=%Rhrc\n", hrc));
        aSourcePaths.setNull();
        hrc = ptrTransfer->GetSourcePaths(ComSafeArrayAsOutParam(aSourcePaths));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetSourcePaths directory failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aSourcePaths.size() == 1);
        if (aSourcePaths.size() == 1)
            RTTESTI_CHECK(!RTUtf16Cmp(aSourcePaths[0], Bstr(szDir1).raw()));
        aTransfers.setNull();
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(Any after directory SourcePaths) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aTransfers.size() == 1);
        if (aTransfers.size() == 1)
            RTTESTI_CHECK(aTransfers[0] == ptrTransfer);
        RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrMainTransferEventSource, ptrMainTransferListener,
                                                100 /* cMsTimeout */, "Main-created transfer directory update"));
        if (ptrTransfer.isNotNull())
        {
            SafeIfaceArray<IClipboardTransferFsObjInfo> aDirNodes;
            hrc = ptrTransfer->List(Bstr("").raw(), ClipboardTransferListFlag_None, ComSafeArrayAsOutParam(aDirNodes));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransfer::List(recursive) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(aDirNodes.size() == 2);
            SafeIfaceArray<IClipboardTransferFsObjInfo> aDirRootOnly;
            hrc = ptrTransfer->List(Bstr("").raw(), ClipboardTransferListFlag_NoRecursion, ComSafeArrayAsOutParam(aDirRootOnly));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransfer::List(NoRecursion) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(aDirRootOnly.size() == 1);

            ComPtr<IClipboardTransferDirectory> ptrInvalidDirectory;
            hrc = ptrTransfer->OpenDirectory(Bstr("").raw(), ClipboardTransferListFlag_NoRecursion,
                                             ptrInvalidDirectory.asOutParam());
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransfer::OpenDirectory(empty path) unexpectedly succeeded\n"));

            ComPtr<IClipboardTransferDirectory> ptrDirectory;
            hrc = ptrTransfer->OpenDirectory(Bstr(RTPathFilename(szDir1)).raw(), ClipboardTransferListFlag_NoRecursion,
                                             ptrDirectory.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransfer::OpenDirectory failed, hrc=%Rhrc\n", hrc));
            if (SUCCEEDED(hrc) && ptrDirectory.isNotNull())
            {
                SafeIfaceArray<IClipboardTransferFsObjInfo> aChildren;
                hrc = ptrDirectory->ListEx(16, ClipboardTransferListFlag_NoRecursion, ComSafeArrayAsOutParam(aChildren));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferDirectory::ListEx failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aChildren.size() == 1);
                hrc = ptrDirectory->Rewind();
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferDirectory::Rewind failed, hrc=%Rhrc\n", hrc));
                SafeIfaceArray<IClipboardTransferFsObjInfo> aRootAndChildren;
                hrc = ptrDirectory->ListEx(16,
                                           ClipboardTransferListFlag_NoRecursion
                                         | ClipboardTransferListFlag_IncludeRoot,
                                           ComSafeArrayAsOutParam(aRootAndChildren));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("IClipboardTransferDirectory::ListEx(IncludeRoot, NoRecursion) failed, hrc=%Rhrc\n",
                                   hrc));
                RTTESTI_CHECK(aRootAndChildren.size() == 2);
                hrc = ptrDirectory->Rewind();
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("IClipboardTransferDirectory::Rewind after IncludeRoot failed, hrc=%Rhrc\n", hrc));
                ComPtr<IFsObjInfo> ptrChild;
                hrc = ptrDirectory->Read(ptrChild.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferDirectory::Read after rewind failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(!ptrChild.isNull());
                hrc = ptrDirectory->Close();
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferDirectory::Close failed, hrc=%Rhrc\n", hrc));
            }

            ComPtr<IClipboardTransferData> ptrTransferData;
            hrc = ptrTransfer->COMGETTER(Data)(ptrTransferData.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc),
                                    ("IClipboardTransfer::COMGETTER(Data directory) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrTransferData.isNull(),
                                    ("IClipboardTransfer::COMGETTER(Data directory) returned NULL\n"));

            LONG64 cRoots = 0;
            hrc = ptrTransferData->Open(ClipboardTransferDataType_RootList, Bstr("").raw(), Bstr("").raw(),
                                        0 /* aFlags */, &cRoots);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Open(directory RootList) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(cRoots == 1);

            Bstr bstrDirRootName;
            ULONG fDirRootInfo = 0;
            SafeArray<BYTE> aDirRootInfo;
            SafeArray<BYTE> aDirRootData;
            hrc = ptrTransferData->Read(ClipboardTransferDataType_RootList, 0 /* aHandle */, 0 /* aSize */,
                                        0 /* aFlags */, bstrDirRootName.asOutParam(), &fDirRootInfo,
                                        ComSafeArrayAsOutParam(aDirRootInfo), ComSafeArrayAsOutParam(aDirRootData));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Read(directory RootList) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(fDirRootInfo & VBOX_SHCL_INFO_F_FSOBJINFO);
            RTTESTI_CHECK(aDirRootInfo.size() == sizeof(SHCLFSOBJINFO));

            LONG64 hList = 0;
            hrc = ptrTransferData->Open(ClipboardTransferDataType_List, bstrDirRootName.raw(), Bstr("").raw(),
                                        0 /* aFlags */, &hList);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Open(List) failed, hrc=%Rhrc\n", hrc));

            Bstr bstrListName;
            ULONG fListInfo = 0;
            SafeArray<BYTE> aListInfo;
            SafeArray<BYTE> aListData;
            hrc = ptrTransferData->Read(ClipboardTransferDataType_List, hList, 0 /* aSize */, 0 /* aFlags */,
                                        bstrListName.asOutParam(), &fListInfo,
                                        ComSafeArrayAsOutParam(aListInfo), ComSafeArrayAsOutParam(aListData));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Read(List) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrListName).c_str(), RTPathFilename(szDirFile1)));
            RTTESTI_CHECK(fListInfo & VBOX_SHCL_INFO_F_FSOBJINFO);
            RTTESTI_CHECK(aListInfo.size() == sizeof(SHCLFSOBJINFO));

            ULONG cbListWritten = 0;
            hrc = ptrTransferData->Write(ClipboardTransferDataType_List, hList, Bstr("invalid-none").raw(),
                                         VBOX_SHCL_INFO_F_NONE, ComSafeArrayAsInParam(aListInfo),
                                         ComSafeArrayAsInParam(aListData), 0 /* aFlags */, &cbListWritten);
            RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                              ("IClipboardTransferData::Write(List mismatched NONE info) returned hrc=%Rhrc\n", hrc));
            SafeArray<BYTE> aEmptyListInfo;
            hrc = ptrTransferData->Write(ClipboardTransferDataType_List, hList, Bstr("invalid-fs-info").raw(),
                                         VBOX_SHCL_INFO_F_FSOBJINFO, ComSafeArrayAsInParam(aEmptyListInfo),
                                         ComSafeArrayAsInParam(aListData), 0 /* aFlags */, &cbListWritten);
            RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                              ("IClipboardTransferData::Write(List missing FS info) returned hrc=%Rhrc\n", hrc));
            hrc = ptrTransferData->Write(ClipboardTransferDataType_List, hList, Bstr("new-entry").raw(),
                                         VBOX_SHCL_INFO_F_FSOBJINFO, ComSafeArrayAsInParam(aListInfo),
                                         ComSafeArrayAsInParam(aListData), 0 /* aFlags */, &cbListWritten);
            RTTESTI_CHECK_MSG(FAILED(hrc), ("IClipboardTransferData::Write(List) unexpectedly succeeded\n"));
            RTTESTI_CHECK(cbListWritten == 0);

            hrc = ptrTransferData->Close(ClipboardTransferDataType_List, hList);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferData::Close(List) failed, hrc=%Rhrc\n", hrc));
        }

        hrc = ptrTransfers->Remove(ptrTransfer);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IClipboardTransferManager::Remove(source-path transfer) failed, hrc=%Rhrc\n", hrc));
        ComPtr<IEvent> ptrMainRemovedEvent;
        VBoxEventType_T enmMainRemovedEventType = VBoxEventType_Invalid;
        fMainTransferEvent = tstClipboardWaitForAnyEvent(ptrMainTransferEventSource, ptrMainTransferListener,
                                                         s_aMainTransferEventTypes,
                                                         RT_ELEMENTS(s_aMainTransferEventTypes),
                                                         1000 /* cMsTimeout */, "Main-created transfer removed",
                                                         ptrMainRemovedEvent, &enmMainRemovedEventType);
        RTTESTI_CHECK(fMainTransferEvent);
        if (fMainTransferEvent)
        {
            ComPtr<IClipboardTransfer> ptrRemovedTransfer;
            RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrMainRemovedEvent, "Main-created transfer removed",
                                                         ClipboardTransferState_Removed,
                                                         ClipboardTransferDirection_ToGuest,
                                                         ClipboardSource_Host, idMainTransfer, ptrRemovedTransfer));
            RTTESTI_CHECK(ptrRemovedTransfer == ptrTransfer);
        }
        aTransfers.setNull();
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(Any after source-path transfer remove) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aTransfers.size() == 0);
        hrc = ptrTransfers->Remove(ptrTransfer);
        RTTESTI_CHECK_MSG(hrc == VBOX_E_OBJECT_NOT_FOUND,
                          ("Repeated IClipboardTransferManager::Remove returned hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrMainTransferEventSource, ptrMainTransferListener,
                                                100 /* cMsTimeout */, "stale Main-created transfer remove"));
        hrc = ptrMainTransferEventSource->UnregisterListener(ptrMainTransferListener);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("UnregisterListener(Main transfer) failed, hrc=%Rhrc\n", hrc));
#endif

        hrc = ptrClipboard->COMGETTER(EventSource)(ptrEventSource.asOutParam());
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(EventSource) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrEventSource.isNull());

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        RTTestSub(hTest, "Clipboard service transfer lifecycle");
        ComPtr<IEventListener> ptrTransferListener;
        static VBoxEventType_T const s_aTransferEventTypes[] =
        {
            VBoxEventType_OnClipboardTransfer
        };
        hrc = tstRegisterClipboardListener(ptrEventSource, s_aTransferEventTypes, RT_ELEMENTS(s_aTransferEventTypes),
                                           ptrTransferListener);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("RegisterListener(service transfer) failed, hrc=%Rhrc\n", hrc));
        if (SUCCEEDED(hrc))
        {
            ComPtr<IInternalClipboardControl> ptrInternalClipboardControl(ptrClipboard);
            RTTESTI_CHECK_MSG(!ptrInternalClipboardControl.isNull(),
                              ("Query IInternalClipboardControl(service transfer) returned NULL\n"));
            if (ptrInternalClipboardControl.isNotNull())
            {
                hrc = ptrInternalClipboardControl->SetTransferStatus(0 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus accepted a zero service session, hrc=%Rhrc\n", hrc));
                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     0xfeed /* aStatus */, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus accepted an invalid status, hrc=%Rhrc\n", hrc));
                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 76 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VERR_ACCESS_DENIED);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus accepted a non-error status with a failing result, hrc=%Rhrc\n",
                                   hrc));
                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 76 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_ERROR, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus accepted ERROR with a successful result, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "status/result-inconsistent service transfer"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 76 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(REQUESTED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrRequestedEvent;
                VBoxEventType_T enmRequestedEventType = VBoxEventType_Invalid;
                bool fRequestedRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                                RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                                "requested service transfer", ptrRequestedEvent,
                                                                &enmRequestedEventType);
                RTTESTI_CHECK(fRequestedRc);
                if (fRequestedRc)
                {
                    ComPtr<IClipboardTransfer> ptrRequestedTransfer;
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrRequestedEvent, "requested service transfer",
                                                                 ClipboardTransferState_Added,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 76, ptrRequestedTransfer));
                }

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 76 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus accepted REQUESTED to COMPLETED, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "requested-to-completed service transfer"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 76 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(requested ERROR) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrRequestedErrorEvent;
                VBoxEventType_T enmRequestedErrorEventType = VBoxEventType_Invalid;
                fRequestedRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                           RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                           "requested service transfer error", ptrRequestedErrorEvent,
                                                           &enmRequestedErrorEventType);
                RTTESTI_CHECK(fRequestedRc);

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(INITIALIZED) failed, hrc=%Rhrc\n", hrc));

                ComPtr<IEvent> ptrAddedEvent;
                VBoxEventType_T enmAddedEventType = VBoxEventType_Invalid;
                bool fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                       RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                       "service transfer added", ptrAddedEvent, &enmAddedEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrStatusTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrAddedEvent, "service transfer added",
                                                                 ClipboardTransferState_Added,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 77, ptrStatusTransfer));

                ComPtr<IProgress> ptrStatusProgress;
                if (ptrStatusTransfer.isNotNull())
                {
                    hrc = ptrStatusTransfer->COMGETTER(Progress)(ptrStatusProgress.asOutParam());
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IClipboardTransfer::COMGETTER(Progress) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(!ptrStatusProgress.isNull());
                }
                if (ptrStatusProgress.isNotNull())
                {
                    BOOL fCompleted = TRUE;
                    hrc = ptrStatusProgress->COMGETTER(Completed)(&fCompleted);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IProgress::COMGETTER(Completed) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(!fCompleted);
                }

                SafeIfaceArray<IClipboardTransfer> aStatusTransfers;
                hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_ToHost, 0,
                                                 ComSafeArrayAsOutParam(aStatusTransfers));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(ToHost service) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aStatusTransfers.size() == 1);

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(backward REQUESTED) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "backward service transfer status"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Host,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(source-mismatched STARTED) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "source-mismatched service transfer status"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(STARTED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrStartedEvent;
                VBoxEventType_T enmStartedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "service transfer started", ptrStartedEvent, &enmStartedEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrStartedTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrStartedEvent, "service transfer started",
                                                                 ClipboardTransferState_InProgress,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 77, ptrStartedTransfer));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(backward INITIALIZED) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "backward initialized service transfer status"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(duplicate STARTED) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "duplicate service transfer status"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(COMPLETED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrCompletedEvent;
                VBoxEventType_T enmCompletedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "service transfer completed", ptrCompletedEvent, &enmCompletedEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrCompletedTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrCompletedEvent, "service transfer completed",
                                                                 ClipboardTransferState_Completed,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 77, ptrCompletedTransfer));
                if (ptrStatusProgress.isNotNull())
                {
                    BOOL fCompleted = FALSE;
                    hrc = ptrStatusProgress->COMGETTER(Completed)(&fCompleted);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IProgress::COMGETTER(Completed after completion) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(fCompleted);
                    LONG hrcResult = E_FAIL;
                    hrc = ptrStatusProgress->COMGETTER(ResultCode)(&hrcResult);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IProgress::COMGETTER(ResultCode after completion) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(hrcResult == S_OK);
                }
                aStatusTransfers.setNull();
                hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0,
                                                 ComSafeArrayAsOutParam(aStatusTransfers));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers after service completion failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aStatusTransfers.size() == 0);

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* stale generation */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(stale STARTED) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "stale service transfer generation"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     2 /* aGeneration */, ClipboardSource_Host,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(second INITIALIZED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrSecondAddedEvent;
                VBoxEventType_T enmSecondAddedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "second service transfer added", ptrSecondAddedEvent,
                                                  &enmSecondAddedEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrFailedTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrSecondAddedEvent, "second service transfer added",
                                                                 ClipboardTransferState_Added,
                                                                 ClipboardTransferDirection_ToGuest,
                                                                 ClipboardSource_Host, 77, ptrFailedTransfer));
                ComPtr<IProgress> ptrFailedProgress;
                if (ptrFailedTransfer.isNotNull())
                {
                    hrc = ptrFailedTransfer->COMGETTER(Progress)(ptrFailedProgress.asOutParam());
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IClipboardTransfer::COMGETTER(Progress failed transfer) failed, hrc=%Rhrc\n", hrc));
                }

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     2 /* aGeneration */, ClipboardSource_Host,
                                                                     SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("SetTransferStatus(ERROR) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrFailedEvent;
                VBoxEventType_T enmFailedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "service transfer failed", ptrFailedEvent, &enmFailedEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrFailedEventTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrFailedEvent, "service transfer failed",
                                                                 ClipboardTransferState_Failed,
                                                                 ClipboardTransferDirection_ToGuest,
                                                                 ClipboardSource_Host, 77, ptrFailedEventTransfer,
                                                                 ClipboardError_AccessDenied));
                if (ptrFailedProgress.isNotNull())
                {
                    BOOL fCompleted = FALSE;
                    hrc = ptrFailedProgress->COMGETTER(Completed)(&fCompleted);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IProgress::COMGETTER(Completed after failure) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(fCompleted);
                    LONG hrcResult = S_OK;
                    hrc = ptrFailedProgress->COMGETTER(ResultCode)(&hrcResult);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IProgress::COMGETTER(ResultCode after failure) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(hrcResult == (LONG)VBOX_E_SHCL_ACCESS_DENIED);
                    ComPtr<IVirtualBoxErrorInfo> ptrProgressErrorInfo;
                    hrc = ptrFailedProgress->COMGETTER(ErrorInfo)(ptrProgressErrorInfo.asOutParam());
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("IProgress::COMGETTER(ErrorInfo after failure) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(!ptrProgressErrorInfo.isNull());
                    if (ptrProgressErrorInfo.isNotNull())
                    {
                        LONG hrcErrorInfo = S_OK;
                        hrc = ptrProgressErrorInfo->COMGETTER(ResultCode)(&hrcErrorInfo);
                        RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                          ("IVirtualBoxErrorInfo::COMGETTER(ResultCode) failed, hrc=%Rhrc\n", hrc));
                        RTTESTI_CHECK(hrcErrorInfo == (LONG)VBOX_E_SHCL_ACCESS_DENIED);
                        LONG vrcErrorInfo = VINF_SUCCESS;
                        hrc = ptrProgressErrorInfo->COMGETTER(ResultDetail)(&vrcErrorInfo);
                        RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                          ("IVirtualBoxErrorInfo::COMGETTER(ResultDetail) failed, hrc=%Rhrc\n", hrc));
                        RTTESTI_CHECK(vrcErrorInfo == VERR_ACCESS_DENIED);
                    }
                }

                /* A replacement service client owns a new, independently numbered generation sequence. */
                hrc = ptrInternalClipboardControl->SetTransferStatus(10 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Host,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(new-session INITIALIZED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrNewSessionAddedEvent;
                VBoxEventType_T enmNewSessionAddedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "new-session service transfer added", ptrNewSessionAddedEvent,
                                                  &enmNewSessionAddedEventType);
                RTTESTI_CHECK(fRc);
                if (fRc)
                {
                    ComPtr<IClipboardTransfer> ptrNewSessionTransfer;
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrNewSessionAddedEvent,
                                                                 "new-session service transfer added",
                                                                 ClipboardTransferState_Added,
                                                                 ClipboardTransferDirection_ToGuest,
                                                                 ClipboardSource_Host, 77, ptrNewSessionTransfer));
                }

                hrc = ptrInternalClipboardControl->SetTransferStatus(10 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Host,
                                                                     SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(new-session COMPLETED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrNewSessionCompletedEvent;
                VBoxEventType_T enmNewSessionCompletedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "new-session service transfer completed",
                                                  ptrNewSessionCompletedEvent, &enmNewSessionCompletedEventType);
                RTTESTI_CHECK(fRc);
                if (fRc)
                {
                    ComPtr<IClipboardTransfer> ptrNewSessionCompletedTransfer;
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrNewSessionCompletedEvent,
                                                                 "new-session service transfer completed",
                                                                 ClipboardTransferState_Completed,
                                                                 ClipboardTransferDirection_ToGuest,
                                                                 ClipboardSource_Host, 77,
                                                                 ptrNewSessionCompletedTransfer));
                }
                hrc = ptrInternalClipboardControl->SetTransferStatus(10 /* aServiceSessionId */, 77 /* aTransferId */,
                                                                     1 /* stale generation */, ClipboardSource_Host,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(new-session stale STARTED) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "new-session stale transfer generation"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     1 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(unknown COMPLETED) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "unknown terminal service transfer"));
                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     1 /* stale generation */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(STARTED after unknown terminal) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "status after unknown terminal generation"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     2 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(pre-reset INITIALIZED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrPreResetEvent;
                VBoxEventType_T enmPreResetEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "pre-reset service transfer added", ptrPreResetEvent,
                                                  &enmPreResetEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrPreResetTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrPreResetEvent, "pre-reset service transfer added",
                                                                 ClipboardTransferState_Added,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 78, ptrPreResetTransfer));

                hrc = ptrTransfers->Reset();
                RTTESTI_CHECK_MSG(hrc == VBOX_E_OBJECT_IN_USE,
                                  ("IClipboardTransferManager::Reset with an active service transfer returned hrc=%Rhrc\n",
                                   hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "rejected service transfer reset"));
                aStatusTransfers.setNull();
                hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0,
                                                 ComSafeArrayAsOutParam(aStatusTransfers));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers after rejected reset failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aStatusTransfers.size() == 1);

                if (ptrPreResetTransfer.isNotNull())
                {
                    hrc = ptrTransfers->Remove(ptrPreResetTransfer);
                    RTTESTI_CHECK_MSG(hrc == VBOX_E_OBJECT_IN_USE,
                                      ("IClipboardTransferManager::Remove(active service transfer) returned hrc=%Rhrc\n", hrc));
                }
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "rejected service transfer remove"));
                aStatusTransfers.setNull();
                hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0,
                                                 ComSafeArrayAsOutParam(aStatusTransfers));
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers after rejected remove failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(aStatusTransfers.size() == 1);

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     2 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_CANCELED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus accepted CANCELED with a successful result, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "status/result-inconsistent cancellation"));
                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     2 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(pre-reset CANCELED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrResetEvent;
                VBoxEventType_T enmResetEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "service transfer canceled", ptrResetEvent, &enmResetEventType);
                RTTESTI_CHECK(fRc);
                if (fRc)
                {
                    ComPtr<IClipboardTransfer> ptrResetTransfer;
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrResetEvent, "service transfer canceled",
                                                                 ClipboardTransferState_Canceled,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 78, ptrResetTransfer));
                }

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     2 /* reset generation */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(hrc == E_INVALIDARG,
                                  ("SetTransferStatus(STARTED after terminal status) returned hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                        "status after terminal service transfer"));

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     3 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(next-generation INITIALIZED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrPostResetEvent;
                VBoxEventType_T enmPostResetEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "next-generation service transfer added", ptrPostResetEvent,
                                                  &enmPostResetEventType);
                RTTESTI_CHECK(fRc);
                ComPtr<IClipboardTransfer> ptrPostResetTransfer;
                if (fRc)
                    RTTESTI_CHECK(tstClipboardCheckTransferEvent(ptrPostResetEvent, "next-generation service transfer added",
                                                                 ClipboardTransferState_Added,
                                                                 ClipboardTransferDirection_ToHost,
                                                                 ClipboardSource_Guest, 78, ptrPostResetTransfer));

                if (ptrPostResetTransfer.isNotNull())
                {
                    hrc = ptrTransfers->Cancel(ptrPostResetTransfer);
                    RTTESTI_CHECK_MSG(FAILED(hrc),
                                      ("IClipboardTransferManager::Cancel(synthetic service transfer) unexpectedly succeeded\n"));
                    RTTESTI_CHECK(tstClipboardExpectNoEvent(ptrEventSource, ptrTransferListener, 100 /* cMsTimeout */,
                                                            "failed service transfer cancel"));
                    aStatusTransfers.setNull();
                    hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0,
                                                     ComSafeArrayAsOutParam(aStatusTransfers));
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                      ("GetTransfers after service cancel failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK(aStatusTransfers.size() == 1);
                }

                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     3 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(STARTED after failed service cancel) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrPostResetStartedEvent;
                VBoxEventType_T enmPostResetStartedEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "service transfer started after failed cancel",
                                                  ptrPostResetStartedEvent, &enmPostResetStartedEventType);
                RTTESTI_CHECK(fRc);
                hrc = ptrInternalClipboardControl->SetTransferStatus(9 /* aServiceSessionId */, 78 /* aTransferId */,
                                                                     3 /* aGeneration */, ClipboardSource_Guest,
                                                                     SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc),
                                  ("SetTransferStatus(next-generation CANCELED) failed, hrc=%Rhrc\n", hrc));
                ComPtr<IEvent> ptrPostResetCanceledEvent;
                VBoxEventType_T enmPostResetCanceledEventType = VBoxEventType_Invalid;
                fRc = tstClipboardWaitForAnyEvent(ptrEventSource, ptrTransferListener, s_aTransferEventTypes,
                                                  RT_ELEMENTS(s_aTransferEventTypes), 1000 /* cMsTimeout */,
                                                  "next-generation service transfer canceled",
                                                  ptrPostResetCanceledEvent, &enmPostResetCanceledEventType);
                RTTESTI_CHECK(fRc);
            }

            hrc = ptrEventSource->UnregisterListener(ptrTransferListener);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("UnregisterListener(service transfer) failed, hrc=%Rhrc\n", hrc));
        }
#endif

        tstClipboardPublicSessionApi(hTest, ptrClipboard, ptrClipboardSettings, ptrEventSource);
        RTTestSub(hTest, "Clipboard public API operations");

        /* Register a broad passive listener for mode changes and explicit host-origin writes. */
        hrc = ptrEventSource->CreateListener(ptrListener.asOutParam());
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("CreateListener failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrListener.isNull());
        SafeArray<VBoxEventType_T> aEventTypes;
        aEventTypes.push_back(VBoxEventType_OnClipboardModeChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardSourceChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardFormatChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardDataChanged);
        aEventTypes.push_back(VBoxEventType_OnClipboardDataRequested);
        hrc = ptrEventSource->RegisterListener(ptrListener, ComSafeArrayAsInParam(aEventTypes), FALSE /* aActive */);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("RegisterListener failed, hrc=%Rhrc\n", hrc));
        fListenerRegistered = SUCCEEDED(hrc);

        /* Check mode-change events before any data writes alter clipboard ownership. */
        hrc = ptrClipboardSettings->COMSETTER(Mode)(ClipboardMode_HostToGuest);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMSETTER(Mode) live change failed, hrc=%Rhrc\n", hrc));
        if (SUCCEEDED(hrc))
        {
            ComPtr<IEvent> ptrEvent;
            hrc = ptrEventSource->GetEvent(ptrListener, 1000 /* aTimeout */, ptrEvent.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetEvent(mode) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(!ptrEvent.isNull(), ("GetEvent(mode) returned no event\n"));
            if (ptrEvent.isNotNull())
            {
                VBoxEventType_T enmType = VBoxEventType_Invalid;
                hrc = ptrEvent->COMGETTER(Type)(&enmType);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Type)(mode) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK_MSG(enmType == VBoxEventType_OnClipboardModeChanged,
                                  ("GetEvent(mode) returned type %d, expected %d\n",
                                   enmType, VBoxEventType_OnClipboardModeChanged));

                ComPtr<IClipboardModeChangedEvent> ptrModeEvent = ptrEvent;
                RTTESTI_CHECK(!ptrModeEvent.isNull());
                if (ptrModeEvent.isNotNull())
                {
                    ClipboardMode_T enmMode = ClipboardMode_Disabled;
                    hrc = ptrModeEvent->COMGETTER(ClipboardMode)(&enmMode);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(ClipboardMode) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK_MSG(enmMode == ClipboardMode_HostToGuest,
                                      ("GetEvent(mode) returned mode %d, expected %d\n",
                                       enmMode, ClipboardMode_HostToGuest));
                }
            }
        }

        /* Verify the initial live clipboard has no formats, data, or queued data events. */
        SafeIfaceArray<IClipboardFormat> aReadFormats;
        hrc = ptrClipboard->ReadFormats(ComSafeArrayAsOutParam(aReadFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("ReadFormats failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aReadFormats.size() == 0);

        ComPtr<IClipboardFormat> ptrTextFormat;
        hrc = tstCreateFormat(ptrClipboard, "text/plain;charset=utf-8", ptrTextFormat);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateFormat(text) failed, hrc=%Rhrc\n", hrc));

        BOOL fAvailable = TRUE;
        hrc = ptrClipboard->IsFormatAvailable(ClipboardSource_Host, ptrTextFormat, &fAvailable);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IsFormatAvailable failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!fAvailable);

        SafeIfaceArray<IClipboardFormat> aSupportedFormats;
        hrc = ptrClipboard->GetSupportedFormats(ClipboardSource_Host, ComSafeArrayAsOutParam(aSupportedFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetSupportedFormats failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aSupportedFormats.size() == 0);

        ComPtr<IClipboardItem> ptrReadItem;
        hrc = ptrClipboard->ReadData(ClipboardAction_Copy, ptrReadItem.asOutParam());
        RTTESTI_CHECK_MSG(FAILED(hrc), ("ReadData without available formats unexpectedly succeeded\n"));

        ComPtr<IEvent> ptrUnexpectedEvent;
        hrc = ptrEventSource->GetEvent(ptrListener, 0 /* aTimeout */, ptrUnexpectedEvent.asOutParam());
        RTTESTI_CHECK_MSG(   hrc == VBOX_E_OBJECT_NOT_FOUND
                          || ptrUnexpectedEvent.isNull(),
                          ("Unexpected clipboard event before explicit API write, hrc=%Rhrc\n", hrc));

        /* Exercise validation failures for empty payloads and unsupported MIME types. */
        std::vector<BYTE> abBuffer;
        ComPtr<IClipboardItem> ptrEmptyItem;
        hrc = tstCreateItem(ptrClipboard, ClipboardSource_Host, ptrTextFormat, abBuffer, ptrEmptyItem);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateItem(empty) failed, hrc=%Rhrc\n", hrc));
        ComPtr<IClipboardItem> ptrWrittenItem;
        hrc = ptrClipboard->WriteData(ClipboardAction_Copy, ptrEmptyItem, ptrWrittenItem.asOutParam());
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_NO_DATA, ("WriteData with empty buffer returned hrc=%Rhrc\n", hrc));

        ComPtr<IClipboardFormat> ptrUnsupportedFormat;
        hrc = tstCreateFormat(ptrClipboard, "application/x-unsupported", ptrUnsupportedFormat);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateFormat(unsupported) failed, hrc=%Rhrc\n", hrc));
        abBuffer.push_back('x');
        ComPtr<IClipboardItem> ptrUnsupportedItem;
        hrc = tstCreateItem(ptrClipboard, ClipboardSource_Host, ptrUnsupportedFormat, abBuffer, ptrUnsupportedItem);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateItem(unsupported) failed, hrc=%Rhrc\n", hrc));
        ptrWrittenItem.setNull();
        hrc = ptrClipboard->WriteData(ClipboardAction_Copy, ptrUnsupportedItem, ptrWrittenItem.asOutParam());
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                          ("WriteData with unsupported MIME type returned hrc=%Rhrc\n", hrc));

        /* Write host-origin data and verify the expected source/format/data events. */
        static const char s_szRoundTripText[] = "tstClipboard host to guest round-trip data";
        std::vector<BYTE> abRoundTripBuffer(sizeof(s_szRoundTripText));
        memcpy(&abRoundTripBuffer[0], s_szRoundTripText, sizeof(s_szRoundTripText));

        ComPtr<IClipboardItem> ptrTextItem;
        hrc = tstCreateItem(ptrClipboard, ClipboardSource_Host, ptrTextFormat, abRoundTripBuffer, ptrTextItem);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateItem(text) failed, hrc=%Rhrc\n", hrc));
        ptrWrittenItem.setNull();
        hrc = ptrClipboard->WriteData(ClipboardAction_Copy, ptrTextItem, ptrWrittenItem.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WriteData failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrWrittenItem.isNull());

        static VBoxEventType_T const s_aExpectedEvents[] =
        {
            VBoxEventType_OnClipboardSourceChanged,
            VBoxEventType_OnClipboardFormatChanged,
            VBoxEventType_OnClipboardDataChanged
        };
        for (size_t i = 0; i < RT_ELEMENTS(s_aExpectedEvents); i++)
        {
            ComPtr<IEvent> ptrEvent;
            hrc = ptrEventSource->GetEvent(ptrListener, 1000 /* aTimeout */, ptrEvent.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("GetEvent[%zu] failed, hrc=%Rhrc\n", i, hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrEvent.isNull(), ("GetEvent[%zu] returned no event\n", i));

            VBoxEventType_T enmType = VBoxEventType_Invalid;
            hrc = ptrEvent->COMGETTER(Type)(&enmType);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Type)[%zu] failed, hrc=%Rhrc\n", i, hrc));
            RTTESTI_CHECK_MSG(enmType == s_aExpectedEvents[i],
                              ("GetEvent[%zu] returned type %d, expected %d\n", i, enmType, s_aExpectedEvents[i]));
            RTTESTI_CHECK_MSG(enmType != VBoxEventType_OnClipboardDataRequested,
                              ("Explicit API write unexpectedly emitted OnClipboardDataRequested\n"));
            if (enmType == VBoxEventType_OnClipboardFormatChanged)
            {
                ComPtr<IClipboardFormatChangedEvent> ptrFormatEvent = ptrEvent;
                RTTESTI_CHECK(!ptrFormatEvent.isNull());
                if (ptrFormatEvent.isNotNull())
                {
                    RTTESTI_CHECK(tstClipboardCheckEventMetadata(ptrFormatEvent, "public API format event",
                                                                VBOX_SHCL_MAIN_CLIENT_NONE));

                    ClipboardSource_T enmSource = ClipboardSource_Custom;
                    hrc = ptrFormatEvent->COMGETTER(ClipboardSource)(&enmSource);
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(ClipboardSource)(format event) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK_MSG(enmSource == ClipboardSource_Host,
                                      ("Format event source %d, expected %d\n", enmSource, ClipboardSource_Host));

                    SafeIfaceArray<IClipboardFormat> aEventFormats;
                    hrc = ptrFormatEvent->COMGETTER(Formats)(ComSafeArrayAsOutParam(aEventFormats));
                    RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Formats)(format event) failed, hrc=%Rhrc\n", hrc));
                    RTTESTI_CHECK_MSG(aEventFormats.size() == 1,
                                      ("Format event returned %zu formats, expected 1\n", aEventFormats.size()));
                    if (aEventFormats.size() == 1)
                    {
                        Bstr bstrEventMimeType;
                        hrc = aEventFormats[0]->COMGETTER(MimeType)(bstrEventMimeType.asOutParam());
                        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(MimeType)(format event) failed, hrc=%Rhrc\n", hrc));
                        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrEventMimeType).c_str(), "text/plain;charset=utf-8"));
                    }
                }
            }
            else if (enmType == VBoxEventType_OnClipboardDataChanged)
                RTTESTI_CHECK(tstClipboardCheckDataChangedEvent(ptrEvent, "public API data event",
                                                                VBOX_SHCL_MAIN_CLIENT_NONE, ClipboardAction_Copy,
                                                                NULL /* pptrItem */));
        }
        ComPtr<IEvent> ptrUnexpectedAfterWrite;
        hrc = ptrEventSource->GetEvent(ptrListener, 0 /* aTimeout */, ptrUnexpectedAfterWrite.asOutParam());
        RTTESTI_CHECK_MSG(   hrc == VBOX_E_OBJECT_NOT_FOUND
                          || ptrUnexpectedAfterWrite.isNull(),
                          ("Unexpected clipboard event after explicit API write, hrc=%Rhrc\n", hrc));
        if (ptrUnexpectedAfterWrite.isNotNull())
        {
            BOOL fWaitable = FALSE;
            hrc = ptrUnexpectedAfterWrite->COMGETTER(Waitable)(&fWaitable);
            if (SUCCEEDED(hrc) && fWaitable)
                ptrEventSource->EventProcessed(ptrListener, ptrUnexpectedAfterWrite);
        }

        if (ptrListener.isNotNull())
            ptrEventSource->UnregisterListener(ptrListener);
        fListenerRegistered = false;
        ptrListener.setNull();

        /* Confirm the host-origin write is visible through query and raw read APIs. */
        aReadFormats.setNull();
        hrc = ptrClipboard->ReadFormats(ComSafeArrayAsOutParam(aReadFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("ReadFormats after WriteData failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aReadFormats.size() == 1);

        fAvailable = FALSE;
        hrc = ptrClipboard->IsFormatAvailable(ClipboardSource_Host, ptrTextFormat, &fAvailable);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("IsFormatAvailable after WriteData failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(fAvailable);

        aSupportedFormats.setNull();
        hrc = ptrClipboard->GetSupportedFormats(ClipboardSource_Host, ComSafeArrayAsOutParam(aSupportedFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetSupportedFormats after WriteData failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aSupportedFormats.size() == 1);

        ClipboardSource_T enmGuestReadSource = ClipboardSource_Custom;
        Bstr bstrGuestRequestedMimeType("");
        Bstr bstrGuestReadMimeType;
        SafeArray<BYTE> aGuestReadBuffer;
        hrc = ptrClipboard->ReadDataRaw(ClipboardAction_Copy, bstrGuestRequestedMimeType.raw(), &enmGuestReadSource,
                                        bstrGuestReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aGuestReadBuffer));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("ReadDataRaw(host -> guest) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmGuestReadSource == ClipboardSource_Host);
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrGuestReadMimeType).c_str(), "text/plain;charset=utf-8"));
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aGuestReadBuffer, abRoundTripBuffer),
                          ("ReadDataRaw(host -> guest) returned %zu bytes, expected %zu\n",
                           aGuestReadBuffer.size(), abRoundTripBuffer.size()));

        ClipboardSource_T enmTextPlainReadSource = ClipboardSource_Custom;
        Bstr bstrTextPlainRequestedMimeType("text/plain");
        Bstr bstrTextPlainReadMimeType;
        SafeArray<BYTE> aTextPlainReadBuffer;
        hrc = ptrClipboard->ReadDataRaw(ClipboardAction_Copy, bstrTextPlainRequestedMimeType.raw(), &enmTextPlainReadSource,
                                        bstrTextPlainReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aTextPlainReadBuffer));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("ReadDataRaw(text/plain, host -> guest) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmTextPlainReadSource == ClipboardSource_Host);
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrTextPlainReadMimeType).c_str(), "text/plain;charset=utf-8"));
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aTextPlainReadBuffer, abRoundTripBuffer),
                          ("ReadDataRaw(text/plain, host -> guest) returned %zu bytes, expected %zu\n",
                           aTextPlainReadBuffer.size(), abRoundTripBuffer.size()));

        /* Switch to bidirectional mode and verify raw guest-origin writes. */
        hrc = ptrClipboardSettings->COMSETTER(Mode)(ClipboardMode_Bidirectional);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc),
                                ("COMSETTER(Mode) bidirectional before guest write failed, hrc=%Rhrc\n", hrc));

        ClipboardSource_T enmWrittenSource = ClipboardSource_Custom;
        Bstr bstrWrittenMimeType;
        SafeArray<BYTE> aWrittenBuffer;
        hrc = ptrClipboard->WriteDataRaw(ClipboardAction_Copy, ClipboardSource_Guest, bstrGuestReadMimeType.raw(),
                                        ComSafeArrayAsInParam(aGuestReadBuffer), &enmWrittenSource,
                                        bstrWrittenMimeType.asOutParam(), ComSafeArrayAsOutParam(aWrittenBuffer));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("WriteDataRaw(guest -> host) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmWrittenSource == ClipboardSource_Guest);
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrWrittenMimeType).c_str(), "text/plain;charset=utf-8"));
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aWrittenBuffer, abRoundTripBuffer),
                          ("WriteDataRaw(guest -> host) returned %zu bytes, expected %zu\n",
                           aWrittenBuffer.size(), abRoundTripBuffer.size()));

        enmTextPlainReadSource = ClipboardSource_Custom;
        bstrTextPlainReadMimeType.setNull();
        aTextPlainReadBuffer.setNull();
        hrc = ptrClipboard->ReadDataRaw(ClipboardAction_Copy, bstrTextPlainRequestedMimeType.raw(), &enmTextPlainReadSource,
                                        bstrTextPlainReadMimeType.asOutParam(), ComSafeArrayAsOutParam(aTextPlainReadBuffer));
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("ReadDataRaw(text/plain, guest -> host) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(enmTextPlainReadSource == ClipboardSource_Guest);
        RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrTextPlainReadMimeType).c_str(), "text/plain;charset=utf-8"));
        RTTESTI_CHECK_MSG(tstByteArrayEquals(aTextPlainReadBuffer, abRoundTripBuffer),
                          ("ReadDataRaw(text/plain, guest -> host) returned %zu bytes, expected %zu\n",
                           aTextPlainReadBuffer.size(), abRoundTripBuffer.size()));

        /* Verify the object read API sees the same guest-origin payload. */
        ptrReadItem.setNull();
        hrc = ptrClipboard->ReadData(ClipboardAction_Copy, ptrReadItem.asOutParam());
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("ReadData after guest write failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrReadItem.isNull());
        if (ptrReadItem.isNotNull())
        {
            ClipboardSource_T enmRoundTripSource = ClipboardSource_Custom;
            hrc = ptrReadItem->COMGETTER(Source)(&enmRoundTripSource);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Source)(round-trip) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(enmRoundTripSource == ClipboardSource_Guest);

            ComPtr<IClipboardFormat> ptrRoundTripFormat;
            hrc = ptrReadItem->COMGETTER(Format)(ptrRoundTripFormat.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Format)(round-trip) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK(!ptrRoundTripFormat.isNull());
            if (ptrRoundTripFormat.isNotNull())
            {
                Bstr bstrRoundTripMimeType;
                hrc = ptrRoundTripFormat->COMGETTER(MimeType)(bstrRoundTripMimeType.asOutParam());
                RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(MimeType)(round-trip) failed, hrc=%Rhrc\n", hrc));
                RTTESTI_CHECK(!RTStrCmp(Utf8Str(bstrRoundTripMimeType).c_str(), "text/plain;charset=utf-8"));
            }

            SafeArray<BYTE> aRoundTripBuffer;
            hrc = ptrReadItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aRoundTripBuffer));
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Buffer)(round-trip) failed, hrc=%Rhrc\n", hrc));
            RTTESTI_CHECK_MSG(tstByteArrayEquals(aRoundTripBuffer, abRoundTripBuffer),
                              ("ReadData(round-trip) returned %zu bytes, expected %zu\n",
                               aRoundTripBuffer.size(), abRoundTripBuffer.size()));
        }

        /* Exercise the explicit IHostClipboard endpoint after normal IClipboard paths are known-good. */
        tstHostClipboard(hTest, ptrClipboard, ptrClipboardSettings, ptrEventSource, ptrTextFormat,
                         bstrGuestReadMimeType, aGuestReadBuffer, abRoundTripBuffer);

        /* Verify unsupported format offers fail before checking repeat format notifications. */
        std::vector<ComPtr<IClipboardFormat> > vecFormats;
        vecFormats.push_back(ptrTextFormat);
        vecFormats.push_back(ptrUnsupportedFormat);
        SafeIfaceArray<IClipboardFormat> aWriteFormats(vecFormats);
        hrc = ptrClipboard->WriteFormats(ComSafeArrayAsInParam(aWriteFormats));
        RTTESTI_CHECK_MSG(hrc == VBOX_E_SHCL_FORMAT_NOT_SUPPORTED,
                          ("WriteFormats with unsupported MIME type returned hrc=%Rhrc\n", hrc));

        /* Listen only for format changes to prove repeated offers still notify observers. */
        hrc = ptrEventSource->CreateListener(ptrListener.asOutParam());
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("CreateListener(format) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(!ptrListener.isNull());
        SafeArray<VBoxEventType_T> aFormatEventTypes;
        aFormatEventTypes.push_back(VBoxEventType_OnClipboardFormatChanged);
        hrc = ptrEventSource->RegisterListener(ptrListener, ComSafeArrayAsInParam(aFormatEventTypes), FALSE /* aActive */);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("RegisterListener(format) failed, hrc=%Rhrc\n", hrc));
        fListenerRegistered = SUCCEEDED(hrc);

        vecFormats.clear();
        vecFormats.push_back(ptrTextFormat);
        SafeIfaceArray<IClipboardFormat> aWriteTextFormats(vecFormats);
        hrc = ptrClipboard->WriteFormats(ComSafeArrayAsInParam(aWriteTextFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("WriteFormats failed, hrc=%Rhrc\n", hrc));

        hrc = ptrClipboard->WriteFormats(ComSafeArrayAsInParam(aWriteTextFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Repeated WriteFormats failed, hrc=%Rhrc\n", hrc));

        /* Repeated explicit host offers must remain visible to observers such as VBoxManage clipboard listen. */
        for (unsigned i = 0; i < 2; i++)
        {
            ComPtr<IEvent> ptrEvent;
            hrc = ptrEventSource->GetEvent(ptrListener, 1000 /* aTimeout */, ptrEvent.asOutParam());
            RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("GetEvent(format write %u) failed, hrc=%Rhrc\n", i, hrc));
            RTTESTI_CHECK_MSG_BREAK(!ptrEvent.isNull(), ("GetEvent(format write %u) returned no event\n", i));
            VBoxEventType_T enmType = VBoxEventType_Invalid;
            hrc = ptrEvent->COMGETTER(Type)(&enmType);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("COMGETTER(Type)(format write %u) failed, hrc=%Rhrc\n", i, hrc));
            RTTESTI_CHECK_MSG(enmType == VBoxEventType_OnClipboardFormatChanged,
                              ("GetEvent(format write %u) returned type %d, expected %d\n",
                               i, enmType, VBoxEventType_OnClipboardFormatChanged));
        }

        if (ptrListener.isNotNull())
            ptrEventSource->UnregisterListener(ptrListener);
        fListenerRegistered = false;
        ptrListener.setNull();

        /* Reset clears transient clipboard transfer and format state. */
        hrc = ptrClipboard->Reset();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("Reset failed, hrc=%Rhrc\n", hrc));

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        SafeIfaceArray<IClipboardTransfer> aResetTransfers;
        hrc = ptrTransfers->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aResetTransfers));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("GetTransfers(Any after Reset) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aResetTransfers.size() == 0);
#endif

        aReadFormats.setNull();
        hrc = ptrClipboard->ReadFormats(ComSafeArrayAsOutParam(aReadFormats));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrc), ("ReadFormats after Reset failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK(aReadFormats.size() == 0);

        RTTestSub(hTest, "Clipboard session survives console teardown setup");
        hrc = tstCreateSession(ptrClipboard, NULL /* paFlags */, 0 /* cFlags */, ptrSurvivingSession);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("CreateSession(surviving) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG_BREAK(!ptrSurvivingSession.isNull(), ("CreateSession(surviving) returned NULL\n"));
        hrc = ptrSurvivingSession->COMGETTER(Id)(&idSurvivingSession);
        RTTESTI_CHECK_MSG_BREAK(SUCCEEDED(hrc), ("COMGETTER(Id)(surviving) failed, hrc=%Rhrc\n", hrc));
        RTTESTI_CHECK_MSG(idSurvivingSession != VBOX_SHCL_MAIN_CLIENT_NONE,
                          ("Surviving session returned anonymous/zero client ID before teardown\n"));
    } while (0);

    /* Clean up listeners and VM state regardless of which subtest exited early. */
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fDirFile1Created)
        RTFileDelete(szDirFile1);
    if (fDir1Created)
        RTDirRemove(szDir1);
    if (fFile1Created)
        RTFileDelete(szFile1);
#endif
    if (fListenerRegistered && ptrEventSource.isNotNull() && ptrListener.isNotNull())
        ptrEventSource->UnregisterListener(ptrListener);
    ptrListener.setNull();
    ptrEventSource.setNull();
    ptrClipboard.setNull();

    if (fMachinePoweredOn && !ptrConsole.isNull())
    {
        ComPtr<IProgress> ptrPowerDownProgress;
        HRESULT hrcPowerDown = ptrConsole->PowerDown(ptrPowerDownProgress.asOutParam());
        RTTESTI_CHECK_MSG(SUCCEEDED(hrcPowerDown), ("PowerDown failed, hrc=%Rhrc\n", hrcPowerDown));
        if (SUCCEEDED(hrcPowerDown) && !ptrPowerDownProgress.isNull())
        {
            hrcPowerDown = ptrPowerDownProgress->WaitForCompletion(-1);
            RTTESTI_CHECK_MSG(SUCCEEDED(hrcPowerDown), ("WaitForCompletion(PowerDown) failed, hrc=%Rhrc\n", hrcPowerDown));
        }
    }

    ptrConsole.setNull();

    if (fMachineLocked)
    {
        HRESULT hrcUnlock = ptrSession->UnlockMachine();
        RTTESTI_CHECK_MSG(SUCCEEDED(hrcUnlock), ("UnlockMachine failed, hrc=%Rhrc\n", hrcUnlock));
    }
    ptrSession.setNull();

    if (fMachineRegistered)
    {
        HRESULT hrcWait = tstWaitMachineUnlocked(hTest, ptrMachine);
        RTTESTI_CHECK_MSG(SUCCEEDED(hrcWait), ("Waiting for machine unlock failed, hrc=%Rhrc\n", hrcWait));

        SafeIfaceArray<IMedium> aMedia;
        HRESULT hrcCleanup = ptrMachine->Unregister(CleanupMode_DetachAllReturnHardDisksOnly, ComSafeArrayAsOutParam(aMedia));
        RTTESTI_CHECK_MSG(SUCCEEDED(hrcCleanup), ("Unregister failed, hrc=%Rhrc\n", hrcCleanup));
        if (SUCCEEDED(hrcCleanup))
        {
            ComPtr<IProgress> ptrProgress;
            hrcCleanup = ptrMachine->DeleteConfig(ComSafeArrayAsInParam(aMedia), ptrProgress.asOutParam());
            RTTESTI_CHECK_MSG(SUCCEEDED(hrcCleanup), ("DeleteConfig failed, hrc=%Rhrc\n", hrcCleanup));
            if (SUCCEEDED(hrcCleanup) && !ptrProgress.isNull())
            {
                hrcCleanup = ptrProgress->WaitForCompletion(-1);
                RTTESTI_CHECK_MSG(SUCCEEDED(hrcCleanup), ("WaitForCompletion(DeleteConfig) failed, hrc=%Rhrc\n", hrcCleanup));
            }
        }
    }

    if (ptrSurvivingSession.isNotNull())
    {
        RTTestSub(hTest, "Clipboard session after console teardown");

        ULONG idAfterTeardown = VBOX_SHCL_MAIN_CLIENT_NONE;
        hrc = ptrSurvivingSession->COMGETTER(Id)(&idAfterTeardown);
        RTTESTI_CHECK_MSG(   FAILED(hrc)
                           || idAfterTeardown == idSurvivingSession,
                           ("Surviving session ID changed from %RU32 to %RU32, hrc=%Rhrc\n",
                            (uint32_t)idSurvivingSession, (uint32_t)idAfterTeardown, hrc));
        RTTESTI_CHECK_MSG(   FAILED(hrc)
                           || idAfterTeardown != VBOX_SHCL_MAIN_CLIENT_NONE,
                           ("Surviving session returned anonymous/zero client ID after teardown\n"));

        SafeIfaceArray<IClipboardFormat> aFormats;
        hrc = ptrSurvivingSession->ReadFormats(ComSafeArrayAsOutParam(aFormats));
        RTTESTI_CHECK_MSG(FAILED(hrc),
                          ("ReadFormats on surviving session after console teardown unexpectedly succeeded\n"));

        ptrSurvivingSession.setNull();
    }
}


int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboard", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    tstInitLogging();

    RTTestBanner(hTest);

#ifndef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RTTestSkipped(hTest, "Shared Clipboard transfers are not available on this platform");
    return RTTestSummaryAndDestroy(hTest);
#endif

    HRESULT hrc = Initialize();
    if (FAILED(hrc))
    {
        RTTestFailed(hTest, "Failed to initialize COM, hrc=%Rhrc", hrc);
        return RTTestSummaryAndDestroy(hTest);
    }

    tstClipboardPublicApi(hTest);

    Shutdown();
    return RTTestSummaryAndDestroy(hTest);
}
