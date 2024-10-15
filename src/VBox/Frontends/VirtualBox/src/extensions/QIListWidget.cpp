/* $Id: QIListWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIListWidget class implementation.
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
#include "QIListWidget.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QAccessibleObject extension used as an accessibility interface for QIListWidgetItem. */
class QIAccessibilityInterfaceForQIListWidgetItem : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QIListWidgetItem accessibility interface: */
        if (pObject && strClassname == QLatin1String("QIListWidgetItem"))
            return new QIAccessibilityInterfaceForQIListWidgetItem(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    QIAccessibilityInterfaceForQIListWidgetItem(QObject *pObject)
        : QAccessibleObject(pObject)
    {}

    /** Returns the parent. */
    virtual QAccessibleInterface *parent() const RT_OVERRIDE;

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE;
    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE;
    /** Returns the index of the passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE;

    /** Returns the rect. */
    virtual QRect rect() const RT_OVERRIDE;
    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE;

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE;
    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE;

private:

    /** Returns corresponding QIListWidgetItem. */
    QIListWidgetItem *item() const { return qobject_cast<QIListWidgetItem*>(object()); }
};


/** QAccessibleWidget extension used as an accessibility interface for QIListWidget. */
class QIAccessibilityInterfaceForQIListWidget : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QIListWidget accessibility interface: */
        if (pObject && strClassname == QLatin1String("QIListWidget"))
            return new QIAccessibilityInterfaceForQIListWidget(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    QIAccessibilityInterfaceForQIListWidget(QWidget *pWidget)
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

    /** Returns corresponding QIListWidget. */
    QIListWidget *list() const { return qobject_cast<QIListWidget*>(widget()); }
};


/*********************************************************************************************************************************
*   Class QIAccessibilityInterfaceForQIListWidgetItem implementation.                                                            *
*********************************************************************************************************************************/

QAccessibleInterface *QIAccessibilityInterfaceForQIListWidgetItem::parent() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);

    /* Return the parent: */
    return QAccessible::queryAccessibleInterface(item()->parentList());
}

int QIAccessibilityInterfaceForQIListWidgetItem::childCount() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);

    /* Zero in any case: */
    return 0;
}

QAccessibleInterface *QIAccessibilityInterfaceForQIListWidgetItem::child(int iIndex) const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);
    /* Make sure index is valid: */
    AssertReturn(iIndex >= 0 && iIndex < childCount(), 0);

    /* Null in any case: */
    return 0;
}

int QIAccessibilityInterfaceForQIListWidgetItem::indexOfChild(const QAccessibleInterface*) const
{
    /* -1 in any case: */
    return -1;
}

QRect QIAccessibilityInterfaceForQIListWidgetItem::rect() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QRect());

    /* Compose common region: */
    QRegion region;

    /* Append item rectangle: */
    const QRect  itemRectInViewport = item()->parentList()->visualItemRect(item());
    const QSize  itemSize           = itemRectInViewport.size();
    const QPoint itemPosInViewport  = itemRectInViewport.topLeft();
    const QPoint itemPosInScreen    = item()->parentList()->viewport()->mapToGlobal(itemPosInViewport);
    const QRect  itemRectInScreen   = QRect(itemPosInScreen, itemSize);
    region += itemRectInScreen;

    /* Return common region bounding rectangle: */
    return region.boundingRect();
}

QString QIAccessibilityInterfaceForQIListWidgetItem::text(QAccessible::Text enmTextRole) const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QString());

    /* Return a text for the passed enmTextRole: */
    switch (enmTextRole)
    {
        case QAccessible::Name: return item()->defaultText();
        default: break;
    }

    /* Null-string by default: */
    return QString();
}

QAccessible::Role QIAccessibilityInterfaceForQIListWidgetItem::role() const
{
    /* ListItem in any case: */
    return QAccessible::ListItem;
}

QAccessible::State QIAccessibilityInterfaceForQIListWidgetItem::state() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QAccessible::State());

    /* Compose the state: */
    QAccessible::State state;
    state.focusable = true;
    state.selectable = true;

    /* Compose the state of current item: */
    if (   item()
        && item() == QIListWidgetItem::toItem(item()->listWidget()->currentItem()))
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
*   Class QIAccessibilityInterfaceForQIListWidget implementation.                                                                *
*********************************************************************************************************************************/

int QIAccessibilityInterfaceForQIListWidget::childCount() const
{
    /* Make sure list still alive: */
    AssertPtrReturn(list(), 0);

    /* Return the number of children: */
    return list()->childCount();
}

QAccessibleInterface *QIAccessibilityInterfaceForQIListWidget::child(int iIndex) const
{
    /* Make sure list still alive: */
    AssertPtrReturn(list(), 0);
    /* Make sure index is valid: */
    AssertReturn(iIndex >= 0, 0);

    /* Return the child with the passed iIndex: */
    return QAccessible::queryAccessibleInterface(list()->childItem(iIndex));
}

int QIAccessibilityInterfaceForQIListWidget::indexOfChild(const QAccessibleInterface *pChild) const
{
    /* Make sure list still alive: */
    AssertPtrReturn(list(), -1);
    /* Make sure child is valid: */
    AssertReturn(pChild, -1);

    // WORKAROUND:
    // Not yet sure how to handle this for list widget with multiple columns, so this is a simple hack:
    const QModelIndex index = list()->itemIndex(qobject_cast<QIListWidgetItem*>(pChild->object()));
    const int iIndex = index.row();
    return iIndex;
}

QString QIAccessibilityInterfaceForQIListWidget::text(QAccessible::Text /* enmTextRole */) const
{
    /* Make sure list still alive: */
    AssertPtrReturn(list(), QString());

    /* Gather suitable text: */
    QString strText = list()->toolTip();
    if (strText.isEmpty())
        strText = list()->whatsThis();
    return strText;
}


/*********************************************************************************************************************************
*   Class QIListWidgetItem implementation.                                                                                       *
*********************************************************************************************************************************/

/* static */
QIListWidgetItem *QIListWidgetItem::toItem(QListWidgetItem *pItem)
{
    /* Make sure alive QIListWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Return casted QIListWidgetItem: */
    return static_cast<QIListWidgetItem*>(pItem);
}

/* static */
const QIListWidgetItem *QIListWidgetItem::toItem(const QListWidgetItem *pItem)
{
    /* Make sure alive QIListWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Return casted QIListWidgetItem: */
    return static_cast<const QIListWidgetItem*>(pItem);
}

/* static */
QList<QIListWidgetItem*> QIListWidgetItem::toList(const QList<QListWidgetItem*> &initialList)
{
    QList<QIListWidgetItem*> resultingList;
    foreach (QListWidgetItem *pItem, initialList)
        resultingList << toItem(pItem);
    return resultingList;
}

/* static */
QList<const QIListWidgetItem*> QIListWidgetItem::toList(const QList<const QListWidgetItem*> &initialList)
{
    QList<const QIListWidgetItem*> resultingList;
    foreach (const QListWidgetItem *pItem, initialList)
        resultingList << toItem(pItem);
    return resultingList;
}

QIListWidgetItem::QIListWidgetItem(QIListWidget *pListWidget)
    : QListWidgetItem(pListWidget, ItemType)
{
}

QIListWidgetItem::QIListWidgetItem(const QString &strText, QIListWidget *pListWidget)
    : QListWidgetItem(strText, pListWidget, ItemType)
{
}

QIListWidgetItem::QIListWidgetItem(const QIcon &icon, const QString &strText, QIListWidget *pListWidget)
    : QListWidgetItem(icon, strText, pListWidget, ItemType)
{
}

QIListWidget *QIListWidgetItem::parentList() const
{
    return listWidget() ? qobject_cast<QIListWidget*>(listWidget()) : 0;
}

QString QIListWidgetItem::defaultText() const
{
    /* Return item text as default: */
    return text();
}


/*********************************************************************************************************************************
*   Class QIListWidget implementation.                                                                                           *
*********************************************************************************************************************************/

QIListWidget::QIListWidget(QWidget *pParent /* = 0 */, bool fDelegatePaintingToSubclass /* = false */)
    : QListWidget(pParent)
    , m_fDelegatePaintingToSubclass(fDelegatePaintingToSubclass)
{
    /* Install QIListWidget accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQIListWidget::pFactory);
    /* Install QIListWidgetItem accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQIListWidgetItem::pFactory);

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

    /* Do not paint frame and background unless requested: */
    if (m_fDelegatePaintingToSubclass)
    {
        setFrameShape(QFrame::NoFrame);
        viewport()->setAutoFillBackground(false);
    }
}

void QIListWidget::setSizeHintForItems(const QSize &sizeHint)
{
    /* Pass the sizeHint to all the items: */
    for (int i = 0; i < count(); ++i)
        item(i)->setSizeHint(sizeHint);
}

int QIListWidget::childCount() const
{
    return count();
}

QIListWidgetItem *QIListWidget::childItem(int iIndex) const
{
    return item(iIndex) ? QIListWidgetItem::toItem(item(iIndex)) : 0;
}

QModelIndex QIListWidget::itemIndex(QListWidgetItem *pItem)
{
    return indexFromItem(pItem);
}

QList<QIListWidgetItem*> QIListWidget::selectedItems() const
{
    return QIListWidgetItem::toList(QListWidget::selectedItems());
}

QList<QIListWidgetItem*> QIListWidget::findItems(const QString &strText, Qt::MatchFlags flags) const
{
    return QIListWidgetItem::toList(QListWidget::findItems(strText, flags));
}

void QIListWidget::paintEvent(QPaintEvent *pEvent)
{
    /* Call to base-class if allowed: */
    if (!m_fDelegatePaintingToSubclass)
        QListWidget::paintEvent(pEvent);

    /* Create item painter: */
    QPainter painter;
    painter.begin(viewport());

    /* Notify listeners about painting: */
    for (int iIndex = 0; iIndex < count(); ++iIndex)
        emit painted(item(iIndex), &painter);

    /* Close item painter: */
    painter.end();
}

void QIListWidget::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QListWidget::resizeEvent(pEvent);

    /* Notify listeners about resizing: */
    emit resized(pEvent->size(), pEvent->oldSize());
}
