/* $Id: QIStyledItemDelegate.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIStyledItemDelegate class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIStyledItemDelegate_h
#define FEQT_INCLUDED_SRC_extensions_QIStyledItemDelegate_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QStyledItemDelegate>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QStyledItemDelegate subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QIStyledItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a pEditor created for particular model @a index. */
    void sigEditorCreated(QWidget *pEditor, const QModelIndex &index) const;

    /** Notifies listeners about editor's Enter key triggering. */
    void sigEditorEnterKeyTriggered();

public:

    /** Constructs delegate passing @a pParent to the base-class. */
    QIStyledItemDelegate(QObject *pParent);

    /** Defines whether delegate should watch for the editor's data commits. */
    void setWatchForEditorDataCommits(bool fWatch);
    /** Defines whether delegate should watch for the editor's Enter key triggering. */
    void setWatchForEditorEnterKeyTriggering(bool fWatch);

protected:

    /** Returns the widget used to edit the item specified by @a index.
      * The @a pParent widget and style @a option are used to control how the editor widget appears. */
    virtual QWidget *createEditor(QWidget *pParent,
                                  const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const RT_OVERRIDE;

private:

    /** Holds whether delegate should watch for the editor's data commits. */
    bool m_fWatchForEditorDataCommits : 1;
    /** Holds whether delegate should watch for the editor's Enter key triggering. */
    bool m_fWatchForEditorEnterKeyTriggering : 1;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIStyledItemDelegate_h */
