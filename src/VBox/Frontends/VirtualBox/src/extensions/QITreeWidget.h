/* $Id: QITreeWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QITreeWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QITreeWidget_h
#define FEQT_INCLUDED_SRC_extensions_QITreeWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTreeWidget>
#include <QTreeWidgetItem>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QITreeWidget;

/** A functor base to be passed to QITabWidget::filterItems(..).
  * Overload operator()(..) to filter out tree items. */
class SHARED_LIBRARY_STUFF QITreeWidgetItemFilter
{
public:

    /** Destructs item filter. */
    virtual ~QITreeWidgetItemFilter() { /* Make VC++ 19.2 happy. */ }

    /** Returns whether item can pass the filter. */
    virtual bool operator()(QTreeWidgetItem*) const
    {
        return true;
    }
};

/** QTreeWidgetItem subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QITreeWidgetItem : public QObject, public QTreeWidgetItem
{
    Q_OBJECT;

public:

    /** Item type for QITreeWidgetItem. */
    enum { ItemType = QTreeWidgetItem::UserType + 1 };

    /** Casts QTreeWidgetItem* to QITreeWidgetItem* if possible. */
    static QITreeWidgetItem *toItem(QTreeWidgetItem *pItem);
    /** Casts const QTreeWidgetItem* to const QITreeWidgetItem* if possible. */
    static const QITreeWidgetItem *toItem(const QTreeWidgetItem *pItem);

    /** Constructs item. */
    QITreeWidgetItem();

    /** Constructs item passing @a pTreeWidget into the base-class. */
    QITreeWidgetItem(QITreeWidget *pTreeWidget);
    /** Constructs item passing @a pTreeWidgetItem into the base-class. */
    QITreeWidgetItem(QITreeWidgetItem *pTreeWidgetItem);

    /** Constructs item passing @a pTreeWidget and @a strings into the base-class. */
    QITreeWidgetItem(QITreeWidget *pTreeWidget, const QStringList &strings);
    /** Constructs item passing @a pTreeWidgetItem and @a strings into the base-class. */
    QITreeWidgetItem(QITreeWidgetItem *pTreeWidgetItem, const QStringList &strings);

    /** Returns the parent tree-widget. */
    QITreeWidget *parentTree() const;
    /** Returns the parent tree-widget item. */
    QITreeWidgetItem *parentItem() const;

    /** Returns the child tree-widget item with @a iIndex. */
    QITreeWidgetItem *childItem(int iIndex) const;

    /** Returns default text. */
    virtual QString defaultText() const;
};


/** QTreeWidget subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QITreeWidget : public QTreeWidget
{
    Q_OBJECT;

signals:

    /** Notifies about particular tree-widget @a pItem is painted with @a pPainter. */
    void painted(QTreeWidgetItem *pItem, QPainter *pPainter);
    /** Notifies about tree-widget being resized from @a oldSize to @a size. */
    void resized(const QSize &size, const QSize &oldSize);

public:

    /** Constructs tree-widget passing @a pParent to the base-class.
      * @param  fDelegatePaintingToSubclass  Brings whether painting should be fully delegated to sub-class. */
    QITreeWidget(QWidget *pParent = 0, bool fDelegatePaintingToSubclass = false);

    /** Defines @a sizeHint for tree-widget items. */
    void setSizeHintForItems(const QSize &sizeHint);

    /** Returns the number of children. */
    int childCount() const;
    /** Returns the child item with @a iIndex. */
    QITreeWidgetItem *childItem(int iIndex) const;
    /** Returns a model-index of @a pItem specified. */
    QModelIndex itemIndex(QTreeWidgetItem *pItem);
    /** Recurses thru the subtree with a root @a pParent and returns a list of tree-items filtered by @a filter.
      * When @a pParent is null then QTreeWidget::invisibleRootItem() is used as the root item. */
    QList<QTreeWidgetItem*> filterItems(const QITreeWidgetItemFilter &filter, QTreeWidgetItem *pParent = 0);

protected:

    /** Handles paint @a pEvent. */
    void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles resize @a pEvent. */
    void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    /** Recurses thru the subtree with a root @a pParent and appends a
      * list of tree-items filtered by @a filter to @a filteredItemList. */
    void filterItemsInternal(const QITreeWidgetItemFilter &filter, QTreeWidgetItem *pParent,
                             QList<QTreeWidgetItem*> &filteredItemList);

    /** Holds whether painting should be fully delegated to sub-class. */
    bool  m_fDelegatePaintingToSubclass;
};


#endif /* !FEQT_INCLUDED_SRC_extensions_QITreeWidget_h */
