/* $Id: UIFrameBuffer.h 108918 2025-03-24 16:07:18Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFrameBuffer class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIFrameBuffer_h
#define FEQT_INCLUDED_SRC_runtime_UIFrameBuffer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSize>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Other VBox includes: */
#include <VBox/com/ptr.h>

/* Forward declarations: */
class UIFrameBufferPrivate;
class UIMachineView;
class QResizeEvent;
class QPaintEvent;
class QRegion;

/** IFramebuffer implementation used to maintain VM display video memory. */
class UIFrameBuffer : public QObject
{
    Q_OBJECT;

public:

    /** Constructs frame-buffer. */
    UIFrameBuffer();
    /** Destructs frame-buffer. */
    virtual ~UIFrameBuffer() RT_OVERRIDE;

    /** Frame-buffer initialization.
      * @param pMachineView defines machine-view this frame-buffer is bounded to. */
    HRESULT init(UIMachineView *pMachineView);
    /** Returns whether frame-buffer was initialized already. */
    bool isInitialized() const { return m_fInitialized; }

    /** Assigns machine-view frame-buffer will be bounded to.
      * @param pMachineView defines machine-view this frame-buffer is bounded to. */
    void setView(UIMachineView *pMachineView);

    /** Attach frame-buffer to the Display. */
    void attach();
    /** Detach frame-buffer from the Display. */
    void detach();

    /** Returns frame-buffer data address. */
    uchar *address();
    /** Returns frame-buffer width. */
    ulong width() const;
    /** Returns frame-buffer height. */
    ulong height() const;
    /** Returns frame-buffer bits-per-pixel value. */
    ulong bitsPerPixel() const;
    /** Returns frame-buffer bytes-per-line value. */
    ulong bytesPerLine() const;

    /** Defines whether frame-buffer is <b>unused</b>.
      * @note Calls to this and any other EMT callback are synchronized (from GUI side). */
    void setMarkAsUnused(bool fUnused);

    /** Returns the frame-buffer's scaled-size. */
    QSize scaledSize() const;
    /** Defines host-to-guest scale ratio as @a size. */
    void setScaledSize(const QSize &size = QSize());
    /** Returns x-origin of the guest (actual) content corresponding to x-origin of host (scaled) content. */
    int convertHostXTo(int iX) const;
    /** Returns y-origin of the guest (actual) content corresponding to y-origin of host (scaled) content. */
    int convertHostYTo(int iY) const;

    /** Returns the scale-factor used by the frame-buffer. */
    double scaleFactor() const;
    /** Define the scale-factor used by the frame-buffer. */
    void setScaleFactor(double dScaleFactor);

    /** Returns device-pixel-ratio set for HiDPI frame-buffer. */
    double devicePixelRatio() const;
    /** Defines device-pixel-ratio set for HiDPI frame-buffer. */
    void setDevicePixelRatio(double dDevicePixelRatio);

    /** Returns whether frame-buffer should use unscaled HiDPI output. */
    bool useUnscaledHiDPIOutput() const;
    /** Defines whether frame-buffer should use unscaled HiDPI output. */
    void setUseUnscaledHiDPIOutput(bool fUseUnscaledHiDPIOutput);

    /** Returns the frame-buffer scaling optimization type. */
    ScalingOptimizationType scalingOptimizationType() const;
    /** Defines the frame-buffer scaling optimization type. */
    void setScalingOptimizationType(ScalingOptimizationType type);

    /** Handles frame-buffer notify-change-event. */
    void handleNotifyChange(int iWidth, int iHeight);
    /** Handles frame-buffer paint-event. */
    void handlePaintEvent(QPaintEvent *pEvent);
    /** Handles frame-buffer set-visible-region-event. */
    void handleSetVisibleRegion(const QRegion &region);

    /** Performs frame-buffer resizing. */
    void performResize(int iWidth, int iHeight);
    /** Performs frame-buffer rescaling. */
    void performRescale();

    /** Handles viewport resize-event. */
    void viewportResized(QResizeEvent *pEvent);

private:

    /** Prepares everything. */
    void prepare();
    /** Cleanups everything. */
    void cleanup();

    /** Holds the frame-buffer private instance. */
    ComObjPtr<UIFrameBufferPrivate>  m_pFrameBuffer;

    /** Holds whether frame-buffer was initialized already. */
    bool  m_fInitialized;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIFrameBuffer_h */
