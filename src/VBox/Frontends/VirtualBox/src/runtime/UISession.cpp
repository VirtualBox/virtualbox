/* $Id: UISession.cpp 106832 2024-11-01 12:16:23Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UISession class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QRegularExpression>
#include <QWidget>

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIConsoleEventHandler.h"
#include "UIConverter.h"
#include "UIDetailsGenerator.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIGuestOSType.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineView.h"
#include "UIMachineWindow.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UIMessageCenter.h"
#include "UIMediumTools.h"
#include "UIModalWindowManager.h"
#include "UIMousePointerShapeData.h"
#include "UINotificationCenter.h"
#include "UISession.h"
#include "UITextTable.h"
#include "UIUSBTools.h"
#include "UIVersion.h"
#ifdef VBOX_GUI_WITH_KEYS_RESET_HANDLER
# include "UIKeyboardHandler.h"
# include <signal.h>
#endif

/* COM includes: */
#include "CAudioAdapter.h"
#include "CAudioSettings.h"
#include "CEmulatedUSB.h"
#include "CGraphicsAdapter.h"
#include "CHostNetworkInterface.h"
#include "CHostUSBDevice.h"
#include "CHostVideoInputDevice.h"
#include "CMedium.h"
#include "CMediumAttachment.h"
#include "CPlatform.h"
#include "CPlatformProperties.h"
#include "CRecordingSettings.h"
#include "CSnapshot.h"
#include "CStorageController.h"
#include "CSystemProperties.h"
#include "CUSBController.h"
#include "CUSBDevice.h"
#include "CUSBDeviceFilter.h"
#include "CUSBDeviceFilters.h"
#include "CVRDEServer.h"
#include "CVRDEServerInfo.h"
#ifdef VBOX_WITH_NETFLT
# include "CNetworkAdapter.h"
#endif

/* Other VBox includes: */
#ifdef VBOX_WITH_DEBUGGER_GUI
# include <VBox/dbggui.h>
# include <iprt/ldr.h>
#endif

#include <VBox/GuestHost/clipboard-helper.h>

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>

/* External includes: */
#ifdef VBOX_WS_NIX
# include <X11/Xlib.h>
# include <X11/Xutil.h>
#endif


#ifdef VBOX_GUI_WITH_KEYS_RESET_HANDLER
static void signalHandlerSIGUSR1(int sig, siginfo_t *, void *);
#endif

UISession::UISession(UIMachine *pMachine)
    : QObject(pMachine)
    /* Base variables: */
    , m_pMachine(pMachine)
    , m_fValid(false)
    , m_pConsoleEventhandler(0)
    /* Common variables: */
    , m_enmMachineStatePrevious(KMachineState_Null)
    , m_enmMachineState(KMachineState_Null)
    /* Guest additions flags: */
    , m_ulGuestAdditionsRunLevel(0)
    , m_fIsGuestSupportsGraphics(false)
    , m_fIsGuestSupportsSeamless(false)
#ifdef VBOX_WITH_DEBUGGER_GUI
    /* Debug UI stuff: */
    , m_pDbgGui(0)
    , m_pDbgGuiVT(0)
#endif /* VBOX_WITH_DEBUGGER_GUI */
{
}

bool UISession::prepare()
{
    /* Prepare COM stuff: */
    if (!prepareCOMStuff())
        return false;

    /* Cache media early if requested: */
    if (uiCommon().agressiveCaching())
        recacheMachineMedia();

    /* Prepare GUI stuff: */
    prepareConsoleEventHandlers();
    prepareFramebuffers();
    prepareConnections();
    prepareSignalHandling();

    /* True by default: */
    return true;
}

bool UISession::initialize()
{
    /* Preprocess initialization: */
    if (!preprocessInitialization())
        return false;

    /* Notify user about mouse&keyboard auto-capturing: */
    if (gEDataManager->autoCaptureEnabled())
        UINotificationMessage::remindAboutAutoCapture();

    m_enmMachineState = machine().GetState();

    /* Apply debug settings from the command line. */
    if (!debugger().isNull() && debugger().isOk())
    {
        if (uiCommon().areWeToExecuteAllInIem())
            debugger().SetExecuteAllInIEM(true);
        if (!uiCommon().isDefaultWarpPct())
            debugger().SetVirtualTimeRate(uiCommon().getWarpPct());
    }

    /* Apply ad-hoc reconfigurations from the command line: */
    if (uiCommon().hasFloppyImageToMount())
        mountAdHocImage(KDeviceType_Floppy, UIMediumDeviceType_Floppy, uiCommon().getFloppyImage());
    if (uiCommon().hasDvdImageToMount())
        mountAdHocImage(KDeviceType_DVD, UIMediumDeviceType_DVD, uiCommon().getDvdImage());

    /* Power UP if this is NOT separate process: */
    if (!uiCommon().isSeparateProcess())
        if (!powerUp())
            return false;

    /* Make sure all the pending Console events converted to signals
     * during the powerUp() progress above reached their destinations.
     * That is necessary to make sure all the pending machine state change events processed.
     * We can't just use the machine state directly acquired from IMachine because there
     * will be few places which are using stale machine state, not just this one. */
    QApplication::sendPostedEvents(0, QEvent::MetaCall);

    /* Check if we missed a really quick termination after successful startup: */
    if (isTurnedOff())
    {
        LogRel(("GUI: Aborting startup due to invalid machine state detected: %d\n", machineState()));
        return false;
    }

    /* Fetch corresponding states: */
    if (uiCommon().isSeparateProcess())
        sltAdditionsChange();

#ifdef VBOX_GUI_WITH_PIDFILE
    uiCommon().createPidfile();
#endif /* VBOX_GUI_WITH_PIDFILE */

    /* Mark as valid finally: */
    m_fValid = true;

    /* True by default: */
    return true;
}

bool UISession::powerUp()
{
    /* Power UP machine: */
    CProgress comProgress = uiCommon().shouldStartPaused() ? console().PowerUpPaused() : console().PowerUp();

    /* Check for immediate failure: */
    if (!console().isOk() || comProgress.isNull())
    {
        if (uiCommon().showStartVMErrors())
            msgCenter().cannotStartMachine(console(), machineName());
        LogRel(("GUI: Aborting startup due to power up issue detected...\n"));
        return false;
    }

    /* Some logging right after we powered up: */
    LogRel(("GUI: Qt version: %s\n", UIVersionInfo::qtRTVersionString().toUtf8().constData()));
#ifdef VBOX_WS_NIX
    LogRel(("GUI: X11 Window Manager code: %d\n", (int)uiCommon().typeOfWindowManager()));
#endif
#if defined(VBOX_WS_MAC) || defined(VBOX_WS_WIN)
    LogRel(("GUI: HID LEDs sync is %s\n", uimachine()->isHidLedsSyncEnabled() ? "enabled" : "disabled"));
#else
    LogRel(("GUI: HID LEDs sync is not supported on this platform\n"));
#endif

    /* Enable 'manual-override',
     * preventing automatic Runtime UI closing
     * and visual representation mode changes: */
    uimachine()->setManualOverrideMode(true);

    /* Show "Starting/Restoring" progress dialog: */
    if (isSaved())
    {
        msgCenter().showModalProgressDialog(comProgress, machineName(), ":/progress_state_restore_90px.png", 0, 0);
        /* After restoring from 'saved' state, machine-window(s) geometry should be adjusted: */
        machineLogic()->adjustMachineWindowsGeometry();
    }
    else
    {
#ifdef VBOX_IS_QT6_OR_LATER /** @todo why is this any problem on qt6? */
        msgCenter().showModalProgressDialog(comProgress, machineName(), ":/progress_start_90px.png", 0, 0);
#else
        msgCenter().showModalProgressDialog(comProgress, machineName(), ":/progress_start_90px.png");
#endif
        /* After VM start, machine-window(s) size-hint(s) should be sent: */
        machineLogic()->sendMachineWindowsSizeHints();
    }

    /* Check for progress failure: */
    if (!comProgress.isOk() || comProgress.GetResultCode() != 0)
    {
        if (uiCommon().showStartVMErrors())
            msgCenter().cannotStartMachine(comProgress, machineName());
        LogRel(("GUI: Aborting startup due to power up progress issue detected...\n"));
        return false;
    }

    /* Disable 'manual-override' finally: */
    uimachine()->setManualOverrideMode(false);

    /* True by default: */
    return true;
}

WId UISession::mainMachineWindowId() const
{
    return mainMachineWindow() ? mainMachineWindow()->winId() : 0;
}

void UISession::acquireMachinePixmap(const QSize &size, QPixmap &pixmap)
{
    QPixmap machinePixmap;
    if (machine().isNotNull())
        machinePixmap = generalIconPool().userMachinePixmap(machine(), size);
    if (machinePixmap.isNull())
        machinePixmap = generalIconPool().guestOSTypePixmap(osTypeId(), size);
    if (!machinePixmap.isNull())
        pixmap = machinePixmap;
}

void UISession::acquireUserMachineIcon(QIcon &icon)
{
    QIcon machineIcon;
    if (machine().isNotNull())
        machineIcon = generalIconPool().userMachineIcon(machine());
    if (machineIcon.isNull())
        machineIcon = generalIconPool().guestOSTypeIcon(osTypeId());
    if (machineIcon.isNull())
        machineIcon = QIcon(":/VirtualBox_48px.png");
    if (!machineIcon.isNull())
        icon = machineIcon;
}

bool UISession::acquireArchitectureType(KPlatformArchitecture &enmType)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    CPlatform comPlatform = comMachine.GetPlatform();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const KPlatformArchitecture enmArchType = comPlatform.GetArchitecture();
        fSuccess = comPlatform.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquirePlatformParameter(comPlatform);
        else
            enmType = enmArchType;
    }
    return fSuccess;
}

bool UISession::acquireChipsetType(KChipsetType &enmType)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    CPlatform comPlatform = comMachine.GetPlatform();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const KChipsetType enmChipsetType = comPlatform.GetChipsetType();
        fSuccess = comPlatform.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquirePlatformParameter(comPlatform);
        else
            enmType = enmChipsetType;
    }
    return fSuccess;
}

bool UISession::acquireLiveMachineState(KMachineState &enmState)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    const KMachineState enmMachineState = comMachine.GetState();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
        enmState = enmMachineState;
    return fSuccess;
}

bool UISession::reset()
{
    CConsole comConsole = console();
    comConsole.Reset();
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotResetMachine(comConsole);
    return fSuccess;
}

bool UISession::setPause(bool fPause)
{
    CConsole comConsole = console();
    if (fPause)
        comConsole.Pause();
    else
        comConsole.Resume();
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
    {
        if (fPause)
            UINotificationMessage::cannotPauseMachine(comConsole);
        else
            UINotificationMessage::cannotResumeMachine(comConsole);
    }
    return fSuccess;
}

bool UISession::acquireSettingsFilePath(QString &strPath)
{
    CMachine comMachine = machine();
    const QString strSettingsFilePath = comMachine.GetSettingsFilePath();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotSaveMachineSettings(comMachine);
    else
        strPath = strSettingsFilePath;
    return fSuccess;
}

bool UISession::saveSettings()
{
    CMachine comMachine = machine();
    comMachine.SaveSettings();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotSaveMachineSettings(comMachine);
    return fSuccess;
}

bool UISession::acquireSnapshotCount(ulong &uCount)
{
    CMachine comMachine = machine();
    const ULONG uSnapshotCount = comMachine.GetSnapshotCount();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
        uCount = uSnapshotCount;
    return fSuccess;
}

bool UISession::acquireCurrentSnapshotName(QString &strName)
{
    CMachine comMachine = machine();
    CSnapshot comSnapshot = comMachine.GetCurrentSnapshot();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    {
        const QString strSnapshotName = comSnapshot.GetName();
        fSuccess = comSnapshot.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireSnapshotParameter(comSnapshot);
        else
            strName = strSnapshotName;
    }
    return fSuccess;
}

bool UISession::acquireMaxSnapshotIndex(const QString &strNameTemplate, ulong &uIndex)
{
    CMachine comMachine = machine();
    CSnapshot comSnapshot = comMachine.FindSnapshot(QString());
    return searchMaxSnapshotIndex(comMachine, comSnapshot, strNameTemplate, uIndex);
}

void UISession::takeSnapshot(const QString &strName, const QString &strDescription)
{
    CMachine comMachine = machine();
    UINotificationProgressSnapshotTake *pNotification = new UINotificationProgressSnapshotTake(comMachine,
                                                                                               strName,
                                                                                               strDescription);
    gpNotificationCenter->append(pNotification);
}

bool UISession::putScancode(LONG iCode)
{
    CKeyboard comKeyboard = keyboard();
    comKeyboard.PutScancode(iCode);
    const bool fSuccess = comKeyboard.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeKeyboardParameter(comKeyboard);
    return fSuccess;
}

bool UISession::putScancodes(const QVector<LONG> &codes)
{
    CKeyboard comKeyboard = keyboard();
    if (comKeyboard.isNull())
        return false;
    comKeyboard.PutScancodes(codes);
    const bool fSuccess = comKeyboard.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeKeyboardParameter(comKeyboard);
    return fSuccess;
}

bool UISession::putCAD()
{
    CKeyboard comKeyboard = keyboard();
    comKeyboard.PutCAD();
    const bool fSuccess = comKeyboard.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeKeyboardParameter(comKeyboard);
    return fSuccess;
}

bool UISession::releaseKeys()
{
    CKeyboard comKeyboard = keyboard();
    comKeyboard.ReleaseKeys();
    const bool fSuccess = comKeyboard.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeKeyboardParameter(comKeyboard);
    return fSuccess;
}

bool UISession::putUsageCode(LONG iUsageCode, LONG iUsagePage, bool fKeyRelease)
{
    CKeyboard comKeyboard = keyboard();
    comKeyboard.PutUsageCode(iUsageCode, iUsagePage, fKeyRelease);
    const bool fSuccess = comKeyboard.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeKeyboardParameter(comKeyboard);
    return fSuccess;
}

bool UISession::acquireWhetherAbsoluteSupported(bool &fSupported)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    const BOOL fAbsoluteSupported = comMouse.GetAbsoluteSupported();
    const bool fSuccess = comMouse.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMouseParameter(comMouse);
    else
        fSupported = fAbsoluteSupported == TRUE;
    return fSuccess;
}

bool UISession::acquireWhetherRelativeSupported(bool &fSupported)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    const BOOL fRelativeSupported = comMouse.GetRelativeSupported();
    const bool fSuccess = comMouse.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMouseParameter(comMouse);
    else
        fSupported = fRelativeSupported == TRUE;
    return fSuccess;
}

bool UISession::acquireWhetherTouchScreenSupported(bool &fSupported)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    const BOOL fTouchScreenSupported = comMouse.GetTouchScreenSupported();
    const bool fSuccess = comMouse.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMouseParameter(comMouse);
    else
        fSupported = fTouchScreenSupported == TRUE;
    return fSuccess;
}

bool UISession::acquireWhetherTouchPadSupported(bool &fSupported)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    const BOOL fTouchPadSupported = comMouse.GetTouchPadSupported();
    const bool fSuccess = comMouse.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMouseParameter(comMouse);
    else
        fSupported = fTouchPadSupported == TRUE;
    return fSuccess;
}

bool UISession::acquireWhetherNeedsHostCursor(bool &fNeeds)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    const BOOL fNeedsHostCursor = comMouse.GetNeedsHostCursor();
    const bool fSuccess = comMouse.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMouseParameter(comMouse);
    else
        fNeeds = fNeedsHostCursor == TRUE;
    return fSuccess;
}

bool UISession::putMouseEvent(long iDx, long iDy, long iDz, long iDw, long iButtonState)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    comMouse.PutMouseEvent(iDx, iDy, iDz, iDw, iButtonState);
    return comMouse.isOk();
}

bool UISession::putMouseEventAbsolute(long iX, long iY, long iDz, long iDw, long iButtonState)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    comMouse.PutMouseEventAbsolute(iX, iY, iDz, iDw, iButtonState);
    return comMouse.isOk();
}

bool UISession::putEventMultiTouch(long iCount, const QVector<LONG64> &contacts, bool fIsTouchScreen, ulong uScanTime)
{
    CMouse comMouse = mouse();
    if (comMouse.isNull())
        return false;
    comMouse.PutEventMultiTouch(iCount, contacts, fIsTouchScreen, uScanTime);
    return comMouse.isOk();
}

bool UISession::acquireClipboardMode(KClipboardMode &enmMode)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    const KClipboardMode enmClipboardMode = comMachine.GetClipboardMode();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
        enmMode = enmClipboardMode;
    return fSuccess;
}

bool UISession::setClipboardMode(KClipboardMode enmMode)
{
    CMachine comMachine = machine();
    comMachine.SetClipboardMode(enmMode);
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    return fSuccess;
}

bool UISession::toggleClipboardFileTransfer(bool fEnabled)
{
    CMachine comMachine = machine();
    comMachine.SetClipboardFileTransfersEnabled(fEnabled);
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeMachineParameter(comMachine);
    return fSuccess;
}

bool UISession::isClipboardFileTransferEnabled()
{
    CMachine comMachine = machine();
    bool fEnabled = comMachine.GetClipboardFileTransfersEnabled();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
    {
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
        return false;
    }
    return fEnabled;
}

bool UISession::acquireDnDMode(KDnDMode &enmMode)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    const KDnDMode enmDnDMode = comMachine.GetDnDMode();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
        enmMode = enmDnDMode;
    return fSuccess;
}

bool UISession::setDnDMode(KDnDMode enmMode)
{
    CMachine comMachine = machine();
    comMachine.SetDnDMode(enmMode);
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    return fSuccess;
}

bool UISession::acquireAmountOfStorageDevices(ulong &cHardDisks, ulong &cOpticalDrives, ulong &cFloppyDrives)
{
    const CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    ulong cActualHardDisks = 0, cActualOpticalDrives = 0, cActualFloppyDrives = 0;
    const CMediumAttachmentVector comAttachments = comMachine.GetMediumAttachments();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        foreach (const CMediumAttachment &comAttachment, comAttachments)
        {
            /* Get device type: */
            const KDeviceType enmDeviceType = comAttachment.GetType();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }

            /* And advance corresponding amount: */
            switch (enmDeviceType)
            {
                case KDeviceType_HardDisk: ++cActualHardDisks; break;
                case KDeviceType_DVD: ++cActualOpticalDrives; break;
                case KDeviceType_Floppy: ++cActualFloppyDrives; break;
                default: break;
            }
        }
    }
    if (fSuccess)
    {
        cHardDisks = cActualHardDisks;
        cOpticalDrives = cActualOpticalDrives;
        cFloppyDrives = cActualFloppyDrives;
    }
    return fSuccess;
}

bool UISession::storageDevices(KDeviceType enmActualDeviceType, QList<StorageDeviceInfo> &guiStorageDevices)
{
    const CMachine comMachine = machine();
    const CMediumAttachmentVector comAttachments = comMachine.GetMediumAttachments();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        foreach (const CMediumAttachment &comAttachment, comAttachments)
        {
            /* Get device type: */
            const KDeviceType enmDeviceType = comAttachment.GetType();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            /* And make sure it's actual one: */
            if (enmDeviceType != enmActualDeviceType)
                continue;

            /* Get controller name: */
            const QString strControllerName = comAttachment.GetController();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            /* And search for it's presence: */
            const CStorageController comController = comMachine.GetStorageControllerByName(strControllerName);
            fSuccess = comMachine.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                break;
            }

            /* Fill structure fields: */
            StorageDeviceInfo guiStorageDevice;
            guiStorageDevice.m_strControllerName = strControllerName; // already confirmed
            const KStorageBus enmStorageBus = comController.GetBus();
            fSuccess = comController.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireStorageControllerParameter(comController);
                break;
            }
            switch (enmStorageBus)
            {
                case KStorageBus_IDE:        guiStorageDevice.m_icon = UIIconPool::iconSet(":/ide_16px.png"); break;
                case KStorageBus_SATA:       guiStorageDevice.m_icon = UIIconPool::iconSet(":/sata_16px.png"); break;
                case KStorageBus_SCSI:       guiStorageDevice.m_icon = UIIconPool::iconSet(":/scsi_16px.png"); break;
                case KStorageBus_Floppy:     guiStorageDevice.m_icon = UIIconPool::iconSet(":/floppy_16px.png"); break;
                case KStorageBus_SAS:        guiStorageDevice.m_icon = UIIconPool::iconSet(":/sas_16px.png"); break;
                case KStorageBus_USB:        guiStorageDevice.m_icon = UIIconPool::iconSet(":/usb_16px.png"); break;
                case KStorageBus_PCIe:       guiStorageDevice.m_icon = UIIconPool::iconSet(":/pcie_16px.png"); break;
                case KStorageBus_VirtioSCSI: guiStorageDevice.m_icon = UIIconPool::iconSet(":/virtio_scsi_16px.png"); break;
                default: break;
            }
            const long iPort = comAttachment.GetPort();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            const long iDevice = comAttachment.GetDevice();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            if (fSuccess)
            {
                const StorageSlot guiStorageSlot(enmStorageBus, iPort, iDevice);
                guiStorageDevice.m_guiStorageSlot = guiStorageSlot;
            }

            /* Append or break if necessary: */
            if (fSuccess)
                guiStorageDevices << guiStorageDevice;
            else
                break;
        }
    }
    return fSuccess;
}

bool UISession::acquireEncryptedMedia(EncryptedMediumMap &media)
{
    EncryptedMediumMap encryptedMedia;
    CMachine comMachine = machine();
    const CMediumAttachmentVector comAttachments = comMachine.GetMediumAttachments();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        foreach (const CMediumAttachment &comAttachment, comAttachments)
        {
            const KDeviceType enmType = comAttachment.GetType();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }

            /* Look for hard-drive attachments only: */
            if (enmType != KDeviceType_HardDisk)
                continue;

            /* Get the attachment medium: */
            const CMedium comMedium = comAttachment.GetMedium();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }

            /* Get the medium ID: */
            const QUuid uId = comMedium.GetId();
            fSuccess = comMedium.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumParameter(comMedium);
                break;
            }

            /* Update the map with this medium if it's encrypted: */
            QString strCipher;
            const QString strPasswordId = comMedium.GetEncryptionSettings(strCipher);
            if (comMedium.isOk()) // GetEncryptionSettings failure is a valid case
                encryptedMedia.insert(strPasswordId, uId);
        }
    }

    if (fSuccess)
        media = encryptedMedia;
    return fSuccess;
}

bool UISession::addEncryptionPassword(const QString &strId, const QString &strPassword, bool fClearOnSuspend)
{
    CConsole comConsole = console();
    comConsole.AddEncryptionPassword(strId, strPassword, fClearOnSuspend);
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        msgCenter().cannotAddDiskEncryptionPassword(comConsole);
    return fSuccess;
}

bool UISession::acquireAmountOfImmutableImages(ulong &cAmount)
{
    CMachine comMachine = machine();
    return UIMediumTools::acquireAmountOfImmutableImages(comMachine, cAmount);
}

bool UISession::mountBootMedium(const QUuid &uMediumId)
{
    AssertReturn(!uMediumId.isNull(), false);

    /* Get recommended controller bus & type: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    if (!comVBox.isOk())
    {
        UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox);
        return false;
    }

    const KStorageBus enmRecommendedDvdBus = gpGlobalSession->guestOSTypeManager().getRecommendedDVDStorageBus(osTypeId());
    const KStorageControllerType enmRecommendedDvdType = gpGlobalSession->guestOSTypeManager().getRecommendedDVDStorageController(osTypeId());

    /* Search for an attachment of required bus & type: */
    CMachine comMachine = machine();
    CMediumAttachmentVector comMediumAttachments = comMachine.GetMediumAttachments();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CMediumAttachment comChosenAttachment;
        QString strChosenControllerName;
        LONG iChosenAttachmentPort = 0;
        LONG iChosenAttachmentDevice = 0;
        foreach (const CMediumAttachment &comAttachment, comMediumAttachments)
        {
            /* Get attachment type: */
            const KDeviceType enmCurrentDeviceType = comAttachment.GetType();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            /* And make sure it's DVD: */
            if (enmCurrentDeviceType != KDeviceType_DVD)
                continue;

            /* Get controller name: */
            const QString strControllerName = comAttachment.GetController();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            /* And look for corresponding controller: */
            const CStorageController comCurrentController = comMachine.GetStorageControllerByName(strControllerName);
            fSuccess = comMachine.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                break;
            }

            /* Get current controller bus: */
            const KStorageBus enmCurrentBus = comCurrentController.GetBus();
            fSuccess = comCurrentController.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireStorageControllerParameter(comCurrentController);
                break;
            }
            /* Get current controller type: */
            const KStorageControllerType enmCurrentType = comCurrentController.GetControllerType();
            fSuccess = comCurrentController.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireStorageControllerParameter(comCurrentController);
                break;
            }
            /* And check if they are suitable: */
            if (   enmCurrentBus != enmRecommendedDvdBus
                || enmCurrentType != enmRecommendedDvdType)
                continue;

            /* Get current attachment port: */
            iChosenAttachmentPort = comAttachment.GetPort();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }
            /* Get current attachment device: */
            iChosenAttachmentDevice = comAttachment.GetDevice();
            fSuccess = comAttachment.isOk();
            if (!fSuccess)
            {
                UINotificationMessage::cannotAcquireMediumAttachmentParameter(comAttachment);
                break;
            }

            /* Everything is nice it seems: */
            comChosenAttachment = comAttachment;
            strChosenControllerName = strControllerName;
            break;
        }
        AssertMsgReturn(!comChosenAttachment.isNull(), ("Storage Controller is NOT properly configured!\n"), false);

        /* Get medium to mount: */
        const UIMedium guiMedium = gpMediumEnumerator->medium(uMediumId);
        const CMedium comMedium = guiMedium.medium();

        /* Mount medium to the predefined port/device: */
        comMachine.MountMedium(strChosenControllerName,
                               iChosenAttachmentPort, iChosenAttachmentDevice,
                               comMedium, false /* force */);
        fSuccess = comMachine.isOk();
        if (!fSuccess)
        {
            QWidget *pParent = windowManager().realParentWindow(activeMachineWindow());
            msgCenter().cannotRemountMedium(machine(), guiMedium, true /* mount? */, false /* retry? */, pParent);
        }
        else
            fSuccess = saveSettings();
    }

    return fSuccess;
}

void UISession::prepareStorageMenu(QMenu *pMenu,
                                   QObject *pListener, const char *pszSlotName,
                                   const QString &strControllerName, const StorageSlot &storageSlot)
{
    CMachine comMachine = machine();
    UIMediumTools::prepareStorageMenu(pMenu,
                                      pListener, pszSlotName,
                                      comMachine, strControllerName, storageSlot);
}

void UISession::updateMachineStorage(const UIMediumTarget &target, UIActionPool *pActionPool)
{
    CMachine comMachine = machine();
    UIMediumTools::updateMachineStorage(comMachine, target, pActionPool);
}

void UISession::acquireWhetherUSBControllerEnabled(bool &fEnabled)
{
    fEnabled = false;

    /* Check whether USB device filters are available: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    CUSBDeviceFilters comUSBDeviceFilters = comMachine.GetUSBDeviceFilters();
    if (!comMachine.isOk() || comUSBDeviceFilters.isNull())
        return;
    /* Check whether USB controllers are available: */
    CUSBControllerVector comUSBControllers = comMachine.GetUSBControllers();
    if (!comMachine.isOk() || comUSBControllers.isEmpty())
        return;
    /* Check whether USB proxy is available: */
    const BOOL fUSBProxyAvailable = comMachine.GetUSBProxyAvailable();
    if (!comMachine.isOk() || fUSBProxyAvailable == FALSE)
        return;

    fEnabled = true;
}

void UISession::acquireWhetherVideoInputDevicesEnabled(bool &fEnabled)
{
    fEnabled = false;

    /* Check whether video input devices are available: */
    CHost comHost = gpGlobalSession->host();
    CHostVideoInputDeviceVector comVideoInputDevices = comHost.GetVideoInputDevices();
    if (!comHost.isOk() || comVideoInputDevices.isEmpty())
        return;
    /* Check whether USB controllers are available: */
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    CUSBControllerVector comUSBControllers = comMachine.GetUSBControllers();
    if (!comMachine.isOk() || comUSBControllers.isEmpty())
        return;

    fEnabled = true;
}

bool UISession::usbDevices(QList<USBDeviceInfo> &guiUSBDevices)
{
    const CHost comHost = gpGlobalSession->host();
    const CHostUSBDeviceVector comHostUSBDevices = comHost.GetUSBDevices();
    bool fSuccess = comHost.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireHostParameter(comHost);
    else
    {
        foreach (const CHostUSBDevice &comHostUSBDevice, comHostUSBDevices)
        {
            /* Get USB device from current host USB device,
             * this stuff requires #include <VBox/com/VirtualBox.h> */
            const CUSBDevice comUSBDevice(comHostUSBDevice);

            /* Fill structure fields: */
            USBDeviceInfo guiUSBDevice;
            if (fSuccess)
            {
                guiUSBDevice.m_uId = comUSBDevice.GetId();
                fSuccess = comUSBDevice.isOk();
            }
            if (fSuccess)
            {
                /// @todo make sure UIUSBTools::usbDetails is checked for errors as well
                guiUSBDevice.m_strName = usbDetails(comUSBDevice);
                fSuccess = comUSBDevice.isOk();
            }
            if (fSuccess)
            {
                /// @todo make sure UIUSBTools::usbToolTip is checked for errors as well
                guiUSBDevice.m_strToolTip = usbToolTip(comUSBDevice);
                fSuccess = comUSBDevice.isOk();
            }
            if (fSuccess)
            {
                /* Check if that USB device was already attached to this session;
                 * Nothing to check for errors here because error is valid case as well. */
                const CUSBDevice comAttachedDevice = console().FindUSBDeviceById(guiUSBDevice.m_uId);
                guiUSBDevice.m_fIsChecked = !comAttachedDevice.isNull();
            }
            if (fSuccess)
            {
                guiUSBDevice.m_fIsEnabled = comHostUSBDevice.GetState() != KUSBDeviceState_Unavailable;
                fSuccess = comHostUSBDevice.isOk();
            }

            /* Append or break if necessary: */
            if (fSuccess)
                guiUSBDevices << guiUSBDevice;
            else
                break;
        }
    }
    return fSuccess;
}

bool UISession::attachUSBDevice(const QUuid &uId)
{
    CConsole comConsole = console();
    comConsole.AttachUSBDevice(uId, QString(""));
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
    {
        CHost comHost = gpGlobalSession->host();
        /* Nothing to check for errors here because error is valid case as well. */
        CHostUSBDevice comHostUSBDevice = comHost.FindUSBDeviceById(uId);
        /* Get USB device from current host USB device,
         * this stuff requires #include <VBox/com/VirtualBox.h> */
        CUSBDevice comUSBDevice(comHostUSBDevice);
        UINotificationMessage::cannotAttachUSBDevice(comConsole, usbDetails(comUSBDevice));
        /// @todo make sure UIUSBTools::usbDetails is checked for errors as well
    }
    return fSuccess;
}

bool UISession::detachUSBDevice(const QUuid &uId)
{
    CConsole comConsole = console();
    comConsole.DetachUSBDevice(uId);
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
    {
        CUSBDevice comUSBDevice = CConsole(comConsole).FindUSBDeviceById(uId);
        UINotificationMessage::cannotDetachUSBDevice(comConsole, usbDetails(comUSBDevice));
        /// @todo make sure UIUSBTools::usbDetails is checked for errors as well
    }
    return fSuccess;
}

bool UISession::webcamDevices(QList<WebcamDeviceInfo> &guiWebcamDevices)
{
    const CHost comHost = gpGlobalSession->host();
    const CHostVideoInputDeviceVector comHostVideoInputDevices = comHost.GetVideoInputDevices();
    bool fSuccess = comHost.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireHostParameter(comHost);
    else
    {
        CConsole comConsole = console();
        CEmulatedUSB comEmulatedUSB = comConsole.GetEmulatedUSB();
        fSuccess = comConsole.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireConsoleParameter(comConsole);
        else
        {
            const QVector<QString> attachedWebcamPaths = comEmulatedUSB.GetWebcams();
            fSuccess = comEmulatedUSB.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotAcquireEmulatedUSBParameter(comEmulatedUSB);
            else
            {
                foreach (const CHostVideoInputDevice &comHostVideoInputDevice, comHostVideoInputDevices)
                {
                    /* Fill structure fields: */
                    WebcamDeviceInfo guiWebcamDevice;
                    if (fSuccess)
                    {
                        guiWebcamDevice.m_strName = comHostVideoInputDevice.GetName();
                        fSuccess = comHostVideoInputDevice.isOk();
                    }
                    if (fSuccess)
                    {
                        guiWebcamDevice.m_strPath = comHostVideoInputDevice.GetPath();
                        fSuccess = comHostVideoInputDevice.isOk();
                    }
                    if (fSuccess)
                    {
                        /// @todo make sure UIUSBTools::usbToolTip is checked for errors as well
                        guiWebcamDevice.m_strToolTip = usbToolTip(comHostVideoInputDevice);
                        fSuccess = comHostVideoInputDevice.isOk();
                    }
                    if (fSuccess)
                        guiWebcamDevice.m_fIsChecked = attachedWebcamPaths.contains(guiWebcamDevice.m_strPath);

                    /* Append or break if necessary: */
                    if (fSuccess)
                        guiWebcamDevices << guiWebcamDevice;
                    else
                        break;
                }
            }
        }
    }
    return fSuccess;
}

bool UISession::webcamAttach(const QString &strPath, const QString &strName)
{
    CConsole comConsole = console();
    CEmulatedUSB comDispatcher = comConsole.GetEmulatedUSB();
    bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireConsoleParameter(comConsole);
    else
    {
        comDispatcher.WebcamAttach(strPath, "");
        fSuccess = comDispatcher.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAttachWebCam(comDispatcher, strName, machineName());
    }
    return fSuccess;
}

bool UISession::webcamDetach(const QString &strPath, const QString &strName)
{
    CConsole comConsole = console();
    CEmulatedUSB comDispatcher = comConsole.GetEmulatedUSB();
    bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireConsoleParameter(comConsole);
    else
    {
        comDispatcher.WebcamDetach(strPath);
        fSuccess = comDispatcher.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotDetachWebCam(comDispatcher, strName, machineName());
    }
    return fSuccess;
}

bool UISession::acquireWhetherNetworkAdapterEnabled(ulong uSlot, bool &fEnabled)
{
    CMachine comMachine = machine();
    CNetworkAdapter comAdapter = comMachine.GetNetworkAdapter(uSlot);
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const BOOL fAdapterEnabled = comAdapter.GetEnabled();
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireNetworkAdapterParameter(comAdapter);
        else
            fEnabled = fAdapterEnabled == TRUE;
    }
    return fSuccess;
}

bool UISession::acquireWhetherAtLeastOneNetworkAdapterEnabled(bool &fEnabled)
{
    /* Acquire architecture type: */
    KPlatformArchitecture enmArchType = KPlatformArchitecture_None;
    bool fSuccess = acquireArchitectureType(enmArchType);
    {
        /* Acquire system properties: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        AssertReturn(comVBox.isNotNull(), false);
        CPlatformProperties comProperties = comVBox.GetPlatformProperties(enmArchType);
        fSuccess = comVBox.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox);
        else
        {
            /* Acquire chipset type: */
            KChipsetType enmChipsetType = KChipsetType_Null;
            fSuccess = acquireChipsetType(enmChipsetType);
            if (fSuccess)
            {
                /* Acquire maximum network adapters count: */
                const ulong cMaxNetworkAdapters = comProperties.GetMaxNetworkAdapters(enmChipsetType);
                fSuccess = comProperties.isOk();
                if (!fSuccess)
                    UINotificationMessage::cannotAcquirePlatformPropertiesParameter(comProperties);
                else
                {
                    /* Search for 1st enabled adapter: */
                    for (ulong uSlot = 0; uSlot < cMaxNetworkAdapters; ++uSlot)
                    {
                        bool fAdapterEnabled = false;
                        fSuccess = acquireWhetherNetworkAdapterEnabled(uSlot, fAdapterEnabled);
                        if (!fSuccess)
                            break;
                        if (!fAdapterEnabled)
                            continue;
                        fEnabled = true;
                        break;
                    }
                }
            }
        }
    }
    return fSuccess;
}

bool UISession::acquireWhetherNetworkCableConnected(ulong uSlot, bool &fConnected)
{
    CMachine comMachine = machine();
    CNetworkAdapter comAdapter = comMachine.GetNetworkAdapter(uSlot);
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const BOOL fCableConnected = comAdapter.GetCableConnected();
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireNetworkAdapterParameter(comAdapter);
        else
            fConnected = fCableConnected == TRUE;
    }
    return fSuccess;
}

bool UISession::setNetworkCableConnected(ulong uSlot, bool fConnected)
{
    CMachine comMachine = machine();
    CNetworkAdapter comAdapter = comMachine.GetNetworkAdapter(uSlot);
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        comAdapter.SetCableConnected(fConnected);
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotToggleNetworkCable(comAdapter, machineName(), fConnected);
    }
    return fSuccess;
}

bool UISession::guestAdditionsUpgradable()
{
    if (!machine().isOk())
        return false;

    /* Auto GA update is currently for Windows and Linux guests only */
    bool fIsWindowOrLinux = gpGlobalSession->guestOSTypeManager().isLinux(uimachine()->osTypeId())
                         || gpGlobalSession->guestOSTypeManager().isWindows(uimachine()->osTypeId());

    if (!fIsWindowOrLinux)
        return false;

    /* Also check whether we have something to update automatically: */
    if (m_ulGuestAdditionsRunLevel < (ulong)KAdditionsRunLevelType_Userland)
        return false;

    return true;
}

bool UISession::acquireGuestAdditionsVersion(QString &strVersion)
{
    CGuest comGuest = guest();
    const QString strGAVersion = comGuest.GetAdditionsVersion();
    const bool fSuccess = comGuest.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireGuestParameter(comGuest);
    else
        strVersion = strGAVersion;
    return fSuccess;
}

bool UISession::acquireGuestAdditionsRevision(ulong &uRevision)
{
    CGuest comGuest = guest();
    const ULONG uGARevision = comGuest.GetAdditionsRevision();
    const bool fSuccess = comGuest.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireGuestParameter(comGuest);
    else
        uRevision = uGARevision;
    return fSuccess;
}

bool UISession::notifyGuiFocusChange(bool fInfocus)
{
    CMachine comMachine = machine();
    comMachine.SetGuestProperty(VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME, fInfocus ? "1" : "0", "RDONLYGUEST,TRANSIENT");
    return comMachine.isOk();
}

bool UISession::acquireWhetherAudioAdapterPresent(bool &fPresent)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    CAudioSettings comSettings = comMachine.GetAudioSettings();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CAudioAdapter comAdapter = comSettings.GetAdapter();
        fPresent = comSettings.isOk() && comAdapter.isNotNull();
    }
    return fSuccess;
}

bool UISession::acquireWhetherAudioAdapterEnabled(bool &fEnabled)
{
    CMachine comMachine = machine();
    CAudioSettings comSettings = comMachine.GetAudioSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CAudioAdapter comAdapter = comSettings.GetAdapter();
        fSuccess = comSettings.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireAudioSettingsParameter(comSettings);
        {
            const BOOL fAdapterEnabled = comAdapter.GetEnabled();
            fSuccess = comAdapter.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotAcquireAudioAdapterParameter(comAdapter);
            else
                fEnabled = fAdapterEnabled == TRUE;
        }
    }
    return fSuccess;
}

bool UISession::acquireWhetherAudioAdapterOutputEnabled(bool &fEnabled)
{
    CMachine comMachine = machine();
    CAudioSettings comSettings = comMachine.GetAudioSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CAudioAdapter comAdapter = comSettings.GetAdapter();
        fSuccess = comSettings.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireAudioSettingsParameter(comSettings);
        {
            const BOOL fOutputEnabled = comAdapter.GetEnabledOut();
            fSuccess = comAdapter.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotAcquireAudioAdapterParameter(comAdapter);
            else
                fEnabled = fOutputEnabled == TRUE;
        }
    }
    return fSuccess;
}

bool UISession::acquireWhetherAudioAdapterInputEnabled(bool &fEnabled)
{
    CMachine comMachine = machine();
    CAudioSettings comSettings = comMachine.GetAudioSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CAudioAdapter comAdapter = comSettings.GetAdapter();
        fSuccess = comSettings.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireAudioSettingsParameter(comSettings);
        {
            const BOOL fInputEnabled = comAdapter.GetEnabledIn();
            fSuccess = comAdapter.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotAcquireAudioAdapterParameter(comAdapter);
            else
                fEnabled = fInputEnabled == TRUE;
        }
    }
    return fSuccess;
}

bool UISession::setAudioAdapterOutputEnabled(bool fEnabled)
{
    CMachine comMachine = machine();
    CAudioSettings comSettings = comMachine.GetAudioSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CAudioAdapter comAdapter = comSettings.GetAdapter();
        fSuccess = comSettings.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireAudioSettingsParameter(comSettings);
        {
            comAdapter.SetEnabledOut(fEnabled);
            fSuccess = comAdapter.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotToggleAudioOutput(comAdapter, machineName(), fEnabled);
        }
    }
    return fSuccess;
}

bool UISession::setAudioAdapterInputEnabled(bool fEnabled)
{
    CMachine comMachine = machine();
    CAudioSettings comSettings = comMachine.GetAudioSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        CAudioAdapter comAdapter = comSettings.GetAdapter();
        fSuccess = comSettings.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireAudioSettingsParameter(comSettings);
        {
            comAdapter.SetEnabledIn(fEnabled);
            fSuccess = comAdapter.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotToggleAudioInput(comAdapter, machineName(), fEnabled);
        }
    }
    return fSuccess;
}

UIFrameBuffer *UISession::frameBuffer(ulong uScreenId) const
{
    return m_frameBufferVector.value((int)uScreenId, 0);
}

QSize UISession::frameBufferSize(ulong uScreenId) const
{
    UIFrameBuffer *pFramebuffer = frameBuffer(uScreenId);
    return pFramebuffer ? QSize(pFramebuffer->width(), pFramebuffer->height()) : QSize();
}

bool UISession::acquireGraphicsControllerType(KGraphicsControllerType &enmType)
{
    CMachine comMachine = machine();
    CGraphicsAdapter comAdapter = comMachine.GetGraphicsAdapter();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const KGraphicsControllerType enmControllerType = comAdapter.GetGraphicsControllerType();
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireGraphicsAdapterParameter(comAdapter);
        else
            enmType = enmControllerType;
    }
    return fSuccess;
}

bool UISession::acquireVRAMSize(ulong &uSize)
{
    CMachine comMachine = machine();
    CGraphicsAdapter comAdapter = comMachine.GetGraphicsAdapter();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const ULONG uVRAMSize = comAdapter.GetVRAMSize();
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireGraphicsAdapterParameter(comAdapter);
        else
            uSize = uVRAMSize;
    }
    return fSuccess;
}

bool UISession::acquireWhetherAccelerate3DEnabled(bool &fEnabled)
{
    CMachine comMachine = machine();
    CGraphicsAdapter comAdapter = comMachine.GetGraphicsAdapter();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const BOOL fAccelerate3DEnabeld = comAdapter.IsFeatureEnabled(KGraphicsFeature_Acceleration3D);
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireGraphicsAdapterParameter(comAdapter);
        else
            fEnabled = fAccelerate3DEnabeld == TRUE;
    }
    return fSuccess;
}

bool UISession::acquireMonitorCount(ulong &uCount)
{
    // WORKAROUND:
    // We certainly don't want to get into
    // 'no visible screens' case if something
    // gone wrong...  Inventing sane default.
    uCount = 1;

    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    CGraphicsAdapter comAdapter = comMachine.GetGraphicsAdapter();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const ULONG uMonitorCount = comAdapter.GetMonitorCount();
        fSuccess = comAdapter.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireGraphicsAdapterParameter(comAdapter);
        else
            uCount = uMonitorCount;
    }
    return fSuccess;
}

bool UISession::acquireGuestScreenParameters(ulong uScreenId,
                                             ulong &uWidth, ulong &uHeight, ulong &uBitsPerPixel,
                                             long &xOrigin, long &yOrigin, KGuestMonitorStatus &enmMonitorStatus)
{
    CDisplay comDisplay = display();
    ULONG uGuestWidth = 0, uGuestHeight = 0, uGuestBitsPerPixel = 0;
    LONG iGuestXOrigin = 0, iGuestYOrigin = 0;
    KGuestMonitorStatus enmGuestMonitorStatus = KGuestMonitorStatus_Disabled;
    comDisplay.GetScreenResolution(uScreenId, uGuestWidth, uGuestHeight, uGuestBitsPerPixel,
                                   iGuestXOrigin, iGuestYOrigin, enmGuestMonitorStatus);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireDisplayParameter(comDisplay);
    else
    {
        uWidth = uGuestWidth;
        uHeight = uGuestHeight;
        uBitsPerPixel = uGuestBitsPerPixel;
        xOrigin = iGuestXOrigin;
        yOrigin = iGuestYOrigin;
        enmMonitorStatus = enmGuestMonitorStatus;
    }
    return fSuccess;
}

bool UISession::acquireSavedGuestScreenInfo(ulong uScreenId,
                                            long &xOrigin, long &yOrigin,
                                            ulong &uWidth, ulong &uHeight, bool &fEnabled)
{
    CMachine comMachine = machine();
    ULONG uGuestXOrigin = 0, uGuestYOrigin = 0, uGuestWidth = 0, uGuestHeight = 0;
    BOOL fGuestEnabled = FALSE;
    comMachine.QuerySavedGuestScreenInfo(uScreenId, uGuestXOrigin, uGuestYOrigin, uGuestWidth, uGuestHeight, fGuestEnabled);
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        xOrigin = uGuestXOrigin;
        yOrigin = uGuestYOrigin;
        uWidth = uGuestWidth;
        uHeight = uGuestHeight;
        fEnabled = fGuestEnabled == TRUE;
    }
    return fSuccess;
}

bool UISession::setVideoModeHint(ulong uScreenId, bool fEnabled, bool fChangeOrigin, long xOrigin, long yOrigin,
                                 ulong uWidth, ulong uHeight, ulong uBitsPerPixel, bool fNotify)
{
    CDisplay comDisplay = display();
    comDisplay.SetVideoModeHint(uScreenId, fEnabled, fChangeOrigin, xOrigin, yOrigin,
                                uWidth, uHeight, uBitsPerPixel, fNotify);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::acquireVideoModeHint(ulong uScreenId, bool &fEnabled, bool &fChangeOrigin,
                                     long &xOrigin, long &yOrigin, ulong &uWidth, ulong &uHeight,
                                     ulong &uBitsPerPixel)
{
    CDisplay comDisplay = display();
    BOOL fGuestEnabled = FALSE, fGuestChangeOrigin = FALSE;
    LONG iGuestXOrigin = 0, iGuestYOrigin = 0;
    ULONG uGuestWidth = 0, uGuestHeight = 0, uGuestBitsPerPixel = 0;
    comDisplay.GetVideoModeHint(uScreenId, fGuestEnabled, fGuestChangeOrigin,
                                iGuestXOrigin, iGuestYOrigin, uGuestWidth, uGuestHeight,
                                uGuestBitsPerPixel);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireDisplayParameter(comDisplay);
    else
    {
        fEnabled = fGuestEnabled == TRUE;
        fChangeOrigin = fGuestChangeOrigin == TRUE;
        xOrigin = iGuestXOrigin;
        yOrigin = iGuestYOrigin;
        uWidth = uGuestWidth;
        uHeight = uGuestHeight;
        uBitsPerPixel = uGuestBitsPerPixel;
    }
    return fSuccess;
}

bool UISession::acquireScreenShot(ulong uScreenId, ulong uWidth, ulong uHeight, KBitmapFormat enmFormat, uchar *pBits)
{
    CDisplay comDisplay = display();
    bool fSuccess = false;
    /* For separate process: */
    if (uiCommon().isSeparateProcess())
    {
        /* Take screen-data to array first: */
        const QVector<BYTE> screenData = comDisplay.TakeScreenShotToArray(uScreenId, uWidth, uHeight, enmFormat);
        fSuccess = comDisplay.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireDisplayParameter(comDisplay);
        else
        {
            /* And copy that data to screen-shot if it is Ok: */
            if (!screenData.isEmpty())
                memcpy(pBits, screenData.data(), uWidth * uHeight * 4);
        }
    }
    /* For the same process: */
    else
    {
        /* Take the screen-shot directly: */
        comDisplay.TakeScreenShot(uScreenId, pBits, uWidth, uHeight, enmFormat);
        fSuccess = comDisplay.isOk();
        if (!fSuccess && comDisplay.rc() != VBOX_E_NOT_SUPPORTED) /* VBOX_E_NOT_SUPPORTED: screens size 0x0 can't be snapshotted */
            UINotificationMessage::cannotAcquireDisplayParameter(comDisplay);
    }
    return fSuccess;
}

bool UISession::acquireSavedScreenshotInfo(ulong uScreenId, ulong &uWidth, ulong &uHeight, QVector<KBitmapFormat> &formats)
{
    CMachine comMachine = machine();
    ULONG uGuestWidth = 0, uGuestHeight = 0;
    QVector<KBitmapFormat> guestFormats = comMachine.QuerySavedScreenshotInfo(uScreenId, uGuestWidth, uGuestHeight);
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        uWidth = uGuestWidth;
        uHeight = uGuestHeight;
        formats = guestFormats;
    }
    return fSuccess;
}

bool UISession::acquireSavedScreenshot(ulong uScreenId, KBitmapFormat enmFormat,
                                       ulong &uWidth, ulong &uHeight, QVector<BYTE> &screenshot)
{
    CMachine comMachine = machine();
    ULONG uGuestWidth = 0, uGuestHeight = 0;
    const QVector<BYTE> guestScreenshot = comMachine.ReadSavedScreenshotToArray(uScreenId, enmFormat,
                                                                                uGuestWidth, uGuestHeight);
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        uWidth = uGuestWidth;
        uHeight = uGuestHeight;
        screenshot = guestScreenshot;
    }
    return fSuccess;
}

bool UISession::notifyScaleFactorChange(ulong uScreenId, ulong uScaleFactorWMultiplied, ulong uScaleFactorHMultiplied)
{
    CDisplay comDisplay = display();
    comDisplay.NotifyScaleFactorChange(uScreenId, uScaleFactorWMultiplied, uScaleFactorHMultiplied);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::notifyHiDPIOutputPolicyChange(bool fUnscaledHiDPI)
{
    CDisplay comDisplay = display();
    comDisplay.NotifyHiDPIOutputPolicyChange(fUnscaledHiDPI);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::setSeamlessMode(bool fEnabled)
{
    CDisplay comDisplay = display();
    comDisplay.SetSeamlessMode(fEnabled);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::viewportChanged(ulong uScreenId, ulong xOrigin, ulong yOrigin, ulong uWidth, ulong uHeight)
{
    CDisplay comDisplay = display();
    if (comDisplay.isNull())
        return false;
    comDisplay.ViewportChanged(uScreenId, xOrigin, yOrigin, uWidth, uHeight);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::invalidateAndUpdate()
{
    CDisplay comDisplay = display();
    comDisplay.InvalidateAndUpdate();
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::invalidateAndUpdateScreen(ulong uScreenId)
{
    CDisplay comDisplay = display();
    comDisplay.InvalidateAndUpdateScreen(uScreenId);
    const bool fSuccess = comDisplay.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeDisplayParameter(comDisplay);
    return fSuccess;
}

bool UISession::acquireWhetherVRDEServerPresent(bool &fPresent)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    CVRDEServer comServer = comMachine.GetVRDEServer();
    fPresent = comMachine.isOk() && comServer.isNotNull();
    return true;
}

bool UISession::acquireWhetherVRDEServerEnabled(bool &fEnabled)
{
    CMachine comMachine = machine();
    CVRDEServer comServer = comMachine.GetVRDEServer();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        const BOOL fServerEnabled = comServer.GetEnabled();
        fSuccess = comServer.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireVRDEServerParameter(comServer);
        else
            fEnabled = fServerEnabled == TRUE;
    }
    return fSuccess;
}

bool UISession::setVRDEServerEnabled(bool fEnabled)
{
    CMachine comMachine = machine();
    CVRDEServer comServer = comMachine.GetVRDEServer();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        comServer.SetEnabled(fEnabled);
        fSuccess = comServer.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotToggleVRDEServer(comServer, machineName(), fEnabled);
    }
    return fSuccess;
}

bool UISession::acquireVRDEServerPort(long &iPort)
{
    CConsole comConsole = console();
    const CVRDEServerInfo comVRDEServerInfo = comConsole.GetVRDEServerInfo();
    bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireConsoleParameter(comConsole);
    else
    {
        const LONG iVRDEPort = comVRDEServerInfo.GetPort();
        fSuccess = comVRDEServerInfo.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireVRDEServerInfoParameter(comVRDEServerInfo);
        else
            iPort = iVRDEPort;
    }
    return fSuccess;
}

bool UISession::acquireWhetherRecordingSettingsPresent(bool &fPresent)
{
    CMachine comMachine = machine();
    CRecordingSettings comSettings = comMachine.GetRecordingSettings();
    fPresent = comMachine.isOk() && comSettings.isNotNull();
    return true;
}

bool UISession::acquireWhetherRecordingSettingsEnabled(bool &fEnabled)
{
    CMachine comMachine = machine();
    CRecordingSettings comSettings = comMachine.GetRecordingSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        /** @todo r=andy Revamp this. */
        CProgress comProgress = comSettings.GetProgress();
        fEnabled = comProgress.isOk() && !comProgress.GetCompleted() && !comProgress.GetCanceled();
    }
    return fSuccess;
}

bool UISession::setRecordingSettingsEnabled(bool fEnabled)
{
    CMachine comMachine = machine();
    CRecordingSettings comSettings = comMachine.GetRecordingSettings();
    bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
    {
        /** @todo r=andy Revamp this function to better use the progress object.
         *               Probably also needs a bit of refactoring of the overall handling within FE/Qt. */
        CProgress comProgress;
        if (fEnabled)
        {
            comProgress = comSettings.Start();
            fSuccess = comSettings.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotToggleRecording(comSettings, machineName(), fEnabled);
        }
        else
        {
            comProgress = comSettings.GetProgress();
            if (comProgress.isOk())
                comProgress.Cancel();
            else
                UINotificationMessage::cannotToggleRecording(comSettings, machineName(), fEnabled);
        }

    }
    return fSuccess;
}

bool UISession::acquireDeviceActivity(const QVector<KDeviceType> &deviceTypes, QVector<KDeviceActivity> &states)
{
    CConsole comConsole = console();
    const QVector<KDeviceActivity> currentStates = comConsole.GetDeviceActivity(deviceTypes);
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireConsoleParameter(comConsole);
    else
        states = currentStates;
    return fSuccess;
}

void UISession::acquireHardDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireHardDiskStatusInfo(comMachine, strInfo, cAttachmentsCount);
}

void UISession::acquireOpticalDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount, uint &cAttachmentsMountedCount)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireOpticalDiskStatusInfo(comMachine, strInfo, cAttachmentsCount, cAttachmentsMountedCount);
}

void UISession::acquireFloppyDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount, uint &cAttachmentsMountedCount)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireFloppyDiskStatusInfo(comMachine, strInfo, cAttachmentsCount, cAttachmentsMountedCount);
}

void UISession::acquireAudioStatusInfo(QString &strInfo, bool &fAudioEnabled, bool &fEnabledOutput, bool &fEnabledInput)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireAudioStatusInfo(comMachine, strInfo, fAudioEnabled, fEnabledOutput, fEnabledInput);
}

void UISession::acquireNetworkStatusInfo(QString &strInfo, bool &fAdaptersPresent, bool &fCablesDisconnected)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireNetworkStatusInfo(comMachine, strInfo, fAdaptersPresent, fCablesDisconnected);
}

void UISession::acquireUsbStatusInfo(QString &strInfo, bool &fUsbEnabled, uint &cUsbFilterCount)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    CConsole comConsole = console();
    if (comConsole.isNull())
        return;
    UIDetailsGenerator::acquireUsbStatusInfo(comMachine, comConsole, strInfo, fUsbEnabled, cUsbFilterCount);
}

void UISession::acquireSharedFoldersStatusInfo(QString &strInfo, uint &cFoldersCount)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    CConsole comConsole = console();
    if (comConsole.isNull())
        return;
    CGuest comGuest = guest();
    if (comGuest.isNull())
        return;
    UIDetailsGenerator::acquireSharedFoldersStatusInfo(comMachine, comConsole, comGuest, strInfo, cFoldersCount);
}

void UISession::acquireDisplayStatusInfo(QString &strInfo, uint &uVRAMSize, uint &cMonitorCount, bool &fAcceleration3D)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireDisplayStatusInfo(comMachine, strInfo, uVRAMSize, cMonitorCount, fAcceleration3D);
}

void UISession::acquireRecordingStatusInfo(QString &strInfo, bool &fRecordingEnabled, bool &fMachinePaused)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    fMachinePaused = isPaused();
    UIDetailsGenerator::acquireRecordingStatusInfo(comMachine, strInfo, fRecordingEnabled);
}

void UISession::acquireFeaturesStatusInfo(QString &strInfo, KVMExecutionEngine &enmEngine,
                                          bool fNestedPagingEnabled, bool fUxEnabled,
                                          KParavirtProvider enmProvider)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return;
    UIDetailsGenerator::acquireFeaturesStatusInfo(comMachine, strInfo,
                                                  enmEngine,
                                                  fNestedPagingEnabled, fUxEnabled,
                                                  enmProvider);
}

void UISession::generateMachineInformationGeneral(const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions,
                                                  UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationGeneral(comMachine, fOptions);
}

void UISession::generateMachineInformationSystem(const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &fOptions,
                                                 UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationSystem(comMachine, fOptions);
}

void UISession::generateMachineInformationDisplay(const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &fOptions,
                                                  UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationDisplay(comMachine, fOptions);
}

void UISession::generateMachineInformationStorage(const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &fOptions,
                                                  UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationStorage(comMachine, fOptions);
}

void UISession::generateMachineInformationAudio(const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &fOptions,
                                                UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationAudio(comMachine, fOptions);
}

void UISession::generateMachineInformationNetwork(const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &fOptions,
                                                  UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationNetwork(comMachine, fOptions);
}
void UISession::generateMachineInformationSerial(const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &fOptions,
                                                 UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationSerial(comMachine, fOptions);
}

void UISession::generateMachineInformationUSB(const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &fOptions,
                                              UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationUSB(comMachine, fOptions);
}

void UISession::generateMachineInformationSharedFolders(const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &fOptions,
                                                        UITextTable &returnTable)
{
    CMachine comMachine = machine();
    returnTable = UIDetailsGenerator::generateMachineInformationSharedFolders(comMachine, fOptions);
}

bool UISession::setLogEnabled(bool fEnabled)
{
    CMachineDebugger comDebugger = debugger();
    comDebugger.SetLogEnabled(fEnabled ? TRUE : FALSE);
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotChangeMachineDebuggerParameter(comDebugger);
    return fSuccess;
}

bool UISession::acquireWhetherLogEnabled(bool &fEnabled)
{
    CMachineDebugger comDebugger = debugger();
    if (comDebugger.isNull())
        return false;
    const BOOL fLogEnabled = comDebugger.GetLogEnabled();
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineDebuggerParameter(comDebugger);
    else
        fEnabled = fLogEnabled == TRUE;
    return fSuccess;
}

bool UISession::acquireLogFolder(QString &strFolder)
{
    CMachine comMachine = machine();
    const QString strLogFolder = comMachine.GetLogFolder();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
        strFolder = strLogFolder;
    return fSuccess;
}

bool UISession::acquireEffectiveParavirtProvider(KParavirtProvider &enmProvider)
{
    CMachine comMachine = machine();
    if (comMachine.isNull())
        return false;
    const KParavirtProvider enmParavirtProvider = comMachine.GetEffectiveParavirtProvider();
    const bool fSuccess = comMachine.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineParameter(comMachine);
    else
        enmProvider = enmParavirtProvider;
    return fSuccess;
}

bool UISession::acquireExecutionEngineType(KVMExecutionEngine &enmType)
{
    CMachineDebugger comDebugger = debugger();
    if (comDebugger.isNull())
        return false;
    const KVMExecutionEngine enmEngineType = comDebugger.GetExecutionEngine();
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineDebuggerParameter(comDebugger);
    else
        enmType = enmEngineType;
    return fSuccess;
}

bool UISession::acquireWhetherHwVirtExNestedPagingEnabled(bool &fEnabled)
{
    CMachineDebugger comDebugger = debugger();
    if (comDebugger.isNull())
        return false;
    const BOOL fFeatureEnabled = comDebugger.GetHWVirtExNestedPagingEnabled();
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineDebuggerParameter(comDebugger);
    else
        fEnabled = fFeatureEnabled == TRUE;
    return fSuccess;
}

bool UISession::acquireWhetherHwVirtExUXEnabled(bool &fEnabled)
{
    CMachineDebugger comDebugger = debugger();
    if (comDebugger.isNull())
        return false;
    const BOOL fFeatureEnabled = comDebugger.GetHWVirtExUXEnabled();
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineDebuggerParameter(comDebugger);
    else
        fEnabled = fFeatureEnabled == TRUE;
    return fSuccess;
}

bool UISession::acquireEffectiveCPULoad(ulong &uLoad)
{
    CMachineDebugger comDebugger = debugger();
    ULONG uPctExecuting;
    ULONG uPctHalted;
    ULONG uPctOther;
    comDebugger.GetCPULoad(0x7fffffff, uPctExecuting, uPctHalted, uPctOther);
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineDebuggerParameter(comDebugger);
    else
        uLoad = uPctExecuting + uPctOther;
    return fSuccess;
}

bool UISession::acquireUptime(LONG64 &iUpTime)
{
    CMachineDebugger comDebugger = debugger();
    const LONG64 iGuestUpTime = comDebugger.GetUptime();
    const bool fSuccess = comDebugger.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireMachineDebuggerParameter(comDebugger);
    else
        iUpTime = iGuestUpTime;
    return fSuccess;
}

#ifdef VBOX_WITH_DEBUGGER_GUI
bool UISession::dbgCreated(void *pActionDebug)
{
    if (m_pDbgGui)
        return true;

    RTLDRMOD hLdrMod = uiCommon().getDebuggerModule();
    if (hLdrMod == NIL_RTLDRMOD)
        return false;

    PFNDBGGUICREATE pfnGuiCreate;
    int rc = RTLdrGetSymbol(hLdrMod, "DBGGuiCreate", (void**)&pfnGuiCreate);
    if (RT_SUCCESS(rc))
    {
        ISession *pISession = session().raw();
        rc = pfnGuiCreate(pISession, &m_pDbgGui, &m_pDbgGuiVT);
        if (RT_SUCCESS(rc))
        {
            if (   DBGGUIVT_ARE_VERSIONS_COMPATIBLE(m_pDbgGuiVT->u32Version, DBGGUIVT_VERSION)
                || m_pDbgGuiVT->u32EndVersion == m_pDbgGuiVT->u32Version)
            {
                m_pDbgGuiVT->pfnSetParent(m_pDbgGui, activeMachineWindow());
                m_pDbgGuiVT->pfnSetMenu(m_pDbgGui, pActionDebug);
                dbgAdjustRelativePos();
                return true;
            }

            LogRel(("GUI: DBGGuiCreate failed, incompatible versions (loaded %#x/%#x, expected %#x)\n",
                    m_pDbgGuiVT->u32Version, m_pDbgGuiVT->u32EndVersion, DBGGUIVT_VERSION));
        }
        else
            LogRel(("GUI: DBGGuiCreate failed, rc=%Rrc\n", rc));
    }
    else
        LogRel(("GUI: RTLdrGetSymbol(,\"DBGGuiCreate\",) -> %Rrc\n", rc));

    m_pDbgGui = 0;
    m_pDbgGuiVT = 0;
    return false;
}

void UISession::dbgDestroy()
{
    if (m_pDbgGui)
    {
        m_pDbgGuiVT->pfnDestroy(m_pDbgGui);
        m_pDbgGui = 0;
        m_pDbgGuiVT = 0;
    }
}

void UISession::dbgShowStatistics()
{
    const QByteArray &filterBytes = uiCommon().getDebuggerStatisticsFilter().toUtf8();
    const QByteArray &expandBytes = uiCommon().getDebuggerStatisticsExpand().toUtf8();
    const QByteArray &configBytes = uiCommon().getDebuggerStatisticsConfig().toUtf8();
    m_pDbgGuiVT->pfnShowStatistics(m_pDbgGui, filterBytes.constData(), expandBytes.constData(), configBytes.constData());
}

void UISession::dbgShowCommandLine()
{
    m_pDbgGuiVT->pfnShowCommandLine(m_pDbgGui);
}

void UISession::dbgAdjustRelativePos()
{
    if (m_pDbgGui)
    {
        const QRect rct = activeMachineWindow()->frameGeometry();
        m_pDbgGuiVT->pfnAdjustRelativePos(m_pDbgGui, rct.x(), rct.y(), rct.width(), rct.height());
    }
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

bool UISession::acquireWhetherGuestEnteredACPIMode(bool &fEntered)
{
    CConsole comConsole = console();
    const BOOL fGuestEntered = comConsole.GetGuestEnteredACPIMode();
    const bool fSuccess = comConsole.isOk();
    if (!fSuccess)
        UINotificationMessage::cannotAcquireConsoleParameter(comConsole);
    else
        fEntered = fGuestEntered == TRUE;
    return fSuccess;
}

void UISession::saveState()
{
    if (   isPaused()
        || (isRunning() && pause()))
    {
        /* Enable 'manual-override',
         * preventing automatic Runtime UI closing: */
        uimachine()->setManualOverrideMode(true);

        /* Now, do the magic: */
        LogRel(("GUI: Saving VM state..\n"));
        UINotificationProgressMachineSaveState *pNotification =
            new UINotificationProgressMachineSaveState(machine());
        connect(pNotification, &UINotificationProgressMachineSaveState::sigMachineStateSaved,
                this, &UISession::sltHandleMachineStateSaved);
        gpNotificationCenter->append(pNotification);
    }
}

void UISession::shutdown()
{
    /* Check whether guest is in proper mode: */
    bool fValidMode = false;
    acquireWhetherGuestEnteredACPIMode(fValidMode);
    if (!fValidMode)
        UINotificationMessage::cannotSendACPIToMachine();
    else
    {
        /* Now, do the magic: */
        LogRel(("GUI: Sending ACPI shutdown signal..\n"));
        CConsole comConsole = console();
        comConsole.PowerButton();
        if (!comConsole.isOk())
            UINotificationMessage::cannotACPIShutdownMachine(comConsole);
    }
}

void UISession::powerOff(bool fIncludingDiscard)
{
    /* Enable 'manual-override',
     * preventing automatic Runtime UI closing: */
    uimachine()->setManualOverrideMode(true);

    /* Now, do the magic: */
    LogRel(("GUI: Powering VM off..\n"));
    UINotificationProgressMachinePowerOff *pNotification =
        new UINotificationProgressMachinePowerOff(machine(),
                                                  console(),
                                                  fIncludingDiscard);
    connect(pNotification, &UINotificationProgressMachinePowerOff::sigMachinePoweredOff,
            this, &UISession::sltHandleMachinePoweredOff);
    gpNotificationCenter->append(pNotification);
}

void UISession::sltInstallGuestAdditionsFrom(const QString &strSource)
{
    if (!guestAdditionsUpgradable())
        return sltMountDVDAdHoc(strSource);

    /* Update guest additions automatically: */
    UINotificationProgressGuestAdditionsInstall *pNotification =
            new UINotificationProgressGuestAdditionsInstall(guest(), strSource);
    connect(pNotification, &UINotificationProgressGuestAdditionsInstall::sigGuestAdditionsInstallationFailed,
            this, &UISession::sltMountDVDAdHoc);
    gpNotificationCenter->append(pNotification);
}

void UISession::sltMountDVDAdHoc(const QString &strSource)
{
    mountAdHocImage(KDeviceType_DVD, UIMediumDeviceType_DVD, strSource);
}

void UISession::sltDetachCOM()
{
    /* Cleanup everything COM related: */
    cleanupFramebuffers();
    cleanupConsoleEventHandlers();
    cleanupCOMStuff();
}

void UISession::sltStateChange(KMachineState enmState)
{
    /* Check if something had changed: */
    if (m_enmMachineState != enmState)
    {
        /* Store new data: */
        m_enmMachineStatePrevious = m_enmMachineState;
        m_enmMachineState = enmState;

        /* Notify listeners about machine state changed: */
        emit sigMachineStateChange();
    }
}

void UISession::sltAdditionsChange()
{
    /* Acquire actual states: */
    const ulong ulGuestAdditionsRunLevel = guest().GetAdditionsRunLevel();
    LONG64 iLastUpdatedIgnored;
    const bool fIsGuestSupportsGraphics = guest().GetFacilityStatus(KAdditionsFacilityType_Graphics, iLastUpdatedIgnored)
                                        == KAdditionsFacilityStatus_Active;
    const bool fIsGuestSupportsSeamless = guest().GetFacilityStatus(KAdditionsFacilityType_Seamless, iLastUpdatedIgnored)
                                        == KAdditionsFacilityStatus_Active;

    /* Check if something had changed: */
    if (   m_ulGuestAdditionsRunLevel != ulGuestAdditionsRunLevel
        || m_fIsGuestSupportsGraphics != fIsGuestSupportsGraphics
        || m_fIsGuestSupportsSeamless != fIsGuestSupportsSeamless)
    {
        /* Store new data: */
        m_ulGuestAdditionsRunLevel = ulGuestAdditionsRunLevel;
        m_fIsGuestSupportsGraphics = fIsGuestSupportsGraphics;
        m_fIsGuestSupportsSeamless = fIsGuestSupportsSeamless;

        /* Notify listeners about GA state really changed: */
        LogRel(("GUI: UISession::sltAdditionsChange: GA state really changed, notifying listeners.\n"));
        emit sigAdditionsStateActualChange();
    }
    else
        LogRel(("GUI: UISession::sltAdditionsChange: GA state doesn't really changed, still notifying listeners.\n"));

    /* Notify listeners about GA state change event came: */
    emit sigAdditionsStateChange();
}

void UISession::sltClipboardError(const QString &strMsg)
{
    UINotificationMessage::warnAboutClipboardError(strMsg);
}

void UISession::sltHandleMachineStateSaved(bool fSuccess)
{
    /* Let user try again if saving failed: */
    if (!fSuccess)
    {
        /* Disable 'manual-override' finally: */
        uimachine()->setManualOverrideMode(false);
    }
    /* Close Runtime UI otherwise: */
    else
        uimachine()->closeRuntimeUI();
}

void UISession::sltHandleMachinePoweredOff(bool fSuccess, bool fIncludingDiscard)
{
    /* Let user try again if power off failed: */
    if (!fSuccess)
    {
        /* Disable 'manual-override' finally: */
        uimachine()->setManualOverrideMode(false);
    }
    /* Check for other tasks otherwise: */
    else
    {
        if (fIncludingDiscard)
        {
            /* Now, do more magic! */
            UINotificationProgressSnapshotRestore *pNotification =
                new UINotificationProgressSnapshotRestore(uiCommon().managedVMUuid());
            connect(pNotification, &UINotificationProgressSnapshotRestore::sigSnapshotRestored,
                    this, &UISession::sltHandleSnapshotRestored);
            gpNotificationCenter->append(pNotification);
        }
        else
            uimachine()->closeRuntimeUI();
    }
}

void UISession::sltHandleSnapshotRestored(bool)
{
    /* Close Runtime UI independent of snapshot restoring state: */
    uimachine()->closeRuntimeUI();
}

bool UISession::prepareCOMStuff()
{
    /* Open session: */
    m_comSession = openSession(uiCommon().managedVMUuid(),
                                 uiCommon().isSeparateProcess()
                               ? KLockType_Shared
                               : KLockType_VM);
    if (m_comSession.isNull())
        return false;

    /* Get machine: */
    m_comMachine = m_comSession.GetMachine();
    if (m_comMachine.isNull())
        return false;

    /* Get console: */
    m_comConsole = m_comSession.GetConsole();
    if (m_comConsole.isNull())
        return false;

    /* Get display: */
    m_comDisplay = m_comConsole.GetDisplay();
    if (m_comDisplay.isNull())
        return false;

    /* Get guest: */
    m_comGuest = m_comConsole.GetGuest();
    if (m_comGuest.isNull())
        return false;

    /* Get mouse: */
    m_comMouse = m_comConsole.GetMouse();
    if (m_comMouse.isNull())
        return false;

    /* Get keyboard: */
    m_comKeyboard = m_comConsole.GetKeyboard();
    if (m_comKeyboard.isNull())
        return false;

    /* Get debugger: */
    m_comDebugger = m_comConsole.GetDebugger();
    if (m_comDebugger.isNull())
        return false;

    /* Update machine attributes: */
    m_strMachineName = machine().GetName();
    m_strOSTypeId = machine().GetOSTypeId();

    /* Update machine-state: */
    m_enmMachineState = machine().GetState();

    /* True by default: */
    return true;
}

void UISession::prepareConsoleEventHandlers()
{
    /* Create console event-handler: */
    m_pConsoleEventhandler = new UIConsoleEventHandler(this);

    /* Console event connections: */
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigAdditionsChange,
            this, &UISession::sltAdditionsChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigAudioAdapterChange,
            this, &UISession::sigAudioAdapterChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigClipboardModeChange,
            this, &UISession::sigClipboardModeChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigClipboardError,
            this, &UISession::sltClipboardError);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigCPUExecutionCapChange,
            this, &UISession::sigCPUExecutionCapChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigDnDModeChange,
            this, &UISession::sigDnDModeChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigGuestMonitorChange,
            this, &UISession::sigGuestMonitorChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigMediumChange,
            this, &UISession::sigMediumChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigNetworkAdapterChange,
            this, &UISession::sigNetworkAdapterChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigRecordingChange,
            this, &UISession::sigRecordingChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigSharedFolderChange,
            this, &UISession::sigSharedFolderChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigStateChange,
            this, &UISession::sltStateChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigStorageDeviceChange,
            this, &UISession::sigStorageDeviceChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigUSBControllerChange,
            this, &UISession::sigUSBControllerChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigUSBDeviceStateChange,
            this, &UISession::sigUSBDeviceStateChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigVRDEChange,
            this, &UISession::sigVRDEChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigRuntimeError,
            this, &UISession::sigRuntimeError);

#ifdef VBOX_WS_MAC
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigShowWindow,
            this, &UISession::sigShowWindows, Qt::QueuedConnection);
#endif

    /* Console keyboard connections: */
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigKeyboardLedsChange,
            this, &UISession::sigKeyboardLedsChange);

    /* Console mouse connections: */
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigMousePointerShapeChange,
            this, &UISession::sigMousePointerShapeChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigMouseCapabilityChange,
            this, &UISession::sigMouseCapabilityChange);
    connect(m_pConsoleEventhandler, &UIConsoleEventHandler::sigCursorPositionChange,
            this, &UISession::sigCursorPositionChange);
}

void UISession::prepareFramebuffers()
{
    /* Acquire guest-screen count: */
    ulong cGuestScreenCount = 0;
    acquireMonitorCount(cGuestScreenCount);
    m_frameBufferVector.resize(cGuestScreenCount);

    /* Create new frame-buffers: */
    for (int iIndex = 0; iIndex < m_frameBufferVector.size(); ++iIndex)
        m_frameBufferVector[iIndex] = new UIFrameBuffer;
}

void UISession::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToDetachCOM, this, &UISession::sltDetachCOM);
}

void UISession::prepareSignalHandling()
{
#ifdef VBOX_GUI_WITH_KEYS_RESET_HANDLER
    struct sigaction sa;
    sa.sa_sigaction = &signalHandlerSIGUSR1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
#endif /* VBOX_GUI_WITH_KEYS_RESET_HANDLER */
}

void UISession::cleanupFramebuffers()
{
    /* Cleanup framebuffers finally: */
    for (int i = m_frameBufferVector.size() - 1; i >= 0; --i)
    {
        UIFrameBuffer *pFrameBuffer = m_frameBufferVector[i];
        if (pFrameBuffer)
        {
            /* Mark framebuffer as unused: */
            pFrameBuffer->setMarkAsUnused(true);
            /* Detach framebuffer from Display: */
            pFrameBuffer->detach();
            /* Delete framebuffer reference: */
            delete pFrameBuffer;
        }
    }
    m_frameBufferVector.clear();
}

void UISession::cleanupConsoleEventHandlers()
{
    /* Destroy console event-handler: */
    delete m_pConsoleEventhandler;
    m_pConsoleEventhandler = 0;
}

void UISession::cleanupCOMStuff()
{
    /* Detach debugger: */
    if (!m_comDebugger.isNull())
        m_comDebugger.detach();

    /* Detach keyboard: */
    if (!m_comKeyboard.isNull())
        m_comKeyboard.detach();

    /* Detach mouse: */
    if (!m_comMouse.isNull())
        m_comMouse.detach();

    /* Detach guest: */
    if (!m_comGuest.isNull())
        m_comGuest.detach();

    /* Detach display: */
    if (!m_comDisplay.isNull())
        m_comDisplay.detach();

    /* Detach console: */
    if (!m_comConsole.isNull())
        m_comConsole.detach();

    /* Detach machine: */
    if (!m_comMachine.isNull())
        m_comMachine.detach();

    /* Close session: */
    if (!m_comSession.isNull() && gpGlobalSession->isVBoxSVCAvailable())
    {
        m_comSession.UnlockMachine();
        m_comSession.detach();
    }
}

UIMachineLogic *UISession::machineLogic() const
{
    return uimachine() ? uimachine()->machineLogic() : 0;
}

UIMachineWindow *UISession::activeMachineWindow() const
{
    return machineLogic() ? machineLogic()->activeMachineWindow() : 0;
}

QWidget *UISession::mainMachineWindow() const
{
    return machineLogic() ? machineLogic()->mainMachineWindow() : 0;
}

bool UISession::preprocessInitialization()
{
#ifdef VBOX_WITH_NETFLT
    /* Skip network interface name checks if VM in saved state: */
    if (!isSaved())
    {
        /* Make sure all the attached and enabled network
         * adapters are present on the host. This check makes sense
         * in two cases only - when attachement type is Bridged Network
         * or Host-only Interface. NOTE: Only currently enabled
         * attachement type is checked (incorrect parameters check for
         * currently disabled attachement types is skipped). */
        QStringList failedInterfaceNames;
        QStringList availableInterfaceNames;

        /* Create host network interface names list: */
        foreach (const CHostNetworkInterface &comNetIface, gpGlobalSession->host().GetNetworkInterfaces())
        {
            availableInterfaceNames << comNetIface.GetName();
            availableInterfaceNames << comNetIface.GetShortName();
        }

        /* Enumerate all the virtual network adapters: */
        CPlatform comPlatform             = machine().GetPlatform();
        CPlatformProperties comProperties = gpGlobalSession->virtualBox().GetPlatformProperties(comPlatform.GetArchitecture());
        const ulong cCount = comProperties.GetMaxNetworkAdapters(comPlatform.GetChipsetType());
        for (ulong uAdapterIndex = 0; uAdapterIndex < cCount; ++uAdapterIndex)
        {
            CNetworkAdapter comNetworkAdapter = machine().GetNetworkAdapter(uAdapterIndex);
            if (comNetworkAdapter.GetEnabled())
            {
                /* Get physical network interface name for
                 * currently enabled network attachement type: */
                QString strInterfaceName;
                switch (comNetworkAdapter.GetAttachmentType())
                {
                    case KNetworkAttachmentType_Bridged:
                        strInterfaceName = comNetworkAdapter.GetBridgedInterface();
                        break;
# ifndef VBOX_WITH_VMNET
                    case KNetworkAttachmentType_HostOnly:
                        strInterfaceName = comNetworkAdapter.GetHostOnlyInterface();
                        break;
# endif /* !VBOX_WITH_VMNET */
                    default:
                        break;
                }

                if (   !strInterfaceName.isEmpty()
                    && !availableInterfaceNames.contains(strInterfaceName))
                {
                    LogRel(("GUI: Invalid network interface found: %s\n", strInterfaceName.toUtf8().constData()));
                    failedInterfaceNames << QString("%1 (adapter %2)").arg(strInterfaceName).arg(uAdapterIndex + 1);
                }
            }
        }

        /* Check if non-existent interfaces found: */
        if (!failedInterfaceNames.isEmpty())
        {
            if (msgCenter().warnAboutNetworkInterfaceNotFound(machineName(), failedInterfaceNames.join(", ")))
                machineLogic()->openNetworkSettingsDialogTheModalWay();
            else
            {
                LogRel(("GUI: Aborting startup due to preprocess initialization issue detected...\n"));
                return false;
            }
        }
    }
#endif /* VBOX_WITH_NETFLT */

    /* Check for USB enumeration warning. Don't return false even if we have a warning: */
    CHost comHost = gpGlobalSession->host();
    if (comHost.GetUSBDevices().isEmpty() && comHost.isWarning())
    {
        /* Do not bitch if USB disabled: */
        if (!machine().GetUSBControllers().isEmpty())
        {
            /* Do not bitch if there are no filters (check if enabled too?): */
            if (!machine().GetUSBDeviceFilters().GetDeviceFilters().isEmpty())
                UINotificationMessage::cannotEnumerateHostUSBDevices(comHost);
        }
    }

    /* True by default: */
    return true;
}

bool UISession::mountAdHocImage(KDeviceType enmDeviceType, UIMediumDeviceType enmMediumType, const QString &strMediumName)
{
    /* Get VBox: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Prepare medium to mount: */
    UIMedium guiMedium;

    /* The 'none' medium name means ejecting what ever is in the drive,
     * in that case => leave the guiMedium variable null. */
    if (strMediumName != "none")
    {
        /* Open the medium: */
        const CMedium comMedium = comVBox.OpenMedium(strMediumName, enmDeviceType, KAccessMode_ReadWrite, false /* fForceNewUuid */);
        if (!comVBox.isOk() || comMedium.isNull())
        {
            UINotificationMessage::cannotOpenMedium(comVBox, strMediumName);
            return false;
        }

        /* Make sure medium ID is valid: */
        const QUuid uMediumId = comMedium.GetId();
        AssertReturn(!uMediumId.isNull(), false);

        /* Try to find UIMedium among cached: */
        guiMedium = gpMediumEnumerator->medium(uMediumId);
        if (guiMedium.isNull())
        {
            /* Cache new one if necessary: */
            guiMedium = UIMedium(comMedium, enmMediumType, KMediumState_Created);
            gpMediumEnumerator->createMedium(guiMedium);
        }
    }

    /* Search for a suitable storage slots: */
    QList<ExactStorageSlot> aFreeStorageSlots;
    QList<ExactStorageSlot> aBusyStorageSlots;
    foreach (const CStorageController &comController, machine().GetStorageControllers())
    {
        foreach (const CMediumAttachment &comAttachment, machine().GetMediumAttachmentsOfController(comController.GetName()))
        {
            /* Look for an optical devices only: */
            if (comAttachment.GetType() == enmDeviceType)
            {
                /* Append storage slot to corresponding list: */
                if (comAttachment.GetMedium().isNull())
                    aFreeStorageSlots << ExactStorageSlot(comController.GetName(), comController.GetBus(),
                                                          comAttachment.GetPort(), comAttachment.GetDevice());
                else
                    aBusyStorageSlots << ExactStorageSlot(comController.GetName(), comController.GetBus(),
                                                          comAttachment.GetPort(), comAttachment.GetDevice());
            }
        }
    }

    /* Make sure at least one storage slot found: */
    QList<ExactStorageSlot> sStorageSlots = aFreeStorageSlots + aBusyStorageSlots;
    if (sStorageSlots.isEmpty())
    {
        UINotificationMessage::cannotMountImage(machineName(), strMediumName);
        return false;
    }

    /* Try to mount medium into first available storage slot: */
    while (!sStorageSlots.isEmpty())
    {
        const ExactStorageSlot storageSlot = sStorageSlots.takeFirst();
        machine().MountMedium(storageSlot.controller, storageSlot.port, storageSlot.device, guiMedium.medium(), false /* force */);
        if (machine().isOk())
            break;
    }

    /* Show error message if necessary: */
    if (!machine().isOk())
    {
        msgCenter().cannotRemountMedium(machine(), guiMedium, true /* mount? */, false /* retry? */, activeMachineWindow());
        return false;
    }

    /* Save machine settings: */
    if (!saveSettings())
        return false;

    /* True by default: */
    return true;
}

void UISession::recacheMachineMedia()
{
    /* Compose a list of machine media: */
    CMediumVector comMedia;

    /* Enumerate all the controllers: */
    foreach (const CStorageController &comController, machine().GetStorageControllers())
    {
        /* Enumerate all the attachments: */
        foreach (const CMediumAttachment &comAttachment, machine().GetMediumAttachmentsOfController(comController.GetName()))
        {
            /* Skip unrelated device types: */
            const KDeviceType enmDeviceType = comAttachment.GetType();
            if (   enmDeviceType != KDeviceType_HardDisk
                && enmDeviceType != KDeviceType_Floppy
                && enmDeviceType != KDeviceType_DVD)
                continue;
            if (   comAttachment.GetIsEjected()
                || comAttachment.GetMedium().isNull())
                continue;
            comMedia.append(comAttachment.GetMedium());
        }
    }

    /* Start media enumeration: */
    gpMediumEnumerator->enumerateMedia(comMedia);
}

/* static */
bool UISession::searchMaxSnapshotIndex(const CMachine &comMachine,
                                       const CSnapshot &comSnapshot,
                                       const QString &strNameTemplate,
                                       ulong &uIndex)
{
    bool fSuccess = true;
    ulong uMaxIndex = 0;
    const QRegularExpression re(QString("^") + strNameTemplate.arg("([0-9]+)") + QString("$"));
    if (!comSnapshot.isNull())
    {
        /* Check current snapshot name: */
        const QString strName = comSnapshot.GetName();
        fSuccess = comSnapshot.isOk();
        if (!fSuccess)
            UINotificationMessage::cannotAcquireSnapshotParameter(comSnapshot);
        else
        {
            const QRegularExpressionMatch mt = re.match(strName);
            if (mt.hasMatch())
            {
                const ulong uFoundIndex = mt.captured(1).toULong();
                uMaxIndex = uFoundIndex > uMaxIndex ? uFoundIndex : uMaxIndex;
            }
            /* Traversing all the snapshot children: */
            QVector<CSnapshot> comSnapshotChildren = comSnapshot.GetChildren();
            fSuccess = comSnapshot.isOk();
            if (!fSuccess)
                UINotificationMessage::cannotAcquireSnapshotParameter(comSnapshot);
            else
                foreach (const CSnapshot &comSnapshotChild, comSnapshotChildren)
                {
                    ulong uMaxIndexOfChildren = 0;
                    fSuccess = searchMaxSnapshotIndex(comMachine, comSnapshotChild, strNameTemplate, uMaxIndexOfChildren);
                    if (!fSuccess)
                        break;
                    uMaxIndex = uMaxIndexOfChildren > uMaxIndex ? uMaxIndexOfChildren : uMaxIndex;
                }
        }
    }
    if (fSuccess)
        uIndex = uMaxIndex;
    return fSuccess;
}

#ifdef VBOX_GUI_WITH_KEYS_RESET_HANDLER
/**
 * Custom signal handler. When switching VTs, we might not get release events
 * for Ctrl-Alt and in case a savestate is performed on the new VT, the VM will
 * be saved with modifier keys stuck. This is annoying enough for introducing
 * this hack.
 */
/* static */
static void signalHandlerSIGUSR1(int sig, siginfo_t * /* pInfo */, void * /*pSecret */)
{
    /* Only SIGUSR1 is interesting: */
    if (sig == SIGUSR1)
        if (gpMachine)
            gpMachine->machineLogic()->keyboardHandler()->releaseAllPressedKeys();
}
#endif /* VBOX_GUI_WITH_KEYS_RESET_HANDLER */
