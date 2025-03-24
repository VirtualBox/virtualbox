/* $Id: UIMouseHandler.cpp 108918 2025-03-24 16:07:18Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMouseHandler class implementation.
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
#include <QtMath>
#include <QMouseEvent>
#include <QTimer>
#include <QTouchEvent>

/* GUI includes: */
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UIKeyboardHandler.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineView.h"
#include "UIMachineWindow.h"
#include "UIMessageCenter.h"
#include "UIMouseHandler.h"
#include "UINotificationCenter.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
# include "CocoaEventHelper.h"
#endif
#ifdef VBOX_WS_WIN
# include "VBoxUtils-win.h"
#endif
#ifdef VBOX_WS_NIX
# include "VBoxUtils-nix.h"
#endif

/* Other VBox includes: */
#include <iprt/time.h>

/* COM includes: */
#include "KMouseButtonState.h"
#include "KTouchContactState.h"

/* Factory function to create mouse-handler: */
UIMouseHandler* UIMouseHandler::create(UIMachineLogic *pMachineLogic,
                                       UIVisualStateType visualStateType)
{
    /* Prepare mouse-handler: */
    UIMouseHandler *pMouseHandler = 0;
    /* Depending on visual-state type: */
    switch (visualStateType)
    {
        /* For now all the states using common mouse-handler: */
        case UIVisualStateType_Normal:
        case UIVisualStateType_Fullscreen:
        case UIVisualStateType_Seamless:
        case UIVisualStateType_Scale:
            pMouseHandler = new UIMouseHandler(pMachineLogic);
            break;
        default:
            break;
    }
    /* Return prepared mouse-handler: */
    return pMouseHandler;
}

/* Factory function to destroy mouse-handler: */
void UIMouseHandler::destroy(UIMouseHandler *pMouseHandler)
{
    /* Delete mouse-handler: */
    delete pMouseHandler;
}

/* Prepare listener for particular machine-window: */
void UIMouseHandler::prepareListener(ulong uIndex, UIMachineWindow *pMachineWindow)
{
    /* If that window is NOT registered yet: */
    if (!m_windows.contains(uIndex))
    {
        /* Register machine-window: */
        m_windows.insert(uIndex, pMachineWindow);
        /* Install event-filter for machine-window: */
        m_windows[uIndex]->installEventFilter(this);
    }

    /* If that view is NOT registered yet: */
    if (!m_views.contains(uIndex))
    {
        /* Register machine-view: */
        m_views.insert(uIndex, pMachineWindow->machineView());
        /* Install event-filter for machine-view: */
        m_views[uIndex]->installEventFilter(this);
        /* Make machine-view notify mouse-handler about mouse pointer shape change: */
        connect(m_views[uIndex], &UIMachineView::sigMousePointerShapeChange, this, &UIMouseHandler::sltMousePointerShapeChanged);
        /* Make machine-view notify mouse-handler about frame-buffer resize: */
        connect(m_views[uIndex], &UIMachineView::sigFrameBufferResize, this, &UIMouseHandler::sltMousePointerShapeChanged);
    }

    /* If that viewport is NOT registered yet: */
    if (!m_viewports.contains(uIndex))
    {
        /* Register machine-view-viewport: */
        m_viewports.insert(uIndex, pMachineWindow->machineView()->viewport());
        /* Install event-filter for machine-view-viewport: */
        m_viewports[uIndex]->installEventFilter(this);
    }
}

/* Cleanup listener for particular machine-window: */
void UIMouseHandler::cleanupListener(ulong uIndex)
{
    /* Check if we should release mouse first: */
    if ((int)uIndex == m_iMouseCaptureViewIndex)
        releaseMouse();

    /* If that window still registered: */
    if (m_windows.contains(uIndex))
    {
        /* Unregister machine-window: */
        m_windows.remove(uIndex);
    }

    /* If that view still registered: */
    if (m_views.contains(uIndex))
    {
        /* Unregister machine-view: */
        m_views.remove(uIndex);
    }

    /* If that viewport still registered: */
    if (m_viewports.contains(uIndex))
    {
        /* Unregister machine-view-viewport: */
        m_viewports.remove(uIndex);
    }
}

void UIMouseHandler::captureMouse(ulong uScreenId)
{
    /* Do not try to capture mouse if its captured already: */
    if (uimachine()->isMouseCaptured())
        return;

    /* If such viewport exists: */
    if (m_viewports.contains(uScreenId))
    {
        /* Store mouse-capturing state value: */
        uimachine()->setMouseCaptured(true);

        /* Memorize the index of machine-view-viewport captured mouse: */
        m_iMouseCaptureViewIndex = uScreenId;

        /* Memorize the host position where the cursor was captured: */
        m_capturedMousePos = QCursor::pos();
        /* Determine geometry of screen cursor was captured at: */
        m_capturedScreenGeo = gpDesktop->screenGeometry(m_capturedMousePos);

        /* Acquiring visible viewport rectangle in global coodrinates: */
        QRect visibleRectangle = m_viewports[m_iMouseCaptureViewIndex]->visibleRegion().boundingRect();
        QPoint visibleRectanglePos = m_views[m_iMouseCaptureViewIndex]->mapToGlobal(m_viewports[m_iMouseCaptureViewIndex]->pos());
        visibleRectangle.translate(visibleRectanglePos);
        visibleRectangle = visibleRectangle.intersected(gpDesktop->availableGeometry(machineLogic()->machineWindows()[m_iMouseCaptureViewIndex]));

#ifdef VBOX_WS_WIN
        /* Move the mouse to the center of the visible area: */
        m_lastMousePos = visibleRectangle.center();
        QCursor::setPos(m_lastMousePos);
        /* Update mouse clipping: */
        updateMouseCursorClipping();
#elif defined (VBOX_WS_MAC)
        /* Grab all mouse events: */
        ::darwinMouseGrab(m_viewports[m_iMouseCaptureViewIndex]);
#else /* VBOX_WS_MAC */
        /* Remember current mouse position: */
        m_lastMousePos = QCursor::pos();
        /* Grab all mouse events: */
        m_viewports[m_iMouseCaptureViewIndex]->grabMouse();
#endif /* !VBOX_WS_MAC */

        /* Switch guest mouse to the relative mode: */
        uimachine()->putMouseEvent(0, 0, 0, 0, 0);

        /* Notify all the listeners: */
        emit sigStateChange(state());
    }
}

void UIMouseHandler::releaseMouse()
{
    /* Do not try to release mouse if its released already: */
    if (!uimachine()->isMouseCaptured())
        return;

    /* If such viewport exists: */
    if (m_viewports.contains(m_iMouseCaptureViewIndex))
    {
        /* Store mouse-capturing state value: */
        uimachine()->setMouseCaptured(false);

        /* Return the cursor to where it was when we captured it: */
        QCursor::setPos(m_capturedMousePos);
#ifdef VBOX_WS_WIN
        /* Update mouse clipping: */
        updateMouseCursorClipping();
#elif defined(VBOX_WS_MAC)
        /* Releasing grabbed mouse from that view: */
        ::darwinMouseRelease(m_viewports[m_iMouseCaptureViewIndex]);
#else /* VBOX_WS_MAC */
        /* Releasing grabbed mouse from that view: */
        m_viewports[m_iMouseCaptureViewIndex]->releaseMouse();
#endif /* !VBOX_WS_MAC */
        /* Reset mouse-capture index: */
        m_iMouseCaptureViewIndex = -1;

        /* Notify all the listeners: */
        emit sigStateChange(state());
    }
}

/* Setter for mouse-integration feature: */
void UIMouseHandler::setMouseIntegrationEnabled(bool fEnabled)
{
    /* Do not do anything if its already done: */
    if (uimachine()->isMouseIntegrated() == fEnabled)
        return;

    /* Store mouse-integration state value: */
    uimachine()->setMouseIntegrated(fEnabled);

    /* Reuse sltMouseCapabilityChanged() to update mouse state: */
    sltMouseCapabilityChanged();
}

/* Current mouse state: */
int UIMouseHandler::state() const
{
    int iResult = 0;
    if (uimachine()->isMouseCaptured())
        iResult |= UIMouseStateType_MouseCaptured;
    if (uimachine()->isMouseSupportsAbsolute())
    {
        iResult |= UIMouseStateType_MouseSupportsAbsolute;
        if (uimachine()->isMouseIntegrated())
            iResult |= UIMouseStateType_MouseIntegrated;
    }
    return iResult;
}

bool UIMouseHandler::nativeEventFilter(void *pMessage, ulong uScreenId)
{
    /* Make sure view with passed index exists: */
    if (!m_views.contains(uScreenId))
        return false;

    /* Check if some system event should be filtered out.
     * Returning @c true means filtering-out,
     * Returning @c false means passing event to Qt. */
    bool fResult = false; /* Pass to Qt by default. */

# if defined(VBOX_WS_MAC)

    /* Acquire carbon event reference from the cocoa one: */
    EventRef event = static_cast<EventRef>(darwinCocoaToCarbonEvent(pMessage));

    /* Depending on event kind: */
    const UInt32 uEventKind = ::GetEventKind(event);
    switch (uEventKind)
    {
        /* Watch for button-events: */
        case kEventMouseDown:
        case kEventMouseUp:
        {
            /* Acquire button number: */
            EventMouseButton enmButton = 0;
            ::GetEventParameter(event, kEventParamMouseButton, typeMouseButton,
                                NULL, sizeof(enmButton), NULL, &enmButton);
            /* If the event comes for primary mouse button: */
            if (enmButton == kEventMouseButtonPrimary)
            {
                /* Acquire modifiers: */
                UInt32 uKeyModifiers = ~0U;
                ::GetEventParameter(event, kEventParamKeyModifiers, typeUInt32,
                                    NULL, sizeof(uKeyModifiers), NULL, &uKeyModifiers);
                /* If the event comes with Control modifier: */
                if (uKeyModifiers == controlKey)
                {
                    /* Replacing it with the stripped one: */
                    darwinPostStrippedMouseEvent(pMessage);
                    /* And filter out initial one: */
                    return true;
                }
            }
        }
    }

# elif defined(VBOX_WS_WIN)

    /* Nothing for now. */
    RT_NOREF(pMessage, uScreenId);

# elif defined(VBOX_WS_NIX)

    if (uiCommon().X11ServerAvailable())
    {
        /* Cast to XCB event: */
        xcb_generic_event_t *pEvent = static_cast<xcb_generic_event_t*>(pMessage);

        /* Depending on event type: */
        switch (pEvent->response_type & ~0x80)
        {
            /* Watch for button-events: */
            case XCB_BUTTON_PRESS:
            {
                /* Do nothing if mouse is actively grabbed: */
                if (uimachine()->isMouseCaptured())
                    break;

                /* If we see a mouse press from a grab while the mouse is not captured,
                 * release the keyboard before letting the event owner see it. This is
                 * because some owners cannot deal with failures to grab the keyboard
                 * themselves (e.g. window managers dragging windows). */

                /* Cast to XCB button-event: */
                xcb_button_press_event_t *pButtonEvent = static_cast<xcb_button_press_event_t*>(pMessage);

                /* If this event is from our button grab then it will be reported relative to the root
                 * window and not to ours. In that case release the keyboard capture, re-capture it
                 * delayed, which will fail if we have lost the input focus in the mean-time, replay
                 * the button event for normal delivery (possibly straight back to us, but not relative
                 * to root this time) and tell Qt not to further process this event: */
                if (pButtonEvent->event == pButtonEvent->root)
                {
                    machineLogic()->keyboardHandler()->releaseKeyboard();
                    /** @todo It would be nicer to do this in the normal Qt button event
                     *       handler to avoid avoidable races if the event was not for us. */
                    machineLogic()->keyboardHandler()->captureKeyboard(uScreenId);
                    /* Re-send the event so that the window which it was meant for gets it: */
                    xcb_allow_events_checked(NativeWindowSubsystem::X11GetConnection(), XCB_ALLOW_REPLAY_POINTER, pButtonEvent->time);
                    /* Do not let Qt see the event: */
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }
# else

#  warning "port me!"

# endif

    /* Return result: */
    return fResult;
}

/* Machine state-change handler: */
void UIMouseHandler::sltMachineStateChanged()
{
    /* Get cached machine states: */
    const KMachineState enmState = uimachine()->machineState();

    /* Handle particular machine states: */
    switch (enmState)
    {
        case KMachineState_Paused:
        case KMachineState_TeleportingPausedVM:
        case KMachineState_Stuck:
        {
            /* Release the mouse: */
            releaseMouse();
            break;
        }
        default:
            break;
    }

    /* Recall reminder about paused VM input
     * if we are not in paused VM state already: */
    if (machineLogic()->activeMachineWindow() &&
        enmState != KMachineState_Paused &&
        enmState != KMachineState_TeleportingPausedVM)
        UINotificationMessage::forgetAboutPausedVMInput();

    /* Notify all the listeners: */
    emit sigStateChange(state());
}

/* Mouse capability-change handler: */
void UIMouseHandler::sltMouseCapabilityChanged()
{
    /* If mouse supports absolute pointing and mouse-integration activated: */
    if (uimachine()->isMouseSupportsAbsolute() && uimachine()->isMouseIntegrated())
    {
        /* Release the mouse: */
        releaseMouse();
        /* Also we should switch guest mouse to the absolute mode: */
        uimachine()->putMouseEventAbsolute(-1, -1, 0, 0, 0);
    }
#if 0 /* current team's decision is NOT to capture mouse on mouse-absolute mode loosing! */
    /* If mouse-integration deactivated or mouse doesn't supports absolute pointing: */
    else
    {
        /* Search for the machine-view focused now: */
        int iFocusedView = -1;
        QList<ulong> screenIds = m_views.keys();
        for (int i = 0; i < screenIds.size(); ++i)
        {
            if (m_views[screenIds[i]]->hasFocus())
            {
                iFocusedView = screenIds[i];
                break;
            }
        }
        /* If there is no focused view but views are present we will use the first one: */
        if (iFocusedView == -1 && !screenIds.isEmpty())
            iFocusedView = screenIds[0];
        /* Capture mouse using that view: */
        if (iFocusedView != -1)
            captureMouse(iFocusedView);
    }
#else /* but just to switch the guest mouse into relative mode! */
    /* If mouse-integration deactivated or mouse doesn't supports absolute pointing: */
    else
    {
        /* Switch guest mouse to the relative mode: */
        uimachine()->putMouseEvent(0, 0, 0, 0, 0);
    }
#endif

    /* Notify user whether mouse supports absolute pointing
     * if that method was called by corresponding signal: */
    if (sender())
    {
        /* Do not annoy user while restoring VM: */
        if (uimachine()->machineState() != KMachineState_Restoring)
            UINotificationMessage::remindAboutMouseIntegration(uimachine()->isMouseSupportsAbsolute());
    }

    /* Notify all the listeners: */
    emit sigStateChange(state());
}

/* Mouse pointer-shape-change handler: */
void UIMouseHandler::sltMousePointerShapeChanged()
{
    /* First of all, we should check if the host pointer should be visible.
     * We should hide host pointer in case of:
     * 1. mouse is 'captured' or
     * 2. machine is NOT 'paused' and mouse is NOT 'captured' and 'integrated' and 'absolute' but host pointer is 'hidden' by the guest. */
    if (uimachine()->isMouseCaptured() ||
        (!uimachine()->isPaused() &&
         uimachine()->isMouseIntegrated() &&
         uimachine()->isMouseSupportsAbsolute() &&
         uimachine()->isHidingHostPointer()))
    {
        QList<ulong> screenIds = m_viewports.keys();
        for (int i = 0; i < screenIds.size(); ++i)
            m_viewports[screenIds[i]]->setCursor(Qt::BlankCursor);
    }

    else

    /* Otherwise we should show host pointer with guest shape assigned to it if:
     * machine is NOT 'paused', mouse is 'integrated' and 'absolute' and valid pointer shape is present. */
    if (!uimachine()->isPaused() &&
        uimachine()->isMouseIntegrated() &&
        uimachine()->isMouseSupportsAbsolute() &&
        uimachine()->isValidPointerShapePresent())
    {
        QList<ulong> screenIds = m_viewports.keys();
        for (int i = 0; i < screenIds.size(); ++i)
            m_viewports[screenIds[i]]->setCursor(m_views[screenIds[i]]->cursor());
    }

    else

    /* There could be other states covering such situations as:
     * 1. machine is 'paused' or
     * 2. mouse is NOT 'captured' and 'integrated' but NOT 'absolute' or
     * 3. mouse is NOT 'captured' and 'absolute' but NOT 'integrated'.
     * We have nothing to do with that except just unset the cursor. */
    {
        QList<ulong> screenIds = m_viewports.keys();
        for (int i = 0; i < screenIds.size(); ++i)
            m_viewports[screenIds[i]]->unsetCursor();
    }
}

void UIMouseHandler::sltMaybeActivateHoveredWindow()
{
    /* Are we still have hovered window to activate? */
    if (m_pHoveredWindow && !m_pHoveredWindow->isActiveWindow())
    {
        /* Activate it: */
        m_pHoveredWindow->activateWindow();
#ifdef VBOX_WS_NIX
        /* On X11 its not enough to just activate window if you
         * want to raise it also, so we will make it separately: */
        m_pHoveredWindow->raise();
#endif /* VBOX_WS_NIX */
    }
}

/* Mouse-handler constructor: */
UIMouseHandler::UIMouseHandler(UIMachineLogic *pMachineLogic)
    : QObject(pMachineLogic)
    , m_pMachineLogic(pMachineLogic)
    , m_iLastMouseWheelDelta(0)
    , m_iMouseCaptureViewIndex(-1)
#ifdef VBOX_WS_WIN
    , m_fCursorPositionReseted(false)
#endif
{
    /* Machine state-change updater: */
    connect(uimachine(), &UIMachine::sigMachineStateChange, this, &UIMouseHandler::sltMachineStateChanged);

    /* Mouse capability state-change updater: */
    connect(uimachine(), &UIMachine::sigMouseCapabilityChange, this, &UIMouseHandler::sltMouseCapabilityChanged);
    /* Mouse cursor position state-change updater: */
    connect(uimachine(), &UIMachine::sigCursorPositionChange, this, &UIMouseHandler::sltMousePointerShapeChanged);

    /* Mouse pointer shape state-change updater: */
    connect(this, &UIMouseHandler::sigStateChange, this, &UIMouseHandler::sltMousePointerShapeChanged);

    /* Initialize: */
    sltMachineStateChanged();
    sltMousePointerShapeChanged();
    sltMouseCapabilityChanged();
}

/* Mouse-handler destructor: */
UIMouseHandler::~UIMouseHandler()
{
}

UIMachine *UIMouseHandler::uimachine() const
{
    return machineLogic()->uimachine();
}

/* Event handler for registered machine-view(s): */
bool UIMouseHandler::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* If that object is of QWidget type: */
    if (QWidget *pWatchedWidget = qobject_cast<QWidget*>(pWatched))
    {
        /* Check if that widget is in windows list: */
        if (m_windows.values().contains(pWatchedWidget))
        {
#ifdef VBOX_WS_WIN
            /* Handle window events: */
            switch (pEvent->type())
            {
                case QEvent::Move:
                {
                    /* Update mouse clipping if window was moved
                     * by Operating System desktop manager: */
                    updateMouseCursorClipping();
                    break;
                }
                default:
                    break;
            }
#endif /* VBOX_WS_WIN */
        }

        else

        /* Check if that widget is of UIMachineView type: */
        if (UIMachineView *pWatchedMachineView = qobject_cast<UIMachineView*>(pWatchedWidget))
        {
            /* Check if that widget is in views list: */
            if (m_views.values().contains(pWatchedMachineView))
            {
                /* Handle view events: */
                switch (pEvent->type())
                {
                    case QEvent::FocusOut:
                    {
                        /* Release the mouse: */
                        releaseMouse();
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        else

        /* Check if that widget is in viewports list: */
        if (m_viewports.values().contains(pWatchedWidget))
        {
            /* Get current watched widget screen id: */
            ulong uScreenId = m_viewports.key(pWatchedWidget);
            /* Handle viewport events: */
            switch (pEvent->type())
            {
#ifdef VBOX_WS_MAC
                case UIGrabMouseEvent::GrabMouseEvent:
                {
                    UIGrabMouseEvent *pDeltaEvent = static_cast<UIGrabMouseEvent*>(pEvent);
                    QPoint p = QPoint(pDeltaEvent->xDelta() + m_lastMousePos.x(),
                                      pDeltaEvent->yDelta() + m_lastMousePos.y());
                    if (mouseEvent(pDeltaEvent->mouseEventType(), uScreenId,
                                   m_viewports[uScreenId]->mapFromGlobal(p), p,
                                   pDeltaEvent->buttons(),
                                   pDeltaEvent->wheelDelta(), pDeltaEvent->orientation()))
                        return true;
                    break;
                }
#endif /* VBOX_WS_MAC */
                case QEvent::MouseMove:
                {
#ifdef VBOX_WS_MAC
                    // WORKAROUND:
                    // Since we are handling mouse move/drag events in the same thread
                    // where we are painting guest content changes corresponding to those
                    // events, we can have input event queue overloaded with the move/drag
                    // events, so we should discard current one if there is subsequent already.
                    EventTypeSpec list[2];
                    list[0].eventClass = kEventClassMouse;
                    list[0].eventKind = kEventMouseMoved;
                    list[1].eventClass = kEventClassMouse;
                    list[1].eventKind = kEventMouseDragged;
                    if (AcquireFirstMatchingEventInQueue(GetCurrentEventQueue(), 2, list,
                                                         kEventQueueOptionsNone) != NULL)
                        return true;
#endif /* VBOX_WS_MAC */

                    /* This event should be also processed using next 'case': */
                }
                RT_FALL_THRU();
                case QEvent::MouseButtonRelease:
                {
                    /* Get mouse-event: */
                    QMouseEvent *pOldMouseEvent = static_cast<QMouseEvent*>(pEvent);
                    const QPoint gPos = pOldMouseEvent->globalPosition().toPoint();

                    /* Check which viewport(s) we *probably* hover: */
                    QWidgetList probablyHoveredViewports;
                    foreach (QWidget *pViewport, m_viewports)
                    {
                        QPoint posInViewport = pViewport->mapFromGlobal(gPos);
                        if (pViewport->geometry().adjusted(0, 0, 1, 1).contains(posInViewport))
                            probablyHoveredViewports << pViewport;
                    }
                    /* Determine actually hovered viewport: */
                    QWidget *pHoveredWidget = probablyHoveredViewports.isEmpty() ? 0 :
                                              probablyHoveredViewports.contains(pWatchedWidget) ? pWatchedWidget :
                                              probablyHoveredViewports.first();

                    /* Check if we should propagate this event to another window: */
                    if (pHoveredWidget && pHoveredWidget != pWatchedWidget && m_viewports.values().contains(pHoveredWidget))
                    {
                        /* Prepare redirected mouse-move event: */
                        QMouseEvent *pNewMouseEvent = new QMouseEvent(pOldMouseEvent->type(),
                                                                      pHoveredWidget->mapFromGlobal(gPos),
                                                                      gPos,
                                                                      pOldMouseEvent->button(),
                                                                      pOldMouseEvent->buttons(),
                                                                      pOldMouseEvent->modifiers());

                        /* Send that event to real destination: */
                        QApplication::postEvent(pHoveredWidget, pNewMouseEvent);

                        /* Filter out that event: */
                        return true;
                    }

#ifdef VBOX_WS_NIX
                    /* Make sure that we are focused after a click.  Rather
                     * ugly, but works around a problem with GNOME
                     * screensaver, which sometimes removes our input focus
                     * and gives us no way to get it back. */
                    if (pEvent->type() == QEvent::MouseButtonRelease)
                        pWatchedWidget->window()->activateWindow();
#endif /* VBOX_WS_NIX */
                    /* Check if we should activate window under cursor: */
                    if (gEDataManager->activateHoveredMachineWindow() &&
                        !uimachine()->isMouseCaptured() &&
                        QApplication::activeWindow() &&
                        m_windows.values().contains(QApplication::activeWindow()) &&
                        m_windows.values().contains(pWatchedWidget->window()) &&
                        QApplication::activeWindow() != pWatchedWidget->window())
                    {
                        /* Put request for hovered window activation in 300msec: */
                        m_pHoveredWindow = pWatchedWidget->window();
                        QTimer::singleShot(300, this, SLOT(sltMaybeActivateHoveredWindow()));
                    }
                    else
                    {
                        /* Revoke request for hovered window activation: */
                        m_pHoveredWindow = 0;
                    }

                    /* This event should be also processed using next 'case': */
                }
                RT_FALL_THRU();
                case QEvent::MouseButtonPress:
                case QEvent::MouseButtonDblClick:
                {
                    QMouseEvent *pMouseEvent = static_cast<QMouseEvent*>(pEvent);
#ifdef VBOX_WS_NIX
                    /* When the keyboard is captured, we also capture mouse button
                     * events, and release the keyboard and re-capture it delayed
                     * on every mouse click. When the click is inside our window
                     * area though the delay is not needed or wanted. Calling
                     * finaliseCaptureKeyboard() skips the delay if a delayed
                     * capture is in progress and has no effect if not: */
                    if (pEvent->type() == QEvent::MouseButtonPress)
                        machineLogic()->keyboardHandler()->finaliseCaptureKeyboard();
#endif /* VBOX_WS_NIX */

                    /* For various mouse click related events
                     * we also reset last mouse wheel delta: */
                    if (pEvent->type() != QEvent::MouseMove)
                        m_iLastMouseWheelDelta = 0;

                    if (mouseEvent(pMouseEvent->type(), uScreenId,
                                   pMouseEvent->position().toPoint(), pMouseEvent->globalPosition().toPoint(),
                                   pMouseEvent->buttons(), 0, Qt::Horizontal))
                        return true;
                    break;
                }
                case QEvent::TouchBegin:
                case QEvent::TouchUpdate:
                case QEvent::TouchEnd:
                {
                    if (uimachine()->isMouseSupportsTouchScreen() || uimachine()->isMouseSupportsTouchPad())
                        return multiTouchEvent(static_cast<QTouchEvent*>(pEvent), uScreenId);
                    break;
                }
                case QEvent::Wheel:
                {
                    QWheelEvent *pWheelEvent = static_cast<QWheelEvent*>(pEvent);
                    /* There are pointing devices which send smaller values for the delta than 120.
                     * Here we sum them up until we are greater than 120. This allows to have finer control
                     * over the speed acceleration & enables such devices to send a valid wheel event to our
                     * guest mouse device at all: */
                    int iDelta = 0;
                    const Qt::Orientation enmOrientation = qFabs(pWheelEvent->angleDelta().x())
                                                         > qFabs(pWheelEvent->angleDelta().y())
                                                         ? Qt::Horizontal
                                                         : Qt::Vertical;
                    m_iLastMouseWheelDelta += enmOrientation == Qt::Horizontal
                                            ? pWheelEvent->angleDelta().x()
                                            : pWheelEvent->angleDelta().y();
                    if (qAbs(m_iLastMouseWheelDelta) >= 120)
                    {
                        /* Rounding iDelta to the nearest multiple of 120: */
                        iDelta = m_iLastMouseWheelDelta / 120;
                        iDelta *= 120;
                        m_iLastMouseWheelDelta = m_iLastMouseWheelDelta % 120;
                    }
                    if (mouseEvent(pWheelEvent->type(),
                                   uScreenId,
                                   pWheelEvent->position().toPoint(),
                                   pWheelEvent->globalPosition().toPoint(),
#ifdef VBOX_WS_MAC
                                   // WORKAROUND:
                                   // Qt Cocoa is buggy. It always reports a left button pressed when the
                                   // mouse wheel event occurs. A workaround is to ask the application which
                                   // buttons are pressed currently:
                                   QApplication::mouseButtons(),
#else /* !VBOX_WS_MAC */
                                   pWheelEvent->buttons(),
#endif /* !VBOX_WS_MAC */
                                   iDelta,
                                   enmOrientation)
                                   )
                        return true;
                    break;
                }
#ifdef VBOX_WS_MAC
                case QEvent::Leave:
                {
                    /* Enable mouse event compression if we leave the VM view.
                     * This is necessary for having smooth resizing of the VM/other windows: */
                    ::darwinSetMouseCoalescingEnabled(true);
                    break;
                }
                case QEvent::Enter:
                {
                    /* Disable mouse event compression if we enter the VM view.
                     * So all mouse events are registered in the VM.
                     * Only do this if the keyboard/mouse is grabbed
                     * (this is when we have a valid event handler): */
                    if (machineLogic()->keyboardHandler()->isKeyboardGrabbed())
                        darwinSetMouseCoalescingEnabled(false);
                    break;
                }
#endif /* VBOX_WS_MAC */
#ifdef VBOX_WS_WIN
                case QEvent::Resize:
                {
                    /* Update mouse clipping: */
                    updateMouseCursorClipping();
                    break;
                }
#endif /* VBOX_WS_WIN */
                default:
                    break;
            }
        }
    }
    return QObject::eventFilter(pWatched, pEvent);
}

/* Try to detect if the mouse event is fake and actually generated by a touch device. */
#ifdef VBOX_WS_WIN
#if (WINVER < 0x0601)
typedef enum tagINPUT_MESSAGE_DEVICE_TYPE {
  IMDT_UNAVAILABLE  = 0, // 0x0
  IMDT_KEYBOARD     = 1, // 0x1
  IMDT_MOUSE        = 2, // 0x2
  IMDT_TOUCH        = 4, // 0x4
  IMDT_PEN          = 8 // 0x8
} INPUT_MESSAGE_DEVICE_TYPE;

typedef enum tagINPUT_MESSAGE_ORIGIN_ID {
  IMO_UNAVAILABLE  = 0x00000000,
  IMO_HARDWARE     = 0x00000001,
  IMO_INJECTED     = 0x00000002,
  IMO_SYSTEM       = 0x00000004
} INPUT_MESSAGE_ORIGIN_ID;

typedef struct tagINPUT_MESSAGE_SOURCE {
  INPUT_MESSAGE_DEVICE_TYPE deviceType;
  INPUT_MESSAGE_ORIGIN_ID   originId;
} INPUT_MESSAGE_SOURCE;
#endif /* WINVER < 0x0601 */

#define MOUSEEVENTF_FROMTOUCH 0xFF515700
#define MOUSEEVENTF_MASK      0xFFFFFF00

typedef BOOL WINAPI FNGetCurrentInputMessageSource(INPUT_MESSAGE_SOURCE *inputMessageSource);
typedef FNGetCurrentInputMessageSource *PFNGetCurrentInputMessageSource;

static bool mouseIsTouchSource(int iEventType, Qt::MouseButtons mouseButtons)
{
    NOREF(mouseButtons);

    static PFNGetCurrentInputMessageSource pfnGetCurrentInputMessageSource = (PFNGetCurrentInputMessageSource)-1;
    if (pfnGetCurrentInputMessageSource == (PFNGetCurrentInputMessageSource)-1)
    {
        HMODULE hUser = GetModuleHandle(L"user32.dll");
        if (hUser)
            pfnGetCurrentInputMessageSource =
                (PFNGetCurrentInputMessageSource)GetProcAddress(hUser, "GetCurrentInputMessageSource");
    }

    int deviceType = -1;
    if (pfnGetCurrentInputMessageSource)
    {
        INPUT_MESSAGE_SOURCE inputMessageSource;
        BOOL fSuccess = pfnGetCurrentInputMessageSource(&inputMessageSource);
        deviceType = fSuccess? inputMessageSource.deviceType: -2;
    }
    else
    {
        if (   iEventType == QEvent::MouseButtonPress
            || iEventType == QEvent::MouseButtonRelease
            || iEventType == QEvent::MouseMove)
            deviceType = (GetMessageExtraInfo() & MOUSEEVENTF_MASK) == MOUSEEVENTF_FROMTOUCH? IMDT_TOUCH: -3;
    }

    LogRelFlow(("mouseIsTouchSource: deviceType %d\n", deviceType));
    return deviceType == IMDT_TOUCH || deviceType == IMDT_PEN;
}
#else
/* Apparently VBOX_WS_MAC does not generate fake mouse events.
 * Other platforms, which have no known method to detect fake events are handled here too.
 */
static bool mouseIsTouchSource(int iEventType, Qt::MouseButtons mouseButtons)
{
    NOREF(iEventType);
    NOREF(mouseButtons);
    return false;
}
#endif

/* Separate function to handle most of existing mouse-events: */
bool UIMouseHandler::mouseEvent(int iEventType, ulong uScreenId,
                                const QPoint &relativePos, const QPoint &globalPos,
                                Qt::MouseButtons mouseButtons,
                                int wheelDelta, Qt::Orientation wheelDirection)
{
    /* Ignore fake mouse events. */
    if (   (uimachine()->isMouseSupportsTouchScreen() || uimachine()->isMouseSupportsTouchPad())
        && mouseIsTouchSource(iEventType, mouseButtons))
        return true;

    /* Check if machine is still running: */
    if (!uimachine()->isRunning())
        return true;

    /* Check if such view & viewport are registered: */
    if (!m_views.contains(uScreenId) || !m_viewports.contains(uScreenId))
        return true;

    int iMouseButtonsState = 0;
    if (mouseButtons & Qt::LeftButton)
        iMouseButtonsState |= KMouseButtonState_LeftButton;
    if (mouseButtons & Qt::RightButton)
        iMouseButtonsState |= KMouseButtonState_RightButton;
    if (mouseButtons & Qt::MiddleButton)
        iMouseButtonsState |= KMouseButtonState_MiddleButton;
    if (mouseButtons & Qt::XButton1)
        iMouseButtonsState |= KMouseButtonState_XButton1;
    if (mouseButtons & Qt::XButton2)
        iMouseButtonsState |= KMouseButtonState_XButton2;

#ifdef VBOX_WS_MAC
    /* Simulate the right click on host-key + left-mouse-button: */
    if (machineLogic()->keyboardHandler()->isHostKeyPressed() &&
        machineLogic()->keyboardHandler()->isHostKeyAlone() &&
        iMouseButtonsState == KMouseButtonState_LeftButton)
        iMouseButtonsState = KMouseButtonState_RightButton;
#endif /* VBOX_WS_MAC */

    int iWheelVertical = 0;
    int iWheelHorizontal = 0;
    if (wheelDirection == Qt::Vertical)
    {
        /* The absolute value of wheel delta is 120 units per every wheel move;
         * positive deltas correspond to counterclockwise rotations (usually up),
         * negative deltas correspond to clockwise (usually down). */
        iWheelVertical = - (wheelDelta / 120);
    }
    else if (wheelDirection == Qt::Horizontal)
        iWheelHorizontal = wheelDelta / 120;

    if (uimachine()->isMouseCaptured())
    {
#ifdef VBOX_WS_WIN
        /* Send pending WM_PAINT events: */
        ::UpdateWindow((HWND)m_viewports[uScreenId]->winId());
#endif

#ifdef VBOX_WS_WIN
        // WORKAROUND:
        // There are situations at least on Windows host that we are receiving
        // previously posted (but not yet handled) mouse event right after we
        // have manually teleported mouse cursor to simulate infinite movement,
        // this makes cursor blink for a large amount of space, so we should
        // ignore such blinks .. well, at least once.
        const QPoint shiftingSpace = globalPos - m_lastMousePos;
        if (m_fCursorPositionReseted && shiftingSpace.manhattanLength() >= 10)
        {
            m_fCursorPositionReseted = false;
            return true;
        }
#endif

        /* Pass event to the guest: */
        uimachine()->putMouseEvent(globalPos.x() - m_lastMousePos.x(),
                                   globalPos.y() - m_lastMousePos.y(),
                                   iWheelVertical, iWheelHorizontal, iMouseButtonsState);

#ifdef VBOX_WS_WIN
        /* Compose viewport-rectangle in local coordinates: */
        QRect viewportRectangle = m_mouseCursorClippingRect;
        QPoint viewportRectangleGlobalPos = m_views[uScreenId]->mapToGlobal(m_viewports[uScreenId]->pos());
        viewportRectangle.translate(-viewportRectangleGlobalPos);

        /* Compose boundaries: */
        const int iX1 = viewportRectangle.left() + 1;
        const int iY1 = viewportRectangle.top() + 1;
        const int iX2 = viewportRectangle.right() - 1;
        const int iY2 = viewportRectangle.bottom() - 1;

        /* Simulate infinite movement: */
        QPoint p = relativePos;
        if (relativePos.x() <= iX1)
            p.setX(iX2 - 1);
        else if (relativePos.x() >= iX2)
            p.setX(iX1 + 1);
        if (relativePos.y() <= iY1)
            p.setY(iY2 - 1);
        else if (relativePos.y() >= iY2)
            p.setY(iY1 + 1);
        if (p != relativePos)
        {
            // WORKAROUND:
            // Underlying QCursor::setPos call requires coordinates, rescaled according to primary screen.
            // For that we have to map logical coordinates to relative origin (to make logical=>physical conversion).
            // Besides that we have to make sure m_lastMousePos still uses logical coordinates afterwards.
            const double dDprPrimary = UIDesktopWidgetWatchdog::devicePixelRatio(UIDesktopWidgetWatchdog::primaryScreenNumber());
            const double dDprCurrent = UIDesktopWidgetWatchdog::devicePixelRatio(m_windows.value(m_iMouseCaptureViewIndex));
            const QRect screenGeometry = gpDesktop->screenGeometry(m_windows.value(m_iMouseCaptureViewIndex));
            QPoint requiredMousePos = (m_viewports[uScreenId]->mapToGlobal(p) - screenGeometry.topLeft()) * dDprCurrent + screenGeometry.topLeft();
            QCursor::setPos(requiredMousePos / dDprPrimary);
            m_lastMousePos = requiredMousePos / dDprCurrent;
            m_fCursorPositionReseted = true;
        }
        else
        {
            m_lastMousePos = globalPos;
            m_fCursorPositionReseted = false;
        }
#else /* !VBOX_WS_WIN */
        /* Compose boundaries: */
        const int iX1 = m_capturedScreenGeo.left() + 1;
        const int iY1 = m_capturedScreenGeo.top() + 1;
        const int iX2 = m_capturedScreenGeo.right() - 1;
        const int iY2 = m_capturedScreenGeo.bottom() - 1;

        /* Simulate infinite movement: */
        QPoint p = globalPos;
        if (globalPos.x() <= iX1)
            p.setX(iX2 - 1);
        else if (globalPos.x() >= iX2)
            p.setX(iX1 + 1);
        if (globalPos.y() <= iY1)
            p.setY(iY2 - 1);
        else if (globalPos.y() >= iY2)
            p.setY(iY1 + 1);

        if (p != globalPos)
        {
            m_lastMousePos =  p;
            /* No need for cursor updating on the Mac, there is no one. */
# ifndef VBOX_WS_MAC
            QCursor::setPos(m_lastMousePos);
# endif /* VBOX_WS_MAC */
        }
        else
            m_lastMousePos = globalPos;
#endif /* !VBOX_WS_WIN */
        return true; /* stop further event handling */
    }
    else /* !uimachine()->isMouseCaptured() */
    {
        if (uimachine()->isMouseSupportsAbsolute() && uimachine()->isMouseIntegrated())
        {
            int iCw = m_views[uScreenId]->contentsWidth(), iCh = m_views[uScreenId]->contentsHeight();
            int iVw = m_views[uScreenId]->visibleWidth(), iVh = m_views[uScreenId]->visibleHeight();

            /* Try to automatically scroll the guest canvas if the
             * mouse goes outside its visible part: */
            int iDx = 0;
            if (relativePos.x() > iVw) iDx = relativePos.x() - iVw;
            else if (relativePos.x() < 0) iDx = relativePos.x();
            int iDy = 0;
            if (relativePos.y() > iVh) iDy = relativePos.y() - iVh;
            else if (relativePos.y() < 0) iDy = relativePos.y();
            if (iDx != 0 || iDy != 0) m_views[uScreenId]->scrollBy(iDx, iDy);

            /* Get mouse-pointer location: */
            QPoint cpnt = m_views[uScreenId]->viewportToContents(relativePos);

            /* Take the scale-factor(s) into account: */
            const UIFrameBuffer *pFrameBuffer = m_views[uScreenId]->frameBuffer();
            if (pFrameBuffer)
            {
                const QSize scaledSize = pFrameBuffer->scaledSize();
                if (scaledSize.isValid())
                {
                    const double xScaleFactor = (double)scaledSize.width()  / pFrameBuffer->width();
                    const double yScaleFactor = (double)scaledSize.height() / pFrameBuffer->height();
                    cpnt.setX((int)(cpnt.x() / xScaleFactor));
                    cpnt.setY((int)(cpnt.y() / yScaleFactor));
                }
            }

            /* Take the device-pixel-ratio into account: */
            const double dDevicePixelRatio = pFrameBuffer->devicePixelRatio();
            if (pFrameBuffer->useUnscaledHiDPIOutput())
            {
                cpnt.setX(cpnt.x() * dDevicePixelRatio);
                cpnt.setY(cpnt.y() * dDevicePixelRatio);
            }

#ifdef VBOX_WITH_DRAG_AND_DROP
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
            QPointer<UIMachineView> pView = m_views[uScreenId];
            bool fHandleDnDPending = RT_BOOL(mouseButtons.testFlag(Qt::LeftButton));

            /* Mouse pointer outside VM window? */
            if (   cpnt.x() < 0
                || cpnt.x() > iCw - 1
                || cpnt.y() < 0
                || cpnt.y() > iCh - 1)
            {
                if (fHandleDnDPending)
                {
                    LogRel2(("DnD: Drag and drop operation from guest to host started\n"));

                    int rc = pView->dragCheckPending();
                    if (RT_SUCCESS(rc))
                    {
                        pView->dragStart();
                        return true; /* Bail out -- we're done here. */
                    }
                }
            }
            else /* Inside VM window? */
            {
                if (fHandleDnDPending)
                    pView->dragStop();
            }
# endif
#endif /* VBOX_WITH_DRAG_AND_DROP */

            /* Bound coordinates: */
            if (cpnt.x() < 0) cpnt.setX(0);
            else if (cpnt.x() > iCw - 1) cpnt.setX(iCw - 1);
            if (cpnt.y() < 0) cpnt.setY(0);
            else if (cpnt.y() > iCh - 1) cpnt.setY(iCh - 1);

            /* Determine shifting: */
            ulong uDummy;
            long xShift = 0, yShift = 0;
            KGuestMonitorStatus enmDummy = KGuestMonitorStatus_Disabled;
            uimachine()->acquireGuestScreenParameters(uScreenId, uDummy, uDummy, uDummy, xShift, yShift, enmDummy);
            /* Set shifting: */
            cpnt.setX(cpnt.x() + xShift);
            cpnt.setY(cpnt.y() + yShift);

            /* Post absolute mouse-event into guest: */
            uimachine()->putMouseEventAbsolute(cpnt.x() + 1, cpnt.y() + 1, iWheelVertical, iWheelHorizontal, iMouseButtonsState);
            return true;
        }
        else
        {
            if (m_views[uScreenId]->hasFocus() && (iEventType == QEvent::MouseButtonRelease && mouseButtons == Qt::NoButton))
            {
                if (uimachine()->isPaused())
                    UINotificationMessage::remindAboutPausedVMInput();
                else if (uimachine()->isRunning())
                {
                    /* Temporarily disable auto capture that will take place after this dialog is dismissed because
                     * the capture state is to be defined by the dialog result itself: */
                    uimachine()->setAutoCaptureDisabled(true);
                    bool fIsAutoConfirmed = false;
                    bool ok = msgCenter().confirmInputCapture(fIsAutoConfirmed);
                    if (fIsAutoConfirmed)
                        uimachine()->setAutoCaptureDisabled(false);
                    /* Otherwise, the disable flag will be reset in the next console view's focus in event (since
                     * may happen asynchronously on some platforms, after we return from this code): */
                    if (ok)
                    {
#ifdef VBOX_WS_NIX
                        /* Make sure that pending FocusOut events from the previous message box are handled,
                         * otherwise the mouse is immediately ungrabbed again: */
                        qApp->processEvents();
#endif /* VBOX_WS_NIX */
                        machineLogic()->keyboardHandler()->captureKeyboard(uScreenId);
                        const MouseCapturePolicy mcp = gEDataManager->mouseCapturePolicy(uiCommon().managedVMUuid());
                        if (mcp == MouseCapturePolicy_Default)
                            captureMouse(uScreenId);
                    }
                }
            }
        }
    }

    return false;
}

bool UIMouseHandler::multiTouchEvent(QTouchEvent *pTouchEvent, ulong uScreenId)
{
    /* Eat if machine isn't running: */
    if (!uimachine()->isRunning())
        return true;

    /* Eat if such view & viewport aren't registered: */
    if (!m_views.contains(uScreenId) || !m_viewports.contains(uScreenId))
        return true;

    QVector<LONG64> contacts(pTouchEvent->points().size());

    long xShift = 0, yShift = 0;

    bool fTouchScreen = (pTouchEvent->device()->type() == QInputDevice::DeviceType::TouchScreen);
    /* Compatibility with previous behavior. If there is no touchpad configured
     * then treat all multitouch events as touchscreen ones: */
    fTouchScreen |= !uimachine()->isMouseSupportsTouchPad();

    if (fTouchScreen)
    {
        ulong uDummy;
        KGuestMonitorStatus enmDummy = KGuestMonitorStatus_Disabled;
        uimachine()->acquireGuestScreenParameters(uScreenId, uDummy, uDummy, uDummy, xShift, yShift, enmDummy);
    }

    /* Pass all multi-touch events into guest: */
    int iTouchPointIndex = 0;
    foreach (const QEventPoint &touchPoint, pTouchEvent->points())
    {
        /* Get touch-point state: */
        LONG iTouchPointState = KTouchContactState_None;
        switch (touchPoint.state())
        {
            case QEventPoint::Pressed:
            case QEventPoint::Updated:
            case QEventPoint::Stationary:
                iTouchPointState = KTouchContactState_InContact;
                if (fTouchScreen)
                    iTouchPointState |= KTouchContactState_InRange;
                break;
            default:
                break;
        }

        if (fTouchScreen)
        {
            /* Get absolute touch-point origin: */
            QPoint currentTouchPoint = touchPoint.position().toPoint();

            /* Pass absolute touch-point data: */
            LogRelFlow(("UIMouseHandler::multiTouchEvent: TouchScreen, Origin: %dx%d, Id: %d, State: %d\n",
                        currentTouchPoint.x(), currentTouchPoint.y(), touchPoint.id(), iTouchPointState));

            contacts[iTouchPointIndex] = RT_MAKE_U64_FROM_U16((uint16_t)currentTouchPoint.x() + 1 + xShift,
                                                              (uint16_t)currentTouchPoint.y() + 1 + yShift,
                                                              RT_MAKE_U16(touchPoint.id(), iTouchPointState),
                                                              0);
        } else {
            /* Get relative touch-point normalized position: */
            QPointF rawTouchPoint = touchPoint.normalizedPosition();

            /* Pass relative touch-point data as Normalized Integer: */
            uint16_t xNorm = rawTouchPoint.x() * 0xffff;
            uint16_t yNorm = rawTouchPoint.y() * 0xffff;
            LogRelFlow(("UIMouseHandler::multiTouchEvent: TouchPad, Normalized Position: %ux%u, Id: %d, State: %d\n",
                        xNorm, yNorm, touchPoint.id(), iTouchPointState));

            contacts[iTouchPointIndex] = RT_MAKE_U64_FROM_U16(xNorm, yNorm,
                                                              RT_MAKE_U16(touchPoint.id(), iTouchPointState),
                                                              0);
        }

        LogRelFlow(("UIMouseHandler::multiTouchEvent: %RX64\n", contacts[iTouchPointIndex]));

        ++iTouchPointIndex;
    }

    uimachine()->putEventMultiTouch(contacts.size(),
                                    contacts,
                                    fTouchScreen,
                                    RTTimeMilliTS());

    /* Eat by default? */
    return true;
}

#ifdef VBOX_WS_WIN
/* This method is actually required only because under win-host
 * we do not really grab the mouse in case of capturing it: */
void UIMouseHandler::updateMouseCursorClipping()
{
    /* Check if such view && viewport are registered: */
    if (!m_views.contains(m_iMouseCaptureViewIndex) || !m_viewports.contains(m_iMouseCaptureViewIndex))
        return;

    if (uimachine()->isMouseCaptured())
    {
        /* Get full-viewport-rectangle in global coordinates: */
        QRect viewportRectangle = m_viewports[m_iMouseCaptureViewIndex]->visibleRegion().boundingRect();
        const QPoint viewportRectangleGlobalPos = m_views[m_iMouseCaptureViewIndex]->mapToGlobal(m_viewports[m_iMouseCaptureViewIndex]->pos());
        viewportRectangle.translate(viewportRectangleGlobalPos);

        /* Trim full-viewport-rectangle by available geometry: */
        viewportRectangle = viewportRectangle.intersected(gpDesktop->availableGeometry(machineLogic()->machineWindows()[m_iMouseCaptureViewIndex]));

        /* Trim partial-viewport-rectangle by top-most windows: */
        QRegion viewportRegion = QRegion(viewportRectangle) - NativeWindowSubsystem::areaCoveredByTopMostWindows();
        /* Check if partial-viewport-region consists of 1 rectangle: */
        if (viewportRegion.rectCount() > 1)
        {
            /* Choose the largest rectangle: */
            QRect largestRect;
            for (QRegion::const_iterator it = viewportRegion.begin(); it != viewportRegion.end(); ++it)
            {
                const QRect rect = *it;
                largestRect = largestRect.width() * largestRect.height() < rect.width() * rect.height() ? rect : largestRect;
            }
            /* Assign the partial-viewport-region to the largest rect: */
            viewportRegion = largestRect;
        }
        /* Assign the partial-viewport-rectangle to the partial-viewport-region: */
        viewportRectangle = viewportRegion.boundingRect();

        /* Assign the visible-viewport-rectangle to the partial-viewport-rectangle: */
        m_mouseCursorClippingRect = viewportRectangle;

        /* Prepare clipping area: */
        // WORKAROUND:
        // Underlying ClipCursor call requires physical coordinates, not logical upscaled Qt stuff.
        // But we will have to map to relative origin (to make logical=>physical conversion) first.
        const double dDpr = UIDesktopWidgetWatchdog::devicePixelRatio(m_windows.value(m_iMouseCaptureViewIndex));
        const QRect screenGeometry = gpDesktop->screenGeometry(m_windows.value(m_iMouseCaptureViewIndex));
        viewportRectangle.moveTo((viewportRectangle.topLeft() - screenGeometry.topLeft()) * dDpr + screenGeometry.topLeft());
        viewportRectangle.setSize(viewportRectangle.size() * dDpr);
        RECT rect = { viewportRectangle.left() + 1, viewportRectangle.top() + 1, viewportRectangle.right(), viewportRectangle.bottom() };
        ::ClipCursor(&rect);
    }
    else
    {
        ::ClipCursor(NULL);
    }
}
#endif /* VBOX_WS_WIN */
