/* $Id: GuestFileImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest file handling.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_GUESTFILE
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestFileImpl.h"
#include "GuestSessionImpl.h"
#include "GuestCtrlImplPrivate.h"
#include "ConsoleImpl.h"
#include "VirtualBoxErrorInfoImpl.h"

#include "Global.h"
#include "AutoCaller.h"
#include "VBoxEvents.h"

#include <iprt/cpp/utils.h> /* For unconst(). */
#include <iprt/file.h>

#include <VBox/com/array.h>
#include <VBox/com/listeners.h>
#include <VBox/AssertGuest.h>


/**
 * Internal listener class to serve events in an
 * active manner, e.g. without polling delays.
 */
class GuestFileListener
{
public:

    GuestFileListener(void)
    {
    }

    virtual ~GuestFileListener()
    {
    }

    HRESULT init(GuestFile *pFile)
    {
        AssertPtrReturn(pFile, E_POINTER);
        mFile = pFile;
        return S_OK;
    }

    void uninit(void)
    {
        mFile = NULL;
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnGuestFileStateChanged:
            case VBoxEventType_OnGuestFileOffsetChanged:
            case VBoxEventType_OnGuestFileRead:
            case VBoxEventType_OnGuestFileWrite:
            {
                AssertPtrReturn(mFile, E_POINTER);
                int vrc2 = mFile->signalWaitEvent(aType, aEvent);
                RT_NOREF(vrc2);
#ifdef DEBUG_andy
                LogFlowFunc(("Signalling events of type=%RU32, file=%p resulted in vrc=%Rrc\n",
                             aType, mFile, vrc2));
#endif
                break;
            }

            default:
                AssertMsgFailed(("Unhandled event %RU32\n", aType));
                break;
        }

        return S_OK;
    }

private:

    /** Weak pointer to the guest file object to listen for. */
    GuestFile *mFile;
};
typedef ListenerImpl<GuestFileListener, GuestFile*> GuestFileListenerImpl;

VBOX_LISTENER_DECLARE(GuestFileListenerImpl)

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestFile)

HRESULT GuestFile::FinalConstruct(void)
{
    LogFlowThisFuncEnter();
    return BaseFinalConstruct();
}

void GuestFile::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes a file object but does *not* open the file on the guest
 * yet. This is done in the dedidcated openFile call.
 *
 * @return  IPRT status code.
 * @param   pConsole                Pointer to console object.
 * @param   pSession                Pointer to session object.
 * @param   aObjectID               The object's ID.
 * @param   openInfo                File opening information.
 */
int GuestFile::init(Console *pConsole, GuestSession *pSession,
                    ULONG aObjectID, const GuestFileOpenInfo &openInfo)
{
    LogFlowThisFunc(("pConsole=%p, pSession=%p, aObjectID=%RU32, strPath=%s\n",
                     pConsole, pSession, aObjectID, openInfo.mFilename.c_str()));

    AssertPtrReturn(pConsole, VERR_INVALID_POINTER);
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    int vrc = bindToSession(pConsole, pSession, aObjectID);
    if (RT_SUCCESS(vrc))
    {
        mSession = pSession;

        mData.mOpenInfo    = openInfo;
        mData.mInitialSize = 0;
        mData.mStatus      = FileStatus_Undefined;
        mData.mLastError   = VINF_SUCCESS;
        mData.mOffCurrent  = 0;

        unconst(mEventSource).createObject();
        HRESULT hr = mEventSource->init();
        if (FAILED(hr))
            vrc = VERR_COM_UNEXPECTED;
    }

    if (RT_SUCCESS(vrc))
    {
        try
        {
            GuestFileListener *pListener = new GuestFileListener();
            ComObjPtr<GuestFileListenerImpl> thisListener;
            HRESULT hr = thisListener.createObject();
            if (SUCCEEDED(hr))
                hr = thisListener->init(pListener, this);

            if (SUCCEEDED(hr))
            {
                com::SafeArray <VBoxEventType_T> eventTypes;
                eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
                eventTypes.push_back(VBoxEventType_OnGuestFileOffsetChanged);
                eventTypes.push_back(VBoxEventType_OnGuestFileRead);
                eventTypes.push_back(VBoxEventType_OnGuestFileWrite);
                hr = mEventSource->RegisterListener(thisListener,
                                                    ComSafeArrayAsInParam(eventTypes),
                                                    TRUE /* Active listener */);
                if (SUCCEEDED(hr))
                {
                    vrc = baseInit();
                    if (RT_SUCCESS(vrc))
                    {
                        mLocalListener = thisListener;
                    }
                }
                else
                    vrc = VERR_COM_UNEXPECTED;
            }
            else
                vrc = VERR_COM_UNEXPECTED;
        }
        catch(std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        /* Confirm a successful initialization when it's the case. */
        autoInitSpan.setSucceeded();
    }
    else
        autoInitSpan.setFailed();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestFile::uninit(void)
{
    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFuncEnter();

    baseUninit();
    LogFlowThisFuncLeave();
}

// implementation of public getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestFile::getCreationMode(ULONG *aCreationMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aCreationMode = mData.mOpenInfo.mCreationMode;

    return S_OK;
}

HRESULT GuestFile::getOpenAction(FileOpenAction_T *aOpenAction)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aOpenAction = mData.mOpenInfo.mOpenAction;

    return S_OK;
}

HRESULT GuestFile::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    /* No need to lock - lifetime constant. */
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());

    return S_OK;
}

HRESULT GuestFile::getFilename(com::Utf8Str &aFilename)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFilename = mData.mOpenInfo.mFilename;

    return S_OK;
}

HRESULT GuestFile::getId(ULONG *aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aId = mObjectID;

    return S_OK;
}

HRESULT GuestFile::getInitialSize(LONG64 *aInitialSize)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aInitialSize = mData.mInitialSize;

    return S_OK;
}

HRESULT GuestFile::getOffset(LONG64 *aOffset)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * This is updated by GuestFile::i_onFileNotify() when read, write and seek
     * confirmation messages are recevied.
     *
     * Note! This will not be accurate with older (< 5.2.32, 6.0.0 - 6.0.9)
     *       Guest Additions when using writeAt, readAt or writing to a file
     *       opened in append mode.
     */
    *aOffset = mData.mOffCurrent;

    return S_OK;
}

HRESULT GuestFile::getAccessMode(FileAccessMode_T *aAccessMode)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAccessMode = mData.mOpenInfo.mAccessMode;

    return S_OK;
}

HRESULT GuestFile::getStatus(FileStatus_T *aStatus)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aStatus = mData.mStatus;

    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Entry point for guest side file callbacks.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCb              Host callback data.
 */
int GuestFile::i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    LogFlowThisFunc(("strName=%s, uContextID=%RU32, uFunction=%RU32, pSvcCb=%p\n",
                     mData.mOpenInfo.mFilename.c_str(), pCbCtx->uContextID, pCbCtx->uMessage, pSvcCb));

    int vrc;
    switch (pCbCtx->uMessage)
    {
        case GUEST_MSG_DISCONNECTED:
            vrc = i_onGuestDisconnected(pCbCtx, pSvcCb);
            break;

        case GUEST_MSG_FILE_NOTIFY:
            vrc = i_onNotify(pCbCtx, pSvcCb);
            break;

        default:
            /* Silently ignore not implemented functions. */
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

#ifdef DEBUG
    LogFlowFuncLeaveRC(vrc);
#endif
    return vrc;
}

/**
 * Closes the file on the guest side and unregisters it.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   prcGuest            Where to return the guest error when VERR_GSTCTL_GUEST_ERROR
 *                              was returned.
 */
int GuestFile::i_close(int *prcGuest)
{
    LogFlowThisFunc(("strFile=%s\n", mData.mOpenInfo.mFilename.c_str()));

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[4];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mObjectID /* Guest file ID */);

    vrc = sendMessage(HOST_MSG_FILE_CLOSE, i, paParms);
    if (RT_SUCCESS(vrc))
        vrc = i_waitForStatusChange(pEvent, GSTCTL_DEFAULT_TIMEOUT_MS, NULL /* FileStatus */, prcGuest);
    unregisterWaitEvent(pEvent);

    /* Unregister the file object from the guest session. */
    AssertPtr(mSession);
    int vrc2 = mSession->i_fileUnregister(this);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Converts a given guest file error to a string.
 *
 * @returns Error string.
 * @param   rcGuest             Guest file error to return string for.
 * @param   pcszWhat            Hint of what was involved when the error occurred.
 */
/* static */
Utf8Str GuestFile::i_guestErrorToString(int rcGuest, const char *pcszWhat)
{
    AssertPtrReturn(pcszWhat, "");

    Utf8Str strErr;
    switch (rcGuest)
    {
#define CASE_MSG(a_iRc, ...) \
        case a_iRc: strErr.printf(__VA_ARGS__); break;
        CASE_MSG(VERR_ACCESS_DENIED     , tr("Access to guest file \"%s\" denied"), pcszWhat);
        CASE_MSG(VERR_ALREADY_EXISTS    , tr("Guest file \"%s\" already exists"), pcszWhat);
        CASE_MSG(VERR_FILE_NOT_FOUND    , tr("Guest file \"%s\" not found"), pcszWhat);
        CASE_MSG(VERR_NET_HOST_NOT_FOUND, tr("Host name \"%s\", not found"), pcszWhat);
        CASE_MSG(VERR_SHARING_VIOLATION , tr("Sharing violation for guest file \"%s\""), pcszWhat);
        default:
            strErr.printf(tr("Error %Rrc for guest file \"%s\" occurred\n"), rcGuest, pcszWhat);
            break;
#undef CASE_MSG
    }

    return strErr;
}

/**
 * Called when the guest side notifies the host of a file event.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 */
int GuestFile::i_onNotify(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    LogFlowThisFuncEnter();

    if (pSvcCbData->mParms < 3)
        return VERR_INVALID_PARAMETER;

    int idx = 1; /* Current parameter index. */
    CALLBACKDATA_FILE_NOTIFY dataCb;
    RT_ZERO(dataCb);
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.uType);
    HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.rc);

    int vrcGuest = (int)dataCb.rc; /* uint32_t vs. int. */

    LogFlowThisFunc(("uType=%RU32, vrcGuest=%Rrc\n", dataCb.uType, vrcGuest));

    if (RT_FAILURE(vrcGuest))
    {
        int vrc2 = i_setFileStatus(FileStatus_Error, vrcGuest);
        AssertRC(vrc2);

        /* Ignore return code, as the event to signal might not be there (anymore). */
        signalWaitEventInternal(pCbCtx, vrcGuest, NULL /* pPayload */);
        return VINF_SUCCESS; /* Report to the guest. */
    }

    AssertMsg(mObjectID == VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(pCbCtx->uContextID),
              ("File ID %RU32 does not match object ID %RU32\n", mObjectID,
               VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(pCbCtx->uContextID)));

    int vrc = VERR_NOT_SUPPORTED; /* Play safe by default. */

    switch (dataCb.uType)
    {
        case GUEST_FILE_NOTIFYTYPE_ERROR:
        {
            vrc = i_setFileStatus(FileStatus_Error, vrcGuest);
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_OPEN:
        {
            if (pSvcCbData->mParms == 4)
            {
                vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.u.open.uHandle);
                if (RT_FAILURE(vrc))
                    break;

                /* Set the process status. */
                vrc = i_setFileStatus(FileStatus_Open, vrcGuest);
            }
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_CLOSE:
        {
            vrc = i_setFileStatus(FileStatus_Closed, vrcGuest);
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_READ:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 4, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_PTR,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);

            vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[idx++], &dataCb.u.read.pvData, &dataCb.u.read.cbData);
            if (RT_FAILURE(vrc))
                break;

            const uint32_t cbRead = dataCb.u.read.cbData;
            Log3ThisFunc(("cbRead=%RU32\n", cbRead));

            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            mData.mOffCurrent += cbRead; /* Bogus for readAt, which is why we've got GUEST_FILE_NOTIFYTYPE_READ_OFFSET. */
            alock.release();

            try
            {
                com::SafeArray<BYTE> data((size_t)cbRead);
                AssertBreakStmt(data.size() == cbRead, vrc = VERR_NO_MEMORY);
                data.initFrom((BYTE *)dataCb.u.read.pvData, cbRead);
                ::FireGuestFileReadEvent(mEventSource, mSession, this, mData.mOffCurrent, cbRead, ComSafeArrayAsInParam(data));
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
            }
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_READ_OFFSET:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 5, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_PTR,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx + 1].type == VBOX_HGCM_SVC_PARM_64BIT,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx + 1].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            BYTE const * const pbData = (BYTE const *)pSvcCbData->mpaParms[idx].u.pointer.addr;
            uint32_t const     cbRead = pSvcCbData->mpaParms[idx].u.pointer.size;
            int64_t            offNew = (int64_t)pSvcCbData->mpaParms[idx + 1].u.uint64;
            Log3ThisFunc(("cbRead=%RU32 offNew=%RI64 (%#RX64)\n", cbRead, offNew, offNew));

            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (offNew < 0) /* non-seekable */
                offNew = mData.mOffCurrent + cbRead;
            mData.mOffCurrent = offNew;
            alock.release();

            try
            {
                com::SafeArray<BYTE> data((size_t)cbRead);
                AssertBreakStmt(data.size() == cbRead, vrc = VERR_NO_MEMORY);
                data.initFrom(pbData, cbRead);
                ::FireGuestFileReadEvent(mEventSource, mSession, this, offNew, cbRead, ComSafeArrayAsInParam(data));
                vrc = VINF_SUCCESS;
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
            }
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_WRITE:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 4, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_32BIT,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);

            uint32_t const cbWritten = pSvcCbData->mpaParms[idx].u.uint32;

            Log3ThisFunc(("cbWritten=%RU32\n", cbWritten));

            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            mData.mOffCurrent += cbWritten; /* Bogus for writeAt and append mode, thus GUEST_FILE_NOTIFYTYPE_WRITE_OFFSET. */
            alock.release();

            ::FireGuestFileWriteEvent(mEventSource, mSession, this, mData.mOffCurrent, cbWritten);
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_WRITE_OFFSET:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 5, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_32BIT,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx + 1].type == VBOX_HGCM_SVC_PARM_64BIT,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            uint32_t const  cbWritten = pSvcCbData->mpaParms[idx].u.uint32;
            int64_t         offNew    = (int64_t)pSvcCbData->mpaParms[idx + 1].u.uint64;
            Log3ThisFunc(("cbWritten=%RU32 offNew=%RI64 (%#RX64)\n", cbWritten, offNew, offNew));

            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            if (offNew < 0) /* non-seekable */
                offNew = mData.mOffCurrent + cbWritten;
            mData.mOffCurrent = offNew;
            alock.release();

            HRESULT hrc2 = ::FireGuestFileWriteEvent(mEventSource, mSession, this, offNew, cbWritten);
            vrc = SUCCEEDED(hrc2) ? VINF_SUCCESS : Global::vboxStatusCodeFromCOM(hrc2);
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_SEEK:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 4, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);

            vrc = HGCMSvcGetU64(&pSvcCbData->mpaParms[idx++], &dataCb.u.seek.uOffActual);
            if (RT_FAILURE(vrc))
                break;

            Log3ThisFunc(("uOffActual=%RU64\n", dataCb.u.seek.uOffActual));

            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
            mData.mOffCurrent = dataCb.u.seek.uOffActual;
            alock.release();

            ::FireGuestFileOffsetChangedEvent(mEventSource, mSession, this, dataCb.u.seek.uOffActual, 0 /* Processed */);
            break;
        }

        case GUEST_FILE_NOTIFYTYPE_TELL:
            /* We don't issue any HOST_MSG_FILE_TELL, so we shouldn't get these notifications! */
            AssertFailed();
            break;

        case GUEST_FILE_NOTIFYTYPE_SET_SIZE:
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 4, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_64BIT,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            dataCb.u.SetSize.cbSize = pSvcCbData->mpaParms[idx].u.uint64;
            Log3ThisFunc(("cbSize=%RU64\n", dataCb.u.SetSize.cbSize));

            ::FireGuestFileSizeChangedEvent(mEventSource, mSession, this, dataCb.u.SetSize.cbSize);
            vrc = VINF_SUCCESS;
            break;

        default:
            break;
    }

    try
    {
        if (RT_SUCCESS(vrc))
        {
            GuestWaitEventPayload payload(dataCb.uType, &dataCb, sizeof(dataCb));

            /* Ignore return code, as the event to signal might not be there (anymore). */
            signalWaitEventInternal(pCbCtx, vrcGuest, &payload);
        }
        else /* OOM situation, wrong HGCM parameters or smth. not expected. */
        {
            /* Ignore return code, as the event to signal might not be there (anymore). */
            signalWaitEventInternalEx(pCbCtx, vrc, 0 /* guestRc */, NULL /* pPayload */);
        }
    }
    catch (int vrcEx) /* Thrown by GuestWaitEventPayload constructor. */
    {
        /* Also try to signal the waiter, to let it know of the OOM situation.
         * Ignore return code, as the event to signal might not be there (anymore). */
        signalWaitEventInternalEx(pCbCtx, vrcEx, 0 /* guestRc */, NULL /* pPayload */);
        vrc = vrcEx;
    }

    LogFlowThisFunc(("uType=%RU32, rcGuest=%Rrc, vrc=%Rrc\n", dataCb.uType, vrcGuest, vrc));
    return vrc;
}

/**
 * Called when the guest side of the file has been disconnected (closed, terminated, +++).
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 */
int GuestFile::i_onGuestDisconnected(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    int vrc = i_setFileStatus(FileStatus_Down, VINF_SUCCESS);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc GuestObject::i_onUnregister
 */
int GuestFile::i_onUnregister(void)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;

    /*
     * Note: The event source stuff holds references to this object,
     *       so make sure that this is cleaned up *before* calling uninit().
     */
    if (!mEventSource.isNull())
    {
        mEventSource->UnregisterListener(mLocalListener);

        mLocalListener.setNull();
        unconst(mEventSource).setNull();
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc GuestObject::i_onSessionStatusChange
 */
int GuestFile::i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus)
{
    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    /* If the session now is in a terminated state, set the file status
     * to "down", as there is not much else we can do now. */
    if (GuestSession::i_isTerminated(enmSessionStatus))
        vrc = i_setFileStatus(FileStatus_Down, 0 /* fileRc, ignored */);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Opens the file on the guest.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   prcGuest            Where to return the guest error when VERR_GSTCTL_GUEST_ERROR
 *                              was returned. Optional.
 */
int GuestFile::i_open(uint32_t uTimeoutMS, int *prcGuest)
{
    AssertReturn(mData.mOpenInfo.mFilename.isNotEmpty(), VERR_INVALID_PARAMETER);

    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("strFile=%s, enmAccessMode=%d, enmOpenAction=%d, uCreationMode=%o, mfOpenEx=%#x\n",
                     mData.mOpenInfo.mFilename.c_str(), mData.mOpenInfo.mAccessMode, mData.mOpenInfo.mOpenAction,
                     mData.mOpenInfo.mCreationMode, mData.mOpenInfo.mfOpenEx));

    /* Validate and translate open action. */
    const char *pszOpenAction = NULL;
    switch (mData.mOpenInfo.mOpenAction)
    {
        case FileOpenAction_OpenExisting:          pszOpenAction = "oe"; break;
        case FileOpenAction_OpenOrCreate:          pszOpenAction = "oc"; break;
        case FileOpenAction_CreateNew:             pszOpenAction = "ce"; break;
        case FileOpenAction_CreateOrReplace:       pszOpenAction = "ca"; break;
        case FileOpenAction_OpenExistingTruncated: pszOpenAction = "ot"; break;
        case FileOpenAction_AppendOrCreate:
            pszOpenAction = "oa"; /** @todo get rid of this one and implement AppendOnly/AppendRead. */
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }

    /* Validate and translate access mode. */
    const char *pszAccessMode = NULL;
    switch (mData.mOpenInfo.mAccessMode)
    {
        case FileAccessMode_ReadOnly:   pszAccessMode = "r";  break;
        case FileAccessMode_WriteOnly:  pszAccessMode = "w";  break;
        case FileAccessMode_ReadWrite:  pszAccessMode = "r+"; break;
        case FileAccessMode_AppendOnly: pszAccessMode = "a";  break;
        case FileAccessMode_AppendRead: pszAccessMode = "a+"; break;
        default:                        return VERR_INVALID_PARAMETER;
    }

    /* Validate and translate sharing mode. */
    const char *pszSharingMode = NULL;
    switch (mData.mOpenInfo.mSharingMode)
    {
        case FileSharingMode_All:           pszSharingMode = ""; break;
        case FileSharingMode_Read:          RT_FALL_THRU();
        case FileSharingMode_Write:         RT_FALL_THRU();
        case FileSharingMode_ReadWrite:     RT_FALL_THRU();
        case FileSharingMode_Delete:        RT_FALL_THRU();
        case FileSharingMode_ReadDelete:    RT_FALL_THRU();
        case FileSharingMode_WriteDelete:   return VERR_NOT_IMPLEMENTED;
        default:                            return VERR_INVALID_PARAMETER;
    }

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[8];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetPv(&paParms[i++], (void*)mData.mOpenInfo.mFilename.c_str(),
                 (ULONG)mData.mOpenInfo.mFilename.length() + 1);
    HGCMSvcSetStr(&paParms[i++], pszAccessMode);
    HGCMSvcSetStr(&paParms[i++], pszOpenAction);
    HGCMSvcSetStr(&paParms[i++], pszSharingMode);
    HGCMSvcSetU32(&paParms[i++], mData.mOpenInfo.mCreationMode);
    HGCMSvcSetU64(&paParms[i++], 0 /*unused offset*/);
    /** @todo Next protocol version: add flags, replace strings, remove initial offset. */

    alock.release(); /* Drop write lock before sending. */

    vrc = sendMessage(HOST_MSG_FILE_OPEN, i, paParms);
    if (RT_SUCCESS(vrc))
        vrc = i_waitForStatusChange(pEvent, uTimeoutMS, NULL /* FileStatus */, prcGuest);

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Queries file system information from a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   objData             Where to store the file system object data on success.
 * @param   prcGuest            Where to return the guest error when VERR_GSTCTL_GUEST_ERROR
 *                              was returned. Optional.
 */
int GuestFile::i_queryInfo(GuestFsObjData &objData, int *prcGuest)
{
    AssertPtrReturn(mSession, VERR_OBJECT_DESTROYED);
    return mSession->i_fsObjQueryInfo(mData.mOpenInfo.mFilename, FALSE /* fFollowSymlinks */, objData, prcGuest);
}

/**
 * Reads data from a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uSize               Size (in bytes) to read.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Where to store the read data on success.
 * @param   cbData              Size (in bytes) of \a pvData on input.
 * @param   pcbRead             Where to return to size (in bytes) read on success.
 *                              Optional.
 */
int GuestFile::i_readData(uint32_t uSize, uint32_t uTimeoutMS,
                          void* pvData, uint32_t cbData, uint32_t* pcbRead)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("uSize=%RU32, uTimeoutMS=%RU32, pvData=%p, cbData=%zu\n",
                     uSize, uTimeoutMS, pvData, cbData));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestFileRead);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[4];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mObjectID /* File handle */);
    HGCMSvcSetU32(&paParms[i++], uSize /* Size (in bytes) to read */);

    alock.release(); /* Drop write lock before sending. */

    vrc = sendMessage(HOST_MSG_FILE_READ, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        uint32_t cbRead = 0;
        vrc = i_waitForRead(pEvent, uTimeoutMS, pvData, cbData, &cbRead);
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("cbRead=%RU32\n", cbRead));
            if (pcbRead)
                *pcbRead = cbRead;
        }
        else if (pEvent->HasGuestError()) /* Return guest vrc if available. */
            vrc = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Reads data from a specific position from a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uOffset             Offset (in bytes) to start reading from.
 * @param   uSize               Size (in bytes) to read.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Where to store the read data on success.
 * @param   cbData              Size (in bytes) of \a pvData on input.
 * @param   pcbRead             Where to return to size (in bytes) read on success.
 *                              Optional.
 */
int GuestFile::i_readDataAt(uint64_t uOffset, uint32_t uSize, uint32_t uTimeoutMS,
                            void* pvData, size_t cbData, size_t* pcbRead)
{
    LogFlowThisFunc(("uOffset=%RU64, uSize=%RU32, uTimeoutMS=%RU32, pvData=%p, cbData=%zu\n",
                     uOffset, uSize, uTimeoutMS, pvData, cbData));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestFileRead);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[4];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mObjectID /* File handle */);
    HGCMSvcSetU64(&paParms[i++], uOffset /* Offset (in bytes) to start reading */);
    HGCMSvcSetU32(&paParms[i++], uSize /* Size (in bytes) to read */);

    alock.release(); /* Drop write lock before sending. */

    vrc = sendMessage(HOST_MSG_FILE_READ_AT, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        uint32_t cbRead = 0;
        vrc = i_waitForRead(pEvent, uTimeoutMS, pvData, cbData, &cbRead);
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("cbRead=%RU32\n", cbRead));

            if (pcbRead)
                *pcbRead = cbRead;
        }
        else if (pEvent->HasGuestError()) /* Return guest vrc if available. */
            vrc = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Seeks a guest file to a specific position.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   iOffset             Offset (in bytes) to seek.
 * @param   eSeekType           Seek type to use.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   puOffset            Where to return the new current file position (in bytes) on success.
 */
int GuestFile::i_seekAt(int64_t iOffset, GUEST_FILE_SEEKTYPE eSeekType,
                        uint32_t uTimeoutMS, uint64_t *puOffset)
{
    LogFlowThisFunc(("iOffset=%RI64, uTimeoutMS=%RU32\n",
                     iOffset, uTimeoutMS));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestFileOffsetChanged);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[4];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mObjectID /* File handle */);
    HGCMSvcSetU32(&paParms[i++], eSeekType /* Seek method */);
    /** @todo uint64_t vs. int64_t! */
    HGCMSvcSetU64(&paParms[i++], (uint64_t)iOffset /* Offset (in bytes) to start reading */);

    alock.release(); /* Drop write lock before sending. */

    vrc = sendMessage(HOST_MSG_FILE_SEEK, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        uint64_t uOffset;
        vrc = i_waitForOffsetChange(pEvent, uTimeoutMS, &uOffset);
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("uOffset=%RU64\n", uOffset));

            if (puOffset)
                *puOffset = uOffset;
        }
        else if (pEvent->HasGuestError()) /* Return guest vrc if available. */
            vrc = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sets the current internal file object status.
 *
 * @returns VBox status code.
 * @param   fileStatus          New file status to set.
 * @param   vrcFile             New result code to set.
 *
 * @note    Takes the write lock.
 */
int GuestFile::i_setFileStatus(FileStatus_T fileStatus, int vrcFile)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("oldStatus=%RU32, newStatus=%RU32, vrcFile=%Rrc\n", mData.mStatus, fileStatus, vrcFile));

#ifdef VBOX_STRICT
    if (fileStatus == FileStatus_Error)
        AssertMsg(RT_FAILURE(vrcFile), ("Guest vrc must be an error (%Rrc)\n", vrcFile));
    else
        AssertMsg(RT_SUCCESS(vrcFile), ("Guest vrc must not be an error (%Rrc)\n", vrcFile));
#endif

    if (mData.mStatus != fileStatus)
    {
        mData.mStatus    = fileStatus;
        mData.mLastError = vrcFile;

        ComObjPtr<VirtualBoxErrorInfo> errorInfo;
        HRESULT hrc = errorInfo.createObject();
        ComAssertComRCRet(hrc, VERR_COM_UNEXPECTED);
        if (RT_FAILURE(vrcFile))
        {
            hrc = errorInfo->initEx(VBOX_E_GSTCTL_GUEST_ERROR, vrcFile,
                                    COM_IIDOF(IGuestFile), getComponentName(),
                                    i_guestErrorToString(vrcFile, mData.mOpenInfo.mFilename.c_str()));
            ComAssertComRCRet(hrc, VERR_COM_UNEXPECTED);
        }
        /* Note: On vrcFile success, errorInfo is set to S_OK and also sent via the event below. */

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestFileStateChangedEvent(mEventSource, mSession, this, fileStatus, errorInfo);
    }

    return VINF_SUCCESS;
}

/**
 * Waits for a guest file offset change.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   pEvent              Guest wait event to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   puOffset            Where to return the new offset (in bytes) on success.
 *                              Optional and can be NULL.
 */
int GuestFile::i_waitForOffsetChange(GuestWaitEvent *pEvent,
                                     uint32_t uTimeoutMS, uint64_t *puOffset)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        if (evtType == VBoxEventType_OnGuestFileOffsetChanged)
        {
            if (puOffset)
            {
                ComPtr<IGuestFileOffsetChangedEvent> pFileEvent = pIEvent;
                Assert(!pFileEvent.isNull());

                HRESULT hr = pFileEvent->COMGETTER(Offset)((LONG64*)puOffset);
                ComAssertComRC(hr);
            }
        }
        else
            vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
    }

    return vrc;
}

/**
 * Waits for reading from a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   pEvent              Guest wait event to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Where to store read file data on success.
 * @param   cbData              Size (in bytes) of \a pvData.
 * @param   pcbRead             Where to return the actual bytes read on success.
 *                              Optional and can be NULL.
 */
int GuestFile::i_waitForRead(GuestWaitEvent *pEvent, uint32_t uTimeoutMS,
                             void *pvData, size_t cbData, uint32_t *pcbRead)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        if (evtType == VBoxEventType_OnGuestFileRead)
        {
            vrc = VINF_SUCCESS;

            ComPtr<IGuestFileReadEvent> pFileEvent = pIEvent;
            Assert(!pFileEvent.isNull());

            if (pvData)
            {
                com::SafeArray <BYTE> data;
                HRESULT hrc1 = pFileEvent->COMGETTER(Data)(ComSafeArrayAsOutParam(data));
                ComAssertComRC(hrc1);
                const size_t cbRead = data.size();
                if (cbRead)
                {
                    if (cbRead <= cbData)
                        memcpy(pvData, data.raw(), cbRead);
                    else
                        vrc = VERR_BUFFER_OVERFLOW;
                }
                /* else: used to be VERR_NO_DATA, but that messes stuff up. */

                if (pcbRead)
                {
                    *pcbRead = (uint32_t)cbRead;
                    Assert(*pcbRead == cbRead);
                }
            }
            else if (pcbRead)
            {
                *pcbRead = 0;
                HRESULT hrc2 = pFileEvent->COMGETTER(Processed)((ULONG *)pcbRead);
                ComAssertComRC(hrc2); NOREF(hrc2);
            }
        }
        else
            vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
    }

    return vrc;
}

/**
 * Waits for a guest file status change.
 *
 * @note Similar code in GuestProcess::i_waitForStatusChange() and
 *       GuestSession::i_waitForStatusChange().
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   pEvent              Guest wait event to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pFileStatus         Where to return the file status on success.
 * @param   prcGuest            Where to return the guest error when VERR_GSTCTL_GUEST_ERROR
 *                              was returned.
 */
int GuestFile::i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS,
                                     FileStatus_T *pFileStatus, int *prcGuest)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);
    /* pFileStatus is optional. */

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        Assert(evtType == VBoxEventType_OnGuestFileStateChanged);
        ComPtr<IGuestFileStateChangedEvent> pFileEvent = pIEvent;
        Assert(!pFileEvent.isNull());

        HRESULT hr;
        if (pFileStatus)
        {
            hr = pFileEvent->COMGETTER(Status)(pFileStatus);
            ComAssertComRC(hr);
        }

        ComPtr<IVirtualBoxErrorInfo> errorInfo;
        hr = pFileEvent->COMGETTER(Error)(errorInfo.asOutParam());
        ComAssertComRC(hr);

        LONG lGuestRc;
        hr = errorInfo->COMGETTER(ResultDetail)(&lGuestRc);
        ComAssertComRC(hr);

        LogFlowThisFunc(("resultDetail=%RI32 (%Rrc)\n",
                         lGuestRc, lGuestRc));

        if (RT_FAILURE((int)lGuestRc))
            vrc = VERR_GSTCTL_GUEST_ERROR;

        if (prcGuest)
            *prcGuest = (int)lGuestRc;
    }
    /* waitForEvent may also return VERR_GSTCTL_GUEST_ERROR like we do above, so make prcGuest is set. */
    /** @todo r=bird: Andy, you seem to have forgotten this scenario.  Showed up occasionally when
     * using the wrong password with a copyto command in a debug  build on windows, error info
     * contained "Unknown Status -858993460 (0xcccccccc)".  As you know windows fills the stack frames
     * with 0xcccccccc in debug builds to highlight use of uninitialized data, so that's what happened
     * here.  It's actually good you didn't initialize lGuest, as it would be heck to find otherwise.
     *
     * I'm still not very impressed with the error managment or the usuefullness of the documentation
     * in this code, though the latter is getting better! */
    else if (vrc == VERR_GSTCTL_GUEST_ERROR && prcGuest)
        *prcGuest = pEvent->GuestResult();
    Assert(vrc != VERR_GSTCTL_GUEST_ERROR || !prcGuest || *prcGuest != (int)0xcccccccc);

    return vrc;
}

int GuestFile::i_waitForWrite(GuestWaitEvent *pEvent,
                              uint32_t uTimeoutMS, uint32_t *pcbWritten)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        if (evtType == VBoxEventType_OnGuestFileWrite)
        {
            if (pcbWritten)
            {
                ComPtr<IGuestFileWriteEvent> pFileEvent = pIEvent;
                Assert(!pFileEvent.isNull());

                HRESULT hr = pFileEvent->COMGETTER(Processed)((ULONG*)pcbWritten);
                ComAssertComRC(hr);
            }
        }
        else
            vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
    }

    return vrc;
}

/**
 * Writes data to a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Data to write.
 * @param   cbData              Size (in bytes) of \a pvData to write.
 * @param   pcbWritten          Where to return to size (in bytes) written on success.
 *                              Optional.
 */
int GuestFile::i_writeData(uint32_t uTimeoutMS, const void *pvData, uint32_t cbData,
                           uint32_t *pcbWritten)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("uTimeoutMS=%RU32, pvData=%p, cbData=%zu\n",
                     uTimeoutMS, pvData, cbData));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestFileWrite);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[8];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mObjectID /* File handle */);
    HGCMSvcSetU32(&paParms[i++], cbData /* Size (in bytes) to write */);
    HGCMSvcSetPv (&paParms[i++], unconst(pvData), cbData);

    alock.release(); /* Drop write lock before sending. */

    vrc = sendMessage(HOST_MSG_FILE_WRITE, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        uint32_t cbWritten = 0;
        vrc = i_waitForWrite(pEvent, uTimeoutMS, &cbWritten);
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("cbWritten=%RU32\n", cbWritten));
            if (pcbWritten)
                *pcbWritten = cbWritten;
        }
        else if (pEvent->HasGuestError()) /* Return guest vrc if available. */
            vrc = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}


/**
 * Writes data to a specific position to a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uOffset             Offset (in bytes) to start writing at.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Data to write.
 * @param   cbData              Size (in bytes) of \a pvData to write.
 * @param   pcbWritten          Where to return to size (in bytes) written on success.
 *                              Optional.
 */
int GuestFile::i_writeDataAt(uint64_t uOffset, uint32_t uTimeoutMS,
                             const void *pvData, uint32_t cbData, uint32_t *pcbWritten)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("uOffset=%RU64, uTimeoutMS=%RU32, pvData=%p, cbData=%zu\n",
                     uOffset, uTimeoutMS, pvData, cbData));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestFileWrite);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /* Prepare HGCM call. */
    VBOXHGCMSVCPARM paParms[8];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mObjectID /* File handle */);
    HGCMSvcSetU64(&paParms[i++], uOffset /* Offset where to starting writing */);
    HGCMSvcSetU32(&paParms[i++], cbData /* Size (in bytes) to write */);
    HGCMSvcSetPv (&paParms[i++], unconst(pvData), cbData);

    alock.release(); /* Drop write lock before sending. */

    vrc = sendMessage(HOST_MSG_FILE_WRITE_AT, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        uint32_t cbWritten = 0;
        vrc = i_waitForWrite(pEvent, uTimeoutMS, &cbWritten);
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("cbWritten=%RU32\n", cbWritten));
            if (pcbWritten)
                *pcbWritten = cbWritten;
        }
        else if (pEvent->HasGuestError()) /* Return guest vrc if available. */
            vrc = pEvent->GuestResult();
    }

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

// Wrapped IGuestFile methods
/////////////////////////////////////////////////////////////////////////////
HRESULT GuestFile::close()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    /* Close file on guest. */
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_close(&vrcGuest);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_GSTCTL_GUEST_ERROR)
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_File, vrcGuest, mData.mOpenInfo.mFilename.c_str());
            return setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Closing guest file failed: %s"),
                                GuestBase::getErrorAsString(ge).c_str());
        }
        return setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Closing guest file \"%s\" failed with %Rrc\n"),
                            mData.mOpenInfo.mFilename.c_str(), vrc);
    }

    LogFlowThisFunc(("Returning S_OK / vrc=%Rrc\n", vrc));
    return S_OK;
}

HRESULT GuestFile::queryInfo(ComPtr<IFsObjInfo> &aObjInfo)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    GuestFsObjData fsObjData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_queryInfo(fsObjData, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        ComObjPtr<GuestFsObjInfo> ptrFsObjInfo;
        hrc = ptrFsObjInfo.createObject();
        if (SUCCEEDED(hrc))
        {
            vrc = ptrFsObjInfo->init(fsObjData);
            if (RT_SUCCESS(vrc))
                hrc = ptrFsObjInfo.queryInterfaceTo(aObjInfo.asOutParam());
            else
                hrc = setErrorVrc(vrc,
                                  tr("Initialization of guest file object for \"%s\" failed: %Rrc"),
                                  mData.mOpenInfo.mFilename.c_str(), vrc);
        }
    }
    else
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
                              GuestErrorInfo::Type_ToolStat,
#else
                              GuestErrorInfo::Type_File,
#endif
                              vrcGuest, mData.mOpenInfo.mFilename.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest file information failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
        }
        else
            hrc = setErrorVrc(vrc,
                              tr("Querying guest file information for \"%s\" failed: %Rrc"), mData.mOpenInfo.mFilename.c_str(), vrc);
    }

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::querySize(LONG64 *aSize)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    GuestFsObjData fsObjData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_queryInfo(fsObjData, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        *aSize = fsObjData.mObjectSize;
    }
    else
    {
        if (GuestProcess::i_isGuestError(vrc))
        {
            GuestErrorInfo ge(
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
                              GuestErrorInfo::Type_ToolStat,
#else
                              GuestErrorInfo::Type_File,
#endif
                              vrcGuest, mData.mOpenInfo.mFilename.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Querying guest file size failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
        }
        else
            hrc = setErrorVrc(vrc, tr("Querying guest file size for \"%s\" failed: %Rrc"), mData.mOpenInfo.mFilename.c_str(), vrc);
    }

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::read(ULONG aToRead, ULONG aTimeoutMS, std::vector<BYTE> &aData)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (aToRead == 0)
        return setError(E_INVALIDARG, tr("The size to read is zero"));

    LogFlowThisFuncEnter();

    /* Cap the read at 1MiB because that's all the guest will return anyway. */
    if (aToRead > _1M)
        aToRead = _1M;

    HRESULT hrc = S_OK;

    int vrc;
    try
    {
        aData.resize(aToRead);

        uint32_t cbRead;
        vrc = i_readData(aToRead, aTimeoutMS,
                         &aData.front(), aToRead, &cbRead);

        if (RT_SUCCESS(vrc))
        {
            if (aData.size() != cbRead)
                aData.resize(cbRead);
        }
        else
        {
            aData.resize(0);
        }
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading from file \"%s\" failed: %Rrc"),
                           mData.mOpenInfo.mFilename.c_str(), vrc);

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::readAt(LONG64 aOffset, ULONG aToRead, ULONG aTimeoutMS, std::vector<BYTE> &aData)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (aToRead == 0)
        return setError(E_INVALIDARG, tr("The size to read for guest file \"%s\" is zero"), mData.mOpenInfo.mFilename.c_str());

    LogFlowThisFuncEnter();

    /* Cap the read at 1MiB because that's all the guest will return anyway. */
    if (aToRead > _1M)
        aToRead = _1M;

    HRESULT hrc = S_OK;

    int vrc;
    try
    {
        aData.resize(aToRead);

        size_t cbRead;
        vrc = i_readDataAt(aOffset, aToRead, aTimeoutMS,
                           &aData.front(), aToRead, &cbRead);
        if (RT_SUCCESS(vrc))
        {
            if (aData.size() != cbRead)
                aData.resize(cbRead);
        }
        else
        {
            aData.resize(0);
        }
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading from file \"%s\" (at offset %RU64) failed: %Rrc"),
                           mData.mOpenInfo.mFilename.c_str(), aOffset, vrc);

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::seek(LONG64 aOffset, FileSeekOrigin_T aWhence, LONG64 *aNewOffset)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    HRESULT hrc = S_OK;

    GUEST_FILE_SEEKTYPE eSeekType;
    switch (aWhence)
    {
        case FileSeekOrigin_Begin:
            eSeekType = GUEST_FILE_SEEKTYPE_BEGIN;
            break;

        case FileSeekOrigin_Current:
            eSeekType = GUEST_FILE_SEEKTYPE_CURRENT;
            break;

        case FileSeekOrigin_End:
            eSeekType = GUEST_FILE_SEEKTYPE_END;
            break;

        default:
            return setError(E_INVALIDARG, tr("Invalid seek type for guest file \"%s\" specified"),
                            mData.mOpenInfo.mFilename.c_str());
    }

    LogFlowThisFuncEnter();

    uint64_t uNewOffset;
    int vrc = i_seekAt(aOffset, eSeekType, GSTCTL_DEFAULT_TIMEOUT_MS, &uNewOffset);
    if (RT_SUCCESS(vrc))
        *aNewOffset = RT_MIN(uNewOffset, (uint64_t)INT64_MAX);
    else
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Seeking file \"%s\" (to offset %RI64) failed: %Rrc"),
                           mData.mOpenInfo.mFilename.c_str(), aOffset, vrc);

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::setACL(const com::Utf8Str &aAcl, ULONG aMode)
{
    RT_NOREF(aAcl, aMode);
    ReturnComNotImplemented();
}

HRESULT GuestFile::setSize(LONG64 aSize)
{
    LogFlowThisFuncEnter();

    /*
     * Validate.
     */
    if (aSize < 0)
        return setError(E_INVALIDARG, tr("The size (%RI64) for guest file \"%s\" cannot be a negative value"),
                        aSize, mData.mOpenInfo.mFilename.c_str());

    /*
     * Register event callbacks.
     */
    int             vrc;
    GuestWaitEvent *pWaitEvent = NULL;
    GuestEventTypes lstEventTypes;
    try
    {
        lstEventTypes.push_back(VBoxEventType_OnGuestFileStateChanged);
        lstEventTypes.push_back(VBoxEventType_OnGuestFileSizeChanged);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    vrc = registerWaitEvent(lstEventTypes, &pWaitEvent);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Send of the HGCM message.
         */
        VBOXHGCMSVCPARM aParms[3];
        HGCMSvcSetU32(&aParms[0], pWaitEvent->ContextID());
        HGCMSvcSetU32(&aParms[1], mObjectID /* File handle */);
        HGCMSvcSetU64(&aParms[2], aSize);

        alock.release(); /* Drop write lock before sending. */

        vrc = sendMessage(HOST_MSG_FILE_SET_SIZE, RT_ELEMENTS(aParms), aParms);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Wait for the event.
             */
            VBoxEventType_T enmEvtType;
            ComPtr<IEvent>  pIEvent;
            vrc = waitForEvent(pWaitEvent, RT_MS_1MIN / 2, &enmEvtType, pIEvent.asOutParam());
            if (RT_SUCCESS(vrc))
            {
                if (enmEvtType == VBoxEventType_OnGuestFileSizeChanged)
                    vrc = VINF_SUCCESS;
                else
                    vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
            }

            if (pWaitEvent->HasGuestError()) /* Return guest vrc if available. */
                vrc = pWaitEvent->GuestResult();
        }

        /*
         * Unregister the wait event and deal with error reporting if needed.
         */
        unregisterWaitEvent(pWaitEvent);
    }
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
        hrc = S_OK;
    else
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                           tr("Setting the guest file size of \"%s\" to %RU64 (%#RX64) bytes failed: %Rrc", "", aSize),
                           mData.mOpenInfo.mFilename.c_str(), aSize, aSize, vrc);
    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::write(const std::vector<BYTE> &aData, ULONG aTimeoutMS, ULONG *aWritten)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (aData.size() == 0)
        return setError(E_INVALIDARG, tr("No data to write specified"), mData.mOpenInfo.mFilename.c_str());

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    const uint32_t cbData = (uint32_t)aData.size();
    const void *pvData = (void *)&aData.front();
    int vrc = i_writeData(aTimeoutMS, pvData, cbData, (uint32_t*)aWritten);
    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Writing %zu bytes to guest file \"%s\" failed: %Rrc", "", aData.size()),
                           aData.size(), mData.mOpenInfo.mFilename.c_str(), vrc);

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestFile::writeAt(LONG64 aOffset, const std::vector<BYTE> &aData, ULONG aTimeoutMS, ULONG *aWritten)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (aData.size() == 0)
        return setError(E_INVALIDARG, tr("No data to write at for guest file \"%s\" specified"), mData.mOpenInfo.mFilename.c_str());

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    const uint32_t cbData = (uint32_t)aData.size();
    const void *pvData = (void *)&aData.front();
    int vrc = i_writeDataAt(aOffset, aTimeoutMS, pvData, cbData, (uint32_t*)aWritten);
    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                           tr("Writing %zu bytes to file \"%s\" (at offset %RU64) failed: %Rrc", "", aData.size()),
                           aData.size(), mData.mOpenInfo.mFilename.c_str(), aOffset, vrc);

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

