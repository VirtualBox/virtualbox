/* $Id: QITableView.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QITableView class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QITableView_h
#define FEQT_INCLUDED_SRC_extensions_QITableView_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTableView>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QITableViewCell;
class QITableViewRow;
class QITableView;


/** OObject subclass used as cell for the QITableView. */
class SHARED_LIBRARY_STUFF QITableViewCell : public QObject
{
    Q_OBJECT;

public:

    /** Constructs table-view cell for passed @a pParentRow. */
    QITableViewCell(QITableViewRow *pParentRow)
        : m_pRow(pParentRow)
    {}

    /** Defines the parent @a pRow reference. */
    void setRow(QITableViewRow *pRow) { m_pRow = pRow; }
    /** Returns the parent row reference. */
    QITableViewRow *row() const { return m_pRow; }

    /** Returns the cell text. */
    virtual QString text() const = 0;

private:

    /** Holds the parent row reference. */
    QITableViewRow *m_pRow;
};


/** OObject subclass used as row for the QITableView. */
class SHARED_LIBRARY_STUFF QITableViewRow : public QObject
{
    Q_OBJECT;

public:

    /** Constructs table-view row for passed @a pParentTable. */
    QITableViewRow(QITableView *pParentTable)
        : m_pTable(pParentTable)
    {}

    /** Defines the parent @a pTable reference. */
    void setTable(QITableView *pTable) { m_pTable = pTable; }
    /** Returns the parent table reference. */
    QITableView *table() const { return m_pTable; }

    /** Returns the number of children. */
    virtual int childCount() const = 0;
    /** Returns the child item with @a iIndex. */
    virtual QITableViewCell *childItem(int iIndex) const = 0;

private:

    /** Holds the parent table reference. */
    QITableView *m_pTable;
};


/** QTableView subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QITableView : public QTableView
{
    Q_OBJECT;

signals:

    /** Notifies listeners about index changed from @a previous to @a current. */
    void sigCurrentChanged(const QModelIndex &current, const QModelIndex &previous);

public:

    /** Constructs table-view passing @a pParent to the base-class. */
    QITableView(QWidget *pParent = 0);
    /** Destructs table-view. */
    virtual ~QITableView() RT_OVERRIDE;

    /** Makes sure current editor data committed. */
    void makeSureEditorDataCommitted();

protected slots:

    /** Stores the created @a pEditor for passed @a index in the map. */
    virtual void sltEditorCreated(QWidget *pEditor, const QModelIndex &index);
    /** Clears the destoyed @a pEditor from the map. */
    virtual void sltEditorDestroyed(QObject *pEditor);

protected:

    /** Handles index change from @a previous to @a current. */
    virtual void currentChanged(const QModelIndex &current, const QModelIndex &previous) RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the map of editors stored for passed indexes. */
    QMap<QModelIndex, QObject*> m_editors;
};


#endif /* !FEQT_INCLUDED_SRC_extensions_QITableView_h */
