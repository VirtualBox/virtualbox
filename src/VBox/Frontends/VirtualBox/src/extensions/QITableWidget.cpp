/* $Id: QITableWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QITableWidget class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QPainter>
#include <QResizeEvent>

/* GUI includes: */
#include "QITableWidget.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QAccessibleObject extension used as an accessibility interface for QITableWidgetItem. */
class QIAccessibilityInterfaceForQITableWidgetItem : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QITableWidgetItem accessibility interface: */
        if (pObject && strClassname == QLatin1String("QITableWidgetItem"))
            return new QIAccessibilityInterfaceForQITableWidgetItem(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    QIAccessibilityInterfaceForQITableWidgetItem(QObject *pObject)
        : QAccessibleObject(pObject)
    {}

    /** Returns the parent. */
    virtual QAccessibleInterface *parent() const RT_OVERRIDE;

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE { return 0; }
    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE { Q_UNUSED(iIndex); return 0; }
    /** Returns the index of the passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE { Q_UNUSED(pChild); return -1; }

    /** Returns the rect. */
    virtual QRect rect() const RT_OVERRIDE;
    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE;

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE;
    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE;

private:

    /** Returns corresponding QITableWidgetItem. */
    QITableWidgetItem *item() const { return qobject_cast<QITableWidgetItem*>(object()); }
};


/** QAccessibleWidget extension used as an accessibility interface for QITableWidget. */
class QIAccessibilityInterfaceForQITableWidget : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QITableWidget accessibility interface: */
        if (pObject && strClassname == QLatin1String("QITableWidget"))
            return new QIAccessibilityInterfaceForQITableWidget(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    QIAccessibilityInterfaceForQITableWidget(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::List)
    {}

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE;
    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE;
    /** Returns the index of the passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE;

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE;

private:

    /** Returns corresponding QITableWidget. */
    QITableWidget *table() const { return qobject_cast<QITableWidget*>(widget()); }
};


/*********************************************************************************************************************************
*   Class QIAccessibilityInterfaceForQITableWidgetItem implementation.                                                           *
*********************************************************************************************************************************/

QAccessibleInterface *QIAccessibilityInterfaceForQITableWidgetItem::parent() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);

    /* Return the parent: */
    return QAccessible::queryAccessibleInterface(item()->parentTable());
}

QRect QIAccessibilityInterfaceForQITableWidgetItem::rect() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QRect());

    /* Compose common region: */
    QRegion region;

    /* Append item rectangle: */
    const QRect  itemRectInViewport = item()->parentTable()->visualItemRect(item());
    const QSize  itemSize           = itemRectInViewport.size();
    const QPoint itemPosInViewport  = itemRectInViewport.topLeft();
    const QPoint itemPosInScreen    = item()->parentTable()->viewport()->mapToGlobal(itemPosInViewport);
    const QRect  itemRectInScreen   = QRect(itemPosInScreen, itemSize);
    region += itemRectInScreen;

    /* Return common region bounding rectangle: */
    return region.boundingRect();
}

QString QIAccessibilityInterfaceForQITableWidgetItem::text(QAccessible::Text enmTextRole) const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QString());

    /* Return a text for the passed enmTextRole: */
    switch (enmTextRole)
    {
        case QAccessible::Name: return item()->text();
        default: break;
    }

    /* Null-string by default: */
    return QString();
}

QAccessible::Role QIAccessibilityInterfaceForQITableWidgetItem::role() const
{
    return QAccessible::ListItem;
}

QAccessible::State QIAccessibilityInterfaceForQITableWidgetItem::state() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QAccessible::State());

    /* Compose the state: */
    QAccessible::State state;
    state.focusable = true;
    state.selectable = true;

    /* Compose the state of current item: */
    if (   item()
        && item() == QITableWidgetItem::toItem(item()->tableWidget()->currentItem()))
    {
        state.active = true;
        state.focused = true;
        state.selected = true;
    }

    /* Compose the state of checked item: */
    if (   item()
        && item()->checkState() != Qt::Unchecked)
    {
        state.checked = true;
        if (item()->checkState() == Qt::PartiallyChecked)
            state.checkStateMixed = true;
    }

    /* Return the state: */
    return state;
}


/*********************************************************************************************************************************
*   Class QIAccessibilityInterfaceForQITableWidget implementation.                                                               *
*********************************************************************************************************************************/

int QIAccessibilityInterfaceForQITableWidget::childCount() const
{
    /* Make sure table still alive: */
    AssertPtrReturn(table(), 0);

    /* Return the number of children: */
    // Since Qt6 both horizontal and vertical table headers
    // are treated as items as well, so we have to take them
    // into account while calculating overall child count.
    return (table()->rowCount() + 1) * (table()->columnCount() + 1);
}

QAccessibleInterface *QIAccessibilityInterfaceForQITableWidget::child(int iIndex) const
{
    /* Make sure table still alive: */
    AssertPtrReturn(table(), 0);
    /* Make sure index is valid: */
    AssertReturn(iIndex >= 0 && iIndex < childCount(), 0);

    /* Return the child with the passed iIndex: */
    // Since Qt6 both horizontal and vertical table headers
    // are treated as items as well, so we have to take them
    // into account while addressing actual table children.
    const int iRow = iIndex / (table()->columnCount() + 1) - 1;
    const int iColumn = iIndex % (table()->columnCount() + 1) - 1;
    return QAccessible::queryAccessibleInterface(table()->childItem(iRow, iColumn));
}

int QIAccessibilityInterfaceForQITableWidget::indexOfChild(const QAccessibleInterface *pChild) const
{
    /* Search for corresponding child: */
    for (int iIndex = 0; iIndex < childCount(); ++iIndex)
        if (child(iIndex) == pChild)
            return iIndex;

    /* -1 by default: */
    return -1;
}

QString QIAccessibilityInterfaceForQITableWidget::text(QAccessible::Text /* enmTextRole */) const
{
    /* Make sure table still alive: */
    AssertPtrReturn(table(), QString());

    /* Gather suitable text: */
    QString strText = table()->toolTip();
    if (strText.isEmpty())
        strText = table()->whatsThis();
    return strText;
}


/*********************************************************************************************************************************
*   Class QITableWidgetItem implementation.                                                                                      *
*********************************************************************************************************************************/

/* static */
QITableWidgetItem *QITableWidgetItem::toItem(QTableWidgetItem *pItem)
{
    /* Make sure alive QITableWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Return casted QITableWidgetItem: */
    return static_cast<QITableWidgetItem*>(pItem);
}

/* static */
const QITableWidgetItem *QITableWidgetItem::toItem(const QTableWidgetItem *pItem)
{
    /* Make sure alive QITableWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Return casted QITableWidgetItem: */
    return static_cast<const QITableWidgetItem*>(pItem);
}

QITableWidgetItem::QITableWidgetItem(const QString &strText /* = QString() */)
    : QTableWidgetItem(strText, ItemType)
{
}

QITableWidget *QITableWidgetItem::parentTable() const
{
    return tableWidget() ? qobject_cast<QITableWidget*>(tableWidget()) : 0;
}


/*********************************************************************************************************************************
*   Class QITableWidget implementation.                                                                                          *
*********************************************************************************************************************************/

QITableWidget::QITableWidget(QWidget *pParent)
    : QTableWidget(pParent)
{
    /* Install QITableWidget accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQITableWidget::pFactory);
    /* Install QITableWidgetItem accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQITableWidgetItem::pFactory);

    // WORKAROUND:
    // Ok, what do we have here..
    // There is a bug in QAccessible framework which might be just treated like
    // a functionality flaw. It consist in fact that if an accessibility client
    // is enabled, base-class can request an accessibility interface in own
    // constructor before the sub-class registers own factory, so we have to
    // recreate interface after we finished with our own initialization.
    QAccessibleInterface *pInterface = QAccessible::queryAccessibleInterface(this);
    if (pInterface)
    {
        QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(pInterface));
        QAccessible::queryAccessibleInterface(this); // <= new one, proper..
    }
}

QITableWidgetItem *QITableWidget::childItem(int iRow, int iColumn) const
{
    return item(iRow, iColumn) ? QITableWidgetItem::toItem(item(iRow, iColumn)) : 0;
}

QModelIndex QITableWidget::itemIndex(QTableWidgetItem *pItem)
{
    return indexFromItem(pItem);
}

void QITableWidget::paintEvent(QPaintEvent *pEvent)
{
    /* Call to base-class: */
    QTableWidget::paintEvent(pEvent);

    /* Create item painter: */
    QPainter painter;
    painter.begin(viewport());

    /* Notify listeners about painting: */
    for (int iRow = 0; iRow < rowCount(); ++iRow)
        for (int iColumn = 0; iColumn < rowCount(); ++iColumn)
            emit painted(item(iRow, iColumn), &painter);

    /* Close item painter: */
    painter.end();
}

void QITableWidget::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QTableWidget::resizeEvent(pEvent);

    /* Notify listeners about resizing: */
    emit resized(pEvent->size(), pEvent->oldSize());
}
