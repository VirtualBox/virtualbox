/* $Id: UIMouseHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMouseHandler class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIMouseHandler_h
#define FEQT_INCLUDED_SRC_runtime_UIMouseHandler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class QTouchEvent;
class QWidget;
class UIMachine;
class UIMachineLogic;
class UIMachineView;
class UIMachineWindow;


/* Delegate to control VM mouse functionality: */
class UIMouseHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about state-change. */
    void sigStateChange(int iState);

public:

    /* Factory functions to create/destroy mouse-handler: */
    static UIMouseHandler* create(UIMachineLogic *pMachineLogic, UIVisualStateType visualStateType);
    static void destroy(UIMouseHandler *pMouseHandler);

    /* Prepare/cleanup listener for particular machine-window: */
    void prepareListener(ulong uIndex, UIMachineWindow *pMachineWindow);
    void cleanupListener(ulong uIndex);

    /* Commands to capture/release mouse: */
    void captureMouse(ulong uScreenId);
    void releaseMouse();

    /* Setter for mouse-integration feature: */
    void setMouseIntegrationEnabled(bool fEnabled);

    /* Current mouse state: */
    int state() const;

    /** Qt5: Performs pre-processing of all the native events. */
    bool nativeEventFilter(void *pMessage, ulong uScreenId);

protected slots:

    /* Machine state-change handler: */
    virtual void sltMachineStateChanged();

    /* Mouse capability-change handler: */
    virtual void sltMouseCapabilityChanged();

    /* Mouse pointer-shape-change handler: */
    virtual void sltMousePointerShapeChanged();

    /** Activate hovered window if any. */
    void sltMaybeActivateHoveredWindow();

protected:

    /* Mouse-handler constructor/destructor: */
    UIMouseHandler(UIMachineLogic *pMachineLogic);
    virtual ~UIMouseHandler();

    /* Getters: */
    UIMachineLogic *machineLogic() const { return m_pMachineLogic; }
    UIMachine *uimachine() const;

    /* Event handler for registered machine-view(s): */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /* Separate function to handle most of existing mouse-events: */
    bool mouseEvent(int iEventType, ulong uScreenId,
                    const QPoint &relativePos, const QPoint &globalPos,
                    Qt::MouseButtons mouseButtons,
                    int wheelDelta, Qt::Orientation wheelDirection);

    /* Separate function to handle incoming multi-touch events: */
    bool multiTouchEvent(QTouchEvent *pTouchEvent, ulong uScreenId);

#ifdef VBOX_WS_WIN
    /* This method is actually required only because under win-host
     * we do not really grab the mouse in case of capturing it: */
    void updateMouseCursorClipping();
    QRect m_mouseCursorClippingRect;
#endif /* VBOX_WS_WIN */

    /* Machine logic parent: */
    UIMachineLogic *m_pMachineLogic;

    /* Registered machine-windows(s): */
    QMap<ulong, QWidget*> m_windows;
    /* Registered machine-view(s): */
    QMap<ulong, UIMachineView*> m_views;
    /* Registered machine-view-viewport(s): */
    QMap<ulong, QWidget*> m_viewports;

    /** Hovered window to be activated. */
    QPointer<QWidget> m_pHoveredWindow;

    /* Other mouse variables: */
    QPoint m_lastMousePos;
    QPoint m_capturedMousePos;
    QRect m_capturedScreenGeo;
    int m_iLastMouseWheelDelta;
    int m_iMouseCaptureViewIndex;

#ifdef VBOX_WS_WIN
    /** Holds whether cursor position was just
      * reseted to simulate infinite mouse moving. */
    bool m_fCursorPositionReseted;
#endif
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIMouseHandler_h */
