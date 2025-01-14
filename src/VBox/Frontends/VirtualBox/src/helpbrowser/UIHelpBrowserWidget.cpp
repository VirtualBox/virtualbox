/* $Id: UIHelpBrowserWidget.cpp 107805 2025-01-14 08:28:19Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHelpBrowserWidget class implementation.
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
#include <QComboBox>
#include <QtGlobal>
#include <QtHelp/QHelpContentWidget>
#include <QtHelp/QHelpEngine>
#include <QtHelp/QHelpLink>
#include <QtHelp/QHelpSearchEngine>
#include <QtHelp/QHelpSearchQueryWidget>
#include <QtHelp/QHelpSearchResultWidget>

#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QtPrintSupport/QPrintDialog>
#include <QtPrintSupport/QPrinter>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidgetAction>

/* GUI includes: */
#include "QITabWidget.h"
#include "QIToolBar.h"
#include "QIToolButton.h"
#include "UICommon.h"
#include "UIExtraDataManager.h"
#include "UIHelpViewer.h"
#include "UIHelpBrowserWidget.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>


enum HelpBrowserTabs
{
    HelpBrowserTabs_TOC = 0,
    HelpBrowserTabs_Search,
    HelpBrowserTabs_Bookmarks,
    HelpBrowserTabs_Index,
    HelpBrowserTabs_Max
};
Q_DECLARE_METATYPE(HelpBrowserTabs);

static const int iBookmarkUrlDataType = 6;

static int iZoomPercentageStep = 20;
const QPair<int, int> zoomPercentageMinMax = QPair<int, int>(20, 300);


/*********************************************************************************************************************************
*   UIZoomMenuAction definition.                                                                                                 *
*********************************************************************************************************************************/
class UIZoomMenuAction : public QWidgetAction
{

    Q_OBJECT;

signals:

    void sigZoomChanged(int iOperation);

public:

    UIZoomMenuAction(QWidget *pParent = 0);
    void setZoomPercentage(int iZoomPercentage);

private slots:

    void sltZoomOperation();
    void sltRetranslateUI();

private:

    void prepare();

    QIToolButton *m_pMinusButton;
    QIToolButton *m_pResetButton;
    QIToolButton *m_pPlusButton;
    QLabel *m_pValueLabel;
    QLabel *m_pLabel;
};


/*********************************************************************************************************************************
*   UIBookmarksListWidget definition.                                                                                            *
*********************************************************************************************************************************/
class UIBookmarksListWidget : public QListWidget
{

    Q_OBJECT;

signals:

    void sigBookmarkDoubleClick(const QUrl &url);

public:

    UIBookmarksListWidget(QWidget *pParent = 0);

protected:

    void mouseDoubleClickEvent(QMouseEvent *event) RT_OVERRIDE;
    void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
};


/*********************************************************************************************************************************
*   UIBookmarksListContainer definition.                                                                                         *
*********************************************************************************************************************************/
class UIBookmarksListContainer : public QWidget
{

    Q_OBJECT;

signals:

    void sigBookmarkDoubleClick(const QUrl &url);
    void sigListWidgetContextMenuRequest(const QPoint &listWidgetLocalPos);

public:

    UIBookmarksListContainer(QWidget *pParent = 0);
    void addBookmark(const QUrl &url, const QString &strTitle);
    /** Return all bookmarks a url, title pair list. */
    QStringList bookmarks() const;
    QUrl currentBookmarkUrl();

public:

    void sltDeleteSelectedBookmark();
    void sltDeleteAllBookmarks();

private:

    void prepare();
    int itemIndex(const QUrl &url);

    QVBoxLayout  *m_pMainLayout;
    UIBookmarksListWidget  *m_pListWidget;
};

/*********************************************************************************************************************************
*   UIHelpBrowserTab definition.                                                                                        *
*********************************************************************************************************************************/

class UIHelpBrowserTab : public QWidget
{
    Q_OBJECT;

signals:

    void sigSourceChanged(const QUrl &url);
    void sigCopyAvailableChanged(bool fAvailable);
    void sigTitleUpdate(const QString &strTitle);
    void sigOpenLinkInNewTab(const QUrl &url, bool fBackground);
    void sigAddBookmark(const QUrl &url, const QString &strTitle);
    void sigLinkHighlighted(const QUrl &url);
    void sigFindInPageWidgetVisibilityChanged(bool fVisible);
    void sigHistoryChanged(bool fBackwardAvailable, bool fForwardAvailable);
    void sigMouseOverImage(const QString &strImageName);
    void sigZoomRequest(UIHelpViewer::ZoomOperation enmZoomOperation);

public:

    UIHelpBrowserTab(const QHelpEngine  *pHelpEngine, const QUrl &homeUrl,
                     const QUrl &initialUrl, QWidget *pParent = 0);

    QUrl source() const;
    void setSource(const QUrl &url);
    QString documentTitle() const;
    void setToolBarVisible(bool fVisible);
    void print(QPrinter &printer);
    void setZoomPercentage(int iZoomPercentage);
    void setHelpFileList(const QList<QUrl> &helpFileList);
    void copySelectedText() const;
    bool hasSelectedText() const;
    bool isFindInPageWidgetVisible() const;
    void findNext();
    void findPrevious();

public slots:

    void sltFindInPageAction(bool fToggled);
    void sltHomeAction();
    void sltForwardAction();
    void sltBackwardAction();
    void sltAddBookmarkAction();
    void sltReloadPageAction();

private slots:

    void sltHistoryChanged();
    void sltAddressBarIndexChanged(int index);
    void sltAnchorClicked(const QUrl &link);
    void sltFindInPageWidgetVisibilityChanged(bool  fVisible);
    void sltRetranslateUI();

private:

    void prepare(const QUrl &initialUrl);
    void prepareWidgets(const QUrl &initialUrl);
    void prepareToolBarAndAddressBar();
    void setActionTextAndToolTip(QAction *pAction, const QString &strText, const QString &strToolTip);

    QAction     *m_pHomeAction;
    QAction     *m_pForwardAction;
    QAction     *m_pBackwardAction;
    QAction     *m_pAddBookmarkAction;
    QAction     *m_pFindInPageAction;
    QAction     *m_pReloadPageAction;

    QVBoxLayout *m_pMainLayout;
    QIToolBar   *m_pToolBar;
    QComboBox   *m_pAddressBar;
    UIHelpViewer *m_pContentViewer;
    const QHelpEngine* m_pHelpEngine;
    QUrl m_homeUrl;
};


/*********************************************************************************************************************************
*   UIHelpBrowserTabManager definition.                                                                                          *
*********************************************************************************************************************************/

class UIHelpBrowserTabManager : public QITabWidget
{
    Q_OBJECT;

signals:

    void sigSourceChanged(const QUrl &url);
    void sigAddBookmark(const QUrl &url, const QString &strTitle);
    /** list.first is tab title and list.second is tab's index. */
    void sigTabsListChanged(const QStringList &titleList);
    void sigLinkHighlighted(const QUrl &url);
    void sigZoomPercentageChanged(int iPercentage);
    void sigCopyAvailableChanged(bool fAvailable);
    void sigFindInPageWidgetVisibilityChanged(bool fVisible);
    void sigHistoryChanged(bool fBackwardAvailable, bool fForwardAvailable);
    void sigMouseOverImage(const QString &strImageName);

public:

    UIHelpBrowserTabManager(const QHelpEngine  *pHelpEngine, const QUrl &homeUrl,
                            const QStringList &urlList, QWidget *pParent = 0);
    /* Returns the list of urls of all open tabs as QStringList. */
    QStringList tabUrlList() const;
    QStringList tabTitleList() const;

    /** Either start with a single tab showin the home url or saved tab(s). Depending on the params. passed to ctor. */
    void initializeTabs();
    /* Url of the current tab. */
    QUrl currentSource() const;
    void setSource(const QUrl &url, bool fNewTab = false);
    void setToolBarVisible(bool fVisible);
    void printCurrent(QPrinter &printer);
    void switchToTab(int iIndex);
    int zoomPercentage() const;
    void setZoomPercentage(int iZoomPercentage);
    void setHelpFileList(const QList<QUrl> &helpFileList);
    void copySelectedText() const;
    bool hasCurrentTabSelectedText() const;
    bool isFindInPageWidgetVisible() const;
    void toggleFindInPage(bool fTrigger);
    void findNext();
    void findPrevious();

public slots:

    void sltCloseCurrentTab();
    void sltCloseOtherTabs();
    void sltHomeAction();
    void sltAddBookmarkAction();
    void sltForwardAction();
    void sltBackwardAction();
    void sltReloadPageAction();
    void sltHandleZoomRequest(UIHelpViewer::ZoomOperation enmOperation);

private slots:

    void sltTabTitleChange(const QString &strTitle);
    void sltOpenLinkInNewTab(const QUrl &url, bool fBackground);
    void sltTabClose(int iTabIndex);
    void sltContextMenuTabClose();
    void sltCurrentChanged(int iTabIndex);
    void sltShowTabBarContextMenu(const QPoint &pos);
    void sltCloseOtherTabsContextMenuAction();
    void sltCopyAvailableChanged(bool fAvailable);

private:

    void prepare();
    void clearAndDeleteTabs();
    void addNewTab(const QUrl &initialUrl, bool fBackground);
    /** Check if lists of tab url/title has changed. if so emit a signal. */
    void updateTabUrlTitleList();
    /** Closes all tabs other than the one with index @param iTabIndex. */
    void closeAllTabsBut(int iTabIndex);
    /* Returns the tab index with @Url if there is one. Returns -1 otherwise. */
    int  findTab(const QUrl &Url) const;

    const QHelpEngine* m_pHelpEngine;
    QUrl m_homeUrl;
    QStringList m_savedUrlList;
    /** Immediately switch the newly created tab. Otherwise open the tab in background. */
    bool m_fSwitchToNewTab;
    bool m_fToolBarVisible;
    QStringList m_tabTitleList;
    QList<QUrl> m_helpFileList;
    /** As percentage. */
    int m_iZoomPercentage;
};


/*********************************************************************************************************************************
*   UIZoomMenuAction implementation.                                                                                *
*********************************************************************************************************************************/
UIZoomMenuAction::UIZoomMenuAction(QWidget *pParent /* = 0 */)
    : QWidgetAction(pParent)
    , m_pMinusButton(0)
    , m_pResetButton(0)
    , m_pPlusButton(0)
    , m_pValueLabel(0)
    , m_pLabel(0)
{
    prepare();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIZoomMenuAction::sltRetranslateUI);
}

void UIZoomMenuAction::setZoomPercentage(int iZoomPercentage)
{
    if (m_pValueLabel)
        m_pValueLabel->setText(QString("%1%2").arg(QString::number(iZoomPercentage)).arg("%"));
}

void UIZoomMenuAction::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(UIHelpBrowserWidget::tr("Zoom"));
}

void UIZoomMenuAction::prepare()
{
    QWidget *pWidget = new QWidget;
    setDefaultWidget(pWidget);

    QHBoxLayout *pMainLayout = new QHBoxLayout(pWidget);
    pMainLayout->setSpacing(0);
    AssertReturnVoid(pMainLayout);

    m_pLabel = new QLabel;
    m_pMinusButton = new QIToolButton;
    m_pResetButton = new QIToolButton;
    m_pPlusButton = new QIToolButton;
    m_pValueLabel = new QLabel;
    m_pValueLabel->setAlignment(Qt::AlignCenter);
    m_pValueLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    AssertReturnVoid(m_pMinusButton &&
                     m_pResetButton &&
                     m_pPlusButton &&
                     m_pValueLabel);

    m_pMinusButton->setIcon(UIIconPool::iconSet(":/help_browser_minus_16px.png", ":/help_browser_minus_disabled_16px.png"));
    m_pResetButton->setIcon(UIIconPool::iconSet(":/help_browser_reset_16px.png", ":/help_browser_reset_disabled_16px.png"));
    m_pPlusButton->setIcon(UIIconPool::iconSet(":/help_browser_plus_16px.png", ":/help_browser_plus_disabled_16px.png"));

    connect(m_pPlusButton, &QIToolButton::pressed, this, &UIZoomMenuAction::sltZoomOperation);
    connect(m_pMinusButton, &QIToolButton::pressed, this, &UIZoomMenuAction::sltZoomOperation);
    connect(m_pResetButton, &QIToolButton::pressed, this, &UIZoomMenuAction::sltZoomOperation);

    pMainLayout->addWidget(m_pLabel);
    pMainLayout->addWidget(m_pResetButton);
    pMainLayout->addWidget(m_pMinusButton);
    pMainLayout->addWidget(m_pValueLabel, Qt::AlignCenter);
    pMainLayout->addWidget(m_pPlusButton);
    setZoomPercentage(100);
}

void UIZoomMenuAction::sltZoomOperation()
{
    if (!sender())
        return;
    UIHelpViewer::ZoomOperation enmOperation = UIHelpViewer::ZoomOperation_In;
    if (sender() == m_pMinusButton)
        enmOperation = UIHelpViewer::ZoomOperation_Out;
    else if (sender() == m_pPlusButton)
        enmOperation = UIHelpViewer::ZoomOperation_In;
    else if (sender() == m_pResetButton)
        enmOperation = UIHelpViewer::ZoomOperation_Reset;
    emit sigZoomChanged((int)enmOperation);
}


/*********************************************************************************************************************************
*   UIBookmarksListWidget implementation.                                                                                        *
*********************************************************************************************************************************/
UIBookmarksListWidget::UIBookmarksListWidget(QWidget *pParent /* = 0 */)
    :QListWidget(pParent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void UIBookmarksListWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    QListWidgetItem *pItem = currentItem();
    if (!pItem)
        return;
    emit sigBookmarkDoubleClick(pItem->data(iBookmarkUrlDataType).toUrl());
    QListWidget::mouseDoubleClickEvent(event);
}

void UIBookmarksListWidget::mousePressEvent(QMouseEvent *pEvent)
{
    if (!indexAt(pEvent->position().toPoint()).isValid())
    {
        clearSelection();
        setCurrentItem(0);
    }
    QListWidget::mousePressEvent(pEvent);
}


/*********************************************************************************************************************************
*   UIBookmarksListContainer implementation.                                                                                     *
*********************************************************************************************************************************/

UIBookmarksListContainer::UIBookmarksListContainer(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pListWidget(0)
{
    prepare();
}

void UIBookmarksListContainer::addBookmark(const QUrl &url, const QString &strTitle)
{
    if (!m_pListWidget)
        return;
    if (itemIndex(url) != -1)
        return;
    QListWidgetItem *pNewItem = new QListWidgetItem(strTitle, m_pListWidget);
    pNewItem->setData(iBookmarkUrlDataType, url);
    pNewItem->setToolTip(url.toString());
}

QStringList UIBookmarksListContainer::bookmarks() const
{
    if (!m_pListWidget)
        return QStringList();
    QStringList bookmarks;
    for (int i = 0; i < m_pListWidget->count(); ++i)
    {
        QListWidgetItem *pItem = m_pListWidget->item(i);
        if (!pItem)
            continue;
        bookmarks << pItem->data(iBookmarkUrlDataType).toUrl().toString() << pItem->text();
    }
    return bookmarks;
}

QUrl UIBookmarksListContainer::currentBookmarkUrl()
{
    if (!m_pListWidget || !m_pListWidget->currentItem())
        return QUrl();
    return m_pListWidget->currentItem()->data(iBookmarkUrlDataType).toUrl();
}

void UIBookmarksListContainer::sltDeleteSelectedBookmark()
{
    if (!m_pListWidget || !m_pListWidget->currentItem())
        return;
    QListWidgetItem *pCurrentItem = m_pListWidget->takeItem(m_pListWidget->currentRow());
    delete pCurrentItem;
}

void UIBookmarksListContainer::sltDeleteAllBookmarks()
{
    if (m_pListWidget)
        m_pListWidget->clear();
}

void UIBookmarksListContainer::prepare()
{
    m_pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(m_pMainLayout);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);

    m_pListWidget = new UIBookmarksListWidget;
    AssertReturnVoid(m_pListWidget);
    m_pMainLayout->addWidget(m_pListWidget);
    m_pListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pListWidget, &UIBookmarksListWidget::sigBookmarkDoubleClick,
            this, &UIBookmarksListContainer::sigBookmarkDoubleClick);
    connect(m_pListWidget, &UIBookmarksListWidget::customContextMenuRequested,
            this, &UIBookmarksListContainer::sigListWidgetContextMenuRequest);
}

int UIBookmarksListContainer::itemIndex(const QUrl &url)
{
    if (!m_pListWidget || !url.isValid())
        return -1;
    for (int i = 0; i < m_pListWidget->count(); ++i)
    {
        if (m_pListWidget->item(i)->data(iBookmarkUrlDataType).toUrl() == url)
            return i;
    }
    return -1;
}

/*********************************************************************************************************************************
*   UIHelpBrowserTab implementation.                                                                                        *
*********************************************************************************************************************************/

UIHelpBrowserTab::UIHelpBrowserTab(const QHelpEngine  *pHelpEngine, const QUrl &homeUrl,
                                   const QUrl &initialUrl, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pHomeAction(0)
    , m_pForwardAction(0)
    , m_pBackwardAction(0)
    , m_pAddBookmarkAction(0)
    , m_pFindInPageAction(0)
    , m_pReloadPageAction(0)
    , m_pMainLayout(0)
    , m_pToolBar(0)
    , m_pAddressBar(0)
    , m_pContentViewer(0)
    , m_pHelpEngine(pHelpEngine)
    , m_homeUrl(homeUrl)
{
    if (initialUrl.isValid())
        prepare(initialUrl);
    else
        prepare(m_homeUrl);
}

QUrl UIHelpBrowserTab::source() const
{
    if (!m_pContentViewer)
        return QUrl();
    return m_pContentViewer->source();
}

void UIHelpBrowserTab::setSource(const QUrl &url)
{
    if (m_pContentViewer)
    {
        m_pContentViewer->blockSignals(true);
        m_pContentViewer->setSource(url);
        m_pContentViewer->blockSignals(false);
        /* emit historyChanged signal explicitly since we have blocked the signals: */
        m_pContentViewer->emitHistoryChangedSignal();
    }
}

QString UIHelpBrowserTab::documentTitle() const
{
    if (!m_pContentViewer)
        return QString();
    return m_pContentViewer->documentTitle();
}

void UIHelpBrowserTab::setToolBarVisible(bool fVisible)
{
    if (m_pToolBar)
        m_pToolBar->setVisible(fVisible);
    if (m_pAddressBar)
        m_pAddressBar->setVisible(fVisible);
}

void UIHelpBrowserTab::print(QPrinter &printer)
{
    if (m_pContentViewer)
        m_pContentViewer->print(&printer);
}

void UIHelpBrowserTab::setZoomPercentage(int iZoomPercentage)
{
    if (m_pContentViewer)
        m_pContentViewer->setZoomPercentage(iZoomPercentage);
}

void UIHelpBrowserTab::setHelpFileList(const QList<QUrl> &helpFileList)
{
    if (m_pContentViewer)
        m_pContentViewer->setHelpFileList(helpFileList);
}

void UIHelpBrowserTab::copySelectedText() const
{
    if (m_pContentViewer && m_pContentViewer->hasSelectedText())
        m_pContentViewer->copy();
}

bool UIHelpBrowserTab::hasSelectedText() const
{
    if (m_pContentViewer)
        return m_pContentViewer->textCursor().hasSelection();
    return false;
}

bool UIHelpBrowserTab::isFindInPageWidgetVisible() const
{
    if (m_pContentViewer)
        return m_pContentViewer->isFindInPageWidgetVisible();
    return false;
}

void UIHelpBrowserTab::findNext()
{
    if (m_pContentViewer)
        return m_pContentViewer->sltSelectNextMatch();
}

void UIHelpBrowserTab::findPrevious()
{
    if (m_pContentViewer)
        return m_pContentViewer->sltSelectPreviousMatch();
}

void UIHelpBrowserTab::prepare(const QUrl &initialUrl)
{
    m_pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(m_pMainLayout);
    prepareToolBarAndAddressBar();
    prepareWidgets(initialUrl);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIHelpBrowserTab::sltRetranslateUI);
}

void UIHelpBrowserTab::prepareWidgets(const QUrl &initialUrl)
{
    m_pContentViewer = new UIHelpViewer(m_pHelpEngine);
    AssertReturnVoid(m_pContentViewer);
    setFocusProxy(m_pContentViewer);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    m_pMainLayout->setSpacing(0);

    m_pMainLayout->addWidget(m_pContentViewer);
    m_pContentViewer->setOpenExternalLinks(false);
    connect(m_pContentViewer, &UIHelpViewer::sourceChanged,
        this, &UIHelpBrowserTab::sigSourceChanged);
    connect(m_pContentViewer, &UIHelpViewer::historyChanged,
        this, &UIHelpBrowserTab::sltHistoryChanged);
    connect(m_pContentViewer, &UIHelpViewer::anchorClicked,
        this, &UIHelpBrowserTab::sltAnchorClicked);
    connect(m_pContentViewer, &UIHelpViewer::sigOpenLinkInNewTab,
        this, &UIHelpBrowserTab::sigOpenLinkInNewTab);
    connect(m_pContentViewer, &UIHelpViewer::sigGoBackward,
            this, &UIHelpBrowserTab::sltBackwardAction);
    connect(m_pContentViewer, &UIHelpViewer::sigGoForward,
            this, &UIHelpBrowserTab::sltForwardAction);
    connect(m_pContentViewer, &UIHelpViewer::sigGoHome,
            this, &UIHelpBrowserTab::sltHomeAction);
    connect(m_pContentViewer, &UIHelpViewer::sigAddBookmark,
            this, &UIHelpBrowserTab::sltAddBookmarkAction);
    connect(m_pContentViewer, &UIHelpViewer::highlighted,
            this, &UIHelpBrowserTab::sigLinkHighlighted);
    connect(m_pContentViewer, &UIHelpViewer::copyAvailable,
            this, &UIHelpBrowserTab::sigCopyAvailableChanged);
    connect(m_pContentViewer, &UIHelpViewer::sigFindInPageWidgetToogle,
            this, &UIHelpBrowserTab::sltFindInPageWidgetVisibilityChanged);
    connect(m_pContentViewer, &UIHelpViewer::sigMouseOverImage,
            this, &UIHelpBrowserTab::sigMouseOverImage);
    connect(m_pContentViewer, &UIHelpViewer::sigZoomRequest,
            this, &UIHelpBrowserTab::sigZoomRequest);

    m_pContentViewer->setSource(initialUrl);
}

void UIHelpBrowserTab::prepareToolBarAndAddressBar()
{
    m_pHomeAction =
        new QAction(UIIconPool::iconSetFull(":/help_browser_home_32px.png", ":/help_browser_home_16px.png",
                                            ":/help_browser_home_disabled_32px.png", ":/help_browser_home_disabled_16px.png"), QString(), this);
    m_pForwardAction =
        new QAction(UIIconPool::iconSetFull(":/help_browser_forward_32px.png", ":/help_browser_forward_16px.png",
                                            ":/help_browser_forward_disabled_32px.png", ":/help_browser_forward_disabled_16px.png"), QString(), this);
    m_pBackwardAction =
        new QAction(UIIconPool::iconSetFull(":/help_browser_backward_32px.png", ":/help_browser_backward_16px.png",
                                            ":/help_browser_backward_disabled_32px.png", ":/help_browser_backward_disabled_16px.png"), QString(), this);
    m_pAddBookmarkAction =
        new QAction(UIIconPool::iconSetFull(":/help_browser_add_bookmark_32px.png", ":/help_browser_add_bookmark_16px.png",
                                            ":/help_browser_add_bookmark_disabled_32px.png", ":/help_browser_add_bookmark_disabled_16px.png"), QString(), this);
    m_pFindInPageAction =
        new QAction(UIIconPool::iconSetFull(":/help_browser_search_32px.png", ":/help_browser_search_16px.png",
                                            ":/help_browser_search_disabled_32px.png", ":/help_browser_search_disabled_16px.png"), QString(), this);
    m_pReloadPageAction =
        new QAction(UIIconPool::iconSetFull(":/help_browser_reload_32px.png", ":/help_browser_reload_16px.png",
                                            ":/help_browser_reload_disabled_32px.png", ":/help_browser_reload_disabled_16px.png"), QString(), this);

    AssertReturnVoid(m_pHomeAction && m_pForwardAction &&
                     m_pBackwardAction && m_pAddBookmarkAction &&
                     m_pFindInPageAction);
    m_pFindInPageAction->setCheckable(true);

    connect(m_pHomeAction, &QAction::triggered, this, &UIHelpBrowserTab::sltHomeAction);
    connect(m_pAddBookmarkAction, &QAction::triggered, this, &UIHelpBrowserTab::sltAddBookmarkAction);
    connect(m_pForwardAction, &QAction::triggered, this, &UIHelpBrowserTab::sltForwardAction);
    connect(m_pBackwardAction, &QAction::triggered, this, &UIHelpBrowserTab::sltBackwardAction);
    connect(m_pFindInPageAction, &QAction::toggled, this, &UIHelpBrowserTab::sltFindInPageAction);
    connect(m_pReloadPageAction, &QAction::triggered, this, &UIHelpBrowserTab::sltReloadPageAction);

    m_pForwardAction->setEnabled(false);
    m_pBackwardAction->setEnabled(false);

    m_pToolBar = new QIToolBar;
    AssertReturnVoid(m_pToolBar);
    m_pToolBar->addAction(m_pBackwardAction);
    m_pToolBar->addAction(m_pForwardAction);
    m_pToolBar->addAction(m_pHomeAction);
    m_pToolBar->addAction(m_pReloadPageAction);
    m_pToolBar->addAction(m_pAddBookmarkAction);
    m_pToolBar->addAction(m_pFindInPageAction);

    m_pAddressBar = new QComboBox();
    m_pAddressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_pAddressBar, &QComboBox::currentIndexChanged,
            this, &UIHelpBrowserTab::sltAddressBarIndexChanged);

    QHBoxLayout *pTopLayout = new QHBoxLayout;
    pTopLayout->addWidget(m_pToolBar);
    pTopLayout->addWidget(m_pAddressBar);
    m_pMainLayout->addLayout(pTopLayout);
}

void UIHelpBrowserTab::setActionTextAndToolTip(QAction *pAction, const QString &strText, const QString &strToolTip)
{
    if (!pAction)
        return;
    pAction->setText(strText);
    pAction->setToolTip(strToolTip);
}

void UIHelpBrowserTab::sltRetranslateUI()
{
    setActionTextAndToolTip(m_pHomeAction, UIHelpBrowserWidget::tr("Home"), UIHelpBrowserWidget::tr("Return to Start Page"));
    setActionTextAndToolTip(m_pBackwardAction, UIHelpBrowserWidget::tr("Backward"), UIHelpBrowserWidget::tr("Go Back to Previous Page"));
    setActionTextAndToolTip(m_pForwardAction, UIHelpBrowserWidget::tr("Forward"), UIHelpBrowserWidget::tr("Go Forward to Next Page"));
    setActionTextAndToolTip(m_pAddBookmarkAction, UIHelpBrowserWidget::tr("Add Bookmark"), UIHelpBrowserWidget::tr("Add a New Bookmark"));
    setActionTextAndToolTip(m_pReloadPageAction, UIHelpBrowserWidget::tr("Reload"), UIHelpBrowserWidget::tr("Reload the Current Page"));
    setActionTextAndToolTip(m_pFindInPageAction, UIHelpBrowserWidget::tr("Find in Page"), UIHelpBrowserWidget::tr("Find a String in the Current Page"));
}

void UIHelpBrowserTab::sltHomeAction()
{
    if (!m_pContentViewer)
        return;
    m_pContentViewer->setSource(m_homeUrl);
}

void UIHelpBrowserTab::sltForwardAction()
{
    if (m_pContentViewer)
    {
        m_pContentViewer->forward();
        /* when we dont reload our overload imag hack does not work and images look ugly: */
        m_pContentViewer->reload();
    }
}

void UIHelpBrowserTab::sltBackwardAction()
{
    if (m_pContentViewer)
    {
        m_pContentViewer->backward();
        /* when we dont reload our overload imag hack does not work and images look ugly: */
        m_pContentViewer->reload();
    }
}

void UIHelpBrowserTab::sltFindInPageAction(bool fToggled)
{
    if (m_pContentViewer)
        m_pContentViewer->toggleFindInPageWidget(fToggled);
}

void UIHelpBrowserTab::sltReloadPageAction()
{
    if (m_pContentViewer)
        m_pContentViewer->reload();
}

void UIHelpBrowserTab::sltHistoryChanged()
{
    if (!m_pContentViewer)
        return;
    int iCurrentIndex = 0;
    /* QTextBrower history has negative and positive indices for bacward and forward items, respectively.
     * 0 is the current item: */
    m_pAddressBar->blockSignals(true);
    m_pAddressBar->clear();
    for (int i = -1 * m_pContentViewer->backwardHistoryCount(); i <= m_pContentViewer->forwardHistoryCount(); ++i)
    {
        int iIndex = m_pAddressBar->count();
        m_pAddressBar->addItem(m_pContentViewer->historyUrl(i).toString(), i);
        m_pAddressBar->setItemData(iIndex, m_pContentViewer->historyTitle(i), Qt::ToolTipRole);
        if (i == 0)
            iCurrentIndex = m_pAddressBar->count();
    }
    /* Make sure address bar show the current item: */
    m_pAddressBar->setCurrentIndex(iCurrentIndex - 1);
    m_pAddressBar->blockSignals(false);

    if (m_pBackwardAction)
        m_pBackwardAction->setEnabled(m_pContentViewer->isBackwardAvailable());
    if (m_pForwardAction)
        m_pForwardAction->setEnabled(m_pContentViewer->isForwardAvailable());

    emit sigTitleUpdate(m_pContentViewer->historyTitle(0));
    emit sigHistoryChanged(m_pContentViewer->isBackwardAvailable(), m_pContentViewer->isForwardAvailable());
}

void UIHelpBrowserTab::sltAddressBarIndexChanged(int iIndex)
{
    if (!m_pAddressBar || iIndex >= m_pAddressBar->count())
        return;
    int iHistoryIndex = m_pAddressBar->itemData(iIndex).toInt();
    /* There seems to be no way to one-step-jump to a history item: */
    if (iHistoryIndex == 0)
        return;
    if (iHistoryIndex > 0)
        for (int i = 0; i < iHistoryIndex; ++i)
            m_pContentViewer->forward();
    else
        for (int i = 0; i > iHistoryIndex ; --i)
            m_pContentViewer->backward();
}

void UIHelpBrowserTab::sltAddBookmarkAction()
{
    emit sigAddBookmark(source(), documentTitle());
}

void UIHelpBrowserTab::sltAnchorClicked(const QUrl &link)
{
    Q_UNUSED(link);
}

void UIHelpBrowserTab::sltFindInPageWidgetVisibilityChanged(bool fVisible)
{
    if (m_pFindInPageAction)
    {
        m_pFindInPageAction->blockSignals(true);
        m_pFindInPageAction->setChecked(fVisible);
        m_pFindInPageAction->blockSignals(false);
    }
    emit sigFindInPageWidgetVisibilityChanged(fVisible);
}


/*********************************************************************************************************************************
*   UIHelpBrowserTabManager definition.                                                                                          *
*********************************************************************************************************************************/

UIHelpBrowserTabManager::UIHelpBrowserTabManager(const QHelpEngine  *pHelpEngine, const QUrl &homeUrl,
                                                 const QStringList &urlList, QWidget *pParent /* = 0 */)
    : QITabWidget(pParent)
    , m_pHelpEngine(pHelpEngine)
    , m_homeUrl(homeUrl)
    , m_savedUrlList(urlList)
    , m_fSwitchToNewTab(true)
    , m_fToolBarVisible(true)
    , m_iZoomPercentage(100)
{
    Q_UNUSED(m_fSwitchToNewTab);
    prepare();
}

void UIHelpBrowserTabManager::addNewTab(const QUrl &initialUrl, bool fBackground)
{
#if 0
    /* If there is already a tab with a source which is equal to @initialUrl then make it current: */
    int iExistIndex = findTab(initialUrl);
    if (iExistIndex != -1)
    {
        setCurrentIndex(iExistIndex);
        setSource(initialUrl);
        return;
    }
#endif
    UIHelpBrowserTab *pTabWidget = new  UIHelpBrowserTab(m_pHelpEngine, m_homeUrl, initialUrl);
    AssertReturnVoid(pTabWidget);
    pTabWidget->setToolBarVisible(m_fToolBarVisible);
    int index = addTab(pTabWidget, pTabWidget->documentTitle());
    connect(pTabWidget, &UIHelpBrowserTab::sigSourceChanged,
            this, &UIHelpBrowserTabManager::sigSourceChanged);
    connect(pTabWidget, &UIHelpBrowserTab::sigTitleUpdate,
            this, &UIHelpBrowserTabManager::sltTabTitleChange);
    connect(pTabWidget, &UIHelpBrowserTab::sigOpenLinkInNewTab,
            this, &UIHelpBrowserTabManager::sltOpenLinkInNewTab);
    connect(pTabWidget, &UIHelpBrowserTab::sigAddBookmark,
            this, &UIHelpBrowserTabManager::sigAddBookmark);
    connect(pTabWidget, &UIHelpBrowserTab::sigLinkHighlighted,
            this, &UIHelpBrowserTabManager::sigLinkHighlighted);
    connect(pTabWidget, &UIHelpBrowserTab::sigCopyAvailableChanged,
            this, &UIHelpBrowserTabManager::sltCopyAvailableChanged);
    connect(pTabWidget, &UIHelpBrowserTab::sigFindInPageWidgetVisibilityChanged,
            this, &UIHelpBrowserTabManager::sigFindInPageWidgetVisibilityChanged);
    connect(pTabWidget, &UIHelpBrowserTab::sigHistoryChanged,
            this, &UIHelpBrowserTabManager::sigHistoryChanged);
   connect(pTabWidget, &UIHelpBrowserTab::sigMouseOverImage,
            this, &UIHelpBrowserTabManager::sigMouseOverImage);
   connect(pTabWidget, &UIHelpBrowserTab::sigZoomRequest,
            this, &UIHelpBrowserTabManager::sltHandleZoomRequest);

   pTabWidget->setZoomPercentage(zoomPercentage());
   pTabWidget->setHelpFileList(m_helpFileList);
   setFocusProxy(pTabWidget);
   if (!fBackground)
       setCurrentIndex(index);
}

void UIHelpBrowserTabManager::updateTabUrlTitleList()
{
    QList<QPair<QString, int> > newList;

    QStringList titles = tabTitleList();

    if (titles == m_tabTitleList)
        return;

    m_tabTitleList = titles;
    emit sigTabsListChanged(m_tabTitleList);
}

void UIHelpBrowserTabManager::closeAllTabsBut(int iTabIndex)
{
    QString strTitle = tabText(iTabIndex);
    QList<QWidget*> widgetList;
    for (int i = 0; i < count(); ++i)
        widgetList.append(widget(i));
    clear();
    for (int i = 0; i < widgetList.size(); ++i)
    {
        if (i != iTabIndex)
            delete widgetList[i];
    }
    addTab(widgetList[iTabIndex], strTitle);
    updateTabUrlTitleList();
}

int  UIHelpBrowserTabManager::findTab(const QUrl &Url) const
{
    for (int i = 0; i < count(); ++i)
    {
        UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(widget(i));
        if (!pTab || !pTab->source().isValid())
            continue;
        if (pTab->source() == Url)
            return i;
    }
    return -1;
}

void UIHelpBrowserTabManager::initializeTabs()
{
    clearAndDeleteTabs();
    /* Start with a single tab showing the home URL: */
    if (m_savedUrlList.isEmpty())
        addNewTab(QUrl(), false);
    /* Start with saved tab(s): */
    else
        for (int i = 0; i < m_savedUrlList.size(); ++i)
            addNewTab(m_savedUrlList[i], false);
    updateTabUrlTitleList();
}

QUrl UIHelpBrowserTabManager::currentSource() const
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (!pTab)
        return QUrl();
    return pTab->source();
}

void UIHelpBrowserTabManager::setSource(const QUrl &url, bool fNewTab /* = false */)
{
    if (!fNewTab)
    {
        UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
        if (!pTab)
            return;
        pTab->setSource(url);
    }
    else
        addNewTab(url, false);
    updateTabUrlTitleList();
}

QStringList UIHelpBrowserTabManager::tabUrlList() const
{
    QStringList list;
    for (int i = 0; i < count(); ++i)
    {
        UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(widget(i));
        if (!pTab || !pTab->source().isValid())
            continue;
        list << pTab->source().toString();
    }
    return list;
}

QStringList UIHelpBrowserTabManager::tabTitleList() const
{
    QStringList list;
    for (int i = 0; i < count(); ++i)
    {
        UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(widget(i));
        if (!pTab || !pTab->source().isValid())
            continue;
        list << pTab->documentTitle();
    }
    return list;
}

void UIHelpBrowserTabManager::setToolBarVisible(bool fVisible)
{
    /* Make sure existing tabs are configured: */
    for (int i = 0; i < count(); ++i)
    {
        UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(widget(i));
        if (!pTab)
            continue;
        pTab->setToolBarVisible(fVisible);
    }
    /* This is for the tabs that will be created later: */
    m_fToolBarVisible = fVisible;
}

void UIHelpBrowserTabManager::printCurrent(QPrinter &printer)
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (!pTab)
        return;
    return pTab->print(printer);
}

void UIHelpBrowserTabManager::switchToTab(int iIndex)
{
    if (iIndex == currentIndex())
        return;
    setCurrentIndex(iIndex);
}

int UIHelpBrowserTabManager::zoomPercentage() const
{
    return m_iZoomPercentage;
}

void UIHelpBrowserTabManager::setHelpFileList(const QList<QUrl> &helpFileList)
{
    m_helpFileList = helpFileList;
}

void UIHelpBrowserTabManager::copySelectedText() const
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (!pTab)
        return;
    return pTab->copySelectedText();
}

bool UIHelpBrowserTabManager::hasCurrentTabSelectedText() const
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (!pTab)
        return false;
    return pTab->hasSelectedText();
}

bool UIHelpBrowserTabManager::isFindInPageWidgetVisible() const
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (!pTab)
        return false;
    return pTab->isFindInPageWidgetVisible();
}

void UIHelpBrowserTabManager::toggleFindInPage(bool fTrigger)
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->sltFindInPageAction(fTrigger);
}

void UIHelpBrowserTabManager::findNext()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->findNext();
}

void UIHelpBrowserTabManager::findPrevious()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->findPrevious();
}

void UIHelpBrowserTabManager::sltTabTitleChange(const QString &strTitle)
{
    for (int i = 0; i < count(); ++i)
    {
        if (sender() == widget(i))
        {
            setTabText(i, strTitle);
            setTabToolTip(i, strTitle);
            continue;
        }
    }
    updateTabUrlTitleList();
}

void UIHelpBrowserTabManager::sltOpenLinkInNewTab(const QUrl &url, bool fBackground)
{
    if (url.isValid())
        addNewTab(url, fBackground);
    updateTabUrlTitleList();
}

void UIHelpBrowserTabManager::sltCopyAvailableChanged(bool fAvailable)
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    /* Emit coresponding signal only if sender is the current tab: */
    if (pTab && sender() == pTab)
        emit sigCopyAvailableChanged(fAvailable);
}

void UIHelpBrowserTabManager::sltTabClose(int iTabIndex)
{
    if (count() <= 1)
        return;
    QWidget *pWidget = widget(iTabIndex);
    if (!pWidget)
        return;
    removeTab(iTabIndex);
    delete pWidget;
    updateTabUrlTitleList();
}

void UIHelpBrowserTabManager::sltContextMenuTabClose()
{
    QAction *pAction = qobject_cast<QAction*>(sender());
    if (!pAction)
        return;
    int iTabIndex = pAction->data().toInt();
    if (iTabIndex < 0 || iTabIndex >= count())
        return;
    sltTabClose(iTabIndex);
}

void UIHelpBrowserTabManager::sltCloseOtherTabsContextMenuAction()
{
    /* Find the index of the sender tab. we will close all tabs but sender tab: */
    QAction *pAction = qobject_cast<QAction*>(sender());
    if (!pAction)
        return;
    int iTabIndex = pAction->data().toInt();
    if (iTabIndex < 0 || iTabIndex >= count())
        return;
    closeAllTabsBut(iTabIndex);
}

void UIHelpBrowserTabManager::sltCloseCurrentTab()
{
    sltTabClose(currentIndex());
}

void UIHelpBrowserTabManager::sltCloseOtherTabs()
{
    closeAllTabsBut(currentIndex());
}

void UIHelpBrowserTabManager::sltCurrentChanged(int iTabIndex)
{
    Q_UNUSED(iTabIndex);
    emit sigSourceChanged(currentSource());
}

void UIHelpBrowserTabManager::sltShowTabBarContextMenu(const QPoint &pos)
{
    if (!tabBar())
        return;
    QMenu menu;
    QAction *pCloseAll = menu.addAction(UIHelpBrowserWidget::tr("Close Other Tabs"));
    connect(pCloseAll, &QAction::triggered, this, &UIHelpBrowserTabManager::sltCloseOtherTabsContextMenuAction);
    pCloseAll->setData(tabBar()->tabAt(pos));

    QAction *pClose = menu.addAction(UIHelpBrowserWidget::tr("Close Tab"));
    connect(pClose, &QAction::triggered, this, &UIHelpBrowserTabManager::sltContextMenuTabClose);
    pClose->setData(tabBar()->tabAt(pos));

    menu.exec(tabBar()->mapToGlobal(pos));
}

void UIHelpBrowserTabManager::sltHomeAction()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->sltHomeAction();
}

void UIHelpBrowserTabManager::sltAddBookmarkAction()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->sltAddBookmarkAction();
}

void UIHelpBrowserTabManager::sltForwardAction()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->sltForwardAction();
}

void UIHelpBrowserTabManager::sltBackwardAction()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->sltBackwardAction();
}

void UIHelpBrowserTabManager::sltHandleZoomRequest(UIHelpViewer::ZoomOperation enmOperation)
{
    int iZoomPercentage = m_iZoomPercentage;
    switch (enmOperation)
    {
        case UIHelpViewer::ZoomOperation_In:
            iZoomPercentage += iZoomPercentageStep;
            break;
        case UIHelpViewer::ZoomOperation_Out:
            iZoomPercentage -= iZoomPercentageStep;
            break;
        case UIHelpViewer::ZoomOperation_Reset:
        default:
            iZoomPercentage = 100;
            break;
    }
    setZoomPercentage(iZoomPercentage);
}

void UIHelpBrowserTabManager::setZoomPercentage(int iZoomPercentage)
{

    if (iZoomPercentage > zoomPercentageMinMax.second ||
        iZoomPercentage < zoomPercentageMinMax.first)
        return;

    m_iZoomPercentage = iZoomPercentage;

    for (int i = 0; i < count(); ++i)
    {
        UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(widget(i));
        if (pTab)
            pTab->setZoomPercentage(m_iZoomPercentage);
    }
    emit sigZoomPercentageChanged(m_iZoomPercentage);
}

void UIHelpBrowserTabManager::sltReloadPageAction()
{
    UIHelpBrowserTab *pTab = qobject_cast<UIHelpBrowserTab*>(currentWidget());
    if (pTab)
        pTab->sltReloadPageAction();
}

void UIHelpBrowserTabManager::prepare()
{
    setTabsClosable(true);
    setTabBarAutoHide(true);
    connect(this, &UIHelpBrowserTabManager::tabCloseRequested, this, &UIHelpBrowserTabManager::sltTabClose);
    connect(this, &UIHelpBrowserTabManager::currentChanged, this, &UIHelpBrowserTabManager::sltCurrentChanged);
    if (tabBar())
    {
        tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tabBar(), &QTabBar::customContextMenuRequested, this, &UIHelpBrowserTabManager::sltShowTabBarContextMenu);
    }
}

void UIHelpBrowserTabManager::clearAndDeleteTabs()
{
    QList<QWidget*> tabList;
    for (int i = 0; i < count(); ++i)
        tabList << widget(i);
    /* QTabWidget::clear() does not delete tab widgets: */
    clear();
    foreach (QWidget *pWidget, tabList)
        delete pWidget;
}


/*********************************************************************************************************************************
*   UIHelpBrowserWidget implementation.                                                                                          *
*********************************************************************************************************************************/

UIHelpBrowserWidget::UIHelpBrowserWidget(EmbedTo enmEmbedding, const QString &strHelpFilePath, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_fIsPolished(false)
    , m_pMainLayout(0)
    , m_pTopLayout(0)
    , m_pTabWidget(0)
    , m_pToolBar(0)
    , m_strHelpFilePath(strHelpFilePath)
    , m_pHelpEngine(0)
    , m_pSplitter(0)
    , m_pFileMenu(0)
    , m_pEditMenu(0)
    , m_pViewMenu(0)
    , m_pTabsMenu(0)
    , m_pNavigationMenu(0)
    , m_pContentWidget(0)
    , m_pIndexWidget(0)
    , m_pContentModel(0)
    , m_pSearchEngine(0)
    , m_pSearchQueryWidget(0)
    , m_pSearchResultWidget(0)
    , m_pTabManager(0)
    , m_pBookmarksWidget(0)
    , m_pSearchContainerWidget(0)
    , m_pPrintAction(0)
    , m_pShowHideSideBarAction(0)
    , m_pShowHideToolBarAction(0)
    , m_pShowHideStatusBarAction(0)
    , m_pCopySelectedTextAction(0)
    , m_pFindInPageAction(0)
    , m_pFindNextInPageAction(0)
    , m_pFindPreviousInPageAction(0)
    , m_pBackwardAction(0)
    , m_pForwardAction(0)
    , m_pHomeAction(0)
    , m_pReloadPageAction(0)
    , m_pAddBookmarkAction(0)
    , m_pZoomMenuAction(0)
    , m_fModelContentCreated(false)
    , m_fIndexingFinished(false)
    , m_fCommitDataSignalReceived(false)
{
    qRegisterMetaType<HelpBrowserTabs>("HelpBrowserTabs");
    prepare();
    loadOptions();
}

UIHelpBrowserWidget::~UIHelpBrowserWidget()
{
    cleanup();
}

QList<QMenu*> UIHelpBrowserWidget::menus() const
{
    QList<QMenu*> menuList;
    menuList
        << m_pFileMenu
        << m_pEditMenu
        << m_pNavigationMenu
        << m_pViewMenu
        << m_pTabsMenu;
    return menuList;
}

void UIHelpBrowserWidget::showHelpForKeyword(const QString &strKeyword)
{
    if (m_fIndexingFinished)
        findAndShowUrlForKeyword(strKeyword);
    else
        m_keywordList.append(strKeyword);
}

int UIHelpBrowserWidget::zoomPercentage() const
{
    if (m_pTabManager)
        return m_pTabManager->zoomPercentage();
    return 0;
}

bool UIHelpBrowserWidget::shouldBeMaximized() const
{
    return gEDataManager->logWindowShouldBeMaximized();
}

void UIHelpBrowserWidget::prepare()
{
    m_pMainLayout = new QVBoxLayout(this);
    m_pMainLayout->setContentsMargins(0.2 * qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin),
                                      qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin),
                                      0.2 * qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin),
                                      0.2 * qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin));

    AssertReturnVoid(m_pMainLayout);

    prepareActions();
    prepareMenu();
    prepareWidgets();
    prepareConnections();
    prepareSearchWidgets();
    loadBookmarks();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIHelpBrowserWidget::sltRetranslateUI);
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIHelpBrowserWidget::sltCommitDataSignalReceived);
}

void UIHelpBrowserWidget::prepareActions()
{
    m_pShowHideSideBarAction = new QAction(this);
    m_pShowHideSideBarAction->setCheckable(true);
    m_pShowHideSideBarAction->setChecked(true);
    connect(m_pShowHideSideBarAction, &QAction::toggled,
            this, &UIHelpBrowserWidget::sltWidgetVisibilityToggle);

    m_pShowHideToolBarAction = new QAction(this);
    m_pShowHideToolBarAction->setCheckable(true);
    m_pShowHideToolBarAction->setChecked(true);
    connect(m_pShowHideToolBarAction, &QAction::toggled,
            this, &UIHelpBrowserWidget::sltWidgetVisibilityToggle);

    m_pShowHideStatusBarAction = new QAction(this);
    m_pShowHideStatusBarAction->setCheckable(true);
    m_pShowHideStatusBarAction->setChecked(true);
    connect(m_pShowHideStatusBarAction, &QAction::toggled,
            this, &UIHelpBrowserWidget::sltWidgetVisibilityToggle);

    m_pCopySelectedTextAction = new QAction(this);
    connect(m_pCopySelectedTextAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sltCopySelectedText);
    m_pCopySelectedTextAction->setShortcut(QString("Ctrl+C"));

    m_pFindInPageAction = new QAction(this);
    m_pFindInPageAction->setCheckable(true);
    m_pFindInPageAction->setChecked(false);
    connect(m_pFindInPageAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sltFindInPage);
    m_pFindInPageAction->setShortcut(QKeySequence::Find);

    m_pFindNextInPageAction = new QAction(this);
    m_pFindNextInPageAction->setEnabled(false);
    connect(m_pFindNextInPageAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sltFindNextInPage);
    m_pFindNextInPageAction->setShortcut(QKeySequence::FindNext);

    m_pFindPreviousInPageAction = new QAction(this);
    m_pFindPreviousInPageAction->setEnabled(false);
    connect(m_pFindPreviousInPageAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sltFindPreviousInPage);
    m_pFindPreviousInPageAction->setShortcut(QKeySequence::FindPrevious);

    m_pPrintAction = new QAction(this);
    connect(m_pPrintAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sltShowPrintDialog);
    m_pPrintAction->setShortcut(QString("Ctrl+P"));

    m_pQuitAction = new QAction(this);
    connect(m_pQuitAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sigCloseDialog);
    m_pQuitAction->setShortcut(QString("Ctrl+Q"));

    m_pBackwardAction = new QAction(this);
    m_pBackwardAction->setShortcut(QKeySequence::Back);
    connect(m_pBackwardAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sigGoBackward);
    m_pBackwardAction->setEnabled(false);

    m_pForwardAction = new QAction(this);
    m_pForwardAction->setShortcut(QKeySequence::Forward);
    connect(m_pForwardAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sigGoForward);
    m_pForwardAction->setEnabled(false);

    m_pHomeAction = new QAction(this);
    connect(m_pHomeAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sigGoHome);

    m_pReloadPageAction = new QAction(this);
    m_pReloadPageAction->setShortcut(QKeySequence::Refresh);
    connect(m_pReloadPageAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sigReloadPage);

    m_pAddBookmarkAction = new QAction(this);
    m_pAddBookmarkAction->setShortcut(QKeySequence("Ctrl+D"));
    connect(m_pAddBookmarkAction, &QAction::triggered,
            this, &UIHelpBrowserWidget::sigAddBookmark);

    m_pZoomMenuAction = new UIZoomMenuAction(this);
    connect(m_pZoomMenuAction, &UIZoomMenuAction::sigZoomChanged,
            this, &UIHelpBrowserWidget::sltZoomActions);
}

void UIHelpBrowserWidget::prepareConnections()
{
    if (m_pTabManager)
    {
        connect(m_pHomeAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltHomeAction);
        connect(m_pAddBookmarkAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltAddBookmarkAction);
        connect(m_pForwardAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltForwardAction);
        connect(m_pBackwardAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltBackwardAction);
        connect(m_pReloadPageAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltReloadPageAction);
    }
}

void UIHelpBrowserWidget::prepareWidgets()
{
    m_pSplitter = new QSplitter;
    AssertReturnVoid(m_pSplitter);

    m_pMainLayout->addWidget(m_pSplitter);
    m_pHelpEngine = new QHelpEngine(m_strHelpFilePath, this);
    m_pBookmarksWidget = new UIBookmarksListContainer(this);
    m_pTabWidget = new QITabWidget;
    m_pTabManager = new UIHelpBrowserTabManager(m_pHelpEngine, findIndexHtml(), loadSavedUrlList());
    m_pTabManager->setHelpFileList(m_pHelpEngine->files(m_pHelpEngine->namespaceName(m_strHelpFilePath), QStringList()));

    AssertReturnVoid(m_pTabWidget &&
                     m_pHelpEngine &&
                     m_pBookmarksWidget &&
                     m_pTabManager);

    m_pContentWidget = m_pHelpEngine->contentWidget();
    m_pIndexWidget = m_pHelpEngine->indexWidget();
    m_pContentModel = m_pHelpEngine->contentModel();

    AssertReturnVoid(m_pContentWidget && m_pIndexWidget && m_pContentModel);
    m_pSplitter->addWidget(m_pTabWidget);
    m_pContentWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    m_pTabWidget->insertTab(HelpBrowserTabs_TOC, m_pContentWidget, QString());
    m_pTabWidget->insertTab(HelpBrowserTabs_Bookmarks, m_pBookmarksWidget, QString());
    /* Dont insert the index widget since we only have automatically generated indexes: */
#if 0
    m_pTabWidget->insertTab(HelpBrowserTabs_Index, m_pIndexWidget, QString());
#endif

    m_pSplitter->addWidget(m_pTabManager);
    m_pSplitter->setStretchFactor(0,0);
    m_pSplitter->setStretchFactor(1,1);

    m_pSplitter->setChildrenCollapsible(false);

    connect(m_pTabManager, &UIHelpBrowserTabManager::sigSourceChanged,
            this, &UIHelpBrowserWidget::sltViewerSourceChange);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigAddBookmark,
            this, &UIHelpBrowserWidget::sltAddNewBookmark);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigTabsListChanged,
            this, &UIHelpBrowserWidget::sltTabListChanged);
    connect(m_pTabManager, &UIHelpBrowserTabManager::currentChanged,
            this, &UIHelpBrowserWidget::sltCurrentTabChanged);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigLinkHighlighted,
            this, &UIHelpBrowserWidget::sltLinkHighlighted);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigZoomPercentageChanged,
            this, &UIHelpBrowserWidget::sltZoomPercentageChanged);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigCopyAvailableChanged,
            this, &UIHelpBrowserWidget::sltCopyAvailableChanged);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigFindInPageWidgetVisibilityChanged,
            this, &UIHelpBrowserWidget::sltFindInPageWidgetVisibilityChanged);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigHistoryChanged,
            this, &UIHelpBrowserWidget::sltHistoryChanged);
    connect(m_pTabManager, &UIHelpBrowserTabManager::sigMouseOverImage,
            this, &UIHelpBrowserWidget::sltMouseOverImage);

    connect(m_pHelpEngine, &QHelpEngine::setupFinished,
            this, &UIHelpBrowserWidget::sltHelpEngineSetupFinished);
    connect(m_pContentWidget, &QHelpContentWidget::clicked,
            this, &UIHelpBrowserWidget::sltContentWidgetItemClicked);
    connect(m_pContentModel, &QHelpContentModel::contentsCreated,
            this, &UIHelpBrowserWidget::sltContentsCreated);
    connect(m_pContentWidget, &QHelpContentWidget::customContextMenuRequested,
            this, &UIHelpBrowserWidget::sltShowLinksContextMenu);
    connect(m_pBookmarksWidget, &UIBookmarksListContainer::sigBookmarkDoubleClick,
            this, &UIHelpBrowserWidget::sltOpenLinkWithUrl);
    connect(m_pBookmarksWidget, &UIBookmarksListContainer::sigListWidgetContextMenuRequest,
            this, &UIHelpBrowserWidget::sltShowLinksContextMenu);

    if (QFile(m_strHelpFilePath).exists() && m_pHelpEngine)
        m_pHelpEngine->setupData();
}

void UIHelpBrowserWidget::prepareSearchWidgets()
{
    AssertReturnVoid(m_pTabWidget && m_pHelpEngine);

    m_pSearchContainerWidget = new QWidget;
    m_pTabWidget->insertTab(HelpBrowserTabs_Search, m_pSearchContainerWidget, QString());
    m_pTabWidget->setTabPosition(QTabWidget::South);

    m_pSearchEngine = m_pHelpEngine->searchEngine();
    AssertReturnVoid(m_pSearchEngine);

    m_pSearchQueryWidget = m_pSearchEngine->queryWidget();
    m_pSearchResultWidget = m_pSearchEngine->resultWidget();
    AssertReturnVoid(m_pSearchQueryWidget && m_pSearchResultWidget);
    m_pSearchResultWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pSearchQueryWidget->setCompactMode(false);

    QVBoxLayout *pSearchLayout = new QVBoxLayout(m_pSearchContainerWidget);
    pSearchLayout->addWidget(m_pSearchQueryWidget);
    pSearchLayout->addWidget(m_pSearchResultWidget);
    m_pSearchQueryWidget->expandExtendedSearch();

    connect(m_pSearchQueryWidget, &QHelpSearchQueryWidget::search,
            this, &UIHelpBrowserWidget::sltSearchStart);
    connect(m_pSearchResultWidget, &QHelpSearchResultWidget::requestShowLink,
            this, &UIHelpBrowserWidget::sltOpenLinkWithUrl);
    connect(m_pSearchResultWidget, &QHelpContentWidget::customContextMenuRequested,
            this, &UIHelpBrowserWidget::sltShowLinksContextMenu);
    connect(m_pSearchEngine, &QHelpSearchEngine::indexingStarted,
            this, &UIHelpBrowserWidget::sltIndexingStarted);
    connect(m_pSearchEngine, &QHelpSearchEngine::indexingFinished,
            this, &UIHelpBrowserWidget::sltIndexingFinished);
    connect(m_pSearchEngine, &QHelpSearchEngine::searchingStarted,
            this, &UIHelpBrowserWidget::sltSearchingStarted);

    m_pSearchEngine->reindexDocumentation();
}

void UIHelpBrowserWidget::prepareToolBar()
{
    m_pTopLayout = new QHBoxLayout;
    m_pToolBar = new QIToolBar(parentWidget());
    if (m_pToolBar)
    {
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            m_pTopLayout->addWidget(m_pToolBar);
            m_pMainLayout->addLayout(m_pTopLayout);
        }
#else
        /* Add into layout: */
        m_pTopLayout->addWidget(m_pToolBar);
        m_pMainLayout->addLayout(m_pTopLayout);
#endif
    }
}

void UIHelpBrowserWidget::prepareMenu()
{
    m_pFileMenu = new QMenu(tr("&File"), this);
    m_pEditMenu = new QMenu(tr("&Edit"), this);
    m_pNavigationMenu = new QMenu(tr("&Navigation"), this);
    m_pViewMenu = new QMenu(tr("&View"), this);
    m_pTabsMenu = new QMenu(tr("&Tabs"), this);

    AssertReturnVoid(m_pFileMenu && m_pViewMenu &&
                     m_pTabsMenu && m_pNavigationMenu);

    addActionToMenu(m_pFileMenu, m_pPrintAction);
    addActionToMenu(m_pFileMenu, m_pQuitAction);

    addActionToMenu(m_pEditMenu, m_pCopySelectedTextAction);
    addActionToMenu(m_pEditMenu, m_pFindInPageAction);
    addActionToMenu(m_pEditMenu, m_pFindNextInPageAction);
    addActionToMenu(m_pEditMenu, m_pFindPreviousInPageAction);

    addActionToMenu(m_pViewMenu, m_pZoomMenuAction);
    addActionToMenu(m_pViewMenu, m_pShowHideSideBarAction);
    addActionToMenu(m_pViewMenu, m_pShowHideToolBarAction);
    addActionToMenu(m_pViewMenu, m_pShowHideStatusBarAction);

    addActionToMenu(m_pNavigationMenu, m_pBackwardAction);
    addActionToMenu(m_pNavigationMenu, m_pForwardAction);
    addActionToMenu(m_pNavigationMenu, m_pHomeAction);
    addActionToMenu(m_pNavigationMenu, m_pReloadPageAction);
    addActionToMenu(m_pNavigationMenu, m_pAddBookmarkAction);
}

void UIHelpBrowserWidget::loadOptions()
{
    if (m_pTabManager)
        m_pTabManager->setZoomPercentage(gEDataManager->helpBrowserZoomPercentage());
}

QStringList UIHelpBrowserWidget::loadSavedUrlList()
{
    return gEDataManager->helpBrowserLastUrlList();
}

void UIHelpBrowserWidget::loadBookmarks()
{
    if (!m_pBookmarksWidget)
        return;

    QStringList bookmarks = gEDataManager->helpBrowserBookmarks();
    /* bookmarks list is supposed to have url title pair: */
    for (int i = 0; i < bookmarks.size(); ++i)
    {
        const QString &url = bookmarks[i];
        if (i+1 >= bookmarks.size())
            break;
        ++i;
        const QString &strTitle = bookmarks[i];
        m_pBookmarksWidget->addBookmark(url, strTitle);
    }
}

void UIHelpBrowserWidget::saveBookmarks()
{
    if (!m_pBookmarksWidget)
        return;
    gEDataManager->setHelpBrowserBookmarks(m_pBookmarksWidget->bookmarks());
}

void UIHelpBrowserWidget::saveOptions()
{
    if (m_pTabManager)
    {
        gEDataManager->setHelpBrowserLastUrlList(m_pTabManager->tabUrlList());
        gEDataManager->setHelpBrowserZoomPercentage(m_pTabManager->zoomPercentage());
    }
}

QUrl UIHelpBrowserWidget::findIndexHtml() const
{
    QList<QUrl> files = m_pHelpEngine->files(m_pHelpEngine->namespaceName(m_strHelpFilePath), QStringList());
    int iIndex = -1;
    for (int i = 0; i < files.size(); ++i)
    {
        if (QString::compare(files[i].fileName(), "preface.html", Qt::CaseInsensitive) == 0)
        {
            iIndex = i;
            break;
        }
    }
    if (iIndex == -1)
    {
        /* If index html/htm could not be found try to find a html file at least: */
        for (int i = 0; i < files.size(); ++i)
        {
            if (files[i].toString().contains(".html", Qt::CaseInsensitive) ||
                files[i].toString().contains(".htm", Qt::CaseInsensitive))
            {
                iIndex = i;
                break;
            }
        }
    }
    if (iIndex != -1 && files.size() > iIndex)
        return files[iIndex];
    else
        return QUrl();
}

QUrl UIHelpBrowserWidget::contentWidgetUrl(const QModelIndex &itemIndex)
{
    QHelpContentModel *pContentModel =
        qobject_cast<QHelpContentModel*>(m_pContentWidget->model());
    if (!pContentModel)
        return QUrl();
    QHelpContentItem *pItem = pContentModel->contentItemAt(itemIndex);
    if (!pItem)
        return QUrl();
    return pItem->url();
}

void UIHelpBrowserWidget::cleanup()
{
    if (m_fCommitDataSignalReceived)
        return;
    saveOptions();
    saveBookmarks();
}

void UIHelpBrowserWidget::sltRetranslateUI()
{
    if (m_pTabWidget)
    {
        m_pTabWidget->setTabText(HelpBrowserTabs_TOC, tr("Contents"));
        m_pTabWidget->setTabText(HelpBrowserTabs_Index, tr("Index"));
        m_pTabWidget->setTabText(HelpBrowserTabs_Search, tr("Search"));
        m_pTabWidget->setTabText(HelpBrowserTabs_Bookmarks, tr("Bookmarks"));
    }

    if (m_pShowHideSideBarAction)
        m_pShowHideSideBarAction->setText(tr("Show &Side Bar"));
    if (m_pShowHideToolBarAction)
        m_pShowHideToolBarAction->setText(tr("Show &Tool Bar"));
    if (m_pShowHideStatusBarAction)
        m_pShowHideStatusBarAction->setText(tr("Show St&atus Bar"));

    if (m_pPrintAction)
        m_pPrintAction->setText(tr("&Print..."));
    if (m_pQuitAction)
        m_pQuitAction->setText(tr("&Quit"));

    if (m_pCopySelectedTextAction)
        m_pCopySelectedTextAction->setText(tr("&Copy Selected Text"));
    if (m_pFindInPageAction)
        m_pFindInPageAction->setText(tr("&Find in Page"));
    if (m_pFindNextInPageAction)
        m_pFindNextInPageAction->setText(tr("Find Ne&xt"));
    if (m_pFindPreviousInPageAction)
        m_pFindPreviousInPageAction->setText(tr("Find &Previous"));

    if (m_pBackwardAction)
        m_pBackwardAction->setText(tr("Go Backward"));
    if (m_pForwardAction)
        m_pForwardAction->setText(tr("Go Forward"));
    if (m_pHomeAction)
        m_pHomeAction->setText(tr("Go to Start Page"));
    if (m_pReloadPageAction)
        m_pReloadPageAction->setText(tr("Reload Page"));
    if (m_pAddBookmarkAction)
        m_pAddBookmarkAction->setText(tr("Add Bookmark"));
}

void UIHelpBrowserWidget::sltCommitDataSignalReceived()
{
    cleanup();
    m_fCommitDataSignalReceived = true;
}

void UIHelpBrowserWidget::showEvent(QShowEvent *pEvent)
{
    QWidget::showEvent(pEvent);
    if (m_fIsPolished)
        return;
    m_fIsPolished = true;
    if (m_pTabManager)
        m_pTabManager->setFocus();
}

void UIHelpBrowserWidget::keyPressEvent(QKeyEvent *pEvent)
{
    QWidget::keyPressEvent(pEvent);
}

void UIHelpBrowserWidget::findAndShowUrlForKeyword(const QString &strKeyword)
{
    QList<QHelpLink> links = m_pHelpEngine->documentsForIdentifier(strKeyword);
    if (!links.isEmpty())
    {
        /* We have to a have a single url per keyword in this case: */
        m_pTabManager->setSource(links.first().url, true /* new tab */);
    }
}

void UIHelpBrowserWidget::sltWidgetVisibilityToggle(bool fToggled)
{
    if (sender() == m_pShowHideSideBarAction)
    {
        if (m_pTabWidget)
            m_pTabWidget->setVisible(fToggled);
    }
    else if (sender() == m_pShowHideToolBarAction)
    {
        if (m_pTabManager)
            m_pTabManager->setToolBarVisible(fToggled);
    }
    else if (sender() == m_pShowHideStatusBarAction)
        emit sigStatusBarVisible(fToggled);
}

void UIHelpBrowserWidget::sltCopySelectedText()
{
    if (m_pTabManager)
        m_pTabManager->copySelectedText();
}

void UIHelpBrowserWidget::sltFindInPage(bool fChecked)
{
    if (m_pTabManager)
        m_pTabManager->toggleFindInPage(fChecked);
}

void UIHelpBrowserWidget::sltFindNextInPage()
{
    if (m_pTabManager)
        m_pTabManager->findNext();
}

void UIHelpBrowserWidget::sltFindPreviousInPage()
{
    if (m_pTabManager)
        m_pTabManager->findPrevious();
}

void UIHelpBrowserWidget::sltHistoryChanged(bool fBackwardAvailable, bool fForwardAvailable)
{
    if (m_pBackwardAction)
        m_pBackwardAction->setEnabled(fBackwardAvailable);
    if (m_pForwardAction)
        m_pForwardAction->setEnabled(fForwardAvailable);
}

void UIHelpBrowserWidget::sltLinkHighlighted(const QUrl &url)
{
    if (url.isEmpty())
    {
        emit sigStatusBarMessage("", 0);
        return;
    }

    QString strMessage = url.url();
    if (url.scheme() == "https" || url.scheme() == "http")
        strMessage = QString("%1: %2").arg(tr("Click to open the following URL with an external browser")).arg(strMessage);
    else if (url.scheme() == "qthelp")
        strMessage = QString("%1: %2").arg(tr("Click to navigate to internal URL")).arg(strMessage);
    else
        strMessage = "";
    emit sigStatusBarMessage(strMessage, 0);
}

void UIHelpBrowserWidget::sltMouseOverImage(const QString &strImageName)
{
    emit sigStatusBarMessage(QString("%1: %2").arg(tr("Click to enlarge the image")).arg(strImageName), 3000);
}

void UIHelpBrowserWidget::sltCopyAvailableChanged(bool fAvailable)
{
    if (m_pCopySelectedTextAction)
        m_pCopySelectedTextAction->setEnabled(fAvailable);
}

void UIHelpBrowserWidget::sltFindInPageWidgetVisibilityChanged(bool fVisible)
{
    if (m_pFindInPageAction)
    {
        m_pFindInPageAction->blockSignals(true);
        m_pFindInPageAction->setChecked(fVisible);
        m_pFindInPageAction->blockSignals(false);
    }
    if (m_pFindNextInPageAction)
        m_pFindNextInPageAction->setEnabled(fVisible);

    if (m_pFindPreviousInPageAction)
        m_pFindPreviousInPageAction->setEnabled(fVisible);
}

void UIHelpBrowserWidget::sltShowPrintDialog()
{
#ifdef VBOX_WS_NIX
    if (!m_pTabManager)
        return;
    QPrinter printer;
    QPrintDialog printDialog(&printer, this);
    if (printDialog.exec() == QDialog::Accepted)
        m_pTabManager->printCurrent(printer);
#endif
}

void UIHelpBrowserWidget::sltHelpEngineSetupFinished()
{
    AssertReturnVoid(m_pTabManager);
    m_fIndexingFinished = true;
    m_pTabManager->initializeTabs();
}

void UIHelpBrowserWidget::sltContentWidgetItemClicked(const QModelIndex & index)
{
    AssertReturnVoid(m_pTabManager && m_pHelpEngine && m_pContentWidget);
    QUrl url = contentWidgetUrl(index);
    if (!url.isValid())
        return;
    m_pTabManager->setSource(url);

    m_pContentWidget->scrollTo(index, QAbstractItemView::EnsureVisible);
    m_pContentWidget->expand(index);
}

void UIHelpBrowserWidget::sltViewerSourceChange(const QUrl &source)
{
    if (m_fModelContentCreated && m_pContentWidget && source.isValid() && m_pContentModel)
    {
        QModelIndex index = m_pContentWidget->indexOf(source);
        QItemSelectionModel *pSelectionModel = m_pContentWidget->selectionModel();
        if (pSelectionModel && index.isValid())
        {
            m_pContentWidget->blockSignals(true);
            pSelectionModel->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_pContentWidget->scrollTo(index, QAbstractItemView::EnsureVisible);
            m_pContentWidget->expand(index);
            m_pContentWidget->blockSignals(false);
        }
    }
}

void UIHelpBrowserWidget::sltContentsCreated()
{
    m_fModelContentCreated = true;
    if (m_pTabManager)
        sltViewerSourceChange(m_pTabManager->currentSource());
}

void UIHelpBrowserWidget::sltIndexingStarted()
{
    if (m_pSearchContainerWidget)
        m_pSearchContainerWidget->setEnabled(false);
}

void UIHelpBrowserWidget::sltIndexingFinished()
{
    AssertReturnVoid(m_pTabManager &&
                     m_pHelpEngine &&
                     m_pSearchContainerWidget);

    m_pSearchContainerWidget->setEnabled(true);
    m_fIndexingFinished = true;
    /* Process the keyword queue. */
    foreach (const QString strKeyword, m_keywordList)
        findAndShowUrlForKeyword(strKeyword);
    m_keywordList.clear();

}

void UIHelpBrowserWidget::sltSearchingStarted()
{
}

void UIHelpBrowserWidget::sltSearchStart()
{
    AssertReturnVoid(m_pSearchEngine && m_pSearchQueryWidget);
    m_pSearchEngine->search(m_pSearchQueryWidget->searchInput());
}

void UIHelpBrowserWidget::sltShowLinksContextMenu(const QPoint &pos)
{
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    if (!pSender)
        return;

    QUrl url;
    if (pSender == m_pContentWidget)
        url = contentWidgetUrl(m_pContentWidget->currentIndex());
    else if (pSender == m_pSearchResultWidget)
    {
        QTextBrowser* browser = m_pSearchResultWidget->findChild<QTextBrowser*>();
        if (!browser)
            return;
        QPoint browserPos = browser->mapFromGlobal(m_pSearchResultWidget->mapToGlobal(pos));
        url = browser->anchorAt(browserPos);
    }
    else if (pSender == m_pBookmarksWidget)
    {
        /* Assuming that only the UIBookmarksListWidget under the m_pBookmarksWidget sends the context menu request: */
        UIBookmarksListWidget *pListWidget = m_pBookmarksWidget->findChild<UIBookmarksListWidget*>();
        if (!pListWidget)
            return;
        url = m_pBookmarksWidget->currentBookmarkUrl();
    }
    else
        return;

    bool fURLValid = url.isValid();

    QMenu menu;
    QAction *pOpen = menu.addAction(tr("Open Link"));
    if (url.scheme() == "qthelp")
    {
        QAction *pOpenInNewTab = menu.addAction(tr("Open Link in New Tab"));
        pOpenInNewTab->setData(url);
        pOpenInNewTab->setEnabled(fURLValid);
        connect(pOpenInNewTab, &QAction::triggered, this, &UIHelpBrowserWidget::sltOpenLinkInNewTab);
    }
    QAction *pCopyLink = menu.addAction(tr("Copy Link"));

    pOpen->setData(url);
    pCopyLink->setData(url);

    pOpen->setEnabled(fURLValid);
    pCopyLink->setEnabled(fURLValid);

    connect(pOpen, &QAction::triggered, this, &UIHelpBrowserWidget::sltOpenLink);
    connect(pCopyLink, &QAction::triggered, this, &UIHelpBrowserWidget::sltCopyLink);

    if (pSender == m_pBookmarksWidget)
    {
        menu.addSeparator();
        QAction *pDeleteBookmark = menu.addAction(tr("Delete Bookmark"));
        QAction *pDeleteAllBookmarks = menu.addAction(tr("Delete All Bookmarks"));
        pDeleteBookmark->setEnabled(fURLValid);

        connect(pDeleteBookmark, &QAction::triggered, m_pBookmarksWidget, &UIBookmarksListContainer::sltDeleteSelectedBookmark);
        connect(pDeleteAllBookmarks, &QAction::triggered, m_pBookmarksWidget, &UIBookmarksListContainer::sltDeleteAllBookmarks);
    }

    menu.exec(pSender->mapToGlobal(pos));
}

void UIHelpBrowserWidget::sltOpenLinkInNewTab()
{
    openLinkSlotHandler(sender(), true);
}

void UIHelpBrowserWidget::sltOpenLink()
{
    openLinkSlotHandler(sender(), false);
}

void UIHelpBrowserWidget::sltCopyLink()
{
    QAction *pAction = qobject_cast<QAction*>(sender());
    if (!pAction)
        return;
    QUrl url = pAction->data().toUrl();
    if (url.isValid())
    {
        QClipboard *pClipboard = QApplication::clipboard();
        if (pClipboard)
            pClipboard->setText(url.toString());
    }
}

void UIHelpBrowserWidget::sltAddNewBookmark(const QUrl &url, const QString &strTitle)
{
    if (m_pBookmarksWidget)
        m_pBookmarksWidget->addBookmark(url, strTitle);
    Q_UNUSED(url);
    emit sigStatusBarMessage(QString("%1 %2").arg(tr("Bookmark added:")).arg(strTitle), 3000);
}

void UIHelpBrowserWidget::openLinkSlotHandler(QObject *pSenderObject, bool fOpenInNewTab)
{
    QAction *pAction = qobject_cast<QAction*>(pSenderObject);
    if (!pAction)
        return;
    QUrl url = pAction->data().toUrl();
    if (m_pTabManager && url.isValid())
        m_pTabManager->setSource(url, fOpenInNewTab);
}

void UIHelpBrowserWidget::updateTabsMenu(const QStringList &titles)
{
    if (!m_pTabsMenu)
        return;
    m_pTabsMenu->clear();

    QAction *pCloseTabAction = m_pTabsMenu->addAction(tr("Close T&ab"));
    QAction *pCloseOtherTabsAction = m_pTabsMenu->addAction(tr("Close &Other Tabs"));

    pCloseTabAction->setShortcut(QString("Ctrl+W"));
    pCloseOtherTabsAction->setShortcut(QString("Ctrl+Shift+W"));

    pCloseTabAction->setEnabled(titles.size() > 1);
    pCloseOtherTabsAction->setEnabled(titles.size() > 1);

    connect(pCloseTabAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltCloseCurrentTab);
    connect(pCloseOtherTabsAction, &QAction::triggered, m_pTabManager, &UIHelpBrowserTabManager::sltCloseOtherTabs);

    m_pTabsMenu->addSeparator();

    for (int i = 0; i < titles.size(); ++i)
    {
        QAction *pAction = m_pTabsMenu->addAction(titles[i]);
        pAction->setData(i);
        connect(pAction, &QAction::triggered, this, &UIHelpBrowserWidget::sltTabChoose);
    }
    if (m_pTabManager)
        sltCurrentTabChanged(m_pTabManager->currentIndex());
}

void UIHelpBrowserWidget::sltOpenLinkWithUrl(const QUrl &url)
{
    if (m_pTabManager && url.isValid())
        m_pTabManager->setSource(url, false);
}

void UIHelpBrowserWidget::sltZoomActions(int iZoomOperation)
{
    if (iZoomOperation >= (int) UIHelpViewer::ZoomOperation_Max)
        return;
    UIHelpViewer::ZoomOperation enmOperation = (UIHelpViewer::ZoomOperation)(iZoomOperation);
    m_pTabManager->sltHandleZoomRequest(enmOperation);
}

void UIHelpBrowserWidget::sltTabListChanged(const QStringList &titleList)
{
    updateTabsMenu(titleList);
}

void UIHelpBrowserWidget::sltTabChoose()
{
    QAction *pAction = qobject_cast<QAction*>(sender());
    if (!pAction)
        return;
    int iIndex = pAction->data().toInt();
    if (m_pTabManager)
        m_pTabManager->switchToTab(iIndex);
}

void UIHelpBrowserWidget::sltCurrentTabChanged(int iIndex)
{
    Q_UNUSED(iIndex);
    if (!m_pTabsMenu)
        return;

    /** Mark the action with iIndex+3 by assigning an icon to it. it is iIndex+3 and not iIndex since we have
      * two additional (close tab, close other tabs and a separator) action on top of the tab selection actions: */
    QList<QAction*> list = m_pTabsMenu->actions();
    for (int i = 0; i < list.size(); ++i)
        list[i]->setIcon(QIcon());
    if (iIndex+3 >= list.size())
        return;
    list[iIndex+3]->setIcon(UIIconPool::iconSet(":/help_browser_star_16px.png"));

    if (m_pTabManager)
    {
        if (m_pCopySelectedTextAction)
            m_pCopySelectedTextAction->setEnabled(m_pTabManager->hasCurrentTabSelectedText());
        if (m_pFindInPageAction)
            m_pFindInPageAction->setChecked(m_pTabManager->isFindInPageWidgetVisible());
        if (m_pFindNextInPageAction)
            m_pFindNextInPageAction->setEnabled(m_pTabManager->isFindInPageWidgetVisible());
        if (m_pFindPreviousInPageAction)
            m_pFindPreviousInPageAction->setEnabled(m_pTabManager->isFindInPageWidgetVisible());
    }
}

void UIHelpBrowserWidget::sltZoomPercentageChanged(int iPercentage)
{
    if (m_pZoomMenuAction)
        m_pZoomMenuAction->setZoomPercentage(iPercentage);
    emit sigZoomPercentageChanged(iPercentage);
}

void UIHelpBrowserWidget::addActionToMenu(QMenu *pMenu, QAction *pAction)
{
    if (!pMenu || !pAction)
        return;
    pMenu->addAction(pAction);
}

#include "UIHelpBrowserWidget.moc"
