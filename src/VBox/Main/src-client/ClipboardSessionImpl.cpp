/* $Id: ClipboardSessionImpl.cpp 115056 2026-08-17 16:44:52Z andreas.loeffler@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include "LoggingNew.h"

#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "ClipboardImpl.h"
#include "ClipboardSessionImpl.h"
#include "EventImpl.h"
#include "HostClipboardImpl.h"

#include "VBoxEvents.h"

#include <VBox/com/listeners.h>

#include <iprt/errcore.h>


/**
 * Internal listener used to detect the first external listener registration on
 * a session event source.
 */
class ClipboardSessionEventSourceListener
{
public:
    ClipboardSessionEventSourceListener()
        : mSession(NULL)
    { }

    HRESULT init(ClipboardSession *aSession)
    {
        AssertPtrReturn(aSession, E_INVALIDARG);
        mSession = aSession;
        return S_OK;
    }

    void uninit()
    {
        mSession = NULL;
    }

    virtual ~ClipboardSessionEventSourceListener()
    { }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        if (aType != VBoxEventType_OnEventSourceChanged)
            return S_OK;

        ComPtr<IEventSourceChangedEvent> ptrChanged = aEvent;
        if (ptrChanged.isNull())
            return S_OK;

        BOOL fAdded = FALSE;
        HRESULT hrc = ptrChanged->COMGETTER(Add)(&fAdded);
        AssertComRCReturnRC(hrc);

        ComPtr<IEventListener> ptrListener;
        hrc = ptrChanged->COMGETTER(Listener)(ptrListener.asOutParam());
        AssertComRCReturnRC(hrc);

        ClipboardSession *pSession = mSession;
        if (pSession)
            return pSession->i_onEventSourceChanged(ptrListener, fAdded);
        return S_OK;
    }

private:
    ClipboardSession *mSession;
};

typedef ListenerImpl<ClipboardSessionEventSourceListener, ClipboardSession *> ClipboardSessionEventSourceListenerImpl;

VBOX_LISTENER_DECLARE(ClipboardSessionEventSourceListenerImpl)


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(ClipboardSession)


/**
 * Completes construction of a clipboard session object.
 *
 * @returns COM status code.
 */
HRESULT ClipboardSession::FinalConstruct()
{
    return BaseFinalConstruct();
}


/**
 * Releases a clipboard session object.
 */
void ClipboardSession::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


/**
 * Initializes a clipboard session object.
 *
 * @returns COM status code.
 * @param   aClientId       Main clipboard client ID this session is associated with.
 * @param   fFlags          IClipboardSessionFlag mask.
 * @param   aParent         Parent clipboard object.
 */
HRESULT ClipboardSession::init(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFlags, Clipboard *aParent)
{
    Log2Func(("aClientId=%RU32, fFlags=%#x, aParent=%p\n", aClientId, fFlags, aParent));
    AssertReturn(aClientId != VBOX_SHCL_MAIN_CLIENT_NONE, E_INVALIDARG);
    AssertPtrReturn(aParent, E_INVALIDARG);

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData.mClientId = aClientId;
    mData.mfFlags = fFlags;
    mData.mParent = aParent;
    mData.mfInitialStateDelivered = false;

    HRESULT hrc = mData.mEventSource.createObject();
    AssertComRCReturnRC(hrc);
    hrc = mData.mEventSource->init();
    AssertComRCReturnRC(hrc);

    hrc = mData.mHostClipboard.createObject();
    AssertComRCReturnRC(hrc);
    hrc = mData.mHostClipboard->init(aClientId, aParent);
    AssertComRCReturnRC(hrc);

    ComPtr<IEventSource> ptrEventSource;
    hrc = mData.mEventSource.queryInterfaceTo(ptrEventSource.asOutParam());
    AssertComRCReturnRC(hrc);

    if (fFlags & IClipboardSessionFlag_IncludeInitialState)
    {
        ComObjPtr<ClipboardSessionEventSourceListenerImpl> ptrListener;
        hrc = ptrListener.createObject();
        AssertComRCReturnRC(hrc);
        hrc = ptrListener->init(new ClipboardSessionEventSourceListener(), this);
        AssertComRCReturnRC(hrc);
        mData.mEventSourceChangedListener = ptrListener;

        com::SafeArray<VBoxEventType_T> aEventTypes;
        aEventTypes.push_back(VBoxEventType_OnEventSourceChanged);
        hrc = mData.mEventSource->RegisterListener(mData.mEventSourceChangedListener,
                                                   ComSafeArrayAsInParam(aEventTypes),
                                                   TRUE /* aActive */);
        AssertComRCReturnRC(hrc);
    }

    hrc = aParent->i_registerSession(aClientId, this, fFlags, ptrEventSource);
    AssertComRCReturnRC(hrc);

    autoInitSpan.setSucceeded();
    return S_OK;
}


#ifdef UNIT_TEST
/**
 * Initializes a parentless clipboard session for unit testing.
 *
 * @returns COM status code.
 * @param   aClientId       Main clipboard client ID this session represents.
 * @param   fFlags          IClipboardSessionFlag mask.
 */
HRESULT ClipboardSession::initForTesting(VBOXSHCLMAINCLIENTID aClientId, uint32_t fFlags)
{
    AssertReturn(aClientId != VBOX_SHCL_MAIN_CLIENT_NONE, E_INVALIDARG);

    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData.mClientId = aClientId;
    mData.mfFlags = fFlags;
    mData.mParent = NULL;
    mData.mfInitialStateDelivered = false;

    HRESULT hrc = mData.mEventSource.createObject();
    AssertComRCReturnRC(hrc);
    hrc = mData.mEventSource->init();
    AssertComRCReturnRC(hrc);

    autoInitSpan.setSucceeded();
    return S_OK;
}
#endif


/**
 * Uninitializes a clipboard session object.
 */
void ClipboardSession::uninit()
{
    Log3Func(("id=%RU32\n", mData.mClientId));
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    VBOXSHCLMAINCLIENTID idClient = VBOX_SHCL_MAIN_CLIENT_NONE;
    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    ComObjPtr<HostClipboard> ptrHostClipboard;
    ComObjPtr<EventSource> ptrEventSource;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        idClient = mData.mClientId;
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
        mData.mParent = NULL;
        ptrHostClipboard = mData.mHostClipboard;
        mData.mHostClipboard.setNull();
        ptrEventSource = mData.mEventSource;
        mData.mEventSource.setNull();
        mData.mEventSourceChangedListener.setNull();
        mData.mfFlags = 0;
        mData.mfInitialStateDelivered = false;
    }

    if (pParent && idClient != VBOX_SHCL_MAIN_CLIENT_NONE)
    {
        if (SUCCEEDED(autoCaller.hrc()))
            pParent->i_unregisterSession(idClient);
        else
            LogFunc(("Cannot unregister clipboard session %RU32: hrc=%#x\n", idClient, autoCaller.hrc()));
    }
    if (!ptrHostClipboard.isNull())
        ptrHostClipboard->uninit();
    if (!ptrEventSource.isNull())
        ptrEventSource->uninit();
}


/**
 * Returns the clipboard session client identifier.
 *
 * @returns COM status code.
 * @param   aId             Where to return the session client identifier.
 */
HRESULT ClipboardSession::getId(ULONG *aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aId = mData.mClientId;
    Log3Func(("aId=%RU32\n", (uint32_t)*aId));
    return S_OK;
}


/**
 * Returns the session-specific event source.
 *
 * @returns COM status code.
 * @param   aEventSource    Where to return the event source.
 */
HRESULT ClipboardSession::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData.mEventSource.isNull())
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session object is not initialized"));
    HRESULT hrc = mData.mEventSource.queryInterfaceTo(aEventSource.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the clipboard session event source failed (%Rhrc)"), hrc);
    return hrc;
}


/**
 * Returns the session-specific native host clipboard endpoint.
 *
 * @returns COM status code.
 * @param   aHostClipboard  Where to return the host clipboard endpoint.
 */
HRESULT ClipboardSession::getHostClipboard(ComPtr<IHostClipboard> &aHostClipboard)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData.mHostClipboard.isNull())
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session object is not initialized"));
    HRESULT hrc = mData.mHostClipboard.queryInterfaceTo(aHostClipboard.asOutParam());
    if (FAILED(hrc))
        return setError(VBOX_E_SHCL_ERROR, tr("Getting the clipboard session host clipboard endpoint failed (%Rhrc)"), hrc);
    return hrc;
}


/**
 * Handles session event source listener changes.
 *
 * @returns COM status code.
 * @param   aListener       Listener which was added or removed.
 * @param   fAdded          Whether the listener was added.
 */
HRESULT ClipboardSession::i_onEventSourceChanged(const ComPtr<IEventListener> &aListener, BOOL fAdded)
{
    if (!fAdded)
        return S_OK;

    VBOXSHCLMAINCLIENTID idClient = VBOX_SHCL_MAIN_CLIENT_NONE;
    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (mData.mEventSourceChangedListener == aListener)
            return S_OK;
        if (!(mData.mfFlags & IClipboardSessionFlag_IncludeInitialState))
            return S_OK;
        idClient = mData.mClientId;
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
    }
    if (!pParent)
        return S_OK;
    AssertReturn(idClient != VBOX_SHCL_MAIN_CLIENT_NONE, E_FAIL);
    AssertComRCReturnRC(autoCaller.hrc());

    return pParent->i_fireSessionInitialState(idClient);
}


/**
 * Reads the currently available clipboard formats.
 *
 * @returns COM status code.
 * @param   aFormats        Where to return the available formats.
 */
HRESULT ClipboardSession::readFormats(std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aFormats);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
    }
    if (!pParent)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session object is not initialized"));
    AssertComRCReturnRC(autoCaller.hrc());

    return pParent->i_readFormatObjects(aFormats);
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
HRESULT ClipboardSession::readDataRaw(ClipboardAction_T aAction,
                                      const com::Utf8Str &aRequestedMimeType,
                                      ClipboardSource_T *aSource,
                                      com::Utf8Str &aMimeType,
                                      std::vector<BYTE> &aBuffer)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aAction, aRequestedMimeType, aSource, aMimeType, aBuffer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
    }
    if (!pParent)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session object is not initialized"));
    AssertComRCReturnRC(autoCaller.hrc());

    return pParent->i_readDataRaw(aAction, aRequestedMimeType, aSource, aMimeType, aBuffer);
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Writes raw clipboard data using the session client identifier.
 *
 * @returns COM status code.
 * @param   aAction             Clipboard action to write for.
 * @param   aSource             Clipboard source.
 * @param   aMimeType           MIME type of the payload.
 * @param   aBuffer             Payload bytes.
 * @param   aWrittenSource      Where to return the written clipboard source.
 * @param   aWrittenMimeType    Where to return the written MIME type.
 * @param   aWrittenBuffer      Where to return the written payload bytes.
 */
HRESULT ClipboardSession::writeDataRaw(ClipboardAction_T aAction,
                                       ClipboardSource_T aSource,
                                       const com::Utf8Str &aMimeType,
                                       const std::vector<BYTE> &aBuffer,
                                       ClipboardSource_T *aWrittenSource,
                                       com::Utf8Str &aWrittenMimeType,
                                       std::vector<BYTE> &aWrittenBuffer)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aAction, aSource, aMimeType, aBuffer, aWrittenSource, aWrittenMimeType, aWrittenBuffer);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    VBOXSHCLMAINCLIENTID idClient = VBOX_SHCL_MAIN_CLIENT_NONE;
    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        idClient = mData.mClientId;
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
    }
    if (!pParent)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session object is not initialized"));
    AssertReturn(idClient != VBOX_SHCL_MAIN_CLIENT_NONE, E_FAIL);
    AssertComRCReturnRC(autoCaller.hrc());

    return pParent->i_writeDataRaw(idClient, aAction, aSource, aMimeType, aBuffer,
                                   aWrittenSource, aWrittenMimeType, aWrittenBuffer);
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Writes available clipboard formats using the session client identifier.
 *
 * @returns COM status code.
 * @param   aFormats        Clipboard formats to write.
 */
HRESULT ClipboardSession::writeFormats(const std::vector<ComPtr<IClipboardFormat> > &aFormats)
{
#ifndef VBOX_WITH_SHARED_CLIPBOARD
    RT_NOREF(aFormats);
    ReturnComNotImplemented();
#else /* VBOX_WITH_SHARED_CLIPBOARD */

    VBOXSHCLMAINCLIENTID idClient = VBOX_SHCL_MAIN_CLIENT_NONE;
    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        idClient = mData.mClientId;
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
    }
    if (!pParent)
        return setError(VBOX_E_SHCL_ERROR, tr("Clipboard session object is not initialized"));
    AssertReturn(idClient != VBOX_SHCL_MAIN_CLIENT_NONE, E_FAIL);
    AssertComRCReturnRC(autoCaller.hrc());

    return pParent->i_writeFormatObjects(idClient, aFormats);
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
}


/**
 * Closes the clipboard session.
 *
 * @returns COM status code.
 */
HRESULT ClipboardSession::close()
{
    Log2Func(("id=%RU32\n", mData.mClientId));

    VBOXSHCLMAINCLIENTID idClient = VBOX_SHCL_MAIN_CLIENT_NONE;
    Clipboard *pParent = NULL;
    AutoCaller autoCaller;
    ComObjPtr<HostClipboard> ptrHostClipboard;
    ComObjPtr<EventSource> ptrEventSource;
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        idClient = mData.mClientId;
        pParent = mData.mParent;
        if (pParent)
            autoCaller.attach(pParent);
        mData.mParent = NULL;
        ptrHostClipboard = mData.mHostClipboard;
        mData.mHostClipboard.setNull();
        ptrEventSource = mData.mEventSource;
        mData.mEventSource.setNull();
        mData.mEventSourceChangedListener.setNull();
        mData.mfFlags = 0;
        mData.mfInitialStateDelivered = false;
    }

    if (pParent && idClient != VBOX_SHCL_MAIN_CLIENT_NONE)
    {
        if (SUCCEEDED(autoCaller.hrc()))
            pParent->i_unregisterSession(idClient);
        else
            LogFunc(("Cannot unregister clipboard session %RU32: hrc=%#x\n", idClient, autoCaller.hrc()));
    }
    if (!ptrHostClipboard.isNull())
        ptrHostClipboard->uninit();
    if (!ptrEventSource.isNull())
        ptrEventSource->uninit();
    return S_OK;
}
