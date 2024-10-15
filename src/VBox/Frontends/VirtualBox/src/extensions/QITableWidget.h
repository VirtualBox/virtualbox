/* $Id: QITableWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QITableWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QITableWidget_h
#define FEQT_INCLUDED_SRC_extensions_QITableWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTableWidget>
#include <QTableWidgetItem>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QITableWidget;

/** QTableWidgetItem subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QITableWidgetItem : public QObject, public QTableWidgetItem
{
    Q_OBJECT;

public:

    /** Item type for QITableWidgetItem. */
    enum { ItemType = QTableWidgetItem::UserType + 1 };

    /** Casts QTableWidgetItem* to QITableWidgetItem* if possible. */
    static QITableWidgetItem *toItem(QTableWidgetItem *pItem);
    /** Casts const QTableWidgetItem* to const QITableWidgetItem* if possible. */
    static const QITableWidgetItem *toItem(const QTableWidgetItem *pItem);

    /** Constructs item passing @a strText into the base-class. */
    QITableWidgetItem(const QString &strText = QString());

    /** Returns the parent table-widget. */
    QITableWidget *parentTable() const;
};

/** QTableWidget subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QITableWidget : public QTableWidget
{
    Q_OBJECT;

signals:

    /** Notifies about particular tree-widget @a pItem is painted with @a pPainter. */
    void painted(QTableWidgetItem *pItem, QPainter *pPainter);
    /** Notifies about tree-widget being resized from @a oldSize to @a size. */
    void resized(const QSize &size, const QSize &oldSize);

public:

    /** Constructs tree-widget passing @a pParent to the base-class. */
    QITableWidget(QWidget *pParent = 0);

    /** Returns the child item with @a iRow and @a iColumn. */
    QITableWidgetItem *childItem(int iRow, int iColumn) const;
    /** Returns a model-index of @a pItem specified. */
    QModelIndex itemIndex(QTableWidgetItem *pItem);

protected:

    /** Handles paint @a pEvent. */
    void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles resize @a pEvent. */
    void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE RT_FINAL;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QITableWidget_h */
