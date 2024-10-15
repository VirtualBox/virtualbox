/* $Id: UICloudEntityKey.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudEntityKey class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UICloudEntityKey_h
#define FEQT_INCLUDED_SRC_manager_UICloudEntityKey_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QHash>
#include <QString>
#include <QUuid>

/** Cloud entity key definition.
  * This is a key for various indexed containers,
  * allowing to distinguish one cloud entity from another. */
struct UICloudEntityKey
{
    /** Constructs cloud entity key on the basis of passed parameters.
      * @param  strProviderShortName  Brings provider short name.
      * @param  strProfileName        Brings profile name.
      * @param  uMachineId            Brings machine id. */
    UICloudEntityKey(const QString &strProviderShortName = QString(),
                     const QString &strProfileName = QString(),
                     const QUuid &uMachineId = QUuid());
    /** Constructs cloud entity key on the basis of @a another one. */
    UICloudEntityKey(const UICloudEntityKey &another);

    /** Returns whether this one key equals to @a another one. */
    bool operator==(const UICloudEntityKey &another) const;
    /** Returns whether this one key is less than @a another one. */
    bool operator<(const UICloudEntityKey &another) const;

    /** Returns string key representation. */
    QString toString() const;

    /** Holds provider short name. */
    QString m_strProviderShortName;
    /** Holds profile name. */
    QString m_strProfileName;
    /** Holds machine id. */
    QUuid m_uMachineId;
};

inline size_t qHash(const UICloudEntityKey &key, size_t uSeed)
{
    return qHash(key.toString(), uSeed);
}

#endif /* !FEQT_INCLUDED_SRC_manager_UICloudEntityKey_h */
