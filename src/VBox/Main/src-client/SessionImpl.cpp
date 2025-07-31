/* $Id: SessionImpl.cpp 110489 2025-07-31 08:32:38Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Client Session COM Class implementation in VBoxC.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_SESSION
#include "LoggingNew.h"

#include "SessionImpl.h"
#include "ConsoleImpl.h"
#include "ClientTokenHolder.h"
#include "Global.h"
#include "StringifyEnums.h"

#include "AutoCaller.h"

#include <iprt/errcore.h>
#include <iprt/process.h>


/**
 *  Local macro to check whether the session is open and return an error if not.
 *  @note Don't forget to do |Auto[Reader]Lock alock (this);| before using this
 *  macro.
 */
#define CHECK_OPEN() \
    do { \
        if (mState != SessionState_Locked) \
            return setError(E_UNEXPECTED, Session::tr("The session is not locked (session state: %s)"), \
                            Global::stringifySessionState(mState)); \
    } while (0)

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

Session::Session()
{
}

Session::~Session()
{
}

HRESULT Session::FinalConstruct()
{
    LogFlowThisFunc(("\n"));

    HRESULT hrc = init();

    BaseFinalConstruct();

    return hrc;
}

void Session::FinalRelease()
{
    LogFlowThisFunc(("\n"));

    uninit();

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the Session object.
 */
HRESULT Session::init()
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    LogFlowThisFuncEnter();

    mState = SessionState_Unlocked;
    mType = SessionType_Null;

    mClientTokenHolder = NULL;

    /* Confirm a successful initialization when it's the case */
    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();

    return S_OK;
}

/**
 *  Uninitializes the Session object.
 *
 *  @note Locks this object for writing.
 */
void Session::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
    {
        LogFlowThisFunc(("Already uninitialized.\n"));
        LogFlowThisFuncLeave();
        return;
    }

    /* close() needs write lock */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mState != SessionState_Unlocked)
    {
        Assert(mState == SessionState_Locked ||
               mState == SessionState_Spawning);

        HRESULT hrc = i_unlockMachine(true /* aFinalRelease */, false /* aFromServer */, alock);
        AssertComRC(hrc);
    }

    LogFlowThisFuncLeave();
}

// ISession properties
/////////////////////////////////////////////////////////////////////////////

HRESULT Session::getState(SessionState_T *aState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aState = mState;

    return S_OK;
}

HRESULT Session::getType(SessionType_T *aType)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    CHECK_OPEN();

    *aType = mType;
    return S_OK;
}

HRESULT Session::getName(com::Utf8Str &aName)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aName = mName;
    return S_OK;
}

HRESULT Session::setName(const com::Utf8Str &aName)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mState != SessionState_Unlocked)
        return setError(VBOX_E_INVALID_OBJECT_STATE, tr("Trying to set name for a session which is not in state \"unlocked\""));

    mName = aName;
    return S_OK;
}

HRESULT Session::getMachine(ComPtr<IMachine> &aMachine)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    CHECK_OPEN();

    HRESULT hrc;
#ifndef VBOX_COM_INPROC_API_CLIENT
    if (mConsole)
        hrc = mConsole->i_machine().queryInterfaceTo(aMachine.asOutParam());
    else
#endif
        hrc = mRemoteMachine.queryInterfaceTo(aMachine.asOutParam());
    if (FAILED(hrc))
    {
#ifndef VBOX_COM_INPROC_API_CLIENT
        if (mConsole)
            setError(hrc, tr("Failed to query the session machine"));
        else
#endif
        if (FAILED_DEAD_INTERFACE(hrc))
            setError(hrc, tr("Peer process crashed"));
        else
            setError(hrc, tr("Failed to query the remote session machine"));
    }

    return hrc;
}

HRESULT Session::getConsole(ComPtr<IConsole> &aConsole)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    CHECK_OPEN();

    HRESULT hrc = S_OK;
#ifndef VBOX_COM_INPROC_API_CLIENT
    if (mConsole)
        hrc = mConsole.queryInterfaceTo(aConsole.asOutParam());
    else
#endif
        hrc = mRemoteConsole.queryInterfaceTo(aConsole.asOutParam());

    if (FAILED(hrc))
    {
#ifndef VBOX_COM_INPROC_API_CLIENT
        if (mConsole)
            setError(hrc, tr("Failed to query the console"));
        else
#endif
        if (FAILED_DEAD_INTERFACE(hrc))
            setError(hrc, tr("Peer process crashed"));
        else
            setError(hrc, tr("Failed to query the remote console"));
    }

    return hrc;
}

// ISession methods
/////////////////////////////////////////////////////////////////////////////
HRESULT Session::unlockMachine()
{
    LogFlowThisFunc(("mState=%d, mType=%d\n", mState, mType));

    /* close() needs write lock */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    CHECK_OPEN();
    return i_unlockMachine(false /* aFinalRelease */, false /* aFromServer */, alock);
}

// IInternalSessionControl methods
/////////////////////////////////////////////////////////////////////////////
HRESULT Session::getPID(ULONG *aPid)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aPid = (ULONG)RTProcSelf();
    AssertCompile(sizeof(*aPid) == sizeof(RTPROCESS));

    return S_OK;
}

HRESULT Session::getRemoteConsole(ComPtr<IConsole> &aConsole)
{
    LogFlowThisFuncEnter();
#ifndef VBOX_COM_INPROC_API_CLIENT
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mType == SessionType_WriteLock && !!mConsole)
    {
        /* return a failure if the session already transitioned to Closing
         * but the server hasn't processed Machine::OnSessionEnd() yet. */
        if (mState == SessionState_Locked)
        {
            mConsole.queryInterfaceTo(aConsole.asOutParam());

            LogFlowThisFuncLeave();
            return S_OK;
        }
        return VBOX_E_INVALID_VM_STATE;
    }
    return setError(VBOX_E_INVALID_OBJECT_STATE, "This is not a direct session");

#else  /* VBOX_COM_INPROC_API_CLIENT */
    RT_NOREF(aConsole);
    AssertFailed();
    return VBOX_E_INVALID_OBJECT_STATE;
#endif /* VBOX_COM_INPROC_API_CLIENT */
}

HRESULT Session::getNominalState(MachineState_T *aNominalState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_getNominalState(*aNominalState);
#else
    RT_NOREF(aNominalState);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
HRESULT Session::assignMachine(const ComPtr<IMachine> &aMachine,
                               LockType_T aLockType,
                               const com::Utf8Str &aTokenId)
#else
HRESULT Session::assignMachine(const ComPtr<IMachine> &aMachine,
                               LockType_T aLockType,
                               const ComPtr<IToken> &aToken)
#endif /* !VBOX_WITH_GENERIC_SESSION_WATCHER */
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(mState == SessionState_Unlocked, VBOX_E_INVALID_VM_STATE);

    if (!aMachine)
    {
        /*
         *  A special case: the server informs us that this session has been
         *  passed to IMachine::launchVMProcess() so this session will become
         *  remote (but not existing) when AssignRemoteMachine() is called.
         */

        AssertReturn(mType == SessionType_Null, VBOX_E_INVALID_OBJECT_STATE);
        mType = SessionType_Remote;
        mState = SessionState_Spawning;

        return S_OK;
    }

    /* query IInternalMachineControl interface */
    mControl = aMachine;
    AssertReturn(!!mControl, E_FAIL);

    HRESULT hrc = S_OK;
#ifndef VBOX_COM_INPROC_API_CLIENT
    if (aLockType == LockType_VM)
    {
        /* This is what is special about VM processes: they have a Console
         * object which is the root of all VM related activity. */
        hrc = mConsole.createObject();
        AssertComRCReturn(hrc, hrc);

        hrc = mConsole->initWithMachine(aMachine, mControl, aLockType);
        if (hrc != VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED)
            AssertComRCReturn(hrc, hrc);
        else
            return hrc;
    }
    else
        mRemoteMachine = aMachine;
#else
    RT_NOREF(aLockType);
    mRemoteMachine = aMachine;
#endif

#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
    Utf8Str strTokenId(aTokenId);
    Assert(!strTokenId.isEmpty());
#else /* VBOX_WITH_GENERIC_SESSION_WATCHER */
    Assert(!aToken.isNull());
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */
    /* create the machine client token */
    try
    {
#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
        mClientTokenHolder = new ClientTokenHolder(strTokenId);
#else /* VBOX_WITH_GENERIC_SESSION_WATCHER */
        mClientTokenHolder = new ClientTokenHolder(aToken);
#endif /* VBOX_WITH_GENERIC_SESSION_WATCHER */
        if (!mClientTokenHolder->isReady())
        {
            delete mClientTokenHolder;
            mClientTokenHolder = NULL;
            hrc = E_FAIL;
        }
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }

    /*
     *  Reference the VirtualBox object to ensure the server is up
     *  until the session is closed
     */
    if (SUCCEEDED(hrc))
       hrc = aMachine->COMGETTER(Parent)(mVirtualBox.asOutParam());

    if (SUCCEEDED(hrc))
    {
        mType = SessionType_WriteLock;
        mState = SessionState_Locked;
    }
    else
    {
        /* some cleanup */
        mControl.setNull();
#ifndef VBOX_COM_INPROC_API_CLIENT
        if (!mConsole.isNull())
        {
            mConsole->uninit();
            mConsole.setNull();
        }
#endif
    }

    return hrc;
}

HRESULT Session::assignRemoteMachine(const ComPtr<IMachine> &aMachine,
                                     const ComPtr<IConsole> &aConsole)

{
    AssertReturn(aMachine, E_INVALIDARG);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(mState == SessionState_Unlocked ||
                 mState == SessionState_Spawning, VBOX_E_INVALID_VM_STATE);

    HRESULT hrc = E_FAIL;

    /* query IInternalMachineControl interface */
    mControl = aMachine;
    AssertReturn(!!mControl, E_FAIL);

    /// @todo (dmik)
    //      currently, the remote session returns the same machine and
    //      console objects as the direct session, thus giving the
    //      (remote) client full control over the direct session. For the
    //      console, it is the desired behavior (the ability to control
    //      VM execution is a must for the remote session). What about
    //      the machine object, we may want to prevent the remote client
    //      from modifying machine data. In this case, we must:
    //      1)  assign the Machine object (instead of the SessionMachine
    //          object that is passed to this method) to mRemoteMachine;
    //      2)  remove GetMachine() property from the IConsole interface
    //          because it always returns the SessionMachine object
    //          (alternatively, we can supply a separate IConsole
    //          implementation that will return the Machine object in
    //          response to GetMachine()).

    mRemoteMachine = aMachine;
    mRemoteConsole = aConsole;

    /*
     *  Reference the VirtualBox object to ensure the server is up
     *  until the session is closed
     */
    hrc = aMachine->COMGETTER(Parent)(mVirtualBox.asOutParam());

    if (SUCCEEDED(hrc))
    {
        /*
         *  RemoteSession type can be already set by AssignMachine() when its
         *  argument is NULL (a special case)
         */
        if (mType != SessionType_Remote)
            mType = SessionType_Shared;
        else
            Assert(mState == SessionState_Spawning);

        mState = SessionState_Locked;
    }
    else
    {
        /* some cleanup */
        mControl.setNull();
        mRemoteMachine.setNull();
        mRemoteConsole.setNull();
    }

    LogFlowThisFunc(("hrc=%08X\n", hrc));
    LogFlowThisFuncLeave();

    return hrc;
}

HRESULT Session::updateMachineState(MachineState_T aMachineState)
{

    if (getObjectState().getState() != ObjectState::Ready)
    {
        /*
         *  We might have already entered Session::uninit() at this point, so
         *  return silently (not interested in the state change during uninit)
         */
        LogFlowThisFunc(("Already uninitialized.\n"));
        return S_OK;
    }

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mState == SessionState_Unlocking)
    {
        LogFlowThisFunc(("Already being unlocked.\n"));
        return S_OK;
    }

    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);

    AssertReturn(!mControl.isNull(), E_FAIL);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(!mConsole.isNull(), E_FAIL);

    return mConsole->i_updateMachineState(aMachineState);
#else
    RT_NOREF(aMachineState);
    return S_OK;
#endif
}

HRESULT Session::uninitialize()
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);

    HRESULT hrc = S_OK;

    if (getObjectState().getState() == ObjectState::Ready)
    {
        /* close() needs write lock */
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

        LogFlowThisFunc(("mState=%s, mType=%d\n", ::stringifySessionState(mState), mType));

        if (mState == SessionState_Unlocking)
        {
            LogFlowThisFunc(("Already being unlocked.\n"));
            return S_OK;
        }

        if (   mState == SessionState_Locked
            || mState == SessionState_Spawning)
        { /* likely */ }
        else
        {
#ifndef DEBUG_bird /* bird: hitting this all the time running tdAddBaseic1.py. */
            AssertMsgFailed(("Session is in wrong state (%d), expected locked (%d) or spawning (%d)\n",
                             mState, SessionState_Locked, SessionState_Spawning));
#endif
            return VBOX_E_INVALID_VM_STATE;
        }

        /* close ourselves */
        hrc = i_unlockMachine(false /* aFinalRelease */, true /* aFromServer */, alock);
    }
    else if (getObjectState().getState() == ObjectState::InUninit)
    {
        /*
         *  We might have already entered Session::uninit() at this point,
         *  return silently
         */
        LogFlowThisFunc(("Already uninitialized.\n"));
    }
    else
    {
        Log1WarningThisFunc(("UNEXPECTED uninitialization!\n"));
        hrc = autoCaller.hrc();
    }

    LogFlowThisFunc(("hrc=%08X\n", hrc));
    LogFlowThisFuncLeave();

    return hrc;
}

HRESULT Session::onNetworkAdapterChange(const ComPtr<INetworkAdapter> &aNetworkAdapter,
                                        BOOL aChangeAdapter)

{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onNetworkAdapterChange(aNetworkAdapter, aChangeAdapter);
#else
    RT_NOREF(aNetworkAdapter, aChangeAdapter);
    return S_OK;
#endif
}

HRESULT Session::onAudioAdapterChange(const ComPtr<IAudioAdapter> &aAudioAdapter)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onAudioAdapterChange(aAudioAdapter);
#else
    RT_NOREF(aAudioAdapter);
    return S_OK;
#endif

}

HRESULT Session::onHostAudioDeviceChange(const ComPtr<IHostAudioDevice> &aDevice,
                                         BOOL aNew, AudioDeviceState_T aState,
                                         const ComPtr<IVirtualBoxErrorInfo> &aErrInfo)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onHostAudioDeviceChange(aDevice, aNew, aState, aErrInfo);
#else
    RT_NOREF(aDevice, aNew, aState, aErrInfo);
    return S_OK;
#endif
}

HRESULT Session::onSerialPortChange(const ComPtr<ISerialPort> &aSerialPort)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onSerialPortChange(aSerialPort);
#else
    RT_NOREF(aSerialPort);
    return S_OK;
#endif
}

HRESULT Session::onParallelPortChange(const ComPtr<IParallelPort> &aParallelPort)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onParallelPortChange(aParallelPort);
#else
    RT_NOREF(aParallelPort);
    return S_OK;
#endif
}

HRESULT Session::onStorageControllerChange(const Guid &aMachineId, const Utf8Str &aControllerName)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onStorageControllerChange(aMachineId, aControllerName);
#else
    NOREF(aMachineId);
    NOREF(aControllerName);
    return S_OK;
#endif
}

HRESULT Session::onMediumChange(const ComPtr<IMediumAttachment> &aMediumAttachment,
                                BOOL aForce)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onMediumChange(aMediumAttachment, aForce);
#else
    RT_NOREF(aMediumAttachment, aForce);
    return S_OK;
#endif
}

HRESULT Session::onVMProcessPriorityChange(VMProcPriority_T priority)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onVMProcessPriorityChange(priority);
#else
    RT_NOREF(priority);
    return S_OK;
#endif
}

HRESULT Session::onCPUChange(ULONG aCpu, BOOL aAdd)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onCPUChange(aCpu, aAdd);
#else
    RT_NOREF(aCpu, aAdd);
    return S_OK;
#endif
}

HRESULT Session::onCPUExecutionCapChange(ULONG aExecutionCap)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onCPUExecutionCapChange(aExecutionCap);
#else
    RT_NOREF(aExecutionCap);
    return S_OK;
#endif
}

HRESULT Session::onVRDEServerChange(BOOL aRestart)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onVRDEServerChange(aRestart);
#else
    RT_NOREF(aRestart);
    return S_OK;
#endif
}

HRESULT Session::onRecordingStateChange(RecordingState_T aState, ComPtr<IProgress> &aProgress)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onRecordingStateChange(aState, aProgress);
#else
    RT_NOREF(aState, aProgress);
    return S_OK;
#endif
}

HRESULT Session::onRecordingScreenStateChange(RecordingState_T aState, ULONG aScreen)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onRecordingScreenStateChange(aState, aScreen);
#else
    RT_NOREF(aState, aScreen);
    return S_OK;
#endif
}

HRESULT Session::onUSBControllerChange()
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onUSBControllerChange();
#else
    return S_OK;
#endif
}

HRESULT Session::onSharedFolderChange(BOOL aGlobal)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onSharedFolderChange(aGlobal);
#else
    RT_NOREF(aGlobal);
    return S_OK;
#endif
}

HRESULT Session::onClipboardModeChange(ClipboardMode_T aClipboardMode)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onClipboardModeChange(aClipboardMode);
#else
    RT_NOREF(aClipboardMode);
    return S_OK;
#endif
}

HRESULT Session::onClipboardFileTransferModeChange(BOOL aEnabled)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onClipboardFileTransferModeChange(RT_BOOL(aEnabled));
#else
    RT_NOREF(aEnabled);
    return S_OK;
#endif
}

HRESULT Session::onDnDModeChange(DnDMode_T aDndMode)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onDnDModeChange(aDndMode);
#else
    RT_NOREF(aDndMode);
    return S_OK;
#endif
}

HRESULT Session::onGuestDebugControlChange(const ComPtr<IGuestDebugControl> &aGuestDebugControl)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onGuestDebugControlChange(aGuestDebugControl);
#else
    RT_NOREF(aGuestDebugControl);
    return S_OK;
#endif
}

HRESULT Session::onUSBDeviceAttach(const ComPtr<IUSBDevice> &aDevice,
                                   const ComPtr<IVirtualBoxErrorInfo> &aError,
                                   ULONG aMaskedInterfaces,
                                   const com::Utf8Str &aCaptureFilename)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onUSBDeviceAttach(aDevice, aError, aMaskedInterfaces, aCaptureFilename);
#else
    RT_NOREF(aDevice, aError, aMaskedInterfaces, aCaptureFilename);
    return S_OK;
#endif
}

HRESULT Session::onUSBDeviceDetach(const com::Guid &aId,
                                   const ComPtr<IVirtualBoxErrorInfo> &aError)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onUSBDeviceDetach(aId.toUtf16().raw(), aError);
#else
    RT_NOREF(aId, aError);
    return S_OK;
#endif
}

HRESULT Session::onShowWindow(BOOL aCheck, BOOL *aCanShow, LONG64 *aWinId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);
#endif

    if (mState != SessionState_Locked)
    {
        /* the call from Machine issued when the session is open can arrive
         * after the session starts closing or gets closed. Note that when
         * aCheck is false, we return E_FAIL to indicate that aWinId we return
         * is not valid */
        *aCanShow = FALSE;
        *aWinId = 0;
        return aCheck ? S_OK : E_FAIL;
    }

#ifndef VBOX_COM_INPROC_API_CLIENT
    return mConsole->i_onShowWindow(aCheck, aCanShow, aWinId);
#else
    return S_OK;
#endif
}

HRESULT Session::onBandwidthGroupChange(const ComPtr<IBandwidthGroup> &aBandwidthGroup)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onBandwidthGroupChange(aBandwidthGroup);
#else
    RT_NOREF(aBandwidthGroup);
    return S_OK;
#endif
}

HRESULT Session::onStorageDeviceChange(const ComPtr<IMediumAttachment> &aMediumAttachment, BOOL aRemove, BOOL aSilent)
{
    LogFlowThisFunc(("\n"));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onStorageDeviceChange(aMediumAttachment, aRemove, aSilent);
#else
    RT_NOREF(aMediumAttachment, aRemove, aSilent);
    return S_OK;
#endif
}

HRESULT Session::accessGuestProperty(const com::Utf8Str &aName, const com::Utf8Str &aValue, const com::Utf8Str &aFlags,
                                     ULONG aAccessMode, com::Utf8Str &aRetValue, LONG64 *aRetTimestamp, com::Utf8Str &aRetFlags)
{
#ifdef VBOX_WITH_GUEST_PROPS
# ifndef VBOX_COM_INPROC_API_CLIENT
    if (mState != SessionState_Locked)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Machine is not locked by session (session state: %s)."),
                        Global::stringifySessionState(mState));
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
    if (aName.isEmpty())
        return E_INVALIDARG;
    if (aAccessMode == 0 && !RT_VALID_PTR(aRetTimestamp))
        return E_POINTER;

    /* If this session is not in a VM process fend off the call. The caller
     * handles this correctly, by doing the operation in VBoxSVC. */
    if (!mConsole)
        return E_ACCESSDENIED;

    HRESULT hr;
    if (aAccessMode == 2)
        hr = mConsole->i_deleteGuestProperty(aName);
    else if (aAccessMode == 1)
        hr = mConsole->i_setGuestProperty(aName, aValue, aFlags);
    else if (aAccessMode == 0)
        hr = mConsole->i_getGuestProperty(aName, &aRetValue, aRetTimestamp, &aRetFlags);
    else
        hr = E_INVALIDARG;

    return hr;
# else  /* VBOX_COM_INPROC_API_CLIENT */
    /** @todo This is nonsense, non-VM API users shouldn't need to deal with this
     *        method call, VBoxSVC should be clever enough to see that the
     *        session doesn't have a console! */
    RT_NOREF(aName, aValue, aFlags, aAccessMode, aRetValue, aRetTimestamp, aRetFlags);
    return E_ACCESSDENIED;
# endif /* VBOX_COM_INPROC_API_CLIENT */

#else  /* VBOX_WITH_GUEST_PROPS */
    ReturnComNotImplemented();
#endif /* VBOX_WITH_GUEST_PROPS */
}

HRESULT Session::enumerateGuestProperties(const com::Utf8Str &aPatterns,
                                          std::vector<com::Utf8Str> &aKeys,
                                          std::vector<com::Utf8Str> &aValues,
                                          std::vector<LONG64> &aTimestamps,
                                          std::vector<com::Utf8Str> &aFlags)
{
#if defined(VBOX_WITH_GUEST_PROPS) && !defined(VBOX_COM_INPROC_API_CLIENT)
    if (mState != SessionState_Locked)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Machine is not locked by session (session state: %s)."),
                        Global::stringifySessionState(mState));
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);

    /* If this session is not in a VM process fend off the call. The caller
     * handles this correctly, by doing the operation in VBoxSVC. */
    if (!mConsole)
        return E_ACCESSDENIED;

    return mConsole->i_enumerateGuestProperties(aPatterns, aKeys, aValues, aTimestamps, aFlags);

#else /* VBOX_WITH_GUEST_PROPS not defined */
    RT_NOREF(aPatterns, aKeys, aValues, aTimestamps, aFlags);
    ReturnComNotImplemented();
#endif /* VBOX_WITH_GUEST_PROPS not defined */
}

HRESULT Session::onlineMergeMedium(const ComPtr<IMediumAttachment> &aMediumAttachment, ULONG aSourceIdx,
                                   ULONG aTargetIdx, const ComPtr<IProgress> &aProgress)
{
    if (mState != SessionState_Locked)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Machine is not locked by session (session state: %s)."),
                        Global::stringifySessionState(mState));
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_onlineMergeMedium(aMediumAttachment,
                                         aSourceIdx, aTargetIdx,
                                         aProgress);
#else
    RT_NOREF(aMediumAttachment, aSourceIdx, aTargetIdx, aProgress);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

HRESULT Session::reconfigureMediumAttachments(const std::vector<ComPtr<IMediumAttachment> > &aAttachments)
{
    if (mState != SessionState_Locked)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Machine is not locked by session (session state: %s)."),
                        Global::stringifySessionState(mState));
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_reconfigureMediumAttachments(aAttachments);
#else
    RT_NOREF(aAttachments);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

HRESULT Session::enableVMMStatistics(BOOL aEnable)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    mConsole->i_enableVMMStatistics(aEnable);

    return S_OK;
#else
    RT_NOREF(aEnable);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

HRESULT Session::pauseWithReason(Reason_T aReason)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_pause(aReason);
#else
    RT_NOREF(aReason);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

HRESULT Session::resumeWithReason(Reason_T aReason)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    AutoWriteLock dummyLock(mConsole COMMA_LOCKVAL_SRC_POS);
    return mConsole->i_resume(aReason, dummyLock);
#else
    RT_NOREF(aReason);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

HRESULT Session::saveStateWithReason(Reason_T aReason,
                                     const ComPtr<IProgress> &aProgress,
                                     const ComPtr<ISnapshot> &aSnapshot,
                                     const Utf8Str &aStateFilePath,
                                     BOOL aPauseVM, BOOL *aLeftPaused)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    bool fLeftPaused = false;
    HRESULT hrc = mConsole->i_saveState(aReason, aProgress, aSnapshot, aStateFilePath, !!aPauseVM, fLeftPaused);
    if (aLeftPaused)
        *aLeftPaused = fLeftPaused;
    return hrc;
#else
    RT_NOREF(aReason, aProgress, aSnapshot, aStateFilePath, aPauseVM, aLeftPaused);
    AssertFailed();
    return E_NOTIMPL;
#endif
}

HRESULT Session::cancelSaveStateWithReason()
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    AssertReturn(mState == SessionState_Locked, VBOX_E_INVALID_VM_STATE);
    AssertReturn(mType == SessionType_WriteLock, VBOX_E_INVALID_OBJECT_STATE);
#ifndef VBOX_COM_INPROC_API_CLIENT
    AssertReturn(mConsole, VBOX_E_INVALID_OBJECT_STATE);

    return mConsole->i_cancelSaveState();
#else
    AssertFailed();
    return E_NOTIMPL;
#endif
}

// private methods
///////////////////////////////////////////////////////////////////////////////

/**
 *  Unlocks a machine associated with the current session.
 *
 *  @param aFinalRelease    called as a result of FinalRelease()
 *  @param aFromServer      called as a result of Uninitialize()
 *  @param aLockW           The write lock this object is protected with.
 *                          Must be acquired already and will be released
 *                          and later reacquired during the unlocking.
 *
 *  @note To be called only from #uninit(), ISession::UnlockMachine() or
 *        ISession::Uninitialize().
 */
HRESULT Session::i_unlockMachine(bool aFinalRelease, bool aFromServer, AutoWriteLock &aLockW)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aFinalRelease=%d, isFromServer=%d\n",
                      aFinalRelease, aFromServer));

    LogFlowThisFunc(("mState=%s, mType=%d\n", ::stringifySessionState(mState), mType));

    Assert(aLockW.isWriteLockOnCurrentThread());

    if (mState != SessionState_Locked)
    {
        Assert(mState == SessionState_Spawning);

        /* The session object is going to be uninitialized before it has been
         * assigned a direct console of the machine the client requested to open
         * a remote session to using IVirtualBox:: openRemoteSession(). It is OK
         * only if this close request comes from the server (for example, it
         * detected that the VM process it started terminated before opening a
         * direct session). Otherwise, it means that the client is too fast and
         * trying to close the session before waiting for the progress object it
         * got from IVirtualBox:: openRemoteSession() to complete, so assert. */
        Assert(aFromServer);

        mState = SessionState_Unlocked;
        mType = SessionType_Null;

        Assert(!mClientTokenHolder);

        LogFlowThisFuncLeave();
        return S_OK;
    }

    /* go to the closing state */
    mState = SessionState_Unlocking;

    if (mType == SessionType_WriteLock)
    {
#ifndef VBOX_COM_INPROC_API_CLIENT
        if (!mConsole.isNull())
        {
            mConsole->uninit();
            mConsole.setNull();
        }
#else
        mRemoteMachine.setNull();
#endif
    }
    else
    {
        mRemoteMachine.setNull();
        mRemoteConsole.setNull();
    }

    ComPtr<IProgress> progress;

    if (!aFinalRelease && !aFromServer)
    {
        /*
         *  We trigger OnSessionEnd() only when the session closes itself using
         *  Close(). Note that if isFinalRelease = TRUE here, this means that
         *  the client process has already initialized the termination procedure
         *  without issuing Close() and the IPC channel is no more operational --
         *  so we cannot call the server's method (it will definitely fail). The
         *  server will instead simply detect the abnormal client death (since
         *  OnSessionEnd() is not called) and reset the machine state to Aborted.
         */

        /*
         *  while waiting for OnSessionEnd() to complete one of our methods
         *  can be called by the server (for example, Uninitialize(), if the
         *  direct session has initiated a closure just a bit before us) so
         *  we need to release the lock to avoid deadlocks. The state is already
         *  SessionState_Closing here, so it's safe.
         */
        aLockW.release();

        Assert(!aLockW.isWriteLockOnCurrentThread());

        LogFlowThisFunc(("Calling mControl->OnSessionEnd()...\n"));
        HRESULT hrc = mControl->OnSessionEnd(this, progress.asOutParam());
        LogFlowThisFunc(("mControl->OnSessionEnd()=%08X\n", hrc));

        aLockW.acquire();

        /*
         *  If we get E_UNEXPECTED this means that the direct session has already
         *  been closed, we're just too late with our notification and nothing more
         *
         *  bird: Seems E_ACCESSDENIED is what gets returned these days; see
         *        ObjectState::addCaller.
         */
        if (mType != SessionType_WriteLock && (hrc == E_UNEXPECTED || hrc == E_ACCESSDENIED))
            hrc = S_OK;

#if !defined(DEBUG_bird) && !defined(DEBUG_andy) /* I don't want clients crashing on me just because VBoxSVC went belly up. */
        AssertComRC(hrc);
#endif
    }

    mControl.setNull();

    if (mType == SessionType_WriteLock)
    {
        if (mClientTokenHolder)
        {
            delete mClientTokenHolder;
            mClientTokenHolder = NULL;
        }

        if (!aFinalRelease && !aFromServer)
        {
            /*
             *  Wait for the server to grab the semaphore and destroy the session
             *  machine (allowing us to open a new session with the same machine
             *  once this method returns)
             */
            Assert(!!progress);
            if (progress)
                progress->WaitForCompletion(-1);
        }
    }

    mState = SessionState_Unlocked;
    mType = SessionType_Null;

    /* release the VirtualBox instance as the very last step */
    mVirtualBox.setNull();

    LogFlowThisFuncLeave();
    return S_OK;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
