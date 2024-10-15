/* $Id: UIDetailsContextMenu.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsContextMenu class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsContextMenu_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsContextMenu_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declaration: */
class QListWidget;
class QListWidgetItem;
class UIDetailsModel;

/** QWidget subclass used as Details pane context menu. */
class UIDetailsContextMenu : public QWidget
{
    Q_OBJECT;

public:

    /** Context menu data fields. */
    enum DataField
    {
        DataField_Type = Qt::UserRole + 1,
        DataField_Name = Qt::UserRole + 2,
    };

    /** Constructs context-menu.
      * @param  pModel  Brings model object reference. */
    UIDetailsContextMenu(UIDetailsModel *pModel);

    /** Updates category check-states. */
    void updateCategoryStates();
    /** Updates option check-states for certain @a enmRequiredCategoryType. */
    void updateOptionStates(DetailsElementType enmRequiredCategoryType = DetailsElementType_Invalid);

protected:

    /** Handles translation event for categories list. */
    void retranslateCategories();
    /** Handles translation event for options list. */
    void retranslateOptions();

private slots:

    /** Handles signal about category list-widget @a pItem hovered. */
    void sltCategoryItemEntered(QListWidgetItem *pItem);
    /** Handles signal about category list-widget @a pItem clicked. */
    void sltCategoryItemClicked(QListWidgetItem *pItem);
    /** Handles signal about current category list-widget @a pItem hovered. */
    void sltCategoryItemChanged(QListWidgetItem *pCurrent, QListWidgetItem *pPrevious);

    /** Handles signal about option list-widget @a pItem hovered. */
    void sltOptionItemEntered(QListWidgetItem *pItem);
    /** Handles signal about option list-widget @a pItem clicked. */
    void sltOptionItemClicked(QListWidgetItem *pItem);
    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** (Re)populates categories. */
    void populateCategories();
    /** (Re)populates options. */
    void populateOptions();

    /** Adjusts both list widgets. */
    void adjustListWidgets();

    /** Creates category list item with specified @a icon. */
    QListWidgetItem *createCategoryItem(const QIcon &icon);
    /** Creates option list item. */
    QListWidgetItem *createOptionItem();

    /** Holds the model reference. */
    UIDetailsModel *m_pModel;

    /** Holds the categories list instance. */
    QListWidget *m_pListWidgetCategories;
    /** Holds the options list instance. */
    QListWidget *m_pListWidgetOptions;
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsContextMenu_h */
