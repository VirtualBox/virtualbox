/* $Id: UIMachineViewScale.cpp 108918 2025-03-24 16:07:18Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineViewScale class implementation.
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
#include <QMainWindow>
#include <QTimer>

/* GUI includes */
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineViewScale.h"
#include "UIMachineWindow.h"

/* COM includes: */
#include "CGraphicsAdapter.h"

/* Other VBox includes: */
#include <VBox/VBoxOGL.h> // For VBOX_OGL_SCALE_FACTOR_MULTIPLIER


UIMachineViewScale::UIMachineViewScale(UIMachineWindow *pMachineWindow, ulong uScreenId)
    : UIMachineView(pMachineWindow, uScreenId)
{
}

void UIMachineViewScale::sltPerformGuestScale()
{
    /* Assign new frame-buffer logical-size: */
    QSize scaledSize = size();
    const bool fUseUnscaledHiDPIOutput = frameBuffer()->useUnscaledHiDPIOutput();
    const double dDevicePixelRatio = frameBuffer()->devicePixelRatio();
    if (fUseUnscaledHiDPIOutput)
        scaledSize *= dDevicePixelRatio;
    frameBuffer()->setScaledSize(scaledSize);
    frameBuffer()->performRescale();

    /* If scaled-size is valid: */
    if (scaledSize.isValid())
    {
        /* Propagate scale-factor to 3D service if necessary: */
        bool fAccelerate3DEnabled = false;
        uimachine()->acquireWhetherAccelerate3DEnabled(fAccelerate3DEnabled);
        if (fAccelerate3DEnabled)
        {
            double xScaleFactor = (double)scaledSize.width()  / frameBuffer()->width();
            double yScaleFactor = (double)scaledSize.height() / frameBuffer()->height();
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
            // WORKAROUND:
            // On Windows and Linux opposing to macOS it's only Qt which can auto scale up,
            // not 3D overlay itself, so for auto scale-up mode we have to take that into account.
            if (!fUseUnscaledHiDPIOutput)
            {
                xScaleFactor *= dDevicePixelRatio;
                yScaleFactor *= dDevicePixelRatio;
            }
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
            uimachine()->notifyScaleFactorChange(m_uScreenId,
                                                 (uint32_t)(xScaleFactor * VBOX_OGL_SCALE_FACTOR_MULTIPLIER),
                                                 (uint32_t)(yScaleFactor * VBOX_OGL_SCALE_FACTOR_MULTIPLIER));
        }
    }

    /* Scale the pause-pixmap: */
    updateScaledPausePixmap();

    /* Update viewport: */
    viewport()->repaint();

    /* Update machine-view sliders: */
    updateSliders();
}

bool UIMachineViewScale::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    if (pWatched != 0 && pWatched == viewport())
    {
        switch (pEvent->type())
        {
            case QEvent::Resize:
            {
                /* Perform the actual resize: */
                sltPerformGuestScale();
                break;
            }
            default:
                break;
        }
    }

    return UIMachineView::eventFilter(pWatched, pEvent);
}

void UIMachineViewScale::applyMachineViewScaleFactor()
{
    /* If scaled-size is valid: */
    const QSize scaledSize = frameBuffer()->scaledSize();
    const bool fUseUnscaledHiDPIOutput = frameBuffer()->useUnscaledHiDPIOutput();
    if (scaledSize.isValid())
    {
        /* Propagate scale-factor to 3D service if necessary: */
        bool fAccelerate3DEnabled = false;
        uimachine()->acquireWhetherAccelerate3DEnabled(fAccelerate3DEnabled);
        if (fAccelerate3DEnabled)
        {
            double xScaleFactor = (double)scaledSize.width()  / frameBuffer()->width();
            double yScaleFactor = (double)scaledSize.height() / frameBuffer()->height();
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
            // WORKAROUND:
            // On Windows and Linux opposing to macOS it's only Qt which can auto scale up,
            // not 3D overlay itself, so for auto scale-up mode we have to take that into account.
            if (!fUseUnscaledHiDPIOutput)
            {
                const double dDevicePixelRatio = frameBuffer()->devicePixelRatio();
                xScaleFactor *= dDevicePixelRatio;
                yScaleFactor *= dDevicePixelRatio;
            }
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
            uimachine()->notifyScaleFactorChange(m_uScreenId,
                                                 (uint32_t)(xScaleFactor * VBOX_OGL_SCALE_FACTOR_MULTIPLIER),
                                                 (uint32_t)(yScaleFactor * VBOX_OGL_SCALE_FACTOR_MULTIPLIER));
        }
    }

    /* Take unscaled HiDPI output mode into account: */
    frameBuffer()->setUseUnscaledHiDPIOutput(fUseUnscaledHiDPIOutput);
    /* Propagate unscaled-hidpi-output feature to 3D service if necessary: */
    bool fAccelerate3DEnabled = false;
    uimachine()->acquireWhetherAccelerate3DEnabled(fAccelerate3DEnabled);
    if (fAccelerate3DEnabled)
        uimachine()->notifyHiDPIOutputPolicyChange(fUseUnscaledHiDPIOutput);

    /* Perform frame-buffer rescaling: */
    frameBuffer()->performRescale();

    /* Update console's display viewport and 3D overlay: */
    updateViewport();
}

void UIMachineViewScale::resendSizeHint()
{
    /* Skip if VM isn't running/paused yet: */
    if (   !uimachine()->isRunning()
        && !uimachine()->isPaused())
        return;

    /* Get the last guest-screen size-hint, taking the scale factor into account. */
    const QSize sizeHint = scaledBackward(storedGuestScreenSizeHint());
    LogRel(("GUI: UIMachineViewScale::resendSizeHint: Restoring guest size-hint for screen %d to %dx%d\n",
            (int)screenId(), sizeHint.width(), sizeHint.height()));

    /* Expand current limitations: */
    setMaximumGuestSize(sizeHint);

    /* Send saved size-hint to the guest: */
    uimachine()->setScreenVisibleHostDesires(screenId(), guestScreenVisibilityStatus());
    uimachine()->setVideoModeHint(screenId(),
                                  guestScreenVisibilityStatus(),
                                  false /* change origin? */,
                                  0 /* origin x */, 0 /* origin y */,
                                  sizeHint.width(), sizeHint.height(),
                                  0 /* bits per pixel */,
                                  true /* notify? */);
}

QSize UIMachineViewScale::sizeHint() const
{
    /* Base-class have its own thoughts about size-hint
     * but scale-mode needs no size-hint to be set: */
    return QSize();
}

QRect UIMachineViewScale::workingArea() const
{
    return gpDesktop->availableGeometry(this);
}

QSize UIMachineViewScale::calculateMaxGuestSize() const
{
    /* 1) The calculation below is not reliable on some (X11) platforms until we
     *    have been visible for a fraction of a second, so so the best we can
     *    otherwise.
     * 2) We also get called early before "machineWindow" has been fully
     *    initialised, at which time we can't perform the calculation. */
    if (!isVisible())
        return workingArea().size() * 0.95;
    /* The area taken up by the machine window on the desktop, including window
     * frame, title, menu bar and status bar. */
    QSize windowSize = machineWindow()->frameGeometry().size();
    /* The window shouldn't be allowed to expand beyond the working area
     * unless it already does.  In that case the guest shouldn't expand it
     * any further though. */
    QSize maximumSize = workingArea().size().expandedTo(windowSize);
    /* The current size of the machine display. */
    QSize centralWidgetSize = machineWindow()->centralWidget()->size();
    /* To work out how big the guest display can get without the window going
     * over the maximum size we calculated above, we work out how much space
     * the other parts of the window (frame, menu bar, status bar and so on)
     * take up and subtract that space from the maximum window size. The
     * central widget shouldn't be bigger than the window, but we bound it for
     * sanity (or insanity) reasons. */
    return maximumSize - (windowSize - centralWidgetSize.boundedTo(windowSize));
}

void UIMachineViewScale::updateSliders()
{
    if (horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff)
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff)
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}
