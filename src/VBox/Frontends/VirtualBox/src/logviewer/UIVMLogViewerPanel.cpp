/* $Id: UIVMLogViewerPanel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <QPlainTextEdit>
#include <QPushButton>

/* GUI includes: */
#include "UIIconPool.h"
#include "UIVMLogPage.h"
#include "UIVMLogViewerPanel.h"
#include "UIVMLogViewerWidget.h"
#include "UIVMLogViewerSearchWidget.h"
#include "UIVMLogViewerFilterWidget.h"
#include "UIVMLogViewerBookmarksWidget.h"
#include "UIVMLogViewerPreferencesWidget.h"
#include "UITranslationEventListener.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

UIVMLogViewerPaneContainer::UIVMLogViewerPaneContainer(QWidget *pParent,
                                                       UIVMLogViewerWidget *pViewer,
                                                       EmbedTo enmEmbedTo /* = EmbedTo_Stack */)
    : UIPaneContainer(pParent, enmEmbedTo, true /* detach allowed */)
    , m_pViewer(pViewer)
    , m_pSearchWidget(0)
    , m_pFilterWidget(0)
    , m_pBookmarksWidget(0)
    , m_pPreferencesWidget(0)
{
    prepare();
}

void UIVMLogViewerPaneContainer::prepare()
{
    /* Search tab: */
    m_pSearchWidget = new UIVMLogViewerSearchWidget(0, m_pViewer);
    insertTab(Page_Search, m_pSearchWidget);

    connect(m_pSearchWidget, &UIVMLogViewerSearchWidget::sigHighlightingUpdated,
            this, &UIVMLogViewerPaneContainer::sigHighlightingUpdated);
    connect(m_pSearchWidget, &UIVMLogViewerSearchWidget::sigSearchUpdated,
            this, &UIVMLogViewerPaneContainer::sigSearchUpdated);
    connect(m_pSearchWidget, &UIVMLogViewerSearchWidget::sigShowPane,
            this, &UIVMLogViewerPaneContainer::sigShowSearchPane);

    /* Filter tab: */
    m_pFilterWidget = new UIVMLogViewerFilterWidget(0, m_pViewer);
    insertTab(Page_Filter, m_pFilterWidget);

    connect(m_pFilterWidget, &UIVMLogViewerFilterWidget::sigFilterApplied,
            this, &UIVMLogViewerPaneContainer::sigFilterApplied);

    /* Bookmark tab: */
    m_pBookmarksWidget = new UIVMLogViewerBookmarksWidget(0, m_pViewer);
    insertTab(Page_Bookmark, m_pBookmarksWidget);

    connect(m_pBookmarksWidget, &UIVMLogViewerBookmarksWidget::sigDeleteBookmarkByIndex,
            this, &UIVMLogViewerPaneContainer::sigDeleteBookmarkByIndex);
    connect(m_pBookmarksWidget, &UIVMLogViewerBookmarksWidget::sigDeleteAllBookmarks,
            this, &UIVMLogViewerPaneContainer::sigDeleteAllBookmarks);
    connect(m_pBookmarksWidget, &UIVMLogViewerBookmarksWidget::sigBookmarkSelected,
            this, &UIVMLogViewerPaneContainer::sigBookmarkSelected);

    /* Preferences tab: */
    m_pPreferencesWidget = new UIVMLogViewerPreferencesWidget(0, m_pViewer);
    insertTab(Page_Preferences, m_pPreferencesWidget);

    connect(m_pPreferencesWidget, &UIVMLogViewerPreferencesWidget::sigShowLineNumbers,
            this, &UIVMLogViewerPaneContainer::sigShowLineNumbers);
    connect(m_pPreferencesWidget, &UIVMLogViewerPreferencesWidget::sigWrapLines,
            this, &UIVMLogViewerPaneContainer::sigWrapLines);
    connect(m_pPreferencesWidget, &UIVMLogViewerPreferencesWidget::sigChangeFontSizeInPoints,
            this, &UIVMLogViewerPaneContainer::sigChangeFontSizeInPoints);
    connect(m_pPreferencesWidget, &UIVMLogViewerPreferencesWidget::sigChangeFont,
            this, &UIVMLogViewerPaneContainer::sigChangeFont);
    connect(m_pPreferencesWidget, &UIVMLogViewerPreferencesWidget::sigResetToDefaults,
            this, &UIVMLogViewerPaneContainer::sigResetToDefaults);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVMLogViewerPaneContainer::sltRetranslateUI);
}

void UIVMLogViewerPaneContainer::refreshSearch()
{
    if (m_pSearchWidget)
        m_pSearchWidget->refreshSearch();
}

QVector<float> UIVMLogViewerPaneContainer::matchLocationVector() const
{
    if (!m_pSearchWidget)
        return QVector<float>();
    return m_pSearchWidget->matchLocationVector();
}

int UIVMLogViewerPaneContainer::matchCount() const
{
    if (!m_pSearchWidget)
        return 0;
    return m_pSearchWidget->matchCount();
}

void UIVMLogViewerPaneContainer::applyFilter()
{
    if (m_pFilterWidget)
        m_pFilterWidget->applyFilter();
}

void UIVMLogViewerPaneContainer::updateBookmarkList(const QVector<UIVMLogBookmark>& bookmarkList)
{
    if (m_pBookmarksWidget)
        m_pBookmarksWidget->updateBookmarkList(bookmarkList);
}

void UIVMLogViewerPaneContainer::disableEnableBookmarking(bool flag)
{
    if (m_pBookmarksWidget)
        m_pBookmarksWidget->disableEnableBookmarking(flag);
}

void UIVMLogViewerPaneContainer::setShowLineNumbers(bool bShowLineNumbers)
{
    if (m_pPreferencesWidget)
        m_pPreferencesWidget->setShowLineNumbers(bShowLineNumbers);
}

void UIVMLogViewerPaneContainer::setWrapLines(bool bWrapLines)
{
    if (m_pPreferencesWidget)
        m_pPreferencesWidget->setWrapLines(bWrapLines);
}

void UIVMLogViewerPaneContainer::setFontSizeInPoints(int fontSizeInPoints)
{
    if (m_pPreferencesWidget)
        m_pPreferencesWidget->setFontSizeInPoints(fontSizeInPoints);
}

void UIVMLogViewerPaneContainer::sltRetranslateUI()
{
    setTabText(Page_Search, UIVMLogViewerWidget::tr("Find"));
    setTabText(Page_Filter, UIVMLogViewerWidget::tr("Filter"));
    setTabText(Page_Bookmark, UIVMLogViewerWidget::tr("Bookmarks"));
    setTabText(Page_Preferences, UIVMLogViewerWidget::tr("Preferences"));
}

bool UIVMLogViewerPaneContainer::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Check if the search panel is interested in the event (most prob. key event): */
    if (m_pSearchWidget && m_pSearchWidget->handleSearchRelatedEvents(pObject, pEvent))
        return true;

    return UIPaneContainer::eventFilter(pObject, pEvent);
}


/*********************************************************************************************************************************
*   UIVMLogViewerPanel implementation.                                                                                           *
*********************************************************************************************************************************/

UIVMLogViewerPane::UIVMLogViewerPane(QWidget *pParent, UIVMLogViewerWidget *pViewer)
    : QWidget(pParent)
    , m_pViewer(pViewer)
{
}

UIVMLogViewerWidget* UIVMLogViewerPane::viewer()
{
    return m_pViewer;
}

const UIVMLogViewerWidget* UIVMLogViewerPane::viewer() const
{
    return m_pViewer;
}

QTextDocument  *UIVMLogViewerPane::textDocument()
{
    QPlainTextEdit *pEdit = textEdit();
    if (!pEdit)
        return 0;
    return textEdit()->document();
}

QPlainTextEdit *UIVMLogViewerPane::textEdit()
{
    if (!viewer())
        return 0;
    UIVMLogPage *logPage = viewer()->currentLogPage();
    if (!logPage)
        return 0;
    return logPage->textEdit();
}

const QString* UIVMLogViewerPane::logString() const
{
    if (!viewer())
        return 0;
    const UIVMLogPage* const page = qobject_cast<const UIVMLogPage* const>(viewer()->currentLogPage());
    if (!page)
        return 0;
    return &(page->logString());
}
