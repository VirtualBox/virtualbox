/* $Id: UICloudEntityKey.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudEntityKey class implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UICloudEntityKey.h"


UICloudEntityKey::UICloudEntityKey(const QString &strProviderShortName /* = QString() */,
                                   const QString &strProfileName /* = QString() */,
                                   const QUuid &uMachineId /* = QUuid() */)
    : m_strProviderShortName(strProviderShortName)
    , m_strProfileName(strProfileName)
    , m_uMachineId(uMachineId)
{
}

UICloudEntityKey::UICloudEntityKey(const UICloudEntityKey &another)
    : m_strProviderShortName(another.m_strProviderShortName)
    , m_strProfileName(another.m_strProfileName)
    , m_uMachineId(another.m_uMachineId)
{
}

bool UICloudEntityKey::operator==(const UICloudEntityKey &another) const
{
    return    true
           && toString() == another.toString()
              ;
}

bool UICloudEntityKey::operator<(const UICloudEntityKey &another) const
{
    return    true
           && toString() < another.toString()
              ;
}

QString UICloudEntityKey::toString() const
{
    QString strResult;
    if (m_strProviderShortName.isEmpty())
        return strResult;
    strResult += QString("/%1").arg(m_strProviderShortName);
    if (m_strProfileName.isEmpty())
        return strResult;
    strResult += QString("/%1").arg(m_strProfileName);
    if (m_uMachineId.isNull())
        return strResult;
    strResult += QString("/%1").arg(m_uMachineId.toString());
    return strResult;
}
