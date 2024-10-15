/* $Id: QIListWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIListWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIListWidget_h
#define FEQT_INCLUDED_SRC_extensions_QIListWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QListWidget>
#include <QListWidgetItem>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QIListWidget;

/** QListWidgetItem subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QIListWidgetItem : public QObject, public QListWidgetItem
{
    Q_OBJECT;

public:

    /** Item type for QIListWidgetItem. */
    enum { ItemType = QListWidgetItem::UserType + 1 };

    /** Casts QListWidgetItem* to QIListWidgetItem* if possible. */
    static QIListWidgetItem *toItem(QListWidgetItem *pItem);
    /** Casts const QListWidgetItem* to const QIListWidgetItem* if possible. */
    static const QIListWidgetItem *toItem(const QListWidgetItem *pItem);

    /** Casts QList<QListWidgetItem*> to QList<QIListWidgetItem*> if possible. */
    static QList<QIListWidgetItem*> toList(const QList<QListWidgetItem*> &initialList);
    /** Casts QList<QListWidgetItem*> to QList<QIListWidgetItem*> if possible. */
    static QList<const QIListWidgetItem*> toList(const QList<const QListWidgetItem*> &initialList);

    /** Constructs item passing @a pListWidget into the base-class. */
    QIListWidgetItem(QIListWidget *pListWidget);
    /** Constructs item passing @a strText and @a pListWidget into the base-class. */
    QIListWidgetItem(const QString &strText, QIListWidget *pListWidget);
    /** Constructs item passing @a icon, @a strText and @a pListWidget into the base-class. */
    QIListWidgetItem(const QIcon &icon, const QString &strText, QIListWidget *pListWidget);

    /** Returns the parent list-widget. */
    QIListWidget *parentList() const;

    /** Returns default text. */
    virtual QString defaultText() const;
};

/** QListWidget subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QIListWidget : public QListWidget
{
    Q_OBJECT;

signals:

    /** Notifies about particular list-widget @a pItem is painted with @a pPainter. */
    void painted(QListWidgetItem *pItem, QPainter *pPainter);
    /** Notifies about list-widget being resized from @a oldSize to @a size. */
    void resized(const QSize &size, const QSize &oldSize);

public:

    /** Constructs list-widget passing @a pParent to the base-class.
      * @param  fDelegatePaintingToSubclass  Brings whether painting should be fully delegated to sub-class. */
    QIListWidget(QWidget *pParent = 0, bool fDelegatePaintingToSubclass = false);

    /** Defines @a sizeHint for list-widget items. */
    void setSizeHintForItems(const QSize &sizeHint);

    /** Returns the number of children. */
    int childCount() const;
    /** Returns the child item with @a iIndex. */
    QIListWidgetItem *childItem(int iIndex) const;
    /** Returns a model-index of @a pItem specified. */
    QModelIndex itemIndex(QListWidgetItem *pItem);

    /** Returns a list of all selected items in the list widget. */
    QList<QIListWidgetItem*> selectedItems() const;
    /** Finds items with the text that matches the string text using the given flags. */
    QList<QIListWidgetItem*> findItems(const QString &text, Qt::MatchFlags flags) const;

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    /** Holds whether painting should be fully delegated to sub-class. */
    bool  m_fDelegatePaintingToSubclass;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIListWidget_h */
