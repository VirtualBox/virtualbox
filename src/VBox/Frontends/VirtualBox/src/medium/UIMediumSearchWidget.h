/* $Id: UIMediumSearchWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumSearchWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMediumSearchWidget_h
#define FEQT_INCLUDED_SRC_medium_UIMediumSearchWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes. */
#include <QWidget>

/* GUI includes. */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QComboBox;
class QTreeWidgetItem;
class QIToolButton;
class QITreeWidget;
class UISearchLineEdit;

/** QWidget extension providing a simple way to enter a earch term and search type for medium searching
 *  in virtual media manager, medium selection dialog, etc. */
class  SHARED_LIBRARY_STUFF UIMediumSearchWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigPerformSearch();

public:

    enum SearchType
    {
        SearchByName,
        SearchByUUID,
        SearchByMax
    };

public:

    UIMediumSearchWidget(QWidget *pParent = 0);
    SearchType searchType() const;
    QString searchTerm() const;
    /** Performs the search on the items of the @p pTreeWidget. If @p is true
      * then the next marched item is selected. */
    void    search(QITreeWidget* pTreeWidget, bool fGotoNext = true);

protected:

    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltShowNextMatchingItem();
    void sltShowPreviousMatchingItem();
    void sltRetranslateUI();

private:

    void    prepareWidgets();
    /** Marks/unmarks the items of @p itemList depending on @p fMark. */
    void    markUnmarkItems(QList<QTreeWidgetItem*> &itemList, bool fMark);
    void    setUnderlineItemText(QTreeWidgetItem* pItem, bool fUnderline);
    /** Increases (or decreases if @p fNext is false) the m_iScrollToIndex and
     *  takes care of the necessary decoration changes to mark the current item. */
    void    goToNextPrevious(bool fNext);
    /** Updates the feedback text of th line edit that shows # of matches. */
    void    updateSearchLineEdit(int iMatchCount, int iScrollToIndex);

    QComboBox        *m_pSearchComboxBox;
    UISearchLineEdit *m_pSearchTermLineEdit;
    QIToolButton     *m_pShowNextMatchButton;
    QIToolButton     *m_pShowPreviousMatchButton;

    QList<QTreeWidgetItem*> m_matchedItemList;
    QITreeWidget           *m_pTreeWidget;
    /** The index to the matched item (in m_matchedItemList) which is currently selected/scrolled to. */
    int                     m_iScrollToIndex;
};

#endif /* !FEQT_INCLUDED_SRC_medium_UIMediumSearchWidget_h */
