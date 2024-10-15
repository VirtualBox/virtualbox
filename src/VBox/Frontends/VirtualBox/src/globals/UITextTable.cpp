/* $Id: UITextTable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITextTable class implementation.
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

/* Qt includes: */
#include <QAccessibleObject>
#include <QRegularExpression>

/* GUI includes: */
#include "UITextTable.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QAccessibleObject extension used as an accessibility interface for UITextTableLine. */
class UIAccessibilityInterfaceForUITextTableLine : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating UITextTableLine accessibility interface: */
        if (pObject && strClassname == QLatin1String("UITextTableLine"))
            return new UIAccessibilityInterfaceForUITextTableLine(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    UIAccessibilityInterfaceForUITextTableLine(QObject *pObject)
        : QAccessibleObject(pObject)
    {}

    /** Returns the parent. */
    virtual QAccessibleInterface *parent() const RT_OVERRIDE
    {
        /* Make sure item still alive: */
        AssertPtrReturn(line(), 0);

        /* Always return parent object: */
        return QAccessible::queryAccessibleInterface(line()->parent());
    }

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE { return 0; }
    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE { Q_UNUSED(iIndex); return 0; }
    /** Returns the index of the passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE { Q_UNUSED(pChild); return -1; }

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure line still alive: */
        AssertPtrReturn(line(), QString());

        /* Return the description: */
        if (enmTextRole == QAccessible::Description)
        {
            const QString str1 = line()->string1();
            QString str2 = line()->string2();
            if (!str2.isEmpty())
                str2.remove(QRegularExpression("<a[^>]*>|</a>"));
            return str2.isEmpty() ? str1 : QString("%1: %2").arg(str1, str2);
        }

        /* Null-string by default: */
        return QString();
    }

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE
    {
        /* Return the role: */
        return QAccessible::ListItem;
    }

    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE
    {
        /* Return the state: */
        return QAccessible::State();
    }

private:

    /** Returns corresponding UITextTableLine. */
    UITextTableLine *line() const { return qobject_cast<UITextTableLine*>(object()); }
};


/*********************************************************************************************************************************
*   Class UITextTableLine implementation.                                                                                        *
*********************************************************************************************************************************/

UITextTableLine::UITextTableLine(const QString &str1, const QString &str2, QObject *pParent /* = 0 */)
    : QObject(pParent)
    , m_str1(str1)
    , m_str2(str2)
{
    /* Install UITextTableLine accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUITextTableLine::pFactory);
}

UITextTableLine::UITextTableLine(const UITextTableLine &other)
    : QObject(other.parent())
    , m_str1(other.string1())
    , m_str2(other.string2())
{
    /* Install UITextTableLine accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUITextTableLine::pFactory);
}

UITextTableLine &UITextTableLine::operator=(const UITextTableLine &other)
{
    setParent(other.parent());
    set1(other.string1());
    set2(other.string2());
    return *this;
}

bool UITextTableLine::operator==(const UITextTableLine &other) const
{
    return    string1() == other.string1()
           && string2() == other.string2();
}
