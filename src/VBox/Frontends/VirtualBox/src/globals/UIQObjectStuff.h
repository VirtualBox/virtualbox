/* $Id: UIQObjectStuff.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIQObjectStuff class declaration.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIQObjectStuff_h
#define FEQT_INCLUDED_SRC_globals_UIQObjectStuff_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QPointer>
#include <QVariant>

/* GUI includes: */
#include "UILibraryDefs.h"

/** Guard block which sets/clears QObject property on RAII basis. */
class SHARED_LIBRARY_STUFF UIQObjectPropertySetter : public QObject
{
    Q_OBJECT;

signals:

    /** Notify listeners that we are about to be destroyed.
      * @note  This signal is emitted one call-stack frame earlier
      *        than QObject::destroyed(QObject *obj = Q_NULLPTR),
      *        so we still can use info stored in this object. */
    void sigAboutToBeDestroyed();

public:

    /** Constructs guard block which sets for @a pObject a property with certain @a strPropertyName and @a value. */
    UIQObjectPropertySetter(QObject *pObject, const QString &strPropertyName, const QVariant &value);
    /** Constructs guard block which sets for @a objects a property with certain @a strPropertyName and @a value. */
    UIQObjectPropertySetter(const QList<QObject*> &objects, const QString &strPropertyName, const QVariant &value);

    /** Destructs guard block clearing previously set property for good. */
    virtual ~UIQObjectPropertySetter() RT_OVERRIDE;

private:

    /** Inits properties. */
    void init();
    /** Deinits properties. */
    void deinit();

    /** Holds the list of live QObject pointers. */
    QList<QPointer<QObject> >  m_objects;
    /** Holds the property name. */
    QString                    m_strPropertyName;
    /** Holds the property value. */
    QVariant                   m_value;
};

#endif /* !FEQT_INCLUDED_SRC_globals_UIQObjectStuff_h */
