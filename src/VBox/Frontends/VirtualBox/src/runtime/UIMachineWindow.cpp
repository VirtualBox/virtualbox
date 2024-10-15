/* $Id: UIMachineWindow.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindow class implementation.
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
#include <QCloseEvent>
#include <QGridLayout>
#include <QProcess>
#include <QStyle>
#include <QTimer>

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIModalWindowManager.h"
#include "UIExtraDataManager.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMessageCenter.h"
#include "UIMachineLogic.h"
#include "UIMachineWindow.h"
#include "UIMachineWindowNormal.h"
#include "UIMachineWindowFullscreen.h"
#include "UIMachineWindowSeamless.h"
#include "UIMachineWindowScale.h"
#include "UIMachineView.h"
#include "UIKeyboardHandler.h"
#include "UIMouseHandler.h"
#include "UITranslationEventListener.h"
#include "UIVMCloseDialog.h"

/* COM includes: */
#include "CGraphicsAdapter.h"
#include "CSnapshot.h"

/* Other VBox includes: */
#include <VBox/version.h>
#ifdef VBOX_BLEEDING_EDGE
# include <iprt/buildconfig.h>
#endif /* VBOX_BLEEDING_EDGE */


/* static */
UIMachineWindow* UIMachineWindow::create(UIMachineLogic *pMachineLogic, ulong uScreenId)
{
    /* Create machine-window: */
    UIMachineWindow *pMachineWindow = 0;
    switch (pMachineLogic->visualStateType())
    {
        case UIVisualStateType_Normal:
            pMachineWindow = new UIMachineWindowNormal(pMachineLogic, uScreenId);
            break;
        case UIVisualStateType_Fullscreen:
            pMachineWindow = new UIMachineWindowFullscreen(pMachineLogic, uScreenId);
            break;
        case UIVisualStateType_Seamless:
            pMachineWindow = new UIMachineWindowSeamless(pMachineLogic, uScreenId);
            break;
        case UIVisualStateType_Scale:
            pMachineWindow = new UIMachineWindowScale(pMachineLogic, uScreenId);
            break;
        default:
            AssertMsgFailed(("Incorrect visual state!"));
            break;
    }
    /* Prepare machine-window: */
    pMachineWindow->prepare();
    /* Return machine-window: */
    return pMachineWindow;
}

/* static */
void UIMachineWindow::destroy(UIMachineWindow *pWhichWindow)
{
    /* Cleanup machine-window: */
    pWhichWindow->cleanup();
    /* Delete machine-window: */
    delete pWhichWindow;
}

void UIMachineWindow::prepare()
{
    /* Prepare dialog itself: */
    prepareSelf();

    /* Prepare session-connections: */
    prepareSessionConnections();

    /* Prepare main-layout: */
    prepareMainLayout();

    /* Prepare menu: */
    prepareMenu();

    /* Prepare status-bar: */
    prepareStatusBar();

    /* Prepare visual-state: */
    prepareVisualState();

    /* Prepare machine-view: */
    prepareMachineView();

    /* Prepare notification-center: */
    prepareNotificationCenter();

    /* Prepare handlers: */
    prepareHandlers();

    /* Load settings: */
    loadSettings();

    /* Retranslate window: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIMachineWindow::sltRetranslateUI);

    /* Show (must be done before updating the appearance): */
    showInNecessaryMode();

    /* Update all the elements: */
    updateAppearanceOf(UIVisualElement_AllStuff);

#ifdef VBOX_WS_NIX
    /* Prepare default class/name values: */
    const QString strWindowClass = QString("VirtualBox Machine");
    QString strWindowName = strWindowClass;
    /* Check if we want Window Manager to distinguish Virtual Machine windows: */
    if (gEDataManager->distinguishMachineWindowGroups(uiCommon().managedVMUuid()))
        strWindowName = QString("VirtualBox Machine UUID: %1").arg(uiCommon().managedVMUuid().toString());
    /* Assign WM_CLASS property: */
    NativeWindowSubsystem::setWMClass(uiCommon().X11ServerAvailable(), this, strWindowName, strWindowClass);
    /* Tell the WM we are well behaved wrt Xwayland keyboard-grabs: */
    NativeWindowSubsystem::setXwaylandMayGrabKeyboardFlag(uiCommon().X11ServerAvailable(), this);
#endif
}

void UIMachineWindow::cleanup()
{
    /* Save window settings: */
    saveSettings();

    /* Cleanup handlers: */
    cleanupHandlers();

    /* Cleanup visual-state: */
    cleanupVisualState();

    /* Cleanup notification-center: */
    cleanupNotificationCenter();

    /* Cleanup machine-view: */
    cleanupMachineView();

    /* Cleanup status-bar: */
    cleanupStatusBar();

    /* Cleanup menu: */
    cleanupMenu();

    /* Cleanup main layout: */
    cleanupMainLayout();

    /* Cleanup session connections: */
    cleanupSessionConnections();
}

void UIMachineWindow::sltMachineStateChanged()
{
    /* Update window-title: */
    updateAppearanceOf(UIVisualElement_WindowTitle);
}

UIMachineWindow::UIMachineWindow(UIMachineLogic *pMachineLogic, ulong uScreenId)
    : QMainWindow(0, pMachineLogic->windowFlags(uScreenId))
    , m_pMachineLogic(pMachineLogic)
    , m_pMachineView(0)
    , m_uScreenId(uScreenId)
    , m_pMainLayout(0)
    , m_pTopSpacer(0)
    , m_pBottomSpacer(0)
    , m_pLeftSpacer(0)
    , m_pRightSpacer(0)
{
}

UIMachine *UIMachineWindow::uimachine() const
{
    return machineLogic()->uimachine();
}

UIActionPool *UIMachineWindow::actionPool() const
{
    return machineLogic()->actionPool();
}

QString UIMachineWindow::machineName() const
{
    return uimachine()->machineName();
}

bool UIMachineWindow::shouldResizeToGuestDisplay() const
{
    return    actionPool()
           && actionPool()->action(UIActionIndexRT_M_View_T_GuestAutoresize)
           && actionPool()->action(UIActionIndexRT_M_View_T_GuestAutoresize)->isChecked();
}

void UIMachineWindow::adjustMachineViewSize()
{
    /* We need to adjust guest-screen size if necessary: */
    machineView()->adjustGuestScreenSize();
}

void UIMachineWindow::sendMachineViewSizeHint()
{
    /* Send machine-view size-hint to the guest: */
    machineView()->resendSizeHint();
}

#ifdef VBOX_WITH_MASKED_SEAMLESS
void UIMachineWindow::setMask(const QRegion &region)
{
    /* Call to base-class: */
    QMainWindow::setMask(region);
}
#endif /* VBOX_WITH_MASKED_SEAMLESS */

void UIMachineWindow::updateAppearanceOf(int iElement)
{
    /* Update window title: */
    if (iElement & UIVisualElement_WindowTitle)
    {
        /* Make sure machine state is one of valid: */
        const KMachineState enmState = uimachine()->machineState();
        if (enmState == KMachineState_Null)
        {
            /* Assign default window title: */
            setWindowTitle(defaultWindowTitle());
        }
        else
        {
            /* Prepare full name: */
            QString strMachineName = machineName();

            /* Append snapshot name: */
            ulong uSnapshotCount = 0;
            uimachine()->acquireSnapshotCount(uSnapshotCount);
            if (uSnapshotCount > 0)
            {
                QString strCurrentSnapshotName;
                uimachine()->acquireCurrentSnapshotName(strCurrentSnapshotName);
                strMachineName += " (" + strCurrentSnapshotName + ")";
            }

            /* Append state name: */
            strMachineName += " [" + gpConverter->toString(enmState) + "]";

#ifndef VBOX_WS_MAC
            /* Append user product name (besides macOS): */
            const QString strUserProductName = uimachine()->machineWindowNamePostfix();
            strMachineName += " - " + (strUserProductName.isEmpty() ? defaultWindowTitle() : strUserProductName);
#endif /* !VBOX_WS_MAC */

            /* Append screen number only if there are more than one present: */
            ulong cMonitorCount = 0;
            uimachine()->acquireMonitorCount(cMonitorCount);
            if (cMonitorCount > 1)
                strMachineName += QString(" : %1").arg(m_uScreenId + 1);

            /* Assign title finally: */
            setWindowTitle(strMachineName);
        }
    }
}

void UIMachineWindow::sltRetranslateUI()
{
    /* Compose window-title prefix: */
    m_strWindowTitlePrefix = VBOX_PRODUCT;
#ifdef VBOX_BLEEDING_EDGE
    m_strWindowTitlePrefix += UIMachineWindow::tr(" EXPERIMENTAL build %1r%2 - %3")
                              .arg(RTBldCfgVersion())
                              .arg(RTBldCfgRevisionStr())
                              .arg(VBOX_BLEEDING_EDGE);
#endif /* VBOX_BLEEDING_EDGE */
    /* Update appearance of the window-title: */
    updateAppearanceOf(UIVisualElement_WindowTitle);
}

bool UIMachineWindow::event(QEvent *pEvent)
{
    /* Call to base-class: */
    const bool fResult = QMainWindow::event(pEvent);

    /* Handle particular events: */
    switch (pEvent->type())
    {
        case QEvent::WindowActivate:
        {
            /* Initiate registration in the modal window manager: */
            windowManager().setMainWindowShown(this);
            break;
        }
        default:
            break;
    }

    /* Return result: */
    return fResult;
}

void UIMachineWindow::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QMainWindow::showEvent(pEvent);

    /* Initiate registration in the modal window manager: */
    windowManager().setMainWindowShown(this);

    /* Update appearance for indicator-pool: */
    updateAppearanceOf(UIVisualElement_IndicatorPool);
}

void UIMachineWindow::hideEvent(QHideEvent *pEvent)
{
    /* Update registration in the modal window manager: */
    if (windowManager().mainWindowShown() == this)
    {
        if (machineLogic()->activeMachineWindow())
            windowManager().setMainWindowShown(machineLogic()->activeMachineWindow());
        else
            windowManager().setMainWindowShown(machineLogic()->mainMachineWindow());
    }

    /* Call to base-class: */
    QMainWindow::hideEvent(pEvent);
}

void UIMachineWindow::closeEvent(QCloseEvent *pCloseEvent)
{
    /* Always ignore close-event first: */
    pCloseEvent->ignore();
    /* But accept close-event if app quit was requested: */
    if (uimachine()->isQuitRequested())
    {
        pCloseEvent->accept();
        return;
    }

    /* In certain cases we need to just request app quit: */
    if (   !uimachine()->isSessionValid()
        || uimachine()->isTurnedOff())
    {
        uimachine()->closeRuntimeUI();
        return;
    }

    /* Make sure machine is in one of the allowed states: */
    if (!uimachine()->isRunning() && !uimachine()->isPaused() && !uimachine()->isStuck())
        return;

    /* If there is a close hook script defined: */
    const QString strScript = gEDataManager->machineCloseHookScript(uiCommon().managedVMUuid());
    if (!strScript.isEmpty())
    {
        /* Execute asynchronously and leave: */
        QProcess::startDetached(strScript, QStringList() << uiCommon().managedVMUuid().toString());
        return;
    }

    /* Choose the close action: */
    MachineCloseAction closeAction = MachineCloseAction_Invalid;

    /* If default close-action defined and not restricted: */
    MachineCloseAction defaultCloseAction = uimachine()->defaultCloseAction();
    MachineCloseAction restrictedCloseActions = uimachine()->restrictedCloseActions();
    if ((defaultCloseAction != MachineCloseAction_Invalid) &&
        !(restrictedCloseActions & defaultCloseAction))
    {
        switch (defaultCloseAction)
        {
            /* If VM is stuck, and the default close-action is 'detach', 'save-state' or 'shutdown',
             * we should ask the user about what to do: */
            case MachineCloseAction_Detach:
            case MachineCloseAction_SaveState:
            case MachineCloseAction_Shutdown:
                closeAction = uimachine()->isStuck() ? MachineCloseAction_Invalid : defaultCloseAction;
                break;
            /* Otherwise we just use what we have: */
            default:
                closeAction = defaultCloseAction;
                break;
        }
    }

    /* If the close-action still undefined: */
    if (closeAction == MachineCloseAction_Invalid)
    {
        /* Prepare close-dialog: */
        QWidget *pParentDlg = windowManager().realParentWindow(this);
        bool fInACPIMode = false;
        uimachine()->acquireWhetherGuestEnteredACPIMode(fInACPIMode);
        QPointer<UIVMCloseDialog> pCloseDlg = new UIVMCloseDialog(pParentDlg, uimachine(),
                                                                  fInACPIMode,
                                                                  restrictedCloseActions);
        /* Configure close-dialog: */
        if (uimachine()->machineWindowIcon())
            pCloseDlg->setIcon(*uimachine()->machineWindowIcon());

        /* Make sure close-dialog is valid: */
        if (pCloseDlg->isValid())
        {
            /* We are going to show close-dialog: */
            bool fShowCloseDialog = true;
            /* Check if VM is paused or stuck: */
            const bool fWasPaused = uimachine()->isPaused();
            const bool fIsStuck = uimachine()->isStuck();
            /* If VM is NOT paused and NOT stuck: */
            if (!fWasPaused && !fIsStuck)
            {
                /* We should pause it first: */
                const bool fIsPaused = uimachine()->pause();
                /* If we were unable to pause VM: */
                if (!fIsPaused)
                {
                    /* If that is NOT the separate VM process UI: */
                    if (!uiCommon().isSeparateProcess())
                    {
                        /* We are not going to show close-dialog: */
                        fShowCloseDialog = false;
                    }
                    /* If that is the separate VM process UI: */
                    else
                    {
                        // WORKAROUND:
                        // We are going to show close-dialog only
                        // if headless frontend stopped/killed already:
                        KMachineState enmActualState = KMachineState_Null;
                        uimachine()->acquireLiveMachineState(enmActualState);
                        fShowCloseDialog = enmActualState == KMachineState_Null;
                    }
                }
            }
            /* If we are going to show close-dialog: */
            if (fShowCloseDialog)
            {
                /* Show close-dialog to let the user make the choice: */
                windowManager().registerNewParent(pCloseDlg, pParentDlg);
                closeAction = static_cast<MachineCloseAction>(pCloseDlg->exec());

                /* Make sure the dialog still valid: */
                if (!pCloseDlg)
                    return;

                /* If VM was not paused before but paused now,
                 * we should resume it if user canceled dialog or chosen shutdown: */
                if (!fWasPaused && uimachine()->isPaused() &&
                    (closeAction == MachineCloseAction_Invalid ||
                     closeAction == MachineCloseAction_Detach ||
                     closeAction == MachineCloseAction_Shutdown))
                {
                    /* If we unable to resume VM, cancel closing: */
                    if (!uimachine()->unpause())
                        closeAction = MachineCloseAction_Invalid;
                }
            }
        }
        else
        {
            /* Else user misconfigured .vbox file, we will reject closing UI: */
            closeAction = MachineCloseAction_Invalid;
        }

        /* Cleanup close-dialog: */
        delete pCloseDlg;
    }

    /* Depending on chosen result: */
    switch (closeAction)
    {
        case MachineCloseAction_Detach:
        {
            /* Detach GUI: */
            LogRel(("GUI: Request for close-action to detach GUI.\n"));
            uimachine()->detachUi();
            break;
        }
        case MachineCloseAction_SaveState:
        {
            /* Save VM state: */
            LogRel(("GUI: Request for close-action to save VM state.\n"));
            uimachine()->saveState();
            break;
        }
        case MachineCloseAction_Shutdown:
        {
            /* Shutdown VM: */
            LogRel(("GUI: Request for close-action to shutdown VM.\n"));
            uimachine()->shutdown();
            break;
        }
        case MachineCloseAction_PowerOff:
        case MachineCloseAction_PowerOff_RestoringSnapshot:
        {
            /* Power VM off: */
            LogRel(("GUI: Request for close-action to power VM off.\n"));
            ulong uSnapshotCount = 0;
            uimachine()->acquireSnapshotCount(uSnapshotCount);
            const bool fDiscardStateOnPowerOff  = gEDataManager->discardStateOnPowerOff(uiCommon().managedVMUuid())
                                               || closeAction == MachineCloseAction_PowerOff_RestoringSnapshot;
            uimachine()->powerOff(uSnapshotCount > 0 && fDiscardStateOnPowerOff);
            break;
        }
        default:
            break;
    }
}

void UIMachineWindow::prepareSelf()
{
#ifndef VBOX_WS_MAC
    /* Set machine-window icon if any: */
    // On macOS window icon is referenced in info.plist.
    if (uimachine()->machineWindowIcon())
        setWindowIcon(*uimachine()->machineWindowIcon());
#endif /* !VBOX_WS_MAC */
}

void UIMachineWindow::prepareSessionConnections()
{
    /* We should watch for console events: */
    connect(uimachine(), &UIMachine::sigMachineStateChange, this, &UIMachineWindow::sltMachineStateChanged);
}

void UIMachineWindow::prepareMainLayout()
{
    /* Create central-widget: */
    setCentralWidget(new QWidget);

    /* Create main-layout: */
    m_pMainLayout = new QGridLayout(centralWidget());
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    m_pMainLayout->setSpacing(0);

    /* Create shifting-spacers: */
    m_pTopSpacer = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_pBottomSpacer = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_pLeftSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pRightSpacer = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Fixed);

    /* Add shifting-spacers into main-layout: */
    m_pMainLayout->addItem(m_pTopSpacer, 0, 1);
    m_pMainLayout->addItem(m_pBottomSpacer, 2, 1);
    m_pMainLayout->addItem(m_pLeftSpacer, 1, 0);
    m_pMainLayout->addItem(m_pRightSpacer, 1, 2);
}

void UIMachineWindow::prepareMachineView()
{
    /* Get visual-state type: */
    UIVisualStateType visualStateType = machineLogic()->visualStateType();

    /* Create machine-view: */
    m_pMachineView = UIMachineView::create(this, m_uScreenId, visualStateType);

    /* Add machine-view into main-layout: */
    m_pMainLayout->addWidget(m_pMachineView, 1, 1, viewAlignment(visualStateType));

    /* Install focus-proxy: */
    setFocusProxy(m_pMachineView);
}

void UIMachineWindow::prepareNotificationCenter()
{
    // for now it will be added from within particular visual mode windows ..
}

void UIMachineWindow::prepareHandlers()
{
    /* Register keyboard-handler: */
    machineLogic()->keyboardHandler()->prepareListener(m_uScreenId, this);

    /* Register mouse-handler: */
    machineLogic()->mouseHandler()->prepareListener(m_uScreenId, this);
}

void UIMachineWindow::cleanupHandlers()
{
    /* Unregister mouse-handler: */
    machineLogic()->mouseHandler()->cleanupListener(m_uScreenId);

    /* Unregister keyboard-handler: */
    machineLogic()->keyboardHandler()->cleanupListener(m_uScreenId);
}

void UIMachineWindow::cleanupNotificationCenter()
{
    // for now it will be removed from within particular visual mode windows ..
}

void UIMachineWindow::cleanupMachineView()
{
    /* Destroy machine-view: */
    UIMachineView::destroy(m_pMachineView);
    m_pMachineView = 0;
}

void UIMachineWindow::cleanupSessionConnections()
{
    /* We should stop watching for console events: */
    disconnect(uimachine(), &UIMachine::sigMachineStateChange, this, &UIMachineWindow::sltMachineStateChanged);
}

#ifdef VBOX_WITH_DEBUGGER_GUI
void UIMachineWindow::updateDbgWindows()
{
    /* The debugger windows are bind to the main VM window. */
    if (m_uScreenId == 0)
        uimachine()->dbgAdjustRelativePos();
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

/* static */
Qt::Alignment UIMachineWindow::viewAlignment(UIVisualStateType visualStateType)
{
    switch (visualStateType)
    {
        case UIVisualStateType_Normal: return Qt::Alignment();
        case UIVisualStateType_Fullscreen: return Qt::AlignVCenter | Qt::AlignHCenter;
        case UIVisualStateType_Seamless: return Qt::Alignment();
        case UIVisualStateType_Scale: return Qt::Alignment();
        case UIVisualStateType_Invalid: case UIVisualStateType_All: break; /* Shut up, MSC! */
    }
    AssertMsgFailed(("Incorrect visual state!"));
    return Qt::Alignment();
}

#ifdef VBOX_WS_MAC
void UIMachineWindow::handleStandardWindowButtonCallback(StandardWindowButtonType enmButtonType, bool fWithOptionKey)
{
    switch (enmButtonType)
    {
        case StandardWindowButtonType_Zoom:
        {
            /* Handle 'Zoom' button for 'Normal' and 'Scaled' modes: */
            if (   machineLogic()->visualStateType() == UIVisualStateType_Normal
                || machineLogic()->visualStateType() == UIVisualStateType_Scale)
            {
                if (fWithOptionKey)
                {
                    /* Toggle window zoom: */
                    darwinToggleWindowZoom(this);
                }
                else
                {
                    /* Enter 'full-screen' mode: */
                    uimachine()->setRequestedVisualState(UIVisualStateType_Invalid);
                    uimachine()->asyncChangeVisualState(UIVisualStateType_Fullscreen);
                }
            }
            break;
        }
        default:
            break;
    }
}

/* static */
void UIMachineWindow::handleNativeNotification(const QString &strNativeNotificationName, QWidget *pWidget)
{
    /* Handle arrived notification: */
    LogRel(("GUI: UIMachineWindow::handleNativeNotification: Notification '%s' received\n",
            strNativeNotificationName.toLatin1().constData()));
    AssertPtrReturnVoid(pWidget);
    if (UIMachineWindow *pMachineWindow = qobject_cast<UIMachineWindow*>(pWidget))
    {
        /* Redirect arrived notification: */
        LogRel2(("UIMachineWindow::handleNativeNotification: Redirecting '%s' notification to corresponding machine-window...\n",
                 strNativeNotificationName.toLatin1().constData()));
        pMachineWindow->handleNativeNotification(strNativeNotificationName);
    }
}

/* static */
void UIMachineWindow::handleStandardWindowButtonCallback(StandardWindowButtonType enmButtonType, bool fWithOptionKey, QWidget *pWidget)
{
    /* Handle arrived callback: */
    LogRel(("GUI: UIMachineWindow::handleStandardWindowButtonCallback: Callback for standard window button '%d' with option key '%d' received\n",
            (int)enmButtonType, (int)fWithOptionKey));
    AssertPtrReturnVoid(pWidget);
    if (UIMachineWindow *pMachineWindow = qobject_cast<UIMachineWindow*>(pWidget))
    {
        /* Redirect arrived callback: */
        LogRel2(("UIMachineWindow::handleStandardWindowButtonCallback: Redirecting callback for standard window button '%d' with option key '%d' to corresponding machine-window...\n",
                 (int)enmButtonType, (int)fWithOptionKey));
        pMachineWindow->handleStandardWindowButtonCallback(enmButtonType, fWithOptionKey);
    }
}
#endif /* VBOX_WS_MAC */
