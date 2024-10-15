/* $Id: UIMachineLogicSeamless.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogicSeamless class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_seamless_UIMachineLogicSeamless_h
#define FEQT_INCLUDED_SRC_runtime_seamless_UIMachineLogicSeamless_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineLogic.h"

/* Forward declarations: */
class UIMultiScreenLayout;

/** UIMachineLogic subclass used as seamless machine logic implementation. */
class UIMachineLogicSeamless : public UIMachineLogic
{
    Q_OBJECT;

public:

    /** Constructs a logic passing @a pMachine and @a pSession to the base-class.
      * @param  pMachine  Brings the machine this logic belongs to. */
    UIMachineLogicSeamless(UIMachine *pMachine);
    /** Destructs the logic. */
    virtual ~UIMachineLogicSeamless() RT_OVERRIDE;

    /** Returns visual state type. */
    virtual UIVisualStateType visualStateType() const RT_OVERRIDE { return UIVisualStateType_Seamless; }

    /** Returns an index of host-screen for guest-screen with @a iScreenId specified. */
    int hostScreenForGuestScreen(int iScreenId) const;
    /** Returns whether there is a host-screen for guest-screen with @a iScreenId specified. */
    bool hasHostScreenForGuestScreen(int iScreenId) const;

protected:

    /* Check if this logic is available: */
    bool checkAvailability() RT_OVERRIDE;

    /** Returns machine-window flags for 'Seamless' machine-logic and passed @a uScreenId. */
    virtual Qt::WindowFlags windowFlags(ulong uScreenId) const RT_OVERRIDE { Q_UNUSED(uScreenId); return Qt::FramelessWindowHint; }

    /** Adjusts machine-window geometry if necessary for 'Seamless'. */
    virtual void adjustMachineWindowsGeometry() RT_OVERRIDE;

private slots:

    /** Checks if some visual-state type was requested. */
    void sltCheckForRequestedVisualStateType() RT_OVERRIDE;

    /* Handler: Console callback stuff: */
    void sltMachineStateChanged() RT_OVERRIDE;

    /** Updates machine-window(s) location/size on screen-layout changes. */
    void sltScreenLayoutChanged();

    /** Handles guest-screen count change. */
    virtual void sltGuestMonitorChange(KGuestMonitorChangedEventType changeType, ulong uScreenId, QRect screenGeo) RT_OVERRIDE;
    /** Handles host-screen count change. */
    virtual void sltHostScreenCountChange() RT_OVERRIDE;
    /** Handles additions-state change. */
    virtual void sltAdditionsStateChanged() RT_OVERRIDE;

#ifndef RT_OS_DARWIN
    /** Invokes popup-menu. */
    void sltInvokePopupMenu();
#endif /* !RT_OS_DARWIN */

private:

    /* Prepare helpers: */
    void prepareActionGroups() RT_OVERRIDE;
    void prepareActionConnections() RT_OVERRIDE;
    void prepareMachineWindows() RT_OVERRIDE;
#ifndef VBOX_WS_MAC
    void prepareMenu() RT_OVERRIDE RT_FINAL;
#endif /* !VBOX_WS_MAC */

    /* Cleanup helpers: */
#ifndef VBOX_WS_MAC
    void cleanupMenu() RT_OVERRIDE RT_FINAL;
#endif /* !VBOX_WS_MAC */
    void cleanupMachineWindows() RT_OVERRIDE;
    void cleanupActionConnections() RT_OVERRIDE;
    void cleanupActionGroups() RT_OVERRIDE;

    /* Variables: */
    UIMultiScreenLayout *m_pScreenLayout;

#ifndef RT_OS_DARWIN
    /** Holds the popup-menu instance. */
    QMenu *m_pPopupMenu;
#endif /* !RT_OS_DARWIN */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_seamless_UIMachineLogicSeamless_h */
