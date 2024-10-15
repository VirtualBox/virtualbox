/* $Id: UISnapshotPane.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISnapshotPane class declaration.
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

#ifndef FEQT_INCLUDED_SRC_snapshots_UISnapshotPane_h
#define FEQT_INCLUDED_SRC_snapshots_UISnapshotPane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QUuid>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* COM includes: */
#include "CMachine.h"

/* Forward declarations: */
class QIcon;
class QReadWriteLock;
class QTimer;
class QTreeWidgetItem;
class QVBoxLayout;
class QIToolBar;
class QITreeWidgetItem;
class UIActionPool;
class UISnapshotDetailsWidget;
class UISnapshotItem;
class UISnapshotTree;
class UIVirtualMachineItem;
class CSnapshot;


/** Snapshot age format. */
enum SnapshotAgeFormat
{
    SnapshotAgeFormat_InSeconds,
    SnapshotAgeFormat_InMinutes,
    SnapshotAgeFormat_InHours,
    SnapshotAgeFormat_InDays,
    SnapshotAgeFormat_Max
};


/** QWidget extension providing GUI with the pane to control snapshot related functionality. */
class UISnapshotPane : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about current item change. */
    void sigCurrentItemChange();

public:

    /** Constructs snapshot pane passing @a pParent to the base-class. */
    UISnapshotPane(UIActionPool *pActionPool, bool fShowToolbar = true, QWidget *pParent = 0);
    /** Destructs snapshot pane. */
    virtual ~UISnapshotPane() RT_OVERRIDE;

    /** Defines the machine @a items to be parsed. */
    void setMachineItems(const QList<UIVirtualMachineItem*> &items);

    /** Returns cached snapshot-item icon depending on @a fOnline flag. */
    const QIcon *snapshotItemIcon(bool fOnline) const;

    /** Returns whether "current state" item selected. */
    bool isCurrentStateItemSelected() const;

    /** Returns currently selected snapshot ID if any. */
    QUuid currentSnapshotId();

protected:

    /** @name Qt event handlers.
      * @{ */
        /** Handles resize @a pEvent. */
        virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

        /** Handles show @a pEvent. */
        virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** @} */

private slots:

    /** @name Main event handlers.
      * @{ */
        /** Handles machine data change for machine with @a uMachineId. */
        void sltHandleMachineDataChange(const QUuid &uMachineId);
        /** Handles machine @a enmState change for machine with @a uMachineId. */
        void sltHandleMachineStateChange(const QUuid &uMachineId, const KMachineState enmState);

        /** Handles session @a enmState change for machine with @a uMachineId. */
        void sltHandleSessionStateChange(const QUuid &uMachineId, const KSessionState enmState);

        /** Handles snapshot take event for machine with @a uMachineId. */
        void sltHandleSnapshotTake(const QUuid &uMachineId, const QUuid &uSnapshotId);
        /** Handles snapshot delete event for machine with @a uMachineId. */
        void sltHandleSnapshotDelete(const QUuid &uMachineId, const QUuid &uSnapshotId);
        /** Handles snapshot change event for machine with @a uMachineId. */
        void sltHandleSnapshotChange(const QUuid &uMachineId, const QUuid &uSnapshotId);
        /** Handles snapshot restore event for machine with @a uMachineId. */
        void sltHandleSnapshotRestore(const QUuid &uMachineId, const QUuid &uSnapshotId);
    /** @} */

    /** @name Timer event handlers.
      * @{ */
        /** Updates snapshots age. */
        void sltUpdateSnapshotsAge();
    /** @} */

    /** @name Toolbar handlers.
      * @{ */
        /** Handles command to take a snapshot. */
        void sltTakeSnapshot() { takeSnapshot(); }
        /** Handles command to restore the snapshot. */
        void sltRestoreSnapshot() { restoreSnapshot(); }
        /** Handles command to delete the snapshot. */
        void sltDeleteSnapshot() { deleteSnapshot(); }
        /** Handles command to make snapshot details @a fVisible. */
        void sltToggleSnapshotDetailsVisibility(bool fVisible);
        /** Handles command to apply snapshot details changes. */
        void sltApplySnapshotDetailsChanges();
    /** @} */

    /** @name Tree-widget handlers.
      * @{ */
        /** Handles tree-widget current item change. */
        void sltHandleCurrentItemChange();
        /** Handles context menu request for tree-widget @a position. */
        void sltHandleContextMenuRequest(const QPoint &position);
        /** Handles tree-widget @a pItem change. */
        void sltHandleItemChange(QTreeWidgetItem *pItem);
        /** Handles tree-widget @a pItem double-click. */
        void sltHandleItemDoubleClick(QTreeWidgetItem *pItem);
        /** Handles tree-widget's scroll-bar visibility change. */
        void sltHandleScrollBarVisibilityChange();
    /** @} */

    /** @name Qt event handlers.
      * @{ */
        /** Handles translation event. */
        void sltRetranslateUI();
    /** @} */

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares connections. */
        void prepareConnections();
        /** Prepares actions. */
        void prepareActions();
        /** Prepares widgets. */
        void prepareWidgets();
        /** Prepares toolbar. */
        void prepareToolbar();
        /** Prepares tree-widget. */
        void prepareTreeWidget();
        /** Prepares details-widget. */
        void prepareDetailsWidget();
        /** Load settings: */
        void loadSettings();

        /** Refreshes everything. */
        void refreshAll();
        /** Populates snapshot items for corresponding @a comSnapshot using @a pItem as parent. */
        void populateSnapshots(const QUuid &uMachineId, const CSnapshot &comSnapshot, QITreeWidgetItem *pItem);

        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** @name Toolbar helpers.
      * @{ */
        /** Updates action states. */
        void updateActionStates();

        /** Proposes to take a snapshot. */
        bool takeSnapshot(bool fAutomatically = false);
        /** Proposes to delete the snapshot. */
        bool deleteSnapshot(bool fAutomatically = false);
        /** Proposes to restore the snapshot. */
        bool restoreSnapshot(bool fAutomatically = false);
    /** @} */

    /** @name Tree-widget helpers.
      * @{ */
        /** Handles command to adjust snapshot tree. */
        void adjustTreeWidget();

        /** Searches for an item with corresponding @a uSnapshotID. */
        UISnapshotItem *findItem(const QUuid &uSnapshotID) const;

        /** Searches for smallest snapshot age starting with @a pItem as parent. */
        SnapshotAgeFormat traverseSnapshotAge(QTreeWidgetItem *pItem) const;

        /** Expand all the children starting with @a pItem. */
        void expandItemChildren(QTreeWidgetItem *pItem);
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the action-pool reference. */
        UIActionPool *m_pActionPool;
        /** Holds whether we should show toolbar. */
        bool          m_fShowToolbar;

        /** Holds the COM machine object list. */
        QMap<QUuid, CMachine>       m_machines;
        /** Holds the cached session state list. */
        QMap<QUuid, KSessionState>  m_sessionStates;
        /** Holds the list of operation allowance states. */
        QMap<QUuid, bool>           m_operationAllowed;

        /** Holds the snapshot item editing protector. */
        QReadWriteLock *m_pLockReadWrite;

        /** Holds the cached snapshot-item pixmap for 'offline' state. */
        QIcon *m_pIconSnapshotOffline;
        /** Holds the cached snapshot-item pixmap for 'online' state. */
        QIcon *m_pIconSnapshotOnline;

        /** Holds the snapshot age update timer. */
        QTimer *m_pTimerUpdateAge;
    /** @} */

    /** @name Widget variables.
      * @{ */
        /** Holds the main layout instance. */
        QVBoxLayout *m_pLayoutMain;

        /** Holds the toolbar instance. */
        QIToolBar *m_pToolBar;

        /** Holds the snapshot tree instance. */
        UISnapshotTree *m_pSnapshotTree;

        /** Holds the "current snapshot" item list. */
        QMap<QUuid, UISnapshotItem*>  m_currentSnapshotItems;
        /** Holds the "current state" item list. */
        QMap<QUuid, UISnapshotItem*>  m_currentStateItems;

        /** Holds the details-widget instance. */
        UISnapshotDetailsWidget *m_pDetailsWidget;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_snapshots_UISnapshotPane_h */
