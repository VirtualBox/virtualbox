/* $Id: UIVMLogViewerTextEdit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#if defined(RT_OS_SOLARIS)
# include <QFontDatabase>
#endif
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QLabel>
#include <QScrollBar>
#include <QStyle>
#include <QTextBlock>

/* GUI includes: */
#include "UIIconPool.h"
#include "UITranslationEventListener.h"
#include "UIVMLogViewerTextEdit.h"
#include "UIVMLogViewerWidget.h"

/* Other VBox includes: */
#include "iprt/assert.h"

/** We use a modified scrollbar style for our QPlainTextEdits to get the
    markings on the scrollbars correctly. The default scrollbarstyle does not
    reveal the height of the pushbuttons on the scrollbar (on either side of it, with arrow on them)
    to compute the marking locations correctly. Thus we turn these push buttons off: */
const QString verticalScrollBarStyle("QScrollBar:vertical {"
                                     "border: 1px ridge grey; "
                                     "margin: 0px 0px 0 0px;}"
                                     "QScrollBar::handle:vertical {"
                                     "min-height: 10px;"
                                     "background: grey;}"
                                     "QScrollBar::add-line:vertical {"
                                     "width: 0px;}"
                                     "QScrollBar::sub-line:vertical {"
                                     "width: 0px;}");

const QString horizontalScrollBarStyle("QScrollBar:horizontal {"
                                       "border: 1px ridge grey; "
                                       "margin: 0px 0px 0 0px;}"
                                       "QScrollBar::handle:horizontal {"
                                       "min-height: 10px;"
                                       "background: grey;}"
                                       "QScrollBar::add-line:horizontal {"
                                       "height: 0px;}"
                                       "QScrollBar::sub-line:horizontal {"
                                       "height: 0px;}");

/*********************************************************************************************************************************
*   UILogScrollLabel definition.                                                                                             *
*********************************************************************************************************************************/

class UILogScrollLabel : public QLabel
{
    Q_OBJECT;

public:

    UILogScrollLabel(QWidget *pParent);
    void setOpacity(qreal fOpacity);

protected:

    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private:

    qreal m_fOpacity;
};


/*********************************************************************************************************************************
*   UIIndicatorScrollBar definition.                                                                                             *
*********************************************************************************************************************************/

class UIIndicatorScrollBar : public QScrollBar
{
    Q_OBJECT;

public:

    UIIndicatorScrollBar(QWidget *parent = 0);
    void setMarkingsVector(const QVector<float> &vector);
    void clearMarkingsVector();

protected:

    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private:

    /* Stores the relative (to scrollbar's height) positions of markings,
       where we draw a horizontal line. Values are in [0.0, 1.0]*/
    QVector<float> m_markingsVector;
};


/*********************************************************************************************************************************
*   UILogScrollLabel definition.                                                                                             *
*********************************************************************************************************************************/

UILogScrollLabel::UILogScrollLabel(QWidget *pParent)
    : QLabel(pParent)
    , m_fOpacity(1.f)
{
}

void UILogScrollLabel::setOpacity(qreal fOpacity)
{
    if (m_fOpacity == fOpacity)
        return;
    m_fOpacity = fOpacity;
    update();
}

void UILogScrollLabel::paintEvent(QPaintEvent *pEvent)
{
    Q_UNUSED(pEvent);
    QPainter painter(this);
    painter.setBrush(Qt::red);
    painter.setOpacity(m_fOpacity);
    painter.drawPixmap(0, 0, pixmap());
}


/*********************************************************************************************************************************
*   UIIndicatorScrollBar implemetation.                                                                                          *
*********************************************************************************************************************************/

UIIndicatorScrollBar::UIIndicatorScrollBar(QWidget *parent /*= 0 */)
    :QScrollBar(parent)
{
    setStyleSheet(verticalScrollBarStyle);
}

void UIIndicatorScrollBar::setMarkingsVector(const QVector<float> &vector)
{
    m_markingsVector = vector;
}

void UIIndicatorScrollBar::clearMarkingsVector()
{
    m_markingsVector.clear();
}

void UIIndicatorScrollBar::paintEvent(QPaintEvent *pEvent) /* RT_OVERRIDE */
{
    QScrollBar::paintEvent(pEvent);
    /* Put a red line to mark the bookmark positions: */
    for (int i = 0; i < m_markingsVector.size(); ++i)
    {
        QPointF p1 = QPointF(0, m_markingsVector[i] * height());
        QPointF p2 = QPointF(width(), m_markingsVector[i] * height());

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(255, 0, 0, 75), 1.1f));
        painter.drawLine(p1, p2);
    }
}


/*********************************************************************************************************************************
*   UILineNumberArea definition.                                                                                                 *
*********************************************************************************************************************************/

class UILineNumberArea : public QWidget
{
public:
    UILineNumberArea(UIVMLogViewerTextEdit *textEdit);
    QSize sizeHint() const RT_OVERRIDE RT_FINAL;

protected:

    void paintEvent(QPaintEvent *event) RT_OVERRIDE RT_FINAL;
    void mouseMoveEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;
    void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:
    UIVMLogViewerTextEdit *m_pTextEdit;
};


/*********************************************************************************************************************************
*   UILineNumberArea implemetation.                                                                                              *
*********************************************************************************************************************************/

UILineNumberArea::UILineNumberArea(UIVMLogViewerTextEdit *textEdit)
    :QWidget(textEdit)
    , m_pTextEdit(textEdit)
{
    setMouseTracking(true);
}

QSize UILineNumberArea::sizeHint() const
{
    if (!m_pTextEdit)
        return QSize();
    return QSize(m_pTextEdit->lineNumberAreaWidth(), 0);
}

void UILineNumberArea::paintEvent(QPaintEvent *event)
{
    if (m_pTextEdit)
        m_pTextEdit->lineNumberAreaPaintEvent(event);
}

void UILineNumberArea::mouseMoveEvent(QMouseEvent *pEvent)
{
    if (m_pTextEdit)
        m_pTextEdit->setMouseCursorLine(m_pTextEdit->lineNumberForPos(pEvent->position().toPoint()));
    update();
}

void UILineNumberArea::mousePressEvent(QMouseEvent *pEvent)
{
    if (m_pTextEdit)
        m_pTextEdit->toggleBookmark(m_pTextEdit->bookmarkForPos(pEvent->position().toPoint()));
}


/*********************************************************************************************************************************
*   UIVMLogViewerTextEdit implemetation.                                                                                         *
*********************************************************************************************************************************/

UIVMLogViewerTextEdit::UIVMLogViewerTextEdit(QWidget* parent /* = 0 */)
    : QPlainTextEdit(parent)
    , m_pLineNumberArea(0)
    , m_mouseCursorLine(-1)
    , m_bShownTextIsFiltered(false)
    , m_bShowLineNumbers(true)
    , m_bWrapLines(true)
    , m_bHasContextMenu(false)
    , m_iVerticalScrollBarValue(0)
    , m_pScrollToBottomLabel(0)
    , m_pScrollToTopLabel(0)
{
    configure();
    prepare();
}

void UIVMLogViewerTextEdit::configure()
{
    setMouseTracking(true);

    /* Prepare modified standard palette: */
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Inactive, QPalette::Highlight, pal.color(QPalette::Active, QPalette::Highlight));
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, pal.color(QPalette::Active, QPalette::HighlightedText));
    setPalette(pal);

    /* Configure this' wrap mode: */
    setWrapLines(false);
    setReadOnly(true);
    m_originalCursor = cursor();
}

void UIVMLogViewerTextEdit::prepare()
{
    prepareWidgets();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVMLogViewerTextEdit::sltRetranslateUI);
}

void UIVMLogViewerTextEdit::prepareWidgets()
{
    m_pLineNumberArea = new UILineNumberArea(this);
    AssertReturnVoid(m_pLineNumberArea);

    m_pScrollToBottomLabel = new UILogScrollLabel(this);
    m_pScrollToTopLabel = new UILogScrollLabel(this);
    AssertReturnVoid(m_pScrollToBottomLabel);
    AssertReturnVoid(m_pScrollToTopLabel);

    m_pScrollToBottomLabel->installEventFilter(this);
    m_pScrollToTopLabel->installEventFilter(this);

    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
    m_pScrollToBottomLabel->setPixmap(UIIconPool::iconSet(":/arrow_to_bottom_32px.png").pixmap(iIconMetric, iIconMetric));
    m_pScrollToBottomLabel->setOpacity(0.4);

    m_pScrollToTopLabel->setPixmap(UIIconPool::iconSet(":/arrow_to_top_32px.png").pixmap(iIconMetric, iIconMetric));
    m_pScrollToTopLabel->setOpacity(0.4);

    connect(this, &UIVMLogViewerTextEdit::blockCountChanged, this, &UIVMLogViewerTextEdit::sltUpdateLineNumberAreaWidth);
    connect(this, &UIVMLogViewerTextEdit::updateRequest, this, &UIVMLogViewerTextEdit::sltHandleUpdateRequest);
    sltUpdateLineNumberAreaWidth(0);

    setVerticalScrollBar(new UIIndicatorScrollBar());
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    QScrollBar *pHorizontalScrollBar = horizontalScrollBar();
    if (pHorizontalScrollBar)
        pHorizontalScrollBar->setStyleSheet(horizontalScrollBarStyle);
}

void UIVMLogViewerTextEdit::setCurrentFont(QFont font)
{
    setFont(font);
    if (m_pLineNumberArea)
        m_pLineNumberArea->setFont(font);
}

void UIVMLogViewerTextEdit::saveScrollBarPosition()
{
    if (verticalScrollBar())
        m_iVerticalScrollBarValue = verticalScrollBar()->value();
}

void UIVMLogViewerTextEdit::restoreScrollBarPosition()
{
    QScrollBar *pBar = verticalScrollBar();
    if (pBar && pBar->maximum() >= m_iVerticalScrollBarValue && pBar->minimum() <= m_iVerticalScrollBarValue)
        pBar->setValue(m_iVerticalScrollBarValue);
}

void UIVMLogViewerTextEdit::setCursorPosition(int iPosition)
{
    QTextCursor cursor = textCursor();
    cursor.setPosition(iPosition);
    setTextCursor(cursor);
    centerCursor();
}

int UIVMLogViewerTextEdit::lineNumberAreaWidth()
{
    if (!m_bShowLineNumbers)
        return 0;

    int iDigits = 1;
    int iMax = qMax(1, blockCount());
    while (iMax >= 10) {
        iMax /= 10;
        ++iDigits;
    }

    return 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * iDigits;
}

void UIVMLogViewerTextEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    if (!m_bShowLineNumbers)
        return;
    QPainter painter(m_pLineNumberArea);
    painter.fillRect(event->rect(), Qt::lightGray);
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            /* Mark this line if it is bookmarked, but only if the text is not filtered. */
            if (m_bookmarkLineSet.contains(blockNumber + 1) && !m_bShownTextIsFiltered)
            {
                QPainterPath path;
                path.addRect(0, top, m_pLineNumberArea->width(), m_pLineNumberArea->fontMetrics().lineSpacing());
                painter.fillPath(path, QColor(204, 255, 51, 125));
                painter.drawPath(path);
            }
            /* Draw a unfilled red rectangled around the line number to indicate line the mouse cursor is currently
               hovering on. Do this only if mouse is over the ext edit or the context menu is around: */
            if ((blockNumber + 1) == m_mouseCursorLine && (underMouse() || m_bHasContextMenu))
            {
                painter.setPen(Qt::red);
                painter.drawRect(0, top, m_pLineNumberArea->width(), m_pLineNumberArea->fontMetrics().lineSpacing());
            }

            painter.setPen(Qt::black);
            painter.drawText(0, top, m_pLineNumberArea->width(), m_pLineNumberArea->fontMetrics().lineSpacing(),
                             Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
}

void UIVMLogViewerTextEdit::sltRetranslateUI()
{
    m_strBackgroungText = QString(UIVMLogViewerWidget::tr("Filtered"));
}

bool UIVMLogViewerTextEdit::eventFilter(QObject *pObject, QEvent *pEvent)
{
    if (pObject == m_pScrollToBottomLabel || pObject == m_pScrollToTopLabel)
    {
        UILogScrollLabel *pLabel = qobject_cast<UILogScrollLabel*>(pObject);
        if (pLabel)
        {
            switch(pEvent->type())
            {
                case (QEvent::Enter):
                {
                    setCursor(Qt::PointingHandCursor);
                    pLabel->setOpacity(1.0);
                    break;
                }
                case (QEvent::Leave):
                {
                    setCursor(m_originalCursor);
                    pLabel->setOpacity(0.4);
                    break;
                }
                case (QEvent::MouseButtonPress):
                {
                    if (pObject == m_pScrollToBottomLabel)
                        scrollToBottom();
                    else
                        scrollToTop();
                    break;
                }
                default:
                    break;
            }
        }
    }
    return QPlainTextEdit::eventFilter(pObject, pEvent);
}

void UIVMLogViewerTextEdit::contextMenuEvent(QContextMenuEvent *pEvent)
{
    /* If shown text is filtered, do not create Bookmark action since
       we disable all bookmarking related functionalities in this case. */
    if (m_bShownTextIsFiltered)
    {
        QPlainTextEdit::contextMenuEvent(pEvent);
        return;
    }
    m_bHasContextMenu = true;
    QMenu *menu = createStandardContextMenu();


    QAction *pAction = menu->addAction(UIVMLogViewerWidget::tr("Bookmark"));
    if (pAction)
    {
        pAction->setCheckable(true);
        UIVMLogBookmark menuBookmark = bookmarkForPos(pEvent->pos());
        pAction->setChecked(m_bookmarkLineSet.contains(menuBookmark.m_iLineNumber));
        if (pAction->isChecked())
            pAction->setIcon(UIIconPool::iconSet(":/log_viewer_bookmark_on_16px.png"));
        else
            pAction->setIcon(UIIconPool::iconSet(":/log_viewer_bookmark_off_16px.png"));

        m_iContextMenuBookmark = menuBookmark;
        connect(pAction, &QAction::triggered, this, &UIVMLogViewerTextEdit::sltBookmark);

    }
    menu->exec(pEvent->globalPos());

    if (pAction)
        disconnect(pAction, &QAction::triggered, this, &UIVMLogViewerTextEdit::sltBookmark);

    delete menu;
    m_bHasContextMenu = false;
}

void UIVMLogViewerTextEdit::resizeEvent(QResizeEvent *pEvent)
{
    QPlainTextEdit::resizeEvent(pEvent);
    if (m_pLineNumberArea)
    {
        QRect cr = contentsRect();
        m_pLineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

   if (m_pScrollToBottomLabel && m_pScrollToTopLabel)
   {
       QScrollBar *pVScrollBar = verticalScrollBar();
       QScrollBar *pHScrollBar = horizontalScrollBar();

       int iMarginX = 0;
       if (pVScrollBar)
           iMarginX = pVScrollBar->width();
       if (iMarginX == 0)
           iMarginX = m_pScrollToBottomLabel->width();
       iMarginX += 2 * QApplication::style()->pixelMetric(QStyle::PM_FocusFrameHMargin);
       int iMarginY = 0;
       if (pHScrollBar)
           iMarginY = pHScrollBar->height();
       if (iMarginY == 0)
           iMarginY = m_pScrollToTopLabel->height();


       m_pScrollToBottomLabel->move(width() - iMarginX - m_pScrollToBottomLabel->width(),
                                     0.5 * m_pScrollToBottomLabel->height());

       m_pScrollToTopLabel->move(width() - iMarginX - m_pScrollToTopLabel->width(),
                                  height() - iMarginY - 1.5 * m_pScrollToTopLabel->height());
   }
}

void UIVMLogViewerTextEdit::mouseMoveEvent(QMouseEvent *pEvent)
{
    setMouseCursorLine(lineNumberForPos(pEvent->position().toPoint()));
    if (m_pLineNumberArea)
        m_pLineNumberArea->update();
    QPlainTextEdit::mouseMoveEvent(pEvent);
}

void UIVMLogViewerTextEdit::leaveEvent(QEvent * pEvent)
{
    QPlainTextEdit::leaveEvent(pEvent);
    /* Force a redraw as mouse leaves this to remove the mouse
       cursor track rectangle (the red rectangle we draw on the line number area). */
    update();
}

void UIVMLogViewerTextEdit::sltUpdateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void UIVMLogViewerTextEdit::sltHandleUpdateRequest(const QRect &rect, int dy)
{
    if (dy)
        m_pLineNumberArea->scroll(0, dy);
    else
        m_pLineNumberArea->update(0, rect.y(), m_pLineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        sltUpdateLineNumberAreaWidth(0);

    if (viewport())
        viewport()->update();
}

void UIVMLogViewerTextEdit::sltBookmark()
{
    toggleBookmark(m_iContextMenuBookmark);
}

void UIVMLogViewerTextEdit::setScrollBarMarkingsVector(const QVector<float> &vector)
{
    UIIndicatorScrollBar* vScrollBar = qobject_cast<UIIndicatorScrollBar*>(verticalScrollBar());
    if (vScrollBar)
        vScrollBar->setMarkingsVector(vector);
}

void UIVMLogViewerTextEdit::clearScrollBarMarkingsVector()
{
    UIIndicatorScrollBar* vScrollBar = qobject_cast<UIIndicatorScrollBar*>(verticalScrollBar());
    if (vScrollBar)
        vScrollBar->clearMarkingsVector();
}

void UIVMLogViewerTextEdit::scrollToLine(int lineNumber)
{
    QTextDocument* pDocument = document();
    if (!pDocument)
        return;

    moveCursor(QTextCursor::End);
    int halfPageLineCount = 0.5 * visibleLineCount() ;
    QTextCursor cursor(pDocument->findBlockByLineNumber(qMax(lineNumber - halfPageLineCount, 0)));
    setTextCursor(cursor);
}

void UIVMLogViewerTextEdit::scrollToBottom()
{
    moveCursor(QTextCursor::End);
    ensureCursorVisible();
}

void UIVMLogViewerTextEdit::scrollToTop()
{
    moveCursor(QTextCursor::Start);
    ensureCursorVisible();
}

int UIVMLogViewerTextEdit::visibleLineCount()
{
    int height = 0;
    if (viewport())
        height = viewport()->height();
    if (verticalScrollBar() && verticalScrollBar()->isVisible())
        height -= horizontalScrollBar()->height();
    int singleLineHeight = fontMetrics().lineSpacing();
    if (singleLineHeight == 0)
        return 0;
    return height / singleLineHeight;
}

void UIVMLogViewerTextEdit::setBookmarkLineSet(const QSet<int>& lineSet)
{
    m_bookmarkLineSet = lineSet;
    update();
}

int  UIVMLogViewerTextEdit::lineNumberForPos(const QPoint &position)
{
    QTextCursor cursor = cursorForPosition(position);
    QTextBlock block = cursor.block();
    return block.blockNumber() + 1;
}

UIVMLogBookmark UIVMLogViewerTextEdit::bookmarkForPos(const QPoint &position)
{
    QTextCursor cursor = cursorForPosition(position);
    QTextBlock block = cursor.block();
    return UIVMLogBookmark(block.blockNumber() + 1, cursor.position(), block.text());
}

void UIVMLogViewerTextEdit::setMouseCursorLine(int lineNumber)
{
    m_mouseCursorLine = lineNumber;
}

void UIVMLogViewerTextEdit::toggleBookmark(const UIVMLogBookmark& bookmark)
{
    if (m_bShownTextIsFiltered)
        return;

    if (m_bookmarkLineSet.contains(bookmark.m_iLineNumber))
        emit sigDeleteBookmark(bookmark);
    else
        emit sigAddBookmark(bookmark);
}

void UIVMLogViewerTextEdit::setShownTextIsFiltered(bool warning)
{
    if (m_bShownTextIsFiltered == warning)
        return;
    m_bShownTextIsFiltered = warning;
    if (viewport())
        viewport()->update();
}

void UIVMLogViewerTextEdit::setShowLineNumbers(bool bShowLineNumbers)
{
    if (m_bShowLineNumbers == bShowLineNumbers)
        return;
    m_bShowLineNumbers = bShowLineNumbers;
    emit updateRequest(viewport()->rect(), 0);
}

bool UIVMLogViewerTextEdit::showLineNumbers() const
{
    return m_bShowLineNumbers;
}

void UIVMLogViewerTextEdit::setWrapLines(bool bWrapLines)
{
    if (m_bWrapLines == bWrapLines)
        return;
    m_bWrapLines = bWrapLines;
    if (m_bWrapLines)
    {
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WordWrap);
    }
    else
    {
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setWordWrapMode(QTextOption::NoWrap);
    }
    update();
}

bool UIVMLogViewerTextEdit::wrapLines() const
{
    return m_bWrapLines;
}

int  UIVMLogViewerTextEdit::currentVerticalScrollBarValue() const
{
    if (!verticalScrollBar())
        return -1;
    return verticalScrollBar()->value();
}

void UIVMLogViewerTextEdit::setCurrentVerticalScrollBarValue(int value)
{
    if (!verticalScrollBar())
        return;

    setCenterOnScroll(true);

    verticalScrollBar()->setValue(value);
    verticalScrollBar()->setSliderPosition(value);
    viewport()->update();
    update();
}

#include "UIVMLogViewerTextEdit.moc"
