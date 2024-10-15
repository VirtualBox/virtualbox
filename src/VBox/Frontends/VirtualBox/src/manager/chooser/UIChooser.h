/* $Id: UIChooser.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooser class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooser_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooser_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* Forward declarations: */
class UIActionPool;
class UIChooserModel;
class UIChooserView;
class UIVirtualMachineItem;
class UIVirtualMachineItemCloud;

/** QWidget extension used as VM Chooser-pane. */
class UIChooser : public QWidget
{
    Q_OBJECT;

signals:

    /** @name Group saving stuff.
      * @{ */
        /** Notifies listeners about group saving state change. */
        void sigGroupSavingStateChanged();
    /** @} */

    /** @name Cloud update stuff.
      * @{ */
        /** Notifies listeners about cloud profile state change.
          * @param  strProviderShortName  Brings the cloud provider short name.
          * @param  strProfileName        Brings the cloud profile name. */
        void sigCloudProfileStateChange(const QString &strProviderShortName,
                                        const QString &strProfileName);
        /** Notifies listeners about cloud machine state change.
          * @param  uId  Brings the cloud machine ID. */
        void sigCloudMachineStateChange(const QUuid &uId);

        /** Notifies listeners about cloud update state change. */
        void sigCloudUpdateStateChanged();
    /** @} */

    /** @name Tool stuff.
      * @{ */
        /** Notifies listeners about tool popup-menu request for certain @a position and optionally machine @a pItem. */
        void sigToolMenuRequested(const QPoint &position, UIVirtualMachineItem *pItem);
    /** @} */

    /** @name Selection stuff.
      * @{ */
        /** Notifies listeners about selection changed. */
        void sigSelectionChanged();
        /** Notifies listeners about selection invalidated. */
        void sigSelectionInvalidated();

        /** Notifies listeners about group toggling started. */
        void sigToggleStarted();
        /** Notifies listeners about group toggling finished. */
        void sigToggleFinished();
    /** @} */

    /** @name Action stuff.
      * @{ */
        /** Notifies listeners about start or show request. */
        void sigStartOrShowRequest();
        /** Notifies listeners about machine search widget visibility changed to @a fVisible. */
        void sigMachineSearchWidgetVisibilityChanged(bool fVisible);
    /** @} */

public:

    /** Constructs Chooser-pane passing @a pParent to the base-class.
      * @param  pActionPool  Brings the action-pool reference.  */
    UIChooser(QWidget *pParent, UIActionPool *pActionPool);
    /** Destructs Chooser-pane. */
    virtual ~UIChooser() RT_OVERRIDE;

    /** @name General stuff.
      * @{ */
        /** Returns the action-pool reference. */
        UIActionPool *actionPool() const { return m_pActionPool; }

        /** Return the Chooser-model instance. */
        UIChooserModel *model() const { return m_pChooserModel; }
        /** Return the Chooser-view instance. */
        UIChooserView *view() const { return m_pChooserView; }
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Returns whether group saving is in progress. */
        bool isGroupSavingInProgress() const;
    /** @} */

    /** @name Cloud update stuff.
      * @{ */
        /** Defines whether real cloud nodes should be kept updated. */
        void setKeepCloudNodesUpdated(bool fUpdate);

        /** Returns whether at least one cloud profile currently being updated. */
        bool isCloudProfileUpdateInProgress() const;

        /** Returns a list of real cloud machine items. */
        QList<UIVirtualMachineItemCloud*> cloudMachineItems() const;
    /** @} */

    /** @name Current-item stuff.
      * @{ */
        /** Returns current-item. */
        UIVirtualMachineItem *currentItem() const;
        /** Returns a list of current-items. */
        QList<UIVirtualMachineItem*> currentItems() const;

        /** Returns whether group item is selected. */
        bool isGroupItemSelected() const;
        /** Returns whether global item is selected. */
        bool isGlobalItemSelected() const;
        /** Returns whether machine item is selected. */
        bool isMachineItemSelected() const;
        /** Returns whether local machine item is selected. */
        bool isLocalMachineItemSelected() const;
        /** Returns whether cloud machine item is selected. */
        bool isCloudMachineItemSelected() const;

        /** Returns whether single group is selected. */
        bool isSingleGroupSelected() const;
        /** Returns whether single local group is selected. */
        bool isSingleLocalGroupSelected() const;
        /** Returns whether single cloud provider group is selected. */
        bool isSingleCloudProviderGroupSelected() const;
        /** Returns whether single cloud profile group is selected. */
        bool isSingleCloudProfileGroupSelected() const;
        /** Returns whether all items of one group are selected. */
        bool isAllItemsOfOneGroupSelected() const;

        /** Returns full name of currently selected group. */
        QString fullGroupName() const;
    /** @} */

    /** @name Action handling stuff.
      * @{ */
        /** Opens group name editor. */
        void openGroupNameEditor();
        /** Disbands group. */
        void disbandGroup();
        /** Removes machine. */
        void removeMachine();
        /** Moves machine to a group with certain @a strName. */
        void moveMachineToGroup(const QString &strName);
        /** Returns possible groups for machine with passed @a uId to move to. */
        QStringList possibleGroupsForMachineToMove(const QUuid &uId);
        /** Returns possible groups for group with passed @a strFullName to move to. */
        QStringList possibleGroupsForGroupToMove(const QString &strFullName);
        /** Refreshes machine. */
        void refreshMachine();
        /** Sorts group. */
        void sortGroup();
        /** Toggle machine search widget to be @a fVisible. */
        void setMachineSearchWidgetVisibility(bool fVisible);
        /** Changes current machine to the one with certain @a uId. */
        void setCurrentMachine(const QUuid &uId);
        /** Set global tools to be the current item. */
        void setCurrentGlobal();
    /** @} */

public slots:

    /** @name Layout stuff.
      * @{ */
        /** Defines global item @a iHeight. */
        void setGlobalItemHeightHint(int iHeight);
    /** @} */

private slots:

    /** @name General stuff.
      * @{ */
        /** Handles signal about tool popup-menu request for certain @a position and optionally machine @a pItem. */
        void sltToolMenuRequested(const QPoint &position, UIVirtualMachineItem *pItem);
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares model. */
        void prepareModel();
        /** Prepares widgets. */
        void prepareWidgets();
        /** Prepares connections. */
        void prepareConnections();
        /** Init model. */
        void initModel();

        /** Deinit model. */
        void deinitModel();
        /** Cleanups connections. */
        void cleanupConnections();
        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the action-pool reference. */
        UIActionPool *m_pActionPool;

        /** Holds the Chooser-model instane. */
        UIChooserModel *m_pChooserModel;
        /** Holds the Chooser-view instane. */
        UIChooserView  *m_pChooserView;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooser_h */
