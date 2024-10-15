/* $Id: UIQObjectStuff.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIQObjectStuff class implementation.
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

/* GUI includes: */
#include "UIQObjectStuff.h"


UIQObjectPropertySetter::UIQObjectPropertySetter(QObject *pObject, const QString &strPropertyName, const QVariant &value)
    : m_strPropertyName(strPropertyName)
    , m_value(value)
{
    /* Add object into list: */
    m_objects << pObject;
    /* Init properties: */
    init();
}

UIQObjectPropertySetter::UIQObjectPropertySetter(const QList<QObject*> &objects, const QString &strPropertyName, const QVariant &value)
    : m_strPropertyName(strPropertyName)
    , m_value(value)
{
    /* Add objects into list: */
    foreach (QObject *pObject, objects)
        m_objects << pObject;
    /* Init properties: */
    init();
}

UIQObjectPropertySetter::~UIQObjectPropertySetter()
{
    /* Deinit properties: */
    deinit();
    /* Notify listeners that we are done: */
    emit sigAboutToBeDestroyed();
}

void UIQObjectPropertySetter::init()
{
    foreach (const QPointer<QObject> &pObject, m_objects)
    {
        if (pObject)
        {
            pObject->setProperty(m_strPropertyName.toLatin1().constData(), m_value);
            //printf("UIQObjectPropertySetter::UIQObjectPropertySetter: Property {%s} set.\n",
            //       m_strPropertyName.toLatin1().constData());
        }
    }
}

void UIQObjectPropertySetter::deinit()
{
    foreach (const QPointer<QObject> &pObject, m_objects)
    {
        if (pObject)
        {
            pObject->setProperty(m_strPropertyName.toLatin1().constData(), QVariant());
            //printf("UIQObjectPropertySetter::~UIQObjectPropertySetter: Property {%s} cleared.\n",
            //       m_strPropertyName.toLatin1().constData());
        }
    }
}
