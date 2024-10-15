/* $Id: UIVMLogViewerBookmarksWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerBookmarksWidget_h
#define FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerBookmarksWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIVMLogBookmark.h"
#include "UIVMLogViewerPanel.h"

/* Forward declarations: */
class QComboBox;
class QWidget;
class QIToolButton;

/** UIVMLogViewerPanel extension providing GUI for bookmark management. Show a list of bookmarks currently set
 *  for displayed log page. It has controls to navigate and clear bookmarks. */
class UIVMLogViewerBookmarksWidget : public UIVMLogViewerPane
{
    Q_OBJECT;

public:

    UIVMLogViewerBookmarksWidget(QWidget *pParent, UIVMLogViewerWidget *pViewer);

    /** Adds a single bookmark to an existing list of bookmarks. Possibly called
     *  by UIVMLogViewerWidget when user adds a bookmark thru context menu etc. */
    void addBookmark(const QPair<int, QString> &newBookmark);
    /** Clear the bookmark list and show this list instead. Probably done after
     *  user switches to another log page tab etc. */
    void setBookmarksList(const QVector<QPair<int, QString> > &bookmarkList);
    void updateBookmarkList(const QVector<UIVMLogBookmark>& bookmarkList);
    /** Disable/enable all the widget except the close button */
    void disableEnableBookmarking(bool flag);

signals:

    void sigDeleteBookmarkByIndex(int bookmarkIndex);
    void sigDeleteAllBookmarks();
    void sigBookmarkSelected(int index);

protected:

    virtual void prepareWidgets() RT_OVERRIDE;
    virtual void prepareConnections() RT_OVERRIDE;

private slots:

    void sltDeleteCurrentBookmark();
    void sltBookmarkSelected(int index);
    void sltGotoNextBookmark();
    void sltGotoPreviousBookmark();
    void sltGotoSelectedBookmark();
    /** Handles the translation event. */
    void sltRetranslateUI();

private:

    /** @a index is the index of the curent bookmark. */
    void setBookmarkIndex(int index);

    const int     m_iMaxBookmarkTextLength;
    QComboBox    *m_pBookmarksComboBox;
    QIToolButton *m_pGotoSelectedBookmark;
    QIToolButton *m_pDeleteAllButton;
    QIToolButton *m_pDeleteCurrentButton;
    QIToolButton *m_pNextButton;
    QIToolButton *m_pPreviousButton;
};

#endif /* !FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerBookmarksWidget_h */
