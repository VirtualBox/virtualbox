/* $Id: UIMachine.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachine class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIMachine_h
#define FEQT_INCLUDED_SRC_runtime_UIMachine_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QPixmap>

/* GUI includes: */
#include "UIAddDiskEncryptionPasswordDialog.h"
#include "UIExtraDataDefs.h"
#include "UIMachineDefs.h"
#include "UIMediumDefs.h"
#include "UIMousePointerShapeData.h"
#include "UITextTable.h"

/* COM includes: */
#include "KBitmapFormat.h"
#include "KChipsetType.h"
#include "KClipboardMode.h"
#include "KDeviceActivity.h"
#include "KDnDMode.h"
#include "KGraphicsControllerType.h"
#include "KGuestMonitorChangedEventType.h"
#include "KGuestMonitorStatus.h"
#include "KMachineState.h"
#include "KParavirtProvider.h"
#include "KPlatformArchitecture.h"
#include "KVMExecutionEngine.h"

/* Forward declarations: */
class QMenu;
class QWidget;
class UIActionPool;
class UIFrameBuffer;
class UIMachineLogic;
class UISession;
class CMediumAttachment;
class CNetworkAdapter;
class CUSBDevice;
class CVirtualBoxErrorInfo;
#ifdef VBOX_WS_MAC
 class QMenuBar;
 class QTimer;
#endif

/** Singleton QObject extension
  * used as virtual machine (VM) singleton instance. */
class UIMachine : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about machine initialized. */
    void sigInitialized();

    /** Requests async visual-state change. */
    void sigRequestAsyncVisualStateChange(UIVisualStateType enmVisualStateType);

    /** @name COM events stuff.
     ** @{ */
        /** Notifies about additions state change. */
        void sigAdditionsStateChange();
        /** Notifies about additions state actually change. */
        void sigAdditionsStateActualChange();
        /** Notifies about audio adapter change. */
        void sigAudioAdapterChange();
        /** Notifies about clipboard mode change. */
        void sigClipboardModeChange(KClipboardMode enmMode);
        /** Notifies about CPU execution cap change. */
        void sigCPUExecutionCapChange();
        /** Notifies about DnD mode change. */
        void sigDnDModeChange(KDnDMode enmMode);
        /** Notifies about guest monitor change. */
        void sigGuestMonitorChange(KGuestMonitorChangedEventType emnChangeType, ulong uScreenId, QRect screenGeo);
        /** Notifies about machine change. */
        void sigMachineStateChange();
        /** Notifies about medium change. */
        void sigMediumChange(const CMediumAttachment &comMediumAttachment);
        /** Notifies about network adapter change. */
        void sigNetworkAdapterChange(const CNetworkAdapter &comNetworkAdapter);
        /** Notifies about recording change. */
        void sigRecordingChange();
        /** Notifies about shared folder change. */
        void sigSharedFolderChange();
        /** Handles storage device change signal. */
        void sigStorageDeviceChange(const CMediumAttachment &comAttachment, bool fRemoved, bool fSilent);
        /** Handles USB controller change signal. */
        void sigUSBControllerChange();
        /** Handles USB device state change signal. */
        void sigUSBDeviceStateChange(const CUSBDevice &comDevice, bool fAttached, const CVirtualBoxErrorInfo &comError);
        /** Notifies about VRDE change. */
        void sigVRDEChange();

        /** Notifies about runtime error happened. */
        void sigRuntimeError(bool bIsFatal, const QString &strErrorId, const QString &strMessage);

#ifdef VBOX_WS_MAC
        /** macOS X: Notifies about VM window should be shown. */
        void sigShowWindows();
#endif
    /** @} */

    /** @name Keyboard stuff.
     ** @{ */
        /** Notifies about keyboard LEDs change. */
        void sigKeyboardLedsChange();

        /** Notifies listeners about keyboard state-change. */
        void sigKeyboardStateChange(int iState);
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Notifies listeners about mouse pointer shape change. */
        void sigMousePointerShapeChange();
        /** Notifies listeners about mouse capability change. */
        void sigMouseCapabilityChange();
        /** Notifies listeners about cursor position change. */
        void sigCursorPositionChange();

        /** Notifies listeners about mouse state-change. */
        void sigMouseStateChange(int iState);
    /** @} */

    /** @name Host-screen stuff.
     ** @{ */
        /** Notifies about host-screen count change. */
        void sigHostScreenCountChange();
        /** Notifies about host-screen geometry change. */
        void sigHostScreenGeometryChange();
        /** Notifies about host-screen available-area change. */
        void sigHostScreenAvailableAreaChange();
    /** @} */

public:

    /** Static factory to start machine. */
    static bool startMachine();
    /** Static constructor. */
    static bool create();
    /** Static destructor. */
    static void destroy();
    /** Static instance. */
    static UIMachine *instance() { return s_pInstance; }

    /** Returns whether machine is initialized. */
    bool isInitialized() const { return m_fInitialized; }

    /** Returns session UI instance. */
    UISession *uisession() const { return m_pSession; }
    /** Returns machine-logic instance. */
    UIMachineLogic *machineLogic() const { return m_pMachineLogic; }
    /** Returns frame-buffer reference for screen with @a uScreenId specified. */
    UIFrameBuffer *frameBuffer(ulong uScreenId);
    /** Returns active machine-window reference (if possible). */
    QWidget *activeWindow() const;

    /** Returns whether session UI is valid. */
    bool isSessionValid() const;

    /** Returns whether requested visual @a state allowed. */
    bool isVisualStateAllowed(UIVisualStateType state) const { return m_enmAllowedVisualStates & state; }

    /** Requests async visual-state change. */
    void asyncChangeVisualState(UIVisualStateType enmVisualStateType);

    /** Requests visual-state to be entered when possible. */
    void setRequestedVisualState(UIVisualStateType enmVisualStateType);
    /** Returns requested visual-state to be entered when possible. */
    UIVisualStateType requestedVisualState() const;

    /** @name General stuff.
     ** @{ */
        /** Returns the machine name. */
        QString machineName() const;
        /** Returns the OS type id. */
        QString osTypeId() const;

        /** Acquire machine pixmap. */
        void acquireMachinePixmap(const QSize &size, QPixmap &pixmap);
        /** Acquire user machine icon. */
        void acquireUserMachineIcon(QIcon &icon);

        /** Acquires architecture type. */
        bool acquireArchitectureType(KPlatformArchitecture &enmType);
        /** Acquires chipset type. */
        bool acquireChipsetType(KChipsetType &enmType);
    /** @} */

    /** @name Branding stuff.
     ** @{ */
        /** Returns the cached machine-window icon. */
        QIcon *machineWindowIcon() const { return m_pMachineWindowIcon; }
#ifndef VBOX_WS_MAC
        /** Returns redefined machine-window name postfix. */
        QString machineWindowNamePostfix() const { return m_strMachineWindowNamePostfix; }
#endif
    /** @} */

    /** @name Actions stuff.
     ** @{ */
        /** Returns the action-pool instance. */
        UIActionPool *actionPool() const { return m_pActionPool; }

        /** Updates additions actions state. */
        void updateStateAdditionsActions();
        /** Updates Audio action state. */
        void updateStateAudioActions();
        /** Updates Recording action state. */
        void updateStateRecordingAction();
        /** Updates VRDE server action state. */
        void updateStateVRDEServerAction();
    /** @} */

    /** @name Machine-state stuff.
     ** @{ */
        /** Returns previous machine state. */
        KMachineState machineStatePrevious() const;
        /** Returns cached machine state. */
        KMachineState machineState() const;

        /** Resets previous state to be the same as current one. */
        void forgetPreviousMachineState();

        /** Acquire live machine state. */
        bool acquireLiveMachineState(KMachineState &enmState);

        /** Returns whether VM is in one of turned off states. */
        bool isTurnedOff() const;
        /** Returns whether VM is in one of paused states. */
        bool isPaused() const;
        /** Returns whether VM was in one of paused states. */
        bool wasPaused() const;
        /** Returns whether VM is in one of running states. */
        bool isRunning() const;
        /** Returns whether VM is in one of stuck states. */
        bool isStuck() const;
        /** Returns whether VM is one of states where guest-screen is undrawable. */
        bool isGuestScreenUnDrawable() const;

        /** Resets VM. */
        bool reset();
        /** Performes VM pausing. */
        bool pause();
        /** Performes VM resuming. */
        bool unpause();
        /** Performes VM pausing/resuming depending on @a fPause state. */
        bool setPause(bool fPause);
    /** @} */

    /** @name Machine-data stuff.
     ** @{ */
        /** Acquires settings file path. */
        bool acquireSettingsFilePath(QString &strPath);

        /** Saves machine data. */
        bool saveSettings();
    /** @} */

    /** @name Snapshot stuff.
     ** @{ */
        /** Acquires snapshot count. */
        bool acquireSnapshotCount(ulong &uCount);
        /** Acquires current snapshot name. */
        bool acquireCurrentSnapshotName(QString &strName);

        /** Recursively searches for a first snapshot matching name template conditions. */
        bool acquireMaxSnapshotIndex(const QString &strNameTemplate, ulong &uIndex);

        /** Takes snapshot with name & description specified. */
        void takeSnapshot(const QString &strName, const QString &strDescription);
    /** @} */

    /** @name Audio stuff.
     ** @{ */
        /** Acquires whether audio adapter is present. */
        bool acquireWhetherAudioAdapterPresent(bool &fPresent);
        /** Acquires whether audio adapter is enabled. */
        bool acquireWhetherAudioAdapterEnabled(bool &fEnabled);
        /** Acquires whether audio adapter output is enabled. */
        bool acquireWhetherAudioAdapterOutputEnabled(bool &fEnabled);
        /** Acquires whether audio adapter input is enabled. */
        bool acquireWhetherAudioAdapterInputEnabled(bool &fEnabled);
        /** Defines whether audio adapter output is enabled. */
        bool setAudioAdapterOutputEnabled(bool fEnabled);
        /** Defines whether audio adapter input is enabled. */
        bool setAudioAdapterInputEnabled(bool fEnabled);
    /** @} */

    /** @name Host-screen stuff.
     ** @{ */
        /** Returns the list of host-screen geometries we currently have. */
        QList<QRect> hostScreens() const { return m_hostScreens; }
    /** @} */

    /** @name Guest-screen stuff.
     ** @{ */
        /** Returns whether guest-screen with @a uScreenId specified is expected to be visible. */
        bool isScreenVisibleHostDesires(ulong uScreenId) const;
        /** Defines whether guest-screen with @a uScreenId specified is expected to be @a fVisible. */
        void setScreenVisibleHostDesires(ulong uScreenId, bool fVisible);

        /** Returns whether guest-screen with @a uScreenId specified is actually visible. */
        bool isScreenVisible(ulong uScreenId) const;
        /** Defines whether guest-screen with @a uScreenId specified is actually @a fVisible. */
        void setScreenVisible(ulong uScreenId, bool fIsMonitorVisible);

        /** Returns a number of visible guest-windows. */
        int countOfVisibleWindows();
        /** Returns the list of visible guest-windows. */
        QList<int> listOfVisibleWindows() const;

        /** Returns size for guest-screen with index @a uScreenId. */
        QSize guestScreenSize(ulong uScreenId) const;

        /** Returns last full-screen size for guest-screen with index @a uScreenId. */
        QSize lastFullScreenSize(ulong uScreenId) const;
        /** Defines last full-screen @a size for guest-screen with index @a uScreenId. */
        void setLastFullScreenSize(ulong uScreenId, QSize size);

        /** Returns whether guest screen resize should be ignored. */
        bool isGuestResizeIgnored() const { return m_fIsGuestResizeIgnored; }
        /** Defines whether guest screen resize should be @a fIgnored. */
        void setGuestResizeIgnored(bool fIgnored) { m_fIsGuestResizeIgnored = fIgnored; }

        /** Acquires graphics controller type. */
        bool acquireGraphicsControllerType(KGraphicsControllerType &enmType);
        /** Acquires VRAM size. */
        bool acquireVRAMSize(ulong &uSize);
        /** Acquires whether accelerate 3D is enabled. */
        bool acquireWhetherAccelerate3DEnabled(bool &fEnabled);
        /** Acquires monitor count. */
        bool acquireMonitorCount(ulong &uCount);

        /** Acquires parameters for guest-screen with passed uScreenId. */
        bool acquireGuestScreenParameters(ulong uScreenId,
                                          ulong &uWidth, ulong &uHeight, ulong &uBitsPerPixel,
                                          long &xOrigin, long &yOrigin, KGuestMonitorStatus &enmMonitorStatus);
        /** Acquires saved info for guest-screen with passed uScreenId. */
        bool acquireSavedGuestScreenInfo(ulong uScreenId,
                                         long &xOrigin, long &yOrigin,
                                         ulong &uWidth, ulong &uHeight, bool &fEnabled);
        /** Defines video mode hint for guest-screen with passed uScreenId. */
        bool setVideoModeHint(ulong uScreenId, bool fEnabled, bool fChangeOrigin,
                              long xOrigin, long yOrigin, ulong uWidth, ulong uHeight,
                              ulong uBitsPerPixel, bool fNotify);
        /** Acquires video mode hint for guest-screen with passed uScreenId. */
        bool acquireVideoModeHint(ulong uScreenId, bool &fEnabled, bool &fChangeOrigin,
                                  long &xOrigin, long &yOrigin, ulong &uWidth, ulong &uHeight,
                                  ulong &uBitsPerPixel);
        /** Acquires screen-shot for guest-screen with passed uScreenId. */
        bool acquireScreenShot(ulong uScreenId, ulong uWidth, ulong uHeight, KBitmapFormat enmFormat, uchar *pBits);
        /** Acquires saved screen-shot info for guest-screen with passed uScreenId. */
        bool acquireSavedScreenshotInfo(ulong uScreenId, ulong &uWidth, ulong &uHeight, QVector<KBitmapFormat> &formats);
        /** Acquires saved screen-shot for guest-screen with passed uScreenId. */
        bool acquireSavedScreenshot(ulong uScreenId, KBitmapFormat enmFormat,
                                    ulong &uWidth, ulong &uHeight, QVector<BYTE> &screenshot);
        /** Notifies guest-screen with passed uScreenId about scale-factor change. */
        bool notifyScaleFactorChange(ulong uScreenId, ulong uScaleFactorWMultiplied, ulong uScaleFactorHMultiplied);
        /** Notifies display about unscaled HiDPI policy change. */
        bool notifyHiDPIOutputPolicyChange(bool fUnscaledHiDPI);
        /** Defines whether seamless mode is enabled for display. */
        bool setSeamlessMode(bool fEnabled);
        /** Notifies display about viewport changes. */
        bool viewportChanged(ulong uScreenId, ulong xOrigin, ulong yOrigin, ulong uWidth, ulong uHeight);
        /** Notifies display about all screens were invalidated. */
        bool invalidateAndUpdate();
        /** Notifies display about screen with passed uScreenId was invalidated. */
        bool invalidateAndUpdateScreen(ulong uScreenId);

        /** Acquires whether VRDE server is present. */
        bool acquireWhetherVRDEServerPresent(bool &fPresent);
        /** Acquires whether VRDE server is enabled. */
        bool acquireWhetherVRDEServerEnabled(bool &fEnabled);
        /** Defines whether VRDE server is enabled. */
        bool setVRDEServerEnabled(bool fEnabled);
        /** Acquires VRDE server port. */
        bool acquireVRDEServerPort(long &iPort);

        /** Acquires whether recording settings is present. */
        bool acquireWhetherRecordingSettingsPresent(bool &fPresent);
        /** Acquires whether recording settings is enabled. */
        bool acquireWhetherRecordingSettingsEnabled(bool &fEnabled);
        /** Defines whether recording settings is enabled. */
        bool setRecordingSettingsEnabled(bool fEnabled);
    /** @} */

    /** @name Guest additions stuff.
     ** @{ */
        /** Returns whether guest additions is active. */
        bool isGuestAdditionsActive() const;
        /** Returns whether guest additions supports graphics. */
        bool isGuestSupportsGraphics() const;
        /** Returns whether guest additions supports seamless. */
        bool isGuestSupportsSeamless() const;
        /** Acquires the guest addition's version. */
        bool acquireGuestAdditionsVersion(QString &strVersion);
        /** Acquires the guest addition's revision. */
        bool acquireGuestAdditionsRevision(ulong &uRevision);
        /** Notifies guest about VM window focus changes. */
        bool notifyGuiFocusChange(bool fInfocus);
    /** @} */

    /** @name Keyboard stuff.
     ** @{ */
        /** Returns the NUM lock status. */
        bool isNumLock() const { return m_fNumLock; }
        /** Returns the CAPS lock status. */
        bool isCapsLock() const { return m_fCapsLock; }
        /** Returns the SCROLL lock status. */
        bool isScrollLock() const { return m_fScrollLock; }

        /** Returns the NUM lock adaption count. */
        uint numLockAdaptionCnt() const { return m_uNumLockAdaptionCnt; }
        /** Defines the NUM lock adaption @a uCount. */
        void setNumLockAdaptionCnt(uint uCount) { m_uNumLockAdaptionCnt = uCount; }

        /** Returns the CAPS lock adaption count. */
        uint capsLockAdaptionCnt() const { return m_uCapsLockAdaptionCnt; }
        /** Defines the CAPS lock adaption @a uCount. */
        void setCapsLockAdaptionCnt(uint uCount) { m_uCapsLockAdaptionCnt = uCount; }

        /** Returns whether VM should perform HID LEDs synchronization. */
        bool isHidLedsSyncEnabled() const { return m_fIsHidLedsSyncEnabled; }

        /** Returns whether auto-capture is disabled. */
        bool isAutoCaptureDisabled() const { return m_fIsAutoCaptureDisabled; }
        /** Defines whether auto-capture is @a fDisabled. */
        void setAutoCaptureDisabled(bool fDisabled) { m_fIsAutoCaptureDisabled = fDisabled; }

        /** Returns the keyboard-state. */
        int keyboardState() const { return m_iKeyboardState; }

        /** Sends a scan @a iCode to VM's keyboard. */
        bool putScancode(LONG iCode);
        /** Sends a list of scan @a codes to VM's keyboard. */
        bool putScancodes(const QVector<LONG> &codes);
        /** Sends the CAD sequence to VM's keyboard. */
        bool putCAD();
        /** Releases all keys. */
        bool releaseKeys();
        /** Sends a USB HID @a iUsageCode and @a iUsagePage to VM's keyboard.
          * The @a fKeyRelease flag is set when the key is being released. */
        bool putUsageCode(LONG iUsageCode, LONG iUsagePage, bool fKeyRelease);
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Returns whether we should hide host mouse pointer. */
        bool isHidingHostPointer() const { return m_fIsHidingHostPointer; }
        /** Returns whether there is valid mouse pointer shape present. */
        bool isValidPointerShapePresent() const { return m_fIsValidPointerShapePresent; }
        /** Returns whether the @a cursorPosition() is valid and could be used by the GUI now. */
        bool isValidCursorPositionPresent() const { return m_fIsValidCursorPositionPresent; }

        /** Returns whether mouse supports absolute coordinates. */
        bool isMouseSupportsAbsolute() const { return m_fIsMouseSupportsAbsolute; }
        /** Returns whether mouse supports relative coordinates. */
        bool isMouseSupportsRelative() const { return m_fIsMouseSupportsRelative; }
        /** Returns whether touch screen is supported. */
        bool isMouseSupportsTouchScreen() const { return m_fIsMouseSupportsTouchScreen; }
        /** Returns whether touch pad is supported. */
        bool isMouseSupportsTouchPad() const { return m_fIsMouseSupportsTouchPad; }
        /** Returns whether guest requires host cursor to be shown. */
        bool isMouseHostCursorNeeded() const { return m_fIsMouseHostCursorNeeded; }

        /** Returns whether mouse is captured. */
        bool isMouseCaptured() const { return m_fIsMouseCaptured; }
        /** Returns whether mouse is integrated. */
        bool isMouseIntegrated() const { return m_fIsMouseIntegrated; }
        /** Defines whether mouse is @a fCaptured. */
        void setMouseCaptured(bool fCaptured) { m_fIsMouseCaptured = fCaptured; }
        /** Defines whether mouse is @a fIntegrated. */
        void setMouseIntegrated(bool fIntegrated) { m_fIsMouseIntegrated = fIntegrated; }

        /** Returns currently cached mouse cursor shape pixmap. */
        QPixmap cursorShapePixmap() const { return m_cursorShapePixmap; }
        /** Returns currently cached mouse cursor mask pixmap. */
        QPixmap cursorMaskPixmap() const { return m_cursorMaskPixmap; }
        /** Returns currently cached mouse cursor size. */
        QSize cursorSize() const { return m_cursorSize; }
        /** Returns currently cached mouse cursor hotspot. */
        QPoint cursorHotspot() const { return m_cursorHotspot; }
        /** Returns currently cached mouse cursor position. */
        QPoint cursorPosition() const { return m_cursorPosition; }

        /** Returns mouse-state. */
        int mouseState() const { return m_iMouseState; }

        /** Sends relative mouse move event to VM's mouse. */
        bool putMouseEvent(long iDx, long iDy, long iDz, long iDw, long iButtonState);
        /** Sends absolute mouse move event to VM's mouse. */
        bool putMouseEventAbsolute(long iX, long iY, long iDz, long iDw, long iButtonState);
        /** Sends multi-touch event to VM's mouse. */
        bool putEventMultiTouch(long iCount, const QVector<LONG64> &contacts, bool fIsTouchScreen, ulong uScanTime);

        /** Acquires clipboard mode. */
        bool acquireClipboardMode(KClipboardMode &enmMode);
        /** Defines clipboard mode. */
        bool setClipboardMode(KClipboardMode enmMode);
        /** En/disables guest clipboard file transfers. */
        bool toggleClipboardFileTransfer(bool fEnabled);
        /** Returns true if clipboard file transfer is enabled. Returns false otherwise or in case of an error. */
        bool isClipboardFileTransferEnabled();

        /** Acquires D&D mode. */
        bool acquireDnDMode(KDnDMode &enmMode);
        /** Defines D&D mode. */
        bool setDnDMode(KDnDMode enmMode);
    /** @} */

    /** @name Storage stuff.
     ** @{ */
        /** Enumerates amount of storage devices. */
        bool acquireAmountOfStorageDevices(ulong &cHardDisks, ulong &cOpticalDrives, ulong &cFloppyDrives);

        /** Returns a list of storage devices. */
        bool storageDevices(KDeviceType enmDeviceType, QList<StorageDeviceInfo> &guiStorageDevices);

        /** Acquires encrypted media map. */
        bool acquireEncryptedMedia(EncryptedMediumMap &media);
        /** Adds encryption password. */
        bool addEncryptionPassword(const QString &strId, const QString &strPassword, bool fClearOnSuspend);

        /** Calculates @a cAmount of immutable images. */
        bool acquireAmountOfImmutableImages(ulong &cAmount);

        /** Attempts to mount medium with @p uMediumId to the machine
          * if it can find an appropriate controller and port. */
        bool mountBootMedium(const QUuid &uMediumId);

        /** Prepares storage menu. */
        void prepareStorageMenu(QMenu *pMenu,
                                QObject *pListener, const char *pszSlotName,
                                const QString &strControllerName, const StorageSlot &storageSlot);
        /** Updates machine storage with data described by target. */
        void updateMachineStorage(const UIMediumTarget &target, UIActionPool *pActionPool);
    /** @} */

    /** @name USB stuff.
     ** @{ */
        /** Acquires whether USB controller is enabled. */
        void acquireWhetherUSBControllerEnabled(bool &fEnabled);
        /** Acquires whether video input devices are enabled. */
        void acquireWhetherVideoInputDevicesEnabled(bool &fEnabled);

        /** Returns a list of USB devices. */
        bool usbDevices(QList<USBDeviceInfo> &guiUSBDevices);
        /** Attaches USB device with passed @a uId. */
        bool attachUSBDevice(const QUuid &uId);
        /** Detaches USB device with passed @a uId. */
        bool detachUSBDevice(const QUuid &uId);

        /** Returns a list of web cam devices. */
        bool webcamDevices(QList<WebcamDeviceInfo> &guiWebcamDevices);
        /** Attaches web cam device with passed @a strName and @a strPath. */
        bool webcamAttach(const QString &strPath, const QString &strName);
        /** Detaches web cam device with passed @a strName and @a strPath. */
        bool webcamDetach(const QString &strPath, const QString &strName);
    /** @} */

    /** @name Network stuff.
     ** @{ */
        /** Acquires whether network adapter is enabled. */
        bool acquireWhetherNetworkAdapterEnabled(ulong uSlot, bool &fEnabled);
        /** Acquires whether at leasst one network adapter is enabled. */
        bool acquireWhetherAtLeastOneNetworkAdapterEnabled(bool &fEnabled);
        /** Acquires whether network adapter cable is connected. */
        bool acquireWhetherNetworkCableConnected(ulong uSlot, bool &fConnected);
        /** Set whether network adapter cable is connected. */
        bool setNetworkCableConnected(ulong uSlot, bool fConnected);
    /** @} */

    /** @name Virtualization stuff.
     ** @{ */
        /** Returns IMachineDebugger::ExecutionEngine reference. */
        KVMExecutionEngine vmExecutionEngine() const { return m_enmVMExecutionEngine; }

        /** Returns whether nested-paging CPU hardware virtualization extension is enabled. */
        bool isHWVirtExNestedPagingEnabled() const { return m_fIsHWVirtExNestedPagingEnabled; }
        /** Returns whether the VM is currently making use of the unrestricted execution feature of VT-x. */
        bool isHWVirtExUXEnabled() const { return m_fIsHWVirtExUXEnabled; }

        /** Returns VM's effective paravirtualization provider. */
        KParavirtProvider paravirtProvider() const { return m_enmParavirtProvider; }
    /** @} */

    /** @name Status-bar stuff.
     ** @{ */
        /** Acquires device activity composing a vector of current @a states for device with @a deviceTypes specified. */
        bool acquireDeviceActivity(const QVector<KDeviceType> &deviceTypes, QVector<KDeviceActivity> &states);

        /** Acquires status info for hard disk indicator. */
        void acquireHardDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount);
        /** Acquires status info for optical disk indicator. */
        void acquireOpticalDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount, uint &cAttachmentsMountedCount);
        /** Acquires status info for floppy disk indicator. */
        void acquireFloppyDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount, uint &cAttachmentsMountedCount);
        /** Acquires status info for audio indicator. */
        void acquireAudioStatusInfo(QString &strInfo, bool &fAudioEnabled, bool &fEnabledOutput, bool &fEnabledInput);
        /** Acquires status info for network indicator. */
        void acquireNetworkStatusInfo(QString &strInfo, bool &fAdaptersPresent, bool &fCablesDisconnected);
        /** Acquires status info for USB indicator. */
        void acquireUsbStatusInfo(QString &strInfo, bool &fUsbEnabled, uint &cUsbFilterCount);
        /** Acquires status info for Shared Folders indicator. */
        void acquireSharedFoldersStatusInfo(QString &strInfo, uint &cFoldersCount);
        /** Acquires status info for Display indicator. */
        void acquireDisplayStatusInfo(QString &strInfo, uint &uVRAMSize, uint &cMonitorCount, bool &fAcceleration3D);
        /** Acquires status info for Recording indicator. */
        void acquireRecordingStatusInfo(QString &strInfo, bool &fRecordingEnabled, bool &fMachinePaused);
        /** Acquires status info for Features indicator. */
        void acquireFeaturesStatusInfo(QString &strInfo, KVMExecutionEngine &enmEngine);
    /** @} */

    /** @name Debugger stuff.
     ** @{ */
        /** Defines whether log is @a fEnabled. */
        bool setLogEnabled(bool fEnabled);
        /** Acquires whether log is @a fEnabled. */
        bool acquireWhetherLogEnabled(bool &fEnabled);

        /** Acquire log folder. */
        bool acquireLogFolder(QString &strFolder);

        /** Acquires effective CPU @a uLoad. */
        bool acquireEffectiveCPULoad(ulong &uLoad);
        /** Acquires uptime @a iUpTime as milliseconds. */
        bool acquireUptime(LONG64 &iUpTime);

#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Makes sure debugger GUI is created. */
        bool dbgCreated(void *pActionDebug);
        /** Makes sure debugger GUI is destroyed. */
        void dbgDestroy();

        /** Shows debugger UI statistics window. */
        void dbgShowStatistics();
        /** Shows debugger UI command line window. */
        void dbgShowCommandLine();

        /** Adjusts relative position for debugger window. */
        void dbgAdjustRelativePos();
#endif /* VBOX_WITH_DEBUGGER_GUI */
    /** @} */

    /** @name Close stuff.
     ** @{ */
        /** Returns whether VM is in 'manual-override' mode.
          * @note S.a. #m_fIsManualOverride description for more information. */
        bool isManualOverrideMode() const { return m_fIsManualOverride; }
        /** Defines whether VM is in 'manual-override' mode.
          * @note S.a. #m_fIsManualOverride description for more information. */
        void setManualOverrideMode(bool fIsManualOverride) { m_fIsManualOverride = fIsManualOverride; }

        /** Returns default close action. */
        MachineCloseAction defaultCloseAction() const { return m_defaultCloseAction; }
        /** Returns merged restricted close actions. */
        MachineCloseAction restrictedCloseActions() const { return m_restrictedCloseActions; }

        /** Acquires whether guest @a fEntered ACPI mode. */
        bool acquireWhetherGuestEnteredACPIMode(bool &fEntered);

        /** Returns whether qpp quit is requested. */
        bool isQuitRequested() const { return m_fQuitRequested; }

        /** Detaches and closes Runtime UI. */
        void detachUi();
        /** Saves VM state, then closes Runtime UI. */
        void saveState();
        /** Calls for guest shutdown to close Runtime UI. */
        void shutdown();
        /** Powers VM off, then closes Runtime UI. */
        void powerOff(bool fIncludingDiscard);
    /** @} */

    /** @name VM information stuff.
     ** @{ */
        /** Return general info. of the machine(). */
        void generateMachineInformationGeneral(const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions,
                                               UITextTable &returnTable);
        /** Return system info. of the machine(). */
        void generateMachineInformationSystem(const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &fOptions,
                                              UITextTable &returnTable);
        /** Returns system info. of the machine(). */
        void generateMachineInformationDisplay(const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &fOptions,
                                               UITextTable &returnTable);
        /** Returns storage info. of the machine(). */
        void generateMachineInformationStorage(const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &fOptions,
                                               UITextTable &returnTable);
        /** Returns audio info. of the machine(). */
        void generateMachineInformationAudio(const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &fOptions,
                                             UITextTable &returnTable);
        /** Returns network info. of the machine(). */
        void generateMachineInformationNetwork(const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &fOptions,
                                               UITextTable &returnTable);
        /** Returns serial info. of the machine(). */
        void generateMachineInformationSerial(const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &fOptions,
                                              UITextTable &returnTable);
        /** Returns USB info. of the machine(). */
        void generateMachineInformationUSB(const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &fOptions,
                                           UITextTable &returnTable);
        /** Returns shared folders info. of the machine(). */
        void generateMachineInformationSharedFolders(const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &fOptions,
                                                     UITextTable &returnTable);
    /** @} */

public slots:

    /** @name Guest additions stuff.
     ** @{ */
        /** Handles request to install guest additions image.
          * @param  strSource  Brings the source of image being installed. */
        void sltInstallGuestAdditionsFrom(const QString &strSource);
        /** Mounts DVD adhoc.
          * @param  strSource  Brings the source of image being mounted. */
        void sltMountDVDAdHoc(const QString &strSource);
    /** @} */

    /** @name Keyboard stuff.
     ** @{ */
        /** Defines @a iKeyboardState. */
        void setKeyboardState(int iKeyboardState) { m_iKeyboardState = iKeyboardState; emit sigKeyboardStateChange(m_iKeyboardState); }
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Defines @a iMouseState. */
        void setMouseState(int iMouseState) { m_iMouseState = iMouseState; emit sigMouseStateChange(m_iMouseState); }
    /** @} */

    /** @name Close stuff.
     ** @{ */
        /** Closes Runtime UI. */
        void closeRuntimeUI();
    /** @} */

private slots:

    /** Visual state-change handler. */
    void sltChangeVisualState(UIVisualStateType enmVisualStateType);

    /** @name COM events stuff.
     ** @{ */
        /** Handles additions state actual change signal. */
        void sltHandleAdditionsActualChange();
        /** Handles audio adapter change signal. */
        void sltHandleAudioAdapterChange();
        /** Handles recording change signal. */
        void sltHandleRecordingChange();
        /** Handles storage device change for @a attachment, which was @a fRemoved and it was @a fSilent for guest. */
        void sltHandleStorageDeviceChange(const CMediumAttachment &comAttachment, bool fRemoved, bool fSilent);
        /** Handles VRDE change signal. */
        void sltHandleVRDEChange();
    /** @} */

    /** @name Actions stuff.
     ** @{ */
#ifdef VBOX_WS_MAC
        /** macOS X: Handles menu-bar configuration-change. */
        void sltHandleMenuBarConfigurationChange(const QUuid &uMachineID);
#endif
    /** @} */

    /** @name Host-screen stuff.
     ** @{ */
        /** Handles host-screen count change. */
        void sltHandleHostScreenCountChange();
        /** Handles host-screen geometry change. */
        void sltHandleHostScreenGeometryChange();
        /** Handles host-screen available-area change. */
        void sltHandleHostScreenAvailableAreaChange();

#ifdef VBOX_WS_MAC
        /** macOS X: Restarts display-reconfiguration watchdog timer from the beginning.
          * @note Watchdog is trying to determine display reconfiguration in
          *       UISession::sltCheckIfHostDisplayChanged() slot every 500ms for 40 tries. */
        void sltHandleHostDisplayAboutToChange();
        /** MacOS X: Determines display reconfiguration.
          * @note Calls for UISession::sltHandleHostScreenCountChange() if screen count changed.
          * @note Calls for UISession::sltHandleHostScreenGeometryChange() if screen geometry changed. */
        void sltCheckIfHostDisplayChanged();
#endif /* VBOX_WS_MAC */
    /** @} */

    /** @name Guest-screen stuff.
     ** @{ */
        /** Handles guest-monitor state change. */
        void sltHandleGuestMonitorChange(KGuestMonitorChangedEventType enmChangeType, ulong uScreenId, QRect screenGeo);
    /** @} */

    /** @name Keyboard stuff.
     ** @{ */
        /** Handles signal about keyboard LEDs change.
          * @param  fNumLock     Brings NUM lock status.
          * @param  fCapsLock    Brings CAPS lock status.
          * @param  fScrollLock  Brings SCROLL lock status. */
        void sltHandleKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock);

        /** Handles signal about keyboard LEDs sync state change.
          * @param  fEnabled  Brings sync status. */
        void sltHidLedsSyncStateChanged(bool fEnabled) { m_fIsHidLedsSyncEnabled = fEnabled; }
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Handles signal about mouse pointer shape data change.
          * @param  shapeData  Brings complex struct describing mouse pointer shape aspects. */
        void sltMousePointerShapeChange(const UIMousePointerShapeData &shapeData);
        /** Handles signal about mouse capability change.
          * @param  fSupportsAbsolute     Brings whether mouse supports absolute coordinates.
          * @param  fSupportsRelative     Brings whether mouse supports relative coordinates.
          * @param  fSupportsTouchScreen  Brings whether touch screen is supported.
          * @param  fSupportsTouchPad     Brings whether touch pad is supported.
          * @param  fNeedsHostCursor      Brings whether guest requires host cursor to be shown. */
        void sltMouseCapabilityChange(bool fSupportsAbsolute, bool fSupportsRelative,
                                      bool fSupportsTouchScreen, bool fSupportsTouchPad,
                                      bool fNeedsHostCursor);
        /** Handles signal about guest request to change the cursor position to @a uX * @a uY.
          * @param  fContainsData  Brings whether the @a uX and @a uY values are valid and could be used by the GUI now.
          * @param  uX             Brings cursor position X origin.
          * @param  uY             Brings cursor position Y origin. */
        void sltCursorPositionChange(bool fContainsData,
                                     unsigned long uX,
                                     unsigned long uY);
    /** @} */

private:

    /** Constructs machine UI singleton. */
    UIMachine();
    /** Destructs machine UI singleton. */
    virtual ~UIMachine() RT_OVERRIDE RT_FINAL;

    /** Prepare routine. */
    bool prepare();
    /** Prepares notification-center. */
    void prepareNotificationCenter();
    /** Prepare routine: Session stuff. */
    bool prepareSession();
    /** Prepare routine: Actions stuff. */
    void prepareActions();
    /** Prepare routine: Host-screen data stuff. */
    void prepareHostScreenData();
    /** Prepare routine: Keyboard stuff. */
    void prepareKeyboard();
    /** Prepare routine: Close stuff. */
    void prepareClose();
    /** Prepare routine: Visual-state stuff. */
    void prepareVisualState();

    /** Update routine: Branding. */
    void updateBranding();
    /** Update routine: Guest screen data stuff. */
    void updateGuestScreenData();

    /** Moves VM to initial state. */
    void enterInitialVisualState();

    /** Cleanup routine: Machine-logic stuff. */
    void cleanupMachineLogic();
    /** Cleanup routine: Branding. */
    void cleanupBranding();
    /** Cleanup routine: Host-screen data stuff. */
    void cleanupHostScreenData();
    /** Cleanup routine: Actions stuff. */
    void cleanupActions();
    /** Cleanup routine: Session stuff. */
    void cleanupSession();
    /** Cleanups notification-center. */
    void cleanupNotificationCenter();
    /** Cleanup routine. */
    void cleanup();

    /** @name Actions stuff.
     ** @{ */
        /** Updates action restrictions. */
        void updateActionRestrictions();

#ifdef VBOX_WS_MAC
        /** macOS X: Updates menu-bar content. */
        void updateMenu();
#endif
    /** @} */

    /** @name Host-screen stuff.
     ** @{ */
        /** Update host-screen data. */
        void updateHostScreenData();
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Updates mouse pointer shape. */
        void updateMousePointerShape();

        /** Updates mouse states. */
        void updateMouseState();

#if defined(VBOX_WS_NIX) || defined(VBOX_WS_MAC)
        /** Generate a BGRA bitmap which approximates a XOR/AND mouse pointer.
          *
          * Pixels which has 1 in the AND mask and not 0 in the XOR mask are replaced by
          * the inverted pixel and 8 surrounding pixels with the original color.
          * Fort example a white pixel (W) is replaced with a black (B) pixel:
          *         WWW
          *  W   -> WBW
          *         WWW
          * The surrounding pixels are written only if the corresponding source pixel
          * does not affect the screen, i.e. AND bit is 1 and XOR value is 0. */
        static void renderCursorPixels(const uint32_t *pu32XOR, const uint8_t *pu8AND,
                                       uint32_t u32Width, uint32_t u32Height,
                                       uint32_t *pu32Pixels, uint32_t cbPixels);
#endif /* VBOX_WS_NIX || VBOX_WS_MAC */

#ifdef VBOX_WS_WIN
        /** Windows: Returns whether pointer of 1bpp depth. */
        static bool isPointer1bpp(const uint8_t *pu8XorMask,
                                  uint uWidth,
                                  uint uHeight);
#endif /* VBOX_WS_WIN */
    /** @} */

    /** @name Virtualization stuff.
     ** @{ */
        /** Updates virtualization state. */
        void updateVirtualizationState();
    /** @} */

    /** Holds the static instance. */
    static UIMachine *s_pInstance;

    /** Holds whether machine is initialized. */
    bool  m_fInitialized;

    /** Holds the session UI instance. */
    UISession *m_pSession;

    /** Holds allowed visual states. */
    UIVisualStateType m_enmAllowedVisualStates;
    /** Holds initial visual state. */
    UIVisualStateType m_enmInitialVisualState;
    /** Holds current visual state. */
    UIVisualStateType m_enmVisualState;
    /** Holds visual state which should be entered when possible. */
    UIVisualStateType m_enmRequestedVisualState;
    /** Holds current machine-logic. */
    UIMachineLogic *m_pMachineLogic;

    /** @name Branding stuff.
     ** @{ */
        /** Holds the cached machine-window icon. */
        QIcon *m_pMachineWindowIcon;

#ifndef VBOX_WS_MAC
        /** Holds redefined machine-window name postfix. */
        QString m_strMachineWindowNamePostfix;
#endif
    /** @} */

    /** @name Actions stuff.
     ** @{ */
        /** Holds the action-pool instance. */
        UIActionPool *m_pActionPool;

#ifdef VBOX_WS_MAC
        /** macOS X: Holds the menu-bar instance. */
        QMenuBar *m_pMenuBar;
#endif
    /** @} */

    /** @name Host-screen stuff.
     ** @{ */
        /** Holds the list of host-screen geometries we currently have. */
        QList<QRect>  m_hostScreens;

#ifdef VBOX_WS_MAC
        /** macOS X: Watchdog timer looking for display reconfiguration. */
        QTimer *m_pWatchdogDisplayChange;
#endif
    /** @} */

    /** @name Guest-screen stuff.
     ** @{ */
        /** Holds the list of desired guest-screen visibility flags. */
        QVector<bool>  m_guestScreenVisibilityVectorHostDesires;
        /** Holds the list of actual guest-screen visibility flags. */
        QVector<bool>  m_guestScreenVisibilityVector;

        /** Holds the list of guest-screen full-screen sizes. */
        QVector<QSize>  m_monitorLastFullScreenSizeVector;

        /** Holds whether guest screen resize should be ignored. */
        bool m_fIsGuestResizeIgnored;
    /** @} */

    /** @name Keyboard stuff.
     ** @{ */
        /** Holds the NUM lock status. */
        bool  m_fNumLock;
        /** Holds the CAPS lock status. */
        bool  m_fCapsLock;
        /** Holds the SCROLL lock status. */
        bool  m_fScrollLock;

        /** Holds the NUM lock adaption count. */
        uint  m_uNumLockAdaptionCnt;
        /** Holds the CAPS lock adaption count. */
        uint  m_uCapsLockAdaptionCnt;

        /** Holds whether VM should perform HID LEDs synchronization. */
        bool m_fIsHidLedsSyncEnabled;

        /** Holds whether auto-capture is disabled. */
        bool m_fIsAutoCaptureDisabled;

        /** Holds the keyboard-state. */
        int  m_iKeyboardState;
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Holds whether we should hide host mouse pointer. */
        bool  m_fIsHidingHostPointer;
        /** Holds whether there is valid mouse pointer shape present. */
        bool  m_fIsValidPointerShapePresent;
        /** Holds whether the @a m_cursorPosition is valid and could be used by the GUI now. */
        bool  m_fIsValidCursorPositionPresent;

        /** Holds whether mouse supports absolute coordinates. */
        bool  m_fIsMouseSupportsAbsolute;
        /** Holds whether mouse supports relative coordinates. */
        bool  m_fIsMouseSupportsRelative;
        /** Holds whether touch screen is supported. */
        bool  m_fIsMouseSupportsTouchScreen;
        /** Holds whether touch pad is supported. */
        bool  m_fIsMouseSupportsTouchPad;
        /** Holds whether guest requires host cursor to be shown. */
        bool  m_fIsMouseHostCursorNeeded;

        /** Holds whether mouse is captured. */
        bool  m_fIsMouseCaptured;
        /** Holds whether mouse is integrated. */
        bool  m_fIsMouseIntegrated;

        /** Holds the mouse pointer shape data. */
        UIMousePointerShapeData  m_shapeData;

        /** Holds cached mouse cursor shape pixmap. */
        QPixmap  m_cursorShapePixmap;
        /** Holds cached mouse cursor mask pixmap. */
        QPixmap  m_cursorMaskPixmap;
        /** Holds cached mouse cursor size. */
        QSize    m_cursorSize;
        /** Holds cached mouse cursor hotspot. */
        QPoint   m_cursorHotspot;
        /** Holds cached mouse cursor position. */
        QPoint   m_cursorPosition;

        /** Holds the mouse-state. */
        int  m_iMouseState;
    /** @} */

    /** @name Virtualization stuff.
     ** @{ */
        /** Holds the IMachineDebugger::ExecutionEngine reference. */
        KVMExecutionEngine  m_enmVMExecutionEngine;

        /** Holds whether nested-paging CPU hardware virtualization extension is enabled. */
        bool  m_fIsHWVirtExNestedPagingEnabled;
        /** Holds whether the VM is currently making use of the unrestricted execution feature of VT-x. */
        bool  m_fIsHWVirtExUXEnabled;

        /** Holds the VM's effective paravirtualization provider. */
        KParavirtProvider  m_enmParavirtProvider;
    /** @} */

    /** @name Close stuff.
     ** @{ */
        /** Holds whether VM is in 'manual-override' mode
          * which means there will be no automatic UI shutdowns,
          * visual representation mode changes and other stuff. */
        bool  m_fIsManualOverride : 1;

        /** Default close action. */
        MachineCloseAction  m_defaultCloseAction;
        /** Merged restricted close actions. */
        MachineCloseAction  m_restrictedCloseActions;

        /** Holds whether app quit is requested. */
        bool  m_fQuitRequested;
    /** @} */
};

#define gpMachine UIMachine::instance()

#endif /* !FEQT_INCLUDED_SRC_runtime_UIMachine_h */
