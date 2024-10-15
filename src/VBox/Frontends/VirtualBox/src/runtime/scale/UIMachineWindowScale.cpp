/* $Id: UIMachineWindowScale.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindowScale class implementation.
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
#include <QTimerEvent>
#include <QSpacerItem>
#include <QResizeEvent>
#ifdef VBOX_WS_NIX
# include <QTimer>
#endif

/* GUI includes: */
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineWindowScale.h"
#include "UIMachineView.h"
#include "UINotificationCenter.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils.h"
# include "UIImageTools.h"
# include "UICocoaApplication.h"
# include "UIVersion.h"
#endif


UIMachineWindowScale::UIMachineWindowScale(UIMachineLogic *pMachineLogic, ulong uScreenId)
    : UIMachineWindow(pMachineLogic, uScreenId)
    , m_iGeometrySaveTimerId(-1)
{
}

void UIMachineWindowScale::prepareMainLayout()
{
    /* Call to base-class: */
    UIMachineWindow::prepareMainLayout();

    /* Strict spacers to hide them, they are not necessary for scale-mode: */
    m_pTopSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_pBottomSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_pLeftSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_pRightSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void UIMachineWindowScale::prepareNotificationCenter()
{
    if (gpNotificationCenter && (m_uScreenId == 0))
        gpNotificationCenter->setParent(centralWidget());
}

#ifdef VBOX_WS_MAC
void UIMachineWindowScale::prepareVisualState()
{
    /* Call to base-class: */
    UIMachineWindow::prepareVisualState();

    /* Beta label? */
    if (UIVersionInfo::showBetaLabel())
    {
        QPixmap betaLabel = ::betaLabel(QSize(74, darwinWindowTitleHeight(this) - 1));
        ::darwinLabelWindow(this, &betaLabel);
    }

    /* Enable fullscreen support for every screen which requires it: */
    if (darwinScreensHaveSeparateSpaces() || m_uScreenId == 0)
        darwinEnableFullscreenSupport(this);
    /* Register 'Zoom' button to use our full-screen: */
    UICocoaApplication::instance()->registerCallbackForStandardWindowButton(this, StandardWindowButtonType_Zoom,
                                                                            UIMachineWindow::handleStandardWindowButtonCallback);
}
#endif /* VBOX_WS_MAC */

void UIMachineWindowScale::loadSettings()
{
    /* Call to base-class: */
    UIMachineWindow::loadSettings();

    /* Load extra-data settings: */
    {
        /* Load extra-data: */
        QRect geo = gEDataManager->machineWindowGeometry(machineLogic()->visualStateType(),
                                                         m_uScreenId, uiCommon().managedVMUuid());

        /* If we do have proper geometry: */
        if (!geo.isNull())
        {
            /* Restore window geometry: */
            m_geometry = geo;
            UIDesktopWidgetWatchdog::setTopLevelGeometry(this, m_geometry);

            /* Maximize (if necessary): */
            if (gEDataManager->machineWindowShouldBeMaximized(machineLogic()->visualStateType(),
                                                              m_uScreenId, uiCommon().managedVMUuid()))
                setWindowState(windowState() | Qt::WindowMaximized);
        }
        /* If we do NOT have proper geometry: */
        else
        {
            /* Get available geometry, for screen with (x,y) coords if possible: */
            QRect availableGeo = !geo.isNull() ? gpDesktop->availableGeometry(QPoint(geo.x(), geo.y())) :
                                                 gpDesktop->availableGeometry(this);

            /* Resize to default size: */
            resize(640, 480);
            /* Move newly created window to the screen-center: */
            m_geometry = geometry();
            m_geometry.moveCenter(availableGeo.center());
            UIDesktopWidgetWatchdog::setTopLevelGeometry(this, m_geometry);
        }

        /* Normalize to the optimal size: */
#ifdef VBOX_WS_NIX
        QTimer::singleShot(0, this, SLOT(sltNormalizeGeometry()));
#else /* !VBOX_WS_NIX */
        normalizeGeometry(true /* adjust position */, true /* resize to fit guest display. ignored in scaled case */);
#endif /* !VBOX_WS_NIX */
    }
}

#ifdef VBOX_WS_MAC
void UIMachineWindowScale::cleanupVisualState()
{
    /* Unregister 'Zoom' button from using our full-screen: */
    UICocoaApplication::instance()->unregisterCallbackForStandardWindowButton(this, StandardWindowButtonType_Zoom);
}
#endif /* VBOX_WS_MAC */

void UIMachineWindowScale::cleanupNotificationCenter()
{
    if (gpNotificationCenter && (gpNotificationCenter->parent() == centralWidget()))
        gpNotificationCenter->setParent(0);
}

void UIMachineWindowScale::showInNecessaryMode()
{
    /* Make sure this window should be shown at all: */
    if (!uimachine()->isScreenVisible(m_uScreenId))
        return hide();

    /* Make sure this window is not minimized: */
    if (isMinimized())
        return;

    /* Show in normal mode: */
    show();

    /* Make sure machine-view have focus: */
    m_pMachineView->setFocus();
}

void UIMachineWindowScale::restoreCachedGeometry()
{
    /* Restore the geometry cached by the window: */
    resize(m_geometry.size());
    move(m_geometry.topLeft());

    /* Adjust machine-view accordingly: */
    adjustMachineViewSize();
}

void UIMachineWindowScale::normalizeGeometry(bool fAdjustPosition, bool fResizeToGuestDisplay)
{
    Q_UNUSED(fResizeToGuestDisplay);
    /* Skip if maximized: */
    if (isMaximized())
        return;

    /* Calculate client window offsets: */
    QRect frGeo = frameGeometry();
    const QRect geo = geometry();
    const int dl = geo.left() - frGeo.left();
    const int dt = geo.top() - frGeo.top();
    const int dr = frGeo.right() - geo.right();
    const int db = frGeo.bottom() - geo.bottom();

    /* Adjust position if necessary: */
    if (fAdjustPosition)
        frGeo = UIDesktopWidgetWatchdog::normalizeGeometry(frGeo, gpDesktop->overallAvailableRegion());

    /* Finally, set the frame geometry: */
    UIDesktopWidgetWatchdog::setTopLevelGeometry(this, frGeo.left() + dl, frGeo.top() + dt,
                                    frGeo.width() - dl - dr, frGeo.height() - dt - db);
}

bool UIMachineWindowScale::event(QEvent *pEvent)
{
    switch (pEvent->type())
    {
        case QEvent::Resize:
        {
#ifdef VBOX_WS_NIX
            /* Prevent handling if fake screen detected: */
            if (UIDesktopWidgetWatchdog::isFakeScreenDetected())
                break;
#endif /* VBOX_WS_NIX */

            QResizeEvent *pResizeEvent = static_cast<QResizeEvent*>(pEvent);
            if (!isMaximizedChecked())
            {
                m_geometry.setSize(pResizeEvent->size());
#ifdef VBOX_WITH_DEBUGGER_GUI
                /* Update debugger window position: */
                updateDbgWindows();
#endif /* VBOX_WITH_DEBUGGER_GUI */
            }

            /* Restart geometry save timer: */
            if (m_iGeometrySaveTimerId != -1)
                killTimer(m_iGeometrySaveTimerId);
            m_iGeometrySaveTimerId = startTimer(300);
            break;
        }
        case QEvent::Move:
        {
#ifdef VBOX_WS_NIX
            /* Prevent handling if fake screen detected: */
            if (UIDesktopWidgetWatchdog::isFakeScreenDetected())
                break;
#endif /* VBOX_WS_NIX */

            if (!isMaximizedChecked())
            {
                m_geometry.moveTo(geometry().x(), geometry().y());
#ifdef VBOX_WITH_DEBUGGER_GUI
                /* Update debugger window position: */
                updateDbgWindows();
#endif /* VBOX_WITH_DEBUGGER_GUI */
            }

            /* Restart geometry save timer: */
            if (m_iGeometrySaveTimerId != -1)
                killTimer(m_iGeometrySaveTimerId);
            m_iGeometrySaveTimerId = startTimer(300);
            break;
        }
        /* Handle timer event started above: */
        case QEvent::Timer:
        {
            QTimerEvent *pTimerEvent = static_cast<QTimerEvent*>(pEvent);
            if (pTimerEvent->timerId() == m_iGeometrySaveTimerId)
            {
                killTimer(m_iGeometrySaveTimerId);
                m_iGeometrySaveTimerId = -1;
                LogRel2(("GUI: UIMachineWindowScale: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
                         m_geometry.x(), m_geometry.y(), m_geometry.width(), m_geometry.height()));
                gEDataManager->setMachineWindowGeometry(machineLogic()->visualStateType(),
                                                        m_uScreenId, m_geometry,
                                                        isMaximizedChecked(), uiCommon().managedVMUuid());
            }
            break;
        }
        default:
            break;
    }
    return UIMachineWindow::event(pEvent);
}

bool UIMachineWindowScale::isMaximizedChecked()
{
#ifdef VBOX_WS_MAC
    /* On the Mac the WindowStateChange signal doesn't seems to be delivered
     * when the user get out of the maximized state. So check this ourself. */
    return ::darwinIsWindowMaximized(this);
#else /* VBOX_WS_MAC */
    return isMaximized();
#endif /* !VBOX_WS_MAC */
}
