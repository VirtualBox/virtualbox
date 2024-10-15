/* $Id: UIVMLogViewerSearchWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#if defined(RT_OS_SOLARIS)
# include <QFontDatabase>
#endif
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextBlock>

/* GUI includes: */
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UISearchLineEdit.h"
#include "UITranslationEventListener.h"
#include "UIVMLogPage.h"
#include "UIVMLogViewerSearchWidget.h"
#include "UIVMLogViewerWidget.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

/* Other VBox includes: */
#include <iprt/assert.h>

UIVMLogViewerSearchWidget::UIVMLogViewerSearchWidget(QWidget *pParent, UIVMLogViewerWidget *pViewer)
    : UIVMLogViewerPane(pParent, pViewer)
    , m_pSearchEditor(0)
    , m_pNextButton(0)
    , m_pPreviousButton(0)
    , m_pCaseSensitiveCheckBox(0)
    , m_pMatchWholeWordCheckBox(0)
    , m_pHighlightAllCheckBox(0)
{
    /* Prepare: */
    prepareWidgets();
    prepareConnections();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVMLogViewerSearchWidget::sltRetranslateUI);
}

void UIVMLogViewerSearchWidget::refreshSearch()
{
    /* We start the search from the end of the doc. assuming log's end is more interesting: */
    if (isVisible())
        performSearch(BackwardSearch, true);
    else
        reset();

    emit sigHighlightingUpdated();
}

void UIVMLogViewerSearchWidget::reset()
{
    m_iSelectedMatchIndex = 0;
    m_matchLocationVector.clear();
    m_matchedCursorPosition.clear();
    if (m_pSearchEditor)
        m_pSearchEditor->reset();
    emit sigHighlightingUpdated();
}

const QVector<float> &UIVMLogViewerSearchWidget::matchLocationVector() const
{
    return m_matchLocationVector;
}

int UIVMLogViewerSearchWidget::matchCount() const
{
    return m_matchedCursorPosition.size();
}

void UIVMLogViewerSearchWidget::hideEvent(QHideEvent *pEvent)
{
    /* Get focus-widget: */
    QWidget *pFocus = QApplication::focusWidget();
    /* If focus-widget is valid and child-widget of search-panel,
     * focus next child-widget in line: */
    if (pFocus && pFocus->parent() == this)
        focusNextPrevChild(true);
    /* Call to base-class: */
    UIVMLogViewerPane::hideEvent(pEvent);
    reset();
}

void UIVMLogViewerSearchWidget::sltSearchTextChanged(const QString &strSearchString)
{
    /* Enable/disable Next-Previous buttons as per search-string validity: */
    m_pNextButton->setEnabled(!strSearchString.isEmpty());
    m_pPreviousButton->setEnabled(!strSearchString.isEmpty());

    /* If search-string is not empty: */
    if (!strSearchString.isEmpty())
    {
        /* Reset the position to force the search restart from the document's end: */
        performSearch(BackwardSearch, true);
        emit sigHighlightingUpdated();
        return;
    }

    /* If search-string is empty, reset cursor position: */
    if (!viewer())
        return;

    QPlainTextEdit *pBrowser = textEdit();
    if (!pBrowser)
        return;
    /* If  cursor has selection: */
    if (pBrowser->textCursor().hasSelection())
    {
        /* Get cursor and reset position: */
        QTextCursor cursor = pBrowser->textCursor();
        cursor.setPosition(cursor.anchor());
        pBrowser->setTextCursor(cursor);
    }
    m_matchedCursorPosition.clear();
    m_matchLocationVector.clear();
    clearHighlighting();
    emit sigSearchUpdated();
}

void UIVMLogViewerSearchWidget::sltHighlightAllCheckBox()
{
    if (!viewer())
        return;

    QTextDocument *pDocument = textDocument();
    if (!pDocument)
        return;

    if (m_pHighlightAllCheckBox->isChecked())
    {
        const QString &searchString = m_pSearchEditor->text();
        if (searchString.isEmpty())
            return;
        highlightAll(searchString);
    }
    else
        clearHighlighting();

    emit sigHighlightingUpdated();
}

void UIVMLogViewerSearchWidget::sltCaseSentitiveCheckBox()
{
    refreshSearch();
}

void UIVMLogViewerSearchWidget::sltMatchWholeWordCheckBox()
{
    refreshSearch();
}

void UIVMLogViewerSearchWidget::sltSelectNextPreviousMatch()
{
    moveSelection(sender() == m_pNextButton);
}

void UIVMLogViewerSearchWidget::prepareWidgets()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);

    /* Create search field layout: */
    QHBoxLayout *pSearchFieldLayout = new QHBoxLayout;
    AssertReturnVoid(pSearchFieldLayout);
    pSearchFieldLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
    pSearchFieldLayout->setSpacing(5);
#else
    pSearchFieldLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) / 2);
#endif

    /* Create search-editor: */
    m_pSearchEditor = new UISearchLineEdit(0 /* parent */);
    AssertReturnVoid(m_pSearchEditor);
    m_pSearchEditor->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    pSearchFieldLayout->addWidget(m_pSearchEditor);

    /* Create search button layout: */
    QHBoxLayout *pSearchButtonsLayout = new QHBoxLayout;
    AssertReturnVoid(pSearchButtonsLayout);

    pSearchButtonsLayout->setContentsMargins(0, 0, 0, 0);
    pSearchButtonsLayout->setSpacing(0);

    /* Create Previous button: */
    m_pPreviousButton = new QIToolButton;
    AssertReturnVoid(m_pPreviousButton);
    m_pPreviousButton->setIcon(UIIconPool::iconSet(":/log_viewer_search_backward_16px.png"));
    pSearchButtonsLayout->addWidget(m_pPreviousButton);

    /* Create Next button: */
    m_pNextButton = new QIToolButton;
    AssertReturnVoid(m_pNextButton);
    m_pNextButton->setIcon(UIIconPool::iconSet(":/log_viewer_search_forward_16px.png"));
    pSearchButtonsLayout->addWidget(m_pNextButton);

    pSearchFieldLayout->addLayout(pSearchButtonsLayout);

    pMainLayout->addLayout(pSearchFieldLayout);

    /* Create case-sensitive check-box: */
    m_pCaseSensitiveCheckBox = new QCheckBox;
    AssertReturnVoid(m_pCaseSensitiveCheckBox);
    pMainLayout->addWidget(m_pCaseSensitiveCheckBox);

    /* Create whole-word check-box: */
    m_pMatchWholeWordCheckBox = new QCheckBox;
    AssertReturnVoid(m_pMatchWholeWordCheckBox);
    setFocusProxy(m_pMatchWholeWordCheckBox);
    pMainLayout->addWidget(m_pMatchWholeWordCheckBox);

    /* Create highlight-all check-box: */
    m_pHighlightAllCheckBox = new QCheckBox;
    AssertReturnVoid(m_pHighlightAllCheckBox);
    pMainLayout->addWidget(m_pHighlightAllCheckBox);

    pMainLayout->addStretch(1);
}

void UIVMLogViewerSearchWidget::prepareConnections()
{
    connect(m_pSearchEditor, &UISearchLineEdit::textChanged, this, &UIVMLogViewerSearchWidget::sltSearchTextChanged);
    connect(m_pNextButton, &QIToolButton::clicked, this, &UIVMLogViewerSearchWidget::sltSelectNextPreviousMatch);
    connect(m_pPreviousButton, &QIToolButton::clicked, this, &UIVMLogViewerSearchWidget::sltSelectNextPreviousMatch);

    connect(m_pHighlightAllCheckBox, &QCheckBox::stateChanged,
            this, &UIVMLogViewerSearchWidget::sltHighlightAllCheckBox);
    connect(m_pCaseSensitiveCheckBox, &QCheckBox::stateChanged,
            this, &UIVMLogViewerSearchWidget::sltCaseSentitiveCheckBox);
    connect(m_pMatchWholeWordCheckBox, &QCheckBox::stateChanged,
            this, &UIVMLogViewerSearchWidget::sltMatchWholeWordCheckBox);
}

void UIVMLogViewerSearchWidget::sltRetranslateUI()
{
    m_pSearchEditor->setToolTip(UIVMLogViewerWidget::tr("Enter a search string here"));
    m_pNextButton->setToolTip(UIVMLogViewerWidget::tr("Search for the next occurrence of the string (F3)"));
    m_pPreviousButton->setToolTip(UIVMLogViewerWidget::tr("Search for the previous occurrence of the string (Shift+F3)"));

    m_pCaseSensitiveCheckBox->setText(UIVMLogViewerWidget::tr("C&ase Sensitive"));
    m_pCaseSensitiveCheckBox->setToolTip(UIVMLogViewerWidget::tr("When checked, perform case sensitive search"));

    m_pMatchWholeWordCheckBox->setText(UIVMLogViewerWidget::tr("Ma&tch Whole Word"));
    m_pMatchWholeWordCheckBox->setToolTip(UIVMLogViewerWidget::tr("When checked, search matches only complete words"));

    m_pHighlightAllCheckBox->setText(UIVMLogViewerWidget::tr("&Highlight All"));
    m_pHighlightAllCheckBox->setToolTip(UIVMLogViewerWidget::tr("When checked, all occurence of the search text are highlighted"));
}

void UIVMLogViewerSearchWidget::keyPressEvent(QKeyEvent *pEvent)
{
    switch (pEvent->key())
    {
        /* Process Enter press as 'search-next',
         * performed for any search panel widget: */
        case Qt::Key_Enter:
        case Qt::Key_Return:
            {
                if (pEvent->modifiers() == 0 ||
                    pEvent->modifiers() & Qt::KeypadModifier)
                {
                    /* Animate click on 'Next' button: */
                m_pNextButton->animateClick();
                return;
                }
                break;
            }
        default:
            break;
    }
    /* Call to base-class: */
    UIVMLogViewerPane::keyPressEvent(pEvent);
}

bool UIVMLogViewerSearchWidget::handleSearchRelatedEvents(QObject *pObject, QEvent *pEvent)
{
    /* Handle only events sent to viewer(): */
    if (pObject != viewer())
        return false;

    /* Depending on event-type: */
    switch (pEvent->type())
    {
        /* Process key press only: */
        case QEvent::KeyPress:
        {
            /* Cast to corresponding key press event: */
            QKeyEvent *pKeyEvent = static_cast<QKeyEvent*>(pEvent);

            /* Handle F3/Shift+F3 as search next/previous shortcuts: */
            if (pKeyEvent->key() == Qt::Key_F3)
            {
                /* If there is no modifier 'Key-F3' is pressed: */
                if (pKeyEvent->QInputEvent::modifiers() == 0)
                {
                    /* Animate click on 'Next' button: */
                    m_pNextButton->animateClick();
                    return true;
                }
                /* If there is 'ShiftModifier' 'Shift + Key-F3' is pressed: */
                else if (pKeyEvent->QInputEvent::modifiers() == Qt::ShiftModifier)
                {
                    /* Animate click on 'Prev' button: */
                    m_pPreviousButton->animateClick();
                    return true;
                }
            }
            /* Handle Ctrl+F key combination as a shortcut to focus search field: */
            else if (pKeyEvent->QInputEvent::modifiers() == Qt::ControlModifier &&
                     pKeyEvent->key() == Qt::Key_F)
            {
                /* Make sure current log-page is visible: */
                emit sigShowPane();
                /* Set focus on search-editor: */
                m_pSearchEditor->setFocus();
                return true;
            }
            /* Handle alpha-numeric keys to implement the "find as you type" feature: */
            else if ((pKeyEvent->QInputEvent::modifiers() & ~Qt::ShiftModifier) == 0 &&
                     pKeyEvent->key() >= Qt::Key_Exclam && pKeyEvent->key() <= Qt::Key_AsciiTilde)
            {
                /* Make sure current log-page is visible: */
                emit sigShowPane();
                /* Set focus on search-editor: */
                m_pSearchEditor->setFocus();
                /* Insert the text to search-editor, which triggers the search-operation for new text: */
                m_pSearchEditor->insert(pKeyEvent->text());
                return true;
            }
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return false;
}

void UIVMLogViewerSearchWidget::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    UIVMLogViewerPane::showEvent(pEvent);
    if (m_pSearchEditor)
    {
        /* Set focus on search-editor: */
        m_pSearchEditor->setFocus();
        /* Select all the text: */
        m_pSearchEditor->selectAll();
        m_pSearchEditor->setMatchCount(m_matchedCursorPosition.size());
    }
}

void UIVMLogViewerSearchWidget::performSearch(SearchDirection , bool )
{
    QPlainTextEdit *pTextEdit = textEdit();
    if (!pTextEdit)
        return;
    QTextDocument *pDocument = textDocument();
    if (!pDocument)
        return;
    if (!m_pSearchEditor)
        return;

    const QString &searchString = m_pSearchEditor->text();
    emit sigSearchUpdated();

    if (searchString.isEmpty())
        return;

    findAll(pDocument, searchString);
    m_iSelectedMatchIndex = 0;
    selectMatch(m_iSelectedMatchIndex, searchString);
    if (m_pSearchEditor)
    {
        m_pSearchEditor->setMatchCount(m_matchedCursorPosition.size());
        m_pSearchEditor->setScrollToIndex(m_matchedCursorPosition.empty() ? -1 : 0);
    }
    if (m_pHighlightAllCheckBox->isChecked())
        highlightAll(searchString);
}

void UIVMLogViewerSearchWidget::clearHighlighting()
{
    QPlainTextEdit *pTextEdit = textEdit();
    if (pTextEdit)
        pTextEdit->setExtraSelections(QList<QTextEdit::ExtraSelection>());
    emit sigHighlightingUpdated();
}

void UIVMLogViewerSearchWidget::highlightAll(const QString &searchString)
{
    clearHighlighting();
    QPlainTextEdit *pTextEdit = textEdit();

    if (!pTextEdit)
        return;

    QList<QTextEdit::ExtraSelection> extraSelections;
    for (int i = 0; i < m_matchedCursorPosition.size(); ++i)
    {
        QTextEdit::ExtraSelection selection;
        QTextCursor cursor = pTextEdit->textCursor();
        cursor.setPosition(m_matchedCursorPosition[i]);
        cursor.setPosition(m_matchedCursorPosition[i] + searchString.length(), QTextCursor::KeepAnchor);
        QTextCharFormat format = cursor.charFormat();
        format.setBackground(Qt::yellow);

        selection.cursor = cursor;
        selection.format = format;
        extraSelections.append(selection);
    }
    pTextEdit->setExtraSelections(extraSelections);

}

void UIVMLogViewerSearchWidget::findAll(QTextDocument *pDocument, const QString &searchString)
{
    if (!pDocument)
        return;
    m_matchedCursorPosition.clear();
    m_matchLocationVector.clear();
    if (searchString.isEmpty())
        return;
    QTextCursor cursor(pDocument);
    QTextDocument::FindFlags flags = constructFindFlags(ForwardSearch);
    int blockCount = pDocument->blockCount();
    while (!cursor.isNull() && !cursor.atEnd())
    {
        cursor = pDocument->find(searchString, cursor, flags);

        if (!cursor.isNull())
        {
            m_matchedCursorPosition << cursor.position() - searchString.length();
            /* The following assumes we have single line blocks only: */
            int cursorLine = pDocument->findBlock(cursor.position()).blockNumber();
            if (blockCount != 0)
                m_matchLocationVector.push_back(cursorLine / static_cast<float>(blockCount));
        }
    }
}

void UIVMLogViewerSearchWidget::selectMatch(int iMatchIndex, const QString &searchString)
{
    if (!textEdit())
        return;
    if (searchString.isEmpty())
        return;
    if (iMatchIndex < 0 || iMatchIndex >= m_matchedCursorPosition.size())
        return;

    QTextCursor cursor = textEdit()->textCursor();
    /* Move the cursor to the beginning of the matched string: */
    cursor.setPosition(m_matchedCursorPosition.at(iMatchIndex), QTextCursor::MoveAnchor);
    /* Move the cursor to the end of the matched string while keeping the anchor at the begining thus selecting the text: */
    cursor.setPosition(m_matchedCursorPosition.at(iMatchIndex) + searchString.length(), QTextCursor::KeepAnchor);
    textEdit()->ensureCursorVisible();
    textEdit()->setTextCursor(cursor);
}

void UIVMLogViewerSearchWidget::moveSelection(bool fForward)
{
    if (matchCount() == 0)
        return;
    if (fForward)
        m_iSelectedMatchIndex = m_iSelectedMatchIndex >= m_matchedCursorPosition.size() - 1 ? 0 : (m_iSelectedMatchIndex + 1);
    else
        m_iSelectedMatchIndex = m_iSelectedMatchIndex <= 0 ? m_matchedCursorPosition.size() - 1 : (m_iSelectedMatchIndex - 1);
    selectMatch(m_iSelectedMatchIndex, m_pSearchEditor->text());
    if (m_pSearchEditor)
        m_pSearchEditor->setScrollToIndex(m_iSelectedMatchIndex);
}

int UIVMLogViewerSearchWidget::countMatches(QTextDocument *pDocument, const QString &searchString) const
{
    if (!pDocument)
        return 0;
    if (searchString.isEmpty())
        return 0;
    int count = 0;
    QTextCursor cursor(pDocument);
    QTextDocument::FindFlags flags = constructFindFlags(ForwardSearch);
    while (!cursor.isNull() && !cursor.atEnd())
    {
        cursor = pDocument->find(searchString, cursor, flags);

        if (!cursor.isNull())
            ++count;
    }
    return count;
}

QTextDocument::FindFlags UIVMLogViewerSearchWidget::constructFindFlags(SearchDirection eDirection) const
{
   QTextDocument::FindFlags findFlags;
   if (eDirection == BackwardSearch)
       findFlags = findFlags | QTextDocument::FindBackward;
   if (m_pCaseSensitiveCheckBox->isChecked())
       findFlags = findFlags | QTextDocument::FindCaseSensitively;
   if (m_pMatchWholeWordCheckBox->isChecked())
       findFlags = findFlags | QTextDocument::FindWholeWords;
   return findFlags;
}
