/* $Id: UIMachine.cpp 109005 2025-03-31 09:41:54Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachine class implementation.
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

/* Qt includes: */
#include <QApplication>
#ifdef VBOX_WS_WIN
# include <QBitmap>
#endif
#ifdef VBOX_WS_MAC
# include <QMenuBar>
# include <QTimer>
#endif

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineWindow.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UISession.h"
#ifdef VBOX_WS_MAC
# include "UICocoaApplication.h"
# include "VBoxUtils-darwin.h"
#endif

/* COM includes: */
#include "CAudioAdapter.h"
#include "CAudioSettings.h"
#include "CConsole.h"
#include "CGraphicsAdapter.h"
#include "CHostVideoInputDevice.h"
#include "CMachine.h"
#include "CMediumAttachment.h"
#include "CNetworkAdapter.h"
#include "CProgress.h"
#include "CRecordingSettings.h"
#include "CSession.h"
#include "CSnapshot.h"
#include "CSystemProperties.h"
#include "CUSBController.h"
#include "CUSBDeviceFilters.h"
#include "CVRDEServer.h"


#ifdef VBOX_WS_MAC
/**
 * MacOS X: Application Services: Core Graphics: Display reconfiguration callback.
 *
 * Notifies UIMachine about @a display configuration change.
 * Corresponding change described by Core Graphics @a flags.
 * Uses UIMachine @a pHandler to process this change.
 *
 * @note Last argument (@a pHandler) must always be valid pointer to UIMachine object.
 * @note Calls for UIMachine::sltHandleHostDisplayAboutToChange() slot if display configuration changed.
 */
void cgDisplayReconfigurationCallback(CGDirectDisplayID display, CGDisplayChangeSummaryFlags flags, void *pHandler)
{
    /* Which flags we are handling? */
    int iHandledFlags = kCGDisplayAddFlag     /* display added */
                        | kCGDisplayRemoveFlag  /* display removed */
                        | kCGDisplaySetModeFlag /* display mode changed */;

    /* Handle 'display-add' case: */
    if (flags & kCGDisplayAddFlag)
        LogRelFlow(("GUI: UIMachine::cgDisplayReconfigurationCallback: Display added.\n"));
    /* Handle 'display-remove' case: */
    else if (flags & kCGDisplayRemoveFlag)
        LogRelFlow(("GUI: UIMachine::cgDisplayReconfigurationCallback: Display removed.\n"));
    /* Handle 'mode-set' case: */
    else if (flags & kCGDisplaySetModeFlag)
        LogRelFlow(("GUI: UIMachine::cgDisplayReconfigurationCallback: Display mode changed.\n"));

    /* Ask handler to process our callback: */
    if (flags & iHandledFlags)
        QTimer::singleShot(0, static_cast<UIMachine*>(pHandler),
                           SLOT(sltHandleHostDisplayAboutToChange()));

    Q_UNUSED(display);
}
#endif /* VBOX_WS_MAC */


/* static */
UIMachine *UIMachine::s_pInstance = 0;

/* static */
bool UIMachine::startMachine()
{
    /* Make sure machine is not created: */
    AssertReturn(!s_pInstance, false);

    /* Restore current snapshot if requested: */
    if (   uiCommon().shouldRestoreCurrentSnapshot()
        || !uiCommon().getSnapshotToRestore().isEmpty())
    {
        /* Create temporary session: */
        CSession comSession = openSession(KLockType_VM);
        if (comSession.isNull())
            return false;

        /* Which VM we operate on? */
        CMachine comMachine = comSession.GetMachine();
        /* Which snapshot are we restoring? */
        CSnapshot comSnapshot = uiCommon().shouldRestoreCurrentSnapshot()
                              ? comMachine.GetCurrentSnapshot()
                              : comMachine.FindSnapshot(uiCommon().getSnapshotToRestore());

        /* Prepare restore-snapshot progress: */
        CProgress comProgress = comMachine.RestoreSnapshot(comSnapshot);
        if (!comMachine.isOk())
            return msgCenter().cannotRestoreSnapshot(comMachine,
                                                     comSnapshot.isOk() ? comSnapshot.GetName()
                                                     /** @todo deal better with snapshot-not found issues: */
                                                     : uiCommon().shouldRestoreCurrentSnapshot() ? "current"
                                                     : uiCommon().getSnapshotToRestore(),
                                                     comMachine.GetName());

        /* Show the snapshot-discarding progress: */
        msgCenter().showModalProgressDialog(comProgress, comMachine.GetName(), ":/progress_snapshot_discard_90px.png");
        if (comProgress.GetResultCode() != 0)
            return msgCenter().cannotRestoreSnapshot(comProgress, comSnapshot.GetName(), comMachine.GetName());

        /* Unlock session finally: */
        comSession.UnlockMachine();

        /* Clear snapshot-restoring request: */
        uiCommon().clearSnapshotRestoreOptions();
    }

    /* For separate process we should launch VM before UI: */
    if (uiCommon().isSeparateProcess())
    {
        /* Get corresponding machine: */
        CMachine machine = gpGlobalSession->virtualBox().FindMachine(uiCommon().managedVMUuid().toString());
        AssertMsgReturn(!machine.isNull(), ("UICommon::managedVMUuid() should have filter that case before!\n"), false);

        /* Try to launch corresponding machine: */
        if (!launchMachine(machine, UILaunchMode_Separate))
            return false;
    }

    /* Try to create machine UI: */
    return create();
}

/* static */
bool UIMachine::create()
{
    /* Make sure machine is not created: */
    AssertReturn(!s_pInstance, false);

    /* Create machine UI: */
    new UIMachine;

    /* Make sure it's prepared: */
    if (!s_pInstance->prepare())
    {
        /* Destroy machine UI otherwise: */
        destroy();
        /* False in that case: */
        return false;
    }

    /* True by default: */
    return true;
}

/* static */
void UIMachine::destroy()
{
    /* Make sure machine is created: */
    if (!s_pInstance)
        return;

    /* Protect versus recursive call: */
    UIMachine *pInstance = s_pInstance;
    s_pInstance = 0;
    /* Cleanup machine UI: */
    pInstance->cleanup();
    /* Destroy machine UI: */
    delete pInstance;
}

UIFrameBuffer *UIMachine::frameBuffer(ulong uScreenId)
{
    return uisession()->frameBuffer(uScreenId);
}

QWidget* UIMachine::activeWindow() const
{
    return   machineLogic() && machineLogic()->activeMachineWindow()
           ? machineLogic()->activeMachineWindow()
           : 0;
}

bool UIMachine::isSessionValid() const
{
    return uisession() ? uisession()->isValid() : false;
}

void UIMachine::asyncChangeVisualState(UIVisualStateType visualState)
{
    emit sigRequestAsyncVisualStateChange(visualState);
}

void UIMachine::setRequestedVisualState(UIVisualStateType visualStateType)
{
    /* Remember requested visual state: */
    m_enmRequestedVisualState = visualStateType;

    /* Save only if it's different from Invalid and from current one: */
    if (   m_enmRequestedVisualState != UIVisualStateType_Invalid
        && gEDataManager->requestedVisualState(uiCommon().managedVMUuid()) != m_enmRequestedVisualState)
        gEDataManager->setRequestedVisualState(m_enmRequestedVisualState, uiCommon().managedVMUuid());
}

UIVisualStateType UIMachine::requestedVisualState() const
{
    return m_enmRequestedVisualState;
}

QString UIMachine::machineName() const
{
    return uisession()->machineName();
}

QString UIMachine::osTypeId() const
{
    return uisession()->osTypeId();
}

void UIMachine::acquireMachinePixmap(const QSize &size, QPixmap &pixmap)
{
    return uisession()->acquireMachinePixmap(size, pixmap);
}

void UIMachine::acquireUserMachineIcon(QIcon &icon)
{
    return uisession()->acquireUserMachineIcon(icon);
}

bool UIMachine::acquireArchitectureType(KPlatformArchitecture &enmType)
{
    return uisession()->acquireArchitectureType(enmType);
}

bool UIMachine::acquireChipsetType(KChipsetType &enmType)
{
    return uisession()->acquireChipsetType(enmType);
}

void UIMachine::updateStateAdditionsActions()
{
    /* Make sure action-pool knows whether GA supports graphics: */
    actionPool()->toRuntime()->setGuestSupportsGraphics(isGuestSupportsGraphics());
    /* Enable/Disable Upgrade Additions action depending on feature status: */
    actionPool()->action(UIActionIndexRT_M_Devices_S_UpgradeGuestAdditions)->setEnabled(uisession()->guestAdditionsUpgradable());
}

void UIMachine::updateStateAudioActions()
{
    /* Make sure Audio adapter is present: */
    bool fAdapterPresent = false;
    acquireWhetherAudioAdapterPresent(fAdapterPresent);
    if (fAdapterPresent)
    {
        /* Check/Uncheck Audio adapter output/input actions depending on features status: */
        bool fAudioOutputEnabled = false;
        bool fAudioInputEnabled = false;
        acquireWhetherAudioAdapterOutputEnabled(fAudioOutputEnabled);
        acquireWhetherAudioAdapterInputEnabled(fAudioInputEnabled);
        actionPool()->action(UIActionIndexRT_M_Devices_M_Audio_T_Output)->blockSignals(true);
        actionPool()->action(UIActionIndexRT_M_Devices_M_Audio_T_Output)->setChecked(fAudioOutputEnabled);
        actionPool()->action(UIActionIndexRT_M_Devices_M_Audio_T_Output)->blockSignals(false);
        actionPool()->action(UIActionIndexRT_M_Devices_M_Audio_T_Input)->blockSignals(true);
        actionPool()->action(UIActionIndexRT_M_Devices_M_Audio_T_Input)->setChecked(fAudioInputEnabled);
        actionPool()->action(UIActionIndexRT_M_Devices_M_Audio_T_Input)->blockSignals(false);
    }
}

void UIMachine::updateStateRecordingAction()
{
    /* Make sure Recording settings present: */
    bool fSettingsPresent = false;
    acquireWhetherRecordingSettingsPresent(fSettingsPresent);
    AssertMsgReturnVoid(fSettingsPresent,
                        ("Recording settings can't be null!\n"));

    /* Check/Uncheck Recording action depending on feature status: */
    bool fSettingsEnabled = false;
    acquireWhetherRecordingSettingsEnabled(fSettingsEnabled);
    actionPool()->action(UIActionIndexRT_M_View_M_Recording_T_Start)->blockSignals(true);
    actionPool()->action(UIActionIndexRT_M_View_M_Recording_T_Start)->setChecked(fSettingsEnabled);
    actionPool()->action(UIActionIndexRT_M_View_M_Recording_T_Start)->blockSignals(false);
}

void UIMachine::updateStateVRDEServerAction()
{
    /* Make sure VRDE server present: */
    bool fServerPresent = false;
    acquireWhetherVRDEServerPresent(fServerPresent);
    AssertMsgReturnVoid(fServerPresent,
                        ("VRDE server can't be null!\n"));

    /* Check/Uncheck VRDE Server action depending on feature status: */
    bool fServerEnabled = false;
    acquireWhetherVRDEServerEnabled(fServerEnabled);
    actionPool()->action(UIActionIndexRT_M_View_T_VRDEServer)->blockSignals(true);
    actionPool()->action(UIActionIndexRT_M_View_T_VRDEServer)->setChecked(fServerEnabled);
    actionPool()->action(UIActionIndexRT_M_View_T_VRDEServer)->blockSignals(false);
}

KMachineState UIMachine::machineStatePrevious() const
{
    return uisession()->machineStatePrevious();
}

KMachineState UIMachine::machineState() const
{
    return uisession()->machineState();
}

void UIMachine::forgetPreviousMachineState()
{
    uisession()->forgetPreviousMachineState();
}

bool UIMachine::acquireLiveMachineState(KMachineState &enmState)
{
    return uisession()->acquireLiveMachineState(enmState);
}

bool UIMachine::isTurnedOff() const
{
    return uisession()->isTurnedOff();
}

bool UIMachine::isPaused() const
{
    return uisession()->isPaused();
}

bool UIMachine::wasPaused() const
{
    return uisession()->wasPaused();
}

bool UIMachine::isRunning() const
{
    return uisession()->isRunning();
}

bool UIMachine::isStuck() const
{
    return uisession()->isStuck();
}

bool UIMachine::isGuestScreenUnDrawable() const
{
    return uisession()->isGuestScreenUnDrawable();
}

bool UIMachine::reset()
{
    return uisession()->reset();
}

bool UIMachine::pause()
{
    return uisession()->pause();
}

bool UIMachine::unpause()
{
    return uisession()->unpause();
}

bool UIMachine::setPause(bool fPause)
{
    return uisession()->setPause(fPause);
}

bool UIMachine::acquireSettingsFilePath(QString &strPath)
{
    return uisession()->acquireSettingsFilePath(strPath);
}

bool UIMachine::saveSettings()
{
    return uisession()->saveSettings();
}

bool UIMachine::acquireSnapshotCount(ulong &uCount)
{
    return uisession()->acquireSnapshotCount(uCount);
}

bool UIMachine::acquireCurrentSnapshotName(QString &strName)
{
    return uisession()->acquireCurrentSnapshotName(strName);
}

bool UIMachine::acquireMaxSnapshotIndex(const QString &strNameTemplate, ulong &uIndex)
{
    return uisession()->acquireMaxSnapshotIndex(strNameTemplate, uIndex);
}

void UIMachine::takeSnapshot(const QString &strName, const QString &strDescription)
{
    return uisession()->takeSnapshot(strName, strDescription);
}

bool UIMachine::acquireWhetherAudioAdapterPresent(bool &fPresent)
{
    return uisession()->acquireWhetherAudioAdapterPresent(fPresent);
}

bool UIMachine::acquireWhetherAudioAdapterEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherAudioAdapterEnabled(fEnabled);
}

bool UIMachine::acquireWhetherAudioAdapterOutputEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherAudioAdapterOutputEnabled(fEnabled);
}

bool UIMachine::acquireWhetherAudioAdapterInputEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherAudioAdapterInputEnabled(fEnabled);
}

bool UIMachine::setAudioAdapterOutputEnabled(bool fEnabled)
{
    return uisession()->setAudioAdapterOutputEnabled(fEnabled);
}

bool UIMachine::setAudioAdapterInputEnabled(bool fEnabled)
{
    return uisession()->setAudioAdapterInputEnabled(fEnabled);
}

bool UIMachine::isScreenVisibleHostDesires(ulong uScreenId) const
{
    /* Make sure index feats the bounds: */
    AssertReturn(uScreenId < (ulong)m_guestScreenVisibilityVectorHostDesires.size(), false);

    /* Return 'actual' (host-desire) visibility status: */
    return m_guestScreenVisibilityVectorHostDesires.value((int)uScreenId);
}

void UIMachine::setScreenVisibleHostDesires(ulong uScreenId, bool fIsMonitorVisible)
{
    /* Make sure index feats the bounds: */
    AssertReturnVoid(uScreenId < (ulong)m_guestScreenVisibilityVectorHostDesires.size());

    /* Remember 'actual' (host-desire) visibility status: */
    m_guestScreenVisibilityVectorHostDesires[(int)uScreenId] = fIsMonitorVisible;

    /* And remember the request in extra data for guests with VMSVGA: */
    /* This should be done before the actual hint is sent in case the guest overrides it. */
    gEDataManager->setLastGuestScreenVisibilityStatus(uScreenId, fIsMonitorVisible, uiCommon().managedVMUuid());
}

bool UIMachine::isScreenVisible(ulong uScreenId) const
{
    /* Make sure index feats the bounds: */
    AssertReturn(uScreenId < (ulong)m_guestScreenVisibilityVector.size(), false);

    /* Return 'actual' visibility status: */
    return m_guestScreenVisibilityVector.value((int)uScreenId);
}

void UIMachine::setScreenVisible(ulong uScreenId, bool fIsMonitorVisible)
{
    /* Make sure index feats the bounds: */
    AssertReturnVoid(uScreenId < (ulong)m_guestScreenVisibilityVector.size());

    /* Remember 'actual' visibility status: */
    m_guestScreenVisibilityVector[(int)uScreenId] = fIsMonitorVisible;
    /* Remember 'desired' visibility status: */
    // See note in UIMachineView::sltHandleNotifyChange() regarding the graphics controller check. */
    KGraphicsControllerType enmType = KGraphicsControllerType_Null;
    acquireGraphicsControllerType(enmType);
    if (enmType != KGraphicsControllerType_VMSVGA)
        gEDataManager->setLastGuestScreenVisibilityStatus(uScreenId, fIsMonitorVisible, uiCommon().managedVMUuid());

    /* Make sure action-pool knows guest-screen visibility status: */
    actionPool()->toRuntime()->setGuestScreenVisible(uScreenId, fIsMonitorVisible);
}

int UIMachine::countOfVisibleWindows()
{
    int cCountOfVisibleWindows = 0;
    for (int i = 0; i < m_guestScreenVisibilityVector.size(); ++i)
        if (m_guestScreenVisibilityVector[i])
            ++cCountOfVisibleWindows;
    return cCountOfVisibleWindows;
}

QList<int> UIMachine::listOfVisibleWindows() const
{
    QList<int> visibleWindows;
    for (int i = 0; i < m_guestScreenVisibilityVector.size(); ++i)
        if (m_guestScreenVisibilityVector.at(i))
            visibleWindows.push_back(i);
    return visibleWindows;
}

QSize UIMachine::guestScreenSize(ulong uScreenId) const
{
    return uisession()->frameBufferSize(uScreenId);
}

QSize UIMachine::lastFullScreenSize(ulong uScreenId) const
{
    /* Make sure index fits the bounds: */
    AssertReturn(uScreenId < (ulong)m_monitorLastFullScreenSizeVector.size(), QSize(-1, -1));

    /* Return last full-screen size: */
    return m_monitorLastFullScreenSizeVector.value((int)uScreenId);
}

void UIMachine::setLastFullScreenSize(ulong uScreenId, QSize size)
{
    /* Make sure index fits the bounds: */
    AssertReturnVoid(uScreenId < (ulong)m_monitorLastFullScreenSizeVector.size());

    /* Remember last full-screen size: */
    m_monitorLastFullScreenSizeVector[(int)uScreenId] = size;
}

bool UIMachine::acquireGraphicsControllerType(KGraphicsControllerType &enmType)
{
    return uisession()->acquireGraphicsControllerType(enmType);
}

bool UIMachine::acquireVRAMSize(ulong &uSize)
{
    return uisession()->acquireVRAMSize(uSize);
}

bool UIMachine::acquireWhetherAccelerate3DEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherAccelerate3DEnabled(fEnabled);
}

bool UIMachine::acquireMonitorCount(ulong &uCount)
{
    return uisession()->acquireMonitorCount(uCount);
}

bool UIMachine::acquireGuestScreenParameters(ulong uScreenId,
                                             ulong &uWidth, ulong &uHeight, ulong &uBitsPerPixel,
                                             long &xOrigin, long &yOrigin, KGuestMonitorStatus &enmMonitorStatus)
{
    return uisession()->acquireGuestScreenParameters(uScreenId,
                                                     uWidth, uHeight, uBitsPerPixel,
                                                     xOrigin, yOrigin, enmMonitorStatus);
}

bool UIMachine::acquireSavedGuestScreenInfo(ulong uScreenId,
                                            long &xOrigin, long &yOrigin,
                                            ulong &uWidth, ulong &uHeight, bool &fEnabled)
{
    return uisession()->acquireSavedGuestScreenInfo(uScreenId,
                                                    xOrigin, yOrigin,
                                                    uWidth, uHeight, fEnabled);
}

bool UIMachine::setVideoModeHint(ulong uScreenId, bool fEnabled, bool fChangeOrigin,
                                 long xOrigin, long yOrigin, ulong uWidth, ulong uHeight,
                                 ulong uBitsPerPixel, bool fNotify)
{
    return uisession()->setVideoModeHint(uScreenId, fEnabled, fChangeOrigin,
                                         xOrigin, yOrigin, uWidth, uHeight,
                                         uBitsPerPixel, fNotify);
}

bool UIMachine::acquireVideoModeHint(ulong uScreenId, bool &fEnabled, bool &fChangeOrigin,
                                     long &xOrigin, long &yOrigin, ulong &uWidth, ulong &uHeight,
                                     ulong &uBitsPerPixel)
{
    return uisession()->acquireVideoModeHint(uScreenId, fEnabled, fChangeOrigin,
                                             xOrigin, yOrigin, uWidth, uHeight,
                                             uBitsPerPixel);
}

bool UIMachine::acquireScreenShot(ulong uScreenId, ulong uWidth, ulong uHeight, KBitmapFormat enmFormat, uchar *pBits)
{
    return uisession()->acquireScreenShot(uScreenId, uWidth, uHeight, enmFormat, pBits);
}

bool UIMachine::acquireSavedScreenshotInfo(ulong uScreenId, ulong &uWidth, ulong &uHeight, QVector<KBitmapFormat> &formats)
{
    return uisession()->acquireSavedScreenshotInfo(uScreenId, uWidth, uHeight, formats);
}

bool UIMachine::acquireSavedScreenshot(ulong uScreenId, KBitmapFormat enmFormat,
                                       ulong &uWidth, ulong &uHeight, QVector<BYTE> &screenshot)
{
    return uisession()->acquireSavedScreenshot(uScreenId, enmFormat,
                                               uWidth, uHeight, screenshot);
}

bool UIMachine::notifyScaleFactorChange(ulong uScreenId, ulong uScaleFactorWMultiplied, ulong uScaleFactorHMultiplied)
{
    return uisession()->notifyScaleFactorChange(uScreenId, uScaleFactorWMultiplied, uScaleFactorHMultiplied);
}

bool UIMachine::notifyHiDPIOutputPolicyChange(bool fUnscaledHiDPI)
{
    return uisession()->notifyHiDPIOutputPolicyChange(fUnscaledHiDPI);
}

bool UIMachine::setSeamlessMode(bool fEnabled)
{
    return uisession()->setSeamlessMode(fEnabled);
}

bool UIMachine::viewportChanged(ulong uScreenId, ulong xOrigin, ulong yOrigin, ulong uWidth, ulong uHeight)
{
    return uisession()->viewportChanged(uScreenId, xOrigin, yOrigin, uWidth, uHeight);
}

bool UIMachine::invalidateAndUpdate()
{
    return uisession()->invalidateAndUpdate();
}

bool UIMachine::invalidateAndUpdateScreen(ulong uScreenId)
{
    return uisession()->invalidateAndUpdateScreen(uScreenId);
}

bool UIMachine::acquireWhetherVRDEServerPresent(bool &fPresent)
{
    return uisession()->acquireWhetherVRDEServerPresent(fPresent);
}

bool UIMachine::acquireWhetherVRDEServerEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherVRDEServerEnabled(fEnabled);
}

bool UIMachine::setVRDEServerEnabled(bool fEnabled)
{
    return uisession()->setVRDEServerEnabled(fEnabled);
}

bool UIMachine::acquireVRDEServerPort(long &iPort)
{
    return uisession()->acquireVRDEServerPort(iPort);
}

bool UIMachine::acquireWhetherRecordingSettingsPresent(bool &fPresent)
{
    return uisession()->acquireWhetherRecordingSettingsPresent(fPresent);
}

bool UIMachine::acquireWhetherRecordingSettingsEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherRecordingSettingsEnabled(fEnabled);
}

bool UIMachine::setRecordingSettingsEnabled(bool fEnabled)
{
    return uisession()->setRecordingSettingsEnabled(fEnabled);
}

bool UIMachine::isGuestAdditionsActive() const
{
    return uisession()->isGuestAdditionsActive();
}

bool UIMachine::isGuestSupportsGraphics() const
{
    return uisession()->isGuestSupportsGraphics();
}

bool UIMachine::isGuestSupportsSeamless() const
{
    return uisession()->isGuestSupportsSeamless();
}

bool UIMachine::acquireGuestAdditionsVersion(QString &strVersion)
{
    return uisession()->acquireGuestAdditionsVersion(strVersion);
}

bool UIMachine::acquireGuestAdditionsRevision(ulong &uRevision)
{
    return uisession()->acquireGuestAdditionsRevision(uRevision);
}

bool UIMachine::notifyGuiFocusChange(bool fInfocus)
{
    return uisession()->notifyGuiFocusChange(fInfocus);
}

bool UIMachine::putScancode(LONG iCode)
{
    return uisession()->putScancode(iCode);
}

bool UIMachine::putScancodes(const QVector<LONG> &codes)
{
    return uisession()->putScancodes(codes);
}

bool UIMachine::putCAD()
{
    return uisession()->putCAD();
}

bool UIMachine::releaseKeys()
{
    return uisession()->releaseKeys();
}

bool UIMachine::putUsageCode(LONG iUsageCode, LONG iUsagePage, bool fKeyRelease)
{
    return uisession()->putUsageCode(iUsageCode, iUsagePage, fKeyRelease);
}

bool UIMachine::putMouseEvent(long iDx, long iDy, long iDz, long iDw, long iButtonState)
{
    return uisession()->putMouseEvent(iDx, iDy, iDz, iDw, iButtonState);
}

bool UIMachine::putMouseEventAbsolute(long iX, long iY, long iDz, long iDw, long iButtonState)
{
    return uisession()->putMouseEventAbsolute(iX, iY, iDz, iDw, iButtonState);
}

bool UIMachine::putEventMultiTouch(long iCount, const QVector<LONG64> &contacts, bool fIsTouchScreen, ulong uScanTime)
{
    return uisession()->putEventMultiTouch(iCount, contacts, fIsTouchScreen, uScanTime);
}

bool UIMachine::acquireClipboardMode(KClipboardMode &enmMode)
{
    return uisession()->acquireClipboardMode(enmMode);
}

bool UIMachine::setClipboardMode(KClipboardMode enmMode)
{
    return uisession()->setClipboardMode(enmMode);
}

bool UIMachine::toggleClipboardFileTransfer(bool fEnabled)
{
    return uisession()->toggleClipboardFileTransfer(fEnabled);
}

bool UIMachine::isClipboardFileTransferEnabled()
{
    return uisession()->isClipboardFileTransferEnabled();
}

bool UIMachine::acquireDnDMode(KDnDMode &enmMode)
{
    return uisession()->acquireDnDMode(enmMode);
}

bool UIMachine::setDnDMode(KDnDMode enmMode)
{
    return uisession()->setDnDMode(enmMode);
}

bool UIMachine::acquireAmountOfStorageDevices(ulong &cHardDisks, ulong &cOpticalDrives, ulong &cFloppyDrives)
{
    return uisession()->acquireAmountOfStorageDevices(cHardDisks, cOpticalDrives, cFloppyDrives);
}

bool UIMachine::storageDevices(KDeviceType enmDeviceType, QList<StorageDeviceInfo> &guiStorageDevices)
{
    return uisession()->storageDevices(enmDeviceType, guiStorageDevices);
}

bool UIMachine::acquireEncryptedMedia(EncryptedMediumMap &media)
{
    return uisession()->acquireEncryptedMedia(media);
}

bool UIMachine::addEncryptionPassword(const QString &strId, const QString &strPassword, bool fClearOnSuspend)
{
    return uisession()->addEncryptionPassword(strId, strPassword, fClearOnSuspend);
}

bool UIMachine::acquireAmountOfImmutableImages(ulong &cAmount)
{
    return uisession()->acquireAmountOfImmutableImages(cAmount);
}

bool UIMachine::mountBootMedium(const QUuid &uMediumId)
{
    return uisession()->mountBootMedium(uMediumId);
}

void UIMachine::prepareStorageMenu(QMenu *pMenu,
                                   QObject *pListener, const char *pszSlotName,
                                   const QString &strControllerName, const StorageSlot &storageSlot)
{
    return uisession()->prepareStorageMenu(pMenu,
                                           pListener, pszSlotName,
                                           strControllerName, storageSlot);
}

void UIMachine::updateMachineStorage(const UIMediumTarget &target, UIActionPool *pActionPool)
{
    return uisession()->updateMachineStorage(target, pActionPool);
}

void UIMachine::acquireWhetherUSBControllerEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherUSBControllerEnabled(fEnabled);
}

void UIMachine::acquireWhetherVideoInputDevicesEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherVideoInputDevicesEnabled(fEnabled);
}

bool UIMachine::usbDevices(QList<USBDeviceInfo> &guiUSBDevices)
{
    return uisession()->usbDevices(guiUSBDevices);
}

bool UIMachine::attachUSBDevice(const QUuid &uId)
{
    return uisession()->attachUSBDevice(uId);
}

bool UIMachine::detachUSBDevice(const QUuid &uId)
{
    return uisession()->detachUSBDevice(uId);
}

bool UIMachine::webcamDevices(QList<WebcamDeviceInfo> &guiWebcamDevices)
{
    return uisession()->webcamDevices(guiWebcamDevices);
}

bool UIMachine::webcamAttach(const QString &strPath, const QString &strName)
{
    return uisession()->webcamAttach(strPath, strName);
}

bool UIMachine::webcamDetach(const QString &strPath, const QString &strName)
{
    return uisession()->webcamDetach(strPath, strName);
}

bool UIMachine::acquireWhetherNetworkAdapterEnabled(ulong uSlot, bool &fEnabled)
{
    return uisession()->acquireWhetherNetworkAdapterEnabled(uSlot, fEnabled);
}

bool UIMachine::acquireWhetherAtLeastOneNetworkAdapterEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherAtLeastOneNetworkAdapterEnabled(fEnabled);
}

bool UIMachine::acquireWhetherNetworkCableConnected(ulong uSlot, bool &fConnected)
{
    return uisession()->acquireWhetherNetworkCableConnected(uSlot, fConnected);
}

bool UIMachine::setNetworkCableConnected(ulong uSlot, bool fConnected)
{
    return uisession()->setNetworkCableConnected(uSlot, fConnected);
}

bool UIMachine::acquireDeviceActivity(const QVector<KDeviceType> &deviceTypes, QVector<KDeviceActivity> &states)
{
    return uisession()->acquireDeviceActivity(deviceTypes, states);
}

void UIMachine::acquireHardDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount)
{
    uisession()->acquireHardDiskStatusInfo(strInfo, cAttachmentsCount);
}

void UIMachine::acquireOpticalDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount, uint &cAttachmentsMountedCount)
{
    uisession()->acquireOpticalDiskStatusInfo(strInfo, cAttachmentsCount, cAttachmentsMountedCount);
}

void UIMachine::acquireFloppyDiskStatusInfo(QString &strInfo, uint &cAttachmentsCount, uint &cAttachmentsMountedCount)
{
    uisession()->acquireFloppyDiskStatusInfo(strInfo, cAttachmentsCount, cAttachmentsMountedCount);
}

void UIMachine::acquireAudioStatusInfo(QString &strInfo, bool &fAudioEnabled, bool &fEnabledOutput, bool &fEnabledInput)
{
    uisession()->acquireAudioStatusInfo(strInfo, fAudioEnabled, fEnabledOutput, fEnabledInput);
}

void UIMachine::acquireNetworkStatusInfo(QString &strInfo, bool &fAdaptersPresent, bool &fCablesDisconnected)
{
    uisession()->acquireNetworkStatusInfo(strInfo, fAdaptersPresent, fCablesDisconnected);
}

void UIMachine::acquireUsbStatusInfo(QString &strInfo, bool &fUsbEnabled, uint &cUsbFilterCount)
{
    uisession()->acquireUsbStatusInfo(strInfo, fUsbEnabled, cUsbFilterCount);
}

void UIMachine::acquireSharedFoldersStatusInfo(QString &strInfo, uint &cFoldersCount)
{
    uisession()->acquireSharedFoldersStatusInfo(strInfo, cFoldersCount);
}

void UIMachine::acquireDisplayStatusInfo(QString &strInfo, uint &uVRAMSize, uint &cMonitorCount, bool &fAcceleration3D)
{
    uisession()->acquireDisplayStatusInfo(strInfo, uVRAMSize, cMonitorCount, fAcceleration3D);
}

void UIMachine::acquireRecordingStatusInfo(QString &strInfo, bool &fRecordingEnabled, bool &fMachinePaused)
{
    uisession()->acquireRecordingStatusInfo(strInfo, fRecordingEnabled, fMachinePaused);
}

void UIMachine::acquireFeaturesStatusInfo(QString &strInfo, KVMExecutionEngine &enmEngine)
{
    enmEngine = vmExecutionEngine();
    uisession()->acquireFeaturesStatusInfo(strInfo, enmEngine,
                                           isHWVirtExNestedPagingEnabled(),
                                           isHWVirtExUXEnabled(),
                                           paravirtProvider());
}

void UIMachine::generateMachineInformationGeneral(const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions,
                                                  UITextTable &returnTable)
{
    uisession()->generateMachineInformationGeneral(fOptions, returnTable);
}

void UIMachine::generateMachineInformationSystem(const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &fOptions,
                                                 UITextTable &returnTable)
{
    uisession()->generateMachineInformationSystem(fOptions, returnTable);
}

void UIMachine::generateMachineInformationDisplay(const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &fOptions,
                                                  UITextTable &returnTable)
{
    uisession()->generateMachineInformationDisplay(fOptions, returnTable);
}

void UIMachine::generateMachineInformationStorage(const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &fOptions,
                                                  UITextTable &returnTable)
{
    uisession()->generateMachineInformationStorage(fOptions, returnTable);
}

void UIMachine::generateMachineInformationAudio(const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &fOptions,
                                                UITextTable &returnTable)
{
    uisession()->generateMachineInformationAudio(fOptions, returnTable);
}

void UIMachine::generateMachineInformationNetwork(const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &fOptions,
                                                  UITextTable &returnTable)
{
    uisession()->generateMachineInformationNetwork(fOptions, returnTable);
}

void UIMachine::generateMachineInformationSerial(const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &fOptions,
                                                 UITextTable &returnTable)
{
    uisession()->generateMachineInformationSerial(fOptions, returnTable);
}

void UIMachine::generateMachineInformationUSB(const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &fOptions,
                                              UITextTable &returnTable)
{
    uisession()->generateMachineInformationUSB(fOptions, returnTable);
}

void UIMachine::generateMachineInformationSharedFolders(const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &fOptions,
                                                        UITextTable &returnTable)
{
    uisession()->generateMachineInformationSharedFolders(fOptions, returnTable);
}

bool UIMachine::setLogEnabled(bool fEnabled)
{
    return uisession()->setLogEnabled(fEnabled);
}

bool UIMachine::acquireWhetherLogEnabled(bool &fEnabled)
{
    return uisession()->acquireWhetherLogEnabled(fEnabled);
}

bool UIMachine::acquireLogFolder(QString &strFolder)
{
    return uisession()->acquireLogFolder(strFolder);
}

bool UIMachine::acquireEffectiveCPULoad(ulong &uLoad)
{
    return uisession()->acquireEffectiveCPULoad(uLoad);
}

bool UIMachine::acquireUptime(LONG64 &iUpTime)
{
    return uisession()->acquireUptime(iUpTime);
}

#ifdef VBOX_WITH_DEBUGGER_GUI
bool UIMachine::dbgCreated(void *pActionDebug)
{
    return uisession()->dbgCreated(pActionDebug);
}

void UIMachine::dbgDestroy()
{
    return uisession()->dbgDestroy();
}

void UIMachine::dbgShowStatistics()
{
    return uisession()->dbgShowStatistics();
}

void UIMachine::dbgShowCommandLine()
{
    return uisession()->dbgShowCommandLine();
}

void UIMachine::dbgAdjustRelativePos()
{
    return uisession()->dbgAdjustRelativePos();
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

bool UIMachine::acquireWhetherGuestEnteredACPIMode(bool &fEntered)
{
    return uisession()->acquireWhetherGuestEnteredACPIMode(fEntered);
}

void UIMachine::detachUi()
{
    /* Manually close Runtime UI: */
    LogRel(("GUI: Detaching UI..\n"));
    closeRuntimeUI();
}

void UIMachine::saveState()
{
    uisession()->saveState();
}

void UIMachine::shutdown()
{
    uisession()->shutdown();
}

void UIMachine::powerOff(bool fIncludingDiscard)
{
    uisession()->powerOff(fIncludingDiscard);
}

void UIMachine::sltInstallGuestAdditionsFrom(const QString &strSource)
{
    uisession()->sltInstallGuestAdditionsFrom(strSource);
}

void UIMachine::sltMountDVDAdHoc(const QString &strSource)
{
    uisession()->sltMountDVDAdHoc(strSource);
}

void UIMachine::closeRuntimeUI()
{
    /* First, we have to hide any opened modal/popup widgets.
     * They then should unlock their event-loops asynchronously.
     * If all such loops are unlocked, we can close Runtime UI. */
    QWidget *pWidget = QApplication::activeModalWidget()
                     ? QApplication::activeModalWidget()
                     : QApplication::activePopupWidget()
                     ? QApplication::activePopupWidget()
                     : 0;
    if (pWidget)
    {
        /* First we should try to close this widget: */
        pWidget->close();
        /* If widget rejected the 'close-event' we can
         * still hide it and hope it will behave correctly
         * and unlock his event-loop if any: */
        if (!pWidget->isHidden())
            pWidget->hide();
        /* Asynchronously restart this slot: */
        QMetaObject::invokeMethod(this, "closeRuntimeUI", Qt::QueuedConnection);
        return;
    }

    /* Asynchronously ask QApplication to quit: */
    LogRel(("GUI: Request for async QApp quit.\n"));
    m_fQuitRequested = true;
    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
}

void UIMachine::sltChangeVisualState(UIVisualStateType visualState)
{
    /* Create new machine-logic: */
    UIMachineLogic *pMachineLogic = UIMachineLogic::create(this, visualState);

    /* First we have to check if the selected machine-logic is available at all.
     * Only then we delete the old machine-logic and switch to the new one. */
    if (pMachineLogic->checkAvailability())
    {
        /* Delete previous machine-logic if any: */
        UIMachineLogic::destroy(m_pMachineLogic);

        /* Set the new machine-logic as current one: */
        m_pMachineLogic = pMachineLogic;
        m_pMachineLogic->prepare();

        /* Remember new visual state: */
        m_enmVisualState = visualState;

        /* Save requested visual state: */
        gEDataManager->setRequestedVisualState(m_enmVisualState, uiCommon().managedVMUuid());
    }
    else
    {
        /* Delete temporary created machine-logic: */
        UIMachineLogic::destroy(pMachineLogic);
    }

    /* Make sure machine-logic exists: */
    if (!m_pMachineLogic)
    {
        /* Reset initial visual state to normal: */
        m_enmInitialVisualState = UIVisualStateType_Normal;
        /* Enter initial visual state again: */
        enterInitialVisualState();
    }
}

void UIMachine::sltHandleAdditionsActualChange()
{
    updateStateAdditionsActions();
    emit sigAdditionsStateActualChange();
}

void UIMachine::sltHandleAudioAdapterChange()
{
    updateStateAudioActions();
    emit sigAudioAdapterChange();
}

void UIMachine::sltHandleRecordingChange()
{
    updateStateRecordingAction();
    emit sigRecordingChange();
}

void UIMachine::sltHandleStorageDeviceChange(const CMediumAttachment &comAttachment, bool fRemoved, bool fSilent)
{
    updateActionRestrictions();
    emit sigStorageDeviceChange(comAttachment, fRemoved, fSilent);
}

void UIMachine::sltHandleVRDEChange()
{
    updateStateVRDEServerAction();
    emit sigVRDEChange();
}

#ifdef VBOX_WS_MAC
void UIMachine::sltHandleMenuBarConfigurationChange(const QUuid &uMachineID)
{
    /* Skip unrelated machine IDs: */
    if (uiCommon().managedVMUuid() != uMachineID)
        return;

    /* Update Mac OS X menu-bar: */
    updateMenu();
}
#endif /* VBOX_WS_MAC */

void UIMachine::sltHandleHostScreenCountChange()
{
    LogRelFlow(("GUI: UIMachine: Host-screen count changed.\n"));

    /* Recache display data: */
    updateHostScreenData();

    /* Notify current machine-logic: */
    emit sigHostScreenCountChange();
}

void UIMachine::sltHandleHostScreenGeometryChange()
{
    LogRelFlow(("GUI: UIMachine: Host-screen geometry changed.\n"));

    /* Recache display data: */
    updateHostScreenData();

    /* Notify current machine-logic: */
    emit sigHostScreenGeometryChange();
}

void UIMachine::sltHandleHostScreenAvailableAreaChange()
{
    LogRelFlow(("GUI: UIMachine: Host-screen available-area changed.\n"));

    /* Notify current machine-logic: */
    emit sigHostScreenAvailableAreaChange();
}

#ifdef VBOX_WS_MAC
void UIMachine::sltHandleHostDisplayAboutToChange()
{
    LogRelFlow(("GUI: UIMachine::sltHandleHostDisplayAboutToChange()\n"));

    if (m_pWatchdogDisplayChange->isActive())
        m_pWatchdogDisplayChange->stop();
    m_pWatchdogDisplayChange->setProperty("tryNumber", 1);
    m_pWatchdogDisplayChange->start();
}

void UIMachine::sltCheckIfHostDisplayChanged()
{
    LogRelFlow(("GUI: UIMachine::sltCheckIfHostDisplayChanged()\n"));

    /* Check if display count changed: */
    if (UIDesktopWidgetWatchdog::screenCount() != m_hostScreens.size())
    {
        /* Reset watchdog: */
        m_pWatchdogDisplayChange->setProperty("tryNumber", 0);
        /* Notify listeners about screen-count changed: */
        return sltHandleHostScreenCountChange();
    }
    else
    {
        /* Check if at least one display geometry changed: */
        for (int iScreenIndex = 0; iScreenIndex < UIDesktopWidgetWatchdog::screenCount(); ++iScreenIndex)
        {
            if (gpDesktop->screenGeometry(iScreenIndex) != m_hostScreens.at(iScreenIndex))
            {
                /* Reset watchdog: */
                m_pWatchdogDisplayChange->setProperty("tryNumber", 0);
                /* Notify listeners about screen-geometry changed: */
                return sltHandleHostScreenGeometryChange();
            }
        }
    }

    /* Check if watchdog expired, restart if not: */
    int cTryNumber = m_pWatchdogDisplayChange->property("tryNumber").toInt();
    if (cTryNumber > 0 && cTryNumber < 40)
    {
        /* Restart watchdog again: */
        m_pWatchdogDisplayChange->setProperty("tryNumber", ++cTryNumber);
        m_pWatchdogDisplayChange->start();
    }
    else
    {
        /* Reset watchdog: */
        m_pWatchdogDisplayChange->setProperty("tryNumber", 0);
    }
}
#endif /* VBOX_WS_MAC */

void UIMachine::sltHandleGuestMonitorChange(KGuestMonitorChangedEventType changeType, ulong uScreenId, QRect screenGeo)
{
    /* Ignore KGuestMonitorChangedEventType_NewOrigin change event: */
    if (changeType == KGuestMonitorChangedEventType_NewOrigin)
        return;
    /* Ignore KGuestMonitorChangedEventType_Disabled event for primary screen: */
    AssertMsg(countOfVisibleWindows() > 0, ("All machine windows are hidden!"));
    if (changeType == KGuestMonitorChangedEventType_Disabled && uScreenId == 0)
        return;

    /* Process KGuestMonitorChangedEventType_Enabled change event: */
    if (   !isScreenVisible(uScreenId)
        && changeType == KGuestMonitorChangedEventType_Enabled)
        setScreenVisible(uScreenId, true);
    /* Process KGuestMonitorChangedEventType_Disabled change event: */
    else if (   isScreenVisible(uScreenId)
             && changeType == KGuestMonitorChangedEventType_Disabled)
        setScreenVisible(uScreenId, false);

    /* Notify listeners about the change: */
    emit sigGuestMonitorChange(changeType, uScreenId, screenGeo);
}

void UIMachine::sltHandleKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock)
{
    /* Check if something had changed: */
    if (   m_fNumLock != fNumLock
        || m_fCapsLock != fCapsLock
        || m_fScrollLock != fScrollLock)
    {
        /* Store new num lock data: */
        if (m_fNumLock != fNumLock)
        {
            m_fNumLock = fNumLock;
            m_uNumLockAdaptionCnt = 2;
        }

        /* Store new caps lock data: */
        if (m_fCapsLock != fCapsLock)
        {
            m_fCapsLock = fCapsLock;
            m_uCapsLockAdaptionCnt = 2;
        }

        /* Store new scroll lock data: */
        if (m_fScrollLock != fScrollLock)
        {
            m_fScrollLock = fScrollLock;
        }

        /* Notify listeners: */
        emit sigKeyboardLedsChange();
    }
}

void UIMachine::sltMousePointerShapeChange(const UIMousePointerShapeData &shapeData)
{
    LogRelFlow(("GUI: UIMachine::sltMousePointerShapeChange: "
                "Is visible: %s, Has alpha: %s, "
                "Hot spot: %dx%d, Shape size: %dx%d, "
                "Shape data: %s\n",
                shapeData.isVisible() ? "TRUE" : "FALSE",
                shapeData.hasAlpha() ? "TRUE" : "FALSE",
                shapeData.hotSpot().x(), shapeData.hotSpot().y(),
                shapeData.shapeSize().width(), shapeData.shapeSize().height(),
                shapeData.shape().isEmpty() ? "EMPTY" : "PRESENT"));

    /* In case if shape itself is present: */
    if (shapeData.shape().size() > 0)
    {
        /* We are ignoring visibility flag: */
        m_fIsHidingHostPointer = false;

        /* And updating current shape data: */
        m_shapeData = shapeData;
        updateMousePointerShape();
    }
    /* In case if shape itself is NOT present: */
    else
    {
        /* Remember if we should hide the cursor: */
        m_fIsHidingHostPointer = !shapeData.isVisible();
    }

    /* Notify listeners: */
    emit sigMousePointerShapeChange();
}

void UIMachine::sltMouseCapabilityChange(bool fSupportsAbsolute, bool fSupportsRelative,
                                         bool fSupportsTouchScreen, bool fSupportsTouchPad,
                                         bool fNeedsHostCursor)
{
    LogRelFlow(("GUI: UIMachine::sltMouseCapabilityChange: "
                "Supports absolute: %s, Supports relative: %s, "
                "Supports touchscreen: %s, Supports touchpad: %s, "
                "Needs host cursor: %s\n",
                fSupportsAbsolute ? "TRUE" : "FALSE", fSupportsRelative ? "TRUE" : "FALSE",
                fSupportsTouchScreen ? "TRUE" : "FALSE", fSupportsTouchPad ? "TRUE" : "FALSE",
                fNeedsHostCursor ? "TRUE" : "FALSE"));

    /* Check if something had changed: */
    if (   m_fIsMouseSupportsAbsolute != fSupportsAbsolute
        || m_fIsMouseSupportsRelative != fSupportsRelative
        || m_fIsMouseSupportsTouchScreen != fSupportsTouchScreen
        || m_fIsMouseSupportsTouchPad != fSupportsTouchPad
        || m_fIsMouseHostCursorNeeded != fNeedsHostCursor)
    {
        /* Store new data: */
        m_fIsMouseSupportsAbsolute = fSupportsAbsolute;
        m_fIsMouseSupportsRelative = fSupportsRelative;
        m_fIsMouseSupportsTouchScreen = fSupportsTouchScreen;
        m_fIsMouseSupportsTouchPad = fSupportsTouchPad;
        m_fIsMouseHostCursorNeeded = fNeedsHostCursor;

        /* Notify listeners: */
        emit sigMouseCapabilityChange();
    }
}

void UIMachine::sltCursorPositionChange(bool fContainsData, unsigned long uX, unsigned long uY)
{
    LogRelFlow(("GUI: UIMachine::sltCursorPositionChange: "
                "Cursor position valid: %d, Cursor position: %dx%d\n",
                fContainsData ? "TRUE" : "FALSE", uX, uY));

    /* Check if something had changed: */
    if (   m_fIsValidCursorPositionPresent != fContainsData
        || m_cursorPosition.x() != (int)uX
        || m_cursorPosition.y() != (int)uY)
    {
        /* Store new data: */
        m_fIsValidCursorPositionPresent = fContainsData;
        m_cursorPosition = QPoint(uX, uY);

        /* Notify listeners: */
        emit sigCursorPositionChange();
    }
}

UIMachine::UIMachine()
    : QObject(0)
    , m_fInitialized(false)
    , m_pSession(0)
    , m_enmAllowedVisualStates(UIVisualStateType_Invalid)
    , m_enmInitialVisualState(UIVisualStateType_Normal)
    , m_enmVisualState(UIVisualStateType_Invalid)
    , m_enmRequestedVisualState(UIVisualStateType_Invalid)
    , m_pMachineLogic(0)
    , m_pMachineWindowIcon(0)
    , m_pActionPool(0)
#ifdef VBOX_WS_MAC
    , m_pMenuBar(0)
#endif
#ifdef VBOX_WS_MAC
    , m_pWatchdogDisplayChange(0)
#endif
    , m_fIsGuestResizeIgnored(false)
    , m_fNumLock(false)
    , m_fCapsLock(false)
    , m_fScrollLock(false)
    , m_uNumLockAdaptionCnt(2)
    , m_uCapsLockAdaptionCnt(2)
    , m_fIsHidLedsSyncEnabled(false)
    , m_fIsAutoCaptureDisabled(false)
    , m_iKeyboardState(0)
    , m_fIsHidingHostPointer(true)
    , m_fIsValidPointerShapePresent(false)
    , m_fIsValidCursorPositionPresent(false)
    , m_fIsMouseSupportsAbsolute(false)
    , m_fIsMouseSupportsRelative(false)
    , m_fIsMouseSupportsTouchScreen(false)
    , m_fIsMouseSupportsTouchPad(false)
    , m_fIsMouseHostCursorNeeded(false)
    , m_fIsMouseCaptured(false)
    , m_fIsMouseIntegrated(true)
    , m_iMouseState(0)
    , m_enmVMExecutionEngine(KVMExecutionEngine_NotSet)
    , m_fIsHWVirtExNestedPagingEnabled(false)
    , m_fIsHWVirtExUXEnabled(false)
    , m_enmParavirtProvider(KParavirtProvider_None)
    , m_fIsManualOverride(false)
    , m_defaultCloseAction(MachineCloseAction_Invalid)
    , m_restrictedCloseActions(MachineCloseAction_Invalid)
    , m_fQuitRequested(false)
{
    s_pInstance = this;
}

UIMachine::~UIMachine()
{
    s_pInstance = 0;
}

bool UIMachine::prepare()
{
    /* Prepare stuff unrelated to VM: */
    prepareNotificationCenter();

    /* Prepare VM session: */
    if (!prepareSession())
        return false;

    /* Prepare VM related stuff: */
    prepareActions();
    prepareHostScreenData();
    prepareKeyboard();
    prepareClose();
    prepareVisualState();

    /* Update VM related stuff: */
    updateBranding();
    updateGuestScreenData();
    enterInitialVisualState();

    /* Try to initialize VM session: */
    if (!uisession()->initialize())
        return false;

    /* Update stuff which doesn't send events on init: */
    updateVirtualizationState();
    updateStateAudioActions();
    updateMouseState();

    /* Warn listeners about we are initialized: */
    m_fInitialized = true;
    emit sigInitialized();

    /* True by default: */
    return true;
}

void UIMachine::prepareNotificationCenter()
{
    UINotificationCenter::create();
}

bool UIMachine::prepareSession()
{
    /* Create session UI: */
    m_pSession = new UISession(this);
    AssertPtrReturn(uisession(), false);

    /* Console events stuff: */
    connect(uisession(), &UISession::sigAudioAdapterChange,
            this, &UIMachine::sltHandleAudioAdapterChange);
    connect(uisession(), &UISession::sigAdditionsStateChange,
            this, &UIMachine::sigAdditionsStateChange);
    connect(uisession(), &UISession::sigAdditionsStateActualChange,
            this, &UIMachine::sltHandleAdditionsActualChange);
    connect(uisession(), &UISession::sigClipboardModeChange,
            this, &UIMachine::sigClipboardModeChange);
    connect(uisession(), &UISession::sigCPUExecutionCapChange,
            this, &UIMachine::sigCPUExecutionCapChange);
    connect(uisession(), &UISession::sigDnDModeChange,
            this, &UIMachine::sigDnDModeChange);
    connect(uisession(), &UISession::sigGuestMonitorChange,
            this, &UIMachine::sltHandleGuestMonitorChange);
    connect(uisession(), &UISession::sigMachineStateChange,
            this, &UIMachine::sigMachineStateChange);
    connect(uisession(), &UISession::sigMediumChange,
            this, &UIMachine::sigMediumChange);
    connect(uisession(), &UISession::sigNetworkAdapterChange,
            this, &UIMachine::sigNetworkAdapterChange);
    connect(uisession(), &UISession::sigRecordingChange,
            this, &UIMachine::sltHandleRecordingChange);
    connect(uisession(), &UISession::sigSharedFolderChange,
            this, &UIMachine::sigSharedFolderChange);
    connect(uisession(), &UISession::sigStorageDeviceChange,
            this, &UIMachine::sltHandleStorageDeviceChange);
    connect(uisession(), &UISession::sigUSBControllerChange,
            this, &UIMachine::sigUSBControllerChange);
    connect(uisession(), &UISession::sigUSBDeviceStateChange,
            this, &UIMachine::sigUSBDeviceStateChange);
    connect(uisession(), &UISession::sigVRDEChange,
            this, &UIMachine::sltHandleVRDEChange);
    connect(uisession(), &UISession::sigRuntimeError,
            this, &UIMachine::sigRuntimeError);
#ifdef VBOX_WS_MAC
    connect(uisession(), &UISession::sigShowWindows,
            this, &UIMachine::sigShowWindows);
#endif

    /* Keyboard stuff: */
    connect(uisession(), &UISession::sigKeyboardLedsChange,
            this, &UIMachine::sltHandleKeyboardLedsChange);

    /* Mouse stuff: */
    connect(uisession(), &UISession::sigMousePointerShapeChange,
            this, &UIMachine::sltMousePointerShapeChange);
    connect(uisession(), &UISession::sigMouseCapabilityChange,
            this, &UIMachine::sltMouseCapabilityChange);
    connect(uisession(), &UISession::sigCursorPositionChange,
            this, &UIMachine::sltCursorPositionChange);

    /* Make sure session prepared: */
    return uisession()->prepare();
}

void UIMachine::prepareActions()
{
    /* Create action-pool: */
    m_pActionPool = UIActionPool::create(UIType_RuntimeUI);
    if (actionPool())
    {
        /* Make sure action-pool knows guest-screen count: */
        actionPool()->toRuntime()->setGuestScreenCount(uisession()->frameBuffers().size());
        /* Update action restrictions: */
        updateActionRestrictions();

#ifdef VBOX_WS_MAC
        /* Create Mac OS X menu-bar: */
        m_pMenuBar = new QMenuBar;
        if (m_pMenuBar)
        {
            /* Configure Mac OS X menu-bar: */
            connect(gEDataManager, &UIExtraDataManager::sigMenuBarConfigurationChange,
                    this, &UIMachine::sltHandleMenuBarConfigurationChange);
            /* Update Mac OS X menu-bar: */
            updateMenu();
        }
#endif /* VBOX_WS_MAC */

        /* Get machine ID: */
        const QUuid uMachineID = uiCommon().managedVMUuid();
        Q_UNUSED(uMachineID);

#ifdef VBOX_WS_MAC
        /* User-element (Menu-bar and Dock) options: */
        {
            const bool fDisabled = gEDataManager->guiFeatureEnabled(GUIFeatureType_NoUserElements);
            if (fDisabled)
                UICocoaApplication::instance()->hideUserElements();
        }
#else /* !VBOX_WS_MAC */
        /* Menu-bar options: */
        {
            const bool fEnabledGlobally = !gEDataManager->guiFeatureEnabled(GUIFeatureType_NoMenuBar);
            const bool fEnabledForMachine = gEDataManager->menuBarEnabled(uMachineID);
            const bool fEnabled = fEnabledGlobally && fEnabledForMachine;
            actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_S_Settings)->setEnabled(fEnabled);
            actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->blockSignals(true);
            actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->setChecked(fEnabled);
            actionPool()->action(UIActionIndexRT_M_View_M_MenuBar_T_Visibility)->blockSignals(false);
        }
#endif /* !VBOX_WS_MAC */

        /* View options: */
        const bool fGuestScreenAutoresize = gEDataManager->guestScreenAutoResizeEnabled(uMachineID);
        actionPool()->action(UIActionIndexRT_M_View_T_GuestAutoresize)->blockSignals(true);
        actionPool()->action(UIActionIndexRT_M_View_T_GuestAutoresize)->setChecked(fGuestScreenAutoresize);
        actionPool()->action(UIActionIndexRT_M_View_T_GuestAutoresize)->blockSignals(false);

        /* Input options: */
        const bool fMouseIntegrated = isMouseIntegrated(); // no e-data for now ..
        actionPool()->action(UIActionIndexRT_M_Input_M_Mouse_T_Integration)->blockSignals(true);
        actionPool()->action(UIActionIndexRT_M_Input_M_Mouse_T_Integration)->setChecked(fMouseIntegrated);
        actionPool()->action(UIActionIndexRT_M_Input_M_Mouse_T_Integration)->blockSignals(false);

        /* Device options: */
        actionPool()->action(UIActionIndexRT_M_Devices_S_UpgradeGuestAdditions)->setEnabled(false);

        /* Status-bar options: */
        {
            const bool fEnabledGlobally = !gEDataManager->guiFeatureEnabled(GUIFeatureType_NoStatusBar);
            const bool fEnabledForMachine = gEDataManager->statusBarEnabled(uMachineID);
            const bool fEnabled = fEnabledGlobally && fEnabledForMachine;
            actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_S_Settings)->setEnabled(fEnabled);
            actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->blockSignals(true);
            actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->setChecked(fEnabled);
            actionPool()->action(UIActionIndexRT_M_View_M_StatusBar_T_Visibility)->blockSignals(false);
        }
    }
}

void UIMachine::prepareHostScreenData()
{
    /* Recache display data: */
    updateHostScreenData();

#ifdef VBOX_WS_MAC
    /* Prepare display-change watchdog: */
    m_pWatchdogDisplayChange = new QTimer(this);
    {
        m_pWatchdogDisplayChange->setInterval(500);
        m_pWatchdogDisplayChange->setSingleShot(true);
        connect(m_pWatchdogDisplayChange, &QTimer::timeout,
                this, &UIMachine::sltCheckIfHostDisplayChanged);
    }
    /* Install native display reconfiguration callback: */
    CGDisplayRegisterReconfigurationCallback(cgDisplayReconfigurationCallback, this);
#else /* !VBOX_WS_MAC */
    /* Install Qt display reconfiguration callbacks: */
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenCountChanged,
            this, &UIMachine::sltHandleHostScreenCountChange);
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenResized,
            this, &UIMachine::sltHandleHostScreenGeometryChange);
# if defined(VBOX_WS_NIX) && !defined(VBOX_GUI_WITH_CUSTOMIZATIONS1)
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenWorkAreaRecalculated,
            this, &UIMachine::sltHandleHostScreenAvailableAreaChange);
# else /* !VBOX_WS_NIX || VBOX_GUI_WITH_CUSTOMIZATIONS1 */
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenWorkAreaResized,
            this, &UIMachine::sltHandleHostScreenAvailableAreaChange);
# endif /* !VBOX_WS_NIX || VBOX_GUI_WITH_CUSTOMIZATIONS1 */
#endif /* !VBOX_WS_MAC */
}

void UIMachine::prepareKeyboard()
{
#if defined(VBOX_WS_MAC) || defined(VBOX_WS_WIN)
    /* Load extra-data value: */
    m_fIsHidLedsSyncEnabled = gEDataManager->hidLedsSyncState(uiCommon().managedVMUuid());
    /* Connect to extra-data changes to be able to enable/disable feature dynamically: */
    connect(gEDataManager, &UIExtraDataManager::sigHidLedsSyncStateChange,
            this, &UIMachine::sltHidLedsSyncStateChanged);
#endif /* VBOX_WS_MAC || VBOX_WS_WIN */
}

void UIMachine::prepareClose()
{
    /* What is the default close action and the restricted are? */
    const QUuid uMachineID = uiCommon().managedVMUuid();
    m_defaultCloseAction = gEDataManager->defaultMachineCloseAction(uMachineID);
    m_restrictedCloseActions = gEDataManager->restrictedMachineCloseActions(uMachineID);
}

void UIMachine::prepareVisualState()
{
    /* Prepare async visual state type change handler: */
    qRegisterMetaType<UIVisualStateType>();
    connect(this, &UIMachine::sigRequestAsyncVisualStateChange,
            this, &UIMachine::sltChangeVisualState,
            Qt::QueuedConnection);

    /* Load restricted visual states: */
    UIVisualStateType enmRestrictedVisualStates = gEDataManager->restrictedVisualStates(uiCommon().managedVMUuid());
    /* Acquire allowed visual states: */
    m_enmAllowedVisualStates = static_cast<UIVisualStateType>(UIVisualStateType_All ^ enmRestrictedVisualStates);

    /* Load requested visual state, it can override initial one: */
    m_enmRequestedVisualState = gEDataManager->requestedVisualState(uiCommon().managedVMUuid());
    /* Check if requested visual state is allowed: */
    if (isVisualStateAllowed(m_enmRequestedVisualState))
    {
        switch (m_enmRequestedVisualState)
        {
            /* Direct transition allowed to scale/fullscreen modes only: */
            case UIVisualStateType_Scale:
            case UIVisualStateType_Fullscreen:
                m_enmInitialVisualState = m_enmRequestedVisualState;
                break;
            default:
                break;
        }
    }
}

void UIMachine::updateBranding()
{
    /* Create the icon dynamically: */
    delete m_pMachineWindowIcon;
    m_pMachineWindowIcon = new QIcon;
    acquireUserMachineIcon(*m_pMachineWindowIcon);

#ifndef VBOX_WS_MAC
    /* Load user's machine-window name postfix: */
    const QUuid uMachineID = uiCommon().managedVMUuid();
    m_strMachineWindowNamePostfix = gEDataManager->machineWindowNamePostfix(uMachineID);
#endif /* !VBOX_WS_MAC */
}

void UIMachine::updateGuestScreenData()
{
    /* Accquire guest-screen count: */
    ulong cMonitorCount = 0;
    acquireMonitorCount(cMonitorCount);

    /* Prepare initial screen visibility status: */
    m_guestScreenVisibilityVector.fill(false, cMonitorCount);
    m_guestScreenVisibilityVector[0] = true;

    /* Prepare empty last full-screen size vector: */
    m_monitorLastFullScreenSizeVector.fill(QSize(-1, -1), cMonitorCount);

    /* If machine is in 'saved' state: */
    if (uisession()->isSaved())
    {
        /* Update screen visibility status from saved-state: */
        for (int iScreenIndex = 0; iScreenIndex < m_guestScreenVisibilityVector.size(); ++iScreenIndex)
        {
            long iDummy = 0;
            ulong uDummy = 0;
            bool fEnabled = true;
            acquireSavedGuestScreenInfo(iScreenIndex, iDummy, iDummy, uDummy, uDummy, fEnabled);
            m_guestScreenVisibilityVector[iScreenIndex] = fEnabled;
        }
        /* And make sure at least one of them is visible (primary if others are hidden): */
        if (countOfVisibleWindows() < 1)
            m_guestScreenVisibilityVector[0] = true;
    }
    else if (uiCommon().isSeparateProcess())
    {
        /* Update screen visibility status from display directly: */
        for (int iScreenIndex = 0; iScreenIndex < m_guestScreenVisibilityVector.size(); ++iScreenIndex)
        {
            ulong uDummy = 0;
            long iDummy = 0;
            KGuestMonitorStatus enmMonitorStatus = KGuestMonitorStatus_Disabled;
            acquireGuestScreenParameters(iScreenIndex, uDummy, uDummy, uDummy, iDummy, iDummy, enmMonitorStatus);
            m_guestScreenVisibilityVector[iScreenIndex] = (   enmMonitorStatus == KGuestMonitorStatus_Enabled
                                                           || enmMonitorStatus == KGuestMonitorStatus_Blank);
        }
        /* And make sure at least one of them is visible (primary if others are hidden): */
        if (countOfVisibleWindows() < 1)
            m_guestScreenVisibilityVector[0] = true;
    }

    /* Prepare initial screen visibility status of host-desires (same as facts): */
    m_guestScreenVisibilityVectorHostDesires.resize(cMonitorCount);
    for (int iScreenIndex = 0; iScreenIndex < m_guestScreenVisibilityVector.size(); ++iScreenIndex)
        m_guestScreenVisibilityVectorHostDesires[iScreenIndex] = m_guestScreenVisibilityVector[iScreenIndex];

    /* Make sure action-pool knows guest-screen visibility status: */
    for (int iScreenIndex = 0; iScreenIndex < m_guestScreenVisibilityVector.size(); ++iScreenIndex)
        actionPool()->toRuntime()->setGuestScreenVisible(iScreenIndex, m_guestScreenVisibilityVector.at(iScreenIndex));
}

void UIMachine::enterInitialVisualState()
{
    sltChangeVisualState(m_enmInitialVisualState);
}

void UIMachine::cleanupMachineLogic()
{
    /* Delete machine-logic if any: */
    UIMachineLogic::destroy(m_pMachineLogic);
}

void UIMachine::cleanupBranding()
{
    /* Cleanup machine-window icon: */
    delete m_pMachineWindowIcon;
    m_pMachineWindowIcon = 0;
}

void UIMachine::cleanupHostScreenData()
{
#ifdef VBOX_WS_MAC
    /* Remove display reconfiguration callback: */
    CGDisplayRemoveReconfigurationCallback(cgDisplayReconfigurationCallback, this);
#endif /* VBOX_WS_MAC */
}

void UIMachine::cleanupActions()
{
#ifdef VBOX_WS_MAC
    /* Destroy Mac OS X menu-bar: */
    delete m_pMenuBar;
    m_pMenuBar = 0;
#endif /* VBOX_WS_MAC */

    /* Destroy action-pool if necessary: */
    if (actionPool())
        UIActionPool::destroy(actionPool());
}

void UIMachine::cleanupSession()
{
    /* Destroy session UI if exists: */
    delete m_pSession;
    m_pSession = 0;
}

void UIMachine::cleanupNotificationCenter()
{
    UINotificationCenter::destroy();
}

void UIMachine::cleanup()
{
    /* Preprocess all the meta-events: */
    QApplication::sendPostedEvents(0, QEvent::MetaCall);

    /* Cleanup stuff: */
    cleanupMachineLogic();
    cleanupBranding();
    cleanupHostScreenData();
    cleanupActions();
    cleanupSession();
    cleanupNotificationCenter();
}

void UIMachine::updateActionRestrictions()
{
    /* Get host and prepare restrictions: */
    UIExtraDataMetaDefs::RuntimeMenuMachineActionType restrictionForMachine =
        UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Invalid;
    UIExtraDataMetaDefs::RuntimeMenuViewActionType restrictionForView =
        UIExtraDataMetaDefs::RuntimeMenuViewActionType_Invalid;
    UIExtraDataMetaDefs::RuntimeMenuDevicesActionType restrictionForDevices =
        UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Invalid;

    /* Separate process stuff: */
    {
        /* Initialize 'Machine' menu: */
        if (!uiCommon().isSeparateProcess())
            restrictionForMachine = (UIExtraDataMetaDefs::RuntimeMenuMachineActionType)
                                    (restrictionForMachine | UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Detach);
    }

    /* VRDE server stuff: */
    {
        /* Initialize 'View' menu: */
        bool fServerPresent = false;
        acquireWhetherVRDEServerPresent(fServerPresent);
        if (!fServerPresent)
            restrictionForView = (UIExtraDataMetaDefs::RuntimeMenuViewActionType)
                                 (restrictionForView | UIExtraDataMetaDefs::RuntimeMenuViewActionType_VRDEServer);
    }

    /* Storage stuff: */
    {
        /* Initialize CD/FD menus: */
        ulong cHardDisks = 0;
        ulong iOpticalDevices = 0;
        ulong cFloppyDevices = 0;
        acquireAmountOfStorageDevices(cHardDisks, iOpticalDevices, cFloppyDevices);
        QAction *pOpticalDevicesMenu = actionPool()->action(UIActionIndexRT_M_Devices_M_OpticalDevices);
        QAction *pFloppyDevicesMenu = actionPool()->action(UIActionIndexRT_M_Devices_M_FloppyDevices);
        pOpticalDevicesMenu->setData((int)iOpticalDevices);
        pFloppyDevicesMenu->setData((int)cFloppyDevices);
        if (!iOpticalDevices)
            restrictionForDevices = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
                                    (restrictionForDevices | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_OpticalDevices);
        if (!cFloppyDevices)
            restrictionForDevices = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
                                    (restrictionForDevices | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_FloppyDevices);
    }

    /* Audio stuff: */
    {
        /* Do we need to restrict something? */
        bool fRestricted = false;
        /* Check whether audio adapter is present: */
        bool fAdapterPresent = false;
        acquireWhetherAudioAdapterPresent(fAdapterPresent);
        if (!fAdapterPresent)
            fRestricted = true;
        else
        {
            /* Check whether audio adapter is enabled: */
            bool fAdapterEnabled = false;
            acquireWhetherAudioAdapterEnabled(fAdapterEnabled);
            if (!fAdapterEnabled)
                fRestricted = true;
        }
        /* Apply restrictions: */
        if (fRestricted)
            restrictionForDevices = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
                                    (restrictionForDevices | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Audio);
    }

    /* Network stuff: */
    {
        /* Initialize Network menu: */
        bool fAtLeastOneAdapterEnabled = false;
        acquireWhetherAtLeastOneNetworkAdapterEnabled(fAtLeastOneAdapterEnabled);
        if (!fAtLeastOneAdapterEnabled)
            restrictionForDevices = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
                                    (restrictionForDevices | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Network);
    }

    /* USB stuff: */
    {
        /* Check whether there is at least one USB controller with an available proxy. */
        bool fUSBEnabled = false;
        acquireWhetherUSBControllerEnabled(fUSBEnabled);
        if (!fUSBEnabled)
            restrictionForDevices = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
                                    (restrictionForDevices | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_USBDevices);
    }

    /* WebCams stuff: */
    {
        /* Check whether there is an accessible video input devices pool: */
        bool fWebCamsEnabled = false;
        acquireWhetherVideoInputDevicesEnabled(fWebCamsEnabled);
        if (!fWebCamsEnabled)
            restrictionForDevices = (UIExtraDataMetaDefs::RuntimeMenuDevicesActionType)
                                    (restrictionForDevices | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_WebCams);
    }

    /* Apply cumulative restriction for 'Machine' menu: */
    actionPool()->toRuntime()->setRestrictionForMenuMachine(UIActionRestrictionLevel_Session, restrictionForMachine);
    /* Apply cumulative restriction for 'View' menu: */
    actionPool()->toRuntime()->setRestrictionForMenuView(UIActionRestrictionLevel_Session, restrictionForView);
    /* Apply cumulative restriction for 'Devices' menu: */
    actionPool()->toRuntime()->setRestrictionForMenuDevices(UIActionRestrictionLevel_Session, restrictionForDevices);
}

#ifdef VBOX_WS_MAC
void UIMachine::updateMenu()
{
    /* Rebuild Mac OS X menu-bar: */
    m_pMenuBar->clear();
    foreach (QMenu *pMenu, actionPool()->menus())
    {
        UIMenu *pMenuUI = qobject_cast<UIMenu*>(pMenu);
        if (!pMenuUI->isConsumable() || !pMenuUI->isConsumed())
            m_pMenuBar->addMenu(pMenuUI);
        if (pMenuUI->isConsumable() && !pMenuUI->isConsumed())
            pMenuUI->setConsumed(true);
    }
    /* Update the dock menu as well: */
    if (machineLogic())
        machineLogic()->updateDock();
}
#endif /* VBOX_WS_MAC */

void UIMachine::updateHostScreenData()
{
    /* Rebuild host-screen data vector: */
    m_hostScreens.clear();
    for (int iScreenIndex = 0; iScreenIndex < UIDesktopWidgetWatchdog::screenCount(); ++iScreenIndex)
        m_hostScreens << gpDesktop->screenGeometry(iScreenIndex);

    /* Make sure action-pool knows host-screen count: */
    actionPool()->toRuntime()->setHostScreenCount(m_hostScreens.size());
}

void UIMachine::updateMousePointerShape()
{
    /* Fetch incoming shape data: */
    const bool fHasAlpha = m_shapeData.hasAlpha();
    const uint uWidth = m_shapeData.shapeSize().width();
    const uint uHeight = m_shapeData.shapeSize().height();
    const uchar *pShapeData = m_shapeData.shape().constData();
    AssertMsgReturnVoid(pShapeData, ("Shape data must not be NULL!\n"));

    /* Invalidate mouse pointer shape initially: */
    m_fIsValidPointerShapePresent = false;
    m_cursorShapePixmap = QPixmap();
    m_cursorMaskPixmap = QPixmap();

    /* Parse incoming shape data: */
    const uchar *pSrcAndMaskPtr = pShapeData;
    const uint uAndMaskSize = (uWidth + 7) / 8 * uHeight;
    const uchar *pSrcShapePtr = pShapeData + ((uAndMaskSize + 3) & ~3);

#if defined (VBOX_WS_WIN)

    /* Create an ARGB image out of the shape data: */

    // WORKAROUND:
    // Qt5 QCursor recommends 32 x 32 cursor, therefore the original data is copied to
    // a larger QImage if necessary. Cursors like 10x16 did not work correctly (Solaris 10 guest).
    // Align the cursor dimensions to 32 bit pixels, because for example a 56x56 monochrome cursor
    // did not work correctly on Windows host.
    const uint uCursorWidth = RT_ALIGN_32(uWidth, 32);
    const uint uCursorHeight = RT_ALIGN_32(uHeight, 32);

    if (fHasAlpha)
    {
        QImage image(uCursorWidth, uCursorHeight, QImage::Format_ARGB32);
        memset(image.bits(), 0, image.sizeInBytes());

        const uint32_t *pu32SrcShapeScanline = (uint32_t *)pSrcShapePtr;
        for (uint y = 0; y < uHeight; ++y, pu32SrcShapeScanline += uWidth)
            memcpy(image.scanLine(y), pu32SrcShapeScanline, uWidth * sizeof(uint32_t));

        m_cursorShapePixmap = QPixmap::fromImage(image);
    }
    else
    {
#if 0 // disabled for public ticket
        if (isPointer1bpp(pSrcShapePtr, uWidth, uHeight))
        {
            // Incoming data consist of 32 bit BGR XOR mask and 1 bit AND mask.
            // XOR pixels contain either 0x00000000 or 0x00FFFFFF.
            //
            // Originally intended result (F denotes 0x00FFFFFF):
            // XOR AND
            //   0   0 black
            //   F   0 white
            //   0   1 transparent
            //   F   1 xor'd
            //
            // Actual Qt5 result for color table 0:0xFF000000, 1:0xFFFFFFFF
            // (tested on Windows 7 and 10 64 bit hosts):
            // Bitmap Mask
            //  0   0 black
            //  1   0 white
            //  0   1 xor
            //  1   1 transparent

            QVector<QRgb> colors(2);
            colors[0] = UINT32_C(0xFF000000);
            colors[1] = UINT32_C(0xFFFFFFFF);

            QImage bitmap(uCursorWidth, uCursorHeight, QImage::Format_Mono);
            bitmap.setColorTable(colors);
            memset(bitmap.bits(), 0xFF, bitmap.sizeInBytes());

            QImage mask(uCursorWidth, uCursorHeight, QImage::Format_Mono);
            mask.setColorTable(colors);
            memset(mask.bits(), 0xFF, mask.sizeInBytes());

            const uint8_t *pu8SrcAndScanline = pSrcAndMaskPtr;
            const uint32_t *pu32SrcShapeScanline = (uint32_t *)pSrcShapePtr;
            for (uint y = 0; y < uHeight; ++y)
            {
                for (uint x = 0; x < uWidth; ++x)
                {
                    const uint8_t u8Bit = (uint8_t)(1 << (7 - x % 8));

                    const uint8_t u8SrcMaskByte = pu8SrcAndScanline[x / 8];
                    const uint8_t u8SrcMaskBit = u8SrcMaskByte & u8Bit;
                    const uint32_t u32SrcPixel = pu32SrcShapeScanline[x] & UINT32_C(0xFFFFFF);

                    uint8_t *pu8DstMaskByte = &mask.scanLine(y)[x / 8];
                    uint8_t *pu8DstBitmapByte = &bitmap.scanLine(y)[x / 8];

                    if (u8SrcMaskBit == 0)
                    {
                        if (u32SrcPixel == 0)
                        {
                            /* Black: Qt Bitmap = 0, Mask = 0 */
                            *pu8DstMaskByte &= ~u8Bit;
                            *pu8DstBitmapByte &= ~u8Bit;
                        }
                        else
                        {
                            /* White: Qt Bitmap = 1, Mask = 0 */
                            *pu8DstMaskByte &= ~u8Bit;
                            *pu8DstBitmapByte |= u8Bit;
                        }
                    }
                    else
                    {
                        if (u32SrcPixel == 0)
                        {
                            /* Transparent: Qt Bitmap = 1, Mask = 1 */
                            *pu8DstMaskByte |= u8Bit;
                            *pu8DstBitmapByte |= u8Bit;
                        }
                        else
                        {
                            /* Xor'ed: Qt Bitmap = 0, Mask = 1 */
                            *pu8DstMaskByte |= u8Bit;
                            *pu8DstBitmapByte &= ~u8Bit;
                        }
                    }
                }

                pu8SrcAndScanline += (uWidth + 7) / 8;
                pu32SrcShapeScanline += uWidth;
            }

            m_cursorShapePixmap = QBitmap::fromImage(bitmap);
            m_cursorMaskPixmap = QBitmap::fromImage(mask);
        }
        else
#endif // disabled for public ticket
        {
            /* Assign alpha channel values according to the AND mask: 1 -> 0x00, 0 -> 0xFF: */
            QImage image(uCursorWidth, uCursorHeight, QImage::Format_ARGB32);
            memset(image.bits(), 0, image.sizeInBytes());

            const uint8_t *pu8SrcAndScanline = pSrcAndMaskPtr;
            const uint32_t *pu32SrcShapeScanline = (uint32_t *)pSrcShapePtr;

            for (uint y = 0; y < uHeight; ++y)
            {
                uint32_t *pu32DstPixel = (uint32_t *)image.scanLine(y);

                for (uint x = 0; x < uWidth; ++x)
                {
                    const uint8_t u8Bit = (uint8_t)(1 << (7 - x % 8));
                    const uint8_t u8SrcMaskByte = pu8SrcAndScanline[x / 8];

                    if (u8SrcMaskByte & u8Bit)
                    {
                        /* Inverse cannot be emulated by alpha mask, so make these pixels gray,
                         * to make it visible on both black and white
                         * (this is usually used for text select cursor). */
                        if ((pu32SrcShapeScanline[x] & UINT32_C(0x00FFFFFF)) == UINT32_C(0x00FFFFFF))
                            *pu32DstPixel++ = UINT32_C(0xFF7F7F7F);
                        else
                            *pu32DstPixel++ = pu32SrcShapeScanline[x] & UINT32_C(0x00FFFFFF);
                    }
                    else
                        *pu32DstPixel++ = pu32SrcShapeScanline[x] | UINT32_C(0xFF000000);
                }

                pu32SrcShapeScanline += uWidth;
                pu8SrcAndScanline += (uWidth + 7) / 8;
            }

            m_cursorShapePixmap = QPixmap::fromImage(image);
        }
    }

    /* Mark mouse pointer shape valid: */
    m_fIsValidPointerShapePresent = true;

#elif defined(VBOX_WS_NIX) || defined(VBOX_WS_MAC)

    /* Create an ARGB image out of the shape data: */
    QImage image(uWidth, uHeight, QImage::Format_ARGB32);

    if (fHasAlpha)
    {
        memcpy(image.bits(), pSrcShapePtr, uHeight * uWidth * 4);
    }
    else
    {
        renderCursorPixels((uint32_t *)pSrcShapePtr, pSrcAndMaskPtr,
                           uWidth, uHeight,
                           (uint32_t *)image.bits(), uHeight * uWidth * 4);
    }

    /* Create cursor-pixmap from the image: */
    m_cursorShapePixmap = QPixmap::fromImage(image);

    /* Mark mouse pointer shape valid: */
    m_fIsValidPointerShapePresent = true;

#else

# warning "port me"

#endif

    /* Cache cursor pixmap size and hotspot: */
    m_cursorSize = m_cursorShapePixmap.size();
    m_cursorHotspot = m_shapeData.hotSpot();
}

void UIMachine::updateMouseState()
{
    uisession()->acquireWhetherAbsoluteSupported(m_fIsMouseSupportsAbsolute);
    uisession()->acquireWhetherRelativeSupported(m_fIsMouseSupportsRelative);
    uisession()->acquireWhetherTouchScreenSupported(m_fIsMouseSupportsTouchScreen);
    uisession()->acquireWhetherTouchPadSupported(m_fIsMouseSupportsTouchPad);
    uisession()->acquireWhetherNeedsHostCursor(m_fIsMouseHostCursorNeeded);
}

#if defined(VBOX_WS_NIX) || defined(VBOX_WS_MAC)
/* static */
void UIMachine::renderCursorPixels(const uint32_t *pu32XOR, const uint8_t *pu8AND,
                                   uint32_t u32Width, uint32_t u32Height,
                                   uint32_t *pu32Pixels, uint32_t cbPixels)
{
    /* Output pixels set to 0 which allow to not write transparent pixels anymore. */
    memset(pu32Pixels, 0, cbPixels);

    const uint32_t *pu32XORSrc = pu32XOR;  /* Iterator for source XOR pixels. */
    const uint8_t *pu8ANDSrcLine = pu8AND; /* The current AND mask scanline. */
    uint32_t *pu32Dst = pu32Pixels;        /* Iterator for all destination BGRA pixels. */

    /* Some useful constants. */
    const int cbANDLine = ((int)u32Width + 7) / 8;

    int y;
    for (y = 0; y < (int)u32Height; ++y)
    {
        int x;
        for (x = 0; x < (int)u32Width; ++x)
        {
            const uint32_t u32Pixel = *pu32XORSrc; /* Current pixel at (x,y) */
            const uint8_t *pu8ANDSrc = pu8ANDSrcLine + x / 8; /* Byte which containt current AND bit. */

            if ((*pu8ANDSrc << (x % 8)) & 0x80)
            {
                if (u32Pixel)
                {
                    const uint32_t u32PixelInverted = ~u32Pixel;

                    /* Scan neighbor pixels and assign them if they are transparent. */
                    int dy;
                    for (dy = -1; dy <= 1; ++dy)
                    {
                        const int yn = y + dy;
                        if (yn < 0 || yn >= (int)u32Height)
                            continue; /* Do not cross the bounds. */

                        int dx;
                        for (dx = -1; dx <= 1; ++dx)
                        {
                            const int xn = x + dx;
                            if (xn < 0 || xn >= (int)u32Width)
                                continue;  /* Do not cross the bounds. */

                            if (dx != 0 || dy != 0)
                            {
                                /* Check if the neighbor pixel is transparent. */
                                const uint32_t *pu32XORNeighborSrc = &pu32XORSrc[dy * (int)u32Width + dx];
                                const uint8_t *pu8ANDNeighborSrc = pu8ANDSrcLine + dy * cbANDLine + xn / 8;
                                if (   *pu32XORNeighborSrc == 0
                                    && ((*pu8ANDNeighborSrc << (xn % 8)) & 0x80) != 0)
                                {
                                    /* Transparent neighbor pixels are replaced with the source pixel value. */
                                    uint32_t *pu32PixelNeighborDst = &pu32Dst[dy * (int)u32Width + dx];
                                    *pu32PixelNeighborDst = u32Pixel | 0xFF000000;
                                }
                            }
                            else
                            {
                                /* The pixel itself is replaced with inverted value. */
                                *pu32Dst = u32PixelInverted | 0xFF000000;
                            }
                        }
                    }
                }
                else
                {
                    /* The pixel does not affect the screen.
                     * Do nothing. Do not touch destination which can already contain generated pixels.
                     */
                }
            }
            else
            {
                /* AND bit is 0, the pixel will be just drawn. */
                *pu32Dst = u32Pixel | 0xFF000000;
            }

            ++pu32XORSrc; /* Next source pixel. */
            ++pu32Dst;    /* Next destination pixel. */
        }

        /* Next AND scanline. */
        pu8ANDSrcLine += cbANDLine;
    }
}
#endif /* VBOX_WS_NIX || VBOX_WS_MAC */

#ifdef VBOX_WS_WIN
/* static */
bool UIMachine::isPointer1bpp(const uint8_t *pu8XorMask,
                              uint uWidth,
                              uint uHeight)
{
    /* Check if the pointer has only 0 and 0xFFFFFF pixels, ignoring the alpha channel. */
    const uint32_t *pu32Src = (uint32_t *)pu8XorMask;

    uint y;
    for (y = 0; y < uHeight ; ++y)
    {
        uint x;
        for (x = 0; x < uWidth; ++x)
        {
            const uint32_t u32Pixel = pu32Src[x] & UINT32_C(0xFFFFFF);
            if (u32Pixel != 0 && u32Pixel != UINT32_C(0xFFFFFF))
                return false;
        }

        pu32Src += uWidth;
    }

    return true;
}
#endif /* VBOX_WS_WIN */

void UIMachine::updateVirtualizationState()
{
    uisession()->acquireExecutionEngineType(m_enmVMExecutionEngine);
    uisession()->acquireWhetherHwVirtExNestedPagingEnabled(m_fIsHWVirtExNestedPagingEnabled);
    uisession()->acquireWhetherHwVirtExUXEnabled(m_fIsHWVirtExUXEnabled);
    uisession()->acquireEffectiveParavirtProvider(m_enmParavirtProvider);
}
