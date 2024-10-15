/* $Id: UIMachineView.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineView class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIMachineView_h
#define FEQT_INCLUDED_SRC_runtime_UIMachineView_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QAbstractScrollArea>
#include <QEventLoop>

/* GUI includes: */
#include "UIExtraDataDefs.h"
#include "UIFrameBuffer.h"
#include "UIMachineDefs.h"
#ifdef VBOX_WITH_DRAG_AND_DROP
# include "UIDnDHandler.h"
#endif /* VBOX_WITH_DRAG_AND_DROP */

/* COM includes: */
#include "KMachineState.h"

/* Other VBox includes: */
#include "VBox/com/ptr.h"
#ifdef VBOX_WS_MAC
#  include <ApplicationServices/ApplicationServices.h>
#endif /* VBOX_WS_MAC */

/* External includes: */
#ifdef VBOX_WS_MAC
# include <CoreFoundation/CFBase.h>
#endif /* VBOX_WS_MAC */

/* Forward declarations: */
class UIActionPool;
class UIMachine;
class UIMachineLogic;
class UIMachineWindow;
class UINativeEventFilter;
class CGuest;
class CSession;
#ifdef VBOX_WITH_DRAG_AND_DROP
class CDnDTarget;
#endif


class UIMachineView : public QAbstractScrollArea
{
    Q_OBJECT;

signals:

    /** Notifies about mouse pointer shape change. */
    void sigMousePointerShapeChange();
    /** Notifies about frame-buffer resize. */
    void sigFrameBufferResize();

public:

    /* Factory function to create machine-view: */
    static UIMachineView* create(UIMachineWindow *pMachineWindow, ulong uScreenId, UIVisualStateType visualStateType);
    /* Factory function to destroy required machine-view: */
    static void destroy(UIMachineView *pMachineView);

    /** Returns whether the guest-screen auto-resize is enabled. */
    virtual bool isGuestAutoresizeEnabled() const { return true; }
    /** Defines whether the guest-screen auto-resize is @a fEnabled. */
    virtual void setGuestAutoresizeEnabled(bool fEnabled) { Q_UNUSED(fEnabled); }

    /** Send saved guest-screen size-hint to the guest.
      * @note Reimplemented in sub-classes. Base implementation does nothing. */
    virtual void resendSizeHint() {}

    /** Adjusts guest-screen size to correspond current visual-style.
      * @note Reimplemented in sub-classes. Base implementation does nothing. */
    virtual void adjustGuestScreenSize() {}

    /** Applies machine-view scale-factor. */
    virtual void applyMachineViewScaleFactor();

    /** Returns screen ID for this view. */
    ulong screenId() const { return m_uScreenId; }

    /** Returns the machine UI reference. */
    UIMachine *uimachine() const;
    /** Returns the machine-logic reference. */
    UIMachineLogic *machineLogic() const;
    /** Returns the machine-window reference. */
    UIMachineWindow *machineWindow() const { return m_pMachineWindow; }
    /** Returns view's frame-buffer reference. */
    UIFrameBuffer *frameBuffer() const;

    /** Returns actual contents width. */
    int contentsWidth() const;
    /** Returns actual contents height. */
    int contentsHeight() const;
    /** Returns actual contents x origin. */
    int contentsX() const;
    /** Returns actual contents y origin. */
    int contentsY() const;
    /** Returns visible contents width. */
    int visibleWidth() const;
    /** Returns visible contents height. */
    int visibleHeight() const;
    /** Translates viewport point to contents point. */
    QPoint viewportToContents(const QPoint &viewportPoint) const;
    /** Scrolls contents by @a iDx x iDy pixels. */
    void scrollBy(int iDx, int iDy);

    /** What view mode (normal, fullscreen etc.) are we in? */
    UIVisualStateType visualStateType() const;

    /** Returns cached mouse cursor. */
    QCursor cursor() const { return m_cursor; }

    /* Framebuffer aspect ratio: */
    double aspectRatio() const;

    /** Atomically store the maximum guest resolution which we currently wish
     * to handle for @a maximumGuestSize() to read.  Should be called if anything
     * happens (e.g. a screen hotplug) which might cause the value to change.
     * @sa m_u64MaximumGuestSize. */
    void setMaximumGuestSize(const QSize &minimumSizeHint = QSize());
    /** Atomically read the maximum guest resolution which we currently wish to
     * handle.  This may safely be called from another thread (called by
     * UIFramebuffer on EMT).
     * @sa m_u64MaximumGuestSize. */
    QSize maximumGuestSize();

    /** Updates console's display viewport.
      * @remarks Used to update 3D-service overlay viewport as well. */
    void updateViewport();

#ifdef VBOX_WITH_DRAG_AND_DROP
    /** Checks for a pending drag and drop event within the guest and
      * (optionally) starts a drag and drop operation on the host. */
    int dragCheckPending();
    /** Starts a drag and drop operation from guest to the host.
      * This internally either uses Qt's abstract QDrag methods
      * or some other OS-dependent implementation. */
    int dragStart();
    /** Aborts (and resets) the current (pending)
      * guest to host drag and drop operation. */
    int dragStop();
#endif /* VBOX_WITH_DRAG_AND_DROP */

    /** Performs pre-processing of all the native events. */
    virtual bool nativeEventPreprocessor(const QByteArray &eventType, void *pMessage);

#ifdef VBOX_WS_MAC
    /** Returns VM contents image. */
    CGImageRef vmContentImage();
#endif

public slots:

    /** Handles NotifyChange event received from frame-buffer.
      * @todo To make it right, this have to be protected, but
      *       connection should be moved from frame-buffer to this class. */
    virtual void sltHandleNotifyChange(int iWidth, int iHeight);

    /** Handles NotifyUpdate event received from frame-buffer.
      * @todo To make it right, this have to be protected, but
      *       connection should be moved from frame-buffer to this class. */
    virtual void sltHandleNotifyUpdate(int iX, int iY, int iWidth, int iHeight);

    /** Handles SetVisibleRegion event received from frame-buffer.
      * @todo To make it right, this have to be protected, but
      *       connection should be moved from frame-buffer to this class. */
    virtual void sltHandleSetVisibleRegion(QRegion region);

protected slots:

    /* Performs guest-screen resize to a size specified.
     * @param  toSize  Brings the size guest-screen needs to be resized to.
     * @note   If toSize isn't valid or sane one, it will be replaced with actual
     *         size of centralWidget() containing this machine-view currently.
     * @note   Also, take into acount that since this method is also called to
     *         resize to centralWidget() size, the size passed is expected to be
     *         tranformed to internal coordinate system and thus to be restored to
     *         guest coordinate system (absolute one) before passing to guest. */
    void sltPerformGuestResize(const QSize &toSize = QSize());

    /** Handles guest-screen toggle request.
      * @param  iScreen   Brings the number of screen being referred.
      * @param  fEnabled  Brings whether this screen should be enabled. */
    void sltHandleActionTriggerViewScreenToggle(int iScreen, bool fEnabled);
    /** Handles guest-screen resize request.
      * @param  iScreen  Brings the number of screen being referred.
      * @param  size     Brings the size of screen to be applied. */
    void sltHandleActionTriggerViewScreenResize(int iScreen, const QSize &size);

    /* Watch dog for desktop resizes: */
    void sltDesktopResized();

    /** Handles the scale-factor change. */
    void sltHandleScaleFactorChange(const QUuid &uMachineID);

    /** Handles the scaling-optimization change. */
    void sltHandleScalingOptimizationChange(const QUuid &uMachineID);

    /* Console callback handlers: */
    virtual void sltMachineStateChanged();
    /** Handles guest request to change the mouse pointer shape. */
    void sltMousePointerShapeChange();

    /** Detaches COM. */
    void sltDetachCOM();

    /** Handles translation event. */
    void sltRetranslateUI();

protected:

    /* Machine-view constructor: */
    UIMachineView(UIMachineWindow *pMachineWindow, ulong uScreenId);
    /* Machine-view destructor: */
    virtual ~UIMachineView() {}

    /* Prepare routines: */
    virtual void loadMachineViewSettings();
    //virtual void prepareNativeFilters() {}
    virtual void prepareViewport();
    virtual void prepareFrameBuffer();
    virtual void prepareCommon();
#ifdef VBOX_WITH_DRAG_AND_DROP
    virtual int  prepareDnd();
#endif
    virtual void prepareFilters();
    virtual void prepareConnections();
    virtual void prepareConsoleConnections();

    /* Cleanup routines: */
    //virtual void cleanupConsoleConnections() {}
    //virtual void cleanupConnections() {}
    //virtual void cleanupFilters() {}
#ifdef VBOX_WITH_DRAG_AND_DROP
    virtual void cleanupDnd();
#endif
    //virtual void cleanupCommon() {}
    virtual void cleanupFrameBuffer();
    //virtual void cleanupViewport();
    virtual void cleanupNativeFilters();
    //virtual void saveMachineViewSettings() {}

    /* Protected getters: */
    UIActionPool* actionPool() const;
    QSize sizeHint() const RT_OVERRIDE;

    /** Retrieves the last guest-screen size-hint from extra-data. */
    QSize storedGuestScreenSizeHint() const;
    /** Stores a guest-screen @a sizeHint to extra-data. */
    void setStoredGuestScreenSizeHint(const QSize &sizeHint);

    /** Retrieves the sent guest-screen size-hint from display or frame-buffer. */
    QSize requestedGuestScreenSizeHint() const;

    /** Retrieves the last guest-screen visibility status from extra-data. */
    bool guestScreenVisibilityStatus() const;

    /** Handles machine-view scale changes. */
    void handleScaleChange();

    /** Returns the pause-pixmap: */
    const QPixmap& pausePixmap() const { return m_pausePixmap; }
    /** Returns the scaled pause-pixmap: */
    const QPixmap& pausePixmapScaled() const { return m_pausePixmapScaled; }
    /** Resets the pause-pixmap. */
    void resetPausePixmap();
    /** Acquires live pause-pixmap. */
    void takePausePixmapLive();
    /** Acquires snapshot pause-pixmap. */
    void takePausePixmapSnapshot();
    /** Updates the scaled pause-pixmap. */
    void updateScaledPausePixmap();

    /** The available area on the current screen for application windows. */
    virtual QRect workingArea() const = 0;
    /** Calculate how big the guest desktop can be while still fitting on one
     * host screen. */
    virtual QSize calculateMaxGuestSize() const = 0;
    virtual void updateSliders();
    static void dimImage(QImage &img);
    void scrollContentsBy(int dx, int dy) RT_OVERRIDE RT_FINAL;
#ifdef VBOX_WS_MAC
    void updateDockIcon();
    CGImageRef frameBuffertoCGImageRef(UIFrameBuffer *pFrameBuffer);
#endif /* VBOX_WS_MAC */
    /** Is this a fullscreen-type view? */
    bool isFullscreenOrSeamless() const;

    /* Cross-platforms event processors: */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;
    void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;
    void moveEvent(QMoveEvent *pEvent) RT_OVERRIDE RT_FINAL;
    void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Handles focus-in @a pEvent. */
    void focusInEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles focus-out @a pEvent. */
    void focusOutEvent(QFocusEvent *pEvent) RT_OVERRIDE RT_FINAL;
#ifdef VBOX_WS_NIX
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE RT_FINAL;
    virtual void keyReleaseEvent(QKeyEvent *pEvent) RT_OVERRIDE RT_FINAL;
#endif

#ifdef VBOX_WITH_DRAG_AND_DROP
    /**
     * Returns @true if the VM window can accept (start is, start) a drag and drop
     * operation, @false if not.
     */
    bool dragAndDropCanAccept() const;

    /**
     * Returns @true if drag and drop for this machine is active
     * (that is, host->guest, guest->host or bidirectional), @false if not.
     */
    bool dragAndDropIsActive() const;

    /**
     * Host -> Guest: Issued when the host cursor enters the guest (VM) window.
     *                The guest will receive the relative cursor coordinates of the
     *                appropriate screen ID.
     *
     * @param pEvent                Related enter event.
     */
    void dragEnterEvent(QDragEnterEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /**
     * Host -> Guest: Issued when the host cursor moves inside (over) the guest (VM) window.
     *                The guest will receive the relative cursor coordinates of the
     *                appropriate screen ID.
     *
     * @param pEvent                Related move event.
     */
    void dragLeaveEvent(QDragLeaveEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /**
     * Host -> Guest: Issued when the host cursor leaves the guest (VM) window again.
     *                This will ask the guest to stop any further drag'n drop operation.
     *
     * @param pEvent                Related leave event.
     */
    void dragMoveEvent(QDragMoveEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /**
     * Host -> Guest: Issued when the host drops data into the guest (VM) window.
     *
     * @param pEvent                Related drop event.
     */
    void dropEvent(QDropEvent *pEvent) RT_OVERRIDE RT_FINAL;
#endif /* VBOX_WITH_DRAG_AND_DROP */

    /** Scales passed size forward. */
    QSize scaledForward(QSize size) const;
    /** Scales passed size backward. */
    QSize scaledBackward(QSize size) const;

    /** Updates mouse pointer @a pixmap, @a uXHot and @a uYHot according to scaling attributes. */
    void updateMousePointerPixmapScaling(QPixmap &pixmap, uint &uXHot, uint &uYHot);

    /* Protected members: */
    UIMachineWindow *m_pMachineWindow;
    ulong m_uScreenId;
    KMachineState m_previousState;
    /** HACK: when switching out of fullscreen or seamless we wish to override
     * the default size hint to avoid short resizes back to fullscreen size.
     * Not explicitly initialised (i.e. invalid by default). */
    QSize m_sizeHintOverride;

    /** Last size hint sent as a part of guest auto-resize feature.
      * @note Useful to avoid spamming CDisplay with same hint before
      *       frame-buffer finally resized to requested size. */
    QSize  m_lastSizeHint;

    /** Holds current host-screen number. */
    int m_iHostScreenNumber;

    /** Holds the maximum guest screen size policy. */
    MaximumGuestScreenSizePolicy m_enmMaximumGuestScreenSizePolicy;
    /** The maximum guest size for fixed size policy. */
    QSize m_fixedMaxGuestSize;
    /** Maximum guest resolution which we wish to handle.  Must be accessed
     * atomically.
     * @note The background for this variable is that we need this value to be
     * available to the EMT thread, but it can only be calculated by the
     * GUI, and GUI code can only safely be called on the GUI thread due to
     * (at least) X11 threading issues.  So we calculate the value in advance,
     * monitor things in case it changes and update it atomically when it does.
     */
    /** @todo This should be private. */
    volatile uint64_t m_u64MaximumGuestSize;

    /** Holds the pause-pixmap. */
    QPixmap m_pausePixmap;
    /** Holds the scaled pause-pixmap. */
    QPixmap m_pausePixmapScaled;

    /** Holds cached mouse cursor. */
    QCursor  m_cursor;

#ifdef VBOX_WITH_DRAG_AND_DROP
    /** Pointer to drag and drop handler instance. */
    UIDnDHandler *m_pDnDHandler;
# ifdef VBOX_WITH_DRAG_AND_DROP_GH
    /** Flag indicating whether a guest->host drag currently is in
     *  progress or not. */
    bool m_fIsDraggingFromGuest;
# endif
#endif

    /** Holds the native event filter instance. */
    UINativeEventFilter *m_pNativeEventFilter;
};

/* This maintenance class is a part of future roll-back mechanism.
 * It allows to block main GUI thread until specific event received.
 * Later it will become more abstract but now its just used to help
 * fullscreen & seamless modes to restore normal guest size hint. */
/** @todo This class is now unused - can it be removed altogether? */
class UIMachineViewBlocker : public QEventLoop
{
    Q_OBJECT;

public:

    UIMachineViewBlocker()
        : QEventLoop(0)
        , m_iTimerId(0)
    {
        /* Also start timer to unlock pool in case of
         * required condition doesn't happens by some reason: */
        m_iTimerId = startTimer(3000);
    }

    virtual ~UIMachineViewBlocker()
    {
        /* Kill the timer: */
        killTimer(m_iTimerId);
    }

protected:

    void timerEvent(QTimerEvent *pEvent) RT_OVERRIDE RT_FINAL
    {
        /* If that timer event occurs => it seems
         * guest resize event doesn't comes in time,
         * shame on it, but we just unlocking 'this': */
        QEventLoop::timerEvent(pEvent);
        exit();
    }

    int m_iTimerId;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIMachineView_h */
