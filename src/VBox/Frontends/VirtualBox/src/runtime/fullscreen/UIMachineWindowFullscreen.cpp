/* $Id: UIMachineWindowFullscreen.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindowFullscreen class implementation.
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
#include <QMenu>
#include <QTimer>
#include <QWindowStateChangeEvent>
#ifdef VBOX_WS_WIN
# include <QWindow>
#endif

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineView.h"
#include "UIMachineLogicFullscreen.h"
#include "UIMachineWindowFullscreen.h"
#include "UINotificationCenter.h"
#if   defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
# include "UIMachineDefs.h"
# include "UIMiniToolBar.h"
#elif defined(VBOX_WS_MAC)
# include "VBoxUtils-darwin.h"
# include "UICocoaApplication.h"
#endif /* VBOX_WS_MAC */

/* COM includes: */
#include "CSnapshot.h"


UIMachineWindowFullscreen::UIMachineWindowFullscreen(UIMachineLogic *pMachineLogic, ulong uScreenId)
    : UIMachineWindow(pMachineLogic, uScreenId)
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    , m_pMiniToolBar(0)
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
#ifdef VBOX_WS_MAC
    , m_fIsInFullscreenTransition(false)
#endif /* VBOX_WS_MAC */
    , m_fWasMinimized(false)
#ifdef VBOX_WS_NIX
    , m_fIsMinimizationRequested(false)
    , m_fIsMinimized(false)
#endif
{
}

#ifdef VBOX_WS_MAC
void UIMachineWindowFullscreen::handleNativeNotification(const QString &strNativeNotificationName)
{
    /* Log all arrived notifications: */
    LogRel(("UIMachineWindowFullscreen::handleNativeNotification: Notification '%s' received.\n",
            strNativeNotificationName.toLatin1().constData()));

    /* Handle 'NSWindowWillEnterFullScreenNotification' notification: */
    if (strNativeNotificationName == "NSWindowWillEnterFullScreenNotification")
    {
        LogRel(("UIMachineWindowFullscreen::handleNativeNotification: "
                "Native fullscreen mode about to enter, notifying listener...\n"));
        emit sigNotifyAboutNativeFullscreenWillEnter();
    }
    /* Handle 'NSWindowDidEnterFullScreenNotification' notification: */
    else if (strNativeNotificationName == "NSWindowDidEnterFullScreenNotification")
    {
        /* Mark window transition complete: */
        m_fIsInFullscreenTransition = false;
        LogRel(("UIMachineWindowFullscreen::handleNativeNotification: "
                "Native fullscreen mode entered, notifying listener...\n"));
        /* Update console's display viewport and 3D overlay: */
        machineView()->updateViewport();
        emit sigNotifyAboutNativeFullscreenDidEnter();
    }
    /* Handle 'NSWindowWillExitFullScreenNotification' notification: */
    else if (strNativeNotificationName == "NSWindowWillExitFullScreenNotification")
    {
        LogRel(("UIMachineWindowFullscreen::handleNativeNotification: "
                "Native fullscreen mode about to exit, notifying listener...\n"));
        emit sigNotifyAboutNativeFullscreenWillExit();
    }
    /* Handle 'NSWindowDidExitFullScreenNotification' notification: */
    else if (strNativeNotificationName == "NSWindowDidExitFullScreenNotification")
    {
        /* Mark window transition complete: */
        m_fIsInFullscreenTransition = false;
        LogRel(("UIMachineWindowFullscreen::handleNativeNotification: "
                "Native fullscreen mode exited, notifying listener...\n"));
        /* Update console's display viewport and 3D overlay: */
        machineView()->updateViewport();
        emit sigNotifyAboutNativeFullscreenDidExit();
    }
    /* Handle 'NSWindowDidFailToEnterFullScreenNotification' notification: */
    else if (strNativeNotificationName == "NSWindowDidFailToEnterFullScreenNotification")
    {
        /* Mark window transition complete: */
        m_fIsInFullscreenTransition = false;
        LogRel(("UIMachineWindowFullscreen::handleNativeNotification: "
                "Native fullscreen mode fail to enter, notifying listener...\n"));
        emit sigNotifyAboutNativeFullscreenFailToEnter();
    }
}
#endif /* VBOX_WS_MAC */

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowFullscreen::sltMachineStateChanged()
{
    /* Call to base-class: */
    UIMachineWindow::sltMachineStateChanged();

    /* Update mini-toolbar: */
    updateAppearanceOf(UIVisualElement_MiniToolBar);
}

void UIMachineWindowFullscreen::sltRevokeWindowActivation()
{
#ifdef VBOX_WS_NIX
    // WORKAROUND:
    // We could be asked to minimize already, but just
    // not yet executed that order to current moment.
    if (m_fIsMinimizationRequested)
        return;
#endif

    /* Make sure window is visible: */
    if (!isVisible() || isMinimized())
        return;

    /* Revoke stolen activation: */
#ifdef VBOX_WS_NIX
    raise();
#endif /* VBOX_WS_NIX */
    activateWindow();
}

void UIMachineWindowFullscreen::sltHandleMiniToolBarAutoHideToggled(bool fEnabled)
{
    /* Save mini-toolbar settings: */
    gEDataManager->setAutoHideMiniToolbar(fEnabled, uiCommon().managedVMUuid());
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#ifdef VBOX_WS_MAC
void UIMachineWindowFullscreen::sltEnterNativeFullscreen(UIMachineWindow *pMachineWindow)
{
    /* Make sure it is NULL or 'this' window passed: */
    if (pMachineWindow && pMachineWindow != this)
        return;

    /* Make sure this window has fullscreen logic: */
    UIMachineLogicFullscreen *pFullscreenLogic = qobject_cast<UIMachineLogicFullscreen*>(machineLogic());
    AssertPtrReturnVoid(pFullscreenLogic);

    /* Make sure this window should be shown and mapped to host-screen: */
    if (!uimachine()->isScreenVisible(m_uScreenId) ||
        !pFullscreenLogic->hasHostScreenForGuestScreen(m_uScreenId))
        return;

    /* Mark window 'transitioned to fullscreen': */
    m_fIsInFullscreenTransition = true;

    /* Enter native fullscreen mode if necessary: */
    if (   (pFullscreenLogic->screensHaveSeparateSpaces() || m_uScreenId == 0)
        && !darwinIsInFullscreenMode(this))
        darwinToggleFullscreenMode(this);
}

void UIMachineWindowFullscreen::sltExitNativeFullscreen(UIMachineWindow *pMachineWindow)
{
    /* Make sure it is NULL or 'this' window passed: */
    if (pMachineWindow && pMachineWindow != this)
        return;

    /* Make sure this window has fullscreen logic: */
    UIMachineLogicFullscreen *pFullscreenLogic = qobject_cast<UIMachineLogicFullscreen*>(machineLogic());
    AssertPtrReturnVoid(pFullscreenLogic);

    /* Mark window 'transitioned from fullscreen': */
    m_fIsInFullscreenTransition = true;

    /* Exit native fullscreen mode if necessary: */
    if (   (pFullscreenLogic->screensHaveSeparateSpaces() || m_uScreenId == 0)
        && darwinIsInFullscreenMode(this))
        darwinToggleFullscreenMode(this);
}
#endif /* VBOX_WS_MAC */

void UIMachineWindowFullscreen::sltShowMinimized()
{
#ifdef VBOX_WS_NIX
    /* Remember that we are asked to minimize: */
    m_fIsMinimizationRequested = true;
#endif

    showMinimized();
}

void UIMachineWindowFullscreen::prepareNotificationCenter()
{
    if (gpNotificationCenter && (m_uScreenId == 0))
        gpNotificationCenter->setParent(centralWidget());
}

void UIMachineWindowFullscreen::prepareVisualState()
{
    /* Call to base-class: */
    UIMachineWindow::prepareVisualState();

    /* The background has to go black: */
    QPalette palette(centralWidget()->palette());
    palette.setColor(centralWidget()->backgroundRole(), Qt::black);
    centralWidget()->setPalette(palette);
    centralWidget()->setAutoFillBackground(true);
    setAutoFillBackground(true);

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Prepare mini-toolbar: */
    prepareMiniToolbar();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#ifdef VBOX_WS_MAC
    /* Make sure this window has fullscreen logic: */
    UIMachineLogicFullscreen *pFullscreenLogic = qobject_cast<UIMachineLogicFullscreen*>(machineLogic());
    AssertPtrReturnVoid(pFullscreenLogic);
    /* Enable fullscreen support for every screen which requires it: */
    if (pFullscreenLogic->screensHaveSeparateSpaces() || m_uScreenId == 0)
        darwinEnableFullscreenSupport(this);
    /* Enable transience support for other screens: */
    else
        darwinEnableTransienceSupport(this);
    /* Register to native fullscreen notifications: */
    UICocoaApplication::instance()->registerToNotificationOfWindow("NSWindowWillEnterFullScreenNotification", this,
                                                                   UIMachineWindow::handleNativeNotification);
    UICocoaApplication::instance()->registerToNotificationOfWindow("NSWindowDidEnterFullScreenNotification", this,
                                                                   UIMachineWindow::handleNativeNotification);
    UICocoaApplication::instance()->registerToNotificationOfWindow("NSWindowWillExitFullScreenNotification", this,
                                                                   UIMachineWindow::handleNativeNotification);
    UICocoaApplication::instance()->registerToNotificationOfWindow("NSWindowDidExitFullScreenNotification", this,
                                                                   UIMachineWindow::handleNativeNotification);
    UICocoaApplication::instance()->registerToNotificationOfWindow("NSWindowDidFailToEnterFullScreenNotification", this,
                                                                   UIMachineWindow::handleNativeNotification);
#endif /* VBOX_WS_MAC */
}

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowFullscreen::prepareMiniToolbar()
{
    /* Make sure mini-toolbar is not restricted: */
    if (!gEDataManager->miniToolbarEnabled(uiCommon().managedVMUuid()))
        return;

    /* Create mini-toolbar: */
    m_pMiniToolBar = new UIMiniToolBar(this,
                                       GeometryType_Full,
                                       gEDataManager->miniToolbarAlignment(uiCommon().managedVMUuid()),
                                       gEDataManager->autoHideMiniToolbar(uiCommon().managedVMUuid()),
                                       screenId());
    AssertPtrReturnVoid(m_pMiniToolBar);
    {
        /* Configure mini-toolbar: */
        m_pMiniToolBar->addMenus(actionPool()->menus());
        connect(m_pMiniToolBar, &UIMiniToolBar::sigMinimizeAction,
                this, &UIMachineWindowFullscreen::sltShowMinimized, Qt::QueuedConnection);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigExitAction,
                actionPool()->action(UIActionIndexRT_M_View_T_Fullscreen), &UIAction::trigger);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigCloseAction,
                actionPool()->action(UIActionIndex_M_Application_S_Close), &UIAction::trigger);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigNotifyAboutWindowActivationStolen,
                this, &UIMachineWindowFullscreen::sltRevokeWindowActivation, Qt::QueuedConnection);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigAutoHideToggled,
                this, &UIMachineWindowFullscreen::sltHandleMiniToolBarAutoHideToggled);
    }
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowFullscreen::cleanupMiniToolbar()
{
    /* Delete mini-toolbar: */
    delete m_pMiniToolBar;
    m_pMiniToolBar = 0;
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

void UIMachineWindowFullscreen::cleanupVisualState()
{
#ifdef VBOX_WS_MAC
    /* Unregister from native fullscreen notifications: */
    UICocoaApplication::instance()->unregisterFromNotificationOfWindow("NSWindowWillEnterFullScreenNotification", this);
    UICocoaApplication::instance()->unregisterFromNotificationOfWindow("NSWindowDidEnterFullScreenNotification", this);
    UICocoaApplication::instance()->unregisterFromNotificationOfWindow("NSWindowWillExitFullScreenNotification", this);
    UICocoaApplication::instance()->unregisterFromNotificationOfWindow("NSWindowDidExitFullScreenNotification", this);
    UICocoaApplication::instance()->unregisterFromNotificationOfWindow("NSWindowDidFailToEnterFullScreenNotification", this);
#endif /* VBOX_WS_MAC */

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Cleanup mini-toolbar: */
    cleanupMiniToolbar();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

    /* Call to base-class: */
    UIMachineWindow::cleanupVisualState();
}

void UIMachineWindowFullscreen::cleanupNotificationCenter()
{
    if (gpNotificationCenter && (gpNotificationCenter->parent() == centralWidget()))
        gpNotificationCenter->setParent(0);
}

void UIMachineWindowFullscreen::placeOnScreen()
{
    /* Make sure this window has fullscreen logic: */
    UIMachineLogicFullscreen *pFullscreenLogic = qobject_cast<UIMachineLogicFullscreen*>(machineLogic());
    AssertPtrReturnVoid(pFullscreenLogic);

    /* Get corresponding host-screen: */
    const int iHostScreen = pFullscreenLogic->hostScreenForGuestScreen(m_uScreenId);
    /* And corresponding working area: */
    const QRect workingArea = gpDesktop->screenGeometry(iHostScreen);
    Q_UNUSED(workingArea);

#if defined(VBOX_WS_MAC)

    /* Move window to the appropriate position: */
    move(workingArea.topLeft());

    /* Resize window to the appropriate size if it's screen has no own user-space: */
    if (!pFullscreenLogic->screensHaveSeparateSpaces() && m_uScreenId != 0)
        resize(workingArea.size());
    /* Resize the window if we are already in the full screen mode. This covers cases like host-resolution changes while in full screen mode: */
    else if (darwinIsInFullscreenMode(this))
        resize(workingArea.size());
    else
    {
        /* Load normal geometry first of all: */
        QRect geo = gEDataManager->machineWindowGeometry(UIVisualStateType_Normal, m_uScreenId, uiCommon().managedVMUuid());
        /* If normal geometry is null => use frame-buffer size: */
        if (geo.isNull())
        {
            const QSize guestScreenSize = uimachine()->guestScreenSize(m_uScreenId);
            geo = QRect(QPoint(0, 0), guestScreenSize.boundedTo(workingArea.size()));
        }
        /* If normal geometry still null => use default size: */
        if (geo.isNull())
            geo = QRect(QPoint(0, 0), QSize(800, 600).boundedTo(workingArea.size()));
        /* Move window to the center of working-area: */
        geo.moveCenter(workingArea.center());
        UIDesktopWidgetWatchdog::setTopLevelGeometry(this, geo);
    }

#elif defined(VBOX_WS_WIN)

    /* Map window onto required screen: */
    windowHandle()->setScreen(qApp->screens().at(iHostScreen));
    /* Set appropriate window size: */
    resize(workingArea.size());

#elif defined(VBOX_WS_NIX)

    /* Determine whether we should use the native full-screen mode: */
    const bool fUseNativeFullScreen =    NativeWindowSubsystem::X11SupportsFullScreenMonitorsProtocol()
                                      && !gEDataManager->legacyFullscreenModeRequested();
    if (fUseNativeFullScreen)
    {
        /* Tell recent window managers which host-screen this window should be mapped to: */
        NativeWindowSubsystem::X11SetFullScreenMonitor(this, pFullscreenLogic->hostScreenForGuestScreen(m_uScreenId));
    }

    /* Set appropriate window geometry: */
    resize(workingArea.size());
    move(workingArea.topLeft());

#else

# warning "port me"

#endif
}

void UIMachineWindowFullscreen::showInNecessaryMode()
{
    /* Make sure window has fullscreen logic: */
    UIMachineLogicFullscreen *pFullscreenLogic = qobject_cast<UIMachineLogicFullscreen*>(machineLogic());
    AssertPtrReturnVoid(pFullscreenLogic);

#if defined(VBOX_WS_MAC)

    /* If window shouldn't be shown or mapped to some host-screen: */
    if (!uimachine()->isScreenVisible(m_uScreenId) ||
        !pFullscreenLogic->hasHostScreenForGuestScreen(m_uScreenId))
    {
        /* Hide window: */
        hide();
    }
    /* If window should be shown and mapped to some host-screen: */
    else
    {
        /* Make sure window have appropriate geometry: */
        placeOnScreen();

        /* Just show instead of showFullScreen: */
        show();

        /* Adjust machine-view size if necessary: */
        adjustMachineViewSize();

        /* Make sure machine-view have focus: */
        m_pMachineView->setFocus();
    }

#elif defined(VBOX_WS_WIN)

    /* If window shouldn't be shown or mapped to some host-screen: */
    if (!uimachine()->isScreenVisible(m_uScreenId) ||
        !pFullscreenLogic->hasHostScreenForGuestScreen(m_uScreenId))
    {
        /* Remember whether the window was minimized: */
        if (isMinimized())
            m_fWasMinimized = true;

        /* Hide window and reset it's state to NONE: */
        setWindowState(Qt::WindowNoState);
        hide();
    }
    /* If window should be shown and mapped to some host-screen: */
    else
    {
        /* Check whether window was minimized: */
        const bool fWasMinimized = isMinimized() && isVisible();
        /* And reset it's state in such case before exposing: */
        if (fWasMinimized)
            setWindowState(Qt::WindowNoState);

        /* Make sure window have appropriate geometry: */
        placeOnScreen();

        /* Show window: */
        showFullScreen();

        /* Restore minimized state if necessary: */
        if (m_fWasMinimized || fWasMinimized)
        {
            m_fWasMinimized = false;
            QMetaObject::invokeMethod(this, "showMinimized", Qt::QueuedConnection);
        }

        /* Adjust machine-view size if necessary: */
        adjustMachineViewSize();

        /* Make sure machine-view have focus: */
        m_pMachineView->setFocus();
    }

#elif defined(VBOX_WS_NIX)

    /* If window shouldn't be shown or mapped to some host-screen: */
    if (!uimachine()->isScreenVisible(m_uScreenId) ||
        !pFullscreenLogic->hasHostScreenForGuestScreen(m_uScreenId))
    {
        /* Remember whether the window was minimized: */
        if (isMinimized())
            m_fWasMinimized = true;

        /* Hide window and reset it's state to NONE: */
        setWindowState(Qt::WindowNoState);
        hide();
    }
    /* If window should be shown and mapped to some host-screen: */
    else
    {
        /* Check whether window was minimized: */
        const bool fWasMinimized = isMinimized() && isVisible();
        /* And reset it's state in such case before exposing: */
        if (fWasMinimized)
            setWindowState(Qt::WindowNoState);

        /* Show window: */
        showFullScreen();

        /* Make sure window have appropriate geometry: */
        placeOnScreen();

        /* Restore full-screen state after placeOnScreen() call: */
        setWindowState(Qt::WindowFullScreen);

        /* Restore minimized state if necessary: */
        if (m_fWasMinimized || fWasMinimized)
        {
            m_fWasMinimized = false;
            QMetaObject::invokeMethod(this, "showMinimized", Qt::QueuedConnection);
        }

        /* Adjust machine-view size if necessary: */
        adjustMachineViewSize();

        /* Make sure machine-view have focus: */
        m_pMachineView->setFocus();
    }

#else

# warning "port me"

#endif
}

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowFullscreen::updateAppearanceOf(int iElement)
{
    /* Call to base-class: */
    UIMachineWindow::updateAppearanceOf(iElement);

    /* Update mini-toolbar: */
    if (iElement & UIVisualElement_MiniToolBar)
    {
        /* If there is a mini-toolbar: */
        if (m_pMiniToolBar)
        {
            /* Get snapshot(s): */
            QString strSnapshotName;
            ulong uSnapshotCount = 0;
            uimachine()->acquireSnapshotCount(uSnapshotCount);
            if (uSnapshotCount > 0)
            {
                QString strCurrentSnapshotName;
                uimachine()->acquireCurrentSnapshotName(strCurrentSnapshotName);
                strSnapshotName = " (" + strCurrentSnapshotName + ")";
            }
            /* Update mini-toolbar text: */
            m_pMiniToolBar->setText(machineName() + strSnapshotName);
        }
    }
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#ifdef VBOX_WS_NIX
void UIMachineWindowFullscreen::changeEvent(QEvent *pEvent)
{
    switch (pEvent->type())
    {
        case QEvent::WindowStateChange:
        {
            /* Watch for window state changes: */
            QWindowStateChangeEvent *pChangeEvent = static_cast<QWindowStateChangeEvent*>(pEvent);
            LogRel2(("GUI: UIMachineWindowFullscreen::changeEvent: Window state changed from %d to %d\n",
                     (int)pChangeEvent->oldState(), (int)windowState()));
            if (   windowState() == Qt::WindowMinimized
                && pChangeEvent->oldState() == Qt::WindowNoState
                && !m_fIsMinimized)
            {
                /* Mark window minimized, isMinimized() is not enough due to Qt5vsX11 fight: */
                LogRel2(("GUI: UIMachineWindowFullscreen::changeEvent: Window minimized\n"));
                m_fIsMinimized = true;
            }
            else
            if (   windowState() == Qt::WindowNoState
                && pChangeEvent->oldState() == Qt::WindowMinimized
                && m_fIsMinimized)
            {
                /* Mark window restored, and do manual restoring with showInNecessaryMode(): */
                LogRel2(("GUI: UIMachineWindowFullscreen::changeEvent: Window restored\n"));
                m_fIsMinimized = false;
                /* Remember that we no more asked to minimize: */
                m_fIsMinimizationRequested = false;
                showInNecessaryMode();
            }
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    UIMachineWindow::changeEvent(pEvent);
}
#endif /* VBOX_WS_NIX */

#ifdef VBOX_WS_WIN
void UIMachineWindowFullscreen::showEvent(QShowEvent *pEvent)
{
    /* Expose workaround again,
     * Qt devs will never fix that it seems.
     * This time they forget to set 'Mapped'
     * attribute for initially frame-less window. */
    setAttribute(Qt::WA_Mapped);

    /* Call to base-class: */
    UIMachineWindow::showEvent(pEvent);
}
#endif /* VBOX_WS_WIN */
