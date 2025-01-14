/* $Id: UIHelpBrowserWidget.h 107834 2025-01-14 15:44:02Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHelpBrowserWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_helpbrowser_UIHelpBrowserWidget_h
#define FEQT_INCLUDED_SRC_helpbrowser_UIHelpBrowserWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIManagerDialog.h" /* for EmbedTo */

/* Forward declarations: */
class QHBoxLayout;
class QVBoxLayout;
class QHelpEngine;
class QHelpContentModel;
class QHelpContentWidget;
class QHelpIndexWidget;
class QHelpSearchEngine;
class QHelpSearchQueryWidget;
class QHelpSearchResultWidget;
class QSplitter;
class QITabWidget;
class QIToolBar;
class UIActionPool;
class UIBookmarksListContainer;
class UIHelpBrowserTabManager;
class UIZoomMenuAction;

class SHARED_LIBRARY_STUFF UIHelpBrowserWidget  : public QWidget
{
    Q_OBJECT;

signals:

    void sigCloseDialog();
    void sigStatusBarVisible(bool fToggled);
    void sigZoomPercentageChanged(int iPercentage);
    void sigGoBackward();
    void sigGoForward();
    void sigReloadPage();
    void sigAddBookmark();
    void sigStatusBarMessage(const QString &strMessage, int iTimeOut);

public:

    UIHelpBrowserWidget(EmbedTo enmEmbedding, const QString &strHelpFilePath, QWidget *pParent = 0);
    ~UIHelpBrowserWidget();
    QList<QMenu*> menus() const;
    void showHelpForKeyword(const QString &strKeyword);
#ifdef VBOX_WS_MAC
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif
    int zoomPercentage() const;

protected:

    /** Returns whether the window should be maximized when geometry being restored. */
    virtual bool shouldBeMaximized() const;

private slots:

    void sltHelpEngineSetupFinished();
    void sltContentWidgetItemClicked(const QModelIndex &index);
    void sltWidgetVisibilityToggle(bool togggled);
    void sltShowPrintDialog();
    void sltContentsCreated();
    void sltIndexingStarted();
    void sltIndexingFinished();
    void sltSearchingStarted();
    void sltSearchStart();
    void sltViewerSourceChange(const QUrl &source);
    void sltOpenLinkWithUrl(const QUrl &url);
    void sltShowLinksContextMenu(const QPoint &pos);
    void sltOpenLinkInNewTab();
    void sltOpenLink();
    void sltCopyLink();
    void sltAddNewBookmark(const QUrl &url, const QString &strTitle);
    void sltZoomActions(int iZoomOperation);
    void sltTabListChanged(const QStringList &titleList);
    void sltTabChoose();
    void sltCurrentTabChanged(int iIndex);
    void sltZoomPercentageChanged(int iPercentage);
    void sltCopySelectedText();
    void sltCopyAvailableChanged(bool fAvailable);
    void sltFindInPage(bool fChecked);
    void sltFindInPageWidgetVisibilityChanged(bool fVisible);
    void sltFindNextInPage();
    void sltFindPreviousInPage();
    void sltHistoryChanged(bool fBackwardAvailable, bool fForwardAvailable);
    void sltLinkHighlighted(const QUrl &url);
    void sltMouseOverImage(const QString &strImageName);
    void sltRetranslateUI();
    void sltCommitDataSignalReceived();
    void sltGoHome();

private:

    void prepare();
    void prepareActions();
    void prepareWidgets();
    void prepareSearchWidgets();
    void prepareToolBar();
    void prepareMenu();
    void prepareConnections();

    void loadOptions();
    QStringList loadSavedUrlList();
    /** Bookmark list is save as url-title pairs. */
    void loadBookmarks();
    void saveBookmarks();
    void saveOptions();
    void cleanup();
    QUrl findHomeUrl() const;
    /* Returns the url of the item with @p itemIndex. */
    QUrl contentWidgetUrl(const QModelIndex &itemIndex) const;
    void openLinkSlotHandler(QObject *pSenderObject, bool fOpenInNewTab);
    void updateTabsMenu(const QStringList &titleList);

    /** @name Event handling stuff.
     * @{ */
       /** Handles Qt show @a pEvent. */
       virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
       /** Handles Qt key-press @a pEvent. */
       virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    /** @} */
    /* Looks for Url for the keyword using QHelpEngine API and shows it in a new tab whne successful. */
    void findAndShowUrlForKeyword(const QString &strKeyword);
    void addActionToMenu(QMenu *pMenu, QAction *pAction);

    /** Holds the widget's embedding type. */
    const EmbedTo m_enmEmbedding;
    UIActionPool *m_pActionPool;
    bool m_fIsPolished;

    QVBoxLayout         *m_pMainLayout;
    QHBoxLayout         *m_pTopLayout;
    /** Container tab widget for content, index, bookmark widgets. Sits on a side bar. */
    QITabWidget *m_pTabWidget;

    /** @name Toolbar and menu variables.
     * @{ */
       QIToolBar *m_pToolBar;
    /** @} */

    QString       m_strHelpFilePath;
    /** Start the browser with this keyword. When not empty widget is shown `only` with html viewer and single tab.*/
    QHelpEngine  *m_pHelpEngine;
    QSplitter           *m_pSplitter;
    QMenu               *m_pFileMenu;
    QMenu               *m_pEditMenu;
    QMenu               *m_pViewMenu;
    QMenu               *m_pTabsMenu;
    QMenu               *m_pNavigationMenu;
    QHelpContentWidget  *m_pContentWidget;
    QHelpIndexWidget    *m_pIndexWidget;
    QHelpContentModel   *m_pContentModel;
    QHelpSearchEngine   *m_pSearchEngine;
    QHelpSearchQueryWidget *m_pSearchQueryWidget;
    QHelpSearchResultWidget  *m_pSearchResultWidget;
    UIHelpBrowserTabManager  *m_pTabManager;
    UIBookmarksListContainer *m_pBookmarksWidget;
    QWidget *m_pSearchContainerWidget;
    QAction *m_pPrintAction;
    QAction *m_pQuitAction;
    QAction *m_pShowHideSideBarAction;
    QAction *m_pShowHideToolBarAction;
    QAction *m_pShowHideStatusBarAction;
    QAction *m_pCopySelectedTextAction;
    QAction *m_pFindInPageAction;
    QAction *m_pFindNextInPageAction;
    QAction *m_pFindPreviousInPageAction;
    QAction *m_pBackwardAction;
    QAction *m_pForwardAction;
    QAction *m_pHomeAction;
    QAction *m_pReloadPageAction;
    QAction *m_pAddBookmarkAction;

    UIZoomMenuAction    *m_pZoomMenuAction;

    /* This is set t true when handling QHelpContentModel::contentsCreated signal. */
    bool                 m_fModelContentCreated;
    bool                 m_fIndexingFinished;
    bool                 m_fCommitDataSignalReceived;
    /** This queue is used in unlikely case where possibly several keywords are requested to be shown
      *  but indexing is not yet finished. In that case we queue the keywords and process them after
      * after indexing is finished. */
    QStringList          m_keywordList;
};

#endif /* !FEQT_INCLUDED_SRC_helpbrowser_UIHelpBrowserWidget_h */
