/* $Id: UIVMLogViewerSearchWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class declaration.
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

#ifndef FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerSearchWidget_h
#define FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerSearchWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTextDocument>

/* GUI includes: */
#include "UIVMLogViewerPanel.h"

/* Forward declarations: */
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QWidget;
class QIToolButton;
class UISearchLineEdit;
class UIVMLogViewerWidget;

/** UIVMLogViewerPanel extension
  * providing GUI for search-panel in VM Log-Viewer. */
class UIVMLogViewerSearchWidget : public UIVMLogViewerPane
{
    Q_OBJECT;

signals:

    void sigHighlightingUpdated();
    void sigSearchUpdated();
    void sigShowPane();

public:

    /** Constructs search-panel by passing @a pParent to the QWidget base-class constructor.
      * @param  pViewer  Specifies instance of VM Log-Viewer. */
    UIVMLogViewerSearchWidget(QWidget *pParent, UIVMLogViewerWidget *pViewer);
    /** Resets the search position and starts a new search. */
    void refreshSearch();
    const QVector<float> &matchLocationVector() const;
    /** Returns the number of the matches to the current search. */
    int matchCount() const;
    /** Handles Qt @a pEvent, used for keyboard processing. */
    bool handleSearchRelatedEvents(QObject *pObject, QEvent *pEvent);

protected:

    virtual void prepareWidgets() RT_OVERRIDE;
    virtual void prepareConnections() RT_OVERRIDE;
    /** Handles Qt key-press @a pEevent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    virtual void hideEvent(QHideEvent* pEvent) RT_OVERRIDE;

private slots:

    /** Handles textchanged signal from search-editor.
      * @param  strSearchString  Specifies search-string. */
    void sltSearchTextChanged(const QString &strSearchString);
    void sltHighlightAllCheckBox();
    void sltCaseSentitiveCheckBox();
    void sltMatchWholeWordCheckBox();
    void sltSelectNextPreviousMatch();
    void sltRetranslateUI();

private:

    enum SearchDirection { ForwardSearch, BackwardSearch };

    /** Clear the highlighting */
    void clearHighlighting();

    /** Search routine.
      * @param  eDirection     Specifies the seach direction
      * @param  highlight      if false highlight function is not called
                               thus we avoid calling highlighting for the same string repeatedly. */
    void performSearch(SearchDirection eDirection, bool highlight);
    void highlightAll(const QString &searchString);
    void findAll(QTextDocument *pDocument, const QString &searchString);
    void selectMatch(int iMatchIndex, const QString &searchString);
    void moveSelection(bool fForward);

    /** Constructs the find flags for QTextDocument::find function. */
    QTextDocument::FindFlags constructFindFlags(SearchDirection eDirection) const;
    /** Searches the whole document and return the number of matches to the current search term. */
    int countMatches(QTextDocument *pDocument, const QString &searchString) const;
    void reset();

    /** Holds the instance of search-editor we create. */
    UISearchLineEdit *m_pSearchEditor;

    QIToolButton *m_pNextButton;
    QIToolButton *m_pPreviousButton;
    /** Holds the instance of case-sensitive checkbox we create. */
    QCheckBox    *m_pCaseSensitiveCheckBox;
    QCheckBox    *m_pMatchWholeWordCheckBox;
    QCheckBox    *m_pHighlightAllCheckBox;
    /** Stores relative positions of the lines of the matches wrt. total # of lines. The values are in [0,1]
        0 being the first line 1 being the last. */
    QVector<float> m_matchLocationVector;
    /** Document positions of the cursors within th document for all matches. */
    QVector<int>   m_matchedCursorPosition;
    /** The index of the curently selected item within m_matchedCursorPosition. */
    int            m_iSelectedMatchIndex;
};


#endif /* !FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerSearchWidget_h */
