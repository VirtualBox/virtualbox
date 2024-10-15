/* $Id: UIVMActivityOverviewWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMActivityOverviewWidget class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_activity_overview_UIVMActivityOverviewWidget_h
#define FEQT_INCLUDED_SRC_activity_overview_UIVMActivityOverviewWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMainWindow>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QAbstractButton;
class QFrame;
class QItemSelection;
class QLabel;
class QTableView;
class QTreeWidgetItem;
class QIDialogButtonBox;
class UIActionPool;
class QIToolBar;
class UIVMActivityOverviewProxyModel;
class UIVMActivityOverviewModel;
class UIVirtualMachineItemCloud;
class UIVMActivityOverviewHostStats;
class UIVMActivityOverviewHostStatsWidget;
class UIVMActivityOverviewTableView;
class UIVMActivityOverviewTableView;
class UIVMActivityOverviewModel;
class UIVMActivityOverviewProxyModel;

/** QWidget extension to display a Linux top like utility that sort running vm wrt. resource allocations. */
class UIVMActivityOverviewWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigSwitchToMachineActivityPane(const QUuid &uMachineId);

public:

    UIVMActivityOverviewWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                               bool fShowToolbar = true, QWidget *pParent = 0);
    QMenu *columnVisiblityToggleMenu() const;
    QMenu *menu() const;

    bool isCurrentTool() const;
    void setIsCurrentTool(bool fIsCurrentTool);
    void setCloudMachineItems(const QList<UIVirtualMachineItemCloud*> &cloudItems);

#ifdef VBOX_WS_MAC
    QIToolBar *toolbar() const { return m_pToolBar; }
#endif

protected:

    /** @name Event-handling stuff.
      * @{ */
        virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** @} */

private slots:

    void sltHandleDataUpdate();
    void sltToggleColumnSelectionMenu(bool fChecked);
    void sltHandleColumnAction(bool fChecked);
    void sltHandleHostStatsUpdate(const UIVMActivityOverviewHostStats &stats);
    void sltHandleTableContextMenuRequest(const QPoint &pos);
    void sltHandleShowVMActivityMonitor();
    void sltHandleTableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void sltNotRunningVMVisibility(bool fShow);
    void sltCloudVMVisibility(bool fShow);
    void sltSaveSettings();
    void sltClearCOMData();
    void sltRetranslateUI();

private:

    void setColumnVisible(int iColumnId, bool fVisible);
    bool columnVisible(int iColumnId) const;
    void updateModelColumVisibilityCache();
    void computeMinimumColumnWidths();

    /** @name Prepare/cleanup cascade.
      * @{ */
        void prepare();
        void prepareWidgets();
        void prepareHostStatsWidgets();
        void prepareToolBar();
        void prepareActions();
        void updateColumnsMenu();
        void loadSettings();
    /** @} */

    /** @name General variables.
      * @{ */
        const EmbedTo m_enmEmbedding;
        UIActionPool *m_pActionPool;
        const bool    m_fShowToolbar;
    /** @} */

    /** @name Misc members.
      * @{ */
        QIToolBar *m_pToolBar;
        UIVMActivityOverviewTableView          *m_pTableView;
        UIVMActivityOverviewProxyModel *m_pProxyModel;
        UIVMActivityOverviewModel   *m_pModel;
        QMenu                              *m_pColumnVisibilityToggleMenu;
        /* The key is the column id (VMActivityOverviewColumn) and value is column title. */
        QMap<int, QString>                  m_columnTitles;
        /* The key is the column id (VMActivityOverviewColumn) and value is true if the column is visible. */
        QMap<int, bool>                     m_columnVisible;
        UIVMActivityOverviewHostStatsWidget *m_pHostStatsWidget;
        QAction                             *m_pVMActivityMonitorAction;
    /** @} */
    /** Indicates if this widget's host tool is current tool. */
    bool    m_fIsCurrentTool;
    int     m_iSortIndicatorWidth;
    bool    m_fShowNotRunningVMs;
    bool    m_fShowCloudVMs;
};

#endif /* !FEQT_INCLUDED_SRC_activity_overview_UIVMActivityOverviewWidget_h */
