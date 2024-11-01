/* $Id: UIMachineLogic.h 106832 2024-11-01 12:16:23Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogic class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIMachineLogic_h
#define FEQT_INCLUDED_SRC_runtime_UIMachineLogic_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIExtraDataDefs.h"
#include "UIAdvancedSettingsDialog.h"

/* COM includes: */
#include "KGuestMonitorChangedEventType.h"

/* Forward declarations: */
class QAction;
class QActionGroup;
class QIManagerDialog;
class UIActionPool;
class UIKeyboardHandler;
class UIMachine;
class UIMachineWindow;
class UIMachineView;
class UIMouseHandler;
class UIDockIconPreview;
class UISoftKeyboard;
class UIVMInformationDialog;
class CUSBDevice;
class CVirtualBoxErrorInfo;
#if defined(VBOX_WS_NIX)
 struct DBusScreenSaverInhibitMethod;
#endif

/* Machine logic interface: */
class UIMachineLogic : public QObject
{
    Q_OBJECT;

    /** Pointer to menu update-handler for this class: */
    typedef void (UIMachineLogic::*MenuUpdateHandler)(QMenu *pMenu);

public:

    /** Factory function to create a logic of required type.
      * @param  pMachine            Brings the machine this logic belongs to.
      * @param  pSession            Brings the session this logic is created for.
      * @param  enmVisualStateType  Brings the visual state type of logic to be created. */
    static UIMachineLogic *create(UIMachine *pMachine, UIVisualStateType enmVisualStateType);
    /** Factory function to destroy passed @a pLogic. */
    static void destroy(UIMachineLogic *&pLogic);

    /** Returns visual state type. */
    virtual UIVisualStateType visualStateType() const = 0;

    /* Check if this logic is available: */
    virtual bool checkAvailability() = 0;

    /** Returns machine-window flags for current machine-logic and passed @a uScreenId. */
    virtual Qt::WindowFlags windowFlags(ulong uScreenId) const = 0;

    /* Prepare/cleanup the logic: */
    virtual void prepare();
    virtual void cleanup();

    /** Returns machine UI reference.  */
    UIMachine *uimachine() const { return m_pMachine; }
    /** Returns action-pool reference.  */
    UIActionPool *actionPool() const;

    /** Returns the machine name. */
    QString machineName() const;
    const QList<UIMachineWindow*>& machineWindows() const { return m_machineWindowsList; }
    UIKeyboardHandler* keyboardHandler() const { return m_pKeyboardHandler; }
    UIMouseHandler* mouseHandler() const { return m_pMouseHandler; }
    UIMachineWindow* mainMachineWindow() const;
    UIMachineWindow* activeMachineWindow() const;

    /** Adjusts machine-window(s) geometry if necessary. */
    virtual void adjustMachineWindowsGeometry();

    /** Send machine-window(s) size-hint(s) to the guest. */
    virtual void sendMachineWindowsSizeHints();

    /** Opens Machine settings / Network page the modal way. */
    void openNetworkSettingsDialogTheModalWay();

#ifdef VBOX_WS_MAC
    void updateDockIcon();
    void updateDockIconSize(int screenId, int width, int height);
    UIMachineView* dockPreviewView() const;
    virtual void updateDock();
#endif /* VBOX_WS_MAC */

    /** An public interface to sltTypeHostKeyComboPressRelease. */
    void typeHostKeyComboPressRelease(bool fToggleSequence);

protected slots:

    /** Handles the VBoxSVC availability change. */
    void sltHandleVBoxSVCAvailabilityChange();

    /** Handles Machine UI initialized event. */
    void sltHandleMachineInitialized();

    /** Checks if some visual-state type was requested. */
    virtual void sltCheckForRequestedVisualStateType() {}

    /** Requests visual-state change to 'normal' (window). */
    virtual void sltChangeVisualStateToNormal();
    /** Requests visual-state change to 'fullscreen'. */
    virtual void sltChangeVisualStateToFullscreen();
    /** Requests visual-state change to 'seamless'. */
    virtual void sltChangeVisualStateToSeamless();
    /** Requests visual-state change to 'scale'. */
    virtual void sltChangeVisualStateToScale();

    /* Console callback handlers: */
    virtual void sltMachineStateChanged();
    virtual void sltSessionStateChanged(const QUuid &uId, const KSessionState enmState);
    virtual void sltAdditionsStateChanged();
    virtual void sltMouseCapabilityChanged();
    virtual void sltKeyboardLedsChanged();
    virtual void sltUSBDeviceStateChange(const CUSBDevice &device, bool fIsAttached, const CVirtualBoxErrorInfo &error);
    virtual void sltRuntimeError(bool fIsFatal, const QString &strErrorId, const QString &strMessage);
#ifdef RT_OS_DARWIN
    virtual void sltShowWindows();
#endif /* RT_OS_DARWIN */
    /** Handles guest-screen count change. */
    virtual void sltGuestMonitorChange(KGuestMonitorChangedEventType changeType, ulong uScreenId, QRect screenGeo);

    /** Handles host-screen count change. */
    virtual void sltHostScreenCountChange();
    /** Handles host-screen geometry change. */
    virtual void sltHostScreenGeometryChange();
    /** Handles host-screen available-area change. */
    virtual void sltHostScreenAvailableAreaChange();
    /* Handles Help Request. */
    virtual void sltHandleHelpRequest();

protected:

    /** Constructs a logic passing @a pMachine to the base-class.
      * @param  pMachine  Brings the machine this logic belongs to. */
    UIMachineLogic(UIMachine *pMachine);
    /* Destructs the logic. */
    virtual ~UIMachineLogic() RT_OVERRIDE;

    /* Protected getters/setters: */
    bool isMachineWindowsCreated() const { return m_fIsWindowsCreated; }
    void setMachineWindowsCreated(bool fIsWindowsCreated) { m_fIsWindowsCreated = fIsWindowsCreated; }

    /* Protected members: */
    void setKeyboardHandler(UIKeyboardHandler *pKeyboardHandler);
    void setMouseHandler(UIMouseHandler *pMouseHandler);
    void addMachineWindow(UIMachineWindow *pMachineWindow);
#ifdef VBOX_WS_MAC
    bool isDockIconPreviewEnabled() const { return m_fIsDockIconEnabled; }
    void setDockIconPreviewEnabled(bool fIsDockIconPreviewEnabled) { m_fIsDockIconEnabled = fIsDockIconPreviewEnabled; }
    void updateDockOverlay();
#endif /* VBOX_WS_MAC */

    /* Prepare helpers: */
    virtual void prepareRequiredFeatures() {}
    virtual void prepareSessionConnections();
    virtual void prepareActionGroups();
    virtual void prepareActionConnections();
    virtual void prepareOtherConnections();
    virtual void prepareHandlers();
    virtual void prepareMachineWindows() = 0;
    virtual void prepareMenu() {}
#ifdef VBOX_WS_MAC
    virtual void prepareDock();
#endif /* VBOX_WS_MAC */
#ifdef VBOX_WITH_DEBUGGER_GUI
    virtual void prepareDebugger();
#endif /* VBOX_WITH_DEBUGGER_GUI */
    virtual void loadSettings();

    /* Cleanup helpers: */
#ifdef VBOX_WITH_DEBUGGER_GUI
    virtual void cleanupDebugger();
#endif /* VBOX_WITH_DEBUGGER_GUI */
#ifdef VBOX_WS_MAC
    virtual void cleanupDock();
#endif /* VBOX_WS_MAC */
    virtual void cleanupMenu() {}
    virtual void cleanupMachineWindows() = 0;
    virtual void cleanupHandlers();
    //virtual void cleanupOtherConnections() {}
    virtual void cleanupActionConnections() {}
    virtual void cleanupActionGroups() {}
    virtual void cleanupSessionConnections();
    //virtual void cleanupRequiredFeatures() {}

    /* Handler: Event-filter stuff: */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handle menu prepare. */
    void sltHandleMenuPrepare(int iIndex, QMenu *pMenu);

    /* "Application" menu functionality: */
    void sltOpenPreferencesDialogDefault() { openPreferencesDialog(); }
    void sltClosePreferencesDialog();
    void sltClose();

    /* "Machine" menu functionality: */
    void sltOpenSettingsDialogDefault() { openSettingsDialog(); }
    void sltCloseSettingsDialog();
    void sltTakeSnapshot();
    void sltShowInformationDialog();
    void sltCloseInformationDialog();
    void sltShowFileManagerDialog();
    void sltCloseFileManagerDialog();
    void sltShowLogDialog();
    void sltCloseLogDialog();
    void sltPause(bool fOn);
    void sltReset();
    void sltDetach();
    void sltSaveState();
    void sltShutdown();
    void sltPowerOff();

    /* "View" menu functionality: */
    void sltMinimizeActiveMachineWindow();
    void sltAdjustMachineWindows();
    void sltToggleGuestAutoresize(bool fEnabled);
    void sltTakeScreenshot();
    void sltOpenRecordingOptions();
    void sltToggleRecording(bool fEnabled);
    void sltToggleVRDE(bool fEnabled);

    /* "Input" menu functionality: */
    void sltShowKeyboardSettings();
    void sltShowSoftKeyboard();
    void sltCloseSoftKeyboard();
    void sltTypeCAD();
#ifdef VBOX_WS_NIX
    void sltTypeCABS();
#endif /* VBOX_WS_NIX */
    void sltTypeCtrlBreak();
    void sltTypeInsert();
    void sltTypePrintScreen();
    void sltTypeAltPrintScreen();
    void sltTypeHostKeyComboPressRelease(bool fToggleSequence);
    void sltToggleMouseIntegration(bool fEnabled);

    /* "Device" menu functionality: */
    void sltOpenSettingsDialogStorage();
    void sltMountStorageMedium();
    void sltToggleAudioOutput(bool fEnabled);
    void sltToggleAudioInput(bool fEnabled);
    void sltOpenSettingsDialogNetwork();
    void sltOpenSettingsDialogUSBDevices();
    void sltOpenSettingsDialogSharedFolders();
    void sltAttachUSBDevice();
    void sltAttachWebcamDevice();
    void sltChangeSharedClipboardType(QAction *pAction);
    void sltFileTransferToggled(bool fChecked);
    void sltToggleNetworkAdapterConnection(bool fChecked);
    void sltChangeDragAndDropType(QAction *pAction);
    void sltInstallGuestAdditions();

#ifdef VBOX_WITH_DEBUGGER_GUI
    /* "Debug" menu functionality: */
    void sltShowDebugStatistics();
    void sltShowDebugCommandLine();
    void sltLoggingToggled(bool);
    void sltShowGuestControlConsoleDialog();
    void sltCloseGuestControlConsoleDialog();
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef RT_OS_DARWIN /* Something is *really* broken in regards of the moc here */
    /* "Window" menu functionality: */
    void sltSwitchToMachineWindow();

    /* "Dock" menu functionality: */
    void sltDockPreviewModeChanged(QAction *pAction);
    void sltDockPreviewMonitorChanged(QAction *pAction);
    void sltChangeDockIconUpdate(bool fEnabled);
    /** Handles dock icon overlay change event. */
    void sltChangeDockIconOverlayAppearance(bool fDisabled);
    /** Handles dock icon overlay disable action triggering. */
    void sltDockIconDisableOverlayChanged(bool fDisabled);
#endif /* RT_OS_DARWIN */

    /* Handlers: Keyboard LEDs sync logic: */
    void sltSwitchKeyboardLedsToGuestLeds();
    void sltSwitchKeyboardLedsToPreviousLeds();

    /* Handle disabling/enabling host screen saver. */
    void sltDisableHostScreenSaverStateChanged(bool fDisabled);

    /** Handles request for visual state change. */
    void sltHandleVisualStateChange();

    /** Handles request to commit data. */
    void sltHandleCommitData();

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Update 'Devices' : 'Optical/Floppy Devices' menu routine. */
    void updateMenuDevicesStorage(QMenu *pMenu);
    /** Update 'Devices' : 'Network' menu routine. */
    void updateMenuDevicesNetwork(QMenu *pMenu);
    /** Update 'Devices' : 'USB Devices' menu routine. */
    void updateMenuDevicesUSB(QMenu *pMenu);
    /** Update 'Devices' : 'Web Cams' menu routine. */
    void updateMenuDevicesWebcams(QMenu *pMenu);
    /** Update 'Devices' : 'Shared Clipboard' menu routine. */
    void updateMenuDevicesSharedClipboard(QMenu *pMenu);
    /** Update 'Devices' : 'Drag and Drop' menu routine. */
    void updateMenuDevicesDragAndDrop(QMenu *pMenu);
#ifdef VBOX_WITH_DEBUGGER_GUI
    /** Update 'Debug' menu routine. */
    void updateMenuDebug(QMenu *pMenu);
#endif /* VBOX_WITH_DEBUGGER_GUI */
#ifdef VBOX_WS_MAC
    /** Update 'Window' menu routine. */
    void updateMenuWindow(QMenu *pMenu);
#endif /* VBOX_WS_MAC */

    /** Asks user for the disks encryption passwords. */
    void askUserForTheDiskEncryptionPasswords();

    /** Opens Global preferences dialog.
      * @param  strCategory  Brings the category to open.
      * @param  strControl   Brings the name of control to choose.
      * @param  fAppModal    Brings whether dialog should be app modal. */
    void openPreferencesDialog(const QString &strCategory = QString(),
                               const QString &strControl = QString(),
                               bool fAppModal = false);

    /** Opens VM settings dialog.
      * @param  strCategory  Brings the category to open.
      * @param  strControl   Brings the name of control to choose.
      * @param  fAppModal    Brings whether dialog should be app modal. */
    void openSettingsDialog(const QString &strCategory = QString(),
                            const QString &strControl = QString(),
                            bool fAppModal = false);

    /* Helpers: */
    void takeScreenshot(const QString &strFile, const QString &strFormat /* = "png" */) const;

    /** Reactivates the screen saver. This is possbily called during vm window close and enables host screen
      * if there are no other vms running at the moment. Note that this seems to be not needed on Linux since
      * closing vm windows re-activates screen saver automatically. On Windows explicit re-activation is needed. */
    void activateScreenSaver();
    /* Shows the boot failure dialog through which user can mount a boot DVD and reset the vm. */
    void showBootFailureDialog();
    /** Resets the machine. If @p fShowConfirmation is true then a confirmation messag box is shown first. */
    void reset(bool fShowConfirmation);

    /* Private variables: */
    UIMachine *m_pMachine;
    UIKeyboardHandler *m_pKeyboardHandler;
    UIMouseHandler *m_pMouseHandler;
    QList<UIMachineWindow*> m_machineWindowsList;

    QActionGroup *m_pRunningActions;
    QActionGroup *m_pRunningOrPausedActions;
    QActionGroup *m_pRunningOrPausedOrStuckActions;
    QActionGroup *m_pSharedClipboardActions;
    QAction      *m_pFileTransferToggleAction;
    QActionGroup *m_pDragAndDropActions;

    /** Holds the map of menu update-handlers. */
    QMap<int, MenuUpdateHandler> m_menuUpdateHandlers;

    bool m_fIsWindowsCreated : 1;

#ifdef VBOX_WS_MAC
    bool m_fIsDockIconEnabled;
    UIDockIconPreview *m_pDockIconPreview;
    QActionGroup *m_pDockPreviewSelectMonitorGroup;
    QAction *m_pDockSettingsMenuSeparator;
    int m_DockIconPreviewMonitor;
    QAction *m_pDockSettingMenuAction;
    /* Keeps a list of machine menu actions that we add to dock menu. */
    QList<QAction*> m_dockMachineMenuActions;
#endif /* VBOX_WS_MAC */

    void *m_pHostLedsState;

    /** Holds the map of settings dialogs. */
    QMap<UIAdvancedSettingsDialog::DialogType, UIAdvancedSettingsDialog*>  m_settings;

    /** Holds the log viewer dialog instance. */
    QIManagerDialog       *m_pLogViewerDialog;
    QIManagerDialog       *m_pFileManagerDialog;
    QIManagerDialog       *m_pProcessControlDialog;
    UISoftKeyboard        *m_pSoftKeyboardDialog;
    UIVMInformationDialog *m_pVMInformationDialog;

    /* Holds the cookies returnd by QDBus inhibition calls. Map keys are service name. These are required during uninhibition.*/
    QMap<QString, uint> m_screenSaverInhibitionCookies;
#if defined(VBOX_WS_NIX)
    QVector<DBusScreenSaverInhibitMethod*> m_methods;
#endif
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIMachineLogic_h */
