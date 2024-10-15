/* $Id: UIMachineViewFullscreen.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineViewFullscreen class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineViewFullscreen_h
#define FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineViewFullscreen_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineView.h"

/** UIMachineView subclass used as full-screen machine view implementation. */
class UIMachineViewFullscreen : public UIMachineView
{
    Q_OBJECT;

public:

    /* Fullscreen machine-view constructor: */
    UIMachineViewFullscreen(UIMachineWindow *pMachineWindow, ulong uScreenId);
    /* Fullscreen machine-view destructor: */
    virtual ~UIMachineViewFullscreen() {}

private slots:

    /* Handler: Console callback stuff: */
    void sltAdditionsStateChanged();

private:

    /* Event handlers: */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;

    /* Prepare routines: */
    void prepareCommon() RT_OVERRIDE;
    void prepareFilters() RT_OVERRIDE;
    void prepareConsoleConnections() RT_OVERRIDE;

    /* Cleanup routines: */
    //void cleanupConsoleConnections() {}
    //void cleanupFilters() {}
    //void cleanupCommon() {}

    /** Returns whether the guest-screen auto-resize is enabled. */
    virtual bool isGuestAutoresizeEnabled() const RT_OVERRIDE { return m_fGuestAutoresizeEnabled; }
    /** Defines whether the guest-screen auto-resize is @a fEnabled. */
    virtual void setGuestAutoresizeEnabled(bool bEnabled) RT_OVERRIDE;

    /** Adjusts guest-screen size to correspond current <i>working area</i> size. */
    void adjustGuestScreenSize() RT_OVERRIDE;

    /* Helpers: Geometry stuff: */
    QRect workingArea() const RT_OVERRIDE;
    QSize calculateMaxGuestSize() const RT_OVERRIDE;

    /* Private variables: */
    bool m_fGuestAutoresizeEnabled : 1;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_fullscreen_UIMachineViewFullscreen_h */
