/* $Id: SessionImpl.h 110489 2025-07-31 08:32:38Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Client Session COM Class definition
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

#ifndef MAIN_INCLUDED_SessionImpl_h
#define MAIN_INCLUDED_SessionImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "SessionWrap.h"
#include "ConsoleImpl.h"

#ifdef RT_OS_WINDOWS
# include "win/resource.h"
#endif

#if defined(RT_OS_WINDOWS) && !RT_MSC_PREREQ(RT_MSC_VER_VC140)
[threading(free)]
#endif
class ATL_NO_VTABLE Session :
    public SessionWrap
#ifdef RT_OS_WINDOWS
    , public ATL::CComCoClass<Session, &CLSID_Session>
#endif
{
public:

    DECLARE_CLASSFACTORY()

    // Do not use any ATL registry support.
    //DECLARE_REGISTRY_RESOURCEID(IDR_VIRTUALBOX)

    DECLARE_NOT_AGGREGATABLE(Session)

    DECLARE_COMMON_CLASS_METHODS(Session)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializers/uninitializers only for internal purposes
    HRESULT init();
    void uninit();

private:

    // Wrapped ISession properties
    HRESULT getState(SessionState_T *aState);
    HRESULT getType(SessionType_T *aType);
    HRESULT getName(com::Utf8Str &aName);
    HRESULT setName(const com::Utf8Str &aName);
    HRESULT getMachine(ComPtr<IMachine> &aMachine);
    HRESULT getConsole(ComPtr<IConsole> &aConsole);

    // Wrapped ISession methods
    HRESULT unlockMachine();

    // Wrapped IInternalSessionControl properties
    HRESULT getPID(ULONG *aPid);
    HRESULT getRemoteConsole(ComPtr<IConsole> &aRemoteConsole);
    HRESULT getNominalState(MachineState_T *aNominalState);

    // Wrapped IInternalSessionControl methods
#ifndef VBOX_WITH_GENERIC_SESSION_WATCHER
    HRESULT assignMachine(const ComPtr<IMachine> &aMachine,
                          LockType_T aLockType,
                          const com::Utf8Str &aTokenId);
#else
    HRESULT assignMachine(const ComPtr<IMachine> &aMachine,
                          LockType_T aLockType,
                          const ComPtr<IToken> &aToken);
#endif /* !VBOX_WITH_GENERIC_SESSION_WATCHER */
    HRESULT assignRemoteMachine(const ComPtr<IMachine> &aMachine,
                                const ComPtr<IConsole> &aConsole);
    HRESULT updateMachineState(MachineState_T aMachineState);
    HRESULT uninitialize();
    HRESULT onNetworkAdapterChange(const ComPtr<INetworkAdapter> &aNetworkAdapter,
                                   BOOL aChangeAdapter);
    HRESULT onAudioAdapterChange(const ComPtr<IAudioAdapter> &aAudioAdapter);
    HRESULT onHostAudioDeviceChange(const ComPtr<IHostAudioDevice> &aDevice,
                                    BOOL aNew, AudioDeviceState_T aState,
                                    const ComPtr<IVirtualBoxErrorInfo> &aErrInfo);
    HRESULT onSerialPortChange(const ComPtr<ISerialPort> &aSerialPort);
    HRESULT onParallelPortChange(const ComPtr<IParallelPort> &aParallelPort);
    HRESULT onStorageControllerChange(const Guid &aMachineId, const com::Utf8Str& aControllerName);
    HRESULT onMediumChange(const ComPtr<IMediumAttachment> &aMediumAttachment,
                           BOOL aForce);
    HRESULT onStorageDeviceChange(const ComPtr<IMediumAttachment> &aMediumAttachment,
                                  BOOL aRemove,
                                  BOOL aSilent);
    HRESULT onVMProcessPriorityChange(VMProcPriority_T priority);
    HRESULT onClipboardModeChange(ClipboardMode_T aClipboardMode);
    HRESULT onClipboardFileTransferModeChange(BOOL aEnabled);
    HRESULT onDnDModeChange(DnDMode_T aDndMode);
    HRESULT onCPUChange(ULONG aCpu,
                        BOOL aAdd);
    HRESULT onCPUExecutionCapChange(ULONG aExecutionCap);
    HRESULT onVRDEServerChange(BOOL aRestart);
    HRESULT onRecordingStateChange(RecordingState_T aState, ComPtr<IProgress> &aProgress);
    HRESULT onRecordingScreenStateChange(RecordingState_T aState, ULONG aScreen);
    HRESULT onUSBControllerChange();
    HRESULT onSharedFolderChange(BOOL aGlobal);
    HRESULT onGuestDebugControlChange(const ComPtr<IGuestDebugControl> &aGuestDebugControl);
    HRESULT onUSBDeviceAttach(const ComPtr<IUSBDevice> &aDevice,
                              const ComPtr<IVirtualBoxErrorInfo> &aError,
                              ULONG aMaskedInterfaces,
                              const com::Utf8Str &aCaptureFilename);
    HRESULT onUSBDeviceDetach(const com::Guid &aId,
                              const ComPtr<IVirtualBoxErrorInfo> &aError);
    HRESULT onShowWindow(BOOL aCheck,
                         BOOL *aCanShow,
                         LONG64 *aWinId);
    HRESULT onBandwidthGroupChange(const ComPtr<IBandwidthGroup> &aBandwidthGroup);
    HRESULT accessGuestProperty(const com::Utf8Str &aName,
                                const com::Utf8Str &aValue,
                                const com::Utf8Str &aFlags,
                                ULONG aAccessMode,
                                com::Utf8Str &aRetValue,
                                LONG64 *aRetTimestamp,
                                com::Utf8Str &aRetFlags);
    HRESULT enumerateGuestProperties(const com::Utf8Str &aPatterns,
                                     std::vector<com::Utf8Str> &aKeys,
                                     std::vector<com::Utf8Str> &aValues,
                                     std::vector<LONG64> &aTimestamps,
                                     std::vector<com::Utf8Str> &aFlags);
    HRESULT onlineMergeMedium(const ComPtr<IMediumAttachment> &aMediumAttachment,
                              ULONG aSourceIdx,
                              ULONG aTargetIdx,
                              const ComPtr<IProgress> &aProgress);
    HRESULT reconfigureMediumAttachments(const std::vector<ComPtr<IMediumAttachment> > &aAttachments);
    HRESULT enableVMMStatistics(BOOL aEnable);
    HRESULT pauseWithReason(Reason_T aReason);
    HRESULT resumeWithReason(Reason_T aReason);
    HRESULT saveStateWithReason(Reason_T aReason,
                                const ComPtr<IProgress> &aProgress,
                                const ComPtr<ISnapshot> &aSnapshot,
                                const Utf8Str &aStateFilePath,
                                BOOL aPauseVM,
                                BOOL *aLeftPaused);
    HRESULT cancelSaveStateWithReason();


    HRESULT i_unlockMachine(bool aFinalRelease, bool aFromServer, AutoWriteLock &aLockW);

    SessionState_T mState;
    SessionType_T mType;
    Utf8Str mName;

    ComPtr<IInternalMachineControl> mControl;

#ifndef VBOX_COM_INPROC_API_CLIENT
    ComObjPtr<Console> mConsole;
#endif

    ComPtr<IMachine> mRemoteMachine;
    ComPtr<IConsole> mRemoteConsole;

    ComPtr<IVirtualBox> mVirtualBox;

    class ClientTokenHolder;

    ClientTokenHolder *mClientTokenHolder;
};

#endif /* !MAIN_INCLUDED_SessionImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
