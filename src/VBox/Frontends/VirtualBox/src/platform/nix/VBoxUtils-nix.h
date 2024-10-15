/* $Id: VBoxUtils-nix.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility classes and functions for handling X11 specific tasks.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_platform_nix_VBoxUtils_nix_h
#define FEQT_INCLUDED_SRC_platform_nix_VBoxUtils_nix_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>
#include <QVector>
#include <QWindow>

#include <VBox/GuestHost/DisplayServerType.h>

/* GUI includes: */
#include "UILibraryDefs.h"

/** X11: Known Window Manager types. */
enum X11WMType
{
    X11WMType_Unknown,
    X11WMType_Compiz,
    X11WMType_GNOMEShell,
    X11WMType_KWin,
    X11WMType_Metacity,
    X11WMType_Mutter,
    X11WMType_Xfwm4,
};

/** X11: Screen-saver inhibit methods. */
struct SHARED_LIBRARY_STUFF DBusScreenSaverInhibitMethod
{
    QString  m_strServiceName;
    QString  m_strInterface;
    QString  m_strPath;
    uint     m_iCookie;
};

/** X11: XCB size-hints. */
typedef struct
{
    /** User specified flags */
    uint32_t flags;
    /** User-specified position */
    int32_t x, y;
    /** User-specified size */
    int32_t width, height;
    /** Program-specified minimum size */
    int32_t min_width, min_height;
    /** Program-specified maximum size */
    int32_t max_width, max_height;
    /** Program-specified resize increments */
    int32_t width_inc, height_inc;
    /** Program-specified minimum aspect ratios */
    int32_t min_aspect_num, min_aspect_den;
    /** Program-specified maximum aspect ratios */
    int32_t max_aspect_num, max_aspect_den;
    /** Program-specified base size */
    int32_t base_width, base_height;
    /** Program-specified window gravity */
    uint32_t win_gravity;
} xcb_size_hints_t;

/* X11 structs to avoid dragging in unnecessary X headers: */
struct xcb_connection_t;
struct _XDisplay;

/* Namespace for native window sub-system functions: */
namespace NativeWindowSubsystem
{
    /** Wrapper function for X11IsCompositingManagerRunning and WaylandIsCompositingManagerRunning. */
    bool isCompositingManagerRunning(bool fIsXServerAvailable);
    /** X11: Determines and returns whether the compositing manager is running. */
    bool X11IsCompositingManagerRunning();
    /** Wayland: Determines and returns whether the compositing manager is running. */
    bool WaylandIsCompositingManagerRunning();

    /** Wrapper for window manager type functions. */
    X11WMType windowManagerType(bool fIsXServerAvailable);
    /** X11: Determines and returns current Window Manager type. */
    X11WMType X11WindowManagerType();
    /** Wayland: Determines and returns current Window Manager type. */
    X11WMType WaylandWindowManagerType();

    /** Wrapper for X11CheckExtension and WaylandCheckExtension functions. */
    bool checkExtension(bool fIsXServerAvailable, const char *extensionName);
    /** X11: Returns true if XLib extension with name @p extensionName is available, false otherwise. */
    bool X11CheckExtension(const char *pExtensionName);
    bool WaylandCheckExtension(const char *pExtensionName);


    /** DBus: Returns whether there are any DBus services whose name contains the substring 'screensaver'. */
    bool checkDBusScreenSaverServices();
    /** DBus: Returns the list of Inhibit methods found by introspecting DBus services. */
    SHARED_LIBRARY_STUFF QVector<DBusScreenSaverInhibitMethod*> findDBusScrenSaverInhibitMethods();
    /** DBus: Disables/enables Screen Saver through QDBus. */
    SHARED_LIBRARY_STUFF void toggleHostScrenSaver(bool fInhibit, QVector<DBusScreenSaverInhibitMethod*> &inOutInhibitMethods);

    /** Wrapper function for X11ActivateWindow or WaylandActivateWindow. */
    bool activateWindow(bool fIsXServerAvailable, WId wId, bool fSwitchDesktop);
    /** Activates window with certain @a wId, @a fSwitchDesktop if requested. */
    bool X11ActivateWindow(WId wId, bool fSwitchDesktop);
    /** Activates window with certain @a wId, @a fSwitchDesktop if requested. */
    bool WaylandActivateWindow(WId wId, bool fSwitchDesktop);

    /** X11: Test whether the current window manager supports full screen mode. */
    SHARED_LIBRARY_STUFF bool X11SupportsFullScreenMonitorsProtocol();
    /** X11: Performs mapping of the passed @a pWidget to host-screen with passed @a uScreenId. */
    SHARED_LIBRARY_STUFF bool X11SetFullScreenMonitor(QWidget *pWidget, ulong uScreenId);

    /** X11: Sets _NET_WM_STATE_SKIP_TASKBAR flag for passed @a pWidget. */
    SHARED_LIBRARY_STUFF void X11SetSkipTaskBarFlag(QWidget *pWidget);
    /** X11: Sets _NET_WM_STATE_SKIP_PAGER flag for passed @a pWidget. */
    SHARED_LIBRARY_STUFF void X11SetSkipPagerFlag(QWidget *pWidget);

    /** Wrapper function for WMClass setters. */
    SHARED_LIBRARY_STUFF void setWMClass(bool fIsXServerAvailable, QWidget *pWidget, const QString &strNameString, const QString &strClassString);
    /** X11: Assigns WM_CLASS property for passed @a pWidget. */
    SHARED_LIBRARY_STUFF void X11SetWMClass(QWidget *pWidget, const QString &strNameString, const QString &strClassString);
    /** Wayland: Assigns WM_CLASS property for passed @a pWidget. */
    SHARED_LIBRARY_STUFF void WaylandSetWMClass(QWidget *pWidget, const QString &strNameString, const QString &strClassString);

    /** X11: Tell the WM we are well behaved wrt Xwayland keyboard-grabs. This will
      *      make the WM turn our grab into a Wayland shortcut inhibition request,
      *      so that e.g. alt+tab will get send to the VM instead of moving the
      *      focus away from the VM. */
    SHARED_LIBRARY_STUFF void setXwaylandMayGrabKeyboardFlag(bool fIsXServerAvailable, QWidget *pWidget);

    /** X11: Gets the X11 display pointer. */
    SHARED_LIBRARY_STUFF struct _XDisplay *X11GetDisplay();
    /** X11: Gets the X11 connection. */
    SHARED_LIBRARY_STUFF struct xcb_connection_t *X11GetConnection();
    /** X11: Gets the X11 root (desktop) window. */
    SHARED_LIBRARY_STUFF uint32_t X11GetAppRootWindow();
}

#endif /* !FEQT_INCLUDED_SRC_platform_nix_VBoxUtils_nix_h */
