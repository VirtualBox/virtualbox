/* $Id: GuestProcessImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest process handling.
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

/**
 * Locking rules:
 * - When the main dispatcher (callbackDispatcher) is called it takes the
 *   WriteLock while dispatching to the various on* methods.
 * - All other outer functions (accessible by Main) must not own a lock
 *   while waiting for a callback or for an event.
 * - Only keep Read/WriteLocks as short as possible and only when necessary.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_GUESTPROCESS
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestImpl.h"
#include "GuestProcessImpl.h"
#include "GuestSessionImpl.h"
#include "GuestCtrlImplPrivate.h"
#include "ConsoleImpl.h"
#include "VirtualBoxErrorInfoImpl.h"

#include "Global.h"
#include "AutoCaller.h"
#include "VBoxEvents.h"
#include "ThreadTask.h"

#include <memory> /* For auto_ptr. */

#include <iprt/asm.h>
#include <iprt/cpp/utils.h> /* For unconst(). */
#include <iprt/getopt.h>

#include <VBox/com/listeners.h>

#include <VBox/com/array.h>


/**
 * Base class for all guest process tasks.
 */
class GuestProcessTask : public ThreadTask
{
public:

    GuestProcessTask(GuestProcess *pProcess)
        : ThreadTask("GenericGuestProcessTask")
        , mProcess(pProcess)
        , mVrc(VINF_SUCCESS) { }

    virtual ~GuestProcessTask(void) { }

    /** Returns the last set result code. */
    int i_vrc(void) const { return mVrc; }
    /** Returns whether the last set result is okay (successful) or not. */
    bool i_isOk(void) const { return RT_SUCCESS(mVrc); }
    /** Returns the reference of the belonging progress object. */
    const ComObjPtr<GuestProcess> &i_process(void) const { return mProcess; }

protected:

    /** Progress object this process belongs to. */
    const ComObjPtr<GuestProcess>    mProcess;
    /** Last set result code. */
    int                              mVrc;
};

/**
 * Task to start a process on the guest.
 */
class GuestProcessStartTask : public GuestProcessTask
{
public:

    GuestProcessStartTask(GuestProcess *pProcess)
        : GuestProcessTask(pProcess)
    {
        m_strTaskName = "gctlPrcStart";
    }

    void handler()
    {
        GuestProcess::i_startProcessThreadTask(this);
    }
};

/**
 * Internal listener class to serve events in an
 * active manner, e.g. without polling delays.
 */
class GuestProcessListener
{
public:

    GuestProcessListener(void)
    {
    }

    virtual ~GuestProcessListener(void)
    {
    }

    HRESULT init(GuestProcess *pProcess)
    {
        AssertPtrReturn(pProcess, E_POINTER);
        mProcess = pProcess;
        return S_OK;
    }

    void uninit(void)
    {
        mProcess = NULL;
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnGuestProcessStateChanged:
            case VBoxEventType_OnGuestProcessInputNotify:
            case VBoxEventType_OnGuestProcessOutput:
            {
                AssertPtrReturn(mProcess, E_POINTER);
                int vrc2 = mProcess->signalWaitEvent(aType, aEvent);
                RT_NOREF(vrc2);
#ifdef LOG_ENABLED
                LogFlowThisFunc(("Signalling events of type=%RU32, pProcess=%p resulted in vrc=%Rrc\n",
                                 aType, &mProcess, vrc2));
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

    GuestProcess *mProcess;
};
typedef ListenerImpl<GuestProcessListener, GuestProcess*> GuestProcessListenerImpl;

VBOX_LISTENER_DECLARE(GuestProcessListenerImpl)

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestProcess)

HRESULT GuestProcess::FinalConstruct(void)
{
    LogFlowThisFuncEnter();
    return BaseFinalConstruct();
}

void GuestProcess::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initialies a guest process object.
 *
 * @returns VBox status code.
 * @param   aConsole            Console this process is bound to.
 * @param   aSession            Guest session this process is bound to.
 * @param   aObjectID           Object ID to use for this process object.
 * @param   aProcInfo           Process startup information to use.
 * @param   pBaseEnv            Guest environment to apply when starting the process on the guest.
 */
int GuestProcess::init(Console *aConsole, GuestSession *aSession, ULONG aObjectID,
                       const GuestProcessStartupInfo &aProcInfo, const GuestEnvironment *pBaseEnv)
{
    LogFlowThisFunc(("aConsole=%p, aSession=%p, aObjectID=%RU32, pBaseEnv=%p\n",
                     aConsole, aSession, aObjectID, pBaseEnv));

    AssertPtrReturn(aConsole, VERR_INVALID_POINTER);
    AssertPtrReturn(aSession, VERR_INVALID_POINTER);

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    HRESULT hrc;

    int vrc = bindToSession(aConsole, aSession, aObjectID);
    if (RT_SUCCESS(vrc))
    {
        hrc = unconst(mEventSource).createObject();
        if (FAILED(hrc))
            vrc = VERR_NO_MEMORY;
        else
        {
            hrc = mEventSource->init();
            if (FAILED(hrc))
                vrc = VERR_COM_UNEXPECTED;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        try
        {
            GuestProcessListener *pListener = new GuestProcessListener();
            ComObjPtr<GuestProcessListenerImpl> thisListener;
            hrc = thisListener.createObject();
            if (SUCCEEDED(hrc))
                hrc = thisListener->init(pListener, this);

            if (SUCCEEDED(hrc))
            {
                com::SafeArray <VBoxEventType_T> eventTypes;
                eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);
                eventTypes.push_back(VBoxEventType_OnGuestProcessInputNotify);
                eventTypes.push_back(VBoxEventType_OnGuestProcessOutput);
                hrc = mEventSource->RegisterListener(thisListener,
                                                    ComSafeArrayAsInParam(eventTypes),
                                                    TRUE /* Active listener */);
                if (SUCCEEDED(hrc))
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
        mData.mProcess = aProcInfo;
        mData.mpSessionBaseEnv = pBaseEnv;
        if (pBaseEnv)
            pBaseEnv->retainConst();
        mData.mExitCode = 0;
        mData.mPID = 0;
        mData.mLastError = VINF_SUCCESS;
        mData.mStatus = ProcessStatus_Undefined;
        /* Everything else will be set by the actual starting routine. */

        /* Confirm a successful initialization when it's the case. */
        autoInitSpan.setSucceeded();

        return vrc;
    }

    autoInitSpan.setFailed();
    return vrc;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease() or IGuestSession::uninit().
 */
void GuestProcess::uninit(void)
{
    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFunc(("mExe=%s, PID=%RU32\n", mData.mProcess.mExecutable.c_str(), mData.mPID));

    if (mData.mpSessionBaseEnv)
    {
        mData.mpSessionBaseEnv->releaseConst();
        mData.mpSessionBaseEnv = NULL;
    }

    baseUninit();

    LogFlowFuncLeave();
}

// implementation of public getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////
HRESULT GuestProcess::getArguments(std::vector<com::Utf8Str> &aArguments)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aArguments = mData.mProcess.mArguments;
    return S_OK;
}

HRESULT GuestProcess::getEnvironment(std::vector<com::Utf8Str> &aEnvironment)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);  /* (Paranoia since both environment objects are immutable.) */
    HRESULT hrc;
    if (mData.mpSessionBaseEnv)
    {
        int vrc;
        if (mData.mProcess.mEnvironmentChanges.count() == 0)
            vrc = mData.mpSessionBaseEnv->queryPutEnvArray(&aEnvironment);
        else
        {
            GuestEnvironment TmpEnv;
            vrc = TmpEnv.copy(*mData.mpSessionBaseEnv);
            if (RT_SUCCESS(vrc))
            {
                vrc = TmpEnv.applyChanges(mData.mProcess.mEnvironmentChanges);
                if (RT_SUCCESS(vrc))
                    vrc = TmpEnv.queryPutEnvArray(&aEnvironment);
            }
        }
        hrc = Global::vboxStatusCodeToCOM(vrc);
    }
    else
        hrc = setError(VBOX_E_NOT_SUPPORTED, tr("The base environment feature is not supported by installed Guest Additions"));
    LogFlowThisFuncLeave();
    return hrc;
#endif
}

HRESULT GuestProcess::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    LogFlowThisFuncEnter();

    // no need to lock - lifetime constant
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());

    LogFlowThisFuncLeave();
    return S_OK;
}

HRESULT GuestProcess::getExecutablePath(com::Utf8Str &aExecutablePath)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aExecutablePath = mData.mProcess.mExecutable;

    return S_OK;
}

HRESULT GuestProcess::getExitCode(LONG *aExitCode)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aExitCode = mData.mExitCode;

    return S_OK;
}

HRESULT GuestProcess::getName(com::Utf8Str &aName)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = mData.mProcess.mName;

    return S_OK;
}

HRESULT GuestProcess::getPID(ULONG *aPID)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aPID = mData.mPID;

    return S_OK;
}

HRESULT GuestProcess::getStatus(ProcessStatus_T *aStatus)
{
    LogFlowThisFuncEnter();

    *aStatus = i_getStatus();

    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Entry point for guest side process callbacks.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCb              Host callback data.
 */
int GuestProcess::i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);
#ifdef DEBUG
    LogFlowThisFunc(("uPID=%RU32, uContextID=%RU32, uMessage=%RU32, pSvcCb=%p\n",
                     mData.mPID, pCbCtx->uContextID, pCbCtx->uMessage, pSvcCb));
#endif

    int vrc;
    switch (pCbCtx->uMessage)
    {
        case GUEST_MSG_DISCONNECTED:
        {
            vrc = i_onGuestDisconnected(pCbCtx, pSvcCb);
            break;
        }

        case GUEST_MSG_EXEC_STATUS:
        {
            vrc = i_onProcessStatusChange(pCbCtx, pSvcCb);
            break;
        }

        case GUEST_MSG_EXEC_OUTPUT:
        {
            vrc = i_onProcessOutput(pCbCtx, pSvcCb);
            break;
        }

        case GUEST_MSG_EXEC_INPUT_STATUS:
        {
            vrc = i_onProcessInputStatus(pCbCtx, pSvcCb);
            break;
        }

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
 * Checks if the current assigned PID matches another PID (from a callback).
 *
 * In protocol v1 we don't have the possibility to terminate/kill
 * processes so it can happen that a formerly started process A
 * (which has the context ID 0 (session=0, process=0, count=0) will
 * send a delayed message to the host if this process has already
 * been discarded there and the same context ID was reused by
 * a process B. Process B in turn then has a different guest PID.
 *
 * Note: This also can happen when restoring from a saved state which
 *       had a guest process running.
 *
 * @return  IPRT status code.
 * @param   uPID                    PID to check.
 */
inline int GuestProcess::i_checkPID(uint32_t uPID)
{
    int vrc = VINF_SUCCESS;

    /* Was there a PID assigned yet? */
    if (mData.mPID)
    {
        if (RT_UNLIKELY(mData.mPID != uPID))
        {
            LogFlowFunc(("Stale guest process (PID=%RU32) sent data to a newly started process (pProcesS=%p, PID=%RU32, status=%RU32)\n",
                         uPID, this, mData.mPID, mData.mStatus));
            vrc = VERR_NOT_FOUND;
        }
    }

    return vrc;
}

/**
 * Returns the current process status.
 *
 * @returns Current process status.
 *
 * @note    Takes the read lock.
 */
ProcessStatus_T GuestProcess::i_getStatus(void)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    return mData.mStatus;
}

/**
 * Converts a given guest process error to a string.
 *
 * @returns Error as a string.
 * @param   vrcGuest    Guest process error to return string for.
 * @param   pcszWhat    Hint of what was involved when the error occurred.
 */
/* static */
Utf8Str GuestProcess::i_guestErrorToString(int vrcGuest, const char *pcszWhat)
{
    AssertPtrReturn(pcszWhat, "");

    Utf8Str strErr;
    switch (vrcGuest)
    {
#define CASE_MSG(a_iRc, ...) \
        case a_iRc: strErr.printf(__VA_ARGS__); break;

        CASE_MSG(VERR_FILE_NOT_FOUND,                 tr("No such file or directory \"%s\" on guest"), pcszWhat); /* This is the most likely error. */
        CASE_MSG(VERR_PATH_NOT_FOUND,                 tr("No such file or directory \"%s\" on guest"), pcszWhat);
        CASE_MSG(VERR_INVALID_VM_HANDLE,              tr("VMM device is not available (is the VM running?)"));
        CASE_MSG(VERR_HGCM_SERVICE_NOT_FOUND,         tr("The guest execution service is not available"));
        CASE_MSG(VERR_BAD_EXE_FORMAT,                 tr("The file \"%s\" is not an executable format on guest"), pcszWhat);
        CASE_MSG(VERR_AUTHENTICATION_FAILURE,         tr("The user \"%s\" was not able to logon on guest"), pcszWhat);
        CASE_MSG(VERR_INVALID_NAME,                   tr("The file \"%s\" is an invalid name"), pcszWhat);
        CASE_MSG(VERR_TIMEOUT,                        tr("The guest did not respond within time"));
        CASE_MSG(VERR_CANCELLED,                      tr("The execution operation for \"%s\" was canceled"), pcszWhat);
        CASE_MSG(VERR_GSTCTL_MAX_CID_OBJECTS_REACHED, tr("Maximum number of concurrent guest processes has been reached"));
        CASE_MSG(VERR_NOT_FOUND,                      tr("The guest execution service is not ready (yet)"));
        default:
            strErr.printf(tr("Error %Rrc for guest process \"%s\" occurred\n"), vrcGuest, pcszWhat);
            break;
#undef CASE_MSG
    }

    return strErr;
}

/**
 * Translates a process status to a human readable string.
 *
 * @returns Process status as a string.
 * @param   enmStatus           Guest process status to return string for.
 */
/* static */
Utf8Str GuestProcess::i_statusToString(ProcessStatus_T enmStatus)
{
    switch (enmStatus)
    {
        case ProcessStatus_Starting:
            return "starting";
        case ProcessStatus_Started:
            return "started";
        case ProcessStatus_Paused:
            return "paused";
        case ProcessStatus_Terminating:
            return "terminating";
        case ProcessStatus_TerminatedNormally:
            return "successfully terminated";
        case ProcessStatus_TerminatedSignal:
            return "terminated by signal";
        case ProcessStatus_TerminatedAbnormally:
            return "abnormally aborted";
        case ProcessStatus_TimedOutKilled:
            return "timed out";
        case ProcessStatus_TimedOutAbnormally:
            return "timed out, hanging";
        case ProcessStatus_Down:
            return "killed";
        case ProcessStatus_Error:
            return "error";
        default:
            break;
    }

    AssertFailed(); /* Should never happen! */
    return "unknown";
}

/**
 * Returns @c true if the passed in error code indicates an error which came
 * from the guest side, or @c false if not.
 *
 * @return  bool                @c true if the passed in error code indicates an error which came
 *                              from the guest side, or @c false if not.
 * @param   vrc                 Error code to check.
 */
/* static */
bool GuestProcess::i_isGuestError(int vrc)
{
    return vrc == VERR_GSTCTL_GUEST_ERROR
        || vrc == VERR_GSTCTL_PROCESS_EXIT_CODE;
}

/**
 * Returns whether the guest process is alive (i.e. running) or not.
 *
 * @returns \c true if alive and running, or \c false if not.
 */
inline bool GuestProcess::i_isAlive(void)
{
    return mData.mStatus == ProcessStatus_Started
        || mData.mStatus == ProcessStatus_Paused
        || mData.mStatus == ProcessStatus_Terminating;
}

/**
 * Returns whether the guest process has ended (i.e. terminated) or not.
 *
 * @returns \c true if ended, or \c false if not.
 */
inline bool GuestProcess::i_hasEnded(void)
{
    return mData.mStatus == ProcessStatus_TerminatedNormally
        || mData.mStatus == ProcessStatus_TerminatedSignal
        || mData.mStatus == ProcessStatus_TerminatedAbnormally
        || mData.mStatus == ProcessStatus_TimedOutKilled
        || mData.mStatus == ProcessStatus_TimedOutAbnormally
        || mData.mStatus == ProcessStatus_Down
        || mData.mStatus == ProcessStatus_Error;
}

/**
 * Called when the guest side of the process has been disconnected (closed, terminated, +++).
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 */
int GuestProcess::i_onGuestDisconnected(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    int vrc = i_setProcessStatus(ProcessStatus_Down, VINF_SUCCESS);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sets (reports) the current input status of the guest process.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_onProcessInputStatus(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);
    /* pCallback is optional. */

    if (pSvcCbData->mParms < 5)
        return VERR_INVALID_PARAMETER;

    CALLBACKDATA_PROC_INPUT dataCb;
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    int vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[1], &dataCb.uPID);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[2], &dataCb.uStatus);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[3], &dataCb.uFlags);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[4], &dataCb.uProcessed);
    AssertRCReturn(vrc, vrc);

    LogFlowThisFunc(("uPID=%RU32, uStatus=%RU32, uFlags=%RI32, cbProcessed=%RU32\n",
                     dataCb.uPID, dataCb.uStatus, dataCb.uFlags, dataCb.uProcessed));

    vrc = i_checkPID(dataCb.uPID);
    if (RT_SUCCESS(vrc))
    {
        ProcessInputStatus_T inputStatus = ProcessInputStatus_Undefined;
        switch (dataCb.uStatus)
        {
            case INPUT_STS_WRITTEN:
                inputStatus = ProcessInputStatus_Written;
                break;
            case INPUT_STS_ERROR:
                inputStatus = ProcessInputStatus_Broken;
                break;
            case INPUT_STS_TERMINATED:
                inputStatus = ProcessInputStatus_Broken;
                break;
            case INPUT_STS_OVERFLOW:
                inputStatus = ProcessInputStatus_Overflow;
                break;
            case INPUT_STS_UNDEFINED:
                /* Fall through is intentional. */
            default:
                AssertMsg(!dataCb.uProcessed, ("Processed data is not 0 in undefined input state\n"));
                break;
        }

        if (inputStatus != ProcessInputStatus_Undefined)
        {
            AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

            /* Copy over necessary data before releasing lock again. */
            uint32_t uPID = mData.mPID;
            /** @todo Also handle mSession? */

            alock.release(); /* Release lock before firing off event. */

            ::FireGuestProcessInputNotifyEvent(mEventSource, mSession, this, uPID, 0 /* StdIn */, dataCb.uProcessed, inputStatus);
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Notifies of an I/O operation of the guest process.
 *
 * @returns VERR_NOT_IMPLEMENTED -- not implemented yet.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 */
int GuestProcess::i_onProcessNotifyIO(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    return VERR_NOT_IMPLEMENTED;
}

/**
 * Sets (reports) the current running status of the guest process.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_onProcessStatusChange(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    if (pSvcCbData->mParms < 5)
        return VERR_INVALID_PARAMETER;

    CALLBACKDATA_PROC_STATUS dataCb;
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    int vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[1], &dataCb.uPID);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[2], &dataCb.uStatus);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[3], &dataCb.uFlags);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[4], &dataCb.pvData, &dataCb.cbData);
    AssertRCReturn(vrc, vrc);

    LogFlowThisFunc(("uPID=%RU32, uStatus=%RU32, uFlags=%RU32\n",
                     dataCb.uPID, dataCb.uStatus, dataCb.uFlags));

    vrc = i_checkPID(dataCb.uPID);
    if (RT_SUCCESS(vrc))
    {
        ProcessStatus_T procStatus = ProcessStatus_Undefined;
        int vrcProc = VINF_SUCCESS;

        switch (dataCb.uStatus)
        {
            case PROC_STS_STARTED:
            {
                procStatus = ProcessStatus_Started;

                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                mData.mPID = dataCb.uPID; /* Set the process PID. */
                break;
            }

            case PROC_STS_TEN:
            {
                procStatus = ProcessStatus_TerminatedNormally;

                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                mData.mExitCode = dataCb.uFlags; /* Contains the exit code. */
                break;
            }

            case PROC_STS_TES:
            {
                procStatus = ProcessStatus_TerminatedSignal;

                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
                mData.mExitCode = dataCb.uFlags; /* Contains the signal. */
                break;
            }

            case PROC_STS_TEA:
            {
                procStatus = ProcessStatus_TerminatedAbnormally;
                break;
            }

            case PROC_STS_TOK:
            {
                procStatus = ProcessStatus_TimedOutKilled;
                break;
            }

            case PROC_STS_TOA:
            {
                procStatus = ProcessStatus_TimedOutAbnormally;
                break;
            }

            case PROC_STS_DWN:
            {
                procStatus = ProcessStatus_Down;
                break;
            }

            case PROC_STS_ERROR:
            {
                vrcProc = dataCb.uFlags; /* mFlags contains the IPRT error sent from the guest. */
                procStatus = ProcessStatus_Error;
                break;
            }

            case PROC_STS_UNDEFINED:
            default:
            {
                /* Silently skip this request. */
                procStatus = ProcessStatus_Undefined;
                break;
            }
        }

        LogFlowThisFunc(("Got vrc=%Rrc, procSts=%RU32, vrcProc=%Rrc\n", vrc, procStatus, vrcProc));

        /* Set the process status. */
        int vrc2 = i_setProcessStatus(procStatus, vrcProc);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sets (reports) the current output status of the guest process.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 */
int GuestProcess::i_onProcessOutput(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
    RT_NOREF(pCbCtx);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    if (pSvcCbData->mParms < 5)
        return VERR_INVALID_PARAMETER;

    CALLBACKDATA_PROC_OUTPUT dataCb;
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    int vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[1], &dataCb.uPID);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[2], &dataCb.uHandle);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[3], &dataCb.uFlags);
    AssertRCReturn(vrc, vrc);
    vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[4], &dataCb.pvData, &dataCb.cbData);
    AssertRCReturn(vrc, vrc);

    LogFlowThisFunc(("uPID=%RU32, uHandle=%RU32, uFlags=%RI32, pvData=%p, cbData=%RU32\n",
                     dataCb.uPID, dataCb.uHandle, dataCb.uFlags, dataCb.pvData, dataCb.cbData));

    vrc = i_checkPID(dataCb.uPID);
    if (RT_SUCCESS(vrc))
    {
        com::SafeArray<BYTE> data((size_t)dataCb.cbData);
        if (dataCb.cbData)
            data.initFrom((BYTE*)dataCb.pvData, dataCb.cbData);

        ::FireGuestProcessOutputEvent(mEventSource, mSession, this,
                                      mData.mPID, dataCb.uHandle, dataCb.cbData, ComSafeArrayAsInParam(data));
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc GuestObject::i_onUnregister
 */
int GuestProcess::i_onUnregister(void)
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
int GuestProcess::i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus)
{
    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    /* If the session now is in a terminated state, set the process status
     * to "down", as there is not much else we can do now. */
    if (GuestSession::i_isTerminated(enmSessionStatus))
    {
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        vrc = i_setProcessStatus(ProcessStatus_Down, VINF_SUCCESS /* vrcProc, ignored */);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Reads data from a guest file.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uHandle             Internal file handle to use for reading.
 * @param   uSize               Size (in bytes) to read.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Where to store the read data on success.
 * @param   cbData              Size (in bytes) of \a pvData on input.
 * @param   pcbRead             Where to return to size (in bytes) read on success.
 *                              Optional.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_readData(uint32_t uHandle, uint32_t uSize, uint32_t uTimeoutMS,
                             void *pvData, size_t cbData, uint32_t *pcbRead, int *pvrcGuest)
{
    LogFlowThisFunc(("uPID=%RU32, uHandle=%RU32, uSize=%RU32, uTimeoutMS=%RU32, pvData=%p, cbData=%RU32, pvrcGuest=%p\n",
                     mData.mPID, uHandle, uSize, uTimeoutMS, pvData, cbData, pvrcGuest));
    AssertReturn(uSize, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData >= uSize, VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mData.mStatus != ProcessStatus_Started
        /* Skip reading if the process wasn't started with the appropriate
         * flags. */
        || (   (   uHandle == GUEST_PROC_OUT_H_STDOUT
                || uHandle == GUEST_PROC_OUT_H_STDOUT_DEPRECATED)
            && !(mData.mProcess.mFlags & ProcessCreateFlag_WaitForStdOut))
        || (   uHandle == GUEST_PROC_OUT_H_STDERR
            && !(mData.mProcess.mFlags & ProcessCreateFlag_WaitForStdErr))
       )
    {
        if (pcbRead)
            *pcbRead = 0;
        if (pvrcGuest)
            *pvrcGuest = VINF_SUCCESS;
        return VINF_SUCCESS; /* Nothing to read anymore. */
    }

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        /*
         * On Guest Additions < 4.3 there is no guarantee that the process status
         * change arrives *after* the output event, e.g. if this was the last output
         * block being read and the process will report status "terminate".
         * So just skip checking for process status change and only wait for the
         * output event.
         */
        if (mSession->i_getProtocolVersion() >= 2)
            eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestProcessOutput);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    if (RT_SUCCESS(vrc))
    {
        VBOXHGCMSVCPARM paParms[8];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetU32(&paParms[i++], mData.mPID);
        HGCMSvcSetU32(&paParms[i++], uHandle);
        HGCMSvcSetU32(&paParms[i++], 0 /* Flags, none set yet. */);

        alock.release(); /* Drop the write lock before sending. */

        vrc = sendMessage(HOST_MSG_EXEC_GET_OUTPUT, i, paParms);
    }

    if (RT_SUCCESS(vrc))
        vrc = i_waitForOutput(pEvent, uHandle, uTimeoutMS,
                              pvData, cbData, pcbRead);

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sets (reports) the current (overall) status of the guest process.
 *
 * @returns VBox status code.
 * @param   procStatus          Guest process status to set.
 * @param   vrcProc             Guest process result code to set.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_setProcessStatus(ProcessStatus_T procStatus, int vrcProc)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("oldStatus=%RU32, newStatus=%RU32, vrcProc=%Rrc\n", mData.mStatus, procStatus, vrcProc));

    if (procStatus == ProcessStatus_Error)
    {
        AssertMsg(RT_FAILURE(vrcProc), ("Guest vrcProc must be an error (%Rrc)\n", vrcProc));
        /* Do not allow overwriting an already set error. If this happens
         * this means we forgot some error checking/locking somewhere. */
        AssertMsg(RT_SUCCESS(mData.mLastError), ("Guest vrcProc already set (to %Rrc)\n", mData.mLastError));
    }
    else
        AssertMsg(RT_SUCCESS(vrcProc), ("Guest vrcProc must not be an error (%Rrc)\n", vrcProc));

    int vrc = VINF_SUCCESS;

    if (mData.mStatus != procStatus) /* Was there a process status change? */
    {
        mData.mStatus    = procStatus;
        mData.mLastError = vrcProc;

        ComObjPtr<VirtualBoxErrorInfo> errorInfo;
        HRESULT hrc = errorInfo.createObject();
        ComAssertComRC(hrc);
        if (RT_FAILURE(mData.mLastError))
        {
            hrc = errorInfo->initEx(VBOX_E_IPRT_ERROR, mData.mLastError,
                                    COM_IIDOF(IGuestProcess), getComponentName(),
                                    i_guestErrorToString(mData.mLastError, mData.mProcess.mExecutable.c_str()));
            ComAssertComRC(hrc);
        }

        /* Copy over necessary data before releasing lock again. */
        uint32_t uPID =  mData.mPID;
        /** @todo Also handle mSession? */

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestProcessStateChangedEvent(mEventSource, mSession, this, uPID, procStatus, errorInfo);
#if 0
        /*
         * On Guest Additions < 4.3 there is no guarantee that outstanding
         * requests will be delivered to the host after the process has ended,
         * so just cancel all waiting events here to not let clients run
         * into timeouts.
         */
        if (   mSession->getProtocolVersion() < 2
            && hasEnded())
        {
            LogFlowThisFunc(("Process ended, canceling outstanding wait events ...\n"));
            vrc = cancelWaitEvents();
        }
#endif
    }

    return vrc;
}

/**
 * Starts the process on the guest.
 *
 * @returns VBox status code.
 * @param   cMsTimeout          Timeout (in ms) to wait for starting the process.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_startProcess(uint32_t cMsTimeout, int *pvrcGuest)
{
    LogFlowThisFunc(("cMsTimeout=%RU32, procExe=%s, cwd=%s, procTimeoutMS=%RU32, procFlags=%x, sessionID=%RU32\n",
                     cMsTimeout, mData.mProcess.mExecutable.c_str(), mData.mProcess.mCwd.c_str(),
                     mData.mProcess.mTimeoutMS, mData.mProcess.mFlags, mSession->i_getId()));

    /* Wait until the caller function (if kicked off by a thread)
     * has returned and continue operation. */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData.mStatus = ProcessStatus_Starting;

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);
        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = i_startProcessInner(cMsTimeout, alock, pEvent, pvrcGuest);

    unregisterWaitEvent(pEvent);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Helper function to start a process on the guest. Do not call directly!
 *
 * @returns VBox status code.
 * @param   cMsTimeout          Timeout (in ms) to wait for starting the process.
 * @param   rLock               Write lock to use for serialization.
 * @param   pEvent              Event to use for notifying waiters.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestProcess::i_startProcessInner(uint32_t cMsTimeout, AutoWriteLock &rLock, GuestWaitEvent *pEvent, int *pvrcGuest)
{
    GuestSession *pSession = mSession;
    AssertPtr(pSession);
    uint32_t const uProtocol = pSession->i_getProtocolVersion();

    const GuestCredentials &sessionCreds = pSession->i_getCredentials();

    /* Prepare arguments. */
    size_t cArgs = mData.mProcess.mArguments.size();
    if (cArgs >= 128*1024)
        return VERR_BUFFER_OVERFLOW;

    Guest *pGuest = mSession->i_getParent();
    AssertPtr(pGuest);
    const uint64_t fGuestControlFeatures0 = pGuest->i_getGuestControlFeatures0();

    /* Check if the Guest Additions support setting the current working directory for the new process
     * if the caller wants to set one, and bail out early if it doesn't. */
    if (   !mData.mProcess.mCwd.isEmpty()
        && (   uProtocol < 2
            || !(fGuestControlFeatures0 & VBOX_GUESTCTRL_GF_0_PROCESS_CWD)))
    {
        LogRel2(("Guest Control: Installed Guest Addtions don't support setting the current working directory to '%s'!\n",
                 mData.mProcess.mCwd.c_str()));
        return VERR_NOT_SUPPORTED;
    }

    size_t cbArgs = 0;
    char *pszArgs = NULL;
    int vrc = VINF_SUCCESS;

    if (cArgs)
    {
        char const **papszArgv = (char const **)RTMemAlloc((cArgs + 1) * sizeof(papszArgv[0]));
        AssertReturn(papszArgv, VERR_NO_MEMORY);

        for (size_t i = 0; i < cArgs; i++)
        {
            papszArgv[i] = mData.mProcess.mArguments[i].c_str();
            AssertPtr(papszArgv[i]);
        }
        papszArgv[cArgs] = NULL;

        /* If the Guest Additions don't support using argv[0] correctly (< 6.1.x), don't supply it. */
        if (!(fGuestControlFeatures0 & VBOX_GUESTCTRL_GF_0_PROCESS_ARGV0))
            vrc = RTGetOptArgvToString(&pszArgs, papszArgv + 1, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
        else /* ... else send the whole argv, including argv[0]. */
            vrc = RTGetOptArgvToString(&pszArgs, papszArgv, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);

        RTMemFree(papszArgv);
        if (RT_FAILURE(vrc))
            return vrc;

        /* Note! No direct returns after this. */
    }

    /* Calculate arguments size (in bytes). */
    AssertPtr(pszArgs);
    cbArgs = strlen(pszArgs) + 1; /* Include terminating zero. */

    /* Prepare environment.  The guest service dislikes the empty string at the end, so drop it. */
    size_t  cbEnvBlock   = 0;    /* Shut up MSVC. */
    char   *pszzEnvBlock = NULL; /* Ditto. */
    vrc = mData.mProcess.mEnvironmentChanges.queryUtf8Block(&pszzEnvBlock, &cbEnvBlock);
    if (RT_SUCCESS(vrc))
    {
        Assert(cbEnvBlock > 0);
        cbEnvBlock--;
        AssertPtr(pszzEnvBlock);

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[16];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetRTCStr(&paParms[i++], mData.mProcess.mExecutable);
        HGCMSvcSetU32(&paParms[i++], mData.mProcess.mFlags);
        HGCMSvcSetU32(&paParms[i++], (uint32_t)mData.mProcess.mArguments.size());
        HGCMSvcSetPv(&paParms[i++], pszArgs, (uint32_t)cbArgs);
        HGCMSvcSetU32(&paParms[i++], mData.mProcess.mEnvironmentChanges.count());
        HGCMSvcSetU32(&paParms[i++], (uint32_t)cbEnvBlock);
        HGCMSvcSetPv(&paParms[i++], pszzEnvBlock, (uint32_t)cbEnvBlock);
        if (uProtocol < 2)
        {
            /* In protocol v1 (VBox < 4.3) the credentials were part of the execution
             * call. In newer protocols these credentials are part of the opened guest
             * session, so not needed anymore here. */
            HGCMSvcSetRTCStr(&paParms[i++], sessionCreds.mUser);
            HGCMSvcSetRTCStr(&paParms[i++], sessionCreds.mPassword);
        }
        /*
         * If the WaitForProcessStartOnly flag is set, we only want to define and wait for a timeout
         * until the process was started - the process itself then gets an infinite timeout for execution.
         * This is handy when we want to start a process inside a worker thread within a certain timeout
         * but let the started process perform lengthly operations then.
         */
        if (mData.mProcess.mFlags & ProcessCreateFlag_WaitForProcessStartOnly)
            HGCMSvcSetU32(&paParms[i++], UINT32_MAX /* Infinite timeout */);
        else
            HGCMSvcSetU32(&paParms[i++], mData.mProcess.mTimeoutMS);
        if (uProtocol >= 2)
        {
            HGCMSvcSetU32(&paParms[i++], mData.mProcess.mPriority);
            /* CPU affinity: We only support one CPU affinity block at the moment,
             * so that makes up to 64 CPUs total. This can be more in the future. */
            HGCMSvcSetU32(&paParms[i++], 1);
            /* The actual CPU affinity blocks. */
            HGCMSvcSetPv(&paParms[i++], (void *)&mData.mProcess.mAffinity, sizeof(mData.mProcess.mAffinity));
            /* Supply working directory, if guest supports it. */
            if (fGuestControlFeatures0 & VBOX_GUESTCTRL_GF_0_PROCESS_CWD)
                HGCMSvcSetRTCStr(&paParms[i++], mData.mProcess.mCwd);
        }

        rLock.release(); /* Drop the write lock before sending. */

        vrc = sendMessage(HOST_MSG_EXEC_CMD, i, paParms);
        if (RT_FAILURE(vrc))
        {
            int vrc2 = i_setProcessStatus(ProcessStatus_Error, vrc);
            AssertRC(vrc2);
        }

        mData.mProcess.mEnvironmentChanges.freeUtf8Block(pszzEnvBlock);
    }

    RTStrFree(pszArgs);

    if (RT_SUCCESS(vrc))
        vrc = i_waitForStatusChange(pEvent, cMsTimeout, NULL /* Process status */, pvrcGuest);
    return vrc;
}

/**
 * Starts the process asynchronously (via worker thread) on the guest.
 *
 * @returns VBox status code.
 */
int GuestProcess::i_startProcessAsync(void)
{
    LogFlowThisFuncEnter();

    /* Create the task: */
    GuestProcessStartTask *pTask = NULL;
    try
    {
        pTask = new GuestProcessStartTask(this);
    }
    catch (std::bad_alloc &)
    {
        LogFlowThisFunc(("out of memory\n"));
        return VERR_NO_MEMORY;
    }
    AssertReturnStmt(pTask->i_isOk(), delete pTask, E_FAIL); /* cannot fail for GuestProcessStartTask. */
    LogFlowThisFunc(("Successfully created GuestProcessStartTask object\n"));

    /* Start the thread (always consumes the task): */
    HRESULT hrc = pTask->createThread();
    pTask = NULL;
    if (SUCCEEDED(hrc))
        return VINF_SUCCESS;
    LogFlowThisFunc(("Failed to create thread for GuestProcessStartTask\n"));
    return VERR_GENERAL_FAILURE;
}

/**
 * Thread task which does the asynchronous starting of a guest process.
 *
 * @returns VBox status code.
 * @param   pTask               Process start task (context) to process.
 */
/* static */
int GuestProcess::i_startProcessThreadTask(GuestProcessStartTask *pTask)
{
    LogFlowFunc(("pTask=%p\n", pTask));

    const ComObjPtr<GuestProcess> pProcess(pTask->i_process());
    Assert(!pProcess.isNull());

    AutoCaller autoCaller(pProcess);
    if (FAILED(autoCaller.hrc()))
        return VERR_COM_UNEXPECTED;

    int vrc = pProcess->i_startProcess(GSTCTL_DEFAULT_TIMEOUT_MS, NULL /* pvrcGuest, ignored */);
    /* Nothing to do here anymore. */

    LogFlowFunc(("pProcess=%p, vrc=%Rrc\n", (GuestProcess *)pProcess, vrc));
    return vrc;
}

/**
 * Terminates a guest process.
 *
 * @returns VBox status code.
 * @retval  VWRN_INVALID_STATE if process not in running state (anymore).
 * @retval  VERR_NOT_SUPPORTED if process termination is not supported on the guest.
 * @param   uTimeoutMS          Timeout (in ms) to wait for process termination.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_terminateProcess(uint32_t uTimeoutMS, int *pvrcGuest)
{
    LogFlowThisFunc(("uTimeoutMS=%RU32\n", uTimeoutMS));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = VINF_SUCCESS;

    if (mData.mStatus != ProcessStatus_Started)
    {
        LogFlowThisFunc(("Process not in started state (state is %RU32), skipping termination\n",
                         mData.mStatus));
        vrc = VWRN_INVALID_STATE;
    }
    else
    {
        AssertPtr(mSession);
        /* Note: VBox < 4.3 (aka protocol version 1) does not
         *       support this, so just skip. */
        if (mSession->i_getProtocolVersion() < 2)
            vrc = VERR_NOT_SUPPORTED;

        if (RT_SUCCESS(vrc))
        {
            GuestWaitEvent *pEvent = NULL;
            GuestEventTypes eventTypes;
            try
            {
                eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);

                vrc = registerWaitEvent(eventTypes, &pEvent);
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
            }

            if (RT_FAILURE(vrc))
                return vrc;

            VBOXHGCMSVCPARM paParms[4];
            int i = 0;
            HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
            HGCMSvcSetU32(&paParms[i++], mData.mPID);

            alock.release(); /* Drop the write lock before sending. */

            vrc = sendMessage(HOST_MSG_EXEC_TERMINATE, i, paParms);
            if (RT_SUCCESS(vrc))
                vrc = i_waitForStatusChange(pEvent, uTimeoutMS,
                                            NULL /* ProcessStatus */, pvrcGuest);
            unregisterWaitEvent(pEvent);
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Converts given process status / flags and wait flag combination
 * to an overall process wait result.
 *
 * @returns  Overall process wait result.
 * @param   fWaitFlags          Process wait flags to use for conversion.
 * @param   oldStatus           Old process status to use for conversion.
 * @param   newStatus           New process status to use for conversion.
 * @param   uProcFlags          Process flags to use for conversion.
 * @param   uProtocol           Guest Control protocol version to use for conversion.
 */
/* static */
ProcessWaitResult_T GuestProcess::i_waitFlagsToResultEx(uint32_t fWaitFlags,
                                                        ProcessStatus_T oldStatus, ProcessStatus_T newStatus,
                                                        uint32_t uProcFlags, uint32_t uProtocol)
{
    ProcessWaitResult_T waitResult = ProcessWaitResult_None;

    switch (newStatus)
    {
        case ProcessStatus_TerminatedNormally:
        case ProcessStatus_TerminatedSignal:
        case ProcessStatus_TerminatedAbnormally:
        case ProcessStatus_Down:
            /* Nothing to wait for anymore. */
            waitResult = ProcessWaitResult_Terminate;
            break;

        case ProcessStatus_TimedOutKilled:
        case ProcessStatus_TimedOutAbnormally:
            /* Dito. */
            waitResult = ProcessWaitResult_Timeout;
            break;

        case ProcessStatus_Started:
            switch (oldStatus)
            {
                case ProcessStatus_Undefined:
                case ProcessStatus_Starting:
                    /* Also wait for process start. */
                    if (fWaitFlags & ProcessWaitForFlag_Start)
                        waitResult = ProcessWaitResult_Start;
                    else
                    {
                        /*
                         * If ProcessCreateFlag_WaitForProcessStartOnly was specified on process creation the
                         * caller is not interested in getting further process statuses -- so just don't notify
                         * anything here anymore and return.
                         */
                        if (uProcFlags & ProcessCreateFlag_WaitForProcessStartOnly)
                            waitResult = ProcessWaitResult_Start;
                    }
                    break;

                case ProcessStatus_Started:
                    /* Only wait for process start. */
                    if (fWaitFlags & ProcessWaitForFlag_Start)
                        waitResult = ProcessWaitResult_Start;
                    break;

                default:
                    AssertMsgFailed(("Unhandled old status %RU32 before new status 'started'\n",
                                     oldStatus));
                    if (fWaitFlags & ProcessWaitForFlag_Start)
                        waitResult = ProcessWaitResult_Start;
                    break;
            }
            break;

        case ProcessStatus_Error:
            /* Nothing to wait for anymore. */
            waitResult = ProcessWaitResult_Error;
            break;

        case ProcessStatus_Undefined:
        case ProcessStatus_Starting:
        case ProcessStatus_Terminating:
        case ProcessStatus_Paused:
            /* No result available yet, leave wait
             * flags untouched. */
            break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
        case ProcessStatus_32BitHack: AssertFailedBreak(); /* (compiler warnings) */
#endif
    }

    if (newStatus == ProcessStatus_Started)
    {
        /*
         * Filter out waits which are *not* supported using
         * older guest control Guest Additions.
         *
         */
        /** @todo ProcessWaitForFlag_Std* flags are not implemented yet. */
        if (uProtocol < 99) /* See @todo above. */
        {
            if (   waitResult == ProcessWaitResult_None
                /* We don't support waiting for stdin, out + err,
                 * just skip waiting then. */
                && (   (fWaitFlags & ProcessWaitForFlag_StdIn)
                    || (fWaitFlags & ProcessWaitForFlag_StdOut)
                    || (fWaitFlags & ProcessWaitForFlag_StdErr)
                   )
               )
            {
                /* Use _WaitFlagNotSupported because we don't know what to tell the caller. */
                waitResult = ProcessWaitResult_WaitFlagNotSupported;
            }
        }
    }

#ifdef DEBUG
    LogFlowFunc(("oldStatus=%RU32, newStatus=%RU32, fWaitFlags=0x%x, waitResult=%RU32\n",
                 oldStatus, newStatus, fWaitFlags, waitResult));
#endif
    return waitResult;
}

/**
 * Converts given wait flags to an overall process wait result.
 *
 * @returns Overall process wait result.
 * @param   fWaitFlags          Process wait flags to use for conversion.
 */
ProcessWaitResult_T GuestProcess::i_waitFlagsToResult(uint32_t fWaitFlags)
{
    AssertPtr(mSession);
    return GuestProcess::i_waitFlagsToResultEx(fWaitFlags,
                                               mData.mStatus /* oldStatus */, mData.mStatus /* newStatus */,
                                               mData.mProcess.mFlags, mSession->i_getProtocolVersion());
}

/**
 * Waits for certain events of the guest process.
 *
 * @returns VBox status code.
 * @param   fWaitFlags          Process wait flags to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   waitResult          Where to return the process wait result on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 * @note    Takes the read lock.
 */
int GuestProcess::i_waitFor(uint32_t fWaitFlags, ULONG uTimeoutMS, ProcessWaitResult_T &waitResult, int *pvrcGuest)
{
    AssertReturn(fWaitFlags, VERR_INVALID_PARAMETER);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("fWaitFlags=0x%x, uTimeoutMS=%RU32, procStatus=%RU32, vrcProc=%Rrc, pvrcGuest=%p\n",
                     fWaitFlags, uTimeoutMS, mData.mStatus, mData.mLastError, pvrcGuest));

    /* Did some error occur before? Then skip waiting and return. */
    ProcessStatus_T curStatus = mData.mStatus;
    if (curStatus == ProcessStatus_Error)
    {
        waitResult = ProcessWaitResult_Error;
        AssertMsg(RT_FAILURE(mData.mLastError),
                             ("No error vrc (%Rrc) set when guest process indicated an error\n", mData.mLastError));
        if (pvrcGuest)
            *pvrcGuest = mData.mLastError; /* Return last set error. */
        LogFlowThisFunc(("Process is in error state (vrcGuest=%Rrc)\n", mData.mLastError));
        return VERR_GSTCTL_GUEST_ERROR;
    }

    waitResult = i_waitFlagsToResult(fWaitFlags);

    /* No waiting needed? Return immediately using the last set error. */
    if (waitResult != ProcessWaitResult_None)
    {
        if (pvrcGuest)
            *pvrcGuest = mData.mLastError; /* Return last set error (if any). */
        LogFlowThisFunc(("Nothing to wait for (vrcGuest=%Rrc)\n", mData.mLastError));
        return RT_SUCCESS(mData.mLastError) ? VINF_SUCCESS : VERR_GSTCTL_GUEST_ERROR;
    }

    /* Adjust timeout. Passing 0 means RT_INDEFINITE_WAIT. */
    if (!uTimeoutMS)
        uTimeoutMS = RT_INDEFINITE_WAIT;

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    alock.release(); /* Release lock before waiting. */

    /*
     * Do the actual waiting.
     */
    ProcessStatus_T newStatus = ProcessStatus_Undefined;
    uint64_t u64StartMS = RTTimeMilliTS();
    for (;;)
    {
        uint64_t u64ElapsedMS = RTTimeMilliTS() - u64StartMS;
        if (   uTimeoutMS   != RT_INDEFINITE_WAIT
            && u64ElapsedMS >= uTimeoutMS)
        {
            vrc = VERR_TIMEOUT;
            break;
        }

        vrc = i_waitForStatusChange(pEvent,
                                    uTimeoutMS == RT_INDEFINITE_WAIT ? RT_INDEFINITE_WAIT : uTimeoutMS - (uint32_t)u64ElapsedMS,
                                    &newStatus, pvrcGuest);
        if (RT_SUCCESS(vrc))
        {
            alock.acquire();

            waitResult = i_waitFlagsToResultEx(fWaitFlags, curStatus, newStatus,
                                               mData.mProcess.mFlags, mSession->i_getProtocolVersion());
#ifdef DEBUG
            LogFlowThisFunc(("Got new status change: fWaitFlags=0x%x, newStatus=%RU32, waitResult=%RU32\n",
                             fWaitFlags, newStatus, waitResult));
#endif
            if (waitResult != ProcessWaitResult_None) /* We got a waiting result. */
                break;
        }
        else /* Waiting failed, bail out. */
            break;

        alock.release(); /* Don't hold lock in next waiting round. */
    }

    unregisterWaitEvent(pEvent);

    LogFlowThisFunc(("returns %Rrc - waitResult=%RU32 newStatus=%RU32 *pvrcGuest=%Rrc\n",
                     vrc, waitResult, newStatus, pvrcGuest ? *pvrcGuest : -VERR_IPE_UNINITIALIZED_STATUS));
    return vrc;
}

/**
 * Waits for a guest process input notification.
 *
 * @param   pEvent              Wait event to use for waiting.
 * @param   uHandle             Guest process file handle to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pInputStatus        Where to return the process input status on success.
 * @param   pcbProcessed        Where to return the processed input (in bytes) on success.
 */
int GuestProcess::i_waitForInputNotify(GuestWaitEvent *pEvent, uint32_t uHandle, uint32_t uTimeoutMS,
                                       ProcessInputStatus_T *pInputStatus, uint32_t *pcbProcessed)
{
    RT_NOREF(uHandle);
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        if (evtType == VBoxEventType_OnGuestProcessInputNotify)
        {
            ComPtr<IGuestProcessInputNotifyEvent> pProcessEvent = pIEvent;
            Assert(!pProcessEvent.isNull());

            if (pInputStatus)
            {
                HRESULT hrc2 = pProcessEvent->COMGETTER(Status)(pInputStatus);
                ComAssertComRC(hrc2);
            }
            if (pcbProcessed)
            {
                HRESULT hrc2 = pProcessEvent->COMGETTER(Processed)((ULONG*)pcbProcessed);
                ComAssertComRC(hrc2);
            }
        }
        else
            vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
    }

    LogFlowThisFunc(("Returning pEvent=%p, uHandle=%RU32, vrc=%Rrc\n", pEvent, uHandle, vrc));
    return vrc;
}

/**
 * Waits for a guest process input notification.
 *
 * @returns VBox status code.
 * @param   pEvent              Wait event to use for waiting.
 * @param   uHandle             Guest process file handle to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvData              Where to store the guest process output on success.
 * @param   cbData              Size (in bytes) of \a pvData.
 * @param   pcbRead             Where to return the size (in bytes) read.
 */
int GuestProcess::i_waitForOutput(GuestWaitEvent *pEvent, uint32_t uHandle, uint32_t uTimeoutMS,
                                  void *pvData, size_t cbData, uint32_t *pcbRead)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);
    /* pvData is optional. */
    /* cbData is optional. */
    /* pcbRead is optional. */

    LogFlowThisFunc(("cEventTypes=%zu, pEvent=%p, uHandle=%RU32, uTimeoutMS=%RU32, pvData=%p, cbData=%zu, pcbRead=%p\n",
                     pEvent->TypeCount(), pEvent, uHandle, uTimeoutMS, pvData, cbData, pcbRead));

    int vrc;

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    do
    {
        vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
        if (RT_SUCCESS(vrc))
        {
            if (evtType == VBoxEventType_OnGuestProcessOutput)
            {
                ComPtr<IGuestProcessOutputEvent> pProcessEvent = pIEvent;
                Assert(!pProcessEvent.isNull());

                ULONG uHandleEvent;
                HRESULT hrc = pProcessEvent->COMGETTER(Handle)(&uHandleEvent);
                if (   SUCCEEDED(hrc)
                    && uHandleEvent == uHandle)
                {
                    if (pvData)
                    {
                        com::SafeArray <BYTE> data;
                        hrc = pProcessEvent->COMGETTER(Data)(ComSafeArrayAsOutParam(data));
                        ComAssertComRC(hrc);
                        size_t cbRead = data.size();
                        if (cbRead)
                        {
                            if (cbRead <= cbData)
                            {
                                /* Copy data from event into our buffer. */
                                memcpy(pvData, data.raw(), data.size());
                            }
                            else
                                vrc = VERR_BUFFER_OVERFLOW;

                            LogFlowThisFunc(("Read %zu bytes (uHandle=%RU32), vrc=%Rrc\n", cbRead, uHandleEvent, vrc));
                        }
                    }

                    if (   RT_SUCCESS(vrc)
                        && pcbRead)
                    {
                        ULONG cbRead;
                        hrc = pProcessEvent->COMGETTER(Processed)(&cbRead);
                        ComAssertComRC(hrc);
                        *pcbRead = (uint32_t)cbRead;
                    }

                    break;
                }
                else if (FAILED(hrc))
                    vrc = VERR_COM_UNEXPECTED;
            }
            else
                vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
        }

    } while (vrc == VINF_SUCCESS);

    if (   vrc != VINF_SUCCESS
        && pcbRead)
    {
        *pcbRead = 0;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Waits for a guest process status change.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   pEvent              Guest wait event to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pProcessStatus      Where to return the process status on success.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned.
 */
int GuestProcess::i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS,
                                        ProcessStatus_T *pProcessStatus, int *pvrcGuest)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);
    /* pProcessStatus is optional. */
    /* pvrcGuest is optional. */

    VBoxEventType_T evtType;
    ComPtr<IEvent> pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS,
                           &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        Assert(evtType == VBoxEventType_OnGuestProcessStateChanged);
        ComPtr<IGuestProcessStateChangedEvent> pProcessEvent = pIEvent;
        Assert(!pProcessEvent.isNull());

        ProcessStatus_T procStatus;
        HRESULT hrc = pProcessEvent->COMGETTER(Status)(&procStatus);
        ComAssertComRC(hrc);
        if (pProcessStatus)
            *pProcessStatus = procStatus;

        ComPtr<IVirtualBoxErrorInfo> errorInfo;
        hrc = pProcessEvent->COMGETTER(Error)(errorInfo.asOutParam());
        ComAssertComRC(hrc);

        LONG lGuestRc;
        hrc = errorInfo->COMGETTER(ResultDetail)(&lGuestRc);
        ComAssertComRC(hrc);

        LogFlowThisFunc(("Got procStatus=%RU32, vrcGuest=%RI32 (%Rrc)\n", procStatus, lGuestRc, lGuestRc));

        if (RT_FAILURE((int)lGuestRc))
            vrc = VERR_GSTCTL_GUEST_ERROR;

        if (pvrcGuest)
            *pvrcGuest = (int)lGuestRc;
    }
    /* waitForEvent may also return VERR_GSTCTL_GUEST_ERROR like we do above, so make pvrcGuest is set. */
    else if (pEvent->HasGuestError() && pvrcGuest)
        *pvrcGuest = pEvent->GuestResult();
    Assert(vrc != VERR_GSTCTL_GUEST_ERROR || !pvrcGuest || *pvrcGuest != (int)0xcccccccc);

    LogFlowFunc(("LEAVE: %Rrc *pvrcGuest=%Rrc *pProcessStatus=%d\n",
                 vrc, pvrcGuest ? *pvrcGuest : -VERR_IPE_UNINITIALIZED_STATUS, pProcessStatus ? *pProcessStatus : -1));
    return vrc;
}

#if 0 /* Unused */
/* static */
bool GuestProcess::i_waitResultImpliesEx(ProcessWaitResult_T waitResult, ProcessStatus_T procStatus, uint32_t uProtocol)
{
    RT_NOREF(uProtocol);

    bool fImplies;

    switch (waitResult)
    {
        case ProcessWaitResult_Start:
            fImplies = procStatus == ProcessStatus_Started;
            break;

        case ProcessWaitResult_Terminate:
            fImplies = (   procStatus == ProcessStatus_TerminatedNormally
                        || procStatus == ProcessStatus_TerminatedSignal
                        || procStatus == ProcessStatus_TerminatedAbnormally
                        || procStatus == ProcessStatus_TimedOutKilled
                        || procStatus == ProcessStatus_TimedOutAbnormally
                        || procStatus == ProcessStatus_Down
                        || procStatus == ProcessStatus_Error);
            break;

        default:
            fImplies = false;
            break;
    }

    return fImplies;
}
#endif /* unused */

/**
 * Writes input data to a guest process.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   uHandle             Guest process file handle to write to.
 * @param   uFlags              Input flags of type PRocessInputFlag_XXX.
 * @param   pvData              Data to write to the guest process.
 * @param   cbData              Size (in bytes) of \a pvData to write.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   puWritten           Where to return the size (in bytes) written. Optional.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @note    Takes the write lock.
 */
int GuestProcess::i_writeData(uint32_t uHandle, uint32_t uFlags,
                              void *pvData, size_t cbData, uint32_t uTimeoutMS, uint32_t *puWritten, int *pvrcGuest)
{
    LogFlowThisFunc(("uPID=%RU32, uHandle=%RU32, uFlags=%RU32, pvData=%p, cbData=%RU32, uTimeoutMS=%RU32, puWritten=%p, pvrcGuest=%p\n",
                     mData.mPID, uHandle, uFlags, pvData, cbData, uTimeoutMS, puWritten, pvrcGuest));
    /* All is optional. There can be 0 byte writes. */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData.mStatus != ProcessStatus_Started)
    {
        if (puWritten)
            *puWritten = 0;
        if (pvrcGuest)
            *pvrcGuest = VINF_SUCCESS;
        return VINF_SUCCESS; /* Not available for writing (anymore). */
    }

    int vrc;

    GuestWaitEvent *pEvent = NULL;
    GuestEventTypes eventTypes;
    try
    {
        /*
         * On Guest Additions < 4.3 there is no guarantee that the process status
         * change arrives *after* the input event, e.g. if this was the last input
         * block being written and the process will report status "terminate".
         * So just skip checking for process status change and only wait for the
         * input event.
         */
        if (mSession->i_getProtocolVersion() >= 2)
            eventTypes.push_back(VBoxEventType_OnGuestProcessStateChanged);
        eventTypes.push_back(VBoxEventType_OnGuestProcessInputNotify);

        vrc = registerWaitEvent(eventTypes, &pEvent);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    VBOXHGCMSVCPARM paParms[5];
    int i = 0;
    HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
    HGCMSvcSetU32(&paParms[i++], mData.mPID);
    HGCMSvcSetU32(&paParms[i++], uFlags);
    HGCMSvcSetPv(&paParms[i++], pvData, (uint32_t)cbData);
    HGCMSvcSetU32(&paParms[i++], (uint32_t)cbData);

    alock.release(); /* Drop the write lock before sending. */

    uint32_t cbProcessed = 0;
    vrc = sendMessage(HOST_MSG_EXEC_SET_INPUT, i, paParms);
    if (RT_SUCCESS(vrc))
    {
        ProcessInputStatus_T inputStatus;
        vrc = i_waitForInputNotify(pEvent, uHandle, uTimeoutMS,
                                   &inputStatus, &cbProcessed);
        if (RT_SUCCESS(vrc))
        {
            /** @todo Set vrcGuest. */

            if (puWritten)
                *puWritten = cbProcessed;
        }
        /** @todo Error handling. */
    }

    unregisterWaitEvent(pEvent);

    LogFlowThisFunc(("Returning cbProcessed=%RU32, vrc=%Rrc\n", cbProcessed, vrc));
    return vrc;
}

// implementation of public methods
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestProcess::read(ULONG aHandle, ULONG aToRead, ULONG aTimeoutMS, std::vector<BYTE> &aData)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    if (aToRead == 0)
        return setError(E_INVALIDARG, tr("The size to read is zero"));

    LogFlowThisFuncEnter();

    aData.resize(aToRead);

    HRESULT hrc = S_OK;

    uint32_t cbRead;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_readData(aHandle, aToRead, aTimeoutMS, &aData.front(), aToRead, &cbRead, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        if (aData.size() != cbRead)
            aData.resize(cbRead);
    }
    else
    {
        aData.resize(0);

        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, mData.mProcess.mExecutable.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                   tr("Reading %RU32 bytes from guest process handle %RU32 failed: %s", "", aToRead),
                                   aToRead, aHandle, GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading from guest process \"%s\" (PID %RU32) failed: %Rrc"),
                                   mData.mProcess.mExecutable.c_str(), mData.mPID, vrc);
                break;
        }
    }

    LogFlowThisFunc(("vrc=%Rrc, cbRead=%RU32\n", vrc, cbRead));
    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestProcess::terminate()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_terminateProcess(GSTCTL_DEFAULT_TIMEOUT_MS, &vrcGuest);

    switch (vrc)
    {
        case VINF_SUCCESS:
            /* Nothing to do here, all good. */
            break;

        case VWRN_INVALID_STATE:
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, mData.mProcess.mExecutable.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, VWRN_INVALID_STATE,
                               tr("Guest process is not in '%s' state anymore (current is in '%s')"),
                               GuestProcess::i_statusToString(ProcessStatus_Started).c_str(),
                               GuestProcess::i_statusToString(i_getStatus()).c_str());
            break;
        }

        case VERR_GSTCTL_GUEST_ERROR:
        {
            GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, mData.mProcess.mExecutable.c_str());
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Terminating guest process failed: %s"),
                               GuestBase::getErrorAsString(ge).c_str());
            break;
        }

        case VERR_NOT_SUPPORTED:
        {
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                               tr("Terminating guest process \"%s\" (PID %RU32) not supported by installed Guest Additions"),
                               mData.mProcess.mExecutable.c_str(), mData.mPID);
            break;
        }

        default:
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Terminating guest process \"%s\" (PID %RU32) failed: %Rrc"),
                               mData.mProcess.mExecutable.c_str(), mData.mPID, vrc);
            break;
    }

    /* Note: Also could be VWRN_INVALID_STATE from i_terminateProcess().
     *       In such a case we have to keep the process in our list in order to fullfill any upcoming responses / requests. */
    if (vrc == VINF_SUCCESS)
    {
        /* Remove process from guest session list. Now only API clients
         * still can hold references to it. */
        AssertPtr(mSession);
        int vrc2 = mSession->i_processUnregister(this);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

/* Deprecated; use GuestProcess::waitForArray() instead. */
HRESULT GuestProcess::waitFor(ULONG aWaitFor, ULONG aTimeoutMS, ProcessWaitResult_T *aReason)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    /* Validate flags: */
    static ULONG const s_fValidFlags = ProcessWaitForFlag_None   | ProcessWaitForFlag_Start  | ProcessWaitForFlag_Terminate
                                     | ProcessWaitForFlag_StdIn  | ProcessWaitForFlag_StdOut | ProcessWaitForFlag_StdErr;
    if (aWaitFor & ~s_fValidFlags)
        return setErrorBoth(E_INVALIDARG, VERR_INVALID_FLAGS, tr("Flags value %#x, invalid: %#x"),
                            aWaitFor, aWaitFor & ~s_fValidFlags);

    /*
     * Note: Do not hold any locks here while waiting!
     */
    HRESULT hrc = S_OK;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    ProcessWaitResult_T waitResult;
    int vrc = i_waitFor(aWaitFor, aTimeoutMS, waitResult, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        *aReason = waitResult;
    }
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, mData.mProcess.mExecutable.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Waiting for guest process (flags %#x) failed: %s"),
                                   aWaitFor, GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            case VERR_TIMEOUT:
                *aReason = ProcessWaitResult_Timeout;
                break;

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Waiting for guest process \"%s\" (PID %RU32) failed: %Rrc"),
                                   mData.mProcess.mExecutable.c_str(), mData.mPID, vrc);
                break;
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestProcess::waitForArray(const std::vector<ProcessWaitForFlag_T> &aWaitFor,
                                   ULONG aTimeoutMS, ProcessWaitResult_T *aReason)
{
    uint32_t fWaitFor = ProcessWaitForFlag_None;
    for (size_t i = 0; i < aWaitFor.size(); i++)
        fWaitFor |= aWaitFor[i];

    return WaitFor(fWaitFor, aTimeoutMS, aReason);
}

HRESULT GuestProcess::write(ULONG aHandle, ULONG aFlags, const std::vector<BYTE> &aData,
                            ULONG aTimeoutMS, ULONG *aWritten)
{
    static ULONG const s_fValidFlags = ProcessInputFlag_None | ProcessInputFlag_EndOfFile;
    if (aFlags & ~s_fValidFlags)
        return setErrorBoth(E_INVALIDARG, VERR_INVALID_FLAGS, tr("Flags value %#x, invalid: %#x"),
                            aFlags, aFlags & ~s_fValidFlags);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    uint32_t cbWritten;
    int      vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    uint32_t cbData   = (uint32_t)aData.size();
    void    *pvData   = cbData > 0 ? (void *)&aData.front() : NULL;
    int vrc = i_writeData(aHandle, aFlags, pvData, cbData, aTimeoutMS, &cbWritten, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Process, vrcGuest, mData.mProcess.mExecutable.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest,
                                   tr("Writing %RU32 bytes (flags %#x) to guest process failed: %s", "", cbData),
                                   cbData, aFlags, GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Writing to guest process \"%s\" (PID %RU32) failed: %Rrc"),
                                   mData.mProcess.mExecutable.c_str(), mData.mPID, vrc);
                break;
        }
    }

    LogFlowThisFunc(("vrc=%Rrc, aWritten=%RU32\n", vrc, cbWritten));

    *aWritten = (ULONG)cbWritten;

    LogFlowFuncLeaveRC(vrc);
    return hrc;
}

HRESULT GuestProcess::writeArray(ULONG aHandle, const std::vector<ProcessInputFlag_T> &aFlags,
                                 const std::vector<BYTE> &aData, ULONG aTimeoutMS, ULONG *aWritten)
{
    LogFlowThisFuncEnter();

    ULONG fWrite = ProcessInputFlag_None;
    for (size_t i = 0; i < aFlags.size(); i++)
        fWrite |= aFlags[i];

    return write(aHandle, fWrite, aData, aTimeoutMS, aWritten);
}

///////////////////////////////////////////////////////////////////////////////

GuestProcessWrapper::GuestProcessWrapper(void)
    : pSession(NULL),
      pProcess(NULL)
{
}

GuestProcessWrapper::~GuestProcessWrapper(void)
{
    uninit();
}

/**
 * Initializes and starts a process on the guest.
 *
 * @returns VBox status code.
 * @param   pGuestSession       Guest session the process should be started in.
 * @param   startupInfo         Guest process startup info to use for starting.
 * @param   fAsync              Whether to start asynchronously or not.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestProcessWrapper::init(GuestSession *pGuestSession, const GuestProcessStartupInfo &startupInfo,
                              bool fAsync, int *pvrcGuest)
{
    LogFlowThisFunc(("pGuestSession=%p, exe=%s, fAsync=%RTbool\n",
                     pGuestSession, startupInfo.mExecutable.c_str(), fAsync));

    AssertPtrReturn(pGuestSession, VERR_INVALID_POINTER);
    Assert(startupInfo.mArguments[0] == startupInfo.mExecutable);

    pSession = pGuestSession;
    mStartupInfo = startupInfo;

    /* Make sure the process is hidden. */
    mStartupInfo.mFlags |= ProcessCreateFlag_Hidden;

    int vrc = pSession->i_processCreateEx(mStartupInfo, pProcess);
    if (RT_SUCCESS(vrc))
    {
        int vrcGuest = VINF_SUCCESS;
        vrc = fAsync
            ? pProcess->i_startProcessAsync()
            : pProcess->i_startProcess(GSTCTL_DEFAULT_TIMEOUT_MS, &vrcGuest);

        if (   RT_SUCCESS(vrc)
            && !fAsync
            && RT_FAILURE(vrcGuest)
           )
        {
            vrc = VERR_GSTCTL_GUEST_ERROR;
        }

        if (pvrcGuest)
            *pvrcGuest = vrcGuest;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Unitializes a guest process by terminating it on the guest.
 */
void GuestProcessWrapper::uninit(void)
{
    /* Make sure the process is terminated and unregistered from the guest session. */
    int vrcGuestIgnored;
    terminate(GSTCTL_DEFAULT_TIMEOUT_MS, &vrcGuestIgnored);

    /* Unregister the process from the process (and the session's object) list. */
    if (   pSession
        && pProcess)
        pSession->i_processUnregister(pProcess);

    /* Release references. */
    pProcess.setNull();
    pSession.setNull();
}

/**
 * Returns whether a guest process is still running or not.
 *
 * @returns \c true if running, or \c false if not.
 */
bool GuestProcessWrapper::isRunning(void)
{
    AssertReturn(!pProcess.isNull(), false);

    ProcessStatus_T procStatus = ProcessStatus_Undefined;
    HRESULT hrc = pProcess->COMGETTER(Status(&procStatus));
    AssertComRC(hrc);

    if (   procStatus == ProcessStatus_Started
        || procStatus == ProcessStatus_Paused
        || procStatus == ProcessStatus_Terminating)
    {
        return true;
    }

    return false;
}

/**
 * Returns whether the guest process has been run correctly or not, based on it's internal process
 * status and reported exit status.
 *
 * @return @c true if the guest process has been run correctly (exit status 0), or @c false if some error
 *         occurred (exit status <> 0 or wrong process state).
 */
bool GuestProcessWrapper::isTerminatedOk(void)
{
    return getTerminationStatus() == VINF_SUCCESS;
}


/**
 * Reports if the guest process has been run correctly.
 *
 * @retval  VINF_SUCCESS if the process has stopped and the exit code is zero.
 * @retval  VERR_GSTCTL_PROCESS_EXIT_CODE if the exit code is _not_ zero.
 * @retval  VERR_GSTCTL_PROCESS_WRONG_STATE if still running.
 *
 * @param   piExitCode      Exit code of the tool. Optional.
 */
int GuestProcessWrapper::getTerminationStatus(int32_t *piExitCode /* = NULL */)
{
    Assert(!pProcess.isNull());
    AssertPtrNull(piExitCode);

    int vrc;
    if (!isRunning())
    {
        LONG iExitCode = -1;
        HRESULT hrc = pProcess->COMGETTER(ExitCode(&iExitCode));
        AssertComRC(hrc);

        if (piExitCode)
            *piExitCode = iExitCode;

        vrc = iExitCode == 0 ? VINF_SUCCESS : VERR_GSTCTL_PROCESS_EXIT_CODE;
    }
    else
    {
        if (piExitCode)
            *piExitCode = -1;
        vrc = VERR_GSTCTL_PROCESS_WRONG_STATE;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Terminates a guest process.
 *
 * @returns VBox status code.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestProcessWrapper::terminate(uint32_t uTimeoutMS, int *pvrcGuest)
{
    LogFlowThisFuncEnter();

    int vrc;
    if (!pProcess.isNull())
        vrc = pProcess->i_terminateProcess(uTimeoutMS, pvrcGuest);
    else
        vrc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

#if 0 /* unused */
/**
 * Default callback implementation for process output.
 *
 * @returns VBox status code.
 * @param   uHandle             Process output handle.
 * @param   vecData             Process output data.
 *
 * @note    Only stdout (handle ID 1) and stderr (handle ID 2) are implemented.
 */
/* virtual */
int GuestProcessWrapper::onOutputCallback(uint32_t uHandle, const std::vector<Utf8Str> &vecData)
{
    GuestProcessOutputStream *pStream = NULL;

    switch (uHandle)
    {
        case GUEST_PROC_OUT_H_STDOUT:
            pStream = &mStdOut;
            break;

        case GUEST_PROC_OUT_H_STDERR:
            pStream = &mStdErr;
            break;

        default:
            /* Ignore. */
            break;
    }

    int vrc = VINF_SUCCESS;

    if (pStream)
    {
        std::vector<Utf8Str>::const_iterator itData = vecData.begin();
        while (itData != vecData.end())
        {
            vrc = pStream->AddData((BYTE *)(*itData).c_str(), (*itData).length());
            if (RT_FAILURE(vrc))
                break;
            ++itData;
        }
    }

    return vrc;
}
#endif

/**
 * Default callback implementation for process output.
 *
 * @returns VBox status code.
 * @param   uHandle             Process output handle.
 * @param   pbData              Pointer to data.
 * @param   cbData              Size (in bytes) of \a pbData.
 *
 * @note    Only stdout (handle ID 1) and stderr (handle ID 2) are implemented.
 */
/* virtual */
int GuestProcessWrapper::onOutputCallback(uint32_t uHandle, const BYTE *pbData, size_t cbData)
{
    AssertPtrReturn(pbData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    GuestProcessOutputStream *pStream = NULL;

    switch (uHandle)
    {
        case GUEST_PROC_OUT_H_STDOUT:
            pStream = &mStdOut;
            break;

        case GUEST_PROC_OUT_H_STDERR:
            pStream = &mStdErr;
            break;

        default:
            /* Ignore. */
            break;
    }

    int vrc = VINF_SUCCESS;

    if (pStream)
        vrc = pStream->AddData(pbData, cbData);

    return vrc;
}

/**
 * Default callback implementation for process input.
 * Does not provide any input data.
 *
 * @returns VBox status code.
 * @param   uHandle             Process input handle.
 * @param   vecData             Process input data.
 */
/* virtual */
int GuestProcessWrapper::onInputCallback(uint32_t uHandle, std::vector<Utf8Str> &vecData)
{
    RT_NOREF(uHandle);

    vecData.clear();

    /* No input data given by default. */
    return VINF_SUCCESS;
}

/**
 * Waits for a guest process and handle input / output via callbacks.
 *
 * @returns VBox status code.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestProcessWrapper::wait(int *pvrcGuest)
{
    int vrc;

    /* Do the waiting. */
    uint32_t fProcWaitForFlags = ProcessWaitForFlag_Terminate;
    if (mStartupInfo.mFlags & ProcessCreateFlag_WaitForStdOut)
        fProcWaitForFlags |= ProcessWaitForFlag_StdOut;
    if (mStartupInfo.mFlags & ProcessCreateFlag_WaitForStdErr)
        fProcWaitForFlags |= ProcessWaitForFlag_StdErr;

    /** @todo Decrease timeout while running. */
    uint64_t u64StartMS = RTTimeMilliTS();
    uint32_t uTimeoutMS = mStartupInfo.mTimeoutMS;

    int vrcGuest = VINF_SUCCESS;
    bool fDone = false;

    BYTE abBuf[_64K];
    uint32_t cbRead;

    bool fHandleStdOut = false;
    bool fHandleStdErr = false;

    /**
     * Updates the elapsed time and checks if a
     * timeout happened, then breaking out of the loop.
     */
#define UPDATE_AND_CHECK_ELAPSED_TIME()          \
    u64ElapsedMS = RTTimeMilliTS() - u64StartMS; \
    if (   uTimeoutMS   != RT_INDEFINITE_WAIT    \
        && u64ElapsedMS >= uTimeoutMS)           \
    {                                            \
        vrc = VERR_TIMEOUT;                      \
        break;                                   \
    }

    /**
     * Returns the remaining time (in ms).
     */
#define GET_REMAINING_TIME (uTimeoutMS == RT_INDEFINITE_WAIT ? RT_INDEFINITE_WAIT : uTimeoutMS - (uint32_t)u64ElapsedMS)

    ProcessWaitResult_T waitRes = ProcessWaitResult_None;
    do
    {
        uint64_t u64ElapsedMS;
        UPDATE_AND_CHECK_ELAPSED_TIME();

        vrc = pProcess->i_waitFor(fProcWaitForFlags, GET_REMAINING_TIME, waitRes, &vrcGuest);
        if (RT_FAILURE(vrc))
            break;

        switch (waitRes)
        {
            case ProcessWaitResult_StdIn:
                vrc = VERR_NOT_IMPLEMENTED;
                break;

            case ProcessWaitResult_StdOut:
                fHandleStdOut = true;
                break;

            case ProcessWaitResult_StdErr:
                fHandleStdErr = true;
                break;

            case ProcessWaitResult_WaitFlagNotSupported:
                if (fProcWaitForFlags & ProcessWaitForFlag_StdOut)
                    fHandleStdOut = true;
                if (fProcWaitForFlags & ProcessWaitForFlag_StdErr)
                    fHandleStdErr = true;
                /* Since waiting for stdout / stderr is not supported by the guest,
                 * wait a bit to not hog the CPU too much when polling for data. */
                RTThreadSleep(1); /* Optional, don't check vrc. */
                break;

            case ProcessWaitResult_Error:
                vrc = VERR_GSTCTL_GUEST_ERROR;
                break;

            case ProcessWaitResult_Terminate:
                fDone = true;
                break;

            case ProcessWaitResult_Timeout:
                vrc = VERR_TIMEOUT;
                break;

            case ProcessWaitResult_Start:
            case ProcessWaitResult_Status:
                /* Not used here, just skip. */
                break;

            default:
                AssertMsgFailed(("Unhandled process wait result %RU32\n", waitRes));
                break;
        }

        if (RT_FAILURE(vrc))
            break;

#define HANDLE_OUTPUT(a_Handle) \
            cbRead = 0; \
            vrc = pProcess->i_readData(a_Handle, sizeof(abBuf), \
                                       GET_REMAINING_TIME, \
                                       abBuf, sizeof(abBuf), \
                                       &cbRead, &vrcGuest); \
            if (   RT_FAILURE(vrc) \
                || vrc == VWRN_GSTCTL_OBJECTSTATE_CHANGED) \
                break; \
            if (cbRead) \
                vrc = onOutputCallback(GUEST_PROC_OUT_H_STDOUT, abBuf, cbRead);

        if (fHandleStdOut)
        {
            UPDATE_AND_CHECK_ELAPSED_TIME();
            HANDLE_OUTPUT(GUEST_PROC_OUT_H_STDOUT);
            fHandleStdOut = false;
        }

        if (fHandleStdErr)
        {
            UPDATE_AND_CHECK_ELAPSED_TIME();
            HANDLE_OUTPUT(GUEST_PROC_OUT_H_STDERR);
            fHandleStdErr = false;
        }

#undef HANDLE_OUTPUT

    } while (!fDone && RT_SUCCESS(vrc));

#undef UPDATE_AND_CHECK_ELAPSED_TIME
#undef GET_REMAINING_TIME

    if (RT_FAILURE(vrcGuest))
        vrc = VERR_GSTCTL_GUEST_ERROR;

    LogFlowThisFunc(("Loop ended with vrc=%Rrc, vrcGuest=%Rrc, waitRes=%RU32\n", vrc, vrcGuest, waitRes));
    if (pvrcGuest)
        *pvrcGuest = vrcGuest;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

///////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
GuestProcessToolbox::GuestProcessToolbox(void)
{
}

GuestProcessToolbox::~GuestProcessToolbox(void)
{
}

/**
 * Waits for a guest toolbox process.
 *
 * @returns VBox status code.
 * @param   fToolWaitFlags      Guest process tool wait flags to use for waiting.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 */
int GuestProcessToolbox::wait(uint32_t fToolWaitFlags, int *pvrcGuest)
{
    return waitEx(fToolWaitFlags, NULL /* pStrmBlkOut */, pvrcGuest);
}

/**
 * Waits for a guest toolbox process, also returning process output.
 *
 * @returns VBox status code.
 * @param   fToolWaitFlags      Guest process tool wait flags to use for waiting.
 * @param   pStrmBlkOut         Where to store the guest toolbox output.
 * @param   pvrcGuest           Where to return the guest error when
 *                              VERR_GSTCTL_GUEST_ERROR was returned. Optional.
 *
 * @sa GuestProcessWrapper::wait() -- mostly duplicate code, kept for backwards compatibility.
 */
int GuestProcessToolbox::waitEx(uint32_t fToolWaitFlags, GuestToolboxStreamBlock *pStrmBlkOut, int *pvrcGuest)
{
    LogFlowThisFunc(("fToolWaitFlags=0x%x, pStrmBlkOut=%p, pvrcGuest=%p\n", fToolWaitFlags, pStrmBlkOut, pvrcGuest));

    int vrc;

    /* Is the next block complete without waiting for new data from the guest? */
    if (fToolWaitFlags & GUESTPROCESSTOOL_WAIT_FLAG_STDOUT_BLOCK)
    {
        AssertPtr(pStrmBlkOut);
        vrc = getCurrentBlock(GUEST_PROC_OUT_H_STDOUT, *pStrmBlkOut);
        if (   RT_SUCCESS(vrc)
            && pStrmBlkOut->IsComplete())
            return vrc;
        /* else do the waiting below. */
    }

    /* Do the waiting. */
    uint32_t fProcWaitForFlags = ProcessWaitForFlag_Terminate;
    if (mStartupInfo.mFlags & ProcessCreateFlag_WaitForStdOut)
        fProcWaitForFlags |= ProcessWaitForFlag_StdOut;
    if (mStartupInfo.mFlags & ProcessCreateFlag_WaitForStdErr)
        fProcWaitForFlags |= ProcessWaitForFlag_StdErr;

    /** @todo Decrease timeout while running. */
    uint64_t u64StartMS = RTTimeMilliTS();
    uint32_t uTimeoutMS = mStartupInfo.mTimeoutMS;

    int vrcGuest = VINF_SUCCESS;
    bool fDone = false;

    BYTE abBuf[_64K];
    uint32_t cbRead;

    bool fHandleStdOut = false;
    bool fHandleStdErr = false;

    /**
     * Updates the elapsed time and checks if a
     * timeout happened, then breaking out of the loop.
     */
#define UPDATE_AND_CHECK_ELAPSED_TIME()          \
    u64ElapsedMS = RTTimeMilliTS() - u64StartMS; \
    if (   uTimeoutMS   != RT_INDEFINITE_WAIT    \
        && u64ElapsedMS >= uTimeoutMS)           \
    {                                            \
        vrc = VERR_TIMEOUT;                      \
        break;                                   \
    }

    /**
     * Returns the remaining time (in ms).
     */
#define GET_REMAINING_TIME (uTimeoutMS == RT_INDEFINITE_WAIT ? RT_INDEFINITE_WAIT : uTimeoutMS - (uint32_t)u64ElapsedMS)

    ProcessWaitResult_T waitRes = ProcessWaitResult_None;
    do
    {
        uint64_t u64ElapsedMS;
        UPDATE_AND_CHECK_ELAPSED_TIME();

        vrc = pProcess->i_waitFor(fProcWaitForFlags, GET_REMAINING_TIME, waitRes, &vrcGuest);
        if (RT_FAILURE(vrc))
            break;

        switch (waitRes)
        {
            case ProcessWaitResult_StdIn:
                vrc = VERR_NOT_IMPLEMENTED;
                break;

            case ProcessWaitResult_StdOut:
                fHandleStdOut = true;
                break;

            case ProcessWaitResult_StdErr:
                fHandleStdErr = true;
                break;

            case ProcessWaitResult_WaitFlagNotSupported:
                if (fProcWaitForFlags & ProcessWaitForFlag_StdOut)
                    fHandleStdOut = true;
                if (fProcWaitForFlags & ProcessWaitForFlag_StdErr)
                    fHandleStdErr = true;
                /* Since waiting for stdout / stderr is not supported by the guest,
                 * wait a bit to not hog the CPU too much when polling for data. */
                RTThreadSleep(1); /* Optional, don't check vrc. */
                break;

            case ProcessWaitResult_Error:
                vrc = VERR_GSTCTL_GUEST_ERROR;
                break;

            case ProcessWaitResult_Terminate:
                fDone = true;
                break;

            case ProcessWaitResult_Timeout:
                vrc = VERR_TIMEOUT;
                break;

            case ProcessWaitResult_Start:
            case ProcessWaitResult_Status:
                /* Not used here, just skip. */
                break;

            default:
                AssertMsgFailed(("Unhandled process wait result %RU32\n", waitRes));
                break;
        }

        if (RT_FAILURE(vrc))
            break;

        if (fHandleStdOut)
        {
            UPDATE_AND_CHECK_ELAPSED_TIME();

            cbRead = 0;
            vrc = pProcess->i_readData(GUEST_PROC_OUT_H_STDOUT, sizeof(abBuf),
                                       GET_REMAINING_TIME,
                                       abBuf, sizeof(abBuf),
                                       &cbRead, &vrcGuest);
            if (   RT_FAILURE(vrc)
                || vrc == VWRN_GSTCTL_OBJECTSTATE_CHANGED)
                break;

            if (cbRead)
            {
                LogFlowThisFunc(("Received %RU32 bytes from stdout\n", cbRead));
                vrc = mStdOut.AddData(abBuf, cbRead);

                if (   RT_SUCCESS(vrc)
                    && (fToolWaitFlags & GUESTPROCESSTOOL_WAIT_FLAG_STDOUT_BLOCK))
                {
                    AssertPtr(pStrmBlkOut);
                    vrc = getCurrentBlock(GUEST_PROC_OUT_H_STDOUT, *pStrmBlkOut);

                    /* When successful, break out of the loop because we're done
                     * with reading the first stream block. */
                    if (RT_SUCCESS(vrc))
                        fDone = true;
                }
            }

            fHandleStdOut = false;
        }

        if (fHandleStdErr)
        {
            UPDATE_AND_CHECK_ELAPSED_TIME();

            cbRead = 0;
            vrc = pProcess->i_readData(GUEST_PROC_OUT_H_STDERR, sizeof(abBuf),
                                       GET_REMAINING_TIME,
                                       abBuf, sizeof(abBuf),
                                       &cbRead, &vrcGuest);
            if (   RT_FAILURE(vrc)
                || vrc == VWRN_GSTCTL_OBJECTSTATE_CHANGED)
                break;

            if (cbRead)
            {
                LogFlowThisFunc(("Received %RU32 bytes from stderr\n", cbRead));
                vrc = mStdErr.AddData(abBuf, cbRead);
            }

            fHandleStdErr = false;
        }

    } while (!fDone && RT_SUCCESS(vrc));

#undef UPDATE_AND_CHECK_ELAPSED_TIME
#undef GET_REMAINING_TIME

    if (RT_FAILURE(vrcGuest))
        vrc = VERR_GSTCTL_GUEST_ERROR;

    LogFlowThisFunc(("Loop ended with vrc=%Rrc, vrcGuest=%Rrc, waitRes=%RU32\n", vrc, vrcGuest, waitRes));
    if (pvrcGuest)
        *pvrcGuest = vrcGuest;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Gets the current guest toolbox process stream block.
 *
 * @returns VBox status code.
 * @retval  VINF_EOF if the stream reached its end.
 * @param   uHandle             Guest process file handle to get current block for.
 * @param   strmBlock           Where to return the stream block on success.
 */
int GuestProcessToolbox::getCurrentBlock(uint32_t uHandle, GuestToolboxStreamBlock &strmBlock)
{
    GuestToolboxStream *pStream = NULL;
    if (uHandle == GUEST_PROC_OUT_H_STDOUT)
        pStream = &mStdOut;
    else if (uHandle == GUEST_PROC_OUT_H_STDERR)
        pStream = &mStdErr;

    if (!pStream)
        return VERR_INVALID_PARAMETER;

    int vrc = pStream->ParseBlock(strmBlock);

    LogFlowThisFunc(("vrc=%Rrc, currently %RU64 pairs\n", vrc, strmBlock.GetCount()));
    return vrc;
}

/**
 * Returns the result code from an ended guest toolbox process.
 *
 * @returns Result code from guest toolbox process.
 */
int GuestProcessToolbox::getRc(void) const
{
    LONG exitCode = -1;
    HRESULT hrc = pProcess->COMGETTER(ExitCode(&exitCode));
    AssertComRC(hrc);

    return GuestProcessToolbox::exitCodeToRc(mStartupInfo, exitCode);
}

/**
 * Static helper function to start and wait for output of a certain toolbox tool.
 *
 * The optional @a pLstStdOutObjects argument enables the caller to retrieve
 * so-called parsable guest stream objects containing additional information on
 * a key value pair basis.  Those objects are emitted on the guest side by the
 * tool to convey retrieve additional information, like names in a directory or
 * file statistics, to the host.
 *
 * @return  IPRT status code.
 * @param   pGuestSession           Guest control session to use for starting the toolbox tool in.
 * @param   startupInfo             Startup information about the toolbox tool.
 * @param   pvrcGuest               Error code returned from the guest side if VERR_GSTCTL_GUEST_ERROR is returned.
 * @param   pLstStdOutObjects       Pointer to vector to receive the stdout
 *                                  objects.  Optional.
 * @param   cStdOutObjectsToRead    Number of parsable guest stream objects to
 *                                  try receive and parse.  Ignored when
 *                                  pLstStdOutObjects is NULL.  Optional.
 */
/* static */
int GuestProcessToolbox::runTool(GuestSession                  *pGuestSession,
                                 GuestProcessStartupInfo const &startupInfo,
                                 int                           *pvrcGuest,
                                 GuestCtrlStreamObjects        *pLstStdOutObjects /* = NULL */,
                                 uint32_t                       cStdOutObjectsToRead /* = 1 */)
{
    GuestProcessToolErrorInfo errorInfo = { VERR_IPE_UNINITIALIZED_STATUS, INT32_MAX };
    int vrc = GuestProcessToolbox::runToolWithErrorInfo(pGuestSession, startupInfo,
                                                        errorInfo, pLstStdOutObjects, cStdOutObjectsToRead);
    if (RT_SUCCESS(vrc))
    {
        /* Translate exit statuses to VBox statuses according to the guest tool: */
        if (errorInfo.vrcGuest == VERR_GSTCTL_PROCESS_EXIT_CODE)
        {
            errorInfo.vrcGuest = GuestProcessToolbox::exitCodeToRc(startupInfo, errorInfo.iExitCode);
            vrc = VERR_GSTCTL_GUEST_ERROR;
        }
        /* In case of something else that may be relevant: */
        else if (errorInfo.vrcGuest == VERR_GSTCTL_PROCESS_WRONG_STATE /* possible getTerminationStatus result, see below */)
            vrc = VERR_GSTCTL_GUEST_ERROR;
        else
            AssertMsg(errorInfo.vrcGuest == VERR_IPE_UNINITIALIZED_STATUS || errorInfo.vrcGuest == VINF_SUCCESS,
                      ("vrcGuest=%Rrc iExitCode=%d vrc=%Rrc\n", errorInfo.vrcGuest, errorInfo.iExitCode, vrc));
    }

    *pvrcGuest = errorInfo.vrcGuest;
    LogFlowFunc(("returns %Rrc - vrcGuest=%Rrc iExitCode=%d\n", vrc, errorInfo.vrcGuest, errorInfo.iExitCode));
    return vrc;
}

/**
 * Bare-bone version of runTool that doesn't translate exit codes to statuses.
 *
 * It's currently not used by anyone other than runTool().
 *
 * @return  VBox status code.
 * @param   pGuestSession           Guest control session to use for starting the toolbox tool in.
 * @param   startupInfo             Startup information about the toolbox tool.
 * @param   errorInfo               Error information returned for error handling.
 * @param   pLstStdOutObjects       Pointer to vector to receive the stdout
 *                                  objects.  Optional.
 * @param   cStdOutObjectsToRead    Number of parsable guest stream objects to
 *                                  try receive and parse.  Ignored when
 *                                  pLstStdOutObjects is NULL.  Optional.
 *
 * @note    This will not do call exitCodeToRc, for that use runTool().
 */
/* static */
int GuestProcessToolbox::runToolWithErrorInfo(GuestSession                  *pGuestSession,
                                              GuestProcessStartupInfo const &startupInfo,
                                              GuestProcessToolErrorInfo     &errorInfo,
                                              GuestCtrlStreamObjects        *pLstStdOutObjects /* = NULL */,
                                              uint32_t                       cStdOutObjectsToRead /* = 1 */)
{
    GuestProcessToolbox procTool;
    int vrc = procTool.init(pGuestSession, startupInfo, false /* fAsync */, &errorInfo.vrcGuest);
    if (pLstStdOutObjects)
        while (RT_SUCCESS(vrc) && cStdOutObjectsToRead-- > 0)
            try
            {
                GuestToolboxStreamBlock strmBlk;
                vrc = procTool.waitEx(GUESTPROCESSTOOL_WAIT_FLAG_STDOUT_BLOCK, &strmBlk, &errorInfo.vrcGuest);
                pLstStdOutObjects->push_back(strmBlk);
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
            }
    if (RT_SUCCESS(vrc))
    {
        /* Make sure the process runs until completion. */
        vrc = procTool.wait(GUESTPROCESSTOOL_WAIT_FLAG_NONE, &errorInfo.vrcGuest);
        if (RT_SUCCESS(vrc))
            errorInfo.vrcGuest = procTool.getTerminationStatus(&errorInfo.iExitCode);
    }

    LogFlowFunc(("returns %Rrc - vrcGuest=%Rrc iExitCode=%d (%#x)\n",
                 vrc, errorInfo.vrcGuest, errorInfo.iExitCode, errorInfo.iExitCode));
    return vrc;
}

/**
 * Converts a toolbox tool's exit code to an IPRT error code.
 *
 * @returns VBox status code.
 * @param   startupInfo         Startup info of the toolbox tool to lookup error code for.
 * @param   iExitCode           The toolbox tool's exit code to lookup IPRT error for.
 */
/* static */
int GuestProcessToolbox::exitCodeToRc(const GuestProcessStartupInfo &startupInfo, int32_t iExitCode)
{
    if (startupInfo.mArguments.size() == 0)
    {
        AssertFailed();
        return VERR_GENERAL_FAILURE; /* Should not happen. */
    }

    return exitCodeToRc(startupInfo.mArguments[0].c_str(), iExitCode);
}

/**
 * Converts a toolbox tool's exit code to an IPRT error code.
 *
 * @returns VBox status code.
 * @param   pszTool             Name of toolbox tool to lookup error code for.
 * @param   iExitCode           The toolbox tool's exit code to lookup IPRT error for.
 */
/* static */
int GuestProcessToolbox::exitCodeToRc(const char *pszTool, int32_t iExitCode)
{
    AssertPtrReturn(pszTool, VERR_INVALID_POINTER);

    LogFlowFunc(("%s: %d\n", pszTool, iExitCode));

    if (iExitCode == 0) /* No error? Bail out early. */
        return VINF_SUCCESS;

    if (!RTStrICmp(pszTool, VBOXSERVICE_TOOL_CAT))
    {
        switch (iExitCode)
        {
            case VBOXSERVICETOOLBOX_CAT_EXITCODE_ACCESS_DENIED:     return VERR_ACCESS_DENIED;
            case VBOXSERVICETOOLBOX_CAT_EXITCODE_FILE_NOT_FOUND:    return VERR_FILE_NOT_FOUND;
            case VBOXSERVICETOOLBOX_CAT_EXITCODE_PATH_NOT_FOUND:    return VERR_PATH_NOT_FOUND;
            case VBOXSERVICETOOLBOX_CAT_EXITCODE_SHARING_VIOLATION: return VERR_SHARING_VIOLATION;
            case VBOXSERVICETOOLBOX_CAT_EXITCODE_IS_A_DIRECTORY:    return VERR_IS_A_DIRECTORY;
            default:                                                break;
        }
    }
    else if (!RTStrICmp(pszTool, VBOXSERVICE_TOOL_LS))
    {
        switch (iExitCode)
        {
            /** @todo Handle access denied? */
            case RTEXITCODE_FAILURE: return VERR_PATH_NOT_FOUND;
            default:                 break;
        }
    }
    else if (!RTStrICmp(pszTool, VBOXSERVICE_TOOL_STAT))
    {
        switch (iExitCode)
        {
            case VBOXSERVICETOOLBOX_STAT_EXITCODE_ACCESS_DENIED:      return VERR_ACCESS_DENIED;
            case VBOXSERVICETOOLBOX_STAT_EXITCODE_FILE_NOT_FOUND:     return VERR_FILE_NOT_FOUND;
            case VBOXSERVICETOOLBOX_STAT_EXITCODE_PATH_NOT_FOUND:     return VERR_PATH_NOT_FOUND;
            case VBOXSERVICETOOLBOX_STAT_EXITCODE_NET_PATH_NOT_FOUND: return VERR_NET_PATH_NOT_FOUND;
            default:                                                  break;
        }
    }
    else if (!RTStrICmp(pszTool, VBOXSERVICE_TOOL_MKDIR))
    {
        switch (iExitCode)
        {
            case RTEXITCODE_FAILURE: return VERR_CANT_CREATE;
            default:                 break;
        }
    }
    else if (!RTStrICmp(pszTool, VBOXSERVICE_TOOL_MKTEMP))
    {
        switch (iExitCode)
        {
            case RTEXITCODE_FAILURE: return VERR_CANT_CREATE;
            default:                 break;
        }
    }
    else if (!RTStrICmp(pszTool, VBOXSERVICE_TOOL_RM))
    {
        switch (iExitCode)
        {
            case RTEXITCODE_FAILURE: return VERR_FILE_NOT_FOUND;
            /** @todo RTPathRmCmd does not yet distinguish between not found and access denied yet. */
            default:                 break;
        }
    }

    LogFunc(("Warning: Exit code %d not handled for tool '%s', returning VERR_GENERAL_FAILURE\n", iExitCode, pszTool));

    if (iExitCode == RTEXITCODE_SYNTAX)
        return VERR_INTERNAL_ERROR_5;
    return VERR_GENERAL_FAILURE;
}

/**
 * Returns a stringyfied error of a guest process tool error.
 *
 * @returns Stringyfied error.
 * @param   pszTool             Toolbox tool name to get stringyfied error for.
 * @param   guestErrorInfo      Guest error info to get stringyfied error for.
 */
/* static */
Utf8Str GuestProcessToolbox::guestErrorToString(const char *pszTool, const GuestErrorInfo &guestErrorInfo)
{
    Utf8Str strErr;

    /** @todo pData->u32Flags: int vs. uint32 -- IPRT errors are *negative* !!! */
    switch (guestErrorInfo.getVrc())
    {
        case VERR_ACCESS_DENIED:
            strErr.printf(tr("Access to \"%s\" denied"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_FILE_NOT_FOUND: /* This is the most likely error. */
            RT_FALL_THROUGH();
        case VERR_PATH_NOT_FOUND:
            strErr.printf(tr("No such file or directory \"%s\""), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_INVALID_VM_HANDLE:
            strErr.printf(tr("VMM device is not available (is the VM running?)"));
            break;

        case VERR_HGCM_SERVICE_NOT_FOUND:
            strErr.printf(tr("The guest execution service is not available"));
            break;

        case VERR_BAD_EXE_FORMAT:
            strErr.printf(tr("The file \"%s\" is not an executable format"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_AUTHENTICATION_FAILURE:
            strErr.printf(tr("The user \"%s\" was not able to logon"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_INVALID_NAME:
            strErr.printf(tr("The file \"%s\" is an invalid name"), guestErrorInfo.getWhat().c_str());
            break;

        case VERR_TIMEOUT:
            strErr.printf(tr("The guest did not respond within time"));
            break;

        case VERR_CANCELLED:
            strErr.printf(tr("The execution operation was canceled"));
            break;

        case VERR_GSTCTL_MAX_CID_OBJECTS_REACHED:
            strErr.printf(tr("Maximum number of concurrent guest processes has been reached"));
            break;

        case VERR_NOT_FOUND:
            strErr.printf(tr("The guest execution service is not ready (yet)"));
            break;

        default:
            strErr.printf(tr("Unhandled error %Rrc for \"%s\" occurred for tool \"%s\" on guest -- please file a bug report"),
                          guestErrorInfo.getVrc(), guestErrorInfo.getWhat().c_str(), pszTool);
            break;
    }

    return strErr;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */
