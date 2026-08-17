/* $Id: tstClipboardAPI.cpp 115056 2026-08-17 16:44:52Z andreas.loeffler@oracle.com $ */
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
#endif

#include <iprt/string.h>
#include <iprt/test.h>


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
HRESULT Clipboard::i_transferCancel(SHCLSESSIONID, SHCLTRANSFERID, SHCLTRANSFERGEN)
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

    ComObjPtr<ClipboardTransferManager> ptrManagerObj;
    HRESULT hrc = ptrManagerObj.createObject();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;
    hrc = ptrManagerObj->init();
    RTTESTI_CHECK_RC(hrc, S_OK);
    if (FAILED(hrc))
        return;

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

    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_ToGuest, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 1);
    if (aTransfers.size() == 1)
        RTTESTI_CHECK(aTransfers[0] == ptrTransfer);

    hrc = ptrManager->Remove(ptrTransfer);
    RTTESTI_CHECK_RC(hrc, S_OK);
    aTransfers.setNull();
    hrc = ptrManager->GetTransfers(ClipboardTransferDirection_Any, 0, ComSafeArrayAsOutParam(aTransfers));
    RTTESTI_CHECK_RC(hrc, S_OK);
    RTTESTI_CHECK(aTransfers.size() == 0);

    RTTESTI_CHECK_RC(RTTestIDisableAssertions(), VINF_SUCCESS);
    hrc = ptrManager->Remove(ptrTransfer);
    RTTESTI_CHECK_RC(RTTestIRestoreAssertions(), VINF_SUCCESS);
    RTTESTI_CHECK(FAILED(hrc));
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

    tstClipboardValues();
    tstClipboardSession();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    tstClipboardTransfer();
    tstClipboardTransferManager();
#endif
    return RTTestSummaryAndDestroy(hTest);
}
