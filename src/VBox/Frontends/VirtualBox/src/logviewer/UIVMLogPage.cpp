/* $Id: UIVMLogPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class implementation.
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

/* Qt includes: */
#include <QHBoxLayout>
#if defined(RT_OS_SOLARIS)
# include <QFontDatabase>
#endif
#include <QScrollBar>

/* GUI includes: */
#include "UIVMLogPage.h"
#include "UIVMLogViewerTextEdit.h"


/*********************************************************************************************************************************
*   UIVMLogBookmarkManager definition.                                                                                           *
*********************************************************************************************************************************/

class UIVMLogBookmarkManager
{
public:
    void addBookmark(const UIVMLogBookmark& newBookmark);
    void addBookmark(int iCursorPosition, int iLineNumber, QString strBlockText);
    void deleteBookmark(const UIVMLogBookmark& bookmark);
    void deleteBookmarkByIndex(int iIndex);
    void deleteAllBookmarks();
    int cursorPosition(int bookmarkIndex);
    QSet<int> lineSet() const;
    const QVector<UIVMLogBookmark>& bookmarkList() const;

private:

    QVector<UIVMLogBookmark> m_bookmarks;
};


/*********************************************************************************************************************************
*   UIVMLogBookmarkManager implementation.                                                                                       *
*********************************************************************************************************************************/


void UIVMLogBookmarkManager::addBookmark(const UIVMLogBookmark& newBookmark)
{
    foreach (const UIVMLogBookmark& bookmark, m_bookmarks)
        if (bookmark == newBookmark)
            return;
    m_bookmarks << newBookmark;
}

void UIVMLogBookmarkManager::addBookmark(int iCursorPosition, int iLineNumber, QString strBlockText)
{
    foreach (const UIVMLogBookmark& bookmark, m_bookmarks)
        if (bookmark.m_iLineNumber == iLineNumber)
            return;
    m_bookmarks << UIVMLogBookmark(iCursorPosition, iLineNumber, strBlockText);
}

void UIVMLogBookmarkManager::deleteBookmark(const UIVMLogBookmark& bookmark)
{
    int index = -1;
    for (int i = 0; i < m_bookmarks.size() && index == -1; ++i)
    {
        if (bookmark == m_bookmarks[i])
            index = i;
    }
    deleteBookmarkByIndex(index);
}

void UIVMLogBookmarkManager::deleteBookmarkByIndex(int iIndex)
{
    if (iIndex >= m_bookmarks.size() || iIndex < 0)
        return;
    m_bookmarks.removeAt(iIndex);
}

void UIVMLogBookmarkManager::deleteAllBookmarks()
{
    m_bookmarks.clear();
}

int UIVMLogBookmarkManager::cursorPosition(int bookmarkIndex)
{
    if (bookmarkIndex >= m_bookmarks.size())
        return 0;
    return m_bookmarks[bookmarkIndex].m_iCursorPosition;
}

QSet<int> UIVMLogBookmarkManager::lineSet() const
{
    QSet<int> lines;
    foreach (const UIVMLogBookmark& bookmark, m_bookmarks)
        lines << bookmark.m_iLineNumber;
    return lines;
}

const QVector<UIVMLogBookmark>& UIVMLogBookmarkManager::bookmarkList() const
{
    return m_bookmarks;
}


/*********************************************************************************************************************************
*   UIVMLogTab implementation.                                                                                                   *
*********************************************************************************************************************************/

UIVMLogTab::UIVMLogTab(QWidget *pParent, const QUuid &uMachineId, const QString &strMachineName)
    : QWidget(pParent)
    , m_uMachineId(uMachineId)
    , m_strMachineName(strMachineName)
{
}
const QUuid &UIVMLogTab::machineId() const
{
    return m_uMachineId;
}

const QString UIVMLogTab::machineName() const
{
    return m_strMachineName;
}


/*********************************************************************************************************************************
*   UIVMLogPage implementation.                                                                                                  *
*********************************************************************************************************************************/

UIVMLogPage::UIVMLogPage(QWidget *pParent, const QUuid &uMachineId, const QString &strMachineName)
    : UIVMLogTab(pParent, uMachineId, strMachineName)
    , m_pMainLayout(0)
    , m_pTextEdit(0)
    , m_pBookmarkManager(new UIVMLogBookmarkManager)
    , m_iSelectedBookmarkIndex(-1)
    , m_bFiltered(false)
    , m_iLogFileId(-1)
{
    prepare();
}

UIVMLogPage::~UIVMLogPage()
{
    cleanup();
}

int UIVMLogPage::defaultLogPageWidth() const
{
    if (!m_pTextEdit)
        return 0;

    /* Compute a width for 132 characters plus scrollbar and frame width: */
    const int iDefaultWidth = m_pTextEdit->fontMetrics().horizontalAdvance(QChar('x')) * 132
                            + m_pTextEdit->verticalScrollBar()->width()
                            + m_pTextEdit->frameWidth() * 2;

    return iDefaultWidth;
}


void UIVMLogPage::prepare()
{
    prepareWidgets();
}

void UIVMLogPage::prepareWidgets()
{
    m_pMainLayout = new QHBoxLayout();
    setLayout(m_pMainLayout);
    m_pMainLayout->setSpacing(0);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);

    m_pTextEdit = new UIVMLogViewerTextEdit(this);
    m_pMainLayout->addWidget(m_pTextEdit);

    connect(m_pTextEdit, &UIVMLogViewerTextEdit::sigAddBookmark, this, &UIVMLogPage::sltAddBookmark);
    connect(m_pTextEdit, &UIVMLogViewerTextEdit::sigDeleteBookmark, this, &UIVMLogPage::sltDeleteBookmark);
}

QPlainTextEdit *UIVMLogPage::textEdit()
{
    return m_pTextEdit;
}

QTextDocument* UIVMLogPage::document()
{
    if (!m_pTextEdit)
        return 0;
    return m_pTextEdit->document();
}

void UIVMLogPage::cleanup()
{
    delete m_pBookmarkManager;
}

void UIVMLogPage::setLogContent(const QString &strLogContent, bool fError)
{
    if (!fError)
    {
        m_strLog = strLogContent;
        setTextEditText(strLogContent);
    }
    else
    {
        markForError();
        setTextEditTextAsHtml(strLogContent);
    }
}

const QString& UIVMLogPage::logString() const
{
    return m_strLog;
}

void UIVMLogPage::setLogFileName(const QString &strLogFileName)
{
    m_strLogFileName = strLogFileName;
}

const QString& UIVMLogPage::logFileName() const
{
    return m_strLogFileName;
}

void UIVMLogPage::setTextEditText(const QString &strText)
{
    if (!m_pTextEdit)
        return;

    m_pTextEdit->setPlainText(strText);
    /* Move the cursor position to end: */
    QTextCursor cursor = m_pTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
    m_pTextEdit->setTextCursor(cursor);
    update();
}

void UIVMLogPage::setTextEditTextAsHtml(const QString &strText)
{
    if (!m_pTextEdit)
        return;
    if (document())
        document()->setHtml(strText);
    update();
}

void UIVMLogPage::markForError()
{
    if (!m_pTextEdit)
        return;
    m_pTextEdit->setWrapLines(true);
}

void UIVMLogPage::setScrollBarMarkingsVector(const QVector<float> &vector)
{
    if (!m_pTextEdit)
        return;
    m_pTextEdit->setScrollBarMarkingsVector(vector);
    update();
}

void UIVMLogPage::clearScrollBarMarkingsVector()
{
    if (!m_pTextEdit)
        return;
    m_pTextEdit->clearScrollBarMarkingsVector();
    update();
}

void UIVMLogPage::documentUndo()
{
    if (!m_pTextEdit)
        return;
    if (m_pTextEdit->document())
        m_pTextEdit->document()->undo();
}



void UIVMLogPage::deleteAllBookmarks()
{
    if (m_pBookmarkManager)
        m_pBookmarkManager->deleteAllBookmarks();
    updateTextEditBookmarkLineSet();
}

void UIVMLogPage::scrollToBookmark(int bookmarkIndex)
{
    if (!m_pTextEdit)
        return;
    if (m_pBookmarkManager)
        m_pTextEdit->setCursorPosition(m_pBookmarkManager->cursorPosition(bookmarkIndex));
}

QVector<UIVMLogBookmark> UIVMLogPage::bookmarkList() const
{
    if (!m_pBookmarkManager)
        return QVector<UIVMLogBookmark>();
    return m_pBookmarkManager->bookmarkList();
}

void UIVMLogPage::sltAddBookmark(const UIVMLogBookmark& bookmark)
{
    if (m_pBookmarkManager)
        m_pBookmarkManager->addBookmark(bookmark);
    updateTextEditBookmarkLineSet();
    emit sigBookmarksUpdated();
}

void UIVMLogPage::sltDeleteBookmark(const UIVMLogBookmark& bookmark)
{
    if (m_pBookmarkManager)
        m_pBookmarkManager->deleteBookmark(bookmark);
    updateTextEditBookmarkLineSet();
    emit sigBookmarksUpdated();
}

void UIVMLogPage::deleteBookmarkByIndex(int iIndex)
{
    if (m_pBookmarkManager)
        m_pBookmarkManager->deleteBookmarkByIndex(iIndex);
    updateTextEditBookmarkLineSet();
    emit sigBookmarksUpdated();
}

void UIVMLogPage::updateTextEditBookmarkLineSet()
{
    if (!m_pTextEdit)
        return;
    if (m_pBookmarkManager)
        m_pTextEdit->setBookmarkLineSet(m_pBookmarkManager->lineSet());
}

bool UIVMLogPage::isFiltered() const
{
    return m_bFiltered;
}

void UIVMLogPage::setFiltered(bool filtered)
{
    if (m_bFiltered == filtered)
        return;
    m_bFiltered = filtered;
    if (m_pTextEdit)
    {
        m_pTextEdit->setShownTextIsFiltered(m_bFiltered);
        m_pTextEdit->update();
    }
    emit sigLogPageFilteredChanged(m_bFiltered);
}

void UIVMLogPage::setShowLineNumbers(bool bShowLineNumbers)
{
    if (!m_pTextEdit)
        return;
    m_pTextEdit->setShowLineNumbers(bShowLineNumbers);
}

void UIVMLogPage::setWrapLines(bool bWrapLines)
{
    if (!m_pTextEdit)
        return;
    m_pTextEdit->setWrapLines(bWrapLines);
}

QFont UIVMLogPage::currentFont() const
{
    if (!m_pTextEdit)
        return QFont();
    return m_pTextEdit->font();
}

void UIVMLogPage::setCurrentFont(QFont font)
{
    if (m_pTextEdit)
        m_pTextEdit->setCurrentFont(font);
}

void UIVMLogPage::setLogFileId(int iLogFileId)
{
    m_iLogFileId = iLogFileId;
}

int UIVMLogPage::logFileId() const
{
    return m_iLogFileId;
}

void UIVMLogPage::scrollToEnd()
{
    if (m_pTextEdit)
        m_pTextEdit->scrollToBottom();
}

void UIVMLogPage::saveScrollBarPosition()
{
    if (m_pTextEdit)
        m_pTextEdit->saveScrollBarPosition();
}

void UIVMLogPage::restoreScrollBarPosition()
{
    if (m_pTextEdit)
        m_pTextEdit->restoreScrollBarPosition();
}
