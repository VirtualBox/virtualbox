/* $Id: UIMachineView.cpp 108917 2025-03-24 15:53:56Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineView class implementation.
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
#include <QAbstractNativeEventFilter>
#include <QAccessibleWidget>
#include <QApplication>
#include <QBitmap>
#include <QCoreApplication>
#include <QMainWindow>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QWindowStateChangeEvent>

/* GUI includes: */
#include "UICommon.h"
#include "UIActionPoolRuntime.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UIKeyboardHandler.h"
#include "UILoggingDefs.h"
#include "UIMachine.h"
#include "UIMessageCenter.h"
#include "UIMachineLogic.h"
#include "UIMachineWindow.h"
#include "UIMachineViewNormal.h"
#include "UIMachineViewFullscreen.h"
#include "UIMachineViewSeamless.h"
#include "UIMachineViewScale.h"
#include "UIMouseHandler.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"
#ifdef VBOX_WS_MAC
# include "UICocoaApplication.h"
# include "DarwinKeyboard.h"
# include "DockIconPreview.h"
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "UIDnDHandler.h"
#endif

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>

/* COM includes: */
#include "CGraphicsAdapter.h"
#include "CSession.h"
#include "CFramebuffer.h"
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "CDnDSource.h"
# include "CDnDTarget.h"
# include "CGuest.h"
# include "CGuestDnDSource.h"
# include "CGuestDnDTarget.h"
#endif

/* Other VBox includes: */
#include <VBox/VBoxOGL.h>
#include <VBoxVideo.h>

/* External includes: */
#include <math.h>
#ifdef VBOX_WITH_DRAG_AND_DROP
# include <new> /* For bad_alloc. */
#endif
#ifdef VBOX_WS_MAC
# include <Carbon/Carbon.h>
#endif
#ifdef VBOX_WS_NIX
#  include <xcb/xcb.h>
#endif

#ifdef DEBUG_andy
/* Macro for debugging drag and drop actions which usually would
 * go to Main's logging group. */
# define DNDDEBUG(x) LogFlowFunc(x)
#else
# define DNDDEBUG(x)
#endif


/** QAccessibleWidget extension used as an accessibility interface for Machine-view. */
class UIAccessibilityInterfaceForUIMachineView : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating Machine-view accessibility interface: */
        if (pObject && strClassname == QLatin1String("UIMachineView"))
            return new UIAccessibilityInterfaceForUIMachineView(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    UIAccessibilityInterfaceForUIMachineView(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::Canvas)
    {}

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), 0);

        /* Zero by default: */
        return 0;
    }

    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), 0);
        /* Make sure index is valid: */
        AssertReturn(iIndex >= 0 && iIndex < childCount(), 0);

        /* Null by default: */
        return 0;
    }

    /** Returns the index of passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), -1);
        /* Make sure child is valid: */
        AssertReturn(pChild, -1);

        /* -1 by default: */
        return -1;;
    }

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), QString());

        /* Gather suitable text: */
        Q_UNUSED(enmTextRole);
        QString strText = view()->toolTip();
        if (strText.isEmpty())
            strText = view()->whatsThis();
        return strText;
    }

private:

    /** Returns corresponding Machine-view. */
    UIMachineView *view() const { return qobject_cast<UIMachineView*>(widget()); }
};


/** QAbstractNativeEventFilter extension
  * allowing to pre-process native platform events. */
class UINativeEventFilter : public QAbstractNativeEventFilter
{
public:

    /** Constructs native event filter storing @a pParent to redirect events to. */
    UINativeEventFilter(UIMachineView *pParent)
        : m_pParent(pParent)
    {}

    /** Redirects all the native events to parent. */
    bool nativeEventFilter(const QByteArray &eventType, void *pMessage, qintptr*) RT_OVERRIDE RT_FINAL
    {
        return m_pParent->nativeEventPreprocessor(eventType, pMessage);
    }

private:

    /** Holds the passed parent reference. */
    UIMachineView *m_pParent;
};


/* static */
UIMachineView* UIMachineView::create(UIMachineWindow *pMachineWindow, ulong uScreenId, UIVisualStateType visualStateType)
{
    UIMachineView *pMachineView = 0;
    switch (visualStateType)
    {
        case UIVisualStateType_Normal:
            pMachineView = new UIMachineViewNormal(pMachineWindow, uScreenId);
            break;
        case UIVisualStateType_Fullscreen:
            pMachineView = new UIMachineViewFullscreen(pMachineWindow, uScreenId);
            break;
        case UIVisualStateType_Seamless:
            pMachineView = new UIMachineViewSeamless(pMachineWindow, uScreenId);
            break;
        case UIVisualStateType_Scale:
            pMachineView = new UIMachineViewScale(pMachineWindow, uScreenId);
            break;
        default:
            break;
    }

    /* Load machine-view settings: */
    pMachineView->loadMachineViewSettings();

    /* Prepare viewport: */
    pMachineView->prepareViewport();

    /* Prepare frame-buffer: */
    pMachineView->prepareFrameBuffer();

    /* Prepare common things: */
    pMachineView->prepareCommon();

#ifdef VBOX_WITH_DRAG_AND_DROP
    /* Prepare DnD: */
    /* rc ignored */ pMachineView->prepareDnd();
#endif

    /* Prepare event-filters: */
    pMachineView->prepareFilters();

    /* Prepare connections: */
    pMachineView->prepareConnections();

    /* Prepare console connections: */
    pMachineView->prepareConsoleConnections();

    /* Initialization: */
    pMachineView->sltMachineStateChanged();
    /** @todo Can we move the call to sltAdditionsStateChanged() from the
     *        subclass constructors here too?  It is called for Normal and Seamless,
     *        but not for Fullscreen and Scale.  However for Scale it is a no op.,
     *        so it would not hurt.  Would it hurt for fullscreen? */

    /* Set a preliminary maximum size: */
    pMachineView->setMaximumGuestSize();

    /* Resend the last resize hint finally: */
    pMachineView->resendSizeHint();

    /* Return the created view: */
    return pMachineView;
}

/* static */
void UIMachineView::destroy(UIMachineView *pMachineView)
{
    if (!pMachineView)
        return;

#ifdef VBOX_WITH_DRAG_AND_DROP
    /* Cleanup DnD: */
    pMachineView->cleanupDnd();
#endif

    /* Cleanup frame-buffer: */
    pMachineView->cleanupFrameBuffer();

    /* Cleanup native filters: */
    pMachineView->cleanupNativeFilters();

    delete pMachineView;
}

void UIMachineView::applyMachineViewScaleFactor()
{
    /* Sanity check: */
    if (!frameBuffer())
        return;

    /* Acquire selected scale-factor: */
    double dScaleFactor = gEDataManager->scaleFactor(uiCommon().managedVMUuid(), m_uScreenId);

    /* Take the device-pixel-ratio into account: */
    frameBuffer()->setDevicePixelRatio(UIDesktopWidgetWatchdog::devicePixelRatio(machineWindow()));
    frameBuffer()->setDevicePixelRatioActual(UIDesktopWidgetWatchdog::devicePixelRatioActual(machineWindow()));
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    const bool fUseUnscaledHiDPIOutput = dScaleFactor != dDevicePixelRatioActual;
    dScaleFactor = fUseUnscaledHiDPIOutput ? dScaleFactor : 1.0;

    /* Assign frame-buffer with new values: */
    frameBuffer()->setScaleFactor(dScaleFactor);
    frameBuffer()->setUseUnscaledHiDPIOutput(fUseUnscaledHiDPIOutput);

    /* Propagate the scale-factor related attributes to 3D service if necessary: */
    bool fAccelerate3DEnabled = false;
    uimachine()->acquireWhetherAccelerate3DEnabled(fAccelerate3DEnabled);
    if (fAccelerate3DEnabled)
    {
        double dScaleFactorFor3D = dScaleFactor;
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
        // WORKAROUND:
        // On Windows and Linux opposing to macOS it's only Qt which can auto scale up,
        // not 3D overlay itself, so for auto scale-up mode we have to take that into account.
        if (!fUseUnscaledHiDPIOutput)
            dScaleFactorFor3D *= dDevicePixelRatioActual;
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
        uimachine()->notifyScaleFactorChange(m_uScreenId,
                                             (uint32_t)(dScaleFactorFor3D * VBOX_OGL_SCALE_FACTOR_MULTIPLIER),
                                             (uint32_t)(dScaleFactorFor3D * VBOX_OGL_SCALE_FACTOR_MULTIPLIER));
        uimachine()->notifyHiDPIOutputPolicyChange(fUseUnscaledHiDPIOutput);
    }

    /* Perform frame-buffer rescaling: */
    frameBuffer()->performRescale();

    /* Update console's display viewport and 3D overlay: */
    updateViewport();
}

UIMachine *UIMachineView::uimachine() const
{
    return machineWindow()->uimachine();
}

UIMachineLogic *UIMachineView::machineLogic() const
{
    return machineWindow()->machineLogic();
}

UIFrameBuffer *UIMachineView::frameBuffer() const
{
    return uimachine()->frameBuffer(m_uScreenId);
}

int UIMachineView::contentsWidth() const
{
    return frameBuffer()->width();
}

int UIMachineView::contentsHeight() const
{
    return frameBuffer()->height();
}

int UIMachineView::contentsX() const
{
    return horizontalScrollBar()->value();
}

int UIMachineView::contentsY() const
{
    return verticalScrollBar()->value();
}

int UIMachineView::visibleWidth() const
{
    return horizontalScrollBar()->pageStep();
}

int UIMachineView::visibleHeight() const
{
    return verticalScrollBar()->pageStep();
}

QPoint UIMachineView::viewportToContents(const QPoint &viewportPoint) const
{
    /* Get physical contents shifts of scroll-bars: */
    int iContentsX = contentsX();
    int iContentsY = contentsY();

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    if (!frameBuffer()->useUnscaledHiDPIOutput())
    {
        iContentsX *= dDevicePixelRatioActual;
        iContentsY *= dDevicePixelRatioActual;
    }
    iContentsX /= dDevicePixelRatioFormal;
    iContentsY /= dDevicePixelRatioFormal;

    /* Return point shifted according scroll-bars: */
    return QPoint(viewportPoint.x() + iContentsX, viewportPoint.y() + iContentsY);
}

void UIMachineView::scrollBy(int iDx, int iDy)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + iDx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + iDy);
}

UIVisualStateType UIMachineView::visualStateType() const
{
    return machineLogic()->visualStateType();
}

double UIMachineView::aspectRatio() const
{
    return frameBuffer() ? (double)(frameBuffer()->width()) / frameBuffer()->height() : 0;
}

void UIMachineView::setMaximumGuestSize(const QSize &minimumSizeHint /* = QSize() */)
{
    QSize maxSize;
    switch (m_enmMaximumGuestScreenSizePolicy)
    {
        case MaximumGuestScreenSizePolicy_Fixed:
            maxSize = m_fixedMaxGuestSize;
            break;
        case MaximumGuestScreenSizePolicy_Automatic:
            maxSize = calculateMaxGuestSize().expandedTo(minimumSizeHint);
            break;
        case MaximumGuestScreenSizePolicy_Any:
            /* (0, 0) means any of course. */
            maxSize = QSize(0, 0);
    }
    ASMAtomicWriteU64(&m_u64MaximumGuestSize,
                      RT_MAKE_U64(maxSize.height(), maxSize.width()));
}

QSize UIMachineView::maximumGuestSize()
{
    uint64_t u64Size = ASMAtomicReadU64(&m_u64MaximumGuestSize);
    return QSize(int(RT_HI_U32(u64Size)), int(RT_LO_U32(u64Size)));
}

void UIMachineView::updateViewport()
{
    uimachine()->viewportChanged(screenId(),
                                 (ulong)contentsX(), (ulong)contentsY(),
                                 (ulong)visibleWidth(), (ulong)visibleHeight());
}

#ifdef VBOX_WITH_DRAG_AND_DROP
int UIMachineView::dragCheckPending()
{
    int rc;

    if (!dragAndDropIsActive())
        rc = VERR_ACCESS_DENIED;
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
    else if (!m_fIsDraggingFromGuest)
    {
        /// @todo Add guest->guest DnD functionality here by getting
        //       the source of guest B (when copying from B to A).
        rc = m_pDnDHandler->dragCheckPending(screenId());
        if (RT_SUCCESS(rc))
            m_fIsDraggingFromGuest = true;
    }
    else /* Already dragging, so report success. */
        rc = VINF_SUCCESS;
# else
    rc = VERR_NOT_SUPPORTED;
# endif

    DNDDEBUG(("DnD: dragCheckPending ended with rc=%Rrc\n", rc));
    return rc;
}

int UIMachineView::dragStart()
{
    int rc;

    if (!dragAndDropIsActive())
        rc = VERR_ACCESS_DENIED;
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
    else if (!m_fIsDraggingFromGuest)
        rc = VERR_WRONG_ORDER;
    else
    {
        /// @todo Add guest->guest DnD functionality here by getting
        //       the source of guest B (when copying from B to A).
        rc = m_pDnDHandler->dragStart(screenId());

        m_fIsDraggingFromGuest = false;
    }
# else
    rc = VERR_NOT_SUPPORTED;
# endif

    DNDDEBUG(("DnD: dragStart ended with rc=%Rrc\n", rc));
    return rc;
}

int UIMachineView::dragStop()
{
    int rc;

    if (!dragAndDropIsActive())
        rc = VERR_ACCESS_DENIED;
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
    else if (!m_fIsDraggingFromGuest)
        rc = VERR_WRONG_ORDER;
    else
        rc = m_pDnDHandler->dragStop(screenId());
# else
    rc = VERR_NOT_SUPPORTED;
# endif

    DNDDEBUG(("DnD: dragStop ended with rc=%Rrc\n", rc));
    return rc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP */

bool UIMachineView::nativeEventPreprocessor(const QByteArray &eventType, void *pMessage)
{
    /* Check if some event should be filtered out.
     * Returning @c true means filtering-out,
     * Returning @c false means passing event to Qt. */

# if defined(VBOX_WS_MAC)

    /* Make sure it's generic NSEvent: */
    if (eventType != "mac_generic_NSEvent")
        return false;
    EventRef event = static_cast<EventRef>(darwinCocoaToCarbonEvent(pMessage));

    switch (::GetEventClass(event))
    {
        // Keep in mind that this stuff should not be enabled while we are still using
        // own native keyboard filter installed through cocoa API, to be reworked.
        // S.a. registerForNativeEvents call in UIKeyboardHandler implementation.
#if 0
        /* Watch for keyboard-events: */
        case kEventClassKeyboard:
        {
            switch (::GetEventKind(event))
            {
                /* Watch for key-events: */
                case kEventRawKeyDown:
                case kEventRawKeyRepeat:
                case kEventRawKeyUp:
                case kEventRawKeyModifiersChanged:
                {
                    /* Delegate key-event handling to the keyboard-handler: */
                    return machineLogic()->keyboardHandler()->nativeEventFilter(pMessage, screenId());
                }
                default:
                    break;
            }
            break;
        }
#endif
        /* Watch for mouse-events: */
        case kEventClassMouse:
        {
            switch (::GetEventKind(event))
            {
                /* Watch for button-events: */
                case kEventMouseDown:
                case kEventMouseUp:
                {
                    /* Delegate button-event handling to the mouse-handler: */
                    return machineLogic()->mouseHandler()->nativeEventFilter(pMessage, screenId());
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

# elif defined(VBOX_WS_WIN)

    /* Make sure it's generic MSG event: */
    if (eventType != "windows_generic_MSG")
        return false;
    MSG *pEvent = static_cast<MSG*>(pMessage);

    switch (pEvent->message)
    {
        /* Watch for key-events: */
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            // WORKAROUND:
            // There is an issue in the Windows Qt5 event processing sequence
            // causing QAbstractNativeEventFilter to receive Windows native events
            // coming not just to the top-level window but to actual target as well.
            // They are calling one - "global event" and another one - "context event".
            // That way native events are always duplicated with almost no possibility
            // to distinguish copies except the fact that synthetic event always have
            // time set to 0 (actually that field was not initialized at all, we had
            // fixed that in our private Qt tool). We should skip such events instantly.
            if (pEvent->time == 0)
                return false;

            /* Delegate key-event handling to the keyboard-handler: */
            return machineLogic()->keyboardHandler()->nativeEventFilter(pMessage, screenId());
        }
        default:
            break;
    }

# elif defined(VBOX_WS_NIX)

    if (uiCommon().X11ServerAvailable())
    {
        /* Make sure it's generic XCB event: */
        if (eventType != "xcb_generic_event_t")
            return false;
        xcb_generic_event_t *pEvent = static_cast<xcb_generic_event_t*>(pMessage);

        switch (pEvent->response_type & ~0x80)
        {
            /* Watch for key-events: */
            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE:
                {
                    /* Delegate key-event handling to the keyboard-handler: */
                    return machineLogic()->keyboardHandler()->nativeEventFilter(pMessage, screenId());
                }
                /* Watch for button-events: */
            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE:
                {
                    /* Delegate button-event handling to the mouse-handler: */
                    return machineLogic()->mouseHandler()->nativeEventFilter(pMessage, screenId());
                }
            default:
                break;
        }
    }

# else

#  warning "port me!"

# endif

    /* Filter nothing by default: */
    return false;
}

#ifdef VBOX_WS_MAC
CGImageRef UIMachineView::vmContentImage()
{
    /* Use pause-image if exists: */
    if (!pausePixmap().isNull())
        return darwinToCGImageRef(&pausePixmap());

    /* Create the image ref out of the frame-buffer: */
    return frameBuffertoCGImageRef(frameBuffer());
}
#endif /* VBOX_WS_MAC */

void UIMachineView::sltHandleNotifyChange(int iWidth, int iHeight)
{
    /* Sanity check: */
    if (!frameBuffer())
        return;

    LogRel2(("GUI: UIMachineView::sltHandleNotifyChange: Screen=%d, Size=%dx%d\n",
             (unsigned long)m_uScreenId, iWidth, iHeight));

    /* Some situations require frame-buffer resize-events to be ignored at all,
     * leaving machine-window, machine-view and frame-buffer sizes preserved: */
    if (uimachine()->isGuestResizeIgnored())
        return;

    /* In some situations especially in some VM states, guest-screen is not drawable: */
    if (uimachine()->isGuestScreenUnDrawable())
        return;

    /* Get old frame-buffer size: */
    const QSize frameBufferSizeOld = QSize(frameBuffer()->width(),
                                           frameBuffer()->height());

    /* Perform frame-buffer mode-change: */
    frameBuffer()->handleNotifyChange(iWidth, iHeight);

    /* Get new frame-buffer size: */
    const QSize frameBufferSizeNew = QSize(frameBuffer()->width(),
                                           frameBuffer()->height());

    /* For 'scale' mode: */
    if (visualStateType() == UIVisualStateType_Scale)
    {
        /* Assign new frame-buffer logical-size: */
        QSize scaledSize = size();
        const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
        const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
        scaledSize *= dDevicePixelRatioFormal;
        if (!frameBuffer()->useUnscaledHiDPIOutput())
            scaledSize /= dDevicePixelRatioActual;
        frameBuffer()->setScaledSize(scaledSize);

        /* Forget the last full-screen size: */
        uimachine()->setLastFullScreenSize(screenId(), QSize(-1, -1));
    }
    /* For other than 'scale' mode: */
    else
    {
        /* Adjust maximum-size restriction for machine-view: */
        setMaximumSize(sizeHint());

        /* Disable the resize hint override hack and forget the last full-screen size: */
        m_sizeHintOverride = QSize(-1, -1);
        if (visualStateType() == UIVisualStateType_Normal)
            uimachine()->setLastFullScreenSize(screenId(), QSize(-1, -1));

        /* Force machine-window update own layout: */
        QCoreApplication::sendPostedEvents(0, QEvent::LayoutRequest);

        /* Update machine-view sliders: */
        updateSliders();

        /* By some reason Win host forgets to update machine-window central-widget
         * after main-layout was updated, let's do it for all the hosts: */
        machineWindow()->centralWidget()->update();

        /* Normalize 'normal' machine-window geometry if necessary: */
        if (visualStateType() == UIVisualStateType_Normal &&
            frameBufferSizeNew != frameBufferSizeOld)
            machineWindow()->normalizeGeometry(true /* adjust position */, machineWindow()->shouldResizeToGuestDisplay());
    }

    /* Perform frame-buffer rescaling: */
    frameBuffer()->performRescale();

#ifdef VBOX_WS_MAC
    /* Update MacOS X dock icon size: */
    machineLogic()->updateDockIconSize(screenId(), frameBufferSizeNew.width(), frameBufferSizeNew.height());
#endif /* VBOX_WS_MAC */

    /* Notify frame-buffer resize: */
    emit sigFrameBufferResize();

    /* Ask for just required guest display update (it will also update
     * the viewport through IFramebuffer::NotifyUpdate): */
    uimachine()->invalidateAndUpdateScreen(m_uScreenId);

    /* Acquire graphics controller type: */
    KGraphicsControllerType enmType = KGraphicsControllerType_Null;
    uimachine()->acquireGraphicsControllerType(enmType);

    /* If we are in normal or scaled mode and if GA are active,
     * remember the guest-screen size to be able to restore it when necessary: */
    /* As machines with Linux/Solaris and VMSVGA are not able to tell us
     * whether a resize was due to the system or user interaction we currently
     * do not store hints for these systems except when we explicitly send them
     * ourselves.  Windows guests should use VBoxVGA controllers, not VMSVGA. */
    if (   !isFullscreenOrSeamless()
        && uimachine()->isGuestSupportsGraphics()
        && (enmType != KGraphicsControllerType_VMSVGA))
        setStoredGuestScreenSizeHint(frameBufferSizeNew);

    LogRel2(("GUI: UIMachineView::sltHandleNotifyChange: Complete for Screen=%d, Size=%dx%d\n",
             (unsigned long)m_uScreenId, frameBufferSizeNew.width(), frameBufferSizeNew.height()));
}

void UIMachineView::sltHandleNotifyUpdate(int iX, int iY, int iWidth, int iHeight)
{
    /* Sanity check: */
    if (!frameBuffer())
        return;

    /* Prepare corresponding viewport part: */
    QRect rect(iX, iY, iWidth, iHeight);

    /* Take the scaling into account: */
    const double dScaleFactor = frameBuffer()->scaleFactor();
    const QSize scaledSize = frameBuffer()->scaledSize();
    if (scaledSize.isValid())
    {
        /* Calculate corresponding scale-factors: */
        const double xScaleFactor = visualStateType() == UIVisualStateType_Scale ?
                                    (double)scaledSize.width()  / frameBuffer()->width()  : dScaleFactor;
        const double yScaleFactor = visualStateType() == UIVisualStateType_Scale ?
                                    (double)scaledSize.height() / frameBuffer()->height() : dScaleFactor;
        /* Adjust corresponding viewport part: */
        rect.moveTo((int)floor((double)rect.x() * xScaleFactor) - 1,
                    (int)floor((double)rect.y() * yScaleFactor) - 1);
        rect.setSize(QSize((int)ceil((double)rect.width()  * xScaleFactor) + 2,
                           (int)ceil((double)rect.height() * yScaleFactor) + 2));
    }

    /* Shift has to be scaled by the device-pixel-ratio
     * but not scaled by the scale-factor. */
    rect.translate(-contentsX(), -contentsY());

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    if (!frameBuffer()->useUnscaledHiDPIOutput() && dDevicePixelRatioActual != 1.0)
    {
        rect.moveTo((int)floor((double)rect.x() * dDevicePixelRatioActual) - 1,
                    (int)floor((double)rect.y() * dDevicePixelRatioActual) - 1);
        rect.setSize(QSize((int)ceil((double)rect.width()  * dDevicePixelRatioActual) + 2,
                           (int)ceil((double)rect.height() * dDevicePixelRatioActual) + 2));
    }
    if (dDevicePixelRatioFormal != 1.0)
    {
        rect.moveTo((int)floor((double)rect.x() / dDevicePixelRatioFormal) - 1,
                    (int)floor((double)rect.y() / dDevicePixelRatioFormal) - 1);
        rect.setSize(QSize((int)ceil((double)rect.width()  / dDevicePixelRatioFormal) + 2,
                           (int)ceil((double)rect.height() / dDevicePixelRatioFormal) + 2));
    }

    /* Limit the resulting part by the viewport rectangle: */
    rect &= viewport()->rect();

    /* Update corresponding viewport part: */
    viewport()->update(rect);
}

void UIMachineView::sltHandleSetVisibleRegion(QRegion region)
{
    /* Used only in seamless-mode. */
    Q_UNUSED(region);
}

void UIMachineView::sltPerformGuestResize(const QSize &toSize)
{
    /* There is a couple of things to keep in mind:
     *
     * First of all, passed size can be invalid (or even not sane one, where one of values equal to zero).  Usually that happens
     * if this function being invoked with default argument for example by some slot.  In such case we get the available size for
     * the guest-screen we have.  We assume here that centralWidget() contains this view only and gives it all available space.
     * In all other cases we have a valid non-zero size which should be handled as usual.
     *
     * Besides that, passed size or size taken from centralWidget() is _not_ absolute one, it's in widget's coordinate system
     * which can and will be be transformed by scale-factor when appropriate, so before passing this size to a guest it has to
     * be scaled backward.  This is the key aspect in which internal resize differs from resize initiated from the outside. */

    /* Make sure we have valid size to work with: */
    QSize size(  toSize.isValid() && toSize.width() > 0 && toSize.height() > 0
               ? toSize : machineWindow()->centralWidget()->size());
    AssertMsgReturnVoid(size.isValid() && size.width() > 0 && size.height() > 0,
                        ("Size should be valid (%dx%d)!\n", size.width(), size.height()));

    /* Take the scale-factor(s) into account: */
    size = scaledBackward(size);

    /* Update current window size limitations: */
    setMaximumGuestSize(size);

    /* Record the hint to extra data, needed for guests using VMSVGA:
     * This should be done before the actual hint is sent in case the guest overrides it.
     * Do not send a hint if nothing has changed to prevent the guest being notified about its own changes. */
    if (   !isFullscreenOrSeamless()
        && uimachine()->isGuestSupportsGraphics()
        && (   (int)frameBuffer()->width() != size.width()
            || (int)frameBuffer()->height() != size.height()
            || uimachine()->isScreenVisible(screenId()) != uimachine()->isScreenVisibleHostDesires(screenId())))
        setStoredGuestScreenSizeHint(size);

    /* If auto-mount of guest-screens (auto-pilot) enabled: */
    if (gEDataManager->autoMountGuestScreensEnabled(uiCommon().managedVMUuid()))
    {
        /* If host and guest have same opinion about guest-screen visibility: */
        if (uimachine()->isScreenVisible(screenId()) == uimachine()->isScreenVisibleHostDesires(screenId()))
        {
            /* Do not send a hint if nothing has changed to prevent the guest being notified about its own changes: */
            if ((int)frameBuffer()->width() != size.width() || (int)frameBuffer()->height() != size.height())
            {
                LogRel(("GUI: UIMachineView::sltPerformGuestResize: Auto-pilot resizing screen %d as %dx%d\n",
                        (int)screenId(), size.width(), size.height()));
                uimachine()->setVideoModeHint(screenId(),
                                              uimachine()->isScreenVisible(screenId()),
                                              false /* change origin? */,
                                              0 /* origin x */, 0 /* origin y */,
                                              (ulong)size.width(), (ulong)size.height(),
                                              0 /* bits per pixel */,
                                              true /* notify? */);
            }
        }
        else
        {
            /* If host desires to have guest-screen enabled and guest-screen is disabled, retrying: */
            if (uimachine()->isScreenVisibleHostDesires(screenId()))
            {
                /* Send enabling size-hint to the guest: */
                LogRel(("GUI: UIMachineView::sltPerformGuestResize: Auto-pilot enabling guest-screen %d\n", (int)screenId()));
                uimachine()->setVideoModeHint(screenId(),
                                              true /* enabled? */,
                                              false /* change origin? */,
                                              0 /* origin x */, 0 /* origin y */,
                                              (ulong)size.width(), (ulong)size.height(),
                                              0 /* bits per pixel */,
                                              true /* notify? */);
            }
            /* If host desires to have guest-screen disabled and guest-screen is enabled, retrying: */
            else
            {
                /* Send disabling size-hint to the guest: */
                LogRel(("GUI: UIMachineView::sltPerformGuestResize: Auto-pilot disabling guest-screen %d\n", (int)screenId()));
                uimachine()->setVideoModeHint(screenId(),
                                              false /* enabled? */,
                                              false /* change origin? */,
                                              0 /* origin x */, 0 /* origin y */,
                                              0 /* width */, 0 /* height */,
                                              0 /* bits per pixel */,
                                              true /* notify? */);
            }
        }
    }
    /* If auto-mount of guest-screens (auto-pilot) disabled: */
    else
    {
        /* Should we send a hint? */
        bool fSendHint = true;
        /* Do not send a hint if nothing has changed to prevent the guest being notified about its own changes: */
        if (fSendHint && (int)frameBuffer()->width() == size.width() && (int)frameBuffer()->height() == size.height())
        {
            LogRel(("GUI: UIMachineView::sltPerformGuestResize: Omitting to send size-hint %dx%d to guest-screen %d "
                    "because frame-buffer is already of the same size.\n", size.width(), size.height(), (int)screenId()));
            fSendHint = false;
        }
        /* Do not send a hint if GA supports graphics and we have sent that hint already: */
        if (fSendHint && uimachine()->isGuestSupportsGraphics() && m_lastSizeHint == size)
        {
            LogRel(("GUI: UIMachineView::sltPerformGuestResize: Omitting to send size-hint %dx%d to guest-screen %d "
                    "because this hint was previously sent.\n", size.width(), size.height(), (int)screenId()));
            fSendHint = false;
        }
        if (fSendHint)
        {
            LogRel(("GUI: UIMachineView::sltPerformGuestResize: Sending guest size-hint to screen %d as %dx%d\n",
                    (int)screenId(), size.width(), size.height()));
            uimachine()->setVideoModeHint(screenId(),
                                          uimachine()->isScreenVisible(screenId()),
                                          false /* change origin? */,
                                          0 /* origin x */, 0 /* origin y */,
                                          (ulong)size.width(), (ulong)size.height(),
                                          0 /* bits per pixel */,
                                          true /* notify? */);
            m_lastSizeHint = size;
        }
    }
}

void UIMachineView::sltHandleActionTriggerViewScreenToggle(int iScreen, bool fEnabled)
{
    /* Skip unrelated guest-screen index: */
    if (iScreen != (int)screenId())
        return;

    /* Acquire current resolution: */
    ulong uWidth = 0, uHeight = 0, uDummy = 0;
    long iDummy = 0;
    KGuestMonitorStatus enmDummy = KGuestMonitorStatus_Disabled;
    const bool fSuccess = uimachine()->acquireGuestScreenParameters(screenId(), uWidth, uHeight,
                                                                    uDummy, iDummy, iDummy, enmDummy);
    if (!fSuccess)
        return;

    /* Update desirable screen status: */
    uimachine()->setScreenVisibleHostDesires(screenId(), fEnabled);

    /* Send enabling size-hint: */
    if (fEnabled)
    {
        /* Defaults: */
        if (!uWidth)
            uWidth = 800;
        if (!uHeight)
            uHeight = 600;

        /* Update current window size limitations: */
        setMaximumGuestSize(QSize(uWidth, uHeight));

        /* Record the hint to extra data, needed for guests using VMSVGA:
         * This should be done before the actual hint is sent in case the guest overrides it.
         * Do not send a hint if nothing has changed to prevent the guest being notified about its own changes. */
        if (   !isFullscreenOrSeamless()
            && uimachine()->isGuestSupportsGraphics()
            && (   frameBuffer()->width() != uWidth
                || frameBuffer()->height() != uHeight
                || uimachine()->isScreenVisible(screenId()) != uimachine()->isScreenVisibleHostDesires(screenId())))
            setStoredGuestScreenSizeHint(QSize(uWidth, uHeight));

        /* Send enabling size-hint to the guest: */
        LogRel(("GUI: UIMachineView::sltHandleActionTriggerViewScreenToggle: Enabling guest-screen %d\n", (int)screenId()));
        uimachine()->setVideoModeHint(screenId(),
                                      true /* enabled? */,
                                      false /* change origin? */,
                                      0 /* origin x */, 0 /* origin y */,
                                      uWidth, uHeight,
                                      0 /* bits per pixel */,
                                      true /* notify? */);
    }
    else
    {
        /* Send disabling size-hint to the guest: */
        LogRel(("GUI: UIMachineView::sltHandleActionTriggerViewScreenToggle: Disabling guest-screen %d\n", (int)screenId()));
        uimachine()->setVideoModeHint(screenId(),
                                      false /* enabled? */,
                                      false /* change origin? */,
                                      0 /* origin x */, 0 /* origin y */,
                                      0 /* width */, 0 /* height */,
                                      0 /* bits per pixel */,
                                      true /* notify? */);
    }
}

void UIMachineView::sltHandleActionTriggerViewScreenResize(int iScreen, const QSize &size)
{
    /* Skip unrelated guest-screen index: */
    if (iScreen != (int)m_uScreenId)
        return;

    /* Make sure we have valid size to work with: */
    AssertMsgReturnVoid(size.isValid() && size.width() > 0 && size.height() > 0,
                        ("Size should be valid (%dx%d)!\n", size.width(), size.height()));

    /* Update current window size limitations: */
    setMaximumGuestSize(size);

    /* Record the hint to extra data, needed for guests using VMSVGA:
     * This should be done before the actual hint is sent in case the guest overrides it.
     * Do not send a hint if nothing has changed to prevent the guest being notified about its own changes. */
    if (   !isFullscreenOrSeamless()
        && uimachine()->isGuestSupportsGraphics()
        && (   (int)frameBuffer()->width() != size.width()
            || (int)frameBuffer()->height() != size.height()
            || uimachine()->isScreenVisible(screenId()) != uimachine()->isScreenVisibleHostDesires(screenId())))
        setStoredGuestScreenSizeHint(size);

    /* Send enabling size-hint to the guest: */
    LogRel(("GUI: UIMachineView::sltHandleActionTriggerViewScreenResize: Resizing guest-screen %d\n", (int)screenId()));
    uimachine()->setVideoModeHint(screenId(),
                                  true /* enabled? */,
                                  false /* change origin? */,
                                  0 /* origin x */, 0 /* origin y */,
                                  size.width(), size.height(),
                                  0 /* bits per pixel */,
                                  true /* notify? */);
}

void UIMachineView::sltDesktopResized()
{
    setMaximumGuestSize();
}

void UIMachineView::sltHandleScaleFactorChange(const QUuid &uMachineID)
{
    /* Skip unrelated machine IDs: */
    if (uMachineID != uiCommon().managedVMUuid())
        return;

    /* Acquire selected scale-factor: */
    double dScaleFactor = gEDataManager->scaleFactor(uiCommon().managedVMUuid(), m_uScreenId);

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    const bool fUseUnscaledHiDPIOutput = dScaleFactor != dDevicePixelRatioActual;
    dScaleFactor = fUseUnscaledHiDPIOutput ? dScaleFactor : 1.0;

    /* Assign frame-buffer with new values: */
    frameBuffer()->setScaleFactor(dScaleFactor);
    frameBuffer()->setUseUnscaledHiDPIOutput(fUseUnscaledHiDPIOutput);

    /* Propagate the scale-factor related attributes to 3D service if necessary: */
    bool fAccelerate3DEnabled = false;
    uimachine()->acquireWhetherAccelerate3DEnabled(fAccelerate3DEnabled);
    if (fAccelerate3DEnabled)
    {
        double dScaleFactorFor3D = dScaleFactor;
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
        // WORKAROUND:
        // On Windows and Linux opposing to macOS it's only Qt which can auto scale up,
        // not 3D overlay itself, so for auto scale-up mode we have to take that into account.
        if (!fUseUnscaledHiDPIOutput)
            dScaleFactorFor3D *= frameBuffer()->devicePixelRatioActual();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
        uimachine()->notifyScaleFactorChange(m_uScreenId,
                                             (uint32_t)(dScaleFactorFor3D * VBOX_OGL_SCALE_FACTOR_MULTIPLIER),
                                             (uint32_t)(dScaleFactorFor3D * VBOX_OGL_SCALE_FACTOR_MULTIPLIER));
        uimachine()->notifyHiDPIOutputPolicyChange(fUseUnscaledHiDPIOutput);
    }

    /* Handle scale attributes change: */
    handleScaleChange();
    /* Adjust guest-screen size: */
    adjustGuestScreenSize();

    /* Update scaled pause pixmap, if necessary: */
    updateScaledPausePixmap();
    viewport()->update();

    /* Update console's display viewport and 3D overlay: */
    updateViewport();
}

void UIMachineView::sltHandleScalingOptimizationChange(const QUuid &uMachineID)
{
    /* Skip unrelated machine IDs: */
    if (uMachineID != uiCommon().managedVMUuid())
        return;

    /* Take the scaling-optimization type into account: */
    frameBuffer()->setScalingOptimizationType(gEDataManager->scalingOptimizationType(uiCommon().managedVMUuid()));

    /* Update viewport: */
    viewport()->update();
}

void UIMachineView::sltMachineStateChanged()
{
    /* Get machine state: */
    KMachineState state = uimachine()->machineState();
    switch (state)
    {
        case KMachineState_Paused:
        case KMachineState_TeleportingPausedVM:
        {
            if (   frameBuffer()
                && (   state           != KMachineState_TeleportingPausedVM
                    || m_previousState != KMachineState_Teleporting))
            {
                // WORKAROUND:
                // We can't take pause pixmap if actual state is Saving, this produces
                // a lock and GUI will be frozen until SaveState call is complete...
                KMachineState enmActualState = KMachineState_Null;
                uimachine()->acquireLiveMachineState(enmActualState);
                if (enmActualState != KMachineState_Saving)
                {
                    /* Take live pause-pixmap: */
                    takePausePixmapLive();
                    /* Fully repaint to pick up pause-pixmap: */
                    viewport()->update();
                }
            }
            break;
        }
        case KMachineState_Restoring:
        {
            /* Only works with the primary screen currently. */
            if (screenId() == 0)
            {
                /* Take snapshot pause-pixmap: */
                takePausePixmapSnapshot();
                /* Fully repaint to pick up pause-pixmap: */
                viewport()->update();
            }
            break;
        }
        case KMachineState_Running:
        {
            if (m_previousState == KMachineState_Paused ||
                m_previousState == KMachineState_TeleportingPausedVM ||
                m_previousState == KMachineState_Restoring)
            {
                if (frameBuffer())
                {
                    /* Reset pause-pixmap: */
                    resetPausePixmap();
                    /* Ask for full guest display update (it will also update
                     * the viewport through IFramebuffer::NotifyUpdate): */
                    uimachine()->invalidateAndUpdate();
                }
            }
            /* Reapply machine-view scale-factor: */
            applyMachineViewScaleFactor();
            break;
        }
        default:
            break;
    }

    m_previousState = state;
}

void UIMachineView::sltMousePointerShapeChange()
{
    /* Fetch the shape and the mask: */
    QPixmap pixmapShape = uimachine()->cursorShapePixmap();
    QPixmap pixmapMask = uimachine()->cursorMaskPixmap();
    const QPoint hotspot = uimachine()->cursorHotspot();
    uint uXHot = hotspot.x();
    uint uYHot = hotspot.y();

    /* If there is no mask: */
    if (pixmapMask.isNull())
    {
        /* Scale the shape pixmap and
         * compose the cursor on the basis of shape only: */
        updateMousePointerPixmapScaling(pixmapShape, uXHot, uYHot);
        m_cursor = QCursor(pixmapShape, uXHot, uYHot);
    }
    /* Otherwise: */
    else
    {
        /* Scale the shape and the mask pixmaps and
         * compose the cursor on the basis of shape and mask both: */
        updateMousePointerPixmapScaling(pixmapShape, uXHot, uYHot);
        /// @todo updateMousePointerPixmapScaling(pixmapMask, uXHot, uYHot);
        m_cursor = QCursor(QBitmap::fromPixmap(pixmapShape), QBitmap::fromPixmap(pixmapMask), uXHot, uYHot);
    }

    /* Let the listeners know: */
    emit sigMousePointerShapeChange();
}

void UIMachineView::sltDetachCOM()
{
#ifdef VBOX_WITH_DRAG_AND_DROP
    /* Cleanup DnD: */
    cleanupDnd();
#endif
}

void UIMachineView::sltRetranslateUI()
{
    setWhatsThis(tr("Holds the graphical canvas containing guest screen contents."));
}

UIMachineView::UIMachineView(UIMachineWindow *pMachineWindow, ulong uScreenId)
    : QAbstractScrollArea(pMachineWindow->centralWidget())
    , m_pMachineWindow(pMachineWindow)
    , m_uScreenId(uScreenId)
    , m_previousState(KMachineState_Null)
    , m_iHostScreenNumber(0)
    , m_enmMaximumGuestScreenSizePolicy(MaximumGuestScreenSizePolicy_Automatic)
    , m_u64MaximumGuestSize(0)
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
    , m_fIsDraggingFromGuest(false)
#endif
    , m_pNativeEventFilter(0)
{
    /* Install Machine-view accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUIMachineView::pFactory);
}

void UIMachineView::loadMachineViewSettings()
{
    /* Global settings: */
    {
        /* Remember the maximum guest size policy for
         * telling the guest about video modes we like: */
        m_enmMaximumGuestScreenSizePolicy = gEDataManager->maxGuestResolutionPolicy();
        if (m_enmMaximumGuestScreenSizePolicy == MaximumGuestScreenSizePolicy_Fixed)
            m_fixedMaxGuestSize = gEDataManager->maxGuestResolutionForPolicyFixed();
    }
}

void UIMachineView::prepareViewport()
{
    /* Prepare viewport: */
    AssertPtrReturnVoid(viewport());
    {
        /* Enable manual painting: */
        viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
        /* Enable multi-touch support: */
        viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    }
}

void UIMachineView::prepareFrameBuffer()
{
    /* Make sure frame-buffer exists: */
    if (!frameBuffer())
        return;

    /* If frame-buffer NOT yet initialized: */
    if (!frameBuffer()->isInitialized())
    {
        LogRelFlow(("GUI: UIMachineView::prepareFrameBuffer: Start EMT callbacks accepting for screen: %d\n", screenId()));
        /* Initialize for this view: */
        frameBuffer()->init(this);
        /* Apply machine-view scale-factor: */
        applyMachineViewScaleFactor();
    }
    /* Otherwise it must be unused yet: */
    else
    {
        LogRelFlow(("GUI: UIMachineView::prepareFrameBuffer: Restart EMT callbacks accepting for screen: %d\n", screenId()));
        /* Assign it's view: */
        frameBuffer()->setView(this);
        /* Mark frame-buffer as used again: */
        frameBuffer()->setMarkAsUnused(false);
    }

    /* Make sure frame-buffer was prepared: */
    AssertPtrReturnVoid(frameBuffer());

    /* Reattach to IDisplay: */
    frameBuffer()->detach();
    frameBuffer()->attach();

    /* Calculate frame-buffer size: */
    QSize size;
    {
        /* Acquire actual machine state to be sure: */
        KMachineState enmActualState = KMachineState_Null;
        uimachine()->acquireLiveMachineState(enmActualState);

#ifdef VBOX_WS_NIX
        // WORKAROUND:
        // No idea why this was required for X11 before.
        // Currently I don't see a reason why I should keep it.
# if 0
        if (enmActualState == KMachineState_Saved || enmActualState == KMachineState_AbortedSaved)
            size = storedGuestScreenSizeHint();
# endif
#endif /* VBOX_WS_NIX */

        /* If there is a preview image saved, we will resize the framebuffer to the size of that image: */
        if (enmActualState == KMachineState_Saved || enmActualState == KMachineState_AbortedSaved)
        {
            ulong uWidth = 0, uHeight = 0;
            QVector<KBitmapFormat> formats;
            uimachine()->acquireSavedScreenshotInfo(m_uScreenId, uWidth, uHeight, formats);
            if (formats.size() > 0)
            {
                /* Init with the screenshot size: */
                size = QSize(uWidth, uHeight);
                /* Try to get the real guest dimensions from the save-state: */
                long iDummy = 0;
                ulong uGuestWidth = 0, uGuestHeight = 0;
                bool fDummy = true;
                uimachine()->acquireSavedGuestScreenInfo(m_uScreenId,
                                                         iDummy, iDummy,
                                                         uGuestWidth, uGuestHeight, fDummy);
                if (uGuestWidth  > 0 && uGuestHeight > 0)
                    size = QSize(uGuestWidth, uGuestHeight);
            }
        }

        /* If we have a valid size, resize/rescale the frame-buffer. */
        if (size.width() > 0 && size.height() > 0)
        {
            frameBuffer()->performResize(size.width(), size.height());
            frameBuffer()->performRescale();
        }
    }
}

void UIMachineView::prepareCommon()
{
    /* Prepare view frame: */
    setFrameStyle(QFrame::NoFrame);

    /* Setup palette: */
    QPalette palette(viewport()->palette());
    palette.setColor(viewport()->backgroundRole(), Qt::black);
    viewport()->setPalette(palette);

    /* Setup focus policy: */
    setFocusPolicy(Qt::WheelFocus);
}

#ifdef VBOX_WITH_DRAG_AND_DROP
int UIMachineView::prepareDnd(void)
{
    /* Enable drag & drop: */
    setAcceptDrops(true);

    int vrc;

    /* Create the drag and drop handler instance: */
    m_pDnDHandler = new UIDnDHandler(uimachine(), this /* pParent */);
    if (m_pDnDHandler)
    {
        vrc = m_pDnDHandler->init();
    }
    else
        vrc = VERR_NO_MEMORY;

    if (RT_FAILURE(vrc))
        LogRel(("DnD: Initialization failed with %Rrc\n", vrc));
    return vrc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP */

void UIMachineView::prepareFilters()
{
    /* Enable MouseMove events: */
    viewport()->setMouseTracking(true);

    /* We have to watch for own events too: */
    installEventFilter(this);

    /* QScrollView does the below on its own, but let's
     * do it anyway for the case it will not do it in the future: */
    viewport()->installEventFilter(this);

    /* We want to be notified on some parent's events: */
    machineWindow()->installEventFilter(this);
}

void UIMachineView::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToDetachCOM, this, &UIMachineView::sltDetachCOM);

    /* Desktop resolution change (e.g. monitor hotplug): */
    connect(gpDesktop, &UIDesktopWidgetWatchdog::sigHostScreenResized,
            this, &UIMachineView::sltDesktopResized);

    /* Scale-factor change: */
    connect(gEDataManager, &UIExtraDataManager::sigScaleFactorChange,
            this, &UIMachineView::sltHandleScaleFactorChange);
    /* Scaling-optimization change: */
    connect(gEDataManager, &UIExtraDataManager::sigScalingOptimizationTypeChange,
            this, &UIMachineView::sltHandleScalingOptimizationChange);

    /* Action-pool connections: */
    UIActionPoolRuntime *pActionPoolRuntime = qobject_cast<UIActionPoolRuntime*>(actionPool());
    if (pActionPoolRuntime)
    {
        connect(pActionPoolRuntime, &UIActionPoolRuntime::sigNotifyAboutTriggeringViewScreenToggle,
                this, &UIMachineView::sltHandleActionTriggerViewScreenToggle);
        connect(pActionPoolRuntime, &UIActionPoolRuntime::sigNotifyAboutTriggeringViewScreenResize,
                this, &UIMachineView::sltHandleActionTriggerViewScreenResize);
    }

    /* Translate initially: */
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMachineView::sltRetranslateUI);
    sltRetranslateUI();
}

void UIMachineView::prepareConsoleConnections()
{
    /* Machine state-change updater: */
    connect(uimachine(), &UIMachine::sigMachineStateChange, this, &UIMachineView::sltMachineStateChanged);
    /* Mouse pointer shape updater: */
    connect(uimachine(), &UIMachine::sigMousePointerShapeChange, this, &UIMachineView::sltMousePointerShapeChange);
}

#ifdef VBOX_WITH_DRAG_AND_DROP
void UIMachineView::cleanupDnd()
{
    delete m_pDnDHandler;
    m_pDnDHandler = 0;
}
#endif /* VBOX_WITH_DRAG_AND_DROP */

void UIMachineView::cleanupFrameBuffer()
{
    /* Make sure framebuffer still present: */
    if (!frameBuffer())
        return;

    /* Mark framebuffer as unused: */
    LogRelFlow(("GUI: UIMachineView::cleanupFrameBuffer: Stop EMT callbacks accepting for screen: %d\n", screenId()));
    frameBuffer()->setMarkAsUnused(true);

    /* Process pending framebuffer events: */
    QApplication::sendPostedEvents(this, QEvent::MetaCall);

    /* Temporarily detach the framebuffer from IDisplay before detaching
     * from view in order to respect the thread synchonisation logic (see UIFrameBuffer.h).
     * Note: VBOX_WITH_CROGL additionally requires us to call DetachFramebuffer
     * to ensure 3D gets notified of view being destroyed... */
    frameBuffer()->detach();

    /* Detach framebuffer from view: */
    frameBuffer()->setView(0);
}

void UIMachineView::cleanupNativeFilters()
{
    /* If native event filter exists: */
    if (m_pNativeEventFilter)
    {
        /* Uninstall/destroy existing native event filter: */
        qApp->removeNativeEventFilter(m_pNativeEventFilter);
        delete m_pNativeEventFilter;
        m_pNativeEventFilter = 0;
    }
}

UIActionPool* UIMachineView::actionPool() const
{
    return machineWindow()->actionPool();
}

QSize UIMachineView::sizeHint() const
{
    /* Make sure frame-buffer exists: */
    QSize size;
    if (!frameBuffer())
        size = QSize(640, 480);
    else
    {
        // WORKAROUND:
        // Temporarily restrict the size to prevent a brief resize to the frame-buffer dimensions when
        // we exit full-screen.  This is only applied if the frame-buffer is at full-screen dimensions
        // and until the first machine view resize.
        /* Get the frame-buffer dimensions: */
        QSize frameBufferSize(frameBuffer()->width(), frameBuffer()->height());
        /* Take the scale-factor(s) into account: */
        frameBufferSize = scaledForward(frameBufferSize);
        /* Check against the last full-screen size: */
        if (frameBufferSize == uimachine()->lastFullScreenSize(screenId()) && m_sizeHintOverride.isValid())
            return m_sizeHintOverride;

        /* Get frame-buffer size-hint: */
        size = QSize(frameBuffer()->width(), frameBuffer()->height());
        /* Take the scale-factor(s) into account: */
        size = scaledForward(size);

#ifdef VBOX_WITH_DEBUGGER_GUI
        /// @todo Fix all DEBUGGER stuff!
        // WORKAROUND:
        // Really ugly workaround for the resizing to 9x1
        // done by DevVGA if provoked before power on.
        if (size.width() < 16 || size.height() < 16)
            if (uiCommon().shouldStartPaused() || uiCommon().isDebuggerAutoShowEnabled())
                size = QSize(640, 480);
#endif /* VBOX_WITH_DEBUGGER_GUI */
    }

    /* Return the resulting size-hint: */
    return QSize(size.width() + frameWidth() * 2, size.height() + frameWidth() * 2);
}

QSize UIMachineView::storedGuestScreenSizeHint() const
{
    /* Load guest-screen size-hint: */
    QSize sizeHint = gEDataManager->lastGuestScreenSizeHint(m_uScreenId, uiCommon().managedVMUuid());

    /* Invent the default if necessary: */
    if (!sizeHint.isValid())
        sizeHint = QSize(800, 600);

    /* Take the scale-factor(s) into account: */
    sizeHint = scaledForward(sizeHint);

    /* Return size-hint: */
    LogRel2(("GUI: UIMachineView::storedGuestScreenSizeHint: Acquired guest-screen size-hint for screen %d as %dx%d\n",
             (int)screenId(), sizeHint.width(), sizeHint.height()));
    return sizeHint;
}

void UIMachineView::setStoredGuestScreenSizeHint(const QSize &sizeHint)
{
    /* Save guest-screen size-hint: */
    LogRel2(("GUI: UIMachineView::setStoredGuestScreenSizeHint: Storing guest-screen size-hint for screen %d as %dx%d\n",
             (int)screenId(), sizeHint.width(), sizeHint.height()));
    gEDataManager->setLastGuestScreenSizeHint(m_uScreenId, sizeHint, uiCommon().managedVMUuid());
}

QSize UIMachineView::requestedGuestScreenSizeHint() const
{
    /* Acquire last guest-screen size-hint set, if any: */
    bool fDummy = false;
    long iDummy = 0;
    ulong uWidth = 0, uHeight = 0, uDummy = 0;
    uimachine()->acquireVideoModeHint(screenId(), fDummy, fDummy,
                                      iDummy, iDummy, uWidth, uHeight,
                                      uDummy);

    /* Acquire effective frame-buffer size otherwise: */
    if (uWidth == 0 || uHeight == 0)
    {
        uWidth = frameBuffer()->width();
        uHeight = frameBuffer()->height();
    }

    /* Return result: */
    return QSize((int)uWidth, (int)uHeight);
}

bool UIMachineView::guestScreenVisibilityStatus() const
{
    /* Always 'true' for primary guest-screen: */
    if (m_uScreenId == 0)
        return true;

    /* Actual value for other guest-screens: */
    return gEDataManager->lastGuestScreenVisibilityStatus(m_uScreenId, uiCommon().managedVMUuid());
}

void UIMachineView::handleScaleChange()
{
    LogRel(("GUI: UIMachineView::handleScaleChange: Screen=%d\n",
            (unsigned long)m_uScreenId));

    /* If machine-window is visible: */
    if (uimachine()->isScreenVisible(m_uScreenId))
    {
        /* For 'scale' mode: */
        if (visualStateType() == UIVisualStateType_Scale)
        {
            /* Assign new frame-buffer logical-size: */
            QSize scaledSize = size();
            const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
            const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
            scaledSize *= dDevicePixelRatioFormal;
            if (!frameBuffer()->useUnscaledHiDPIOutput())
                scaledSize /= dDevicePixelRatioActual;
            frameBuffer()->setScaledSize(scaledSize);
        }
        /* For other than 'scale' mode: */
        else
        {
            /* Adjust maximum-size restriction for machine-view: */
            setMaximumSize(sizeHint());

            /* Force machine-window update own layout: */
            QCoreApplication::sendPostedEvents(0, QEvent::LayoutRequest);

            /* Update machine-view sliders: */
            updateSliders();

            /* By some reason Win host forgets to update machine-window central-widget
             * after main-layout was updated, let's do it for all the hosts: */
            machineWindow()->centralWidget()->update();

            /* Normalize 'normal' machine-window geometry: */
            if (visualStateType() == UIVisualStateType_Normal)
                machineWindow()->normalizeGeometry(true /* adjust position */, machineWindow()->shouldResizeToGuestDisplay());
        }

        /* Perform frame-buffer rescaling: */
        frameBuffer()->performRescale();
    }

    LogRelFlow(("GUI: UIMachineView::handleScaleChange: Complete for Screen=%d\n",
                (unsigned long)m_uScreenId));
}

void UIMachineView::resetPausePixmap()
{
    /* Reset pixmap(s): */
    m_pausePixmap = QPixmap();
    m_pausePixmapScaled = QPixmap();
}

void UIMachineView::takePausePixmapLive()
{
    /* Prepare a screen-shot: */
    QImage screenShot = QImage(frameBuffer()->width(), frameBuffer()->height(), QImage::Format_RGB32);
    /* Which will be a 'black image' by default. */
    screenShot.fill(0);

    /* Acquire screen-shot image: */
    uimachine()->acquireScreenShot(screenId(),
                                   (ulong)screenShot.width(), (ulong)screenShot.height(),
                                   KBitmapFormat_BGR0, screenShot.bits());

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    if (!frameBuffer()->useUnscaledHiDPIOutput() && dDevicePixelRatioActual != 1.0)
        screenShot = screenShot.scaled(screenShot.size() * dDevicePixelRatioActual,
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    /* Dim screen-shot if it is Ok: */
    if (!screenShot.isNull())
        dimImage(screenShot);

    /* Finally copy the screen-shot to pause-pixmap: */
    m_pausePixmap = QPixmap::fromImage(screenShot);

    /* Take the device-pixel-ratio into account: */
    m_pausePixmap.setDevicePixelRatio(frameBuffer()->devicePixelRatio());

    /* Update scaled pause pixmap: */
    updateScaledPausePixmap();
}

void UIMachineView::takePausePixmapSnapshot()
{
    /* Acquire the screen-data from the saved-state: */
    ulong uDummy = 0;
    QVector<BYTE> screenData;
    uimachine()->acquireSavedScreenshot(m_uScreenId, KBitmapFormat_PNG, uDummy, uDummy, screenData);
    if (screenData.isEmpty())
        return;

    /* Acquire the screen-data properties from the saved-state: */
    long iDummy = 0;
    ulong uGuestWidth = 0, uGuestHeight = 0;
    bool fDummy = true;
    uimachine()->acquireSavedGuestScreenInfo(m_uScreenId, iDummy, iDummy, uGuestWidth, uGuestHeight, fDummy);

    /* Calculate effective size: */
    QSize effectiveSize = uGuestWidth > 0 ? QSize(uGuestWidth, uGuestHeight) : storedGuestScreenSizeHint();

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    if (!frameBuffer()->useUnscaledHiDPIOutput() && dDevicePixelRatioActual != 1.0)
        effectiveSize *= dDevicePixelRatioActual;

    /* Create a screen-shot on the basis of the screen-data we have in saved-state: */
    QImage screenShot = QImage::fromData(screenData.data(), screenData.size(), "PNG").scaled(effectiveSize);

    /* Dim screen-shot if it is Ok: */
    if (!screenShot.isNull())
        dimImage(screenShot);

    /* Finally copy the screen-shot to pause-pixmap: */
    m_pausePixmap = QPixmap::fromImage(screenShot);

    /* Take the device-pixel-ratio into account: */
    m_pausePixmap.setDevicePixelRatio(frameBuffer()->devicePixelRatio());

    /* Update scaled pause pixmap: */
    updateScaledPausePixmap();
}

void UIMachineView::updateScaledPausePixmap()
{
    /* Make sure pause pixmap is not null: */
    if (pausePixmap().isNull())
        return;

    /* Make sure scaled-size is not null: */
    QSize scaledSize = frameBuffer()->scaledSize();
    if (!scaledSize.isValid())
        return;

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    if (!frameBuffer()->useUnscaledHiDPIOutput() && dDevicePixelRatioActual != 1.0)
        scaledSize *= dDevicePixelRatioActual;

    /* Update pause pixmap finally: */
    m_pausePixmapScaled = pausePixmap().scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    /* Take the device-pixel-ratio into account: */
    m_pausePixmapScaled.setDevicePixelRatio(frameBuffer()->devicePixelRatio());
}

void UIMachineView::updateSliders()
{
    /* Make sure framebuffer still present: */
    if (!frameBuffer())
        return;

    /* Get current viewport size: */
    QSize curViewportSize = viewport()->size();
    /* Get maximum viewport size: */
    const QSize maxViewportSize = maximumViewportSize();
    /* Get current frame-buffer size: */
    QSize frameBufferSize = QSize(frameBuffer()->width(), frameBuffer()->height());

    /* Take the scale-factor(s) into account: */
    frameBufferSize = scaledForward(frameBufferSize);

    /* If maximum viewport size can cover whole frame-buffer => no scroll-bars required: */
    if (maxViewportSize.expandedTo(frameBufferSize) == maxViewportSize)
        curViewportSize = maxViewportSize;

    /* What length we want scroll-bars of? */
    int xRange = frameBufferSize.width()  - curViewportSize.width();
    int yRange = frameBufferSize.height() - curViewportSize.height();

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    xRange *= dDevicePixelRatioFormal;
    yRange *= dDevicePixelRatioFormal;
    if (!frameBuffer()->useUnscaledHiDPIOutput())
    {
        xRange /= dDevicePixelRatioActual;
        yRange /= dDevicePixelRatioActual;
    }

    /* Configure scroll-bars: */
    horizontalScrollBar()->setRange(0, xRange);
    verticalScrollBar()->setRange(0, yRange);
    horizontalScrollBar()->setPageStep(curViewportSize.width());
    verticalScrollBar()->setPageStep(curViewportSize.height());
}

void UIMachineView::dimImage(QImage &img)
{
    for (int y = 0; y < img.height(); ++ y)
    {
        if (y % 2)
        {
            if (img.depth() == 32)
            {
                for (int x = 0; x < img.width(); ++ x)
                {
                    int gray = qGray(img.pixel (x, y)) / 2;
                    img.setPixel(x, y, qRgb (gray, gray, gray));
                }
            }
            else
            {
                ::memset(img.scanLine (y), 0, img.bytesPerLine());
            }
        }
        else
        {
            if (img.depth() == 32)
            {
                for (int x = 0; x < img.width(); ++ x)
                {
                    int gray = (2 * qGray (img.pixel (x, y))) / 3;
                    img.setPixel(x, y, qRgb (gray, gray, gray));
                }
            }
        }
    }
}

void UIMachineView::scrollContentsBy(int dx, int dy)
{
    /* Call to base-class: */
    QAbstractScrollArea::scrollContentsBy(dx, dy);

    /* Update console's display viewport and 3D overlay: */
    updateViewport();
}

#ifdef VBOX_WS_MAC
void UIMachineView::updateDockIcon()
{
    machineLogic()->updateDockIcon();
}

CGImageRef UIMachineView::frameBuffertoCGImageRef(UIFrameBuffer *pFrameBuffer)
{
    CGImageRef ir = 0;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    if (cs)
    {
        /* Create the image copy of the framebuffer */
        CGDataProviderRef dp = CGDataProviderCreateWithData(pFrameBuffer, pFrameBuffer->address(), pFrameBuffer->bitsPerPixel() / 8 * pFrameBuffer->width() * pFrameBuffer->height(), NULL);
        if (dp)
        {
            ir = CGImageCreate(pFrameBuffer->width(), pFrameBuffer->height(), 8, 32, pFrameBuffer->bytesPerLine(), cs,
                               kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host, dp, 0, false,
                               kCGRenderingIntentDefault);
            CGDataProviderRelease(dp);
        }
        CGColorSpaceRelease(cs);
    }
    return ir;
}
#endif /* VBOX_WS_MAC */

bool UIMachineView::isFullscreenOrSeamless() const
{
    return    visualStateType() == UIVisualStateType_Fullscreen
           || visualStateType() == UIVisualStateType_Seamless;
}

bool UIMachineView::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    if (pWatched == viewport())
    {
        switch (pEvent->type())
        {
            case QEvent::Resize:
            {
                /* Notify framebuffer about viewport resize: */
                QResizeEvent *pResizeEvent = static_cast<QResizeEvent*>(pEvent);
                if (frameBuffer())
                    frameBuffer()->viewportResized(pResizeEvent);
                /* Update console's display viewport and 3D overlay: */
                updateViewport();
                break;
            }
            default:
                break;
        }
    }

    if (pWatched == this)
    {
        switch (pEvent->type())
        {
            case QEvent::Move:
            {
                /* Update console's display viewport and 3D overlay: */
                updateViewport();
                break;
            }
            default:
                break;
        }
    }

    if (pWatched == machineWindow())
    {
        switch (pEvent->type())
        {
            case QEvent::WindowStateChange:
            {
                /* During minimizing and state restoring machineWindow() gets
                 * the focus which belongs to console view window, so returning it properly. */
                QWindowStateChangeEvent *pWindowEvent = static_cast<QWindowStateChangeEvent*>(pEvent);
                if (pWindowEvent->oldState() & Qt::WindowMinimized)
                {
                    if (QApplication::focusWidget())
                    {
                        QApplication::focusWidget()->clearFocus();
                        qApp->processEvents();
                    }
                    QTimer::singleShot(0, this, SLOT(setFocus()));
                }
                break;
            }
            case QEvent::Move:
            {
                /* Get current host-screen number: */
                const int iCurrentHostScreenNumber = UIDesktopWidgetWatchdog::screenNumber(this);
                if (m_iHostScreenNumber != iCurrentHostScreenNumber)
                {
                    /* Recache current host screen: */
                    m_iHostScreenNumber = iCurrentHostScreenNumber;
                    /* Reapply machine-view scale-factor if necessary: */
                    applyMachineViewScaleFactor();
                    /* For 'normal'/'scaled' visual state type: */
                    if (   visualStateType() == UIVisualStateType_Normal
                        || visualStateType() == UIVisualStateType_Scale)
                    {
                        /* Make sure action-pool is of 'runtime' type: */
                        UIActionPoolRuntime *pActionPool = actionPool() && actionPool()->toRuntime() ? actionPool()->toRuntime() : 0;
                        AssertPtr(pActionPool);
                        if (pActionPool)
                        {
                            /* Inform action-pool about current guest-to-host screen mapping: */
                            QMap<int, int> screenMap = pActionPool->hostScreenForGuestScreenMap();
                            screenMap[m_uScreenId] = m_iHostScreenNumber;
                            pActionPool->setHostScreenForGuestScreenMap(screenMap);
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    return QAbstractScrollArea::eventFilter(pWatched, pEvent);
}

void UIMachineView::resizeEvent(QResizeEvent *pEvent)
{
    updateSliders();
    return QAbstractScrollArea::resizeEvent(pEvent);
}

void UIMachineView::moveEvent(QMoveEvent *pEvent)
{
    return QAbstractScrollArea::moveEvent(pEvent);
}

void UIMachineView::paintEvent(QPaintEvent *pPaintEvent)
{
    /* Use pause-image if exists: */
    if (!pausePixmap().isNull())
    {
        /* Create viewport painter: */
        QPainter painter(viewport());
        /* Avoid painting more than necessary: */
        painter.setClipRect(pPaintEvent->rect());
        /* Can be NULL when the event arrive during COM cleanup: */
        UIFrameBuffer *pFramebuffer = frameBuffer();
        /* Take the scale-factor into account: */
        if (  pFramebuffer
            ? pFramebuffer->scaleFactor() == 1.0 && !pFramebuffer->scaledSize().isValid()
            : pausePixmapScaled().isNull())
            painter.drawPixmap(viewport()->rect().topLeft(), pausePixmap());
        else
            painter.drawPixmap(viewport()->rect().topLeft(), pausePixmapScaled());
#ifdef VBOX_WS_MAC
        /* Update the dock icon: */
        updateDockIcon();
#endif /* VBOX_WS_MAC */
        return;
    }

    /* Delegate the paint function to the UIFrameBuffer interface: */
    if (frameBuffer())
        frameBuffer()->handlePaintEvent(pPaintEvent);
#ifdef VBOX_WS_MAC
    /* Update the dock icon if we are in the running state: */
    if (uimachine()->isRunning())
        updateDockIcon();
#endif /* VBOX_WS_MAC */
}

void UIMachineView::focusInEvent(QFocusEvent *pEvent)
{
    /* Call to base-class: */
    QAbstractScrollArea::focusInEvent(pEvent);

    /* If native event filter isn't exists: */
    if (!m_pNativeEventFilter)
    {
        /* Create/install new native event filter: */
        m_pNativeEventFilter = new UINativeEventFilter(this);
        qApp->installNativeEventFilter(m_pNativeEventFilter);
    }
}

void UIMachineView::focusOutEvent(QFocusEvent *pEvent)
{
    /* If native event filter exists: */
    if (m_pNativeEventFilter)
    {
        /* Uninstall/destroy existing native event filter: */
        qApp->removeNativeEventFilter(m_pNativeEventFilter);
        delete m_pNativeEventFilter;
        m_pNativeEventFilter = 0;
    }

    /* Call to base-class: */
    QAbstractScrollArea::focusOutEvent(pEvent);
}
#ifdef VBOX_WS_NIX
void UIMachineView::keyPressEvent(QKeyEvent *pEvent)
{
    if (uiCommon().displayServerType() == VBGHDISPLAYSERVERTYPE_PURE_WAYLAND)
        uimachine()->putScancode(pEvent->nativeScanCode() - 8);
    QAbstractScrollArea::keyPressEvent(pEvent);
}

void UIMachineView::keyReleaseEvent(QKeyEvent *pEvent)
{
    if (uiCommon().displayServerType() == VBGHDISPLAYSERVERTYPE_PURE_WAYLAND)
        uimachine()->putScancode((pEvent->nativeScanCode() - 8) | 0x80);
    QAbstractScrollArea::keyReleaseEvent(pEvent);
}
#endif
#ifdef VBOX_WITH_DRAG_AND_DROP

bool UIMachineView::dragAndDropCanAccept() const
{
    bool fAccept = m_pDnDHandler;
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
    if (fAccept)
        fAccept = !m_fIsDraggingFromGuest;
# endif
    if (fAccept)
    {
        KDnDMode enmDnDMode = KDnDMode_Disabled;
        uimachine()->acquireDnDMode(enmDnDMode);
        fAccept = enmDnDMode != KDnDMode_Disabled;
    }
    return fAccept;
}

bool UIMachineView::dragAndDropIsActive() const
{
    bool fActive = m_pDnDHandler;
    if (fActive)
    {
        KDnDMode enmDnDMode = KDnDMode_Disabled;
        uimachine()->acquireDnDMode(enmDnDMode);
        fActive = enmDnDMode != KDnDMode_Disabled;
    }
    return fActive;
}

void UIMachineView::dragEnterEvent(QDragEnterEvent *pEvent)
{
    AssertPtrReturnVoid(pEvent);

    int rc = dragAndDropCanAccept() ? VINF_SUCCESS : VERR_ACCESS_DENIED;
    if (RT_SUCCESS(rc))
    {
        /* Get mouse-pointer location. */
        const QPoint &cpnt = viewportToContents(pEvent->position().toPoint());

        /* Ask the target for starting a DnD event. */
        Qt::DropAction result = m_pDnDHandler->dragEnter(screenId(),
                                                         frameBuffer()->convertHostXTo(cpnt.x()),
                                                         frameBuffer()->convertHostYTo(cpnt.y()),
                                                         pEvent->proposedAction(),
                                                         pEvent->possibleActions(),
                                                         pEvent->mimeData());

        /* Set the DnD action returned by the guest. */
        pEvent->setDropAction(result);
        pEvent->accept();
    }

    DNDDEBUG(("DnD: dragEnterEvent ended with rc=%Rrc\n", rc));
}

void UIMachineView::dragMoveEvent(QDragMoveEvent *pEvent)
{
    AssertPtrReturnVoid(pEvent);

    int rc = dragAndDropCanAccept() ? VINF_SUCCESS : VERR_ACCESS_DENIED;
    if (RT_SUCCESS(rc))
    {
        /* Get mouse-pointer location. */
        const QPoint &cpnt = viewportToContents(pEvent->position().toPoint());

        /* Ask the guest for moving the drop cursor. */
        Qt::DropAction result = m_pDnDHandler->dragMove(screenId(),
                                                        frameBuffer()->convertHostXTo(cpnt.x()),
                                                        frameBuffer()->convertHostYTo(cpnt.y()),
                                                        pEvent->proposedAction(),
                                                        pEvent->possibleActions(),
                                                        pEvent->mimeData());

        /* Set the DnD action returned by the guest. */
        pEvent->setDropAction(result);
        pEvent->accept();
    }

    DNDDEBUG(("DnD: dragMoveEvent ended with rc=%Rrc\n", rc));
}

void UIMachineView::dragLeaveEvent(QDragLeaveEvent *pEvent)
{
    AssertPtrReturnVoid(pEvent);

    int rc = dragAndDropCanAccept() ? VINF_SUCCESS : VERR_ACCESS_DENIED;
    if (RT_SUCCESS(rc))
    {
        m_pDnDHandler->dragLeave(screenId());

        pEvent->accept();
    }

    DNDDEBUG(("DnD: dragLeaveEvent ended with rc=%Rrc\n", rc));
}

void UIMachineView::dropEvent(QDropEvent *pEvent)
{
    AssertPtrReturnVoid(pEvent);

    int rc = dragAndDropCanAccept() ? VINF_SUCCESS : VERR_ACCESS_DENIED;
    if (RT_SUCCESS(rc))
    {
        /* Get mouse-pointer location. */
        const QPoint &cpnt = viewportToContents(pEvent->position().toPoint());

        /* Ask the guest for dropping data. */
        Qt::DropAction result = m_pDnDHandler->dragDrop(screenId(),
                                                        frameBuffer()->convertHostXTo(cpnt.x()),
                                                        frameBuffer()->convertHostYTo(cpnt.y()),
                                                        pEvent->proposedAction(),
                                                        pEvent->possibleActions(),
                                                        pEvent->mimeData());

        /* Set the DnD action returned by the guest. */
        pEvent->setDropAction(result);
        pEvent->accept();
    }

    DNDDEBUG(("DnD: dropEvent ended with rc=%Rrc\n", rc));
}

#endif /* VBOX_WITH_DRAG_AND_DROP */

QSize UIMachineView::scaledForward(QSize size) const
{
    /* Take the scale-factor into account: */
    const double dScaleFactor = frameBuffer()->scaleFactor();
    if (dScaleFactor != 1.0)
        size = QSize((int)(size.width() * dScaleFactor), (int)(size.height() * dScaleFactor));

    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    if (!frameBuffer()->useUnscaledHiDPIOutput())
        size = QSize((int)(size.width() * dDevicePixelRatioActual), (int)(size.height() * dDevicePixelRatioActual));
    size = QSize((int)(size.width() / dDevicePixelRatioFormal), (int)(size.height() / dDevicePixelRatioFormal));

    /* Return result: */
    return size;
}

QSize UIMachineView::scaledBackward(QSize size) const
{
    /* Take the device-pixel-ratio into account: */
    const double dDevicePixelRatioFormal = frameBuffer()->devicePixelRatio();
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    size = QSize((int)(size.width() * dDevicePixelRatioFormal), (int)(size.height() * dDevicePixelRatioFormal));
    if (!frameBuffer()->useUnscaledHiDPIOutput())
        size = QSize(size.width() / dDevicePixelRatioActual, size.height() / dDevicePixelRatioActual);

    /* Take the scale-factor into account: */
    const double dScaleFactor = frameBuffer()->scaleFactor();
    if (dScaleFactor != 1.0)
        size = QSize((int)(size.width() / dScaleFactor), (int)(size.height() / dScaleFactor));

    /* Return result: */
    return size;
}

void UIMachineView::updateMousePointerPixmapScaling(QPixmap &pixmap, uint &uXHot, uint &uYHot)
{
#if defined(VBOX_WS_MAC)

    /* Take into account scale-factor if necessary: */
    const double dScaleFactor = frameBuffer()->scaleFactor();
    //printf("Scale-factor: %f\n", dScaleFactor);
    if (dScaleFactor > 1.0)
    {
        /* Scale the pixmap up: */
        pixmap = pixmap.scaled((int)(pixmap.width() * dScaleFactor), (int)(pixmap.height() * dScaleFactor),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        uXHot *= dScaleFactor;
        uYHot *= dScaleFactor;
    }

    /* Take into account device-pixel-ratio if necessary: */
    const double dDevicePixelRatio = frameBuffer()->devicePixelRatio();
    const bool fUseUnscaledHiDPIOutput = frameBuffer()->useUnscaledHiDPIOutput();
    //printf("Device-pixel-ratio: %f, Unscaled HiDPI Output: %d\n",
    //       dDevicePixelRatio, fUseUnscaledHiDPIOutput);
    if (dDevicePixelRatio > 1.0 && fUseUnscaledHiDPIOutput)
    {
        /* Scale the pixmap down: */
        pixmap.setDevicePixelRatio(dDevicePixelRatio);
        uXHot /= dDevicePixelRatio;
        uYHot /= dDevicePixelRatio;
    }

#elif defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)

    /* We want to scale the pixmap just once, so let's prepare cumulative multiplier: */
    double dScaleMultiplier = 1.0;

    /* Take into account scale-factor if necessary: */
    const double dScaleFactor = frameBuffer()->scaleFactor();
    //printf("Scale-factor: %f\n", dScaleFactor);
    if (dScaleFactor > 1.0)
        dScaleMultiplier *= dScaleFactor;

    /* Take into account device-pixel-ratio if necessary: */
# ifdef VBOX_WS_WIN
    const double dDevicePixelRatio = frameBuffer()->devicePixelRatio();
# endif
    const double dDevicePixelRatioActual = frameBuffer()->devicePixelRatioActual();
    const bool fUseUnscaledHiDPIOutput = frameBuffer()->useUnscaledHiDPIOutput();
    //printf("Device-pixel-ratio/actual: %f/%f, Unscaled HiDPI Output: %d\n",
    //       dDevicePixelRatio, dDevicePixelRatioActual, fUseUnscaledHiDPIOutput);
    if (dDevicePixelRatioActual > 1.0 && !fUseUnscaledHiDPIOutput)
        dScaleMultiplier *= dDevicePixelRatioActual;

    /* If scale multiplier was set: */
    if (dScaleMultiplier > 1.0)
    {
        /* Scale the pixmap up: */
        pixmap = pixmap.scaled(pixmap.width() * dScaleMultiplier, pixmap.height() * dScaleMultiplier,
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        uXHot *= dScaleMultiplier;
        uYHot *= dScaleMultiplier;
    }

# ifdef VBOX_WS_WIN
    /* If device pixel ratio was set: */
    if (dDevicePixelRatio > 1.0)
    {
        /* Scale the pixmap down: */
        pixmap.setDevicePixelRatio(dDevicePixelRatio);
        uXHot /= dDevicePixelRatio;
        uYHot /= dDevicePixelRatio;
    }
# endif

#else

    Q_UNUSED(pixmap);
    Q_UNUSED(uXHot);
    Q_UNUSED(uYHot);

#endif
}
