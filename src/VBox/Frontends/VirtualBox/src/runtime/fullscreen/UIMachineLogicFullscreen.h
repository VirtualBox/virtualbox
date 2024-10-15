/* $Id: UIMachineLogicFullscreen.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogicFullscreen class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineLogicFullscreen_h
#define FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineLogicFullscreen_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineLogic.h"

/* Other includes: */
#ifdef VBOX_WS_MAC
# include <ApplicationServices/ApplicationServices.h>
#endif /* VBOX_WS_MAC */

/* Forward declarations: */
class UIMultiScreenLayout;

/** UIMachineLogic subclass used as full-screen machine logic implementation. */
class UIMachineLogicFullscreen : public UIMachineLogic
{
    Q_OBJECT;

#ifdef RT_OS_DARWIN
signals:

    /** Mac OS X: Notifies listeners about native fullscreen mode should be entered on @a pMachineWindow. */
    void sigNotifyAboutNativeFullscreenShouldBeEntered(UIMachineWindow *pMachineWindow = 0);
    /** Mac OS X: Notifies listeners about native fullscreen mode should be exited on @a pMachineWindow. */
    void sigNotifyAboutNativeFullscreenShouldBeExited(UIMachineWindow *pMachineWindow = 0);
#endif /* RT_OS_DARWIN */

public:

    /** Constructs a logic passing @a pMachine and @a pSession to the base-class.
      * @param  pMachine  Brings the machine this logic belongs to. */
    UIMachineLogicFullscreen(UIMachine *pMachine);
    /** Destructs the logic. */
    virtual ~UIMachineLogicFullscreen() RT_OVERRIDE;

    /** Returns visual state type. */
    virtual UIVisualStateType visualStateType() const RT_OVERRIDE { return UIVisualStateType_Fullscreen; }

    /** Returns an index of host-screen for guest-screen with @a iScreenId specified. */
    int hostScreenForGuestScreen(int iScreenId) const;
    /** Returns whether there is a host-screen for guest-screen with @a iScreenId specified. */
    bool hasHostScreenForGuestScreen(int iScreenId) const;

#ifdef VBOX_WS_MAC
    /** Returns whether screens have separate spaces. */
    bool screensHaveSeparateSpaces() const { return m_fScreensHaveSeparateSpaces; }
#endif /* VBOX_WS_MAC */

protected:

    /* Check if this logic is available: */
    bool checkAvailability() RT_OVERRIDE;

    /** Returns machine-window flags for 'Fullscreen' machine-logic and passed @a uScreenId. */
    virtual Qt::WindowFlags windowFlags(ulong uScreenId) const RT_OVERRIDE;

    /** Adjusts machine-window geometry if necessary for 'Fullscreen'. */
    virtual void adjustMachineWindowsGeometry() RT_OVERRIDE;

private slots:

#ifdef RT_OS_DARWIN
    /** Mac OS X: Handles native notification about 'fullscreen' will be entered. */
    void sltHandleNativeFullscreenWillEnter();
    /** Mac OS X: Handles native notification about 'fullscreen' entered. */
    void sltHandleNativeFullscreenDidEnter();
    /** Mac OS X: Handles native notification about 'fullscreen' will be exited. */
    void sltHandleNativeFullscreenWillExit();
    /** Mac OS X: Handles native notification about 'fullscreen' exited. */
    void sltHandleNativeFullscreenDidExit();
    /** Mac OS X: Handles native notification about 'fullscreen' fail to enter. */
    void sltHandleNativeFullscreenFailToEnter();

    /** Mac OS X: Requests visual-state change from 'fullscreen' to 'normal' (window). */
    void sltChangeVisualStateToNormal() RT_OVERRIDE;
    /** Mac OS X: Requests visual-state change from 'fullscreen' to 'seamless'. */
    void sltChangeVisualStateToSeamless() RT_OVERRIDE;
    /** Mac OS X: Requests visual-state change from 'fullscreen' to 'scale'. */
    void sltChangeVisualStateToScale() RT_OVERRIDE;

    /** Mac OS X: Checks if some visual-state type was requested. */
    void sltCheckForRequestedVisualStateType() RT_OVERRIDE;
#endif /* RT_OS_DARWIN */

    /* Handler: Console callback stuff: */
    void sltMachineStateChanged() RT_OVERRIDE;

    /** Invokes popup-menu. */
    void sltInvokePopupMenu();

    /** Updates machine-window(s) location/size on screen-layout changes. */
    void sltScreenLayoutChanged();

    /** Handles guest-screen count change. */
    virtual void sltGuestMonitorChange(KGuestMonitorChangedEventType changeType, ulong uScreenId, QRect screenGeo) RT_OVERRIDE;
    /** Handles host-screen count change. */
    virtual void sltHostScreenCountChange() RT_OVERRIDE;
    /** Handles host-screen available-area change. */
    virtual void sltHostScreenAvailableAreaChange() RT_OVERRIDE;
    /** Handles additions-state change. */
    virtual void sltAdditionsStateChanged() RT_OVERRIDE;

private:

    /* Prepare helpers: */
    void prepareActionGroups() RT_OVERRIDE;
    void prepareActionConnections() RT_OVERRIDE;
    void prepareMachineWindows() RT_OVERRIDE;
    void prepareMenu() RT_OVERRIDE;

    /* Cleanup helpers: */
    void cleanupMenu() RT_OVERRIDE;
    void cleanupMachineWindows() RT_OVERRIDE;
    void cleanupActionConnections() RT_OVERRIDE;
    void cleanupActionGroups() RT_OVERRIDE;

#ifdef VBOX_WS_MAC
    /** Mac OS X: Revalidates 'fullscreen' mode for @a pMachineWindow. */
    void revalidateNativeFullScreen(UIMachineWindow *pMachineWindow);
    /** Mac OS X: Revalidates 'fullscreen' mode for all windows. */
    void revalidateNativeFullScreen();

    /** Mac OS X: Proxies native notification about application activation. */
    static void nativeHandlerForApplicationActivation(QObject *pObject, const QMap<QString, QString> &userInfo);
    /** Mac OS X: Handles native notification about application activation. */
    void nativeHandlerForApplicationActivation(const QMap<QString, QString> &userInfo);

    /** Mac OS X: Proxies native notification about active space change. */
    static void nativeHandlerForActiveSpaceChange(QObject *pObject, const QMap<QString, QString> &userInfo);
    /** Mac OS X: Handles native notification about active space change. */
    void nativeHandlerForActiveSpaceChange(const QMap<QString, QString> &userInfo);
#endif /* VBOX_WS_MAC */

    /** Holds the popup-menu instance. */
    QMenu *m_pPopupMenu;

    /* Variables: */
    UIMultiScreenLayout *m_pScreenLayout;

#ifdef VBOX_WS_MAC
    /** Mac OS X: Holds whether screens have separate spaces. */
    const bool m_fScreensHaveSeparateSpaces;

    /** Mac OS X: Contains machine-window(s) marked as 'fullscreen'. */
    QSet<UIMachineWindow*> m_fullscreenMachineWindows;
    /** Mac OS X: Contains machine-window(s) marked as 'invalid fullscreen'. */
    QSet<UIMachineWindow*> m_invalidFullscreenMachineWindows;
#endif /* VBOX_WS_MAC */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineLogicFullscreen_h */
