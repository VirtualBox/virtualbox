/* $Id: VBoxUtils-darwin.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility classes and functions for handling Darwin specific tasks.
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_VBoxUtils_darwin_h
#define FEQT_INCLUDED_SRC_platform_darwin_VBoxUtils_darwin_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QRect>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Other VBox includes: */
#include <VBox/VBoxCocoa.h>
#include <ApplicationServices/ApplicationServices.h>
#undef PVM // Stupid, stupid apple headers (sys/param.h)!!

/* External includes: */
#include <ApplicationServices/ApplicationServices.h>

/* Forward declarations: */
class QImage;
class QMainWindow;
class QMenu;
class QPixmap;
class QToolBar;
class QWidget;

/* Cocoa declarations: */
ADD_COCOA_NATIVE_REF(NSButton);
ADD_COCOA_NATIVE_REF(NSEvent);
ADD_COCOA_NATIVE_REF(NSImage);
ADD_COCOA_NATIVE_REF(NSString);
ADD_COCOA_NATIVE_REF(NSView);
ADD_COCOA_NATIVE_REF(NSWindow);


/** Mac OS X: Standard window button types. */
enum StandardWindowButtonType
{
    StandardWindowButtonType_Close,            // Since OS X 10.2
    StandardWindowButtonType_Miniaturize,      // Since OS X 10.2
    StandardWindowButtonType_Zoom,             // Since OS X 10.2
    StandardWindowButtonType_Toolbar,          // Since OS X 10.2
    StandardWindowButtonType_DocumentIcon,     // Since OS X 10.2
    StandardWindowButtonType_DocumentVersions, // Since OS X 10.7
    StandardWindowButtonType_FullScreen        // Since OS X 10.7
};


RT_C_DECLS_BEGIN

/********************************************************************************
 *
 * Window/View management (OS System native)
 *
 ********************************************************************************/
NativeNSWindowRef darwinToNativeWindowImpl(NativeNSViewRef pView);
NativeNSViewRef darwinToNativeViewImpl(NativeNSWindowRef pWindow);
NativeNSButtonRef darwinNativeButtonOfWindowImpl(NativeNSWindowRef pWindow, StandardWindowButtonType enmButtonType);
SHARED_LIBRARY_STUFF NativeNSStringRef darwinToNativeString(const char* pcszString);
QString darwinFromNativeString(NativeNSStringRef pString);

/********************************************************************************
 *
 * Simple setter methods (OS System native)
 *
 ********************************************************************************/
void darwinSetShowsToolbarButtonImpl(NativeNSWindowRef pWindow, bool fEnabled);
void darwinSetShowsResizeIndicatorImpl(NativeNSWindowRef pWindow, bool fEnabled);
void darwinSetHidesAllTitleButtonsImpl(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF void darwinLabelWindow(NativeNSWindowRef pWindow, NativeNSImageRef pImage, double dDpr);
void darwinSetShowsWindowTransparentImpl(NativeNSWindowRef pWindow, bool fEnabled);
SHARED_LIBRARY_STUFF void darwinSetWindowHasShadow(NativeNSWindowRef pWindow, bool fEnabled);
SHARED_LIBRARY_STUFF void darwinSetMouseCoalescingEnabled(bool fEnabled);

void darwintest(NativeNSWindowRef pWindow);
/********************************************************************************
 *
 * Simple helper methods (OS System native)
 *
 ********************************************************************************/
void darwinWindowAnimateResizeImpl(NativeNSWindowRef pWindow, int x, int y, int width, int height);
void darwinWindowAnimateResizeNewImpl(NativeNSWindowRef pWindow, int height, bool fAnimate);
void darwinTest(NativeNSViewRef pView, NativeNSViewRef pView1, int h);
void darwinWindowInvalidateShapeImpl(NativeNSWindowRef pWindow);
void darwinWindowInvalidateShadowImpl(NativeNSWindowRef pWindow);
int  darwinWindowToolBarHeight(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF int darwinWindowTitleHeight(NativeNSWindowRef pWindow);
bool darwinIsToolbarVisible(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF bool darwinIsWindowMaximized(NativeNSWindowRef pWindow);
void darwinMinaturizeWindow(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF void darwinEnableFullscreenSupport(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF void darwinEnableTransienceSupport(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF void darwinToggleFullscreenMode(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF void darwinToggleWindowZoom(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF bool darwinIsInFullscreenMode(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF bool darwinIsOnActiveSpace(NativeNSWindowRef pWindow);
SHARED_LIBRARY_STUFF bool darwinScreensHaveSeparateSpaces();
SHARED_LIBRARY_STUFF bool darwinIsScrollerStyleOverlay();

bool darwinOpenFile(NativeNSStringRef pstrFile);

SHARED_LIBRARY_STUFF float darwinSmallFontSize();
SHARED_LIBRARY_STUFF bool darwinSetFrontMostProcess();
SHARED_LIBRARY_STUFF uint64_t darwinGetCurrentProcessId();

void darwinInstallResizeDelegate(NativeNSWindowRef pWindow);
void darwinUninstallResizeDelegate(NativeNSWindowRef pWindow);

bool darwinUnifiedToolbarEvents(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser);
bool darwinMouseGrabEvents(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser);
void darwinCreateContextMenuEvent(void *pvWin, int x, int y);

SHARED_LIBRARY_STUFF bool darwinIsApplicationCommand(ConstNativeNSEventRef pEvent);

void darwinRetranslateAppMenu();

void darwinSendMouseGrabEvents(QWidget *pWidget, int type, int button, int buttons, int x, int y);

SHARED_LIBRARY_STUFF QString darwinResolveAlias(const QString &strFile);

RT_C_DECLS_END

DECLINLINE(CGRect) darwinToCGRect(const QRect& aRect) { return CGRectMake(aRect.x(), aRect.y(), aRect.width(), aRect.height()); }
DECLINLINE(CGRect) darwinFlipCGRect(CGRect aRect, double aTargetHeight) { aRect.origin.y = aTargetHeight - aRect.origin.y - aRect.size.height; return aRect; }
DECLINLINE(CGRect) darwinFlipCGRect(CGRect aRect, const CGRect &aTarget) { return darwinFlipCGRect(aRect, aTarget.size.height); }
DECLINLINE(CGRect) darwinCenterRectTo(CGRect aRect, const CGRect& aToRect)
{
    aRect.origin.x = aToRect.origin.x + (aToRect.size.width  - aRect.size.width)  / 2.0;
    aRect.origin.y = aToRect.origin.y + (aToRect.size.height - aRect.size.height) / 2.0;
    return aRect;
}

/********************************************************************************
 *
 * Window/View management (Qt Wrapper)
 *
 ********************************************************************************/

/**
 * Returns a reference to the native View of the QWidget.
 *
 * @returns either HIViewRef or NSView* of the QWidget.
 * @param   pWidget   Pointer to the QWidget
 */
NativeNSViewRef darwinToNativeView(QWidget *pWidget);

/**
 * Returns a reference to the native Window of the QWidget.
 *
 * @returns either WindowRef or NSWindow* of the QWidget.
 * @param   pWidget   Pointer to the QWidget
 */
NativeNSWindowRef darwinToNativeWindow(QWidget *pWidget);

/* This is necessary because of the C calling convention. Its a simple wrapper
   for darwinToNativeWindowImpl to allow operator overloading which isn't
   allowed in C. */
/**
 * Returns a reference to the native Window of the View..
 *
 * @returns either WindowRef or NSWindow* of the View.
 * @param   pWidget   Pointer to the native View
 */
NativeNSWindowRef darwinToNativeWindow(NativeNSViewRef pView);

/**
 * Returns a reference to the native View of the Window.
 *
 * @returns either HIViewRef or NSView* of the Window.
 * @param   pWidget   Pointer to the native Window
 */
NativeNSViewRef darwinToNativeView(NativeNSWindowRef pWindow);

/**
 * Returns a reference to the native button of QWidget.
 *
 * @returns corresponding NSButton* of the QWidget.
 * @param   pWidget       Brings the pointer to the QWidget.
 * @param   enmButtonType Brings the type of the native button required.
 */
NativeNSButtonRef darwinNativeButtonOfWindow(QWidget *pWidget, StandardWindowButtonType enmButtonType);

/********************************************************************************
 *
 * Graphics stuff (Qt Wrapper)
 *
 ********************************************************************************/
/**
 * Returns a reference to the CGContext of the QWidget.
 *
 * @returns CGContextRef of the QWidget.
 * @param   pWidget      Pointer to the QWidget
 */
SHARED_LIBRARY_STUFF CGImageRef darwinToCGImageRef(const QImage *pImage);
SHARED_LIBRARY_STUFF CGImageRef darwinToCGImageRef(const QPixmap *pPixmap);
SHARED_LIBRARY_STUFF CGImageRef darwinToCGImageRef(const char *pczSource);

SHARED_LIBRARY_STUFF NativeNSImageRef darwinToNSImageRef(const CGImageRef pImage);
SHARED_LIBRARY_STUFF NativeNSImageRef darwinToNSImageRef(const QImage *pImage);
SHARED_LIBRARY_STUFF NativeNSImageRef darwinToNSImageRef(const QPixmap *pPixmap);
SHARED_LIBRARY_STUFF NativeNSImageRef darwinToNSImageRef(const char *pczSource);

#include <QEvent>
class UIGrabMouseEvent: public QEvent
{
public:
    enum { GrabMouseEvent = QEvent::User + 200 };

    UIGrabMouseEvent(QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons, int x, int y, int wheelDelta, Qt::Orientation o)
      : QEvent((QEvent::Type)GrabMouseEvent)
      , m_type(type)
      , m_button(button)
      , m_buttons(buttons)
      , m_x(x)
      , m_y(y)
      , m_wheelDelta(wheelDelta)
      , m_orientation(o)
    {}
    QEvent::Type mouseEventType() const { return m_type; }
    Qt::MouseButton button() const { return m_button; }
    Qt::MouseButtons buttons() const { return m_buttons; }
    int xDelta() const { return m_x; }
    int yDelta() const { return m_y; }
    int wheelDelta() const { return m_wheelDelta; }
    Qt::Orientation orientation() const { return m_orientation; }

private:
    /* Private members */
    QEvent::Type m_type;
    Qt::MouseButton m_button;
    Qt::MouseButtons m_buttons;
    int m_x;
    int m_y;
    int m_wheelDelta;
    Qt::Orientation m_orientation;
};

/********************************************************************************
 *
 * Simple setter methods (Qt Wrapper)
 *
 ********************************************************************************/
void darwinSetShowsToolbarButton(QToolBar *aToolBar, bool fEnabled);
SHARED_LIBRARY_STUFF void darwinLabelWindow(QWidget *pWidget, QPixmap *pPixmap);
void darwinSetShowsResizeIndicator(QWidget *pWidget, bool fEnabled);
SHARED_LIBRARY_STUFF void darwinSetHidesAllTitleButtons(QWidget *pWidget);
void darwinSetShowsWindowTransparent(QWidget *pWidget, bool fEnabled);
SHARED_LIBRARY_STUFF void darwinSetWindowHasShadow(QWidget *pWidget, bool fEnabled);
SHARED_LIBRARY_STUFF void darwinDisableIconsInMenus(void);

void darwinTest(QWidget *pWidget1, QWidget *pWidget2, int h);

/********************************************************************************
 *
 * Simple helper methods (Qt Wrapper)
 *
 ********************************************************************************/
SHARED_LIBRARY_STUFF void darwinWindowAnimateResize(QWidget *pWidget, const QRect &aTarget);
void darwinWindowAnimateResizeNew(QWidget *pWidget, int h, bool fAnimate);
void darwinWindowInvalidateShape(QWidget *pWidget);
void darwinWindowInvalidateShadow(QWidget *pWidget);
int  darwinWindowToolBarHeight(QWidget *pWidget);
SHARED_LIBRARY_STUFF int darwinWindowTitleHeight(QWidget *pWidget);
bool darwinIsToolbarVisible(QToolBar *pToolBar);
SHARED_LIBRARY_STUFF bool darwinIsWindowMaximized(QWidget *pWidget);
void darwinMinaturizeWindow(QWidget *pWidget);
SHARED_LIBRARY_STUFF void darwinEnableFullscreenSupport(QWidget *pWidget);
SHARED_LIBRARY_STUFF void darwinEnableTransienceSupport(QWidget *pWidget);
SHARED_LIBRARY_STUFF void darwinToggleFullscreenMode(QWidget *pWidget);
SHARED_LIBRARY_STUFF void darwinToggleWindowZoom(QWidget *pWidget);
SHARED_LIBRARY_STUFF bool darwinIsInFullscreenMode(QWidget *pWidget);
SHARED_LIBRARY_STUFF bool darwinIsOnActiveSpace(QWidget *pWidget);
bool darwinOpenFile(const QString &strFile);

QString darwinSystemLanguage(void);
QPixmap darwinCreateDragPixmap(const QPixmap& aPixmap, const QString &aText);

void darwinInstallResizeDelegate(QWidget *pWidget);
void darwinUninstallResizeDelegate(QWidget *pWidget);

SHARED_LIBRARY_STUFF void darwinRegisterForUnifiedToolbarContextMenuEvents(QMainWindow *pWindow);
SHARED_LIBRARY_STUFF void darwinUnregisterForUnifiedToolbarContextMenuEvents(QMainWindow *pWindow);

SHARED_LIBRARY_STUFF void darwinMouseGrab(QWidget *pWidget);
SHARED_LIBRARY_STUFF void darwinMouseRelease(QWidget *pWidget);

SHARED_LIBRARY_STUFF void *darwinCocoaToCarbonEvent(void *pvCocoaEvent);

#endif /* !FEQT_INCLUDED_SRC_platform_darwin_VBoxUtils_darwin_h */
