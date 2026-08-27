/* $Id: tstClipboardAPI.cpp 115134 2026-08-27 15:09:45Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Public API object testcase.
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
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include "ClipboardFormatImpl.h"
#include "ClipboardItemImpl.h"
#include "ClipboardImpl.h"
#include "ClipboardSessionImpl.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "ClipboardTransferImpl.h"
# include "ClipboardTransferManagerImpl.h"
# include "EventImpl.h"
# include <VBox/com/listeners.h>
#endif

#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/test.h>
#include <iprt/thread.h>


/** @name Parent Clipboard stubs for unexercised session delegation paths.
 * @{ */
/** Test stub for Clipboard::i_registerSession(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_registerSession(VBOXSHCLMAINCLIENTID, ClipboardSession *, uint32_t, const ComPtr<IEventSource> &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_unregisterSession(). */
void Clipboard::i_unregisterSession(VBOXSHCLMAINCLIENTID)
{
}

/** Test stub for Clipboard::i_fireSessionInitialState(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_fireSessionInitialState(VBOXSHCLMAINCLIENTID)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_readFormatObjects(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_readFormatObjects(std::vector<ComPtr<IClipboardFormat> > &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_readDataRaw(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_readDataRaw(ClipboardAction_T, const com::Utf8Str &, ClipboardSource_T *, com::Utf8Str &,
                                std::vector<BYTE> &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_writeDataRaw(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_writeDataRaw(VBOXSHCLMAINCLIENTID, ClipboardAction_T, ClipboardSource_T, const com::Utf8Str &,
                                  const std::vector<BYTE> &, ClipboardSource_T *, com::Utf8Str &, std::vector<BYTE> &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_writeFormatObjects(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_writeFormatObjects(VBOXSHCLMAINCLIENTID, const std::vector<ComPtr<IClipboardFormat> > &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_hostClipboardReportFormats(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_hostClipboardReportFormats(VBOXSHCLMAINCLIENTID, ClipboardAction_T, ClipboardSource_T,
                                                const std::vector<ComPtr<IClipboardFormat> > &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_hostClipboardProvideData(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_hostClipboardProvideData(VBOXSHCLMAINCLIENTID, ULONG, ClipboardAction_T, ClipboardSource_T,
                                              const com::Utf8Str &, const std::vector<BYTE> &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_hostClipboardSetData(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_hostClipboardSetData(VBOXSHCLMAINCLIENTID, ClipboardAction_T, ClipboardSource_T,
                                          const com::Utf8Str &, const std::vector<BYTE> &)
{
    return E_NOTIMPL;
}

/** Test stub for Clipboard::i_hostClipboardClear(); always returns E_NOTIMPL. */
HRESULT Clipboard::i_hostClipboardClear(VBOXSHCLMAINCLIENTID)
{
    return E_NOTIMPL;
}
/** @} */


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Validates an action for the test-local transfer objects.
 *
 * @returns true if @a enmAction is a public ClipboardAction_T value.
 * @param   enmAction           Action to validate.
 */
bool ShClMainIsValidAction(ClipboardAction_T enmAction)
{
    return    enmAction == ClipboardAction_Copy
           || enmAction == ClipboardAction_Cut
           || enmAction == ClipboardAction_Paste
           || enmAction == ClipboardAction_Custom;
}

/**
 * Validates a source for the test-local transfer objects.
 *
 * @returns true if @a enmSource is a public ClipboardSource_T value.
 * @param   enmSource           Source to validate.
 */
bool ShClMainIsValidSource(ClipboardSource_T enmSource)
{
    return    enmSource == ClipboardSource_Host
           || enmSource == ClipboardSource_Guest
           || enmSource == ClipboardSource_Remote
           || enmSource == ClipboardSource_Custom;
}

/** Test stub for the unexercised parent transfer-cancel callback. */
HRESULT Clipboard::i_transferCancel(PCSHCLTRANSFERKEY)
{
    return E_NOTIMPL;
}

/** Test stub for the unexercised parent transfer-event callback. */
void Clipboard::i_fireClipboardTransferEvent(VBOXSHCLMAINCLIENTID, IClipboardTransfer *, ClipboardTransferState_T,
                                             ClipboardTransferInteraction_T, const com::Utf8Str &,
                                             const com::Utf8Str &, ClipboardError_T)
{
}
#endif


/**
 * @page pg_tstClipboardAPI Main Shared Clipboard public API testcase
 *
 * Tests the state and ownership semantics of the small public Main clipboard
 * objects without constructing a VM, HGCM service or native clipboard backend.
 */


/** Tests IClipboardFormat and IClipboardItem value-object semantics. */
static void tstClipboardValues(void)
{
    RTTestISub("Formats and items");

    ComObjPtr<ClipboardFormat> ptrFormatObj;
    HRESULT hrc = ptrFormatObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrFormatObj->init(com::Utf8Str("text/plain;charset=utf-8"));
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComPtr<IClipboardFormat> ptrFormat;
    hrc = ptrFormatObj.queryInterfaceTo(ptrFormat.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    com::Bstr bstrMimeType;
    hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrMimeType).c_str(), "text/plain;charset=utf-8"));

    hrc = ptrFormat->COMSETTER(MimeType)(com::Bstr("text/html").raw());
    RTTESTI_CHECK_RC(hrc, S_OK);
    bstrMimeType.setNull();
    hrc = ptrFormat->COMGETTER(MimeType)(bstrMimeType.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrMimeType).c_str(), "text/html"));

    static uint8_t const s_abPayload[] = { 0, 1, 2, 0xff };
    std::vector<BYTE> abPayload(s_abPayload, s_abPayload + RT_ELEMENTS(s_abPayload));
    ComObjPtr<ClipboardItem> ptrItemObj;
    hrc = ptrItemObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrItemObj->init(7, ClipboardSource_Host, ptrFormat, abPayload);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComPtr<IClipboardItem> ptrItem;
    hrc = ptrItemObj.queryInterfaceTo(ptrItem.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ULONG idItem = 0;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    ULONG cbItem = 0;
    hrc = ptrItem->COMGETTER(Id)(&idItem);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrItem->COMGETTER(Source)(&enmSource);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrItem->COMGETTER(Size)(&cbItem);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(idItem == 7);
    RTTESTI_CHECK(enmSource == ClipboardSource_Host);
    RTTESTI_CHECK(cbItem == sizeof(s_abPayload));

    com::SafeArray<BYTE> aRead;
    hrc = ptrItem->COMGETTER(Buffer)(ComSafeArrayAsOutParam(aRead));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aRead.size() == sizeof(s_abPayload));
    if (aRead.size() == sizeof(s_abPayload))
        RTTESTI_CHECK(!memcmp(aRead.raw(), s_abPayload, sizeof(s_abPayload)));

    static uint8_t const s_abReplacement[] = { 9, 8, 7 };
    com::SafeArray<BYTE> aReplacement;
    hrc = aReplacement.initFrom(s_abReplacement, RT_ELEMENTS(s_abReplacement));
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrItem->COMSETTER(Buffer)(ComSafeArrayAsInParam(aReplacement));
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrItem->COMGETTER(Size)(&cbItem);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(cbItem == sizeof(s_abReplacement));
}


/** Tests IClipboardSession identity, endpoint state and idempotent close. */
static void tstClipboardSession(void)
{
    RTTestISub("Session state");

    ComObjPtr<ClipboardSession> ptrSessionObj;
    HRESULT hrc = ptrSessionObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrSessionObj->initForTesting(17, 0);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComPtr<IClipboardSession> ptrSession;
    hrc = ptrSessionObj.queryInterfaceTo(ptrSession.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ULONG idSession = 0;
    hrc = ptrSession->COMGETTER(Id)(&idSession);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(idSession == 17);

    ComPtr<IEventSource> ptrEventSource;
    hrc = ptrSession->COMGETTER(EventSource)(ptrEventSource.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrEventSource.isNotNull());

    hrc = ptrSession->Close();
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrSession->Close();
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrSession->COMGETTER(Id)(&idSession);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(idSession == 17);

    ptrEventSource.setNull();
    RTTESTI_CHECK_RC(RTTestIDisableAssertions(), VINF_SUCCESS);
    hrc = ptrSession->COMGETTER(EventSource)(ptrEventSource.asOutParam());
    RTTESTI_CHECK_RC(RTTestIRestoreAssertions(), VINF_SUCCESS);
    RTTESTI_CHECK(FAILED(hrc));
    RTTESTI_CHECK(ptrEventSource.isNull());
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** State captured by the active listener reentrancy test. */
struct ClipboardTransferReentryContext
{
    ClipboardTransferReentryContext()
        : pManager(NULL)
        , cDepth(0)
        , cMaxDepth(0)
        , cEvents(0)
        , fOverflow(false)
        , fReentered(false)
        , fUninitOnAdded(false)
        , hrcReentry(E_FAIL)
        , fCompletedAtTerminal(FALSE)
        , uPercentAtInProgress(UINT32_MAX)
        , hDone(NIL_RTSEMEVENT)
    {
        ShClTransferKeyReset(&Key);
        RT_ZERO(aEventStates);
        RT_ZERO(aObjectStates);
        int const vrc = RTSemEventCreate(&hDone);
        AssertRC(vrc);
    }

    ~ClipboardTransferReentryContext()
    {
        if (hDone != NIL_RTSEMEVENT)
        {
            int const vrc = RTSemEventDestroy(hDone);
            AssertRC(vrc);
            hDone = NIL_RTSEMEVENT;
        }
    }

    ClipboardTransferManager  *pManager;
    SHCLTRANSFERKEY            Key;
    uint32_t                   cDepth;
    uint32_t                   cMaxDepth;
    uint32_t                   cEvents;
    bool                       fOverflow;
    bool                       fReentered;
    bool                       fUninitOnAdded;
    HRESULT                    hrcReentry;
    BOOL                       fCompletedAtTerminal;
    ULONG                      uPercentAtInProgress;
    ClipboardTransferState_T   aEventStates[8];
    ClipboardTransferState_T   aObjectStates[8];
    ComPtr<IClipboardTransferEvent> ptrHeldEvent;
    ComPtr<IClipboardTransfer> ptrHeldTransfer;
    ComPtr<IProgress>          ptrHeldProgress;
    RTSEMEVENT                 hDone;
};


/** Active test listener which completes a transfer from its InProgress callback. */
class ClipboardTransferReentryListener
{
public:
    ClipboardTransferReentryListener()
        : mContext(NULL)
    { }

    HRESULT init(ClipboardTransferReentryContext *aContext)
    {
        AssertPtrReturn(aContext, E_INVALIDARG);
        mContext = aContext;
        return S_OK;
    }

    void uninit()
    {
        mContext = NULL;
    }

    virtual ~ClipboardTransferReentryListener()
    { }

    /**
     * Handles a transfer event for the active-listener publication tests.
     *
     * @retval  S_OK                if the event was handled or ignored.
     * @retval  E_FAIL              if the listener context is unavailable.
     * @param   aType               Event type.
     * @param   aEvent              Event object.
     */
    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        if (aType != VBoxEventType_OnClipboardTransfer)
            return S_OK;

        ClipboardTransferReentryContext *pContext = mContext;
        AssertPtrReturn(pContext, E_FAIL);
        bool fSignalDone = false;
        ++pContext->cDepth;
        if (pContext->cMaxDepth < pContext->cDepth)
            pContext->cMaxDepth = pContext->cDepth;

        ComPtr<IClipboardTransferEvent> ptrEvent = aEvent;
        ClipboardTransferState_T enmEventState = ClipboardTransferState_Removed;
        ComPtr<IClipboardTransfer> ptrTransfer;
        HRESULT hrc = ptrEvent.isNull() ? E_NOINTERFACE : ptrEvent->COMGETTER(State)(&enmEventState);
        if (SUCCEEDED(hrc))
            hrc = ptrEvent->COMGETTER(Transfer)(ptrTransfer.asOutParam());

        ClipboardTransferState_T enmObjectState = ClipboardTransferState_Removed;
        if (SUCCEEDED(hrc))
            hrc = ptrTransfer->COMGETTER(State)(&enmObjectState);
        ULONG idTransfer = 0;
        if (SUCCEEDED(hrc))
            hrc = ptrTransfer->COMGETTER(Id)(&idTransfer);
        if (SUCCEEDED(hrc) && idTransfer != ShClTransferKeyGetTransferId(&pContext->Key))
        {
            --pContext->cDepth;
            return S_OK;
        }

        if (pContext->cEvents < RT_ELEMENTS(pContext->aEventStates))
        {
            pContext->aEventStates[pContext->cEvents] = enmEventState;
            pContext->aObjectStates[pContext->cEvents] = enmObjectState;
            ++pContext->cEvents;
        }
        else
            pContext->fOverflow = true;

        if (   SUCCEEDED(hrc)
            && enmEventState == ClipboardTransferState_Added
            && pContext->fUninitOnAdded
            && !pContext->fReentered)
        {
            pContext->fReentered = true;
            pContext->ptrHeldTransfer = ptrTransfer;
            hrc = ptrTransfer->COMGETTER(Progress)(pContext->ptrHeldProgress.asOutParam());
            if (SUCCEEDED(hrc))
                hrc = pContext->pManager->i_handleTransferStatus(&pContext->Key,
                                                                 NULL /* pTransfer */,
                                                                 SHCLSOURCE_REMOTE,
                                                                 SHCLTRANSFERSTATUS_INITIALIZED,
                                                                 VINF_SUCCESS);
            if (SUCCEEDED(hrc))
                hrc = pContext->pManager->i_handleTransferStatus(&pContext->Key,
                                                                 NULL /* pTransfer */,
                                                                 SHCLSOURCE_REMOTE,
                                                                 SHCLTRANSFERSTATUS_STARTED,
                                                                 VINF_SUCCESS);
            if (SUCCEEDED(hrc))
                hrc = pContext->pManager->i_handleTransferProgress(&pContext->Key, 50, 100);
            if (SUCCEEDED(hrc))
                pContext->pManager->uninit();
            pContext->hrcReentry = hrc;
            fSignalDone = true;
        }
        else if (SUCCEEDED(hrc) && enmEventState == ClipboardTransferState_InProgress)
        {
            ComPtr<IProgress> ptrProgress;
            hrc = ptrTransfer->COMGETTER(Progress)(ptrProgress.asOutParam());
            if (SUCCEEDED(hrc))
                hrc = ptrProgress->COMGETTER(Percent)(&pContext->uPercentAtInProgress);
            if (!pContext->fReentered)
            {
                pContext->fReentered = true;
                pContext->hrcReentry = pContext->pManager->i_handleTransferStatus(&pContext->Key,
                                                                                 NULL /* pTransfer */,
                                                                                 SHCLSOURCE_REMOTE,
                                                                                 SHCLTRANSFERSTATUS_COMPLETED,
                                                                                 VINF_SUCCESS);
            }
        }
        else if (   SUCCEEDED(hrc)
                 && (   enmEventState == ClipboardTransferState_Completed
                     || enmEventState == ClipboardTransferState_Failed))
        {
            pContext->ptrHeldEvent = ptrEvent;
            pContext->ptrHeldTransfer = ptrTransfer;
            hrc = ptrTransfer->COMGETTER(Progress)(pContext->ptrHeldProgress.asOutParam());
            if (SUCCEEDED(hrc))
                hrc = pContext->ptrHeldProgress->COMGETTER(Completed)(&pContext->fCompletedAtTerminal);
            fSignalDone = true;
        }

        if (FAILED(hrc) && SUCCEEDED(pContext->hrcReentry))
            pContext->hrcReentry = hrc;
        --pContext->cDepth;
        if (fSignalDone && pContext->hDone != NIL_RTSEMEVENT)
        {
            int const vrc = RTSemEventSignal(pContext->hDone);
            AssertRC(vrc);
        }
        return S_OK;
    }

private:
    ClipboardTransferReentryContext *mContext;
};

typedef ListenerImpl<ClipboardTransferReentryListener, ClipboardTransferReentryContext *>
    ClipboardTransferReentryListenerImpl;

VBOX_LISTENER_DECLARE(ClipboardTransferReentryListenerImpl)


/** Waits for an asynchronously published transfer state. */
static bool tstClipboardTransferWaitState(const ComPtr<IClipboardTransfer> &ptrTransfer,
                                          ClipboardTransferState_T enmExpected)
{
    for (uint32_t i = 0; i < 5000; ++i)
    {
        ClipboardTransferState_T enmState = ClipboardTransferState_Removed;
        if (SUCCEEDED(ptrTransfer->COMGETTER(State)(&enmState)) && enmState == enmExpected)
            return true;
        RTThreadSleep(1);
    }
    return false;
}


/** Waits for an asynchronously published Progress percentage. */
static bool tstClipboardTransferWaitPercent(const ComPtr<IProgress> &ptrProgress, ULONG uExpected)
{
    for (uint32_t i = 0; i < 5000; ++i)
    {
        ULONG uPercent = UINT32_MAX;
        if (SUCCEEDED(ptrProgress->COMGETTER(Percent)(&uPercent)) && uPercent == uExpected)
            return true;
        RTThreadSleep(1);
    }
    return false;
}


/** Waits for asynchronous Progress completion. */
static bool tstClipboardTransferWaitCompleted(const ComPtr<IProgress> &ptrProgress)
{
    for (uint32_t i = 0; i < 5000; ++i)
    {
        BOOL fCompleted = FALSE;
        if (SUCCEEDED(ptrProgress->COMGETTER(Completed)(&fCompleted)) && fCompleted)
            return true;
        RTThreadSleep(1);
    }
    return false;
}


/** Waits for deferred self-cleanup of the manager publication worker. */
static bool tstClipboardTransferWaitWorkerStopped(ClipboardTransferManager *pManager)
{
    for (uint32_t i = 0; i < 5000; ++i)
    {
        if (!pManager->i_isPublicationWorkerRunning())
            return true;
        RTThreadSleep(1);
    }
    return false;
}


/** Tests IClipboardTransfer metadata without attaching a data-plane backend. */
static void tstClipboardTransfer(void)
{
    RTTestISub("Transfer metadata");

    ComObjPtr<ClipboardTransfer> ptrTransferObj;
    HRESULT hrc = ptrTransferObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    ComPtr<IClipboardItem> ptrItem;
    ComPtr<IProgress> ptrProgress;
    hrc = ptrTransferObj->init(23, ClipboardTransferDirection_ToGuest, ClipboardSource_Host,
                               ClipboardAction_Copy, ptrItem, ptrProgress);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComPtr<IClipboardTransfer> ptrTransfer;
    hrc = ptrTransferObj.queryInterfaceTo(ptrTransfer.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ULONG idTransfer = 0;
    ClipboardTransferDirection_T enmDirection = ClipboardTransferDirection_Any;
    ClipboardSource_T enmSource = ClipboardSource_Custom;
    ClipboardAction_T enmAction = ClipboardAction_Invalid;
    ClipboardTransferState_T enmState = ClipboardTransferState_Removed;
    hrc = ptrTransfer->COMGETTER(Id)(&idTransfer);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrTransfer->COMGETTER(Direction)(&enmDirection);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrTransfer->COMGETTER(Source)(&enmSource);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrTransfer->COMGETTER(Action)(&enmAction);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrTransfer->COMGETTER(State)(&enmState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(idTransfer == 23);
    RTTESTI_CHECK(enmDirection == ClipboardTransferDirection_ToGuest);
    RTTESTI_CHECK(enmSource == ClipboardSource_Host);
    RTTESTI_CHECK(enmAction == ClipboardAction_Copy);
    RTTESTI_CHECK(enmState == ClipboardTransferState_Added);

    ptrTransferObj->i_setState(ClipboardTransferState_Failed, com::Utf8Str("failed"), ClipboardError_OperationFailed);
    com::Bstr bstrMessage;
    ClipboardError_T enmError = ClipboardError_None;
    hrc = ptrTransfer->COMGETTER(State)(&enmState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrTransfer->COMGETTER(Message)(bstrMessage.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrTransfer->COMGETTER(Error)(&enmError);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmState == ClipboardTransferState_Failed);
    RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrMessage).c_str(), "failed"));
    RTTESTI_CHECK(enmError == ClipboardError_OperationFailed);

    ComPtr<IClipboardTransferData> ptrData;
    RTTESTI_CHECK_RC(RTTestIDisableAssertions(), VINF_SUCCESS);
    hrc = ptrTransfer->COMGETTER(Data)(ptrData.asOutParam());
    RTTESTI_CHECK_RC(RTTestIRestoreAssertions(), VINF_SUCCESS);
    RTTESTI_CHECK(FAILED(hrc));
    RTTESTI_CHECK(ptrData.isNull());
}


/** Tests IClipboardTransferManager ownership, filtering and removal. */
static void tstClipboardTransferManager(void)
{
    RTTestISub("Transfer manager");

    ComObjPtr<EventSource> ptrEventSourceObj;
    HRESULT hrc = ptrEventSourceObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrEventSourceObj->init();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    ComPtr<IEventSource> ptrEventSource;
    hrc = ptrEventSourceObj.queryInterfaceTo(ptrEventSource.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComObjPtr<ClipboardTransferManager> ptrManagerObj;
    hrc = ptrManagerObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrManagerObj->init(ptrEventSource);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    RTTESTI_CHECK(ptrManagerObj->i_isPublicationWorkerRunning());

    ComPtr<IClipboardTransferManager> ptrManager;
    hrc = ptrManagerObj.queryInterfaceTo(ptrManager.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    com::SafeIfaceArray<IClipboardTransfer> aTransfers;
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    ComPtr<IClipboardTransfer> ptrTransfer;
    hrc = ptrManager->Create(ClipboardTransferDirection_ToGuest, ClipboardSource_Host, ClipboardAction_Copy,
                             ptrTransfer.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrTransfer.isNotNull());

    ComPtr<IProgress> ptrManagerProgress;
    hrc = ptrTransfer->COMGETTER(Progress)(ptrManagerProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrManagerProgress.isNotNull());
    if (ptrManagerProgress.isNull())
        return;
    ComPtr<IProgress> ptrManagerProgressAgain;
    hrc = ptrTransfer->COMGETTER(Progress)(ptrManagerProgressAgain.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrManagerProgressAgain == ptrManagerProgress);
    BOOL fManagerProgressCancelable = FALSE;
    hrc = ptrManagerProgress->COMGETTER(Cancelable)(&fManagerProgressCancelable);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(fManagerProgressCancelable == TRUE);

    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_ToGuest, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() == 1)
        RTTESTI_CHECK(aTransfers[0] == ptrTransfer);

    hrc = ptrManager->Remove(ptrTransfer);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrManagerProgress));
    LONG hrcManagerProgress = S_OK;
    hrc = ptrManagerProgress->COMGETTER(ResultCode)(&hrcManagerProgress);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK((HRESULT)hrcManagerProgress == E_ABORT);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    RTTESTI_CHECK_RC(RTTestIDisableAssertions(), VINF_SUCCESS);
    hrc = ptrManager->Remove(ptrTransfer);
    RTTESTI_CHECK_RC(RTTestIRestoreAssertions(), VINF_SUCCESS);
    RTTESTI_CHECK(FAILED(hrc));

    /* A manager-created transfer has no service key, so IProgress::Cancel()
     * completes its lifecycle locally without attempting a backend call. */
    ComPtr<IClipboardTransfer> ptrLocalCancelTransfer;
    hrc = ptrManager->Create(ClipboardTransferDirection_ToGuest, ClipboardSource_Host, ClipboardAction_Copy,
                             ptrLocalCancelTransfer.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrLocalCancelTransfer.isNotNull());
    ComPtr<IProgress> ptrLocalCancelProgress;
    hrc = ptrLocalCancelTransfer->COMGETTER(Progress)(ptrLocalCancelProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrLocalCancelProgress.isNotNull());
    if (ptrLocalCancelProgress.isNull())
        return;
    hrc = ptrLocalCancelProgress->Cancel();
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrLocalCancelProgress));
    ClipboardTransferState_T enmLocalCancelState = ClipboardTransferState_Added;
    hrc = ptrLocalCancelTransfer->COMGETTER(State)(&enmLocalCancelState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmLocalCancelState == ClipboardTransferState_Canceled);
    LONG hrcLocalCancel = S_OK;
    hrc = ptrLocalCancelProgress->COMGETTER(ResultCode)(&hrcLocalCancel);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK((HRESULT)hrcLocalCancel == E_ABORT);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    SHCLTRANSFERKEY Key;
    ShClTransferKeyInit(&Key, 1, 2, 1);
    ptrManagerObj->i_setPublicationWorkerSignalsSuppressed(true);
    hrc = ptrManagerObj->i_handleTransferStatus(&Key, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&Key, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&Key, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);

    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() != 1)
        return;

    ClipboardTransferState_T enmTransferState = ClipboardTransferState_Removed;
    hrc = aTransfers[0]->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Added);

    ComPtr<IProgress> ptrProgress;
    hrc = aTransfers[0]->COMGETTER(Progress)(ptrProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrProgress.isNotNull());
    if (ptrProgress.isNull())
        return;

    BOOL fCancelable = FALSE;
    hrc = ptrProgress->COMGETTER(Cancelable)(&fCancelable);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(fCancelable == TRUE);

    ULONG uPercent = UINT32_MAX;
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 0);

    SHCLTRANSFERKEY StaleKey = Key;
    StaleKey.uGeneration++;
    hrc = ptrManagerObj->i_handleTransferProgress(&StaleKey, UINT64_MAX / 2, UINT64_MAX);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = aTransfers[0]->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Added);

    /* The first real payload byte exposes InProgress even when it is below one percent. */
    hrc = ptrManagerObj->i_handleTransferProgress(&Key, 1, UINT64_MAX);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitState(aTransfers[0], ClipboardTransferState_InProgress));
    ptrManagerObj->i_setPublicationWorkerSignalsSuppressed(false);
    hrc = aTransfers[0]->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_InProgress);
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 0);

    hrc = ptrManagerObj->i_handleTransferProgress(&Key, UINT64_MAX / 2, UINT64_MAX);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitPercent(ptrProgress, 49));
    hrc = aTransfers[0]->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_InProgress);
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 49);

    hrc = ptrManagerObj->i_handleTransferProgress(&Key, UINT64_MAX / 2 - 1, UINT64_MAX);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferProgress(&Key, UINT64_MAX / 2, UINT64_MAX - 1);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 49);

    hrc = ptrManagerObj->i_handleTransferProgress(&Key, UINT64_MAX, UINT64_MAX);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitPercent(ptrProgress, 99));
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 99);

    hrc = ptrManagerObj->i_handleTransferStatus(&Key, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrProgress));
    BOOL fCompleted = FALSE;
    hrc = ptrProgress->COMGETTER(Completed)(&fCompleted);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(fCompleted == TRUE);
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 100);
    fCancelable = TRUE;
    hrc = ptrProgress->COMGETTER(Cancelable)(&fCancelable);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(fCancelable == FALSE);
    hrc = ptrProgress->Cancel();
    RTTESTI_CHECK(hrc == VBOX_E_INVALID_OBJECT_STATE);

    hrc = ptrManagerObj->i_handleTransferProgress(&Key, UINT64_MAX, UINT64_MAX);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 100);

    /* Force Cancel to win the Progress lock before a direct service
     * COMPLETED status.  Terminal acceptance must normalize that status to
     * Canceled/E_ABORT before erasing the exact record. */
    SHCLTRANSFERKEY CancelFirstKey;
    ShClTransferKeyInit(&CancelFirstKey, 10, 11, 10);
    hrc = ptrManagerObj->i_handleTransferStatus(&CancelFirstKey, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&CancelFirstKey, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&CancelFirstKey, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() != 1)
        return;
    ComPtr<IClipboardTransfer> ptrCancelFirstTransfer = aTransfers[0];
    ComPtr<IProgress> ptrCancelFirstProgress;
    hrc = ptrCancelFirstTransfer->COMGETTER(Progress)(ptrCancelFirstProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrCancelFirstProgress.isNotNull());
    if (ptrCancelFirstProgress.isNull())
        return;

    ptrManagerObj->i_setProgressCancellationPollingSuppressed(true);
    ptrManagerObj->i_setPublicationWorkerSignalsSuppressed(true);
    hrc = ptrCancelFirstProgress->Cancel();
    RTTESTI_CHECK_RC(hrc, S_OK);

    /* A canceled Progress is no longer an active manager transfer even while
     * its exact internal record is retained for backend cleanup and terminal
     * status arbitration. */
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    hrc = ptrManagerObj->i_handleTransferStatus(&CancelFirstKey, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    ptrManagerObj->i_setProgressCancellationPollingSuppressed(false);
    ptrManagerObj->i_setPublicationWorkerSignalsSuppressed(false);

    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrCancelFirstProgress));
    hrc = ptrCancelFirstTransfer->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Canceled);
    LONG hrcCancelFirstResult = S_OK;
    hrc = ptrCancelFirstProgress->COMGETTER(ResultCode)(&hrcCancelFirstResult);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK((HRESULT)hrcCancelFirstResult == E_ABORT);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    /* A progress snapshot preceding STARTED must become visible immediately,
     * then expose InProgress when the lifecycle status catches up. */
    SHCLTRANSFERKEY EarlyKey;
    ShClTransferKeyInit(&EarlyKey, 2, 3, 2);
    hrc = ptrManagerObj->i_handleTransferStatus(&EarlyKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&EarlyKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferProgress(&EarlyKey, 50, 100);
    RTTESTI_CHECK_RC(hrc, S_OK);

    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() != 1)
        return;
    hrc = aTransfers[0]->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Added);
    ptrProgress.setNull();
    hrc = aTransfers[0]->COMGETTER(Progress)(ptrProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitPercent(ptrProgress, 50));
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 50);

    hrc = ptrManagerObj->i_handleTransferStatus(&EarlyKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitState(aTransfers[0], ClipboardTransferState_InProgress));
    hrc = aTransfers[0]->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_InProgress);
    hrc = ptrProgress->COMGETTER(Percent)(&uPercent);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(uPercent == 50);
    hrc = ptrManagerObj->i_handleTransferStatus(&EarlyKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrProgress));

    /* IProgress::Cancel() is observed by the existing publication worker.  A
     * queued byte update after cancellation is harmless, and successful exact
     * backend dispatch completes the Progress as canceled. */
    ptrManagerObj->i_setCancelServiceResult(S_OK);
    SHCLTRANSFERKEY CancelKey;
    ShClTransferKeyInit(&CancelKey, 20, 21, 20);
    hrc = ptrManagerObj->i_handleTransferStatus(&CancelKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&CancelKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&CancelKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);

    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() != 1)
        return;
    ComPtr<IClipboardTransfer> ptrCancelTransfer = aTransfers[0];
    ComPtr<IProgress> ptrCancelProgress;
    hrc = ptrCancelTransfer->COMGETTER(Progress)(ptrCancelProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrCancelProgress.isNotNull());
    if (ptrCancelProgress.isNull())
        return;
    fCancelable = FALSE;
    hrc = ptrCancelProgress->COMGETTER(Cancelable)(&fCancelable);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(fCancelable == TRUE);

    ptrManagerObj->i_setPublicationWorkerSignalsSuppressed(true);
    RTThreadSleep(150); /* Let lifecycle publications drain before arranging the cancel race. */
    hrc = ptrCancelProgress->Cancel();
    RTTESTI_CHECK_RC(hrc, S_OK);
    BOOL fCanceled = FALSE;
    hrc = ptrCancelProgress->COMGETTER(Canceled)(&fCanceled);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(fCanceled == TRUE);
    hrc = ptrManagerObj->i_handleTransferProgress(&CancelKey, 75, 100);
    RTTESTI_CHECK_RC(hrc, S_OK);
    ptrManagerObj->i_setPublicationWorkerSignalsSuppressed(false);

    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrCancelProgress));
    hrc = ptrCancelTransfer->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Canceled);
    ClipboardError_T enmCancelError = ClipboardError_None;
    hrc = ptrCancelTransfer->COMGETTER(Error)(&enmCancelError);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmCancelError == ClipboardError_None);
    LONG hrcCancelResult = S_OK;
    hrc = ptrCancelProgress->COMGETTER(ResultCode)(&hrcCancelResult);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK((HRESULT)hrcCancelResult == E_ABORT);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    /* Cancellation is irreversible once accepted by Progress.  If backend
     * dispatch fails, publish an explicit failure and complete it rather than
     * clearing a marker and leaving waiters blocked forever. */
    ptrManagerObj->i_setCancelServiceResult(E_FAIL);
    SHCLTRANSFERKEY FailedCancelKey;
    ShClTransferKeyInit(&FailedCancelKey, 21, 22, 21);
    hrc = ptrManagerObj->i_handleTransferStatus(&FailedCancelKey, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() != 1)
        return;
    ComPtr<IClipboardTransfer> ptrFailedCancelTransfer = aTransfers[0];
    ComPtr<IProgress> ptrFailedCancelProgress;
    hrc = ptrFailedCancelTransfer->COMGETTER(Progress)(ptrFailedCancelProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrFailedCancelProgress.isNotNull());
    if (ptrFailedCancelProgress.isNull())
        return;
    hrc = ptrFailedCancelProgress->Cancel();
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrFailedCancelProgress));
    hrc = ptrFailedCancelTransfer->COMGETTER(State)(&enmTransferState);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Failed);
    enmCancelError = ClipboardError_None;
    hrc = ptrFailedCancelTransfer->COMGETTER(Error)(&enmCancelError);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(enmCancelError == ClipboardError_OperationFailed);
    hrcCancelResult = S_OK;
    hrc = ptrFailedCancelProgress->COMGETTER(ResultCode)(&hrcCancelResult);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK((HRESULT)hrcCancelResult == VBOX_E_SHCL_ERROR);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    /* A backend disconnect has no guaranteed terminal service status.  Its
     * local asynchronous reset must remove every live record and complete the
     * corresponding Progress while leaving the publication worker available. */
    SHCLTRANSFERKEY ResetKey;
    ShClTransferKeyInit(&ResetKey, 30, 40, 30);
    hrc = ptrManagerObj->i_handleTransferStatus(&ResetKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&ResetKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&ResetKey,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferProgress(&ResetKey, 25, 100);
    RTTESTI_CHECK_RC(hrc, S_OK);

    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() != 1)
        return;
    ComPtr<IClipboardTransfer> ptrResetTransfer = aTransfers[0];
    ComPtr<IProgress> ptrResetProgress;
    hrc = ptrResetTransfer->COMGETTER(Progress)(ptrResetProgress.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(ptrResetProgress.isNotNull());
    if (ptrResetProgress.isNull())
        return;

    ptrManagerObj->i_resetFromService();
    RTTESTI_CHECK(tstClipboardTransferWaitState(ptrResetTransfer, ClipboardTransferState_Removed));
    RTTESTI_CHECK(tstClipboardTransferWaitCompleted(ptrResetProgress));
    LONG hrcResetResult = S_OK;
    hrc = ptrResetProgress->COMGETTER(ResultCode)(&hrcResetResult);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK((HRESULT)hrcResetResult == E_ABORT);
    RTTESTI_CHECK(ptrManagerObj->i_isPublicationWorkerRunning());
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    /* An active event listener reenters the manager with terminal status.  The
     * strand must defer that publication until InProgress delivery returns. */
    ClipboardTransferReentryContext Context;
    Context.pManager = ptrManagerObj;
    ShClTransferKeyInit(&Context.Key, 3, 4, 3);

    ComObjPtr<ClipboardTransferReentryListenerImpl> ptrListenerObj;
    hrc = ptrListenerObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrListenerObj->init(new ClipboardTransferReentryListener(), &Context);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComPtr<IEventListener> ptrListener;
    hrc = ptrListenerObj.queryInterfaceTo(ptrListener.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    com::SafeArray<VBoxEventType_T> aEventTypes;
    aEventTypes.push_back(VBoxEventType_OnClipboardTransfer);
    hrc = ptrEventSource->RegisterListener(ptrListener, ComSafeArrayAsInParam(aEventTypes), TRUE /* aActive */);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key,
                                                NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_STARTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferProgress(&Context.Key, 50, 100);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK_RC(RTSemEventWait(Context.hDone, RT_MS_5SEC), VINF_SUCCESS);

    RTTESTI_CHECK(!Context.fOverflow);
    RTTESTI_CHECK(Context.fReentered);
    RTTESTI_CHECK_RC(Context.hrcReentry, S_OK);
    RTTESTI_CHECK(Context.cDepth == 0);
    RTTESTI_CHECK(Context.cMaxDepth == 1);
    RTTESTI_CHECK(Context.cEvents == 3);
    if (Context.cEvents == 3)
    {
        RTTESTI_CHECK(Context.aEventStates[0] == ClipboardTransferState_Added);
        RTTESTI_CHECK(Context.aEventStates[1] == ClipboardTransferState_InProgress);
        RTTESTI_CHECK(Context.aEventStates[2] == ClipboardTransferState_Completed);
        for (uint32_t i = 0; i < Context.cEvents; ++i)
            RTTESTI_CHECK(Context.aObjectStates[i] == Context.aEventStates[i]);
    }
    RTTESTI_CHECK(Context.uPercentAtInProgress == 50);
    RTTESTI_CHECK(Context.fCompletedAtTerminal == TRUE);

    /* A terminal service error publishes one validated path and one formatted
     * message consistently through the transfer, event and Progress objects. */
    Context.cEvents = 0;
    Context.fOverflow = false;
    Context.fCompletedAtTerminal = FALSE;
    Context.ptrHeldEvent.setNull();
    Context.ptrHeldTransfer.setNull();
    Context.ptrHeldProgress.setNull();
    ShClTransferKeyInit(&Context.Key, 3, 5, 4);
    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key, NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key, NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED, "dir/denied.bin");
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK_RC(RTSemEventWait(Context.hDone, RT_MS_5SEC), VINF_SUCCESS);

    RTTESTI_CHECK(!Context.fOverflow);
    RTTESTI_CHECK(Context.cEvents == 2);
    if (Context.cEvents == 2)
    {
        RTTESTI_CHECK(Context.aEventStates[0] == ClipboardTransferState_Added);
        RTTESTI_CHECK(Context.aEventStates[1] == ClipboardTransferState_Failed);
        RTTESTI_CHECK(Context.aObjectStates[0] == ClipboardTransferState_Added);
        RTTESTI_CHECK(Context.aObjectStates[1] == ClipboardTransferState_Failed);
    }
    RTTESTI_CHECK(Context.fCompletedAtTerminal == TRUE);
    RTTESTI_CHECK(Context.ptrHeldEvent.isNotNull());
    RTTESTI_CHECK(Context.ptrHeldTransfer.isNotNull());
    RTTESTI_CHECK(Context.ptrHeldProgress.isNotNull());
    if (   Context.ptrHeldEvent.isNotNull()
        && Context.ptrHeldTransfer.isNotNull()
        && Context.ptrHeldProgress.isNotNull())
    {
        const char *pszExpectedMessage = "Access denied for 'dir/denied.bin'";
        ClipboardTransferState_T enmFailedState = ClipboardTransferState_Added;
        ClipboardError_T enmFailedError = ClipboardError_None;
        com::Bstr bstrFailedMessage;
        hrc = Context.ptrHeldTransfer->COMGETTER(State)(&enmFailedState);
        RTTESTI_CHECK_RC(hrc, S_OK);
        hrc = Context.ptrHeldTransfer->COMGETTER(Error)(&enmFailedError);
        RTTESTI_CHECK_RC(hrc, S_OK);
        hrc = Context.ptrHeldTransfer->COMGETTER(Message)(bstrFailedMessage.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK(enmFailedState == ClipboardTransferState_Failed);
        RTTESTI_CHECK(enmFailedError == ClipboardError_AccessDenied);
        RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrFailedMessage).c_str(), pszExpectedMessage));

        ClipboardError_T enmEventError = ClipboardError_None;
        com::Bstr bstrEventPath;
        com::Bstr bstrEventMessage;
        hrc = Context.ptrHeldEvent->COMGETTER(Path)(bstrEventPath.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        hrc = Context.ptrHeldEvent->COMGETTER(Message)(bstrEventMessage.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        hrc = Context.ptrHeldEvent->COMGETTER(Error)(&enmEventError);
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrEventPath).c_str(), "dir/denied.bin"));
        RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrEventMessage).c_str(), pszExpectedMessage));
        RTTESTI_CHECK(enmEventError == ClipboardError_AccessDenied);

        LONG hrcFailedProgress = S_OK;
        hrc = Context.ptrHeldProgress->COMGETTER(ResultCode)(&hrcFailedProgress);
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK((HRESULT)hrcFailedProgress == VBOX_E_SHCL_ACCESS_DENIED);
        ComPtr<IVirtualBoxErrorInfo> ptrErrorInfo;
        hrc = Context.ptrHeldProgress->COMGETTER(ErrorInfo)(ptrErrorInfo.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK(ptrErrorInfo.isNotNull());
        if (ptrErrorInfo.isNotNull())
        {
            LONG vrcDetail = VINF_SUCCESS;
            com::Bstr bstrErrorText;
            hrc = ptrErrorInfo->COMGETTER(ResultDetail)(&vrcDetail);
            RTTESTI_CHECK_RC(hrc, S_OK);
            hrc = ptrErrorInfo->COMGETTER(Text)(bstrErrorText.asOutParam());
            RTTESTI_CHECK_RC(hrc, S_OK);
            RTTESTI_CHECK(vrcDetail == VERR_ACCESS_DENIED);
            RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrErrorText).c_str(), pszExpectedMessage));
        }
    }

    /* Display-control characters make an otherwise valid UTF-8 path unsafe to
     * show.  Keep the exact transfer error but publish it without that path. */
    Context.cEvents = 0;
    Context.fOverflow = false;
    Context.fCompletedAtTerminal = FALSE;
    Context.ptrHeldEvent.setNull();
    Context.ptrHeldTransfer.setNull();
    Context.ptrHeldProgress.setNull();
    ShClTransferKeyInit(&Context.Key, 3, 6, 4);
    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key, NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    hrc = ptrManagerObj->i_handleTransferStatus(&Context.Key, NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED,
                                                "dir/\xe2\x80\x8ehidden.bin" /* U+200E */);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK_RC(RTSemEventWait(Context.hDone, RT_MS_5SEC), VINF_SUCCESS);

    RTTESTI_CHECK(!Context.fOverflow);
    RTTESTI_CHECK(Context.cEvents == 2);
    RTTESTI_CHECK(Context.ptrHeldEvent.isNotNull());
    RTTESTI_CHECK(Context.ptrHeldTransfer.isNotNull());
    if (Context.ptrHeldEvent.isNotNull() && Context.ptrHeldTransfer.isNotNull())
    {
        com::Bstr bstrEventPath;
        com::Bstr bstrEventMessage;
        com::Bstr bstrTransferMessage;
        hrc = Context.ptrHeldEvent->COMGETTER(Path)(bstrEventPath.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        hrc = Context.ptrHeldEvent->COMGETTER(Message)(bstrEventMessage.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        hrc = Context.ptrHeldTransfer->COMGETTER(Message)(bstrTransferMessage.asOutParam());
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK(com::Utf8Str(bstrEventPath).isEmpty());
        RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrEventMessage).c_str(), "Access denied"));
        RTTESTI_CHECK(!RTStrCmp(com::Utf8Str(bstrTransferMessage).c_str(), "Access denied"));
    }

    hrc = ptrEventSource->UnregisterListener(ptrListener);
    RTTESTI_CHECK_RC(hrc, S_OK);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    /* Manager teardown may reenter an active event listener.  Publications
     * accepted before teardown must drain first, with Removed/E_ABORT ordered
     * last so a queued InProgress state cannot resurrect the transfer. */
    ClipboardTransferReentryContext TeardownContext;
    TeardownContext.pManager = ptrManagerObj;
    ShClTransferKeyInit(&TeardownContext.Key, 4, 5, 4);
    TeardownContext.fUninitOnAdded = true;

    ComObjPtr<ClipboardTransferReentryListenerImpl> ptrTeardownListenerObj;
    hrc = ptrTeardownListenerObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrTeardownListenerObj->init(new ClipboardTransferReentryListener(), &TeardownContext);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    ComPtr<IEventListener> ptrTeardownListener;
    hrc = ptrTeardownListenerObj.queryInterfaceTo(ptrTeardownListener.asOutParam());
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrEventSource->RegisterListener(ptrTeardownListener, ComSafeArrayAsInParam(aEventTypes), TRUE /* aActive */);
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

    hrc = ptrManagerObj->i_handleTransferStatus(&TeardownContext.Key, NULL /* pTransfer */,
                                                SHCLSOURCE_REMOTE, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK_RC(RTSemEventWait(TeardownContext.hDone, RT_MS_5SEC), VINF_SUCCESS);
    RTTESTI_CHECK(TeardownContext.fReentered);
    RTTESTI_CHECK_RC(TeardownContext.hrcReentry, S_OK);
    RTTESTI_CHECK(TeardownContext.cDepth == 0);
    RTTESTI_CHECK(TeardownContext.cMaxDepth == 1);
    RTTESTI_CHECK(TeardownContext.cEvents == 1);
    RTTESTI_CHECK(TeardownContext.ptrHeldTransfer.isNotNull());
    RTTESTI_CHECK(TeardownContext.ptrHeldProgress.isNotNull());
    if (TeardownContext.ptrHeldTransfer.isNotNull() && TeardownContext.ptrHeldProgress.isNotNull())
    {
        RTTESTI_CHECK(tstClipboardTransferWaitCompleted(TeardownContext.ptrHeldProgress));
        RTTESTI_CHECK(tstClipboardTransferWaitWorkerStopped(ptrManagerObj));
        hrc = TeardownContext.ptrHeldTransfer->COMGETTER(State)(&enmTransferState);
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK(enmTransferState == ClipboardTransferState_Removed);
        fCompleted = FALSE;
        hrc = TeardownContext.ptrHeldProgress->COMGETTER(Completed)(&fCompleted);
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK(fCompleted == TRUE);
        LONG hrcResult = S_OK;
        hrc = TeardownContext.ptrHeldProgress->COMGETTER(ResultCode)(&hrcResult);
        RTTESTI_CHECK_RC(hrc, S_OK);
        RTTESTI_CHECK((HRESULT)hrcResult == E_ABORT);
    }
    hrc = ptrEventSource->UnregisterListener(ptrTeardownListener);
    RTTESTI_CHECK_RC(hrc, S_OK);

    /* A normal caller owns no waitable RTTHREAD handle.  Ordinary teardown,
     * suppressed worker signals and teardown with an assigned service
     * publication must all release the completion semaphore safely. */
    for (uint32_t i = 0; i < 3; ++i)
    {
        ComObjPtr<ClipboardTransferManager> ptrCleanupManager;
        hrc = ptrCleanupManager.createObject();
        RTTESTI_CHECK_RC(hrc, S_OK);
        if (FAILED(hrc))
            break;
        hrc = ptrCleanupManager->init(ptrEventSource);
        RTTESTI_CHECK_RC(hrc, S_OK);
        if (FAILED(hrc))
            break;
        if (i != 0)
            ptrCleanupManager->i_setPublicationWorkerSignalsSuppressed(true);
        if (i == 2)
        {
            SHCLTRANSFERKEY CleanupKey;
            ShClTransferKeyInit(&CleanupKey, ShClTransferKeyGetSessionId(&Key) + 1,
                                ShClTransferKeyGetTransferId(&Key), Key.uGeneration);
            hrc = ptrCleanupManager->i_handleTransferStatus(&CleanupKey,
                                                             NULL /* pTransfer */, SHCLSOURCE_REMOTE,
                                                             SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
            RTTESTI_CHECK_RC(hrc, S_OK);
        }
        ptrCleanupManager->uninit();
        RTTESTI_CHECK(tstClipboardTransferWaitWorkerStopped(ptrCleanupManager));
    }
}
#endif


int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardAPI", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

#ifdef RT_OS_WINDOWS
    /* The in-process Main objects require one ATL module in this linking namespace. */
    new ATL::CComModule;
#endif

    tstClipboardValues();
    tstClipboardSession();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    tstClipboardTransfer();
    tstClipboardTransferManager();
#endif
    return RTTestSummaryAndDestroy(hTest);
}
