/* $Id: UIMachineViewFullscreen.cpp 107703 2025-01-10 10:58:41Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineViewFullscreen class implementation.
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
#include <QResizeEvent>
#include <QMainWindow>
#include <QTimer>
#ifdef VBOX_WS_MAC
# include <QMenuBar>
#endif

/* GUI includes: */
#include "UIActionPoolRuntime.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogicFullscreen.h"
#include "UIMachineViewFullscreen.h"
#include "UIMachineWindow.h"

/* External includes: */
#ifdef VBOX_WS_NIX
# include <limits.h>
#endif


UIMachineViewFullscreen::UIMachineViewFullscreen(UIMachineWindow *pMachineWindow, ulong uScreenId)
    : UIMachineView(pMachineWindow, uScreenId)
    , m_fGuestAutoresizeEnabled(actionPool()->action(UIActionIndexRT_M_View_T_GuestAutoresize)->isChecked())
{
}

void UIMachineViewFullscreen::sltAdditionsStateChanged()
{
    adjustGuestScreenSize();
}

bool UIMachineViewFullscreen::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    if (pWatched != 0 && pWatched == machineWindow())
    {
        switch (pEvent->type())
        {
            case QEvent::Resize:
            {
                /* Send guest-resize hint only if top window resizing to required dimension: */
                QResizeEvent *pResizeEvent = static_cast<QResizeEvent*>(pEvent);
                if (pResizeEvent->size() != calculateMaxGuestSize())
                    break;

                /* Recalculate maximum guest size: */
                setMaximumGuestSize();

                break;
            }
            default:
                break;
        }
    }

    return UIMachineView::eventFilter(pWatched, pEvent);
}

void UIMachineViewFullscreen::prepareCommon()
{
    /* Base class common settings: */
    UIMachineView::prepareCommon();

    /* Setup size-policy: */
    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum));
    /* Maximum size to sizehint: */
    setMaximumSize(sizeHint());
    /* Minimum size is ignored: */
    setMinimumSize(0, 0);
    /* No scrollbars: */
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void UIMachineViewFullscreen::prepareFilters()
{
    /* Base class filters: */
    UIMachineView::prepareFilters();
}

void UIMachineViewFullscreen::prepareConsoleConnections()
{
    /* Base class connections: */
    UIMachineView::prepareConsoleConnections();

    /* Guest additions state-change updater: */
    connect(uimachine(), &UIMachine::sigAdditionsStateActualChange, this, &UIMachineViewFullscreen::sltAdditionsStateChanged);
}

void UIMachineViewFullscreen::setGuestAutoresizeEnabled(bool fEnabled)
{
    if (m_fGuestAutoresizeEnabled != fEnabled)
    {
        m_fGuestAutoresizeEnabled = fEnabled;

        if (m_fGuestAutoresizeEnabled && uimachine()->isGuestSupportsGraphics())
            sltPerformGuestResize();
    }
}

void UIMachineViewFullscreen::adjustGuestScreenSize()
{
    /* Step 0: Is machine running or paused? */
    if (!uimachine()->isRunning() && !uimachine()->isPaused())
    {
        LogRel(("GUI: UIMachineViewFullscreen::adjustGuestScreenSize: "
                "Guest-screen #%d display is not initialized, adjustment is not possible.\n",
                screenId()));
        return;
    }
    /* Step 1: Is guest-screen visible? */
    if (!uimachine()->isScreenVisible(screenId()))
    {
        LogRel(("GUI: UIMachineViewFullscreen::adjustGuestScreenSize: "
                "Guest-screen #%d is not visible, adjustment is not required.\n",
                screenId()));
        return;
    }
    /* Step 2: Is guest-screen auto-resize enabled? */
    if (!isGuestAutoresizeEnabled())
    {
        LogRel(("GUI: UIMachineViewFullscreen::adjustGuestScreenSize: "
                "Guest-screen #%d auto-resize is disabled, adjustment is not required.\n",
                screenId()));
        return;
    }

    /* What are the desired and requested hints? */
    const QSize sizeToApply = calculateMaxGuestSize();
    const QSize desiredSizeHint = scaledBackward(sizeToApply);
    const QSize requestedSizeHint = requestedGuestScreenSizeHint();

    /* Step 3: Is the guest-screen of another size than necessary? */
    if (desiredSizeHint == requestedSizeHint)
    {
        LogRel(("GUI: UIMachineViewFullscreen::adjustGuestScreenSize: "
                "Desired hint %dx%d for guest-screen #%d is already in IDisplay, adjustment is not required.\n",
                desiredSizeHint.width(), desiredSizeHint.height(), screenId()));
        return;
    }

    /* Final step: Adjust .. */
    LogRel(("GUI: UIMachineViewFullscreen::adjustGuestScreenSize: "
            "Desired hint %dx%d for guest-screen #%d differs from the one in IDisplay, adjustment is required.\n",
            desiredSizeHint.width(), desiredSizeHint.height(), screenId()));
    sltPerformGuestResize(sizeToApply);
    /* And remember the size to know what we are resizing out of when we exit: */
    uimachine()->setLastFullScreenSize(screenId(), scaledForward(desiredSizeHint));
}

QRect UIMachineViewFullscreen::workingArea() const
{
    /* Get corresponding screen: */
    int iScreen = static_cast<UIMachineLogicFullscreen*>(machineLogic())->hostScreenForGuestScreen(screenId());
    /* Return available geometry for that screen: */
    return gpDesktop->screenGeometry(iScreen);
}

QSize UIMachineViewFullscreen::calculateMaxGuestSize() const
{
    return workingArea().size();
}
