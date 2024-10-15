/* $Id: UIMultiScreenLayout.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMultiScreenLayout class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIMultiScreenLayout_h
#define FEQT_INCLUDED_SRC_runtime_UIMultiScreenLayout_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QObject>

/* Forward declarations: */
class UIActionPool;
class UIMachine;
class UIMachineLogic;

/** QObject subclass containing a part of
  * machine-logic related to multi-screen layout handling.
  * Used in such modes as Full-screen and Seamless. */
class UIMultiScreenLayout : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about layout change. */
    void sigScreenLayoutChange();

public:

    /** Constructs multi-screen layout passing @a pMachineLogic to the base-class.
      * @param  pMachineLogic  Brings the parent machine-logic reference. */
    UIMultiScreenLayout(UIMachineLogic *pMachineLogic);

    /** Returns whether there is host-monitor suitable for guest-screen with @a iScreenId specified. */
    bool hasHostScreenForGuestScreen(int iScreenId) const;
    /** Returns a host-monitor suitable for guest-screen with @a iScreenId specified. */
    int hostScreenForGuestScreen(int iScreenId) const;

    /** Returns video memory requirements for currently cached multi-screen layout. */
    quint64 memoryRequirements() const;

    /** Updates multi-screen layout on the basis
      * of known host/guest screen count. */
    void update();
    /** Updates everything.
      * Recalculates host/guest screen count
      * and calls for update wrapper above. */
    void rebuild();

private slots:

    /** Handles screen layout change request.
      * @param  iRequestedGuestScreen  Brings the index of guest-screen which needs to be mapped to certain host-monitor.
      * @param  iRequestedHostMonitor  Brings the index of host-monitor which needs to be mapped to certain guest-screen.*/
    void sltHandleScreenLayoutChange(int iRequestedGuestScreen, int iRequestedHostMonitor);

private:

    /** Prepares everything. */
    void prepare();
    /** Prepares connections. */
    void prepareConnections();

    /** Returns parent machine-logic reference. */
    UIMachineLogic *machineLogic() const { return m_pMachineLogic; }
    /** Returns machine UI reference. */
    UIMachine *uimachine() const;
    /** Returns action-pool reference. */
    UIActionPool *actionPool() const;

    /** Recalculates host-monitor count. */
    void calculateHostMonitorCount();
    /** Recalculates guest-screen count. */
    void calculateGuestScreenCount();

    /** Calculates memory requirements for passed @a screenLayout. */
    quint64 memoryRequirements(const QMap<int, int> &screenLayout) const;

    /** Holds the machine-logic reference. */
    UIMachineLogic *m_pMachineLogic;

    /** Holds the number of guest-screens. */
    ulong  m_cGuestScreens;
    /** Holds the number of host-monitors. */
    int    m_cHostMonitors;

    /** Holds currently cached enabled guest-screens. */
    QList<int>  m_guestScreens;
    /** Holds currently cached disabled guest-screens. */
    QList<int>  m_disabledGuestScreens;

    /** Holds the cached screens map.
      * The keys used for guest-screens.
      * The values used for host-monitors. */
    QMap<int, int> m_screenMap;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIMultiScreenLayout_h */

