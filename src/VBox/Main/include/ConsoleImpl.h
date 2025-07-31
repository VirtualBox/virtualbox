/* $Id: ConsoleImpl.h 110489 2025-07-31 08:32:38Z andreas.loeffler@oracle.com $ */
/** @file
 * VBox Console COM Class definition
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_ConsoleImpl_h
#define MAIN_INCLUDED_ConsoleImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cpp/exception.h>

#include "VirtualBoxBase.h"
#include "VBox/com/array.h"
#include "EventImpl.h"
#include "SecretKeyStore.h"
#include "ConsoleWrap.h"
#ifdef VBOX_WITH_RECORDING
# include "RecordingContext.h"
#endif
#ifdef VBOX_WITH_CLOUD_NET
#include "CloudGateway.h"
#endif /* VBOX_WITH_CLOUD_NET */

class Guest;
class Keyboard;
class Mouse;
class Display;
class MachineDebugger;
class TeleporterStateSrc;
class OUSBDevice;
class RemoteUSBDevice;
class ConsoleSharedFolder;
class VRDEServerInfo;
class EmulatedUSB;
class AudioVRDE;
#ifdef VBOX_WITH_AUDIO_RECORDING
class AudioVideoRec;
#endif
#ifdef VBOX_WITH_USB_CARDREADER
class UsbCardReader;
#endif
class ConsoleVRDPServer;
class VMMDev;
class Progress;
class BusAssignmentManager;
COM_STRUCT_OR_CLASS(IEventListener);
#ifdef VBOX_WITH_EXTPACK
class ExtPackManager;
#endif
class VMMDevMouseInterface;
class DisplayMouseInterface;
class VMPowerUpTask;
class VMPowerDownTask;
class NvramStore;
#ifdef VBOX_WITH_VIRT_ARMV8
class ResourceStore;
#endif

#include <iprt/uuid.h>
#include <iprt/log.h>
#include <iprt/memsafer.h>
#include <VBox/RemoteDesktop/VRDE.h>
#include <VBox/vmm/pdmdrv.h>
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>  /* For the property notification callback */
#endif
#ifdef VBOX_WITH_USB
# include <VBox/vrdpusb.h>
#endif
#include <VBox/VBoxCryptoIf.h>
#include <VBox/pci.h>

#if    defined(VBOX_WITH_GUEST_PROPS) || defined(VBOX_WITH_SHARED_CLIPBOARD) \
    || defined(VBOX_WITH_DRAG_AND_DROP)
# include "HGCM.h" /** @todo It should be possible to register a service
                    *        extension using a VMMDev callback. */
#endif

struct VUSBIRHCONFIG;
typedef struct VUSBIRHCONFIG *PVUSBIRHCONFIG;

struct PDMINETWORKNATDNSCONFIG;

#include <list>
#include <vector>

// defines
///////////////////////////////////////////////////////////////////////////////

/**
 *  Checks the availability of the underlying VM device driver corresponding
 *  to the COM interface (IKeyboard, IMouse, IDisplay, etc.). When the driver is
 *  not available (NULL), sets error info and returns returns E_ACCESSDENIED.
 *  The translatable error message is defined in null context.
 *
 *  Intended to used only within Console children (i.e. Keyboard, Mouse,
 *  Display, etc.).
 *
 *  @param drv  driver pointer to check (compare it with NULL)
 */
#define CHECK_CONSOLE_DRV(drv) \
    do { \
        if (!!(drv)) {} \
        else return setError(E_ACCESSDENIED, Console::tr("The console is not powered up (%Rfn)"), __FUNCTION__); \
    } while (0)

// Console
///////////////////////////////////////////////////////////////////////////////

/**
 * Simple class for storing network boot information.
 */
struct BootNic
{
    ULONG          mInstance;
    PCIBusAddress  mPCIAddress;

    ULONG          mBootPrio;
    bool operator < (const BootNic &rhs) const
    {
        ULONG lval = mBootPrio     - 1; /* 0 will wrap around and get the lowest priority. */
        ULONG rval = rhs.mBootPrio - 1;
        return lval < rval; /* Zero compares as highest number (lowest prio). */
    }
};

class ConsoleMouseInterface
{
public:
    virtual ~ConsoleMouseInterface() { }
    virtual VMMDevMouseInterface  *i_getVMMDevMouseInterface(){return NULL;}
    virtual DisplayMouseInterface *i_getDisplayMouseInterface(){return NULL;}
    virtual void i_onMouseCapabilityChange(BOOL supportsAbsolute,
                                           BOOL supportsRelative,
                                           BOOL supportsTouchScreen,
                                           BOOL supportsTouchPad,
                                           BOOL needsHostCursor)
    {
        RT_NOREF(supportsAbsolute, supportsRelative, supportsTouchScreen, supportsTouchPad, needsHostCursor);
    }
};

/** IConsole implementation class */
class ATL_NO_VTABLE Console :
    public ConsoleWrap,
    public ConsoleMouseInterface
{

public:

    DECLARE_COMMON_CLASS_METHODS(Console)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializers/uninitializers for internal purposes only
    HRESULT initWithMachine(IMachine *aMachine, IInternalMachineControl *aControl, LockType_T aLockType);
    void uninit();


    // public methods for internal purposes only

    /*
     *  Note: the following methods do not increase refcount. intended to be
     *  called only by the VM execution thread.
     */

    PCVMMR3VTABLE i_getVMMVTable() const RT_NOEXCEPT { return mpVMM; }
    Guest *i_getGuest() const { return mGuest; }
    Keyboard *i_getKeyboard() const { return mKeyboard; }
    Mouse *i_getMouse() const { return mMouse; }
    Display *i_getDisplay() const { return mDisplay; }
    MachineDebugger *i_getMachineDebugger() const { return mDebugger; }
#ifdef VBOX_WITH_AUDIO_VRDE
    AudioVRDE *i_getAudioVRDE() const { return mAudioVRDE; }
#endif
#ifdef VBOX_WITH_RECORDING
    int i_recordingCreate(ComPtr<IProgress> &pProgress);
    void i_recordingDestroy(void);
    int i_recordingEnable(BOOL fEnable, util::AutoWriteLock *pAutoLock, ComPtr<IProgress> &pProgress);
    int i_recordingStart(util::AutoWriteLock *pAutoLock = NULL);
    int i_recordingStop(util::AutoWriteLock *pAutoLock = NULL);
    int i_recordingCursorShapeChange(bool fVisible, bool fAlpha, uint32_t xHot, uint32_t yHot, uint32_t uWidth, uint32_t uHeight, const uint8_t *pu8Shape, uint32_t cbShape);
# ifdef VBOX_WITH_AUDIO_RECORDING
    AudioVideoRec *i_recordingGetAudioDrv(void) const { return mRecording.mAudioRec; }
# endif
    RecordingContext *i_recordingGetContext(void) { return &mRecording.mCtx; }
# ifdef VBOX_WITH_AUDIO_RECORDING
    HRESULT i_recordingSendAudio(const void *pvData, size_t cbData, uint64_t uDurationMs);
# endif
#endif

    const ComPtr<IMachine> &i_machine() const { return mMachine; }
    const Bstr &i_getId() const { return mstrUuid; }

    bool i_useHostClipboard() { return mfUseHostClipboard; }

    /** Method is called only from ConsoleVRDPServer */
    IVRDEServer *i_getVRDEServer() const { return mVRDEServer; }

    ConsoleVRDPServer *i_consoleVRDPServer() const { return mConsoleVRDPServer; }

    HRESULT i_updateMachineState(MachineState_T aMachineState);
    HRESULT i_getNominalState(MachineState_T &aNominalState);
    Utf8Str i_getAudioAdapterDeviceName(IAudioAdapter *aAudioAdapter);

    // events from IInternalSessionControl
    HRESULT i_onNetworkAdapterChange(INetworkAdapter *aNetworkAdapter, BOOL changeAdapter);
    HRESULT i_onAudioAdapterChange(IAudioAdapter *aAudioAdapter);
    HRESULT i_onHostAudioDeviceChange(IHostAudioDevice *aDevice, BOOL aNew, AudioDeviceState_T aState,
                                      IVirtualBoxErrorInfo *aErrInfo);
    HRESULT i_onSerialPortChange(ISerialPort *aSerialPort);
    HRESULT i_onParallelPortChange(IParallelPort *aParallelPort);
    HRESULT i_onStorageControllerChange(const com::Guid& aMachineId, const com::Utf8Str& aControllerName);
    HRESULT i_onMediumChange(IMediumAttachment *aMediumAttachment, BOOL aForce);
    HRESULT i_onCPUChange(ULONG aCPU, BOOL aRemove);
    HRESULT i_onCPUExecutionCapChange(ULONG aExecutionCap);
    HRESULT i_onClipboardError(const Utf8Str &aId, const Utf8Str &aErrMsg, LONG aRc);
    HRESULT i_onClipboardModeChange(ClipboardMode_T aClipboardMode);
    HRESULT i_onClipboardFileTransferModeChange(bool aEnabled);
    HRESULT i_onDnDModeChange(DnDMode_T aDnDMode);
    HRESULT i_onVRDEServerChange(BOOL aRestart);
    HRESULT i_onRecordingStateChange(RecordingState_T aState, ComPtr<IProgress> &aProgress);
    HRESULT i_onRecordingScreenStateChange(RecordingState_T aState, ULONG aScreen);
    HRESULT i_onUSBControllerChange();
    HRESULT i_onSharedFolderChange(BOOL aGlobal);
    HRESULT i_onUSBDeviceAttach(IUSBDevice *aDevice, IVirtualBoxErrorInfo *aError, ULONG aMaskedIfs,
                                const Utf8Str &aCaptureFilename);
    HRESULT i_onUSBDeviceDetach(IN_BSTR aId, IVirtualBoxErrorInfo *aError);
    HRESULT i_onBandwidthGroupChange(IBandwidthGroup *aBandwidthGroup);
    HRESULT i_onStorageDeviceChange(IMediumAttachment *aMediumAttachment, BOOL aRemove, BOOL aSilent);
    HRESULT i_onExtraDataChange(const Bstr &aMachineId, const Bstr &aKey, const Bstr &aVal);
    HRESULT i_onGuestDebugControlChange(IGuestDebugControl *aGuestDebugControl);

    HRESULT i_getGuestProperty(const Utf8Str &aName, Utf8Str *aValue, LONG64 *aTimestamp, Utf8Str *aFlags);
    HRESULT i_setGuestProperty(const Utf8Str &aName, const Utf8Str &aValue, const Utf8Str &aFlags);
    HRESULT i_deleteGuestProperty(const Utf8Str &aName);
    HRESULT i_enumerateGuestProperties(const Utf8Str &aPatterns,
                                       std::vector<Utf8Str> &aNames,
                                       std::vector<Utf8Str> &aValues,
                                       std::vector<LONG64>  &aTimestamps,
                                       std::vector<Utf8Str> &aFlags);
    HRESULT i_onlineMergeMedium(IMediumAttachment *aMediumAttachment,
                                ULONG aSourceIdx, ULONG aTargetIdx,
                                IProgress *aProgress);
    HRESULT i_reconfigureMediumAttachments(const std::vector<ComPtr<IMediumAttachment> > &aAttachments);
    HRESULT i_onVMProcessPriorityChange(VMProcPriority_T priority);
    int i_hgcmLoadService(const char *pszServiceLibrary, const char *pszServiceName);
    VMMDev *i_getVMMDev() { return m_pVMMDev; }

#ifdef VBOX_WITH_EXTPACK
    ExtPackManager *i_getExtPackManager();
#endif
    EventSource *i_getEventSource() { return mEventSource; }
#ifdef VBOX_WITH_USB_CARDREADER
    UsbCardReader *i_getUsbCardReader() { return mUsbCardReader; }
#endif

    int i_VRDPClientLogon(uint32_t u32ClientId, const char *pszUser, const char *pszPassword, const char *pszDomain);
    void i_VRDPClientStatusChange(uint32_t u32ClientId, const char *pszStatus);
    void i_VRDPClientConnect(uint32_t u32ClientId);
    void i_VRDPClientDisconnect(uint32_t u32ClientId, uint32_t fu32Intercepted);
    void i_VRDPInterceptAudio(uint32_t u32ClientId);
    void i_VRDPInterceptUSB(uint32_t u32ClientId, void **ppvIntercept);
    void i_VRDPInterceptClipboard(uint32_t u32ClientId);

    void i_processRemoteUSBDevices(uint32_t u32ClientId, VRDEUSBDEVICEDESC *pDevList, uint32_t cbDevList, bool fDescExt);
    void i_reportVmStatistics(ULONG aValidStats, ULONG aCpuUser,
                              ULONG aCpuKernel, ULONG aCpuIdle,
                              ULONG aMemTotal, ULONG aMemFree,
                              ULONG aMemBalloon, ULONG aMemShared,
                              ULONG aMemCache, ULONG aPageTotal,
                              ULONG aAllocVMM, ULONG aFreeVMM,
                              ULONG aBalloonedVMM, ULONG aSharedVMM,
                              ULONG aVmNetRx, ULONG aVmNetTx)
    {
        mControl->ReportVmStatistics(aValidStats, aCpuUser, aCpuKernel, aCpuIdle,
                                     aMemTotal, aMemFree, aMemBalloon, aMemShared,
                                     aMemCache, aPageTotal, aAllocVMM, aFreeVMM,
                                     aBalloonedVMM, aSharedVMM, aVmNetRx, aVmNetTx);
    }
    void i_enableVMMStatistics(BOOL aEnable);

    HRESULT i_pause(Reason_T aReason);
    HRESULT i_resume(Reason_T aReason, AutoWriteLock &alock);
    HRESULT i_saveState(Reason_T aReason, const ComPtr<IProgress> &aProgress,
                        const ComPtr<ISnapshot> &aSnapshot,
                        const Utf8Str &aStateFilePath, bool fPauseVM, bool &fLeftPaused);
    HRESULT i_cancelSaveState();

    // callback callers (partly; for some events console callbacks are notified
    // directly from IInternalSessionControl event handlers declared above)
    void i_onMousePointerShapeChange(bool fVisible, bool fAlpha,
                                     uint32_t xHot, uint32_t yHot,
                                     uint32_t width, uint32_t height,
                                     const uint8_t *pu8Shape,
                                     uint32_t cbShape);
    void i_onMouseCapabilityChange(BOOL supportsAbsolute, BOOL supportsRelative,
                                   BOOL supportsTouchScreen, BOOL supportsTouchPad,
                                   BOOL needsHostCursor);
    void i_onStateChange(MachineState_T aMachineState);
    void i_onAdditionsStateChange();
    void i_onAdditionsOutdated();
    void i_onKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock);
    void i_onUSBDeviceStateChange(IUSBDevice *aDevice, bool aAttached,
                                  IVirtualBoxErrorInfo *aError);
    void i_onRuntimeError(BOOL aFatal, IN_BSTR aErrorID, IN_BSTR aMessage);
    HRESULT i_onShowWindow(BOOL aCheck, BOOL *aCanShow, LONG64 *aWinId);
    void i_onVRDEServerInfoChange();
    HRESULT i_sendACPIMonitorHotPlugEvent();

    static const PDMDRVREG DrvStatusReg;

    static HRESULT i_setErrorStatic(HRESULT aResultCode, const char *pcsz, ...);
    static HRESULT i_setErrorStaticBoth(HRESULT aResultCode, int vrc, const char *pcsz, ...);
    HRESULT i_setInvalidMachineStateError();

    static const char *i_storageControllerTypeToStr(StorageControllerType_T enmCtrlType);
    static HRESULT i_storageBusPortDeviceToLun(StorageBus_T enmBus, LONG port, LONG device, unsigned &uLun);
    // Called from event listener
    HRESULT i_onNATRedirectRuleChanged(ULONG ulInstance, BOOL aNatRuleRemove,
                                       NATProtocol_T aProto, IN_BSTR aHostIp, LONG aHostPort, IN_BSTR aGuestIp, LONG aGuestPort);
    HRESULT i_onNATDnsChanged();

    // Mouse interface
    VMMDevMouseInterface *i_getVMMDevMouseInterface();
    DisplayMouseInterface *i_getDisplayMouseInterface();

    EmulatedUSB *i_getEmulatedUSB(void) { return mEmulatedUSB; }

    /**
     * Sets the disk encryption keys.
     *
     * @returns COM status code.
     * @param   strCfg    The config for the disks.
     *
     * @note  One line in the config string contains all required data for one disk.
     *        The format for one disk is some sort of comma separated value using
     *        key=value pairs.
     *        There are two keys defined at the moment:
     *            - uuid: The uuid of the base image the key is for (with or without)
     *                    the curly braces.
     *            - dek: The data encryption key in base64 encoding
     */
    HRESULT i_setDiskEncryptionKeys(const Utf8Str &strCfg);

    int i_retainCryptoIf(PCVBOXCRYPTOIF *ppCryptoIf);
    int i_releaseCryptoIf(PCVBOXCRYPTOIF pCryptoIf);
    HRESULT i_unloadCryptoIfModule(void);

#ifdef VBOX_WITH_GUEST_PROPS
    // VMMDev needs:
    HRESULT                     i_pullGuestProperties(ComSafeArrayOut(BSTR, names), ComSafeArrayOut(BSTR, values),
                                                      ComSafeArrayOut(LONG64, timestamps), ComSafeArrayOut(BSTR, flags));
    static DECLCALLBACK(int)    i_doGuestPropNotification(void *pvExtension, uint32_t, void *pvParms, uint32_t cbParms);
#endif

private:

    // wrapped IConsole properties
    HRESULT getMachine(ComPtr<IMachine> &aMachine);
    HRESULT getState(MachineState_T *aState);
    HRESULT getGuest(ComPtr<IGuest> &aGuest);
    HRESULT getKeyboard(ComPtr<IKeyboard> &aKeyboard);
    HRESULT getMouse(ComPtr<IMouse> &aMouse);
    HRESULT getDisplay(ComPtr<IDisplay> &aDisplay);
    HRESULT getDebugger(ComPtr<IMachineDebugger> &aDebugger);
    HRESULT getUSBDevices(std::vector<ComPtr<IUSBDevice> > &aUSBDevices);
    HRESULT getRemoteUSBDevices(std::vector<ComPtr<IHostUSBDevice> > &aRemoteUSBDevices);
    HRESULT getSharedFolders(std::vector<ComPtr<ISharedFolder> > &aSharedFolders);
    HRESULT getVRDEServerInfo(ComPtr<IVRDEServerInfo> &aVRDEServerInfo);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getAttachedPCIDevices(std::vector<ComPtr<IPCIDeviceAttachment> > &aAttachedPCIDevices);
    HRESULT getUseHostClipboard(BOOL *aUseHostClipboard);
    HRESULT setUseHostClipboard(BOOL aUseHostClipboard);
    HRESULT getEmulatedUSB(ComPtr<IEmulatedUSB> &aEmulatedUSB);

    // wrapped IConsole methods
    HRESULT powerUp(ComPtr<IProgress> &aProgress);
    HRESULT powerUpPaused(ComPtr<IProgress> &aProgress);
    HRESULT powerDown(ComPtr<IProgress> &aProgress);
    HRESULT reset();
    HRESULT pause();
    HRESULT resume();
    HRESULT powerButton();
    HRESULT sleepButton();
    HRESULT getPowerButtonHandled(BOOL *aHandled);
    HRESULT getGuestEnteredACPIMode(BOOL *aEntered);
    HRESULT getDeviceActivity(const std::vector<DeviceType_T> &aType,
                              std::vector<DeviceActivity_T> &aActivity);
    HRESULT attachUSBDevice(const com::Guid &aId, const com::Utf8Str &aCaptureFilename);
    HRESULT detachUSBDevice(const com::Guid &aId,
                            ComPtr<IUSBDevice> &aDevice);
    HRESULT findUSBDeviceByAddress(const com::Utf8Str &aName,
                                   ComPtr<IUSBDevice> &aDevice);
    HRESULT findUSBDeviceById(const com::Guid &aId,
                              ComPtr<IUSBDevice> &aDevice);
    HRESULT createSharedFolder(const com::Utf8Str &aName,
                               const com::Utf8Str &aHostPath,
                               BOOL aWritable,
                               BOOL aAutomount,
                               const com::Utf8Str &aAutoMountPoint);
    HRESULT removeSharedFolder(const com::Utf8Str &aName);
    HRESULT teleport(const com::Utf8Str &aHostname,
                     ULONG aTcpport,
                     const com::Utf8Str &aPassword,
                     ULONG aMaxDowntime,
                     ComPtr<IProgress> &aProgress);
    HRESULT addEncryptionPassword(const com::Utf8Str &aId, const com::Utf8Str &aPassword,
                                  BOOL aClearOnSuspend);
    HRESULT addEncryptionPasswords(const std::vector<com::Utf8Str> &aIds, const std::vector<com::Utf8Str> &aPasswords,
                                   BOOL aClearOnSuspend);
    HRESULT removeEncryptionPassword(const com::Utf8Str &aId);
    HRESULT clearAllEncryptionPasswords();

    static DECLCALLBACK(int) notifyNatDnsChangeCallback(PPDMIBASE pIBase, uint32_t uDrvInstance, bool fUsbDev,
                                                        const char *pszDevice, uint32_t uDevInstance, unsigned uLun,
                                                        void *pvUser);
    Utf8Str VRDPServerErrorToMsg(int vrc);

    /**
     *  Base template for AutoVMCaller and SafeVMPtr. Template arguments
     *  have the same meaning as arguments of Console::addVMCaller().
     */
    template <bool taQuiet = false, bool taAllowNullVM = false>
    class AutoVMCallerBase
    {
    public:
        AutoVMCallerBase(Console *aThat) : mThat(aThat), mRC(E_FAIL)
        {
            Assert(aThat);
            mRC = aThat->i_addVMCaller(taQuiet, taAllowNullVM);
        }
        ~AutoVMCallerBase()
        {
            doRelease();
        }
        /** Decreases the number of callers before the instance is destroyed. */
        void releaseCaller()
        {
            Assert(SUCCEEDED(mRC));
            doRelease();
        }
        /** Restores the number of callers after by #release(). #hrc() must be
         *  rechecked to ensure the operation succeeded. */
        void addYY()
        {
            AssertReturnVoid(!SUCCEEDED(mRC));
            mRC = mThat->i_addVMCaller(taQuiet, taAllowNullVM);
        }
        /** Returns the result of Console::addVMCaller() */
        HRESULT hrc() const { return mRC; }
        /** Shortcut to SUCCEEDED(hrc()) */
        bool isOk() const { return SUCCEEDED(mRC); }
    protected:
        Console *mThat;
        void doRelease()
        {
            if (SUCCEEDED(mRC))
            {
                mThat->i_releaseVMCaller();
                mRC = E_FAIL;
            }
        }
    private:
        HRESULT mRC; /* Whether the caller was added. */
        DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(AutoVMCallerBase);
    };

#if 0
    /**
     *  Helper class that protects sections of code using the mpUVM pointer by
     *  automatically calling addVMCaller() on construction and
     *  releaseVMCaller() on destruction. Intended for Console methods dealing
     *  with mpUVM. The usage pattern is:
     *  <code>
     *      AutoVMCaller autoVMCaller(this);
     *      if (FAILED(autoVMCaller.hrc())) return autoVMCaller.hrc();
     *      ...
     *      VMR3ReqCall (mpUVM, ...
     *  </code>
     *
     *  @note Temporarily locks the argument for writing.
     *
     *  @sa SafeVMPtr, SafeVMPtrQuiet
     *  @note Obsolete, use SafeVMPtr
     */
    typedef AutoVMCallerBase<false, false> AutoVMCaller;
#endif

    /**
     *  Same as AutoVMCaller but doesn't set extended error info on failure.
     *
     *  @note Temporarily locks the argument for writing.
     *  @note Obsolete, use SafeVMPtrQuiet
     */
    typedef AutoVMCallerBase<true, false> AutoVMCallerQuiet;

    /**
     *  Same as AutoVMCaller but allows a null VM pointer (to trigger an error
     *  instead of assertion).
     *
     *  @note Temporarily locks the argument for writing.
     *  @note Obsolete, use SafeVMPtr
     */
    typedef AutoVMCallerBase<false, true> AutoVMCallerWeak;

    /**
     *  Same as AutoVMCaller but doesn't set extended error info on failure
     *  and allows a null VM pointer (to trigger an error instead of
     *  assertion).
     *
     *  @note Temporarily locks the argument for writing.
     *  @note Obsolete, use SafeVMPtrQuiet
     */
    typedef AutoVMCallerBase<true, true> AutoVMCallerQuietWeak;

    /**
     *  Base template for SafeVMPtr and SafeVMPtrQuiet.
     */
    template<bool taQuiet = false>
    class SafeVMPtrBase : public AutoVMCallerBase<taQuiet, true>
    {
        typedef AutoVMCallerBase<taQuiet, true> Base;
    public:
        SafeVMPtrBase(Console *aThat) : Base(aThat), mRC(E_FAIL), mpUVM(NULL), mpVMM(NULL)
        {
            if (Base::isOk())
                mRC = aThat->i_safeVMPtrRetainer(&mpUVM, &mpVMM, taQuiet);
        }
        ~SafeVMPtrBase()
        {
            doRelease();
        }
        /** Direct PUVM access. */
        PUVM rawUVM() const { return mpUVM; }
        /** Direct PCVMMR3VTABLE access. */
        PCVMMR3VTABLE vtable() const { return mpVMM; }
        /** Release the handles. */
        void release()
        {
            Assert(SUCCEEDED(mRC));
            doRelease();
        }

        /** The combined result of Console::addVMCaller() and Console::safeVMPtrRetainer */
        HRESULT hrc() const { return Base::isOk() ? mRC : Base::hrc(); }
        /** Shortcut to SUCCEEDED(hrc()) */
        bool isOk() const { return SUCCEEDED(mRC) && Base::isOk(); }

    private:
        void doRelease()
        {
            if (SUCCEEDED(mRC))
            {
                Base::mThat->i_safeVMPtrReleaser(&mpUVM);
                mRC = E_FAIL;
            }
            Base::doRelease();
        }
        HRESULT         mRC; /* Whether the VM ptr was retained. */
        PUVM            mpUVM;
        PCVMMR3VTABLE   mpVMM;
        DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(SafeVMPtrBase);
    };

public:

    /*
     *  Helper class that safely manages the Console::mpUVM pointer
     *  by calling addVMCaller() on construction and releaseVMCaller() on
     *  destruction. Intended for Console children. The usage pattern is:
     *  <code>
     *      Console::SafeVMPtr ptrVM(mParent);
     *      if (!ptrVM.isOk())
     *          return ptrVM.hrc();
     *      ...
     *      VMR3ReqCall(ptrVM.rawUVM(), ...
     *      ...
     *      printf("%p\n", ptrVM.rawUVM());
     *  </code>
     *
     *  @note Temporarily locks the argument for writing.
     *
     *  @sa SafeVMPtrQuiet, AutoVMCaller
     */
    typedef SafeVMPtrBase<false> SafeVMPtr;

    /**
     *  A deviation of SafeVMPtr that doesn't set the error info on failure.
     *  Intended for pieces of code that don't need to return the VM access
     *  failure to the caller. The usage pattern is:
     *  <code>
     *      Console::SafeVMPtrQuiet pVM(mParent);
     *      if (pVM.hrc())
     *          VMR3ReqCall(pVM, ...
     *      return S_OK;
     *  </code>
     *
     *  @note Temporarily locks the argument for writing.
     *
     *  @sa SafeVMPtr, AutoVMCaller
     */
    typedef SafeVMPtrBase<true> SafeVMPtrQuiet;

    class SharedFolderData
    {
    public:
        SharedFolderData()
        { }

        SharedFolderData(const Utf8Str &aHostPath,
                         bool aWritable,
                         bool aAutoMount,
                         const Utf8Str &aAutoMountPoint)
            : m_strHostPath(aHostPath)
            , m_fWritable(aWritable)
            , m_fAutoMount(aAutoMount)
            , m_strAutoMountPoint(aAutoMountPoint)
        { }

        /** Copy constructor. */
        SharedFolderData(const SharedFolderData& aThat)
            : m_strHostPath(aThat.m_strHostPath)
            , m_fWritable(aThat.m_fWritable)
            , m_fAutoMount(aThat.m_fAutoMount)
            , m_strAutoMountPoint(aThat.m_strAutoMountPoint)
        { }

        /** Copy assignment operator. */
        SharedFolderData &operator=(SharedFolderData const &a_rThat) RT_NOEXCEPT
        {
            m_strHostPath       = a_rThat.m_strHostPath;
            m_fWritable         = a_rThat.m_fWritable;
            m_fAutoMount        = a_rThat.m_fAutoMount;
            m_strAutoMountPoint = a_rThat.m_strAutoMountPoint;

            return *this;
        }

        Utf8Str m_strHostPath;
        bool m_fWritable;
        bool m_fAutoMount;
        Utf8Str m_strAutoMountPoint;
    };

    /**
     * Class for managing emulated USB MSDs.
     */
    class USBStorageDevice
    {
    public:
        USBStorageDevice()
        { }
        /** The UUID associated with the USB device. */
        RTUUID   mUuid;
        /** Port of the storage device. */
        LONG     iPort;
    };

    typedef std::map<Utf8Str, ComObjPtr<ConsoleSharedFolder> > SharedFolderMap;
    typedef std::map<Utf8Str, SharedFolderData> SharedFolderDataMap;
    typedef std::map<Utf8Str, ComPtr<IMediumAttachment> > MediumAttachmentMap;
    typedef std::list<USBStorageDevice> USBStorageDeviceList;

    static void i_powerUpThreadTask(VMPowerUpTask *pTask);
    static void i_powerDownThreadTask(VMPowerDownTask *pTask);

private:

    typedef std::list <ComObjPtr<OUSBDevice> > USBDeviceList;
    typedef std::list <ComObjPtr<RemoteUSBDevice> > RemoteUSBDeviceList;

    HRESULT i_loadVMM(const char *pszVMMMod) RT_NOEXCEPT;
    HRESULT i_addVMCaller(bool aQuiet = false, bool aAllowNullVM = false);
    void    i_releaseVMCaller();
    HRESULT i_safeVMPtrRetainer(PUVM *a_ppUVM, PCVMMR3VTABLE *a_ppVMM, bool aQuiet) RT_NOEXCEPT;
    void    i_safeVMPtrReleaser(PUVM *a_ppUVM);

    HRESULT i_consoleInitReleaseLog(const ComPtr<IMachine> aMachine);

    HRESULT i_powerUp(IProgress **aProgress, bool aPaused);
    HRESULT i_powerDown(IProgress *aProgress = NULL);

/* Note: FreeBSD needs this whether netflt is used or not. */
#if ((defined(RT_OS_LINUX) && !defined(VBOX_WITH_NETFLT)) || defined(RT_OS_FREEBSD))
    HRESULT i_attachToTapInterface(INetworkAdapter *networkAdapter);
    HRESULT i_detachFromTapInterface(INetworkAdapter *networkAdapter);
#endif
    HRESULT i_powerDownHostInterfaces();

    HRESULT i_setMachineState(MachineState_T aMachineState, bool aUpdateServer = true);
    HRESULT i_setMachineStateLocally(MachineState_T aMachineState)
    {
        return i_setMachineState(aMachineState, false /* aUpdateServer */);
    }

    HRESULT i_findSharedFolder(const Utf8Str &strName,
                               ComObjPtr<ConsoleSharedFolder> &aSharedFolder,
                               bool aSetError = false);

    HRESULT i_fetchSharedFolders(BOOL aGlobal);
    bool    i_findOtherSharedFolder(const Utf8Str &straName,
                                    SharedFolderDataMap::const_iterator &aIt);

    HRESULT i_createSharedFolder(const Utf8Str &strName, const SharedFolderData &aData);
    HRESULT i_removeSharedFolder(const Utf8Str &strName);

    HRESULT i_suspendBeforeConfigChange(PUVM pUVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock, bool *pfResume);
    void    i_resumeAfterConfigChange(PUVM pUVM, PCVMMR3VTABLE pVMM);

    static DECLCALLBACK(int) i_configConstructor(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, void *pvConsole);
    void InsertConfigString(PCFGMNODE pNode, const char *pcszName, const char *pcszValue);
    void InsertConfigString(PCFGMNODE pNode, const char *pcszName, const Utf8Str &rStrValue);
    void InsertConfigString(PCFGMNODE pNode, const char *pcszName, const Bstr &rBstrValue);
    void InsertConfigStringF(PCFGMNODE pNode, const char *pcszName, const char *pszFormat, ...);
    void InsertConfigPassword(PCFGMNODE pNode, const char *pcszName, const Utf8Str &rStrValue);
    void InsertConfigBytes(PCFGMNODE pNode, const char *pcszName, const void *pvBytes, size_t cbBytes);
    void InsertConfigInteger(PCFGMNODE pNode, const char *pcszName, uint64_t u64Integer);
    void InsertConfigNode(PCFGMNODE pNode, const char *pcszName, PCFGMNODE *ppChild);
    void InsertConfigNodeF(PCFGMNODE pNode, PCFGMNODE *ppChild, const char *pszNameFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);
    void RemoveConfigValue(PCFGMNODE pNode, const char *pcszName);
    int  SetBiosDiskInfo(ComPtr<IMachine> pMachine, PCFGMNODE pCfg, PCFGMNODE pBiosCfg,
                         Bstr controllerName, const char * const s_apszBiosConfig[4]);
    void i_configAudioDriver(IVirtualBox *pVirtualBox, IMachine *pMachine, PCFGMNODE pLUN, const char *pszDriverName,
                             bool fAudioEnabledIn, bool fAudioEnabledOut);
    int i_configConstructorInner(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock);
    int i_configConstructorX86(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock);
#ifdef VBOX_WITH_VIRT_ARMV8
    int i_configConstructorArmV8(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock);
#endif
    int i_configCfgmOverlay(PCFGMNODE pRoot, IVirtualBox *pVirtualBox, IMachine *pMachine);
    int i_configDumpAPISettingsTweaks(IVirtualBox *pVirtualBox, IMachine *pMachine);

    int i_configGraphicsController(PCFGMNODE pDevices,
                                   const GraphicsControllerType_T graphicsController,
                                   BusAssignmentManager *pBusMgr,
                                   const ComPtr<IMachine> &ptrMachine,
                                   const ComPtr<IGraphicsAdapter> &ptrGraphicsAdapter,
                                   const ComPtr<IFirmwareSettings> &ptrFirmwareSettings,
                                   bool fForceVmSvga3 = false, bool fExposeLegacyVga = true);
    int i_checkMediumLocation(IMedium *pMedium, bool *pfUseHostIOCache);
    int i_unmountMediumFromGuest(PUVM pUVM, PCVMMR3VTABLE pVMM, StorageBus_T enmBus, DeviceType_T enmDevType,
                                 const char *pcszDevice, unsigned uInstance, unsigned uLUN,
                                 bool fForceUnmount) RT_NOEXCEPT;
    int i_removeMediumDriverFromVm(PCFGMNODE pCtlInst,
                                   const char *pcszDevice,
                                   unsigned uInstance,
                                   unsigned uLUN,
                                   StorageBus_T enmBus,
                                   bool fAttachDetach,
                                   bool fHotplug,
                                   bool fForceUnmount,
                                   PUVM pUVM,
                                   PCVMMR3VTABLE pVMM,
                                   DeviceType_T enmDevType,
                                   PCFGMNODE *ppLunL0);
    int i_configMediumAttachment(const char *pcszDevice,
                                 unsigned uInstance,
                                 StorageBus_T enmBus,
                                 bool fUseHostIOCache,
                                 bool fBuiltinIoCache,
                                 bool fInsertDiskIntegrityDrv,
                                 bool fSetupMerge,
                                 unsigned uMergeSource,
                                 unsigned uMergeTarget,
                                 IMediumAttachment *pMediumAtt,
                                 MachineState_T aMachineState,
                                 HRESULT *phrc,
                                 bool fAttachDetach,
                                 bool fForceUnmount,
                                 bool fHotplug,
                                 PUVM pUVM,
                                 PCVMMR3VTABLE pVMM,
                                 DeviceType_T *paLedDevType,
                                 PCFGMNODE *ppLunL0);
    int i_configMedium(PCFGMNODE pLunL0,
                       bool fPassthrough,
                       DeviceType_T enmType,
                       bool fUseHostIOCache,
                       bool fBuiltinIoCache,
                       bool fInsertDiskIntegrityDrv,
                       bool fSetupMerge,
                       unsigned uMergeSource,
                       unsigned uMergeTarget,
                       const char *pcszBwGroup,
                       bool fDiscard,
                       bool fNonRotational,
                       ComPtr<IMedium> ptrMedium,
                       MachineState_T aMachineState,
                       HRESULT *phrc);
    int i_configMediumProperties(PCFGMNODE pCur, IMedium *pMedium, bool *pfHostIP, bool *pfEncrypted);
    /** i_reconfigureMediumAttachment has too many problematic arguments for
     * passing directly to VMR3ReqCallUV, so we pack them up. @bugref{10725} */
    struct ReconfigureMediumAttachmentArgs
    {
        const char *pcszDevice;         /**< The name of the controller type. */
        unsigned uInstance;             /**< The instance of the controller. */
        StorageBus_T enmBus;            /**< The storage bus type of the controller. */
        bool fUseHostIOCache;           /**< Use the host I/O cache (disable async I/O). */
        bool fBuiltinIOCache;           /**< Use the builtin I/O cache. */
        bool fInsertDiskIntegrityDrv;   /**< Flag whether to insert the disk integrity driver into the chain for additionalk debugging aids. */
        bool fSetupMerge;               /**< Whether to set up a medium merge */
        unsigned uMergeSource;          /**< Merge source image index */
        unsigned uMergeTarget;          /**< Merge target image index */
        IMediumAttachment *aMediumAtt;  /**< The medium attachment. */
        MachineState_T aMachineState;   /**< The current machine state. */
    };
    static DECLCALLBACK(int) i_reconfigureMediumAttachment(Console *pThis,
                                                           PUVM pUVM,
                                                           PCVMMR3VTABLE pVMM,
                                                           ReconfigureMediumAttachmentArgs const *pArgs,
                                                           HRESULT *phrc);
    static DECLCALLBACK(int) i_changeRemovableMedium(Console *pThis,
                                                     PUVM pUVM,
                                                     PCVMMR3VTABLE pVMM,
                                                     const char *pcszDevice,
                                                     unsigned uInstance,
                                                     StorageBus_T enmBus,
                                                     bool fUseHostIOCache,
                                                     IMediumAttachment *aMediumAtt,
                                                     bool fForce);

    HRESULT i_attachRawPCIDevices(PUVM pUVM, BusAssignmentManager *BusMgr, PCFGMNODE pDevices);
    struct LEDSET;
    typedef struct LEDSET *PLEDSET;
    PPDMLED volatile *i_getLedSet(uint32_t iLedSet);
    void i_setLedType(DeviceType_T *penmSubTypeEntry, DeviceType_T enmNewType);
    HRESULT i_refreshLedTypeArrays(AutoReadLock *pReadLock);
    uint32_t i_allocateDriverLeds(uint32_t cLeds, uint32_t fTypes, DeviceType_T **ppSubTypes);
    void i_attachStatusDriver(PCFGMNODE pCtlInst, DeviceType_T enmType, uint32_t cLeds = 1);
    void i_attachStatusDriver(PCFGMNODE pCtlInst, uint32_t fTypes, uint32_t cLeds, DeviceType_T **ppaSubTypes,
                              Console::MediumAttachmentMap *pmapMediumAttachments,
                              const char *pcszDevice, unsigned uInstance);

    int i_configNetwork(const char *pszDevice, unsigned uInstance, unsigned uLun, INetworkAdapter *aNetworkAdapter,
                        PCFGMNODE pCfg,  PCFGMNODE pLunL0, PCFGMNODE pInst, bool fAttachDetach, bool fIgnoreConnectFailure,
                        PUVM pUVM, PCVMMR3VTABLE pVMM);
    int i_configProxy(ComPtr<IVirtualBox> virtualBox, PCFGMNODE pCfg, const char *pcszPrefix, const com::Utf8Str &strIpAddr);

    int i_configSerialPort(PCFGMNODE pInst, PortMode_T ePortMode, const char *pszPath, bool fServer);

    int i_configAudioCtrl(ComPtr<IVirtualBox> pVBox, ComPtr<IMachine> pMachine, BusAssignmentManager *pBusMgr, PCFGMNODE pDevices,
                          bool fOsXGuest, bool *pfAudioEnabled);
    int i_configVmmDev(ComPtr<IMachine> pMachine, BusAssignmentManager *pBusMgr, PCFGMNODE pDevices, bool fMmioReq = false);
    int i_configPdm(ComPtr<IMachine> pMachine, PCVMMR3VTABLE pVMM, PUVM pUVM, PCFGMNODE pRoot);
    int i_configUsb(ComPtr<IMachine> pMachine, BusAssignmentManager *pBusMgr, PCFGMNODE pRoot, PCFGMNODE pDevices,
                    KeyboardHIDType_T enmKbdHid, PointingHIDType_T enmPointingHid, PCFGMNODE *ppUsbDevices);
    int i_configGuestDbg(ComPtr<IVirtualBox> pVBox, ComPtr<IMachine> pMachine, PCFGMNODE pRoot);
    int i_configStorageCtrls(ComPtr<IMachine> pMachine, BusAssignmentManager *pBusMgr, PCVMMR3VTABLE pVMM, PUVM pUVM,
                             PCFGMNODE pDevices, PCFGMNODE pUsbDevices, PCFGMNODE pBiosCfg, bool *pfFdcEnabled);
    int i_configNetworkCtrls(ComPtr<IMachine> pMachine, ComPtr<IPlatformProperties> pPlatformProperties,
                             ChipsetType_T enmChipset, BusAssignmentManager *pBusMgr, PCVMMR3VTABLE pVMM, PUVM pUVM,
                             PCFGMNODE pDevices, PCFGMNODE pUsbDevices, std::list<BootNic> &llBootNics);
#if defined(VBOX_WITH_TPM)
    int i_configTpm(ComPtr<ITrustedPlatformModule> pTpm, TpmType_T enmTpmType, PCFGMNODE pDevices,
                    RTGCPHYS GCPhysTpmMmio, uint32_t uIrq, RTGCPHYS GCPhysTpmPpi, bool fCrb = false);
#endif

    static DECLCALLBACK(void) i_vmstateChangeCallback(PUVM pUVM, PCVMMR3VTABLE pVMM, VMSTATE enmState,
                                                      VMSTATE enmOldState, void *pvUser);
    static DECLCALLBACK(int) i_unplugCpu(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, VMCPUID idCpu);
    static DECLCALLBACK(int) i_plugCpu(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, VMCPUID idCpu);
    HRESULT i_doMediumChange(IMediumAttachment *aMediumAttachment, bool fForce, PUVM pUVM, PCVMMR3VTABLE pVMM);
    HRESULT i_doCPURemove(ULONG aCpu, PUVM pUVM, PCVMMR3VTABLE pVMM);
    HRESULT i_doCPUAdd(ULONG aCpu, PUVM pUVM, PCVMMR3VTABLE pVMM);

    HRESULT i_doNetworkAdapterChange(PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszDevice, unsigned uInstance,
                                     unsigned uLun, INetworkAdapter *aNetworkAdapter);
    static DECLCALLBACK(int) i_changeNetworkAttachment(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszDevice,
                                                       unsigned uInstance, unsigned uLun, INetworkAdapter *aNetworkAdapter);
    static DECLCALLBACK(int) i_changeSerialPortAttachment(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, ISerialPort *pSerialPort);

    int i_changeClipboardMode(ClipboardMode_T aClipboardMode);
    int i_changeClipboardFileTransferMode(bool aEnabled);
    int i_changeDnDMode(DnDMode_T aDnDMode);

#ifdef VBOX_WITH_USB
    HRESULT i_attachUSBDevice(IUSBDevice *aHostDevice, ULONG aMaskedIfs, const Utf8Str &aCaptureFilename);
    HRESULT i_detachUSBDevice(const ComObjPtr<OUSBDevice> &aHostDevice);

    static DECLCALLBACK(int) i_usbAttachCallback(Console *that, PUVM pUVM, PCVMMR3VTABLE pVMM, IUSBDevice *aHostDevice,
                                                 PCRTUUID aUuid, const char *aBackend, const char *aAddress, PCFGMNODE pRemoteCfg,
                                                 USBConnectionSpeed_T *penmSpeed, ULONG *pfMaskedIfs,
                                                 const char *pszCaptureFilename);
    static DECLCALLBACK(int) i_usbDetachCallback(Console *that, PUVM pUVM, PCVMMR3VTABLE pVMM, PCRTUUID aUuid);
    static DECLCALLBACK(PREMOTEUSBCALLBACK) i_usbQueryRemoteUsbBackend(void *pvUser, PCRTUUID pUuid, uint32_t idClient);

    /** Interface for the VRDP USB proxy backend to query for a device remote callback table. */
    REMOTEUSBIF mRemoteUsbIf;
#endif

    static DECLCALLBACK(int) i_attachStorageDevice(Console *pThis,
                                                   PUVM pUVM,
                                                   PCVMMR3VTABLE pVMM,
                                                   const char *pcszDevice,
                                                   unsigned uInstance,
                                                   StorageBus_T enmBus,
                                                   bool fUseHostIOCache,
                                                   IMediumAttachment *aMediumAtt,
                                                   bool fSilent);
    static DECLCALLBACK(int) i_detachStorageDevice(Console *pThis,
                                                   PUVM pUVM,
                                                   PCVMMR3VTABLE pVMM,
                                                   const char *pcszDevice,
                                                   unsigned uInstance,
                                                   StorageBus_T enmBus,
                                                   IMediumAttachment *aMediumAtt,
                                                   bool fSilent);
    HRESULT i_doStorageDeviceAttach(IMediumAttachment *aMediumAttachment, PUVM pUVM, PCVMMR3VTABLE pVMM, bool fSilent);
    HRESULT i_doStorageDeviceDetach(IMediumAttachment *aMediumAttachment, PUVM pUVM, PCVMMR3VTABLE pVMM, bool fSilent);

    static DECLCALLBACK(int)    i_stateProgressCallback(PUVM pUVM, unsigned uPercent, void *pvUser);

    static DECLCALLBACK(void)   i_genericVMSetErrorCallback(PUVM pUVM, void *pvUser, int vrc, RT_SRC_POS_DECL,
                                                            const char *pszErrorFmt, va_list va);

    void                        i_atVMRuntimeErrorCallbackF(uint32_t fFatal, const char *pszErrorId, const char *pszFormat, ...);
    static DECLCALLBACK(void)   i_atVMRuntimeErrorCallback(PUVM pUVM, void *pvUser, uint32_t fFatal,
                                                           const char *pszErrorId, const char *pszFormat, va_list va);

    HRESULT                     i_captureUSBDevices(PUVM pUVM);
    void                        i_detachAllUSBDevices(bool aDone);


    static DECLCALLBACK(int)    i_vmm2User_SaveState(PCVMM2USERMETHODS pThis, PUVM pUVM);
    static DECLCALLBACK(void)   i_vmm2User_NotifyEmtInit(PCVMM2USERMETHODS pThis, PUVM pUVM, PUVMCPU pUVCpu);
    static DECLCALLBACK(void)   i_vmm2User_NotifyEmtTerm(PCVMM2USERMETHODS pThis, PUVM pUVM, PUVMCPU pUVCpu);
    static DECLCALLBACK(void)   i_vmm2User_NotifyPdmtInit(PCVMM2USERMETHODS pThis, PUVM pUVM);
    static DECLCALLBACK(void)   i_vmm2User_NotifyPdmtTerm(PCVMM2USERMETHODS pThis, PUVM pUVM);
    static DECLCALLBACK(void)   i_vmm2User_NotifyResetTurnedIntoPowerOff(PCVMM2USERMETHODS pThis, PUVM pUVM);
    static DECLCALLBACK(void *) i_vmm2User_QueryGenericObject(PCVMM2USERMETHODS pThis, PUVM pUVM, PCRTUUID pUuid);

    static DECLCALLBACK(void *) i_drvStatus_QueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(void)   i_drvStatus_UnitChanged(PPDMILEDCONNECTORS pInterface, unsigned iLUN);
    static DECLCALLBACK(int)    i_drvStatus_MediumEjected(PPDMIMEDIANOTIFY pInterface, unsigned iLUN);
    static DECLCALLBACK(void)   i_drvStatus_Destruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(int)    i_drvStatus_Construct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);

    static DECLCALLBACK(int)    i_pdmIfSecKey_KeyRetain(PPDMISECKEY pInterface, const char *pszId, const uint8_t **ppbKey,
                                                        size_t *pcbKey);
    static DECLCALLBACK(int)    i_pdmIfSecKey_KeyRelease(PPDMISECKEY pInterface, const char *pszId);
    static DECLCALLBACK(int)    i_pdmIfSecKey_PasswordRetain(PPDMISECKEY pInterface, const char *pszId, const char **ppszPassword);
    static DECLCALLBACK(int)    i_pdmIfSecKey_PasswordRelease(PPDMISECKEY pInterface, const char *pszId);

    static DECLCALLBACK(int)    i_pdmIfSecKeyHlp_KeyMissingNotify(PPDMISECKEYHLP pInterface);

    volatile uint32_t mcVRDPClients;
    uint32_t mu32SingleRDPClientId; /* The id of a connected client in the single connection mode. */
    volatile  bool mfGuestCredentialsProvided;

    static const char *sSSMConsoleUnit;

    HRESULT i_loadDataFromSavedState();
    int i_loadStateFileExecInternal(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t u32Version);

    static DECLCALLBACK(int)    i_saveStateFileExec(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser);
    static DECLCALLBACK(int)    i_loadStateFileExec(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser,
                                                    uint32_t uVersion, uint32_t uPass);

#ifdef VBOX_WITH_GUEST_PROPS
    HRESULT                     i_doEnumerateGuestProperties(const Utf8Str &aPatterns,
                                                             std::vector<Utf8Str> &aNames,
                                                             std::vector<Utf8Str> &aValues,
                                                             std::vector<LONG64>  &aTimestamps,
                                                             std::vector<Utf8Str> &aFlags);

    void i_guestPropertiesHandleVMReset(void);
    bool i_guestPropertiesVRDPEnabled(void);
    void i_guestPropertiesVRDPUpdateLogon(uint32_t u32ClientId, const char *pszUser, const char *pszDomain);
    void i_guestPropertiesVRDPUpdateActiveClient(uint32_t u32ClientId);
    void i_guestPropertiesVRDPUpdateClientAttach(uint32_t u32ClientId, bool fAttached);
    void i_guestPropertiesVRDPUpdateNameChange(uint32_t u32ClientId, const char *pszName);
    void i_guestPropertiesVRDPUpdateIPAddrChange(uint32_t u32ClientId, const char *pszIPAddr);
    void i_guestPropertiesVRDPUpdateLocationChange(uint32_t u32ClientId, const char *pszLocation);
    void i_guestPropertiesVRDPUpdateOtherInfoChange(uint32_t u32ClientId, const char *pszOtherInfo);
    void i_guestPropertiesVRDPUpdateDisconnect(uint32_t u32ClientId);
#endif

    /** @name Disk encryption support
     * @{ */
    HRESULT i_consoleParseDiskEncryption(const char *psz, const char **ppszEnd);
    HRESULT i_configureEncryptionForDisk(const Utf8Str &strId, unsigned *pcDisksConfigured);
    HRESULT i_clearDiskEncryptionKeysOnAllAttachmentsWithKeyId(const Utf8Str &strId);
    HRESULT i_initSecretKeyIfOnAllAttachments(void);
    int i_consoleParseKeyValue(const char *psz, const char **ppszEnd,
                               char **ppszKey, char **ppszVal);
    void i_removeSecretKeysOnSuspend();
    /** @} */

    /** @name Teleporter support
     * @{ */
    static DECLCALLBACK(int)    i_teleporterSrcThreadWrapper(RTTHREAD hThreadSelf, void *pvUser);
    HRESULT                     i_teleporterSrc(TeleporterStateSrc *pState);
    HRESULT                     i_teleporterSrcReadACK(TeleporterStateSrc *pState, const char *pszWhich, const char *pszNAckMsg = NULL);
    HRESULT                     i_teleporterSrcSubmitCommand(TeleporterStateSrc *pState, const char *pszCommand, bool fWaitForAck = true);
    HRESULT                     i_teleporterTrg(PUVM pUVM, PCVMMR3VTABLE pVMM, IMachine *pMachine, Utf8Str *pErrorMsg,
                                                bool fStartPaused, Progress *pProgress, bool *pfPowerOffOnFailure);
    static DECLCALLBACK(int)    i_teleporterTrgServeConnection(RTSOCKET Sock, void *pvUser);
    /** @} */

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    /** @name Encrypted log interface
     * @{ */
    static DECLCALLBACK(int)    i_logEncryptedDirCtxOpen(PCRTLOGOUTPUTIF pIf, void *pvUser, const char *pszFilename, void **pvDirCtx);
    static DECLCALLBACK(int)    i_logEncryptedDirCtxClose(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx);
    static DECLCALLBACK(int)    i_logEncryptedDelete(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx, const char *pszFilename);
    static DECLCALLBACK(int)    i_logEncryptedRename(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx,
                                                     const char *pszFilenameOld, const char *pszFilenameNew, uint32_t fFlags);
    static DECLCALLBACK(int)    i_logEncryptedOpen(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx, const char *pszFilename, uint32_t fFlags);
    static DECLCALLBACK(int)    i_logEncryptedClose(PCRTLOGOUTPUTIF pIf, void *pvUser);
    static DECLCALLBACK(int)    i_logEncryptedQuerySize(PCRTLOGOUTPUTIF pIf, void *pvUser, uint64_t *pcbSize);
    static DECLCALLBACK(int)    i_logEncryptedWrite(PCRTLOGOUTPUTIF pIf, void *pvUser, const void *pvBuf,
                                                    size_t cbWrite, size_t *pcbWritten);
    static DECLCALLBACK(int)    i_logEncryptedFlush(PCRTLOGOUTPUTIF pIf, void *pvUser);
    /** The logging output interface for encrypted logs. */
    static RTLOGOUTPUTIF const  s_ConsoleEncryptedLogOutputIf;
    /** @} */
#endif

    bool mSavedStateDataLoaded : 1;

    const ComPtr<IMachine> mMachine;
    const ComPtr<IInternalMachineControl> mControl;

    const ComPtr<IVRDEServer> mVRDEServer;

    ConsoleVRDPServer * const mConsoleVRDPServer;
    bool mfVRDEChangeInProcess;
    bool mfVRDEChangePending;
    const ComObjPtr<Guest> mGuest;
    const ComObjPtr<Keyboard> mKeyboard;
    const ComObjPtr<Mouse> mMouse;
    const ComObjPtr<Display> mDisplay;
    const ComObjPtr<MachineDebugger> mDebugger;
    const ComObjPtr<VRDEServerInfo> mVRDEServerInfo;
    /** This can safely be used without holding any locks.
     * An AutoCaller suffices to prevent it being destroy while in use and
     * internally there is a lock providing the necessary serialization. */
    const ComObjPtr<EventSource> mEventSource;
#ifdef VBOX_WITH_EXTPACK
    const ComObjPtr<ExtPackManager> mptrExtPackManager;
#endif
    const ComObjPtr<EmulatedUSB> mEmulatedUSB;
    const ComObjPtr<NvramStore> mptrNvramStore;
#ifdef VBOX_WITH_VIRT_ARMV8
    const ComObjPtr<ResourceStore> mptrResourceStore;
#endif

    USBDeviceList mUSBDevices;
    RemoteUSBDeviceList mRemoteUSBDevices;

    SharedFolderDataMap m_mapGlobalSharedFolders;
    SharedFolderDataMap m_mapMachineSharedFolders;
    SharedFolderMap m_mapSharedFolders;             // the console instances

    /** VMM loader handle. */
    RTLDRMOD mhModVMM;
    /** The VMM vtable. */
    PCVMMR3VTABLE mpVMM;
    /** The user mode VM handle. */
    PUVM mpUVM;
    /** Holds the number of "readonly" mpUVM callers (users). */
    uint32_t mVMCallers;
    /** Semaphore posted when the number of mpUVM callers drops to zero. */
    RTSEMEVENT mVMZeroCallersSem;
    /** true when Console has entered the mpUVM destruction phase. */
    bool mVMDestroying : 1;
    /** true when power down is initiated by vmstateChangeCallback (EMT). */
    bool mVMPoweredOff : 1;
    /** true when vmstateChangeCallback shouldn't initiate a power down.  */
    bool mVMIsAlreadyPoweringOff : 1;
    /** true if we already showed the snapshot folder size warning. */
    bool mfSnapshotFolderSizeWarningShown : 1;
    /** true if we already showed the snapshot folder ext4/xfs bug warning. */
    bool mfSnapshotFolderExt4WarningShown : 1;
    /** true if we already listed the disk type of the snapshot folder. */
    bool mfSnapshotFolderDiskTypeShown : 1;
    /** true if a USB controller is available (i.e. USB devices can be attached). */
    bool mfVMHasUsbController : 1;
    /** Shadow of the VBoxInternal2/TurnResetIntoPowerOff extra data setting.
     * This is initialized by Console::i_configConstructorInner(). */
    bool mfTurnResetIntoPowerOff : 1;
    /** true if the VM power off was caused by reset. */
    bool mfPowerOffCausedByReset : 1;

    /** Pointer to the VMM -> User (that's us) callbacks. */
    struct MYVMM2USERMETHODS : public VMM2USERMETHODS
    {
        Console *pConsole;
        /** The in-progress snapshot. */
        ISnapshot *pISnapshot;
    } *mpVmm2UserMethods;

    /** The current network attachment type in the VM.
     * This doesn't have to match the network attachment type maintained in the
     * NetworkAdapter. This is needed to change the network attachment
     * dynamically.
     */
    typedef std::vector<NetworkAttachmentType_T> NetworkAttachmentTypeVector;
    NetworkAttachmentTypeVector meAttachmentType;

    VMMDev *                    m_pVMMDev;
    AudioVRDE * const           mAudioVRDE;
#ifdef VBOX_WITH_USB_CARDREADER
    UsbCardReader * const       mUsbCardReader;
#endif
    BusAssignmentManager*       mBusMgr;

    /** @name LEDs and their management
     * @{ */
    /** Read/write lock separating LED allocations and per-type data construction
     * (write) from queries (read). */
    RWLockHandle            mLedLock;
    /** LED configuration generation.  This is increased whenever a new set is
     *  allocated or a sub-device type changes. */
    uint32_t                muLedGen;
    /** The LED configuration generation which maLedTypes was constructed for. */
    uint32_t                muLedTypeGen;
    /** Number of LED sets in use in maLedSets. */
    uint32_t                mcLedSets;
    /** LED sets. */
    struct LEDSET
    {
        /** Bitmask of possible DeviceType_T values (e.g. RT_BIT_32(DeviceType_Network)). */
        uint32_t            fTypes;
        /** Number of LEDs.   */
        uint32_t            cLeds;
        /** Array of PDMLED pointers.  The pointers in the array can be changed at any
         *  time by Console::i_drvStatus_UnitChanged(). */
        PPDMLED volatile   *papLeds;
        /** Optionally, device types for each individual LED. Runs parallel to papLeds. */
        DeviceType_T       *paSubTypes;
    } maLedSets[32];
    /** LEDs data organized by DeviceType_T.
     * This is reconstructed by Console::i_refreshLedTypeArrays() when
     * Console::getDeviceActivity is called and mLedTypeGen doesn't match
     * muLedGen. */
    struct
    {
        /** Number of possibly valid entries in pappLeds. */
        uint32_t            cLeds;
        /** Number of allocated entries. */
        uint32_t            cAllocated;
        /** Array of pointer to LEDSET::papLed entries.
         * The indirection is due to Console::i_drvStatus_UnitChanged() only knowing
         * about the LEDSET::papLeds. */
        PPDMLED volatile  **pappLeds;
    } maLedTypes[DeviceType_End];
    /** @} */

    MediumAttachmentMap mapMediumAttachments;

    /** List of attached USB storage devices. */
    USBStorageDeviceList mUSBStorageDevices;

    /** Store for secret keys. */
    SecretKeyStore * const m_pKeyStore;
    /** Number of disks configured for encryption. */
    unsigned               m_cDisksEncrypted;
    /** Number of disks which have the key in the map. */
    unsigned               m_cDisksPwProvided;

    /** Current active port modes of the supported serial ports. */
    PortMode_T             m_aeSerialPortMode[4];

    /** Pointer to the key consumer -> provider (that's us) callbacks. */
    struct MYPDMISECKEY : public PDMISECKEY
    {
        Console *pConsole;
    } *mpIfSecKey;

    /** Pointer to the key helpers -> provider (that's us) callbacks. */
    struct MYPDMISECKEYHLP : public PDMISECKEYHLP
    {
        Console *pConsole;
    } *mpIfSecKeyHlp;

/* Note: FreeBSD needs this whether netflt is used or not. */
#if ((defined(RT_OS_LINUX) && !defined(VBOX_WITH_NETFLT)) || defined(RT_OS_FREEBSD))
    Utf8Str      maTAPDeviceName[8];
    RTFILE       maTapFD[8];
#endif

    bool mVMStateChangeCallbackDisabled;

    bool mfUseHostClipboard;

    /** Local machine state value. */
    MachineState_T mMachineState;

    /** Machine uuid string. */
    Bstr mstrUuid;

    /** @name Members related to the cryptographic support interface.
     * @{ */
    /** The loaded module handle if loaded. */
    RTLDRMOD                            mhLdrModCrypto;
    /** Reference counter tracking how many users of the cryptographic support
     * are there currently. */
    volatile uint32_t                   mcRefsCrypto;
    /** Pointer to the cryptographic support interface. */
    PCVBOXCRYPTOIF                      mpCryptoIf;
    /** @} */

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    /** Flag whether the log is encrypted. */
    bool                                m_fEncryptedLog;
    /** The file handle of the encrypted log. */
    RTVFSFILE                           m_hVfsFileLog;
    /** The log file key ID. */
    Utf8Str                             m_strLogKeyId;
    /** The log file key store. */
    Utf8Str                             m_strLogKeyStore;
#endif

#ifdef VBOX_WITH_SHARED_CLIPBOARD
    /* Service extension for the Shared Clipboard HGCM service. */
    HGCMSVCEXTHANDLE                    m_hHgcmSvcExtShCl;
#endif

#ifdef VBOX_WITH_DRAG_AND_DROP
    /* Service extension for the Drag'n Drop HGCM service. */
    HGCMSVCEXTHANDLE                    m_hHgcmSvcExtDragAndDrop;
#endif
    /** Pointer to the progress object of a live cancelable task.
     *
     * This is currently only used by Console::Teleport(), but is intended to later
     * be used by the live snapshot code path as well.  Actions like
     * Console::PowerDown, which automatically cancels out the running snapshot /
     * teleportation operation, will cancel the teleportation / live snapshot
     * operation before starting. */
    ComPtr<IProgress> mptrCancelableProgress;

    ComPtr<IEventListener> mVmListener;

#ifdef VBOX_WITH_RECORDING
    /** Structure for keeping recording-related stuff. */
    struct Recording
    {
        Recording()
# ifdef VBOX_WITH_AUDIO_RECORDING
            : mAudioRec(NULL)
# endif
        { }

        /** The recording progress object to use. */
        ComObjPtr<IProgress>  mProgress;
        /** The recording context. */
        RecordingContext      mCtx;
# ifdef VBOX_WITH_AUDIO_RECORDING
        /** Pointer to capturing audio backend. */
        AudioVideoRec * const mAudioRec;
# endif
    } mRecording;
#endif /* VBOX_WITH_RECORDING */

#ifdef VBOX_WITH_CLOUD_NET
    GatewayInfo mGateway;
#endif /* VBOX_WITH_CLOUD_NET */

    friend class VMTask;
    friend class ConsoleVRDPServer;
};


class ConfigError : public RTCError
{
public:

    ConfigError(const char *pcszFunction,
                int vrc,
                const char *pcszName)
        : RTCError(Utf8StrFmt(Console::tr("%s failed: vrc=%Rrc, pcszName=%s"), pcszFunction, vrc, pcszName)),
          m_vrc(vrc)
    {
        AssertMsgFailed(("%s\n", what())); // in strict mode, hit a breakpoint here
    }

    int m_vrc;
};

DECL_HIDDEN_THROW(Utf8Str *) GetExtraDataBoth(IVirtualBox *pVirtualBox, IMachine *pMachine, const char *pszName, Utf8Str *pStrValue);

#ifndef VBOX_WITH_EFI_IN_DD2
DECLHIDDEN(int) findEfiRom(IVirtualBox* vbox, PlatformArchitecture_T aPlatformArchitecture, FirmwareType_T aFirmwareType, Utf8Str *pEfiRomFile);
#endif

#endif /* !MAIN_INCLUDED_ConsoleImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
