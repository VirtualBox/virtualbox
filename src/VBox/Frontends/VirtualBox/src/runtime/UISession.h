/* $Id: UISession.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISession class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UISession_h
#define FEQT_INCLUDED_SRC_runtime_UISession_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QCursor>
#include <QEvent>
#include <QMap>
#include <QObject>

/* GUI includes: */
#include "UIAddDiskEncryptionPasswordDialog.h"
#include "UIExtraDataDefs.h"
#include "UIMachineDefs.h"
#include "UIMediumDefs.h"
#include "UIMousePointerShapeData.h"
#include "UITextTable.h"

/* COM includes: */
#include "CConsole.h"
#include "CDisplay.h"
#include "CGuest.h"
#include "CKeyboard.h"
#include "CMachine.h"
#include "CMachineDebugger.h"
#include "CMouse.h"
#include "CSession.h"
#include "KChipsetType.h"
#include "KGraphicsControllerType.h"
#include "KGuestMonitorChangedEventType.h"
#include "KPlatformArchitecture.h"

/* Forward declarations: */
class QMenu;
class UIConsoleEventHandler;
class UIFrameBuffer;
class UIMachine;
class UIMachineLogic;
class UIMachineWindow;
class UIActionPool;
class CUSBDevice;
class CNetworkAdapter;
class CMediumAttachment;

#ifdef VBOX_WITH_DEBUGGER_GUI
typedef struct DBGGUIVT const *PCDBGGUIVT;
typedef struct DBGGUI *PDBGGUI;
#endif /* VBOX_WITH_DEBUGGER_GUI */

/** QObject subclass implementing
  * COM related functionality for Runtime UI. */
class UISession : public QObject
{
    Q_OBJECT;

signals:

    /** @name COM events stuff.
     ** @{ */
        /** Notifies about additions state change. */
        void sigAdditionsStateChange();
        /** Notifies about additions state actually change. */
        void sigAdditionsStateActualChange();
        /** Notifies about additions state actually change. */
        void sigAudioAdapterChange();
        /** Notifies about clipboard mode change. */
        void sigClipboardModeChange(KClipboardMode enmMode);
        /** Notifies about CPU execution cap change. */
        void sigCPUExecutionCapChange();
        /** Notifies about DnD mode change. */
        void sigDnDModeChange(KDnDMode enmMode);
        /** Notifies about guest monitor change. */
        void sigGuestMonitorChange(KGuestMonitorChangedEventType enmChangeType, ulong uScreenId, QRect screenGeo);
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
        /** Notifies about storage device change for @a attachment, which was @a fRemoved and it was @a fSilent for guest. */
        void sigStorageDeviceChange(const CMediumAttachment &comAttachment, bool fRemoved, bool fSilent);
        /** Handles USB controller change signal. */
        void sigUSBControllerChange();
        /** Handles USB device state change signal. */
        void sigUSBDeviceStateChange(const CUSBDevice &comDevice, bool fAttached, const CVirtualBoxErrorInfo &comError);
        /** Notifies about VRDE change. */
        void sigVRDEChange();

        /** Notifies about runtime error happened. */
        void sigRuntimeError(bool fFatal, const QString &strErrorId, const QString &strMessage);

#ifdef VBOX_WS_MAC
        /** Notifies about VM window should be shown. */
        void sigShowWindows();
#endif
    /** @} */

    /** @name Keyboard stuff.
     ** @{ */
        /** Notifies about keyboard LEDs change. */
        void sigKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock);
    /** @} */

    /** @name Mouse stuff.
     ** @{ */
        /** Notifies listeners about mouse pointer shape change. */
        void sigMousePointerShapeChange(const UIMousePointerShapeData &shapeData);
        /** Notifies listeners about mouse capability change. */
        void sigMouseCapabilityChange(bool fSupportsAbsolute, bool fSupportsRelative,
                                      bool fSupportsTouchScreen, bool fSupportsTouchPad,
                                      bool fNeedsHostCursor);
        /** Notifies listeners about cursor position change. */
        void sigCursorPositionChange(bool fContainsData, unsigned long uX, unsigned long uY);
    /** @} */

public:

    /** Constructs session UI passing @a pMachine to the base-class.
      * @param  pMachine  Brings the machine UI reference. */
    UISession(UIMachine *pMachine);

    /** @name General stuff.
     ** @{ */
        /** Prepares everything. */
        bool prepare();
        /** Performs session UI intialization. */
        bool initialize();

        /** Returns whether session UI is valid. */
        bool isValid() const { return m_fValid; }

        /** Powers VM up. */
        bool powerUp();
    /** @} */

    /** @name COM stuff.
     ** @{ */
        /** Returns the session instance. */
        CSession &session() { return m_comSession; }
        /** Returns the session's machine instance. */
        CMachine &machine() { return m_comMachine; }
        /** Returns the session's console instance. */
        CConsole &console() { return m_comConsole; }
        /** Returns the console's display instance. */
        CDisplay &display() { return m_comDisplay; }
        /** Returns the console's guest instance. */
        CGuest &guest() { return m_comGuest; }
        /** Returns the console's mouse instance. */
        CMouse &mouse() { return m_comMouse; }
        /** Returns the console's keyboard instance. */
        CKeyboard &keyboard() { return m_comKeyboard; }
        /** Returns the console's debugger instance. */
        CMachineDebugger &debugger() { return m_comDebugger; }
    /** @} */

    /** @name General stuff.
     ** @{ */
        /** Returns the machine name. */
        QString machineName() const { return m_strMachineName; }
        /** Returns the OS type id. */
        QString osTypeId() const { return m_strOSTypeId; }

        /** Returns main machine-widget id. */
        WId mainMachineWindowId() const;

        /** Acquire machine pixmap. */
        void acquireMachinePixmap(const QSize &size, QPixmap &pixmap);
        /** Acquire user machine icon. */
        void acquireUserMachineIcon(QIcon &icon);

        /** Acquires architecture type. */
        bool acquireArchitectureType(KPlatformArchitecture &enmType);
        /** Acquires chipset type. */
        bool acquireChipsetType(KChipsetType &enmType);
    /** @} */

    /** @name Machine-state stuff.
     ** @{ */
        /** Returns previous machine state. */
        KMachineState machineStatePrevious() const { return m_enmMachineStatePrevious; }
        /** Returns cached machine state. */
        KMachineState machineState() const { return m_enmMachineState; }

        /** Resets previous state to be the same as current one. */
        void forgetPreviousMachineState() { m_enmMachineStatePrevious = m_enmMachineState; }

        /** Acquire live machine state. */
        bool acquireLiveMachineState(KMachineState &enmState);

        /** Returns whether VM is in one of saved states. */
        bool isSaved() const { return    machineState() == KMachineState_Saved
                                      || machineState() == KMachineState_AbortedSaved; }
        /** Returns whether VM is in one of turned off states. */
        bool isTurnedOff() const { return    machineState() == KMachineState_PoweredOff
                                          || machineState() == KMachineState_Saved
                                          || machineState() == KMachineState_Teleported
                                          || machineState() == KMachineState_Aborted
                                          || machineState() == KMachineState_AbortedSaved; }
        /** Returns whether VM is in one of paused states. */
        bool isPaused() const { return    machineState() == KMachineState_Paused
                                       || machineState() == KMachineState_TeleportingPausedVM; }
        /** Returns whether VM was in one of paused states. */
        bool wasPaused() const { return    machineStatePrevious() == KMachineState_Paused
                                        || machineStatePrevious() == KMachineState_TeleportingPausedVM; }
        /** Returns whether VM is in one of running states. */
        bool isRunning() const { return    machineState() == KMachineState_Running
                                        || machineState() == KMachineState_Teleporting
                                        || machineState() == KMachineState_LiveSnapshotting; }
        /** Returns whether VM is in one of stuck states. */
        bool isStuck() const { return machineState() == KMachineState_Stuck; }
        /** Returns whether VM is one of states where guest-screen is undrawable. */
        bool isGuestScreenUnDrawable() const { return    machineState() == KMachineState_Stopping
                                                      || machineState() == KMachineState_Saving; }

        /** Resets VM. */
        bool reset();
        /** Performes VM pausing. */
        bool pause() { return setPause(true); }
        /** Performes VM resuming. */
        bool unpause() { return setPause(false); }
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

    /** @name Keyboard stuff.
     ** @{ */
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
        /** Returns whether VM's mouse supports absolute coordinates. */
        bool acquireWhetherAbsoluteSupported(bool &fSupported);
        /** Returns whether VM's mouse supports relative coordinates. */
        bool acquireWhetherRelativeSupported(bool &fSupported);
        /** Returns whether VM's mouse supports touch screen device. */
        bool acquireWhetherTouchScreenSupported(bool &fSupported);
        /** Returns whether VM's mouse supports touch pad device. */
        bool acquireWhetherTouchPadSupported(bool &fSupported);
        /** Returns whether VM's mouse requires host cursor. */
        bool acquireWhetherNeedsHostCursor(bool &fNeeds);

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

    /** @name Guest additions stuff.
     ** @{ */
        /** Returns whether guest additions is active. */
        bool isGuestAdditionsActive() const { return (m_ulGuestAdditionsRunLevel > KAdditionsRunLevelType_None); }
        /** Returns whether guest additions supports graphics. */
        bool isGuestSupportsGraphics() const { return m_fIsGuestSupportsGraphics; }
        /** Returns whether guest additions supports seamless.
          * @note The double check below is correct, even though it is an implementation
          *       detail of the Additions which the GUI should not ideally have to know. */
        bool isGuestSupportsSeamless() const { return isGuestSupportsGraphics() && m_fIsGuestSupportsSeamless; }
        /** Returns whether GA can be upgraded. */
        bool guestAdditionsUpgradable();
        /** Acquires the guest addition's version. */
        bool acquireGuestAdditionsVersion(QString &strVersion);
        /** Acquires the guest addition's revision. */
        bool acquireGuestAdditionsRevision(ulong &uRevision);
        /** Notifies GAs about VM window focus changes. */
        bool notifyGuiFocusChange(bool fInfocus);
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

    /** @name Graphics stuff.
     ** @{ */
        /** Returns existing framebuffer for the screen with given @a uScreenId;
          * @returns 0 (asserts) if uScreenId attribute is out of bounds. */
        UIFrameBuffer *frameBuffer(ulong uScreenId) const;
        /** Returns existing frame-buffer vector. */
        const QVector<UIFrameBuffer*> &frameBuffers() const { return m_frameBufferVector; }
        /** Returns frame-buffer size for screen with index @a uScreenId. */
        QSize frameBufferSize(ulong uScreenId) const;

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
        void acquireFeaturesStatusInfo(QString &strInfo, KVMExecutionEngine &enmEngine,
                                       bool fNestedPagingEnabled, bool fUxEnabled,
                                       KParavirtProvider enmProvider);
    /** @} */

    /** @name Debugger stuff.
     ** @{ */
        /** Defines whether log is @a fEnabled. */
        bool setLogEnabled(bool fEnabled);
        /** Acquires whether log is @a fEnabled. */
        bool acquireWhetherLogEnabled(bool &fEnabled);

        /** Acquire log folder. */
        bool acquireLogFolder(QString &strFolder);

        /** Acquires effective paravirt provider. */
        bool acquireEffectiveParavirtProvider(KParavirtProvider &enmProvider);

        /** Acquires VM's execution engine @a enmType. */
        bool acquireExecutionEngineType(KVMExecutionEngine &enmType);
        /** Acquires whether nested paging hardware virtualization extension is @a fEnabled. */
        bool acquireWhetherHwVirtExNestedPagingEnabled(bool &fEnabled);
        /** Acquires whether UX hardware virtualization extension is @a fEnabled. */
        bool acquireWhetherHwVirtExUXEnabled(bool &fEnabled);

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
        /** Acquires whether guest @a fEntered ACPI mode. */
        bool acquireWhetherGuestEnteredACPIMode(bool &fEntered);

        /** Saves VM state, then closes Runtime UI. */
        void saveState();
        /** Calls for guest shutdown to close Runtime UI. */
        void shutdown();
        /** Powers VM off, then closes Runtime UI. */
        void powerOff(bool fIncludingDiscard);
    /** @} */

    /** @name VM information stuff.
     ** @{ */
        /** Returns general info. of the machine(). */
        void generateMachineInformationGeneral(const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions,
                                               UITextTable &returnTable);
        /** Returns system info. of the machine(). */
        void generateMachineInformationSystem(const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &fOptions,
                                              UITextTable &returnTable);
        /** Returns display info. of the machine(). */
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

private slots:

    /** @name COM stuff.
     ** @{ */
        /** Detaches COM. */
        void sltDetachCOM();
    /** @} */

    /** @name Machine-state stuff.
     ** @{ */
        /** Handles event about VM @a enmState change. */
        void sltStateChange(KMachineState enmState);
    /** @} */

    /** @name Guest additions stuff.
     ** @{ */
        /** Handles event about guest additions change. */
        void sltAdditionsChange();
    /** @} */

    /** @name Clipboard stuff.
     ** @{ */
        /** Handles clipboard errors. */
        void sltClipboardError(const QString &strMsg);
    /** @} */

    /** @name Close stuff.
     ** @{ */
        /** Handles signal about machine state saved.
          * @param  fSuccess  Brings whether state was saved successfully. */
        void sltHandleMachineStateSaved(bool fSuccess);
        /** Handles signal about machine powered off.
          * @param  fSuccess           Brings whether machine was powered off successfully.
          * @param  fIncludingDiscard  Brings whether machine state should be discarded. */
        void sltHandleMachinePoweredOff(bool fSuccess, bool fIncludingDiscard);
        /** Handles signal about snapshot restored.
          * @param  fSuccess  Brings whether machine was powered off successfully. */
        void sltHandleSnapshotRestored(bool fSuccess);
    /** @} */

private:

    /** @name Prepare/cleanup cascade.
     ** @{ */
        /** Prepares COM stuff. */
        bool prepareCOMStuff();
        /** Prepares console event-handler. */
        void prepareConsoleEventHandlers();
        /** Prepares frame-buffers. */
        void prepareFramebuffers();
        /** Prepares connections. */
        void prepareConnections();
        /** Prepares signal handling. */
        void prepareSignalHandling();

        /** Cleanups frame-buffers. */
        void cleanupFramebuffers();
        /** Cleanups console event-handler. */
        void cleanupConsoleEventHandlers();
        /** Cleanups COM stuff. */
        void cleanupCOMStuff();
    /** @} */

    /** @name General stuff.
     ** @{ */
        /** Returns the machine UI reference. */
        UIMachine *uimachine() const { return m_pMachine; }
        /** Returns the machine-logic reference. */
        UIMachineLogic *machineLogic() const;
        /** Returns main machine-window reference. */
        UIMachineWindow *activeMachineWindow() const;
        /** Returns main machine-widget reference. */
        QWidget *mainMachineWindow() const;

        /** Preprocess initialization. */
        bool preprocessInitialization();

        /** Mounts medium adhoc.
          * @param  enmDeviceType  Brings device type.
          * @param  enmMediumType  Brings medium type.
          * @param  strMediumName  Brings medium name. */
        bool mountAdHocImage(KDeviceType enmDeviceType, UIMediumDeviceType enmMediumType, const QString &strMediumName);

        /** Recaches media attached to the machine. */
        void recacheMachineMedia();
    /** @} */

    /** @name Snapshot stuff.
     ** @{ */
        /** Recursively searches for a first snapshot matching name template conditions. */
        static bool searchMaxSnapshotIndex(const CMachine &machine, const CSnapshot &snapshot,
                                           const QString &strNameTemplate, ulong &uIndex);
    /** @} */

    /** @name General stuff.
     ** @{ */
        /** Holds the machine UI reference. */
        UIMachine *m_pMachine;

        /** Holds whether session UI is valid. */
        bool  m_fValid;

        /** Holds the machine name. */
        QString  m_strMachineName;
        /** Holds the OS type id. */
        QString  m_strOSTypeId;
    /** @} */

    /** @name COM stuff.
     ** @{ */
        /** Holds the CConsole event handler instance. */
        UIConsoleEventHandler *m_pConsoleEventhandler;

        /** Holds the session instance. */
        CSession          m_comSession;
        /** Holds the session's machine instance. */
        CMachine          m_comMachine;
        /** Holds the session's console instance. */
        CConsole          m_comConsole;
        /** Holds the console's display instance. */
        CDisplay          m_comDisplay;
        /** Holds the console's guest instance. */
        CGuest            m_comGuest;
        /** Holds the console's mouse instance. */
        CMouse            m_comMouse;
        /** Holds the console's keyboard instance. */
        CKeyboard         m_comKeyboard;
        /** Holds the console's debugger instance. */
        CMachineDebugger  m_comDebugger;
    /** @} */

    /** @name Machine-state stuff.
     ** @{ */
        /** Holds the previous machine state. */
        KMachineState  m_enmMachineStatePrevious;
        /** Holds the actual machine state. */
        KMachineState  m_enmMachineState;
    /** @} */

    /** @name Guest additions stuff.
     ** @{ */
        /** Holds the guest-additions run level. */
        ulong  m_ulGuestAdditionsRunLevel;
        /** Holds whether guest-additions supports graphics. */
        bool   m_fIsGuestSupportsGraphics;
        /** Holds whether guest-additions supports seamless. */
        bool   m_fIsGuestSupportsSeamless;
    /** @} */

    /** @name Machine-state stuff.
     ** @{ */
        /** Holds the frame-buffer vector. */
        QVector<UIFrameBuffer*>  m_frameBufferVector;
    /** @} */

    /** @name Debugger stuff.
     ** @{ */
#ifdef VBOX_WITH_DEBUGGER_GUI
        /** Holds the handle to the debugger GUI. */
        PDBGGUI     m_pDbgGui;
        /** Holds the virtual method table for the debugger GUI. */
        PCDBGGUIVT  m_pDbgGuiVT;
#endif /* VBOX_WITH_DEBUGGER_GUI */
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UISession_h */
