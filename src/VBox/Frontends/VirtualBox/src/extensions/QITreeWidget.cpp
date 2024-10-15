/* $Id: QITreeWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QITreeWidget class implementation.
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
#include "QITreeWidget.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QAccessibleObject extension used as an accessibility interface for QITreeWidgetItem. */
class QIAccessibilityInterfaceForQITreeWidgetItem : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QITreeWidgetItem accessibility interface: */
        if (pObject && strClassname == QLatin1String("QITreeWidgetItem"))
            return new QIAccessibilityInterfaceForQITreeWidgetItem(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    QIAccessibilityInterfaceForQITreeWidgetItem(QObject *pObject)
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

    /** Returns corresponding QITreeWidgetItem. */
    QITreeWidgetItem *item() const { return qobject_cast<QITreeWidgetItem*>(object()); }
};


/** QAccessibleWidget extension used as an accessibility interface for QITreeWidget. */
class QIAccessibilityInterfaceForQITreeWidget : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QITreeWidget accessibility interface: */
        if (pObject && strClassname == QLatin1String("QITreeWidget"))
            return new QIAccessibilityInterfaceForQITreeWidget(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    QIAccessibilityInterfaceForQITreeWidget(QWidget *pWidget)
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

    /** Returns corresponding QITreeWidget. */
    QITreeWidget *tree() const { return qobject_cast<QITreeWidget*>(widget()); }
};


/*********************************************************************************************************************************
*   Class QIAccessibilityInterfaceForQITreeWidgetItem implementation.                                                            *
*********************************************************************************************************************************/

QAccessibleInterface *QIAccessibilityInterfaceForQITreeWidgetItem::parent() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);

    /* Return the parent: */
    return item()->parentItem() ?
           QAccessible::queryAccessibleInterface(item()->parentItem()) :
           QAccessible::queryAccessibleInterface(item()->parentTree());
}

int QIAccessibilityInterfaceForQITreeWidgetItem::childCount() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);

    /* Return the number of children: */
    return item()->childCount();
}

QAccessibleInterface *QIAccessibilityInterfaceForQITreeWidgetItem::child(int iIndex) const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), 0);
    /* Make sure index is valid: */
    AssertReturn(iIndex >= 0 && iIndex < childCount(), 0);

    /* Return the child with the passed iIndex: */
    return QAccessible::queryAccessibleInterface(item()->childItem(iIndex));
}

int QIAccessibilityInterfaceForQITreeWidgetItem::indexOfChild(const QAccessibleInterface *pChild) const
{
    /* Search for corresponding child: */
    for (int iIndex = 0; iIndex < childCount(); ++iIndex)
        if (child(iIndex) == pChild)
            return iIndex;

    /* -1 by default: */
    return -1;
}

QRect QIAccessibilityInterfaceForQITreeWidgetItem::rect() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QRect());

    /* Compose common region: */
    QRegion region;

    /* Append item rectangle: */
    const QRect  itemRectInViewport = item()->parentTree()->visualItemRect(item());
    const QSize  itemSize           = itemRectInViewport.size();
    const QPoint itemPosInViewport  = itemRectInViewport.topLeft();
    const QPoint itemPosInScreen    = item()->parentTree()->viewport()->mapToGlobal(itemPosInViewport);
    const QRect  itemRectInScreen   = QRect(itemPosInScreen, itemSize);
    region += itemRectInScreen;

    /* Append children rectangles: */
    for (int i = 0; i < childCount(); ++i)
        region += child(i)->rect();

    /* Return common region bounding rectangle: */
    return region.boundingRect();
}

QString QIAccessibilityInterfaceForQITreeWidgetItem::text(QAccessible::Text enmTextRole) const
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

QAccessible::Role QIAccessibilityInterfaceForQITreeWidgetItem::role() const
{
    /* Return the role of item with children: */
    if (childCount() > 0)
        return QAccessible::List;

    /* ListItem by default: */
    return QAccessible::ListItem;
}

QAccessible::State QIAccessibilityInterfaceForQITreeWidgetItem::state() const
{
    /* Make sure item still alive: */
    AssertPtrReturn(item(), QAccessible::State());

    /* Compose the state: */
    QAccessible::State state;
    state.focusable = true;
    state.selectable = true;

    /* Compose the state of current item: */
    if (   item()
        && item() == QITreeWidgetItem::toItem(item()->treeWidget()->currentItem()))
    {
        state.active = true;
        state.focused = true;
        state.selected = true;
    }

    /* Compose the state of checked item: */
    if (   item()
        && item()->checkState(0) != Qt::Unchecked)
    {
        state.checked = true;
        if (item()->checkState(0) == Qt::PartiallyChecked)
            state.checkStateMixed = true;
    }

    /* Return the state: */
    return state;
}


/*********************************************************************************************************************************
*   Class QIAccessibilityInterfaceForQITreeWidget implementation.                                                                *
*********************************************************************************************************************************/

int QIAccessibilityInterfaceForQITreeWidget::childCount() const
{
    /* Make sure tree still alive: */
    AssertPtrReturn(tree(), 0);

    /* Return the number of children: */
    return tree()->childCount();
}

QAccessibleInterface *QIAccessibilityInterfaceForQITreeWidget::child(int iIndex) const
{
    /* Make sure tree still alive: */
    AssertPtrReturn(tree(), 0);
    /* Make sure index is valid: */
    AssertReturn(iIndex >= 0, 0);
    if (iIndex >= childCount())
    {
        // WORKAROUND:
        // Normally I would assert here, but Qt5 accessibility code has
        // a hard-coded architecture for a tree-widgets which we do not like
        // but have to live with and this architecture enumerates children
        // of all levels as children of level 0, so Qt5 can try to address
        // our interface with index which surely out of bounds by our laws.
        // So let's assume that's exactly such case and try to enumerate
        // visible children like they are a part of the list, not tree.
        // printf("Invalid index: %d\n", iIndex);

        // Take into account we also have header with 'column count' indexes,
        // so we should start enumerating tree indexes since 'column count'.
        const int iColumnCount = tree()->columnCount();
        int iCurrentIndex = iColumnCount;

        // Do some sanity check as well, enough?
        AssertReturn(iIndex >= iColumnCount, 0);

        // Search for sibling with corresponding index:
        QTreeWidgetItem *pItem = tree()->topLevelItem(0);
        while (pItem && iCurrentIndex < iIndex)
        {
            ++iCurrentIndex;
            if (iCurrentIndex % iColumnCount == 0)
                pItem = tree()->itemBelow(pItem);
        }

        // Return what we found:
        // if (pItem)
        //     printf("Item found: [%s]\n", pItem->text(0).toUtf8().constData());
        // else
        //     printf("Item not found\n");
        return pItem ? QAccessible::queryAccessibleInterface(QITreeWidgetItem::toItem(pItem)) : 0;
    }

    /* Return the child with the passed iIndex: */
    return QAccessible::queryAccessibleInterface(tree()->childItem(iIndex));
}

int QIAccessibilityInterfaceForQITreeWidget::indexOfChild(const QAccessibleInterface *pChild) const
{
    /* Make sure tree still alive: */
    AssertPtrReturn(tree(), -1);
    /* Make sure child is valid: */
    AssertReturn(pChild, -1);

    // WORKAROUND:
    // Not yet sure how to handle this for tree widget with multiple columns, so this is a simple hack:
    const QModelIndex index = tree()->itemIndex(qobject_cast<QITreeWidgetItem*>(pChild->object()));
    const int iIndex = index.row();
    return iIndex;
}

QString QIAccessibilityInterfaceForQITreeWidget::text(QAccessible::Text /* enmTextRole */) const
{
    /* Make sure tree still alive: */
    AssertPtrReturn(tree(), QString());

    /* Gather suitable text: */
    QString strText = tree()->toolTip();
    if (strText.isEmpty())
        strText = tree()->whatsThis();
    return strText;
}


/*********************************************************************************************************************************
*   Class QITreeWidgetItem implementation.                                                                                       *
*********************************************************************************************************************************/

/* static */
QITreeWidgetItem *QITreeWidgetItem::toItem(QTreeWidgetItem *pItem)
{
    /* Make sure alive QITreeWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Return casted QITreeWidgetItem: */
    return static_cast<QITreeWidgetItem*>(pItem);
}

/* static */
const QITreeWidgetItem *QITreeWidgetItem::toItem(const QTreeWidgetItem *pItem)
{
    /* Make sure alive QITreeWidgetItem passed: */
    if (!pItem || pItem->type() != ItemType)
        return 0;

    /* Return casted QITreeWidgetItem: */
    return static_cast<const QITreeWidgetItem*>(pItem);
}

QITreeWidgetItem::QITreeWidgetItem()
    : QTreeWidgetItem(ItemType)
{
}

QITreeWidgetItem::QITreeWidgetItem(QITreeWidget *pTreeWidget)
    : QTreeWidgetItem(pTreeWidget, ItemType)
{
}

QITreeWidgetItem::QITreeWidgetItem(QITreeWidgetItem *pTreeWidgetItem)
    : QTreeWidgetItem(pTreeWidgetItem, ItemType)
{
}

QITreeWidgetItem::QITreeWidgetItem(QITreeWidget *pTreeWidget, const QStringList &strings)
    : QTreeWidgetItem(pTreeWidget, strings, ItemType)
{
}

QITreeWidgetItem::QITreeWidgetItem(QITreeWidgetItem *pTreeWidgetItem, const QStringList &strings)
    : QTreeWidgetItem(pTreeWidgetItem, strings, ItemType)
{
}

QITreeWidget *QITreeWidgetItem::parentTree() const
{
    return treeWidget() ? qobject_cast<QITreeWidget*>(treeWidget()) : 0;
}

QITreeWidgetItem *QITreeWidgetItem::parentItem() const
{
    return QTreeWidgetItem::parent() ? toItem(QTreeWidgetItem::parent()) : 0;
}

QITreeWidgetItem *QITreeWidgetItem::childItem(int iIndex) const
{
    return QTreeWidgetItem::child(iIndex) ? toItem(QTreeWidgetItem::child(iIndex)) : 0;
}

QString QITreeWidgetItem::defaultText() const
{
    /* Return 1st cell text as default: */
    return text(0);
}


/*********************************************************************************************************************************
*   Class QITreeWidget implementation.                                                                                           *
*********************************************************************************************************************************/

QITreeWidget::QITreeWidget(QWidget *pParent /* = 0 */, bool fDelegatePaintingToSubclass /* = false */)
    : QTreeWidget(pParent)
    , m_fDelegatePaintingToSubclass(fDelegatePaintingToSubclass)
{
    /* Install QITreeWidget accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQITreeWidget::pFactory);
    /* Install QITreeWidgetItem accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForQITreeWidgetItem::pFactory);

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

void QITreeWidget::setSizeHintForItems(const QSize &sizeHint)
{
    /* Pass the sizeHint to all the top-level items: */
    for (int i = 0; i < topLevelItemCount(); ++i)
        topLevelItem(i)->setSizeHint(0, sizeHint);
}

int QITreeWidget::childCount() const
{
    return invisibleRootItem()->childCount();
}

QITreeWidgetItem *QITreeWidget::childItem(int iIndex) const
{
    return invisibleRootItem()->child(iIndex) ? QITreeWidgetItem::toItem(invisibleRootItem()->child(iIndex)) : 0;
}

QModelIndex QITreeWidget::itemIndex(QTreeWidgetItem *pItem)
{
    return indexFromItem(pItem);
}

QList<QTreeWidgetItem*> QITreeWidget::filterItems(const QITreeWidgetItemFilter &filter, QTreeWidgetItem *pParent /* = 0 */)
{
    QList<QTreeWidgetItem*> filteredItemList;
    filterItemsInternal(filter, pParent ? pParent : invisibleRootItem(), filteredItemList);
    return filteredItemList;
}

void QITreeWidget::paintEvent(QPaintEvent *pEvent)
{
    /* Call to base-class if allowed: */
    if (!m_fDelegatePaintingToSubclass)
        QTreeWidget::paintEvent(pEvent);

    /* Create item painter: */
    QPainter painter;
    painter.begin(viewport());

    /* Notify listeners about painting: */
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        emit painted(*it, &painter);
        ++it;
    }

    /* Close item painter: */
    painter.end();
}

void QITreeWidget::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QTreeWidget::resizeEvent(pEvent);

    /* Notify listeners about resizing: */
    emit resized(pEvent->size(), pEvent->oldSize());
}

void QITreeWidget::filterItemsInternal(const QITreeWidgetItemFilter &filter, QTreeWidgetItem *pParent,
                                       QList<QTreeWidgetItem*> &filteredItemList)
{
    if (!pParent)
        return;
    if (filter(pParent))
        filteredItemList.append(pParent);

    for (int i = 0; i < pParent->childCount(); ++i)
        filterItemsInternal(filter, pParent->child(i), filteredItemList);
}
