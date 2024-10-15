/* $Id: UIMachineWindowFullscreen.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindowFullscreen class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineWindowFullscreen_h
#define FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineWindowFullscreen_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineWindow.h"

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
/* Forward declarations: */
class UIMiniToolBar;
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

/** UIMachineWindow subclass used as full-screen machine window implementation. */
class UIMachineWindowFullscreen : public UIMachineWindow
{
    Q_OBJECT;

#ifdef RT_OS_DARWIN
signals:
    /** Mac OS X: Notifies listener about native 'fullscreen' will be entered. */
    void sigNotifyAboutNativeFullscreenWillEnter();
    /** Mac OS X: Notifies listener about native 'fullscreen' entered. */
    void sigNotifyAboutNativeFullscreenDidEnter();
    /** Mac OS X: Notifies listener about native 'fullscreen' will be exited. */
    void sigNotifyAboutNativeFullscreenWillExit();
    /** Mac OS X: Notifies listener about native 'fullscreen' exited. */
    void sigNotifyAboutNativeFullscreenDidExit();
    /** Mac OS X: Notifies listener about native 'fullscreen' fail to enter. */
    void sigNotifyAboutNativeFullscreenFailToEnter();
#endif /* RT_OS_DARWIN */

public:

    /** Constructor, passes @a pMachineLogic and @a uScreenId to the UIMachineWindow constructor. */
    UIMachineWindowFullscreen(UIMachineLogic *pMachineLogic, ulong uScreenId);

protected:

#ifdef VBOX_WS_MAC
    /** Mac OS X: Handles native notifications @a strNativeNotificationName for 'fullscreen' window. */
    void handleNativeNotification(const QString &strNativeNotificationName)  RT_OVERRIDE RT_FINAL;
    /** Mac OS X: Returns whether window is in 'fullscreen' transition. */
    bool isInFullscreenTransition() const { return m_fIsInFullscreenTransition; }
#endif /* VBOX_WS_MAC */

private slots:

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /** Handles machine state change event. */
    void sltMachineStateChanged() RT_OVERRIDE RT_FINAL;

    /** Revokes window activation. */
    void sltRevokeWindowActivation();

    /** Handles signal about mini-toolbar auto-hide toggled.
      * @param  fEnabled  Brings whether auto-hide is enabled. */
    void sltHandleMiniToolBarAutoHideToggled(bool fEnabled);
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#ifdef RT_OS_DARWIN
    /** Mac OS X: Commands @a pMachineWindow to enter native 'fullscreen' mode if possible. */
    void sltEnterNativeFullscreen(UIMachineWindow *pMachineWindow);
    /** Mac OS X: Commands @a pMachineWindow to exit native 'fullscreen' mode if possible. */
    void sltExitNativeFullscreen(UIMachineWindow *pMachineWindow);
#endif /* RT_OS_DARWIN */

    /** Shows window in minimized state. */
    void sltShowMinimized();

private:

    /** Prepare notification-center routine. */
    void prepareNotificationCenter()  RT_OVERRIDE RT_FINAL;
    /** Prepare visual-state routine. */
    void prepareVisualState() RT_OVERRIDE RT_FINAL;
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /** Prepare mini-toolbar routine. */
    void prepareMiniToolbar();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /** Cleanup mini-toolbar routine. */
    void cleanupMiniToolbar();
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
    /** Cleanup visual-state routine. */
    void cleanupVisualState() RT_OVERRIDE RT_FINAL;
    /** Cleanup notification-center routine. */
    void cleanupNotificationCenter() RT_OVERRIDE RT_FINAL;

    /** Updates geometry according to visual-state. */
    void placeOnScreen();
    /** Updates visibility according to visual-state. */
    void showInNecessaryMode() RT_OVERRIDE RT_FINAL;

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /** Common update routine. */
    void updateAppearanceOf(int iElement) RT_OVERRIDE RT_FINAL;
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#ifdef VBOX_WS_NIX
    /** X11: Handles @a pEvent about state change. */
    void changeEvent(QEvent *pEvent) RT_OVERRIDE RT_FINAL;
#endif

#ifdef VBOX_WS_WIN
    /** Win: Handles show @a pEvent. */
    void showEvent(QShowEvent *pEvent);
#endif

#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    /** Holds the mini-toolbar instance. */
    UIMiniToolBar *m_pMiniToolBar;
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */

#ifdef VBOX_WS_MAC
    /** Mac OS X: Reflects whether window is in 'fullscreen' transition. */
    bool m_fIsInFullscreenTransition;
    /** Mac OS X: Allows 'fullscreen' API access: */
    friend class UIMachineLogicFullscreen;
#endif /* VBOX_WS_MAC */

    /** Holds whether the window was minimized before became hidden.
      * Used to restore minimized state when the window shown again. */
    bool m_fWasMinimized;
#ifdef VBOX_WS_NIX
    /** X11: Holds whether the window minimization is currently requested.
      * Used to prevent accidentally restoring to full-screen state. */
    bool m_fIsMinimizationRequested;
    /** X11: Holds whether the window is currently minimized.
      * Used to restore full-screen state when the window restored again. */
    bool m_fIsMinimized;
#endif
};

#endif /* !FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineWindowFullscreen_h */
