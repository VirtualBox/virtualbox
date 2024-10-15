/* $Id: UITextTable.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITextTable class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UITextTable_h
#define FEQT_INCLUDED_SRC_globals_UITextTable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QObject extension used as an
  * accessible wrapper for QString pairs. */
class SHARED_LIBRARY_STUFF UITextTableLine : public QObject
{
    Q_OBJECT;

public:

    /** Constructs text-table line passing @a pParent to the base-class.
      * @param  str1  Brings the 1st table string.
      * @param  str2  Brings the 2nd table string. */
    UITextTableLine(const QString &str1, const QString &str2, QObject *pParent = 0);

    /** Constructs text-table line on the basis of passed @a other. */
    UITextTableLine(const UITextTableLine &other);

    /** Assigns @a other to this. */
    UITextTableLine &operator=(const UITextTableLine &other);

    /** Compares @a other to this. */
    bool operator==(const UITextTableLine &other) const;

    /** Defines 1st table @a strString. */
    void set1(const QString &strString) { m_str1 = strString; }
    /** Returns 1st table string. */
    const QString &string1() const { return m_str1; }

    /** Defines 2nd table @a strString. */
    void set2(const QString &strString) { m_str2 = strString; }
    /** Returns 2nd table string. */
    const QString &string2() const { return m_str2; }

private:

    /** Holds the 1st table string. */
    QString m_str1;
    /** Holds the 2nd table string. */
    QString m_str2;
};

/** Defines the list of UITextTableLine instances. */
typedef QList<UITextTableLine> UITextTable;
Q_DECLARE_METATYPE(UITextTable);

#endif /* !FEQT_INCLUDED_SRC_globals_UITextTable_h */
