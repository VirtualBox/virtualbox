/* $Id: UIMachineWindowSeamless.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindowSeamless class implementation.
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
#include <QMenu>
#include <QSpacerItem>
#include <QTimer>
#include <QWindowStateChangeEvent>

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogicSeamless.h"
#include "UIMachineView.h"
#include "UIMachineWindowSeamless.h"
#if   defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
# include "UIMachineDefs.h"
# include "UIMiniToolBar.h"
#elif defined(VBOX_WS_MAC)
# include "VBoxUtils.h"
#endif /* VBOX_WS_MAC */

/* COM includes: */
#include "CSnapshot.h"


UIMachineWindowSeamless::UIMachineWindowSeamless(UIMachineLogic *pMachineLogic, ulong uScreenId)
    : UIMachineWindow(pMachineLogic, uScreenId)
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    , m_pMiniToolBar(0)
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
    , m_fWasMinimized(false)
#ifdef VBOX_WS_NIX
    , m_fIsMinimizationRequested(false)
    , m_fIsMinimized(false)
#endif
{
}

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowSeamless::sltMachineStateChanged()
{
    /* Call to base-class: */
    UIMachineWindow::sltMachineStateChanged();

    /* Update mini-toolbar: */
    updateAppearanceOf(UIVisualElement_MiniToolBar);
}

void UIMachineWindowSeamless::sltRevokeWindowActivation()
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

void UIMachineWindowSeamless::sltHandleMiniToolBarAutoHideToggled(bool fEnabled)
{
    /* Save mini-toolbar settings: */
    gEDataManager->setAutoHideMiniToolbar(fEnabled, uiCommon().managedVMUuid());
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

void UIMachineWindowSeamless::sltShowMinimized()
{
#ifdef VBOX_WS_NIX
    /* Remember that we are asked to minimize: */
    m_fIsMinimizationRequested = true;
#endif

    showMinimized();
}

void UIMachineWindowSeamless::prepareVisualState()
{
    /* Call to base-class: */
    UIMachineWindow::prepareVisualState();

    /* Make sure we have no background
     * until the first one paint-event: */
    setAttribute(Qt::WA_NoSystemBackground);

#ifdef VBOX_WITH_TRANSLUCENT_SEAMLESS
    /* Using Qt API to enable translucent background: */
    setAttribute(Qt::WA_TranslucentBackground);
#endif /* VBOX_WITH_TRANSLUCENT_SEAMLESS */

#ifdef VBOX_WITH_MASKED_SEAMLESS
    /* Make sure we have no background
     * until the first one set-region-event: */
    setMask(m_maskGuest);
#endif /* VBOX_WITH_MASKED_SEAMLESS */

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Prepare mini-toolbar: */
    prepareMiniToolbar();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
}

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowSeamless::prepareMiniToolbar()
{
    /* Make sure mini-toolbar is not restricted: */
    if (!gEDataManager->miniToolbarEnabled(uiCommon().managedVMUuid()))
        return;

    /* Create mini-toolbar: */
    m_pMiniToolBar = new UIMiniToolBar(this,
                                       GeometryType_Available,
                                       gEDataManager->miniToolbarAlignment(uiCommon().managedVMUuid()),
                                       gEDataManager->autoHideMiniToolbar(uiCommon().managedVMUuid()),
                                       screenId());
    AssertPtrReturnVoid(m_pMiniToolBar);
    {
        /* Configure mini-toolbar: */
        m_pMiniToolBar->addMenus(actionPool()->menus());
        connect(m_pMiniToolBar, &UIMiniToolBar::sigMinimizeAction,
                this, &UIMachineWindowSeamless::sltShowMinimized, Qt::QueuedConnection);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigExitAction,
                actionPool()->action(UIActionIndexRT_M_View_T_Seamless), &UIAction::trigger);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigCloseAction,
                actionPool()->action(UIActionIndex_M_Application_S_Close), &UIAction::trigger);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigNotifyAboutWindowActivationStolen,
                this, &UIMachineWindowSeamless::sltRevokeWindowActivation, Qt::QueuedConnection);
        connect(m_pMiniToolBar, &UIMiniToolBar::sigAutoHideToggled,
                this, &UIMachineWindowSeamless::sltHandleMiniToolBarAutoHideToggled);
    }
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowSeamless::cleanupMiniToolbar()
{
    /* Delete mini-toolbar: */
    delete m_pMiniToolBar;
    m_pMiniToolBar = 0;
}
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

void UIMachineWindowSeamless::cleanupVisualState()
{
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /* Cleanup mini-toolbar: */
    cleanupMiniToolbar();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

    /* Call to base-class: */
    UIMachineWindow::cleanupVisualState();
}

void UIMachineWindowSeamless::placeOnScreen()
{
    /* Make sure this window has seamless logic: */
    UIMachineLogicSeamless *pSeamlessLogic = qobject_cast<UIMachineLogicSeamless*>(machineLogic());
    AssertPtrReturnVoid(pSeamlessLogic);

    /* Get corresponding host-screen: */
    const int iHostScreen = pSeamlessLogic->hostScreenForGuestScreen(m_uScreenId);
    /* And corresponding working area: */
    const QRect workingArea = gpDesktop->availableGeometry(iHostScreen);
    Q_UNUSED(workingArea);

#ifdef VBOX_WS_NIX

    /* Make sure we are located on corresponding host-screen: */
    if (   UIDesktopWidgetWatchdog::screenCount() > 1
        && (x() != workingArea.x() || y() != workingArea.y()))
    {
        // WORKAROUND:
        // With Qt5 on KDE we can't just move the window onto desired host-screen if
        // window is maximized. So we have to show it normal first of all:
        if (isVisible() && isMaximized())
            showNormal();

        // WORKAROUND:
        // With Qt5 on X11 we can't just move the window onto desired host-screen if
        // window size is more than the available geometry (working area) of that
        // host-screen. So we are resizing it to a smaller size first of all:
        const QSize newSize = workingArea.size() * .9;
        LogRel(("GUI: UIMachineWindowSeamless::placeOnScreen: Resize window: %d to smaller size: %dx%d\n",
                m_uScreenId, newSize.width(), newSize.height()));
        resize(newSize);
        /* Move window onto required screen: */
        const QPoint newPosition = workingArea.topLeft();
        LogRel(("GUI: UIMachineWindowSeamless::placeOnScreen: Move window: %d to: %dx%d\n",
                m_uScreenId, newPosition.x(), newPosition.y()));
        move(newPosition);
    }

#else

    /* Set appropriate window geometry: */
    const QSize newSize = workingArea.size();
    LogRel(("GUI: UIMachineWindowSeamless::placeOnScreen: Resize window: %d to: %dx%d\n",
            m_uScreenId, newSize.width(), newSize.height()));
    resize(newSize);
    const QPoint newPosition = workingArea.topLeft();
    LogRel(("GUI: UIMachineWindowSeamless::placeOnScreen: Move window: %d to: %dx%d\n",
            m_uScreenId, newPosition.x(), newPosition.y()));
    move(newPosition);

#endif
}

void UIMachineWindowSeamless::showInNecessaryMode()
{
    /* Make sure window has seamless logic: */
    UIMachineLogicSeamless *pSeamlessLogic = qobject_cast<UIMachineLogicSeamless*>(machineLogic());
    AssertPtrReturnVoid(pSeamlessLogic);

    /* If window shouldn't be shown or mapped to some host-screen: */
    if (!uimachine()->isScreenVisible(m_uScreenId) ||
        !pSeamlessLogic->hasHostScreenForGuestScreen(m_uScreenId))
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

#ifdef VBOX_WS_NIX
        /* Show window: */
        if (!isMaximized())
            showMaximized();
#else
        /* Show window: */
        show();
#endif

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
}

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
void UIMachineWindowSeamless::updateAppearanceOf(int iElement)
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
void UIMachineWindowSeamless::changeEvent(QEvent *pEvent)
{
    switch (pEvent->type())
    {
        case QEvent::WindowStateChange:
        {
            /* Watch for window state changes: */
            QWindowStateChangeEvent *pChangeEvent = static_cast<QWindowStateChangeEvent*>(pEvent);
            LogRel2(("GUI: UIMachineWindowSeamless::changeEvent: Window state changed from %d to %d\n",
                     (int)pChangeEvent->oldState(), (int)windowState()));
            if (   windowState() == Qt::WindowMinimized
                && pChangeEvent->oldState() == Qt::WindowNoState
                && !m_fIsMinimized)
            {
                /* Mark window minimized, isMinimized() is not enough due to Qt5vsX11 fight: */
                LogRel2(("GUI: UIMachineWindowSeamless::changeEvent: Window minimized\n"));
                m_fIsMinimized = true;
            }
            else
            if (   windowState() == Qt::WindowNoState
                && pChangeEvent->oldState() == Qt::WindowMinimized
                && m_fIsMinimized)
            {
                /* Mark window restored, and do manual restoring with showInNecessaryMode(): */
                LogRel2(("GUI: UIMachineWindowSeamless::changeEvent: Window restored\n"));
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
void UIMachineWindowSeamless::showEvent(QShowEvent *pEvent)
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

#ifdef VBOX_WITH_MASKED_SEAMLESS
void UIMachineWindowSeamless::setMask(const QRegion &maskGuest)
{
    /* Remember new guest mask: */
    m_maskGuest = maskGuest;

    /* Prepare full mask: */
    QRegion maskFull(m_maskGuest);

    /* Shift full mask if left or top spacer width is NOT zero: */
    if (m_pLeftSpacer->geometry().width() || m_pTopSpacer->geometry().height())
        maskFull.translate(m_pLeftSpacer->geometry().width(), m_pTopSpacer->geometry().height());

    /* Seamless-window for empty full mask should be empty too,
     * but the QWidget::setMask() wrapper doesn't allow this.
     * Instead, we see a full guest-screen of available-geometry size.
     * So we have to make sure full mask have at least one pixel. */
    if (maskFull.isEmpty())
        maskFull += QRect(0, 0, 1, 1);

    /* Make sure full mask had changed: */
    if (m_maskFull != maskFull)
    {
        /* Compose viewport region to update: */
        QRegion toUpdate = m_maskFull + maskFull;
        /* Remember new full mask: */
        m_maskFull = maskFull;
        /* Assign new full mask: */
        UIMachineWindow::setMask(m_maskFull);
        /* Update viewport region finally: */
        if (m_pMachineView)
            m_pMachineView->viewport()->update(toUpdate);
    }
}
#endif /* VBOX_WITH_MASKED_SEAMLESS */
