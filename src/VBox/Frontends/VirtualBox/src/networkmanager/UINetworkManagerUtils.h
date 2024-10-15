/* $Id: UINetworkManagerUtils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkManagerUtils namespace declaration.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_networkmanager_UINetworkManagerUtils_h
#define FEQT_INCLUDED_SRC_networkmanager_UINetworkManagerUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QStringList>


/** Network Manager: Host network utilities. */
namespace UINetworkManagerUtils
{
    /** Converts IPv4 address from QString to quint32. */
    quint32 ipv4FromQStringToQuint32(const QString &strAddress);
    /** Converts IPv4 address from quint32 to QString. */
    QString ipv4FromQuint32ToQString(quint32 uAddress);

    /** Increments network @a uAddress by 1 avoiding 0/255 values. */
    quint32 incrementNetworkAddress(quint32 uAddress);
    /** Decrements network @a uAddress by 1 avoiding 0/255 values. */
    quint32 decrementNetworkAddress(quint32 uAddress);
    /** Advances network @a uAddress by 1 avoiding 0/255 values.
      * @param  fForward  Brings whether advance should
      *                   go forward or backward otherwise. */
    quint32 advanceNetworkAddress(quint32 uAddress, bool fForward);

    /** Calculates DHCP server proposal on the basis of the passed @a strInterfaceAddress and @a strInterfaceMask. */
    QStringList makeDhcpServerProposal(const QString &strInterfaceAddress, const QString &strInterfaceMask);
}

/* Using this namespace where included: */
using namespace UINetworkManagerUtils;

#endif /* !FEQT_INCLUDED_SRC_networkmanager_UINetworkManagerUtils_h */

