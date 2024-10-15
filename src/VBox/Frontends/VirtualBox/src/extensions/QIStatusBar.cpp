/* $Id: QIStatusBar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIStatusBar class implementation.
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

/* Qt includes: */
#include <QAccessibleWidget>

/* GUI includes: */
#include "QIStatusBar.h"


/** QAccessibleWidget extension used as an accessibility interface for QIStatusBar. */
class QIAccessibilityInterfaceForQIStatusBar : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QIStatusBar accessibility interface: */
        if (pObject && strClassname == QLatin1String("QIStatusBar"))
            return new QIAccessibilityInterfaceForQIStatusBar(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    QIAccessibilityInterfaceForQIStatusBar(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::ToolBar)
    {
        // We are not interested in status-bar text as it's a mean of
        // accessibility in case when accessibility is disabled.
        // Since accessibility is enabled in our case, we wish
        // to pass control token to our sub-elements.
        // So we are using QAccessible::ToolBar.
    }
};


/*********************************************************************************************************************************
*   Class QIStatusBar implementation.                                                                                            *
*********************************************************************************************************************************/

QIStatusBar::QIStatusBar(QWidget *pParent)
    : QStatusBar(pParent)
{
    /* Install QIStatusBar accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQIStatusBar::pFactory);

    /* Make sure we remember the last one status message: */
    connect(this, &QIStatusBar::messageChanged,
            this, &QIStatusBar::sltRememberLastMessage);

    /* Remove that ugly border around the status-bar items on every platform: */
    setStyleSheet("QStatusBar::item { border: 0px none black; }");
}
