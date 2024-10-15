/* $Id: VBoxUtils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility classes and functions.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_VBoxUtils_h
#define FEQT_INCLUDED_SRC_globals_VBoxUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMouseEvent>
#include <QWidget>
#include <QTextBrowser>

/* GUI includes: */
#include "UILibraryDefs.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif


/** QObject subclass,
  * allowing to apply string-property value for a certain QObject. */
class SHARED_LIBRARY_STUFF QObjectPropertySetter : public QObject
{
    Q_OBJECT;

public:

    /** Constructs setter for a property with certain @a strName, passing @a pParent to the base-class. */
    QObjectPropertySetter(QObject *pParent, const QString &strName)
        : QObject(pParent), m_strName(strName)
    {}

public slots:

    /** Assigns string property @a strValue. */
    void sltAssignProperty(const QString &strValue)
    {
        parent()->setProperty(m_strName.toLatin1().constData(), strValue);
    }

private:

    /** Holds the property name. */
    const QString m_strName;
};


#endif /* !FEQT_INCLUDED_SRC_globals_VBoxUtils_h */

