/* $Id: UIMachineLogicScale.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineLogicScale class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_scale_UIMachineLogicScale_h
#define FEQT_INCLUDED_SRC_runtime_scale_UIMachineLogicScale_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineLogic.h"

/** UIMachineLogic subclass used as scaled machine logic implementation. */
class UIMachineLogicScale : public UIMachineLogic
{
    Q_OBJECT;

public:

    /** Constructs a logic passing @a pMachine and @a pSession to the base-class.
      * @param  pMachine  Brings the machine this logic belongs to. */
    UIMachineLogicScale(UIMachine *pMachine);

    /** Returns visual state type. */
    virtual UIVisualStateType visualStateType() const RT_OVERRIDE { return UIVisualStateType_Scale; }

protected:

    /* Check if this logic is available: */
    bool checkAvailability() RT_OVERRIDE;

    /** Returns machine-window flags for 'Scale' machine-logic and passed @a uScreenId. */
    virtual Qt::WindowFlags windowFlags(ulong uScreenId) const RT_OVERRIDE { Q_UNUSED(uScreenId); return Qt::Window; }

private slots:

#ifndef RT_OS_DARWIN
    /** Invokes popup-menu. */
    void sltInvokePopupMenu();
#endif /* !RT_OS_DARWIN */

    /** Handles host-screen available-area change. */
    virtual void sltHostScreenAvailableAreaChange() RT_OVERRIDE;

private:

    /* Prepare helpers: */
    void prepareActionGroups() RT_OVERRIDE;
    void prepareActionConnections() RT_OVERRIDE;
    void prepareMachineWindows() RT_OVERRIDE;
#ifndef RT_OS_DARWIN
    void prepareMenu() RT_OVERRIDE RT_FINAL;
#endif /* !RT_OS_DARWIN */

    /* Cleanup helpers: */
#ifndef RT_OS_DARWIN
    void cleanupMenu() RT_OVERRIDE RT_FINAL;
#endif /* !RT_OS_DARWIN */
    void cleanupMachineWindows() RT_OVERRIDE;
    void cleanupActionConnections() RT_OVERRIDE;
    void cleanupActionGroups() RT_OVERRIDE;

#ifndef RT_OS_DARWIN
    /** Holds the popup-menu instance. */
    QMenu *m_pPopupMenu;
#endif /* !RT_OS_DARWIN */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_scale_UIMachineLogicScale_h */
