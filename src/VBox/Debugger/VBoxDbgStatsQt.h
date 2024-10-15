/* $Id: VBoxDbgStatsQt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Debugger GUI - Statistics.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef DEBUGGER_INCLUDED_SRC_VBoxDbgStatsQt_h
#define DEBUGGER_INCLUDED_SRC_VBoxDbgStatsQt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDbgBase.h"

#include <QTreeView>
#include <QTimer>
#include <QComboBox>
#include <QMenu>

class VBoxDbgStats;
class VBoxDbgStatsModel;
class VBoxDbgStatsSortFileProxyModel;

/** Pointer to a statistics sample. */
typedef struct DBGGUISTATSNODE *PDBGGUISTATSNODE;
/** Pointer to a const statistics sample. */
typedef struct DBGGUISTATSNODE const *PCDBGGUISTATSNODE;


/**
 * The VM statistics tree view.
 *
 * A tree representation of the STAM statistics.
 */
class VBoxDbgStatsView : public QTreeView, public VBoxDbgBase
{
    Q_OBJECT;

public:
    /**
     * Creates a VM statistics list view widget.
     *
     * @param   a_pDbgGui       Pointer to the debugger gui object.
     * @param   a_pModel        The model. Will take ownership of this and delete it
     *                          together with the view later
     * @param   a_pProxyModel   The proxy model object to use for sorting and
     *                          filtering.
     * @param   a_pParent       Parent widget.
     */
    VBoxDbgStatsView(VBoxDbgGui *a_pDbgGui, VBoxDbgStatsModel *a_pModel, VBoxDbgStatsSortFileProxyModel *a_pProxyModel,
                     VBoxDbgStats *a_pParent = NULL);

    /** Destructor. */
    virtual ~VBoxDbgStatsView();

    /**
     * Updates the view with current information from STAM.
     *
     * This will indirectly update the m_PatStr.
     *
     * @param   rPatStr     Selection pattern. NULL means everything, see STAM for further details.
     */
    void updateStats(const QString &rPatStr);

    /**
     * Shows or hides the unused rows.
     *
     * @param   a_fShow     Whether to show (@c true) or hide (@c false) unused
     *                      rows.
     */
    void setShowUnusedRows(bool a_fShow);

    /**
     * Resets the stats items matching the specified pattern.
     *
     * This pattern doesn't have to be the one used for update, thus m_PatStr isn't updated.
     *
     * @param   rPatStr     Selection pattern. NULL means everything, see STAM for further details.
     */
    void resetStats(const QString &rPatStr);

    /**
     * Resizes the columns to fit the content.
     */
    void resizeColumnsToContent();

    /**
     * Expands the trees matching the given expression.
     *
     * @param   rPatStr      Selection pattern.
     */
    void expandMatching(const QString &rPatStr);

protected:
    /**
     * @callback_method_impl{VBoxDbgStatsModel::FNITERATOR,
     * Worker for expandMatching}.
     */
    static bool expandMatchingCallback(PDBGGUISTATSNODE pNode, QModelIndex const &a_rIndex, const char *pszFullName, void *pvUser);

    /**
     * Expands or collapses a sub-tree.
     *
     * @param  a_rIndex     The root of the sub-tree.
     * @param  a_fExpanded  Expand/collapse.
     */
    void setSubTreeExpanded(QModelIndex const &a_rIndex, bool a_fExpanded);

    /**
     * Popup context menu.
     *
     * @param  a_pEvt       The event.
     */
    virtual void contextMenuEvent(QContextMenuEvent *a_pEvt);

    /**
     * Gets the model index of the root node.
     *
     * This takes proxy / no-proxy into account.
     *
     * @returns root index for.
     */
    QModelIndex myGetRootIndex(void) const;

protected slots:
    /**
     * Slot for handling the view/header context menu.
     * @param   a_rPos      The mouse location.
     */
    void headerContextMenuRequested(const QPoint &a_rPos);

    /** @name Action signal slots.
     * @{ */
    void actExpand();
    void actCollapse();
    void actRefresh();
    void actReset();
    void actCopy();
    void actToLog();
    void actToRelLog();
    void actAdjColumns();
    void actFilter();
    /** @} */


protected:
    /** Pointer to the data model. */
    VBoxDbgStatsModel *m_pVBoxModel;
    /** Pointer to the proxy data model for sorting & filtering. */
    VBoxDbgStatsSortFileProxyModel *m_pProxyModel;
    /** Either m_pVBoxModel or m_pProxyModel. */
    QAbstractItemModel *m_pModel;
    /** The current selection pattern. */
    QString m_PatStr;
    /** The parent widget. */
    VBoxDbgStats *m_pParent;

    /** Leaf item menu. */
    QMenu *m_pLeafMenu;
    /** Branch item menu. */
    QMenu *m_pBranchMenu;
    /** View menu. */
    QMenu *m_pViewMenu;

    /** The menu that's currently being executed. */
    QMenu *m_pCurMenu;
    /** The current index relating to the context menu.
     * Considered invalid if m_pCurMenu is NULL. */
    QModelIndex m_CurIndex;

    /** Expand Tree action. */
    QAction *m_pExpandAct;
    /** Collapse Tree action. */
    QAction *m_pCollapseAct;
    /** Refresh Tree action. */
    QAction *m_pRefreshAct;
    /** Reset Tree action. */
    QAction *m_pResetAct;
    /** Copy (to clipboard) action. */
    QAction *m_pCopyAct;
    /** To Log action. */
    QAction *m_pToLogAct;
    /** To Release Log action. */
    QAction *m_pToRelLogAct;
    /** Adjust the columns. */
    QAction *m_pAdjColumnsAct;
    /** Filter sub-tree action. */
    QAction *m_pFilterAct;
#if 0
    /** Save Tree (to file) action. */
    QAction *m_SaveFileAct;
    /** Load Tree (from file) action. */
    QAction *m_LoadFileAct;
    /** Take Snapshot action. */
    QAction *m_TakeSnapshotAct;
    /** Load Snapshot action. */
    QAction *m_LoadSnapshotAct;
    /** Diff With Snapshot action. */
    QAction *m_DiffSnapshotAct;
#endif
};



/**
 * The VM statistics window.
 *
 * This class displays the statistics of a VM. The UI contains
 * a entry field for the selection pattern, a refresh interval
 * spinbutton, and the tree view with the statistics.
 */
class VBoxDbgStats : public VBoxDbgBaseWindow
{
    Q_OBJECT;

public:
    /**
     * Creates a VM statistics list view widget.
     *
     * @param   a_pDbgGui       Pointer to the debugger gui object.
     * @param   pszFilter       Initial selection pattern. NULL means everything.
     *                          (See STAM for details.)
     * @param   pszExpand       Initial expansion pattern. NULL means nothing is
     *                          expanded.
     * @param   pszConfig       Advanced filter configuration (min/max/regexp on
     *                          sub-trees) and more.
     *
     * @param   uRefreshRate    The refresh rate. 0 means not to refresh and is the default.
     * @param   pParent         Parent widget.
     */
    VBoxDbgStats(VBoxDbgGui *a_pDbgGui, const char *pszFilter = NULL, const char *pszExpand = NULL,
                 const char *pszConfig = NULL, unsigned uRefreshRate = 0, QWidget *pParent = NULL);

    /** Destructor. */
    virtual ~VBoxDbgStats();

protected:
    /**
     * Destroy the widget on close.
     *
     * @param  a_pCloseEvt      The close event.
     */
    virtual void closeEvent(QCloseEvent *a_pCloseEvt);

protected slots:
    /** Apply the activated combobox pattern. */
    void apply(const QString &Str);
    /** The "All" button was pressed. */
    void applyAll();
    /** Refresh the data on timer tick and pattern changed. */
    void refresh();
    /**
     * Set the refresh rate.
     *
     * @param   iRefresh        The refresh interval in seconds.
     */
    void setRefresh(int iRefresh);

    /**
     * Change the focus to the pattern combo box.
     */
    void actFocusToPat();

    /** The 'show unused rows' checkbox changed. */
    void sltShowUnusedRowsChanged(int a_iState);

protected:

    /** The current selection pattern. */
    QString             m_PatStr;
    /** The pattern combo box. */
    QComboBox          *m_pPatCB;
    /** The refresh rate in seconds.
     * 0 means not to refresh. */
    unsigned            m_uRefreshRate;
    /** The refresh timer .*/
    QTimer             *m_pTimer;
    /** The tree view widget. */
    VBoxDbgStatsView   *m_pView;

    /** Move to pattern field action. */
    QAction            *m_pFocusToPat;
};


#endif /* !DEBUGGER_INCLUDED_SRC_VBoxDbgStatsQt_h */

