/* $Id: UIVMLogViewerPanel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerPanel_h
#define FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerPanel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIPaneContainer.h"
#include "UIVMLogBookmark.h"

/* Forward declarations: */
class QPlainTextEdit;
class QTextDocument;
class UIVMLogViewerWidget;
class UIVMLogViewerSearchWidget;
class UIVMLogViewerFilterWidget;
class UIVMLogViewerBookmarksWidget;
class UIVMLogViewerPreferencesWidget;

class UIVMLogViewerPaneContainer : public UIPaneContainer
{
    Q_OBJECT;

signals:

    void sigHighlightingUpdated();
    void sigSearchUpdated();
    void sigFilterApplied();
    void sigDeleteBookmarkByIndex(int bookmarkIndex);
    void sigDeleteAllBookmarks();
    void sigBookmarkSelected(int index);

    void sigShowLineNumbers(bool show);
    void sigWrapLines(bool show);
    void sigChangeFontSizeInPoints(int size);
    void sigChangeFont(QFont font);
    void sigResetToDefaults();

    void sigShowSearchPane();

public:

    UIVMLogViewerPaneContainer(QWidget *pParent,
                               UIVMLogViewerWidget *pViewer,
                               EmbedTo enmEmbedTo = EmbedTo_Stack);

    /** @name Search page pass through functions
      * @{ */
        void refreshSearch();
        QVector<float> matchLocationVector() const;
        /** Returns the number of the matches to the current search. */
        int matchCount() const;
    /** @} */

    /** @name Filter page pass through functions
      * @{ */
        void applyFilter();
    /** @} */

    /** @name Bookmark page pass through functions
      * @{ */
        void updateBookmarkList(const QVector<UIVMLogBookmark>& bookmarkList);
        void disableEnableBookmarking(bool flag);
    /** @} */

    /** @name Preferences page pass through functions
      * @{ */
        void setShowLineNumbers(bool bShowLineNumbers);
        void setWrapLines(bool bWrapLines);
        void setFontSizeInPoints(int fontSizeInPoints);
    /** @} */

    enum Page
    {
        Page_Search = 0,
        Page_Filter,
        Page_Bookmark,
        Page_Preferences,
        Page_Max
    };

protected:

    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltRetranslateUI();

private:

    void prepare() RT_OVERRIDE;

    UIVMLogViewerWidget             *m_pViewer;
    UIVMLogViewerSearchWidget        *m_pSearchWidget;
    UIVMLogViewerFilterWidget       *m_pFilterWidget;
    UIVMLogViewerBookmarksWidget     *m_pBookmarksWidget;
    UIVMLogViewerPreferencesWidget  *m_pPreferencesWidget;
};

/** UIDialonPanel extension acting as the base class for UIVMLogViewerXXXPanel widgets. */
class UIVMLogViewerPane : public QWidget
{
    Q_OBJECT;

public:

    UIVMLogViewerPane(QWidget *pParent, UIVMLogViewerWidget *pViewer);

protected:

    virtual void prepareWidgets() = 0;
    virtual void prepareConnections()  = 0;

    /* Access functions for children classes. */
    UIVMLogViewerWidget        *viewer();
    const UIVMLogViewerWidget  *viewer() const;
    QTextDocument  *textDocument();
    QPlainTextEdit *textEdit();
    /* Return the unmodified log. */
    const QString *logString() const;

private:

    /** Holds the reference to VM Log-Viewer this panel belongs to. */
    UIVMLogViewerWidget *m_pViewer;
};

#endif /* !FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerPanel_h */
