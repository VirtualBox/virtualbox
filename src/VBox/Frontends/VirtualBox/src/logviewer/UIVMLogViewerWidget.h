/* $Id: UIVMLogViewerWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewerWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerWidget_h
#define FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QKeySequence>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QWidget>
#include <QUuid>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QVBoxLayout;
class QIToolBar;
class QIToolButton;
class UIActionPool;
class UIDialogPanel;
class UIMachineListMenu;
class UITabWidget;
class UIVirtualMachineItem;
class UIVMLogPage;
class UIVMLogTab;
class UIVMLogViewerBookmarksPanel;
class UIVMLogViewerPaneContainer;
class UIVMLogViewerFilterPanel;
class UIVMLogViewerPanel;
class CMachine;

/** QWidget extension providing GUI for VirtualBox LogViewer. It
 *  encapsulates log pages, toolbar, a tab widget and manages
 *  interaction between these classes. */
class SHARED_LIBRARY_STUFF UIVMLogViewerWidget  : public QWidget
{
    Q_OBJECT;

signals:

    void sigSetCloseButtonShortCut(QKeySequence);
    void sigDetach();

public:

    /** Constructs the VM Log-Viewer by passing @a pParent to QWidget base-class constructor.
      * @param  enmEmbedding  Brings the type of widget embedding.
      * @param  pActionPool   Brings the action-pool reference.
      * @param  fShowToolbar  Brings whether we should create/show toolbar.
      * @param  machineIDs    Brings the list of machine IDs. */
    UIVMLogViewerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                        bool fShowToolbar = true, const QList<QUuid> &machineIDs = QList<QUuid>(), QWidget *pParent = 0);
    ~UIVMLogViewerWidget();

    /** Returns the widget's embedding type. */
    EmbedTo embeddingType() const { return m_enmEmbedding; }

    /** Returns the width of the current log page. return 0 if there is no current log page: */
    int defaultLogPageWidth() const;

    /** Returns the menu. */
    QMenu *menu() const;

#ifdef VBOX_WS_MAC
    /** Returns the toolbar. */
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

    void setSelectedVMListItems(const QList<UIVirtualMachineItem*> &items);
    QFont currentFont() const;

protected:

    /** Returns whether the window should be maximized when geometry being restored. */
    virtual bool shouldBeMaximized() const;

private slots:

    /** Rereads the log file shown in the current tab. */
    void sltRefresh();
    /** Rereads all the log files . */
    void sltReload();
    /** Handles save action triggering. */
    void sltSave();

    /** @name Bookmark related slots
     * @{ */
    /** Deletes the bookmark with @p index from the current logs bookmark list. */
        void sltDeleteBookmarkByIndex(int index);
        /** Receives delete all signal from the bookmark panel and notifies UIVMLogPage. */
        void sltDeleteAllBookmarks();
        /** Manages bookmark panel update when bookmark vector is updated. */
        void sltUpdateBookmarkPanel();
        /** Makes the current UIVMLogPage to goto (scroll) its bookmark with index @a index. */
        void gotoBookmark(int bookmarkIndex);
    /** @} */

    void sltPanelActionToggled(bool fChecked);
    /** Handles the search result highlight changes. */
    void sltSearchResultHighLigting();
    void sltHandleSearchUpdated();
    /** Handles the tab change of the logviewer. */
    void sltCurrentTabChanged(int tabIndex);
    void sltFilterApplied();
    /* Handles the UIVMLogPage signal which is emitted when isFiltered property
       of UIVMLogPage is changed. */
    void sltLogPageFilteredChanged(bool isFiltered);

    /** @name Slots to handle signals from settings panel
     * @{ */
        void sltShowLineNumbers(bool bShowLineNumbers);
        void sltWrapLines(bool bWrapLine);
        void sltFontSizeChanged(int fontSize);
        void sltChangeFont(QFont font);
        void sltResetOptionsToDefault();
    /** @} */
    void sltCloseMachineLogs();
    void sltCommitDataSignalReceived();
    void sltPanelContainerHidden();
    void sltPanelCurrentTabChanged(int iIndex);
    void sltShowSearchPane();
    void sltRetranslateUI();

private:

    /** @name Prepare/Cleanup
      * @{ */
        /** Prepares VM Log-Viewer. */
        void prepare();
        /** Prepares actions. */
        void prepareActions();
        /** Prepares widgets. */
        void prepareWidgets();
        /** Prepares toolbar. */
        void prepareToolBar();
        void saveOptions();
        /** Loads options.  */
        void loadOptions();
    /** @} */

    /** @name Event handling stuff.
      * @{ */
        /** Handles Qt show @a pEvent. */
        virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
        /** Handles Qt key-press @a pEvent. */
        virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    /** @} */

    /** Returns the newly created log-page using @a strPage filename. */
    void createLogPage(const QString &strFileName,
                       const QString &strMachineName,
                       const QUuid &machineId, int iLogFileId,
                       const QString &strLogContent, bool noLogsToShow);

    const UIVMLogPage *currentLogPage() const;
    UIVMLogPage *currentLogPage();
    /** Returns the log tab at tab with iIndex if it contains a log page. Return 0 otherwise. */
    UIVMLogTab *logTab(int iIndex);
    UIVMLogPage *logPage(int iIndex);
    /** Returns a vector of all the log pages of the tab widget. */
    QVector<UIVMLogTab*> logTabs();

    void createLogViewerPages(const QVector<QUuid> &machineList);
    /** Removes the log pages/tabs that shows logs of the machines from @p machineList. */
    void removeLogViewerPages(const QVector<QUuid> &machineList);
    void removeAllLogPages();
    void markLabelTabs();

    /** Resets document (of the current tab) and scrollbar highligthing */
    void resetHighlighthing();
    void setMachines(const QVector<QUuid> &machineIDs);
    /** Returns the content of the ith log file of @comMachine or possibly an empty string */
    QString readLogFile(const CMachine &comConstMachine, int iLogFileId);
    /** If the current tab is a label tab then switch to the next tab and return true. Returns false otherwise. */
    bool labelTabHandler();
    void uncheckPaneActions();

    /** Holds the widget's embedding type. */
    const EmbedTo m_enmEmbedding;
    /** Holds the action-pool reference. Wrapped around with QPointer to avoid use-after-delete case during vm window close.*/
    QPointer<UIActionPool> m_pActionPool;
    /** Holds whether we should create/show toolbar. */
    const bool    m_fShowToolbar;
    QVector<QUuid> m_machines;

    /** Holds whether the dialog is polished. */
    bool m_fIsPolished;

    /** Holds container for log-pages. */
    UITabWidget        *m_pTabWidget;

    /** @name Panel instances and a QMap for mapping panel instances to related actions.
      * @{ */
        QList<UIDialogPanel*>          m_visiblePanelsList;
    /** @} */
    QVBoxLayout         *m_pMainLayout;

    /** @name Toolbar and menu variables.
      * @{ */
        QIToolBar *m_pToolBar;
    /** @} */

    /** @name Toolbar and menu variables. Cache these to restore them after refresh.
     * @{ */
        /** Showing/hiding line numbers and line wraping options are set per
            UIVMLogViewerWidget and applies to all log pages (all tabs) */
        bool  m_bShowLineNumbers;
        bool  m_bWrapLines;
        QFont m_font;
    /** @} */
    QIToolButton *m_pCornerButton;
    UIMachineListMenu *m_pMachineSelectionMenu;
    /** All extra data saves are done dynamically (as an option changes etc.). The this flag is true
      * we should not try to save anything to extra data anymore. */
    bool m_fCommitDataSignalReceived;
    QPointer<UIVMLogPage> m_pPreviousLogPage;
    UIVMLogViewerPaneContainer *m_pPanel;
    QSet<QAction*> m_paneActions;
    friend class UIVMLogViewerFilterWidget;
    friend class UIVMLogViewerPane;
    friend class UIVMLogViewerDialog;
};

#endif /* !FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerWidget_h */
