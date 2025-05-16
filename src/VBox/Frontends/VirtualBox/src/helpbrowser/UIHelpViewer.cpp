/* $Id: UIHelpViewer.cpp 168909 2025-05-16 15:29:41Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHelpViewer class implementation.
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
#include <QClipboard>
#include <QtGlobal>
#include <QtHelp/QHelpEngine>
#include <QtHelp/QHelpContentWidget>
#include <QtHelp/QHelpIndexWidget>
#include <QtHelp/QHelpSearchEngine>
#include <QtHelp/QHelpSearchQueryWidget>
#include <QtHelp/QHelpSearchResultWidget>
#include <QLabel>
#include <QMenu>
#include <QHBoxLayout>
#include <QGraphicsBlurEffect>
#include <QLabel>
#include <QMimeDatabase>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QWidgetAction>
#ifdef RT_OS_SOLARIS
# include <QFontDatabase>
#endif

/* GUI includes: */
#include "QIToolButton.h"
#include "UICommon.h"
#include "UIHelpViewer.h"
#include "UIHelpBrowserWidget.h"
#include "UIIconPool.h"
#include "UISearchLineEdit.h"

/* COM includes: */
#include "CSystemProperties.h"


/*********************************************************************************************************************************
*   UIContextMenuNavigationAction definition.                                                                                    *
*********************************************************************************************************************************/
class UIContextMenuNavigationAction : public QWidgetAction
{

    Q_OBJECT;

signals:

    void sigGoBackward();
    void sigGoForward();
    void sigGoHome();
    void sigReloadPage();
    void sigAddBookmark();

public:

    UIContextMenuNavigationAction(QObject *pParent = 0);
    void setBackwardAvailable(bool fAvailable);
    void setForwardAvailable(bool fAvailable);

private slots:

    void sltGoBackward();
    void sltGoForward();
    void sltGoHome();
    void sltReloadPage();
    void sltAddBookmark();

private:

    void prepare();
    QIToolButton *m_pBackwardButton;
    QIToolButton *m_pForwardButton;
    QIToolButton *m_pHomeButton;
    QIToolButton *m_pReloadPageButton;
    QIToolButton *m_pAddBookmarkButton;
};

/*********************************************************************************************************************************
*   UIFindInPageWidget definition.                                                                                        *
*********************************************************************************************************************************/
class UIFindInPageWidget : public QWidget
{

    Q_OBJECT;

signals:

    void sigDragging(const QPoint &delta);
    void sigSearchTextChanged(const QString &strSearchText);
    void sigSelectNextMatch();
    void sigSelectPreviousMatch();
    void sigClose();

public:

    UIFindInPageWidget(QWidget *pParent = 0);
    void setMatchCountAndCurrentIndex(int iTotalMatchCount, int iCurrentlyScrolledIndex);
    void clearSearchField();

protected:

    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;

private:

    void prepare();
    UISearchLineEdit  *m_pSearchLineEdit;
    QIToolButton      *m_pNextButton;
    QIToolButton      *m_pPreviousButton;
    QIToolButton      *m_pCloseButton;
    QLabel            *m_pDragMoveLabel;
    QPoint m_previousMousePosition;
};


/*********************************************************************************************************************************
*   UIContextMenuNavigationAction implementation.                                                                                *
*********************************************************************************************************************************/
UIContextMenuNavigationAction::UIContextMenuNavigationAction(QObject *pParent /* = 0 */)
    :QWidgetAction(pParent)
    , m_pBackwardButton(0)
    , m_pForwardButton(0)
    , m_pHomeButton(0)
    , m_pReloadPageButton(0)
    , m_pAddBookmarkButton(0)
{
    prepare();
}

void UIContextMenuNavigationAction::setBackwardAvailable(bool fAvailable)
{
    if (m_pBackwardButton)
        m_pBackwardButton->setEnabled(fAvailable);
}

void UIContextMenuNavigationAction::setForwardAvailable(bool fAvailable)
{
    if (m_pForwardButton)
        m_pForwardButton->setEnabled(fAvailable);
}

void UIContextMenuNavigationAction::sltGoBackward()
{
    emit sigGoBackward();
    emit triggered();
}

void UIContextMenuNavigationAction::sltGoForward()
{
    emit sigGoForward();
    emit triggered();
}

void UIContextMenuNavigationAction::sltGoHome()
{
    emit sigGoHome();
    emit triggered();
}

void UIContextMenuNavigationAction::sltReloadPage()
{
    emit sigReloadPage();
    emit triggered();
}

void UIContextMenuNavigationAction::sltAddBookmark()
{
    emit sigAddBookmark();
    emit triggered();
}

void UIContextMenuNavigationAction::prepare()
{
    QWidget *pWidget = new QWidget;
    setDefaultWidget(pWidget);
    QHBoxLayout *pMainLayout = new QHBoxLayout(pWidget);
    AssertReturnVoid(pMainLayout);

    m_pBackwardButton = new QIToolButton;
    m_pForwardButton = new QIToolButton;
    m_pHomeButton = new QIToolButton;
    m_pReloadPageButton = new QIToolButton;
    m_pAddBookmarkButton = new QIToolButton;

    AssertReturnVoid(m_pBackwardButton &&
                     m_pForwardButton &&
                     m_pHomeButton &&
                     m_pReloadPageButton);

    m_pForwardButton->setEnabled(false);
    m_pBackwardButton->setEnabled(false);
    m_pHomeButton->setIcon(UIIconPool::iconSet(":/help_browser_home_16px.png", ":/help_browser_home_disabled_16px.png"));
    m_pReloadPageButton->setIcon(UIIconPool::iconSet(":/help_browser_reload_16px.png", ":/help_browser_reload_disabled_16px.png"));
    m_pForwardButton->setIcon(UIIconPool::iconSet(":/help_browser_forward_16px.png", ":/help_browser_forward_disabled_16px.png"));
    m_pBackwardButton->setIcon(UIIconPool::iconSet(":/help_browser_backward_16px.png", ":/help_browser_backward_disabled_16px.png"));
    m_pAddBookmarkButton->setIcon(UIIconPool::iconSet(":/help_browser_add_bookmark_16px.png", ":/help_browser_add_bookmark_disabled_16px.png"));

    m_pHomeButton->setToolTip(UIHelpBrowserWidget::tr("Return to Start Page"));
    m_pReloadPageButton->setToolTip(UIHelpBrowserWidget::tr("Reload the Current Page"));
    m_pForwardButton->setToolTip(UIHelpBrowserWidget::tr("Go Forward to Next Page"));
    m_pBackwardButton->setToolTip(UIHelpBrowserWidget::tr("Go Back to Previous Page"));
    m_pAddBookmarkButton->setToolTip(UIHelpBrowserWidget::tr("Add a New Bookmark"));

    pMainLayout->addWidget(m_pBackwardButton);
    pMainLayout->addWidget(m_pForwardButton);
    pMainLayout->addWidget(m_pHomeButton);
    pMainLayout->addWidget(m_pReloadPageButton);
    pMainLayout->addWidget(m_pAddBookmarkButton);
    pMainLayout->setContentsMargins(0, 0, 0, 0);

    connect(m_pBackwardButton, &QIToolButton::pressed,
            this, &UIContextMenuNavigationAction::sltGoBackward);
    connect(m_pForwardButton, &QIToolButton::pressed,
            this, &UIContextMenuNavigationAction::sltGoForward);
    connect(m_pHomeButton, &QIToolButton::pressed,
            this, &UIContextMenuNavigationAction::sltGoHome);
    connect(m_pReloadPageButton, &QIToolButton::pressed,
            this, &UIContextMenuNavigationAction::sltReloadPage);
    connect(m_pAddBookmarkButton, &QIToolButton::pressed,
            this, &UIContextMenuNavigationAction::sltAddBookmark);
    connect(m_pReloadPageButton, &QIToolButton::pressed,
            this, &UIContextMenuNavigationAction::sltAddBookmark);
}


/*********************************************************************************************************************************
*   UIFindInPageWidget implementation.                                                                                           *
*********************************************************************************************************************************/
UIFindInPageWidget::UIFindInPageWidget(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pSearchLineEdit(0)
    , m_pNextButton(0)
    , m_pPreviousButton(0)
    , m_pCloseButton(0)
    , m_previousMousePosition(-1, -1)
{
    prepare();
}

void UIFindInPageWidget::setMatchCountAndCurrentIndex(int iTotalMatchCount, int iCurrentlyScrolledIndex)
{
    if (!m_pSearchLineEdit)
        return;
    m_pSearchLineEdit->setMatchCount(iTotalMatchCount);
    m_pSearchLineEdit->setScrollToIndex(iCurrentlyScrolledIndex);
}

void UIFindInPageWidget::clearSearchField()
{
    if (!m_pSearchLineEdit)
        return;
    m_pSearchLineEdit->blockSignals(true);
    m_pSearchLineEdit->reset();
    m_pSearchLineEdit->blockSignals(false);
}

bool UIFindInPageWidget::eventFilter(QObject *pObject, QEvent *pEvent)
{
    if (pObject == m_pDragMoveLabel)
    {
        if (pEvent->type() == QEvent::Enter)
            m_pDragMoveLabel->setCursor(Qt::CrossCursor);
        else if (pEvent->type() == QEvent::Leave)
        {
            if (parentWidget())
                m_pDragMoveLabel->setCursor(parentWidget()->cursor());
        }
        else if (pEvent->type() == QEvent::MouseMove)
        {
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent*>(pEvent);
            const QPoint gPos = pMouseEvent->globalPosition().toPoint();
            if (pMouseEvent->buttons() == Qt::LeftButton)
            {
                if (m_previousMousePosition != QPoint(-1, -1))
                    emit sigDragging(gPos - m_previousMousePosition);
                m_previousMousePosition = gPos;
                m_pDragMoveLabel->setCursor(Qt::ClosedHandCursor);
            }
        }
        else if (pEvent->type() == QEvent::MouseButtonRelease)
        {
            m_previousMousePosition = QPoint(-1, -1);
            m_pDragMoveLabel->setCursor(Qt::CrossCursor);
        }
    }
    return QWidget::eventFilter(pObject, pEvent);
}

void UIFindInPageWidget::keyPressEvent(QKeyEvent *pEvent)
{
    switch (pEvent->key())
    {
        case  Qt::Key_Escape:
            emit sigClose();
            return;
            break;
        case Qt::Key_Down:
            emit sigSelectNextMatch();
            return;
            break;
        case Qt::Key_Up:
            emit sigSelectPreviousMatch();
            return;
            break;
        default:
            QWidget::keyPressEvent(pEvent);
            break;
    }
}

void UIFindInPageWidget::prepare()
{
    setAutoFillBackground(true);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);

    QHBoxLayout *pLayout = new QHBoxLayout(this);
    m_pSearchLineEdit = new UISearchLineEdit;
    AssertReturnVoid(pLayout && m_pSearchLineEdit);
    setFocusProxy(m_pSearchLineEdit);
    const QFontMetrics fontMetric(m_pSearchLineEdit->font());
    setMinimumSize(40 * fontMetric.horizontalAdvance("x"),
                   fontMetric.height() +
                   qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) +
                   qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin));
    connect(m_pSearchLineEdit, &UISearchLineEdit::textChanged,
            this, &UIFindInPageWidget::sigSearchTextChanged);

    m_pDragMoveLabel = new QLabel;
    AssertReturnVoid(m_pDragMoveLabel);
    m_pDragMoveLabel->installEventFilter(this);
    m_pDragMoveLabel->setPixmap(QPixmap(":/drag_move_16px.png"));
    pLayout->addWidget(m_pDragMoveLabel);


    pLayout->setSpacing(0);
    pLayout->addWidget(m_pSearchLineEdit);

    m_pPreviousButton = new QIToolButton;
    m_pNextButton = new QIToolButton;
    m_pCloseButton = new QIToolButton;

    pLayout->addWidget(m_pPreviousButton);
    pLayout->addWidget(m_pNextButton);
    pLayout->addWidget(m_pCloseButton);

    m_pPreviousButton->setIcon(UIIconPool::iconSet(":/arrow_up_10px.png"));
    m_pNextButton->setIcon(UIIconPool::iconSet(":/arrow_down_10px.png"));
    m_pCloseButton->setIcon(UIIconPool::iconSet(":/close_16px.png"));

    connect(m_pPreviousButton, &QIToolButton::pressed, this, &UIFindInPageWidget::sigSelectPreviousMatch);
    connect(m_pNextButton, &QIToolButton::pressed, this, &UIFindInPageWidget::sigSelectNextMatch);
    connect(m_pCloseButton, &QIToolButton::pressed, this, &UIFindInPageWidget::sigClose);
}

/*********************************************************************************************************************************
*   UIHelpViewer implementation.                                                                                          *
*********************************************************************************************************************************/

UIHelpViewer::UIHelpViewer(const QHelpEngine *pHelpEngine, QWidget *pParent /* = 0 */)
    : QTextBrowser(pParent)
    , m_pHelpEngine(pHelpEngine)
    , m_pFindInPageWidget(new UIFindInPageWidget(this))
    , m_fFindWidgetDragged(false)
    , m_iMarginForFindWidget(qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin))
    , m_iSelectedMatchIndex(0)
    , m_iSearchTermLength(0)
    , m_fOverlayMode(false)
    , m_pOverlayLabel(0)
    , m_iZoomPercentage(100)
{
    m_iInitialFontPointSize = font().pointSize();
    setUndoRedoEnabled(true);
    connect(m_pFindInPageWidget, &UIFindInPageWidget::sigDragging,
            this, &UIHelpViewer::sltFindWidgetDrag);
    connect(m_pFindInPageWidget, &UIFindInPageWidget::sigSearchTextChanged,
            this, &UIHelpViewer::sltFindInPageSearchTextChange);

    connect(m_pFindInPageWidget, &UIFindInPageWidget::sigSelectPreviousMatch,
            this, &UIHelpViewer::sltSelectPreviousMatch);
    connect(m_pFindInPageWidget, &UIFindInPageWidget::sigSelectNextMatch,
            this, &UIHelpViewer::sltSelectNextMatch);
    connect(m_pFindInPageWidget, &UIFindInPageWidget::sigClose,
            this, &UIHelpViewer::sltCloseFindInPageWidget);
    connect(this, &UIHelpViewer::highlighted,
            this, &UIHelpViewer::sltUpdateHighlightedURL);

    m_pFindInPageWidget->setVisible(false);

    m_pOverlayLabel = new QLabel(this);
    if (m_pOverlayLabel)
    {
        m_pOverlayLabel->hide();
        m_pOverlayLabel->installEventFilter(this);
    }

    m_pOverlayBlurEffect = new QGraphicsBlurEffect(this);
    if (m_pOverlayBlurEffect)
    {
        viewport()->setGraphicsEffect(m_pOverlayBlurEffect);
        m_pOverlayBlurEffect->setEnabled(false);
        m_pOverlayBlurEffect->setBlurRadius(8);
    }
}

QVariant UIHelpViewer::loadResource(int type, const QUrl &name)
{
    if (name.scheme() == "qthelp" && m_pHelpEngine)
        return QVariant(m_pHelpEngine->fileData(name));
    else
        return QTextBrowser::loadResource(type, name);
}

void UIHelpViewer::emitHistoryChangedSignal()
{
    emit historyChanged();
    emit backwardAvailable(true);
}

void UIHelpViewer::doSetSource(const QUrl &url, QTextDocument::ResourceType type)
{
    clearOverlay();
    if (url.scheme() != "qthelp")
        return;
    QTextBrowser::doSetSource(url, type);
    QTextDocument *pDocument = document();
    if (!pDocument || pDocument->isEmpty())
    {
        setText(UIHelpBrowserWidget::tr("<div><p><h3>Not found.</h3>The page <b>%1</b> could not be found.</p></div>").arg(url.toString()));
        setDocumentTitle(UIHelpBrowserWidget::tr("Not Found"));
    }
    if (m_pFindInPageWidget && m_pFindInPageWidget->isVisible())
    {
        document()->undo();
        m_pFindInPageWidget->clearSearchField();
    }
    iterateDocumentImages();
    scaleImages();
}

void UIHelpViewer::toggleFindInPageWidget(bool fVisible)
{
    if (!m_pFindInPageWidget)
        return;

    /* Closing the find in page widget causes QTextBrowser to jump to the top of the document. This hack puts it back into position: */
    int iPosition = verticalScrollBar()->value();
    m_iMarginForFindWidget = verticalScrollBar()->width() +
        qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
    /* Try to position the widget somewhere meaningful initially: */
    if (!m_fFindWidgetDragged)
        m_pFindInPageWidget->move(width() - m_iMarginForFindWidget - m_pFindInPageWidget->width(),
                                  m_iMarginForFindWidget);

    m_pFindInPageWidget->setVisible(fVisible);

    if (!fVisible)
    {
        /* Clear highlights: */
        setExtraSelections(QList<QTextEdit::ExtraSelection>());
        m_pFindInPageWidget->clearSearchField();
        verticalScrollBar()->setValue(iPosition);
    }
    else
        m_pFindInPageWidget->setFocus();
    emit sigFindInPageWidgetToogle(fVisible);
}

void UIHelpViewer::reload()
{
    setSource(source());
}

void UIHelpViewer::sltToggleFindInPageWidget(bool fVisible)
{
    clearOverlay();
    toggleFindInPageWidget(fVisible);
}

void UIHelpViewer::sltCloseFindInPageWidget()
{
    sltToggleFindInPageWidget(false);
}

void UIHelpViewer::sltUpdateHighlightedURL(const QUrl &url)
{
    m_highlightedUrl = url;
}

void UIHelpViewer::setFont(const QFont &font)
{
    QTextBrowser::setFont(font);
    /* Make sure the font size of the find in widget stays constant: */
    if (m_pFindInPageWidget)
    {
        QFont wFont(font);
        wFont.setPointSize(m_iInitialFontPointSize);
        m_pFindInPageWidget->setFont(wFont);
    }
}

bool UIHelpViewer::isFindInPageWidgetVisible() const
{
    if (m_pFindInPageWidget)
        return m_pFindInPageWidget->isVisible();
    return false;
}

void UIHelpViewer::setZoomPercentage(int iZoomPercentage)
{
    m_iZoomPercentage = iZoomPercentage;
    clearOverlay();
    scaleFont();
    scaleImages();
}

void UIHelpViewer::setHelpFileList(const QList<QUrl> &helpFileList)
{
    m_helpFileList = helpFileList;
    /* File list necessary to get the image data from the help engine: */
    iterateDocumentImages();
    scaleImages();
}

bool UIHelpViewer::hasSelectedText() const
{
    return textCursor().hasSelection();
}

void UIHelpViewer::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu;

    if (textCursor().hasSelection())
    {
        QAction *pCopySelectedTextAction = new QAction(UIHelpBrowserWidget::tr("Copy Selected Text"));
        connect(pCopySelectedTextAction, &QAction::triggered,
                this, &UIHelpViewer::copy);
        menu.addAction(pCopySelectedTextAction);
        menu.addSeparator();
    }

    UIContextMenuNavigationAction *pNavigationActions = new UIContextMenuNavigationAction;
    pNavigationActions->setBackwardAvailable(isBackwardAvailable());
    pNavigationActions->setForwardAvailable(isForwardAvailable());

    connect(pNavigationActions, &UIContextMenuNavigationAction::sigGoBackward,
            this, &UIHelpViewer::sigGoBackward);
    connect(pNavigationActions, &UIContextMenuNavigationAction::sigGoForward,
            this, &UIHelpViewer::sigGoForward);
    connect(pNavigationActions, &UIContextMenuNavigationAction::sigGoHome,
            this, &UIHelpViewer::sigGoHome);
    connect(pNavigationActions, &UIContextMenuNavigationAction::sigReloadPage,
            this, &UIHelpViewer::reload);
    connect(pNavigationActions, &UIContextMenuNavigationAction::sigAddBookmark,
            this, &UIHelpViewer::sigAddBookmark);

    QAction *pOpenLinkAction = new QAction(UIHelpBrowserWidget::tr("Open Link"));
    connect(pOpenLinkAction, &QAction::triggered,
            this, &UIHelpViewer::sltOpenLink);

    QAction *pOpenInNewTabAction = new QAction(UIHelpBrowserWidget::tr("Open Link in New Tab"));
    connect(pOpenInNewTabAction, &QAction::triggered,
            this, &UIHelpViewer::sltOpenLinkInNewTab);

    QAction *pCopyLink = new QAction(UIHelpBrowserWidget::tr("Copy Link"));
    connect(pCopyLink, &QAction::triggered,
            this, &UIHelpViewer::sltCopyLink);

    QAction *pFindInPage = new QAction(UIHelpBrowserWidget::tr("Find in Page"));
    pFindInPage->setCheckable(true);
    if (m_pFindInPageWidget)
        pFindInPage->setChecked(m_pFindInPageWidget->isVisible());
    connect(pFindInPage, &QAction::toggled, this, &UIHelpViewer::sltToggleFindInPageWidget);

    menu.addAction(pNavigationActions);
    menu.addAction(pOpenLinkAction);
    menu.addAction(pOpenInNewTabAction);
    menu.addAction(pCopyLink);
    menu.addAction(pFindInPage);

    if (!m_highlightedUrl.isEmpty())
    {
        QString strLink = m_highlightedUrl.toString();
        pOpenLinkAction->setData(strLink);
        pOpenInNewTabAction->setData(strLink);
        pCopyLink->setData(strLink);
        if (m_highlightedUrl.scheme() == "https" ||
            m_highlightedUrl.scheme() == "http")
            pOpenInNewTabAction->setEnabled(false);
        if (isImage(strLink))
            pOpenInNewTabAction->setEnabled(false);
    }
    else
    {
        pOpenLinkAction->setEnabled(false);
        pOpenInNewTabAction->setEnabled(false);
        pCopyLink->setEnabled(false);
    }

    menu.exec(event->globalPos());
}

void UIHelpViewer::resizeEvent(QResizeEvent *pEvent)
{
    if (m_fOverlayMode)
        clearOverlay();
    /* Make sure the widget stays inside the parent during parent resize: */
    if (m_pFindInPageWidget)
    {
        if (!isRectInside(m_pFindInPageWidget->geometry(), m_iMarginForFindWidget))
            moveFindWidgetIn(m_iMarginForFindWidget);
    }
    QTextBrowser::resizeEvent(pEvent);
}

void UIHelpViewer::wheelEvent(QWheelEvent *pEvent)
{
    if (m_fOverlayMode && !pEvent)
        return;
    /* QTextBrowser::wheelEvent scales font when some modifiers are pressed. We dont want that: */
    if (pEvent->modifiers() == Qt::NoModifier)
        QTextBrowser::wheelEvent(pEvent);
    else if (pEvent->modifiers() & Qt::ControlModifier)
    {
        if (pEvent->angleDelta().y() > 0)
            emit sigZoomRequest(ZoomOperation_In);
        else if (pEvent->angleDelta().y() < 0)
            emit sigZoomRequest(ZoomOperation_Out);
    }
}

void UIHelpViewer::mouseReleaseEvent(QMouseEvent *pEvent)
{
    /* If overlay mode is active just clear it and return: */
    bool fOverlayMode = m_fOverlayMode;
    clearOverlay();
    if (fOverlayMode)
        return;
    QString strAnchor = anchorAt(pEvent->position().toPoint());

    if (!strAnchor.isEmpty())
    {
        QString strLink = source().resolved(strAnchor).toString();
        if (isImage(strLink))
        {
            loadImage(source().resolved(strAnchor));
            return;
        }
        if (source().resolved(strAnchor).scheme() != "qthelp" && pEvent->button() == Qt::LeftButton)
        {
            uiCommon().openURL(strLink);
            return;
        }

        if ((pEvent->modifiers() & Qt::ControlModifier) ||
            pEvent->button() == Qt::MiddleButton)
        {

            emit sigOpenLinkInNewTab(strLink, true);
            return;
        }
    }
    QTextBrowser::mousePressEvent(pEvent);
}

void UIHelpViewer::mousePressEvent(QMouseEvent *pEvent)
{
    QTextBrowser::mousePressEvent(pEvent);
}

void UIHelpViewer::mouseMoveEvent(QMouseEvent *pEvent)
{
    /*if (m_fOverlayMode)
        return;*/
    QTextBrowser::mouseMoveEvent(pEvent);
}

void UIHelpViewer::mouseDoubleClickEvent(QMouseEvent *pEvent)
{
    clearOverlay();
    QTextBrowser::mouseDoubleClickEvent(pEvent);
}

void UIHelpViewer::paintEvent(QPaintEvent *pEvent)
{
    QTextBrowser::paintEvent(pEvent);
    QPainter painter(viewport());
    foreach(const DocumentImage &image, m_imageMap)
    {
        QRect rect = cursorRect(image.m_textCursor);
        QPixmap newPixmap = image.m_pixmap.scaledToWidth(image.m_fScaledWidth, Qt::SmoothTransformation);
        QRectF imageRect(rect.x() - newPixmap.width(), rect.y(), newPixmap.width(), newPixmap.height());

        int iMargin = 3;
        QRectF fillRect(imageRect.x() - iMargin, imageRect.y() - iMargin,
                        imageRect.width() + 2 * iMargin, imageRect.height() + 2 * iMargin);
        /** @todo I need to find the default color somehow and replace hard coded Qt::white. */
        painter.fillRect(fillRect, Qt::white);
        painter.drawPixmap(imageRect, newPixmap, newPixmap.rect());
    }
}

bool UIHelpViewer::eventFilter(QObject *pObject, QEvent *pEvent)
{
    if (pObject == m_pOverlayLabel)
    {
        if (pEvent->type() == QEvent::MouseButtonPress ||
            pEvent->type() == QEvent::MouseButtonDblClick)
            clearOverlay();
    }
    return QTextBrowser::eventFilter(pObject, pEvent);
}

void UIHelpViewer::keyPressEvent(QKeyEvent *pEvent)
{
    if (pEvent && pEvent->key() == Qt::Key_Escape)
        clearOverlay();
    if (pEvent && pEvent->modifiers() &Qt::ControlModifier)
    {
        switch (pEvent->key())
        {
            case Qt::Key_Equal:
                emit sigZoomRequest(ZoomOperation_In);
                break;
            case Qt::Key_Minus:
                emit sigZoomRequest(ZoomOperation_Out);
                break;
            case Qt::Key_0:
                emit sigZoomRequest(ZoomOperation_Reset);
                break;
            default:
                break;
        }
    }
    QTextBrowser::keyPressEvent(pEvent);
}

void UIHelpViewer::moveFindWidgetIn(int iMargin)
{
    if (!m_pFindInPageWidget)
        return;

    QRect  rect = m_pFindInPageWidget->geometry();
    if (rect.left() < iMargin)
        rect.translate(-rect.left() + iMargin, 0);
    if (rect.right() > width() - iMargin)
        rect.translate((width() - iMargin - rect.right()), 0);
    if (rect.top() < iMargin)
        rect.translate(0, -rect.top() + iMargin);

    if (rect.bottom() > height() - iMargin)
        rect.translate(0, (height() - iMargin - rect.bottom()));
    m_pFindInPageWidget->setGeometry(rect);
    m_pFindInPageWidget->update();
}

bool UIHelpViewer::isRectInside(const QRect &rect, int iMargin) const
{
    if (rect.left() < iMargin || rect.top() < iMargin)
        return false;
    if (rect.right() > width() - iMargin || rect.bottom() > height() - iMargin)
        return false;
    return true;
}

void UIHelpViewer::findAllMatches(const QString &searchString)
{
    QTextDocument *pDocument = document();
    AssertReturnVoid(pDocument);

    m_matchedCursorPosition.clear();
    if (searchString.isEmpty())
        return;
    QTextCursor cursor(pDocument);
    QTextDocument::FindFlags flags;
    while (!cursor.isNull() && !cursor.atEnd())
    {
        cursor = pDocument->find(searchString, cursor, flags);
        if (!cursor.isNull())
            m_matchedCursorPosition << cursor.position() - searchString.length();
    }
}

void UIHelpViewer::highlightFinds(int iSearchTermLength)
{
    QList<QTextEdit::ExtraSelection> extraSelections;
    for (int i = 0; i < m_matchedCursorPosition.size(); ++i)
    {
        QTextEdit::ExtraSelection selection;
        QTextCursor cursor = textCursor();
        cursor.setPosition(m_matchedCursorPosition[i]);
        cursor.setPosition(m_matchedCursorPosition[i] + iSearchTermLength, QTextCursor::KeepAnchor);
        QTextCharFormat format = cursor.charFormat();
        format.setBackground(Qt::yellow);

        selection.cursor = cursor;
        selection.format = format;
        extraSelections.append(selection);
    }
    setExtraSelections(extraSelections);
}

void UIHelpViewer::selectMatch(int iMatchIndex, int iSearchStringLength)
{
    QTextCursor cursor = textCursor();
    /* Move the cursor to the beginning of the matched string: */
    cursor.setPosition(m_matchedCursorPosition.at(iMatchIndex), QTextCursor::MoveAnchor);
    /* Move the cursor to the end of the matched string while keeping the anchor at the begining thus selecting the text: */
    cursor.setPosition(m_matchedCursorPosition.at(iMatchIndex) + iSearchStringLength, QTextCursor::KeepAnchor);
    ensureCursorVisible();
    setTextCursor(cursor);
}

void UIHelpViewer::sltOpenLinkInNewTab()
{
    QAction *pSender = qobject_cast<QAction*>(sender());
    if (!pSender)
        return;
    QUrl url = pSender->data().toUrl();
    if (url.isValid())
        emit sigOpenLinkInNewTab(url, false);
}

void UIHelpViewer::sltOpenLink()
{
    QAction *pSender = qobject_cast<QAction*>(sender());
    if (!pSender)
        return;
    QUrl url = pSender->data().toUrl();
    if (isImage(url.toString()))
    {
        loadImage(url);
        return;
    }
    if (url.isValid())
        setSource(url);
}

void UIHelpViewer::sltCopyLink()
{
    QAction *pSender = qobject_cast<QAction*>(sender());
    if (!pSender)
        return;
    QUrl url = pSender->data().toUrl();
    if (url.isValid())
    {
        QClipboard *pClipboard = QApplication::clipboard();
        if (pClipboard)
            pClipboard->setText(url.toString());
    }
}

void UIHelpViewer::sltFindWidgetDrag(const QPoint &delta)
{
    if (!m_pFindInPageWidget)
        return;
    QRect geo = m_pFindInPageWidget->geometry();
    geo.translate(delta);

    /* Allow the move if m_pFindInPageWidget stays inside after the move: */
    if (isRectInside(geo, m_iMarginForFindWidget))
        m_pFindInPageWidget->move(m_pFindInPageWidget->pos() + delta);
    m_fFindWidgetDragged = true;
    update();
}

void UIHelpViewer::sltFindInPageSearchTextChange(const QString &strSearchText)
{
    m_iSearchTermLength = strSearchText.length();
    findAllMatches(strSearchText);
    highlightFinds(m_iSearchTermLength);
    selectMatch(0, m_iSearchTermLength);
    if (m_pFindInPageWidget)
        m_pFindInPageWidget->setMatchCountAndCurrentIndex(m_matchedCursorPosition.size(), 0);
}

void UIHelpViewer::sltSelectPreviousMatch()
{
    m_iSelectedMatchIndex = m_iSelectedMatchIndex <= 0 ? m_matchedCursorPosition.size() - 1 : (m_iSelectedMatchIndex - 1);
    selectMatch(m_iSelectedMatchIndex, m_iSearchTermLength);
    if (m_pFindInPageWidget)
        m_pFindInPageWidget->setMatchCountAndCurrentIndex(m_matchedCursorPosition.size(), m_iSelectedMatchIndex);
}

void UIHelpViewer::sltSelectNextMatch()
{
    m_iSelectedMatchIndex = m_iSelectedMatchIndex >= m_matchedCursorPosition.size() - 1 ? 0 : (m_iSelectedMatchIndex + 1);
    selectMatch(m_iSelectedMatchIndex, m_iSearchTermLength);
    if (m_pFindInPageWidget)
        m_pFindInPageWidget->setMatchCountAndCurrentIndex(m_matchedCursorPosition.size(), m_iSelectedMatchIndex);
}

void UIHelpViewer::iterateDocumentImages()
{
    m_imageMap.clear();
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::Start);
    while (!cursor.atEnd())
    {
        cursor.movePosition(QTextCursor::NextCharacter);
        if (cursor.charFormat().isImageFormat())
        {
            QTextImageFormat imageFormat = cursor.charFormat().toImageFormat();
            /* There seems to be two cursors per image. Use the first one: */
            if (m_imageMap.contains(imageFormat.name()))
                continue;
            QHash<QString, DocumentImage>::iterator iterator = m_imageMap.insert(imageFormat.name(), DocumentImage());
            DocumentImage &image = iterator.value();
            /* QtextImageFormat::width() returns something meaningful only if width is set explicitly in html code, else it returns 0:*/
            image.m_fInitialWidth = imageFormat.width();
            image.m_strName = imageFormat.name();
            image.m_textCursor = cursor;
            QUrl imageFileUrl;
            foreach (const QUrl &fileUrl, m_helpFileList)
            {
                if (fileUrl.toString().contains(imageFormat.name(), Qt::CaseInsensitive))
                {
                    imageFileUrl = fileUrl;
                    break;
                }
            }
            if (imageFileUrl.isValid())
            {
                QByteArray fileData = m_pHelpEngine->fileData(imageFileUrl);
                if (!fileData.isEmpty())
                    image.m_pixmap.loadFromData(fileData,"PNG");
            }
        }
    }
}

void UIHelpViewer::scaleFont()
{
    QFont mFont = font();
    mFont.setPointSize(m_iInitialFontPointSize * m_iZoomPercentage / 100.);
    setFont(mFont);
}

void UIHelpViewer::scaleImages()
{
    for (QHash<QString, DocumentImage>::iterator iterator = m_imageMap.begin();
         iterator != m_imageMap.end(); ++iterator)
    {
        DocumentImage &image = *iterator;
        if (image.m_fInitialWidth <= 0)
            continue;
        QTextCursor cursor = image.m_textCursor;
        QTextCharFormat format = cursor.charFormat();
        if (!format.isImageFormat())
            continue;
        QTextImageFormat imageFormat = format.toImageFormat();
        image.m_fScaledWidth = image.m_fInitialWidth * m_iZoomPercentage / 100.;
        imageFormat.setWidth(image.m_fScaledWidth);
        cursor.deletePreviousChar();
        cursor.deleteChar();
        cursor.insertImage(imageFormat);
    }
}

void UIHelpViewer::clearOverlay()
{
    AssertReturnVoid(m_pOverlayLabel);

    if (!m_fOverlayMode)
        return;
    m_overlayPixmap = QPixmap();
    m_fOverlayMode = false;
    if (m_pOverlayBlurEffect)
        m_pOverlayBlurEffect->setEnabled(false);
    m_pOverlayLabel->hide();
}

void UIHelpViewer::enableOverlay()
{
    AssertReturnVoid(m_pOverlayLabel);
    m_fOverlayMode = true;
    if (m_pOverlayBlurEffect)
        m_pOverlayBlurEffect->setEnabled(true);
    toggleFindInPageWidget(false);

    /* Scale the image to 1:1 as long as it fits into avaible space (minus some margins and scrollbar sizes): */
    int vWidth = 0;
    if (verticalScrollBar() && verticalScrollBar()->isVisible())
        vWidth = verticalScrollBar()->width();
    int hMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) +
        qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) + vWidth;

    int hHeight = 0;
    if (horizontalScrollBar() && horizontalScrollBar()->isVisible())
        hHeight = horizontalScrollBar()->height();
    int vMargin = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) +
        qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) + hHeight;

    QSize size(qMin(width() - hMargin, m_overlayPixmap.width()),
               qMin(height() - vMargin, m_overlayPixmap.height()));
    m_pOverlayLabel->setPixmap(m_overlayPixmap.scaled(size,  Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_pOverlayLabel->show();

    /* Center the label: */
    int x = 0.5 * (width() - vWidth - m_pOverlayLabel->width());
    int y = 0.5 * (height() - hHeight - m_pOverlayLabel->height());
    m_pOverlayLabel->move(x, y);
}

void UIHelpViewer::loadImage(const QUrl &imageFileUrl)
{
    clearOverlay();
    /* Dont zoom into image if mouse button released after a mouse drag: */
    if (textCursor().hasSelection())
        return;
    if (!imageFileUrl.isValid())
        return;
    QByteArray fileData = m_pHelpEngine->fileData(imageFileUrl);
    if (!fileData.isEmpty())
    {
        m_overlayPixmap.loadFromData(fileData,"PNG");
        if (!m_overlayPixmap.isNull())
            enableOverlay();
    }
}

bool UIHelpViewer::isImage(const QString &strLink)
{
    if (strLink.isEmpty())
        return false;
    QFileInfo fInfo(strLink);
    QMimeDatabase base;
    QMimeType type = base.mimeTypeForFile(fInfo);
    if (type.isValid() && type.inherits("image/png"))
        return true;
    return false;
}

#include "UIHelpViewer.moc"
