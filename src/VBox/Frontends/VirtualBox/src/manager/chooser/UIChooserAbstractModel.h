/* $Id: UIChooserAbstractModel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserAbstractModel class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserAbstractModel_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserAbstractModel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSet>
#include <QUuid>

/* GUI includes: */
#include "UIChooserDefs.h"
#include "UICloudEntityKey.h"
#include "UIManagerDefs.h"

/* COM includes: */
#include "CCloudMachine.h" /* required for Qt6 / c++17 */
#include "KMachineState.h"
#include "KSessionState.h"

/* Forward declaration: */
class UIChooser;
class UIChooserNode;
class UIVirtualMachineItemCloud;
class CMachine;

/** QObject extension used as VM Chooser-pane abstract model.
  * This class is used to load/save a tree of abstract invisible
  * nodes representing VMs and their groups from/to extra-data. */
class UIChooserAbstractModel : public QObject
{
    Q_OBJECT;

signals:

    /** @name Group saving stuff.
      * @{ */
        /** Issues request to save settings. */
        void sigSaveSettings();
        /** Notifies listeners about group saving state changed. */
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

        /** Notifies listeners about cloud update state changed. */
        void sigCloudUpdateStateChanged();
    /** @} */

public:

    /** Constructs abstract Chooser-model passing @a pParent to the base-class. */
    UIChooserAbstractModel(UIChooser *pParent);
    /** Destructs abstract Chooser-model. */
    virtual ~UIChooserAbstractModel() RT_OVERRIDE;

    /** @name General stuff.
      * @{ */
        /** Inits model. */
        virtual void init();
        /** Deinits model. */
        virtual void deinit();
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Returns invisible root node instance. */
        UIChooserNode *invisibleRoot() const { return m_pInvisibleRootNode; }

        /** Wipes out empty groups. */
        void wipeOutEmptyGroups();

        /** Returns possible group node names for machine node with passed @a uId to move to. */
        QStringList possibleGroupNodeNamesForMachineNodeToMove(const QUuid &uId);
        /** Returns possible group node names for group node with passed @a strFullName to move to. */
        QStringList possibleGroupNodeNamesForGroupNodeToMove(const QString &strFullName);

        /** Generates unique group name traversing recursively starting from @a pRoot. */
        static QString uniqueGroupName(UIChooserNode *pRoot);
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Performs a search using @a strSearchTerm and @a iSearchFlags specified. */
        virtual void performSearch(const QString &strSearchTerm, int iSearchFlags);
        /** Resets the search result data members and disables item's visual effects.
          * Also returns a list of all nodes which may be utilized by the calling code. */
        virtual QList<UIChooserNode*> resetSearch();
        /** Returns search result. */
        QList<UIChooserNode*> searchResult() const;
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Commands to save groups. */
        void saveGroups();
        /** Returns whether group saving is in progress. */
        bool isGroupSavingInProgress() const;

        /** Returns QString representation for passed @a uId, wiping out {} symbols.
          * @note  Required for backward compatibility after QString=>QUuid change. */
        static QString toOldStyleUuid(const QUuid &uId);

        /** Returns node extra-data prefix of certain @a enmType. */
        static QString prefixToString(UIChooserNodeDataPrefixType enmType);
        /** Returns node extra-data option of certain @a enmType. */
        static QString optionToString(UIChooserNodeDataOptionType enmType);
        /** Returns node extra-data value of certain @a enmType. */
        static QString valueToString(UIChooserNodeDataValueType enmType);
    /** @} */

    /** @name Cloud update stuff.
      * @{ */
        /** Returns whether real cloud nodes should be kept updated. */
        bool isKeepCloudNodesUpdated() const { return m_fKeepCloudNodesUpdated; }
        /** Defines whether real cloud nodes should be kept updated. */
        void setKeepCloudNodesUpdated(bool fUpdate);

        /** Inserts cloud entity @a key into a set of keys currently being updated. */
        void insertCloudEntityKey(const UICloudEntityKey &key);
        /** Removes cloud entity @a key from a set of keys currently being updated. */
        void removeCloudEntityKey(const UICloudEntityKey &key);
        /** Returns whether cloud entity @a key is a part of key set currently being updated. */
        bool containsCloudEntityKey(const UICloudEntityKey &key) const;

        /** Returns whether at least one cloud profile currently being updated. */
        bool isCloudProfileUpdateInProgress() const;

        /** Returns a list of real cloud machine items. */
        QList<UIVirtualMachineItemCloud*> cloudMachineItems() const;
    /** @} */

public slots:

    /** @name Cloud machine stuff.
      * @{ */
        /** Handles cloud machine refresh started. */
        void sltHandleCloudMachineRefreshStarted();
        /** Handles cloud machine refresh finished. */
        void sltHandleCloudMachineRefreshFinished();
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Handles group settings saving complete. */
        void sltGroupSettingsSaveComplete();
        /** Handles group definitions saving complete. */
        void sltGroupDefinitionsSaveComplete();
    /** @} */

protected slots:

    /** @name Main event handling stuff.
      * @{ */
        /** Handles local machine @a enmState change for machine with certain @a uMachineId. */
        virtual void sltLocalMachineStateChanged(const QUuid &uMachineId, const KMachineState enmState);
        /** Handles local machine data change for machine with certain @a uMachineId. */
        virtual void sltLocalMachineDataChanged(const QUuid &uMachineId);
        /** Handles local machine registering/unregistering for machine with certain @a uMachineId. */
        virtual void sltLocalMachineRegistrationChanged(const QUuid &uMachineId, const bool fRegistered);
        /** Handles local machine registering/unregistering for machine with certain @a uMachineId. */
        virtual void sltLocalMachineGroupsChanged(const QUuid &uMachineId);

        /** Handles session @a enmState change for machine with certain @a uMachineId. */
        virtual void sltSessionStateChanged(const QUuid &uMachineId, const KSessionState enmState);

        /** Handles snapshot change for machine/snapshot with certain @a uMachineId / @a uSnapshotId. */
        virtual void sltSnapshotChanged(const QUuid &uMachineId, const QUuid &uSnapshotId);

        /** Handles event about cloud provider with @a uProviderId being uninstalled. */
        virtual void sltHandleCloudProviderUninstall(const QUuid &uProviderId);
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Handles reload machine with certain @a uMachineId request. */
        virtual void sltReloadMachine(const QUuid &uMachineId);

        /** Handles command to commit data. */
        virtual void sltCommitData();
        /** Handles command to detach COM. */
        virtual void sltDetachCOM();
    /** @} */

    /** @name Cloud stuff.
      * @{ */
        /** Handles cloud machine unregistering for @a uId.
          * @param  strProviderShortName  Brings provider short name.
          * @param  strProfileName        Brings profile name. */
        virtual void sltCloudMachineUnregistered(const QString &strProviderShortName,
                                                 const QString &strProfileName,
                                                 const QUuid &uId);
        /** Handles cloud machine unregistering for a list of @a ids.
          * @param  strProviderShortName  Brings provider short name.
          * @param  strProfileName        Brings profile name. */
        virtual void sltCloudMachinesUnregistered(const QString &strProviderShortName,
                                                  const QString &strProfileName,
                                                  const QList<QUuid> &ids);
        /** Handles cloud machine registering for @a comMachine.
          * @param  strProviderShortName  Brings provider short name.
          * @param  strProfileName        Brings profile name. */
        virtual void sltCloudMachineRegistered(const QString &strProviderShortName,
                                               const QString &strProfileName,
                                               const CCloudMachine &comMachine);
        /** Handles cloud machine registering for a list of @a machines.
          * @param  strProviderShortName  Brings provider short name.
          * @param  strProfileName        Brings profile name. */
        virtual void sltCloudMachinesRegistered(const QString &strProviderShortName,
                                                const QString &strProfileName,
                                                const QVector<CCloudMachine> &machines);

        /** Handles read cloud machine list task complete signal. */
        virtual void sltHandleReadCloudMachineListTaskComplete();

        /** Handles Cloud Profile Manager cumulative change. */
        virtual void sltHandleCloudProfileManagerCumulativeChange();
    /** @} */

protected:

    /** @name Children stuff.
      * @{ */
        /** Creates and registers read cloud machine list task with @a guiCloudProfileKey.
          * @param  fWithRefresh  Brings whether machines should be refreshed as well. */
        void createReadCloudMachineListTask(const UICloudEntityKey &guiCloudProfileKey, bool fWithRefresh);
    /** @} */

private slots:

    /** @name Group saving stuff.
      * @{ */
        /** Handles request to save settings. */
        void sltSaveSettings();
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares connections. */
        void prepareConnections();

        /** Cleanups connections. */
        void cleanupConnections();
        /** Cleanups all. */
        void cleanup();
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Reloads local tree. */
        void reloadLocalTree();
        /** Reloads cloud tree. */
        void reloadCloudTree();

        /** Adds local machine item based on certain @a comMachine and optionally @a fMakeItVisible. */
        void addLocalMachineIntoTheTree(const CMachine &comMachine, bool fMakeItVisible = false);
        /** Adds cloud machine item based on certain @a comMachine and optionally @a fMakeItVisible, into @a strGroup. */
        void addCloudMachineIntoTheTree(const QString &strGroup, const CCloudMachine &comMachine, bool fMakeItVisible = false);

        /** Acquires local group node, creates one if necessary.
          * @param  strName           Brings the name of group we looking for.
          * @param  pParentNode       Brings the parent we starting to look for a group from.
          * @param  fAllGroupsOpened  Brings whether we should open all the groups till the required one. */
        UIChooserNode *getLocalGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened);
        /** Acquires cloud group node, never create new, returns root if nothing found.
          * @param  strName           Brings the name of group we looking for.
          * @param  pParentNode       Brings the parent we starting to look for a group from.
          * @param  fAllGroupsOpened  Brings whether we should open all the groups till the required one. */
        UIChooserNode *getCloudGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened);

        /** Returns whether group node * with specified @a enmDataType and @a strName should be opened,
          * searching starting from the passed @a pParentNode. */
        bool shouldGroupNodeBeOpened(UIChooserNode *pParentNode,
                                     UIChooserNodeDataPrefixType enmDataType,
                                     const QString &strName) const;
        /** Returns whether global node should be favorite,
          * searching starting from the passed @a pParentNode. */
        bool shouldGlobalNodeBeFavorite(UIChooserNode *pParentNode) const;

        /** Wipes out empty groups starting from @a pParentItem. */
        void wipeOutEmptyGroupsStartingFrom(UIChooserNode *pParentNode);

        /** Acquires desired position for a child of @a pParentNode with specified @a enmDataType and @a strName. */
        int getDesiredNodePosition(UIChooserNode *pParentNode, UIChooserNodeDataPrefixType enmDataType, const QString &strName);
        /** Acquires defined position for a child of @a pParentNode with specified @a enmDataType and @a strName. */
        int getDefinedNodePosition(UIChooserNode *pParentNode, UIChooserNodeDataPrefixType enmDataType, const QString &strName);

        /** Creates local machine node based on certain @a comMachine as a child of specified @a pParentNode. */
        void createLocalMachineNode(UIChooserNode *pParentNode, const CMachine &comMachine);
        /** Creates fake cloud machine node in passed @a enmState as a child of specified @a pParentNode. */
        void createCloudMachineNode(UIChooserNode *pParentNode, UIFakeCloudVirtualMachineItemState enmState);
        /** Creates real cloud machine node based on certain @a comMachine as a child of specified @a pParentNode. */
        void createCloudMachineNode(UIChooserNode *pParentNode, const CCloudMachine &comMachine);

        /** Gathers a list of possible group node names for machine nodes listed in @a exceptions, starting from @a pCurrentNode. */
        QStringList gatherPossibleGroupNodeNames(UIChooserNode *pCurrentNode, QList<UIChooserNode*> exceptions) const;

        /** Returns whether passed @a pParentNode contains child node with passed @a uId. */
        bool checkIfNodeContainChildWithId(UIChooserNode *pParentNode, const QUuid &uId) const;
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Saves group settings. */
        void saveGroupSettings();
        /** Saves group definitions. */
        void saveGroupDefinitions();

        /** Gathers group @a settings of @a pParentGroup. */
        void gatherGroupSettings(QMap<QString, QStringList> &settings, UIChooserNode *pParentGroup);
        /** Gathers group @a definitions of @a pParentGroup. */
        void gatherGroupDefinitions(QMap<QString, QStringList> &definitions, UIChooserNode *pParentGroup);

        /** Makes sure group settings saving is finished. */
        void makeSureGroupSettingsSaveIsFinished();
        /** Makes sure group definitions saving is finished. */
        void makeSureGroupDefinitionsSaveIsFinished();
    /** @} */

    /** @name Cloud stuff.
      * @{ */
        /** Searches for provider node with passed @a uProviderId. */
        UIChooserNode *searchProviderNode(const QUuid &uProviderId);
        /** Searches for provider node with passed @a strProviderShortName. */
        UIChooserNode *searchProviderNode(const QString &strProviderShortName);

        /** Searches for profile node with passed @a strProfileName under passed @a pProviderNode. */
        UIChooserNode *searchProfileNode(UIChooserNode *pProviderNode, const QString &strProfileName);
        /** Searches for profile node with passed @a strProviderShortName and @a strProfileName. */
        UIChooserNode *searchProfileNode(const QString &strProviderShortName, const QString &strProfileName);

        /** Searches for machine node with passed @a uMachineId under passed @a pProfileNode. */
        UIChooserNode *searchMachineNode(UIChooserNode *pProfileNode, const QUuid &uMachineId);
        /** Searches for machine with passed @a strProviderShortName, @a strProfileName and @a uMachineId. */
        UIChooserNode *searchMachineNode(const QString &strProviderShortName, const QString &strProfileName, const QUuid &uMachineId);

        /** Searches for fake node under passed @a pProfileNode. */
        UIChooserNode *searchFakeNode(UIChooserNode *pProfileNode);
        /** Searches for fake with passed @a strProviderShortName and @a strProfileName. */
        UIChooserNode *searchFakeNode(const QString &strProviderShortName, const QString &strProfileName);
    /** @} */

    /** @name Cloud update stuff.
      * @{ */
        /** Enumerates all cloud machine nodes. */
        QList<UIChooserNode*> enumerateCloudMachineNodes() const;

        /** Stops all cloud updates.
          * @param  fForced  Brings whether cloud updates should be killed. */
        void stopCloudUpdates(bool fForced = false);
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the parent widget reference. */
        UIChooser *m_pParent;
    /** @} */

    /** @name Children stuff.
      * @{ */
        /** Holds the invisible root node instance. */
        UIChooserNode *m_pInvisibleRootNode;
    /** @} */

    /** @name Search stuff.
      * @{ */
        /** Stores the results of the current search. */
        QList<UIChooserNode*>  m_searchResults;
    /** @} */

    /** @name Group saving stuff.
      * @{ */
        /** Holds the consolidated map of group settings/definitions. */
        QMap<QString, QStringList>  m_groups;
    /** @} */

    /** @name Cloud update stuff.
      * @{ */
        /** Holds whether real cloud nodes should be kept updated. */
        bool  m_fKeepCloudNodesUpdated;

        /** Holds the set of cloud entity keys currently being updated. */
        QSet<UICloudEntityKey>  m_cloudEntityKeysBeingUpdated;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserAbstractModel_h */
