/* $Id: UIChooserView.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserView class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserView_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserView_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIGraphicsView.h"

/* Forward declarations: */
class UIChooserModel;
class UIChooserSearchWidget;

/** QIGraphicsView extension used as VM chooser pane view. */
class UIChooserView : public QIGraphicsView
{
    Q_OBJECT;

signals:

    /** Notifies listeners about resize. */
    void sigResized();

    /** Notifies listeners about search widget visibility changed to @a fVisible. */
    void sigSearchWidgetVisibilityChanged(bool fVisible);

public:

    /** Constructs a Chooser-view passing @a pParent to the base-class. */
    UIChooserView(QWidget *pParent);

    /** @name General stuff.
      * @{ */
        /** Defines @a pChooserModel reference. */
        void setModel(UIChooserModel *pChooserModel);
        /** Returns Chooser-model reference. */
        UIChooserModel *model() const;
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Returns whether search widget visible. */
        bool isSearchWidgetVisible() const;
        /** Makes search widget @a fVisible. */
        void setSearchWidgetVisible(bool fVisible);

        /** Updates search widget's results count.
          * @param  iTotalMatchCount             Brings total search results count.
          * @param  iCurrentlyScrolledItemIndex  Brings the item index search currently scrolled to. */
        void setSearchResultsCount(int iTotalMatchCount, int iCurrentlyScrolledItemIndex);
        /** Forwards @a strSearchText to the search widget which in
          * turn appends it to the current (if any) search term. */
        void appendToSearchString(const QString &strSearchText);
        /** Repeats the last search again. */
        void redoSearch();
    /** @} */

public slots:

    /** @name Layout stuff.
      * @{ */
        /** Handles minimum width @a iHint change. */
        void sltMinimumWidthHintChanged(int iHint);
    /** @} */

protected:

    /** @name Event handling stuff.
      * @{ */
        /** Handles resize @a pEvent. */
        virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;
    /** @} */

private slots:

    /** @name Theme stuff stuff.
      * @{ */
        /** Updates palette. */
        void sltUpdatePalette() { preparePalette(); }
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Handles request for a new search.
          * @param  strSearchTerm  Brings the search term.
          * @param  iSearchFlags   Brings the item search flags. */
        void sltRedoSearch(const QString &strSearchTerm, int iSearchFlags);
        /** Handles request to scroll to @a fNext search result. */
        void sltHandleScrollToSearchResult(bool fNext);
        /** Handles request to scroll to make search widget @a fVisible. */
        void sltHandleSearchWidgetVisibilityToggle(bool fVisible);
    /** @} */

    /** @name Event handling stuff.
     * @{ */
       /** Handles translation event. */
       void sltRetranslateUI();
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares this. */
        void prepareThis();
        /** Prepares palette. */
        void preparePalette();
        /** Prepares widgets. */
        void prepareWidget();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Updates scene rectangle. */
        void updateSceneRect();
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Updates search widget's geometry. */
        void updateSearchWidgetGeometry();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the Chooser-model reference. */
        UIChooserModel *m_pChooserModel;
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Holds the search widget instance. */
        UIChooserSearchWidget *m_pSearchWidget;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Holds the minimum width hint. */
        int m_iMinimumWidthHint;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserView_h */
