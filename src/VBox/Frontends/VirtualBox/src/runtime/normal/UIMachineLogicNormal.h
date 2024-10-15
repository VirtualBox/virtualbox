/* $Id: UIMachineLogicNormal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogicNormal class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_normal_UIMachineLogicNormal_h
#define FEQT_INCLUDED_SRC_runtime_normal_UIMachineLogicNormal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineLogic.h"

/** UIMachineLogic subclass used as normal machine logic implementation. */
class UIMachineLogicNormal : public UIMachineLogic
{
    Q_OBJECT;

public:

    /** Constructs a logic passing @a pMachine and @a pSession to the base-class.
      * @param  pMachine  Brings the machine this logic belongs to. */
    UIMachineLogicNormal(UIMachine *pMachine);

    /** Returns visual state type. */
    virtual UIVisualStateType visualStateType() const RT_OVERRIDE { return UIVisualStateType_Normal; }

protected:

    /* Check if this logic is available: */
    bool checkAvailability() RT_OVERRIDE;

    /** Returns machine-window flags for 'Normal' machine-logic and passed @a uScreenId. */
    virtual Qt::WindowFlags windowFlags(ulong uScreenId) const RT_OVERRIDE { Q_UNUSED(uScreenId); return Qt::Window; }

private slots:

    /** Checks if some visual-state type was requested. */
    void sltCheckForRequestedVisualStateType() RT_OVERRIDE;

#ifndef RT_OS_DARWIN
    /** Invokes popup-menu. */
    void sltInvokePopupMenu();
#endif /* RT_OS_DARWIN */

    /** Opens menu-bar editor.*/
    void sltOpenMenuBarSettings();
    /** Handles menu-bar editor closing.*/
    void sltMenuBarSettingsClosed();
#ifndef RT_OS_DARWIN
    /** Toggles menu-bar presence.*/
    void sltToggleMenuBar();
#endif /* !RT_OS_DARWIN */

    /** Opens status-bar editor.*/
    void sltOpenStatusBarSettings();
    /** Handles status-bar editor closing.*/
    void sltStatusBarSettingsClosed();
    /** Toggles status-bar presence.*/
    void sltToggleStatusBar();

    /** Handles host-screen available-area change. */
    virtual void sltHostScreenAvailableAreaChange() RT_OVERRIDE;

private:

    /* Prepare helpers: */
    virtual void prepareActionGroups() RT_OVERRIDE;
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

#ifndef VBOX_WS_MAC
    /** Holds the popup-menu instance. */
    QMenu *m_pPopupMenu;
#endif /* !VBOX_WS_MAC */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_normal_UIMachineLogicNormal_h */
