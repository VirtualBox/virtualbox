/* $Id: UIMachineViewNormal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineViewNormal class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_normal_UIMachineViewNormal_h
#define FEQT_INCLUDED_SRC_runtime_normal_UIMachineViewNormal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineView.h"

/** UIMachineView subclass used as normal machine view implementation. */
class UIMachineViewNormal : public UIMachineView
{
    Q_OBJECT;

public:

    /* Normal machine-view constructor: */
    UIMachineViewNormal(UIMachineWindow *pMachineWindow, ulong uScreenId);
    /* Normal machine-view destructor: */
    virtual ~UIMachineViewNormal() {}

private slots:

    /* Console callback handlers: */
    void sltAdditionsStateChanged();

private:

    /* Event handlers: */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;

    /* Prepare helpers: */
    void prepareCommon() RT_OVERRIDE;
    void prepareFilters() RT_OVERRIDE;
    void prepareConsoleConnections() RT_OVERRIDE;

    /* Cleanup helpers: */
    //void cleanupConsoleConnections() {}
    //void cleanupFilters() {}
    //void cleanupCommon() {}

    /** Returns whether the guest-screen auto-resize is enabled. */
    virtual bool isGuestAutoresizeEnabled() const RT_OVERRIDE { return m_fGuestAutoresizeEnabled; }
    /** Defines whether the guest-screen auto-resize is @a fEnabled. */
    virtual void setGuestAutoresizeEnabled(bool bEnabled) RT_OVERRIDE;

    /** Resends guest size-hint. */
    void resendSizeHint() RT_OVERRIDE;

    /** Adjusts guest-screen size to correspond current <i>machine-window</i> size. */
    void adjustGuestScreenSize() RT_OVERRIDE;

    /* Private helpers: */
    QSize sizeHint() const RT_OVERRIDE;
    QRect workingArea() const RT_OVERRIDE;
    QSize calculateMaxGuestSize() const RT_OVERRIDE;

    /* Private members: */
    bool m_fGuestAutoresizeEnabled : 1;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_normal_UIMachineViewNormal_h */
