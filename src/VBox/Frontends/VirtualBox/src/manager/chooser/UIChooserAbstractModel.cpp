/* $Id: UIChooserAbstractModel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserAbstractModel class implementation.
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

/* Qt includes: */
#include <QRegularExpression>
#include <QThread>

/* GUI includes: */
#include "UICloudMachineManager.h"
#include "UICommon.h"
#include "UIChooser.h"
#include "UIChooserAbstractModel.h"
#include "UIChooserNode.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeGlobal.h"
#include "UIChooserNodeMachine.h"
#include "UICloudNetworkingStuff.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIProgressTaskReadCloudMachineList.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualMachineItemCloud.h"

/* COM includes: */
#include "CCloudMachine.h"
#include "CCloudProfile.h"
#include "CCloudProvider.h"
#include "CMachine.h"

/* Type defs: */
typedef QSet<QString> UIStringSet;


/** QThread subclass allowing to save group settings asynchronously. */
class UIThreadGroupSettingsSave : public QThread
{
    Q_OBJECT;

signals:

    /** Notifies about machine with certain @a uMachineId to be reloaded. */
    void sigReload(const QUuid &uMachineId);

    /** Notifies about task is complete. */
    void sigComplete();

public:

    /** Returns group settings saving thread instance. */
    static UIThreadGroupSettingsSave *instance();
    /** Prepares group settings saving thread instance. */
    static void prepare();
    /** Cleanups group settings saving thread instance. */
    static void cleanup();

    /** Configures @a group settings saving thread with corresponding @a pListener.
      * @param  oldLists  Brings the old settings list to be compared.
      * @param  newLists  Brings the new settings list to be saved. */
    void configure(QObject *pParent,
                   const QMap<QString, QStringList> &oldLists,
                   const QMap<QString, QStringList> &newLists);

protected:

    /** Constructs group settings saving thread. */
    UIThreadGroupSettingsSave();
    /** Destructs group settings saving thread. */
    virtual ~UIThreadGroupSettingsSave() RT_OVERRIDE;

    /** Contains a thread task to be executed. */
    virtual void run() RT_OVERRIDE;

    /** Holds the singleton instance. */
    static UIThreadGroupSettingsSave *s_pInstance;

    /** Holds the map of group settings to be compared. */
    QMap<QString, QStringList> m_oldLists;
    /** Holds the map of group settings to be saved. */
    QMap<QString, QStringList> m_newLists;
};


/** QThread subclass allowing to save group definitions asynchronously. */
class UIThreadGroupDefinitionsSave : public QThread
{
    Q_OBJECT;

signals:

    /** Notifies about task is complete. */
    void sigComplete();

public:

    /** Returns group definitions saving thread instance. */
    static UIThreadGroupDefinitionsSave *instance();
    /** Prepares group definitions saving thread instance. */
    static void prepare();
    /** Cleanups group definitions saving thread instance. */
    static void cleanup();

    /** Configures group definitions saving thread with corresponding @a pListener.
      * @param  lists  Brings definitions lists to be saved. */
    void configure(QObject *pListener,
                   const QMap<QString, QStringList> &lists);

protected:

    /** Constructs group definitions saving thread. */
    UIThreadGroupDefinitionsSave();
    /** Destructs group definitions saving thread. */
    virtual ~UIThreadGroupDefinitionsSave() RT_OVERRIDE;

    /** Contains a thread task to be executed. */
    virtual void run() RT_OVERRIDE;

    /** Holds the singleton instance. */
    static UIThreadGroupDefinitionsSave *s_pInstance;

    /** Holds the map of group definitions to be saved. */
    QMap<QString, QStringList>  m_lists;
};


/*********************************************************************************************************************************
*   Class UIThreadGroupSettingsSave implementation.                                                                              *
*********************************************************************************************************************************/

/* static */
UIThreadGroupSettingsSave *UIThreadGroupSettingsSave::s_pInstance = 0;

/* static */
UIThreadGroupSettingsSave *UIThreadGroupSettingsSave::instance()
{
    return s_pInstance;
}

/* static */
void UIThreadGroupSettingsSave::prepare()
{
    /* Make sure instance is not prepared: */
    if (s_pInstance)
        return;

    /* Crate instance: */
    new UIThreadGroupSettingsSave;
}

/* static */
void UIThreadGroupSettingsSave::cleanup()
{
    /* Make sure instance is prepared: */
    if (!s_pInstance)
        return;

    /* Delete instance: */
    delete s_pInstance;
}

void UIThreadGroupSettingsSave::configure(QObject *pParent,
                                          const QMap<QString, QStringList> &oldLists,
                                          const QMap<QString, QStringList> &newLists)
{
    m_oldLists = oldLists;
    m_newLists = newLists;
    UIChooserAbstractModel *pChooserAbstractModel = qobject_cast<UIChooserAbstractModel*>(pParent);
    AssertPtrReturnVoid(pChooserAbstractModel);
    {
        connect(this, &UIThreadGroupSettingsSave::sigComplete,
                pChooserAbstractModel, &UIChooserAbstractModel::sltGroupSettingsSaveComplete);
    }
}

UIThreadGroupSettingsSave::UIThreadGroupSettingsSave()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIThreadGroupSettingsSave::~UIThreadGroupSettingsSave()
{
    /* Make sure thread work is complete: */
    wait();

    /* Erase instance: */
    s_pInstance = 0;
}

void UIThreadGroupSettingsSave::run()
{
    /* COM prepare: */
    COMBase::InitializeCOM(false);

    /* For every particular machine ID: */
    foreach (const QString &strId, m_newLists.keys())
    {
        /* Get new group list/set: */
        const QStringList newGroupList = m_newLists.value(strId);
        const UIStringSet newGroupSet(newGroupList.begin(), newGroupList.end());
        /* Get old group list/set: */
        const QStringList oldGroupList = m_oldLists.value(strId);
        const UIStringSet oldGroupSet(oldGroupList.begin(), oldGroupList.end());
        /* Make sure group set changed: */
        if (newGroupSet == oldGroupSet)
            continue;

        /* The next steps are subsequent.
         * Every of them is mandatory in order to continue
         * with common cleanup in case of failure.
         * We have to simulate a try-catch block. */
        CSession comSession;
        CMachine comMachine;
        do
        {
            /* 1. Open session: */
            comSession = openSession(QUuid(strId));
            if (comSession.isNull())
                break;

            /* 2. Get session machine: */
            comMachine = comSession.GetMachine();
            if (comMachine.isNull())
                break;

            /* 3. Set new groups: */
            comMachine.SetGroups(newGroupList.toVector());
            if (!comMachine.isOk())
            {
                msgCenter().cannotSetGroups(comMachine);
                break;
            }

            /* 4. Save settings: */
            comMachine.SaveSettings();
            if (!comMachine.isOk())
            {
                msgCenter().cannotSaveMachineSettings(comMachine);
                break;
            }
        } while (0);

        /* Cleanup if necessary: */
        if (comMachine.isNull() || !comMachine.isOk())
            emit sigReload(QUuid(strId));
        if (!comSession.isNull())
            comSession.UnlockMachine();
    }

    /* Notify listeners about completeness: */
    emit sigComplete();

    /* COM cleanup: */
    COMBase::CleanupCOM();
}


/*********************************************************************************************************************************
*   Class UIThreadGroupDefinitionsSave implementation.                                                                           *
*********************************************************************************************************************************/

/* static */
UIThreadGroupDefinitionsSave *UIThreadGroupDefinitionsSave::s_pInstance = 0;

/* static */
UIThreadGroupDefinitionsSave *UIThreadGroupDefinitionsSave::instance()
{
    return s_pInstance;
}

/* static */
void UIThreadGroupDefinitionsSave::prepare()
{
    /* Make sure instance is not prepared: */
    if (s_pInstance)
        return;

    /* Crate instance: */
    new UIThreadGroupDefinitionsSave;
}

/* static */
void UIThreadGroupDefinitionsSave::cleanup()
{
    /* Make sure instance is prepared: */
    if (!s_pInstance)
        return;

    /* Delete instance: */
    delete s_pInstance;
}

void UIThreadGroupDefinitionsSave::configure(QObject *pParent,
                                             const QMap<QString, QStringList> &groups)
{
    m_lists = groups;
    UIChooserAbstractModel *pChooserAbstractModel = qobject_cast<UIChooserAbstractModel*>(pParent);
    AssertPtrReturnVoid(pChooserAbstractModel);
    {
        connect(this, &UIThreadGroupDefinitionsSave::sigComplete,
                pChooserAbstractModel, &UIChooserAbstractModel::sltGroupDefinitionsSaveComplete);
    }
}

UIThreadGroupDefinitionsSave::UIThreadGroupDefinitionsSave()
{
    /* Assign instance: */
    s_pInstance = this;
}

UIThreadGroupDefinitionsSave::~UIThreadGroupDefinitionsSave()
{
    /* Make sure thread work is complete: */
    wait();

    /* Erase instance: */
    s_pInstance = 0;
}

void UIThreadGroupDefinitionsSave::run()
{
    /* COM prepare: */
    COMBase::InitializeCOM(false);

    /* Acquire a list of known group definition keys: */
    QStringList knownKeys = gEDataManager->knownMachineGroupDefinitionKeys();
    /* For every group definition to be saved: */
    foreach (const QString &strId, m_lists.keys())
    {
        /* Save definition only if there is a change: */
        if (gEDataManager->machineGroupDefinitions(strId) != m_lists.value(strId))
            gEDataManager->setMachineGroupDefinitions(strId, m_lists.value(strId));
        /* Remove it from known keys: */
        knownKeys.removeAll(strId);
    }
    /* Wipe out rest of known group definitions: */
    foreach (const QString strId, knownKeys)
        gEDataManager->setMachineGroupDefinitions(strId, QStringList());

    /* Notify listeners about completeness: */
    emit sigComplete();

    /* COM cleanup: */
    COMBase::CleanupCOM();
}


/*********************************************************************************************************************************
*   Class UIChooserAbstractModel implementation.                                                                                 *
*********************************************************************************************************************************/

UIChooserAbstractModel::UIChooserAbstractModel(UIChooser *pParent)
    : QObject(pParent)
    , m_pParent(pParent)
    , m_pInvisibleRootNode(0)
    , m_fKeepCloudNodesUpdated(false)
{
    prepare();
}

UIChooserAbstractModel::~UIChooserAbstractModel()
{
    cleanup();
}

void UIChooserAbstractModel::init()
{
    /* Create invisible root group node: */
    m_pInvisibleRootNode = new UIChooserNodeGroup(0 /* parent */,
                                                  0 /* position */,
                                                  QUuid() /* id */,
                                                  QString() /* name */,
                                                  UIChooserNodeGroupType_Local,
                                                  true /* opened */);
    if (invisibleRoot())
    {
        /* Link root to this model: */
        invisibleRoot()->setModel(this);

        /* Create global node: */
        new UIChooserNodeGlobal(invisibleRoot() /* parent */,
                                0 /* position */,
                                shouldGlobalNodeBeFavorite(invisibleRoot()),
                                QString() /* tip */);

        /* Reload local tree: */
        reloadLocalTree();
        /* Reload cloud tree: */
        reloadCloudTree();
    }
}

void UIChooserAbstractModel::deinit()
{
    /* Make sure all saving steps complete: */
    makeSureGroupSettingsSaveIsFinished();
    makeSureGroupDefinitionsSaveIsFinished();
}

void UIChooserAbstractModel::wipeOutEmptyGroups()
{
    wipeOutEmptyGroupsStartingFrom(invisibleRoot());
}

QStringList UIChooserAbstractModel::possibleGroupNodeNamesForMachineNodeToMove(const QUuid &uId)
{
    /* Search for all the machine nodes with passed ID: */
    QList<UIChooserNode*> machineNodes;
    invisibleRoot()->searchForNodes(uId.toString(),
                                    UIChooserItemSearchFlag_Machine | UIChooserItemSearchFlag_ExactId,
                                    machineNodes);

    /* Return group nodes starting from root one: */
    return gatherPossibleGroupNodeNames(invisibleRoot(), machineNodes);
}

QStringList UIChooserAbstractModel::possibleGroupNodeNamesForGroupNodeToMove(const QString &strFullName)
{
    /* Search for all the group nodes with passed full-name: */
    QList<UIChooserNode*> groupNodes;
    invisibleRoot()->searchForNodes(strFullName,
                                    UIChooserItemSearchFlag_LocalGroup | UIChooserItemSearchFlag_FullName,
                                    groupNodes);

    /* Return group nodes starting from root one: */
    return gatherPossibleGroupNodeNames(invisibleRoot(), groupNodes);
}

/* static */
QString UIChooserAbstractModel::uniqueGroupName(UIChooserNode *pRoot)
{
    /* Enumerate all the group names: */
    QStringList groupNames;
    foreach (UIChooserNode *pNode, pRoot->nodes(UIChooserNodeType_Group))
        groupNames << pNode->name();

    /* Prepare reg-exp: */
    const QString strMinimumName = tr("New group");
    const QString strShortTemplate = strMinimumName;
    const QString strFullTemplate = strShortTemplate + QString(" (\\d+)");
    const QRegularExpression shortRegExp(strShortTemplate);
    const QRegularExpression fullRegExp(strFullTemplate);

    /* Search for the maximum index: */
    int iMinimumPossibleNumber = 0;
    foreach (const QString &strName, groupNames)
    {
        const QRegularExpressionMatch mtShort = shortRegExp.match(strName);
        const QRegularExpressionMatch mtFull = fullRegExp.match(strName);
        if (mtShort.hasMatch())
            iMinimumPossibleNumber = qMax(iMinimumPossibleNumber, 2);
        else if (mtFull.hasMatch())
            iMinimumPossibleNumber = qMax(iMinimumPossibleNumber, mtFull.captured(1).toInt() + 1);
    }

    /* Prepare/return result: */
    QString strResult = strMinimumName;
    if (iMinimumPossibleNumber)
        strResult += " " + QString::number(iMinimumPossibleNumber);
    return strResult;
}

void UIChooserAbstractModel::performSearch(const QString &strSearchTerm, int iSearchFlags)
{
    /* Make sure invisible root exists: */
    AssertPtrReturnVoid(invisibleRoot());

    /* Currently we perform the search only for machines, when this to be changed make
     * sure the disabled flags of the other item types are also managed correctly. */

    /* Reset the search first to erase the disabled flag,
     * this also returns a full list of all machine nodes: */
    const QList<UIChooserNode*> nodes = resetSearch();

    /* Stop here if no search conditions specified: */
    if (strSearchTerm.isEmpty())
        return;

    /* Search for all the nodes matching required condition: */
    invisibleRoot()->searchForNodes(strSearchTerm, iSearchFlags, m_searchResults);

    /* Assign/reset the disabled flag for required nodes: */
    foreach (UIChooserNode *pNode, nodes)
    {
        AssertPtrReturnVoid(pNode);
        pNode->setDisabled(!m_searchResults.contains(pNode));
    }
}

QList<UIChooserNode*> UIChooserAbstractModel::resetSearch()
{
    /* Prepare resulting nodes: */
    QList<UIChooserNode*> nodes;

    /* Make sure invisible root exists: */
    AssertPtrReturn(invisibleRoot(), nodes);

    /* Calling UIChooserNode::searchForNodes with an empty search term
     * returns a list all nodes (of the whole tree) of the required type: */
    invisibleRoot()->searchForNodes(QString(), UIChooserItemSearchFlag_Machine, nodes);

    /* Reset the disabled flag of the nodes first: */
    foreach (UIChooserNode *pNode, nodes)
    {
        AssertPtrReturn(pNode, nodes);
        pNode->setDisabled(false);
    }

    /* Reset the search result related data: */
    m_searchResults.clear();

    /* Return nodes: */
    return nodes;
}

QList<UIChooserNode*> UIChooserAbstractModel::searchResult() const
{
    return m_searchResults;
}

void UIChooserAbstractModel::saveGroups()
{
    emit sigSaveSettings();
}

bool UIChooserAbstractModel::isGroupSavingInProgress() const
{
    return    UIThreadGroupSettingsSave::instance()
           || UIThreadGroupDefinitionsSave::instance();
}

/* static */
QString UIChooserAbstractModel::toOldStyleUuid(const QUuid &uId)
{
    return uId.toString().remove(QRegularExpression("[{}]"));
}

/* static */
QString UIChooserAbstractModel::prefixToString(UIChooserNodeDataPrefixType enmType)
{
    switch (enmType)
    {
        /* Global nodes: */
        case UIChooserNodeDataPrefixType_Global:   return "n";
        /* Machine nodes: */
        case UIChooserNodeDataPrefixType_Machine:  return "m";
        /* Group nodes: */
        case UIChooserNodeDataPrefixType_Local:    return "g";
        case UIChooserNodeDataPrefixType_Provider: return "p";
        case UIChooserNodeDataPrefixType_Profile:  return "a";
    }
    return QString();
}

/* static */
QString UIChooserAbstractModel::optionToString(UIChooserNodeDataOptionType enmType)
{
    switch (enmType)
    {
        /* Global nodes: */
        case UIChooserNodeDataOptionType_GlobalFavorite: return "f";
        /* Group nodes: */
        case UIChooserNodeDataOptionType_GroupOpened:    return "o";
    }
    return QString();
}

/* static */
QString UIChooserAbstractModel::valueToString(UIChooserNodeDataValueType enmType)
{
    switch (enmType)
    {
        /* Global nodes: */
        case UIChooserNodeDataValueType_GlobalDefault: return "GLOBAL";
    }
    return QString();
}

void UIChooserAbstractModel::setKeepCloudNodesUpdated(bool fUpdate)
{
    /* Make sure something changed: */
    if (m_fKeepCloudNodesUpdated == fUpdate)
        return;

    /* Holds the value: */
    m_fKeepCloudNodesUpdated = fUpdate;

    /* Update all the real cloud machine items: */
    foreach (UIChooserNode *pNode, enumerateCloudMachineNodes())
    {
        AssertReturnVoid(pNode && pNode->type() == UIChooserNodeType_Machine);
        UIChooserNodeMachine *pMachineNode = pNode->toMachineNode();
        AssertPtrReturnVoid(pMachineNode);
        if (pMachineNode->cacheType() != UIVirtualMachineItemType_CloudReal)
            continue;
        UIVirtualMachineItemCloud *pCloudMachineItem = pMachineNode->cache()->toCloud();
        AssertPtrReturnVoid(pCloudMachineItem);
        pCloudMachineItem->setUpdateRequiredByGlobalReason(m_fKeepCloudNodesUpdated);
        if (m_fKeepCloudNodesUpdated)
            pCloudMachineItem->updateInfoAsync(false /* delayed? */);
    }
}

void UIChooserAbstractModel::insertCloudEntityKey(const UICloudEntityKey &key)
{
//    printf("Cloud entity with key %s being updated..\n", key.toString().toUtf8().constData());
    m_cloudEntityKeysBeingUpdated.insert(key);
    emit sigCloudUpdateStateChanged();
}

void UIChooserAbstractModel::removeCloudEntityKey(const UICloudEntityKey &key)
{
//    printf("Cloud entity with key %s is updated!\n", key.toString().toUtf8().constData());
    m_cloudEntityKeysBeingUpdated.remove(key);
    emit sigCloudUpdateStateChanged();
}

bool UIChooserAbstractModel::containsCloudEntityKey(const UICloudEntityKey &key) const
{
    return m_cloudEntityKeysBeingUpdated.contains(key);
}

bool UIChooserAbstractModel::isCloudProfileUpdateInProgress() const
{
    /* Compose RE for profile: */
    const QRegularExpression re("^/[^/]+/[^/]+$");
    /* Check whether keys match profile RE: */
    foreach (const UICloudEntityKey &key, m_cloudEntityKeysBeingUpdated)
    {
        const QRegularExpressionMatch mt = re.match(key.toString());
        if (mt.hasMatch())
            return true;
    }
    /* False by default: */
    return false;
}

QList<UIVirtualMachineItemCloud*> UIChooserAbstractModel::cloudMachineItems() const
{
    QList<UIVirtualMachineItemCloud*> items;
    foreach (UIChooserNode *pNode, enumerateCloudMachineNodes())
    {
        AssertReturn(pNode && pNode->type() == UIChooserNodeType_Machine, items);
        UIChooserNodeMachine *pMachineNode = pNode->toMachineNode();
        AssertPtrReturn(pMachineNode, items);
        if (pMachineNode->cacheType() != UIVirtualMachineItemType_CloudReal)
            continue;
        UIVirtualMachineItemCloud *pCloudMachineItem = pMachineNode->cache()->toCloud();
        AssertPtrReturn(pCloudMachineItem, items);
        items << pCloudMachineItem;
    }
    return items;
}

void UIChooserAbstractModel::sltHandleCloudMachineRefreshStarted()
{
    /* Acquire sender: */
    UIVirtualMachineItem *pCache = qobject_cast<UIVirtualMachineItem*>(sender());
    AssertPtrReturnVoid(pCache);

    /* Acquire sender's ID: */
    const QUuid uId = pCache->id();

    /* Search for a first machine node with passed ID: */
    UIChooserNode *pMachineNode = searchMachineNode(invisibleRoot(), uId);

    /* Insert cloud machine key into a list of keys currently being updated: */
    const UICloudEntityKey guiCloudMachineKey = UICloudEntityKey(pMachineNode->parentNode()->parentNode()->name(),
                                                                 pMachineNode->parentNode()->name(),
                                                                 pMachineNode->toMachineNode()->id());
    insertCloudEntityKey(guiCloudMachineKey);
}

void UIChooserAbstractModel::sltHandleCloudMachineRefreshFinished()
{
    /* Acquire sender: */
    UIVirtualMachineItem *pCache = qobject_cast<UIVirtualMachineItem*>(sender());
    AssertPtrReturnVoid(pCache);

    /* Acquire sender's ID: */
    const QUuid uId = pCache->id();

    /* Search for a first machine node with passed ID: */
    UIChooserNode *pMachineNode = searchMachineNode(invisibleRoot(), uId);

    /* Remove cloud machine key from the list of keys currently being updated: */
    const UICloudEntityKey guiCloudMachineKey = UICloudEntityKey(pMachineNode->parentNode()->parentNode()->name(),
                                                                 pMachineNode->parentNode()->name(),
                                                                 pMachineNode->toMachineNode()->id());
    removeCloudEntityKey(guiCloudMachineKey);

    /* Notify listeners: */
    emit sigCloudMachineStateChange(uId);
}

void UIChooserAbstractModel::sltGroupSettingsSaveComplete()
{
    makeSureGroupSettingsSaveIsFinished();
    emit sigGroupSavingStateChanged();
}

void UIChooserAbstractModel::sltGroupDefinitionsSaveComplete()
{
    makeSureGroupDefinitionsSaveIsFinished();
    emit sigGroupSavingStateChanged();
}

void UIChooserAbstractModel::sltLocalMachineStateChanged(const QUuid &uMachineId, const KMachineState)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltLocalMachineDataChanged(const QUuid &uMachineId)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltLocalMachineRegistrationChanged(const QUuid &uMachineId, const bool fRegistered)
{
    /* Existing VM unregistered? */
    if (!fRegistered)
    {
        /* Remove machine-items with passed id: */
        invisibleRoot()->removeAllNodes(uMachineId);
        /* Wipe out empty groups: */
        wipeOutEmptyGroups();
    }
    /* New VM registered? */
    else
    {
        /* Should we show this VM? */
        if (gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
        {
            /* Add new machine-item: */
            const CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(uMachineId.toString());
            if (comMachine.isNotNull())
                addLocalMachineIntoTheTree(comMachine, true /* make it visible */);
        }
    }
}

void UIChooserAbstractModel::sltLocalMachineGroupsChanged(const QUuid &uMachineId)
{
    /* Skip VM if restricted: */
    if (!gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
        return;

    /* Search for cached group list: */
    const QStringList oldGroupList = m_groups.value(toOldStyleUuid(uMachineId));
    //printf("Old groups for VM with ID=%s: %s\n",
    //       uMachineId.toString().toUtf8().constData(),
    //       oldGroupList.join(", ").toUtf8().constData());

    /* Search for existing registered machine: */
    const CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(uMachineId.toString());
    if (comMachine.isNull())
        return;
    /* Look for a new group list: */
    const QStringList newGroupList = comMachine.GetGroups().toList();
    //printf("New groups for VM with ID=%s: %s\n",
    //       uMachineId.toString().toUtf8().constData(),
    //       newGroupList.join(", ").toUtf8().constData());

    /* Re-register VM if required: */
    QSet<QString> newGroupSet(newGroupList.begin(), newGroupList.end());
    QSet<QString> oldGroupSet(oldGroupList.begin(), oldGroupList.end());
    if (newGroupSet != oldGroupSet)
    {
        sltLocalMachineRegistrationChanged(uMachineId, false);
        sltLocalMachineRegistrationChanged(uMachineId, true);
    }
}

void UIChooserAbstractModel::sltSessionStateChanged(const QUuid &uMachineId, const KSessionState)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltSnapshotChanged(const QUuid &uMachineId, const QUuid &)
{
    /* Update machine-nodes with passed id: */
    invisibleRoot()->updateAllNodes(uMachineId);
}

void UIChooserAbstractModel::sltHandleCloudProviderUninstall(const QUuid &uProviderId)
{
    /* First of all, stop all cloud updates: */
    stopCloudUpdates();

    /* Search and delete corresponding cloud provider node if present: */
    delete searchProviderNode(uProviderId);
}

void UIChooserAbstractModel::sltReloadMachine(const QUuid &uMachineId)
{
    /* Remove machine-items with passed id: */
    invisibleRoot()->removeAllNodes(uMachineId);
    /* Wipe out empty groups: */
    wipeOutEmptyGroups();

    /* Should we show this VM? */
    if (gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
    {
        /* Add new machine-item: */
        const CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(uMachineId.toString());
        addLocalMachineIntoTheTree(comMachine, true /* make it visible */);
    }
}

void UIChooserAbstractModel::sltCommitData()
{
    /* Finally, stop all cloud updates: */
    stopCloudUpdates(true /* forced? */);
}

void UIChooserAbstractModel::sltDetachCOM()
{
    /* Delete tree: */
    delete m_pInvisibleRootNode;
    m_pInvisibleRootNode = 0;
}

void UIChooserAbstractModel::sltCloudMachineUnregistered(const QString &strProviderShortName,
                                                         const QString &strProfileName,
                                                         const QUuid &uId)
{
    /* Search for profile node: */
    UIChooserNode *pProfileNode = searchProfileNode(strProviderShortName, strProfileName);
    if (!pProfileNode)
        return;

    /* Remove machine-item with passed uId: */
    pProfileNode->removeAllNodes(uId);

    /* If there are no items left => add fake cloud VM node: */
    if (pProfileNode->nodes(UIChooserNodeType_Machine).isEmpty())
        createCloudMachineNode(pProfileNode, UIFakeCloudVirtualMachineItemState_Done);
}

void UIChooserAbstractModel::sltCloudMachinesUnregistered(const QString &strProviderShortName,
                                                          const QString &strProfileName,
                                                          const QList<QUuid> &ids)
{
    /* Search for profile node: */
    UIChooserNode *pProfileNode = searchProfileNode(strProviderShortName, strProfileName);
    if (!pProfileNode)
        return;

    /* Remove machine-items with passed id: */
    foreach (const QUuid &uId, ids)
        pProfileNode->removeAllNodes(uId);

    /* If there are no items left => add fake cloud VM node: */
    if (pProfileNode->nodes(UIChooserNodeType_Machine).isEmpty())
        createCloudMachineNode(pProfileNode, UIFakeCloudVirtualMachineItemState_Done);
}

void UIChooserAbstractModel::sltCloudMachineRegistered(const QString &strProviderShortName,
                                                       const QString &strProfileName,
                                                       const CCloudMachine &comMachine)
{
    /* Search for profile node: */
    UIChooserNode *pProfileNode = searchProfileNode(strProviderShortName, strProfileName);
    if (!pProfileNode)
        return;

    /* Compose corresponding group path: */
    const QString strGroup = QString("/%1/%2").arg(strProviderShortName, strProfileName);
    /* Make sure there is no VM with such ID already: */
    QUuid uId;
    if (!cloudMachineId(comMachine, uId))
        return;
    if (checkIfNodeContainChildWithId(pProfileNode, uId))
        return;
    /* Add new machine-item: */
    addCloudMachineIntoTheTree(strGroup, comMachine, true /* make it visible? */);

    /* Delete fake node if present: */
    delete searchFakeNode(pProfileNode);
}

void UIChooserAbstractModel::sltCloudMachinesRegistered(const QString &strProviderShortName,
                                                        const QString &strProfileName,
                                                        const QVector<CCloudMachine> &machines)
{
    /* Search for profile node: */
    UIChooserNode *pProfileNode = searchProfileNode(strProviderShortName, strProfileName);
    if (!pProfileNode)
        return;

    /* Compose corresponding group path: */
    const QString strGroup = QString("/%1/%2").arg(strProviderShortName, strProfileName);
    foreach (const CCloudMachine &comMachine, machines)
    {
        /* Make sure there is no VM with such ID already: */
        QUuid uId;
        if (!cloudMachineId(comMachine, uId))
            continue;
        if (checkIfNodeContainChildWithId(pProfileNode, uId))
            continue;
        /* Add new machine-item: */
        addCloudMachineIntoTheTree(strGroup, comMachine, false /* make it visible? */);
    }

    /* Delete fake node if present: */
    delete searchFakeNode(pProfileNode);
}

void UIChooserAbstractModel::sltHandleReadCloudMachineListTaskComplete()
{
    /* Parse task result: */
    UIProgressTaskReadCloudMachineList *pSender = qobject_cast<UIProgressTaskReadCloudMachineList*>(sender());
    AssertPtrReturnVoid(pSender);
    const UICloudEntityKey guiCloudProfileKey = pSender->cloudProfileKey();
    const QVector<CCloudMachine> machines = pSender->machines();
    const QString strErrorMessage = pSender->errorMessage();

    /* Delete task: */
    delete pSender;

    /* Check whether this task was expected: */
    if (!containsCloudEntityKey(guiCloudProfileKey))
        return;

    /* Search for provider node separately, it can be removed already: */
    UIChooserNode *pProviderNode = searchProviderNode(guiCloudProfileKey.m_strProviderShortName);
    if (pProviderNode)
    {
        /* Search for profile node separately, it can be hidden at all: */
        UIChooserNode *pProfileNode = searchProfileNode(pProviderNode, guiCloudProfileKey.m_strProfileName);
        if (pProfileNode)
        {
            /* Compose old set of machine IDs: */
            QSet<QUuid> oldIDs;
            foreach (UIChooserNode *pNode, pProfileNode->nodes(UIChooserNodeType_Machine))
            {
                AssertPtrReturnVoid(pNode);
                UIChooserNodeMachine *pNodeMachine = pNode->toMachineNode();
                AssertPtrReturnVoid(pNodeMachine);
                if (pNodeMachine->cacheType() != UIVirtualMachineItemType_CloudReal)
                    continue;
                oldIDs << pNodeMachine->id();
            }
            /* Compose new set of machine IDs and map of machines: */
            QSet<QUuid> newIDs;
            QMap<QUuid, CCloudMachine> newMachines;
            foreach (const CCloudMachine &comMachine, machines)
            {
                QUuid uId;
                AssertReturnVoid(cloudMachineId(comMachine, uId));
                newMachines[uId] = comMachine;
                newIDs << uId;
            }

            /* Calculate set of unregistered/registered IDs: */
            const QSet<QUuid> unregisteredIDs = oldIDs - newIDs;
            const QSet<QUuid> registeredIDs = newIDs - oldIDs;
            QVector<CCloudMachine> registeredMachines;
            foreach (const QUuid &uId, registeredIDs)
                registeredMachines << newMachines.value(uId);

            /* Remove unregistered cloud VM nodes: */
            if (!unregisteredIDs.isEmpty())
            {
                const QList<QUuid> listUnregisteredIDs(unregisteredIDs.begin(), unregisteredIDs.end());
                sltCloudMachinesUnregistered(guiCloudProfileKey.m_strProviderShortName,
                                             guiCloudProfileKey.m_strProfileName,
                                             listUnregisteredIDs);
            }
            /* Add registered cloud VM nodes: */
            if (!registeredMachines.isEmpty())
                sltCloudMachinesRegistered(guiCloudProfileKey.m_strProviderShortName,
                                           guiCloudProfileKey.m_strProfileName,
                                           registeredMachines);
            /* If we changed nothing and have nothing currently: */
            if (unregisteredIDs.isEmpty() && newIDs.isEmpty())
            {
                /* We should update at least fake cloud machine node: */
                UIChooserNode *pFakeNode = searchFakeNode(pProfileNode);
                AssertPtrReturnVoid(pFakeNode);
                UIVirtualMachineItemCloud *pFakeMachineItem = pFakeNode->toMachineNode()->cache()->toCloud();
                AssertPtrReturnVoid(pFakeMachineItem);
                pFakeMachineItem->setFakeCloudItemState(UIFakeCloudVirtualMachineItemState_Done);
                pFakeMachineItem->setFakeCloudItemErrorMessage(strErrorMessage);
                if (pFakeNode->item())
                    pFakeNode->item()->updateItem();
            }
        }
    }

    /* Remove cloud entity key from the list of keys currently being updated: */
    removeCloudEntityKey(guiCloudProfileKey);

    /* Notify listeners: */
    emit sigCloudProfileStateChange(guiCloudProfileKey.m_strProviderShortName,
                                    guiCloudProfileKey.m_strProfileName);
}

void UIChooserAbstractModel::sltHandleCloudProfileManagerCumulativeChange()
{
    /* Reload cloud tree: */
    reloadCloudTree();
}

void UIChooserAbstractModel::createReadCloudMachineListTask(const UICloudEntityKey &guiCloudProfileKey, bool fWithRefresh)
{
    /* Do not create task if already registered: */
    if (containsCloudEntityKey(guiCloudProfileKey))
        return;

    /* Create task: */
    UIProgressTaskReadCloudMachineList *pTask = new UIProgressTaskReadCloudMachineList(this,
                                                                                       guiCloudProfileKey,
                                                                                       fWithRefresh);
    if (pTask)
    {
        /* It's easy to find child by name later: */
        pTask->setObjectName(guiCloudProfileKey.toString());

        /* Insert cloud profile key into a list of keys currently being updated: */
        insertCloudEntityKey(guiCloudProfileKey);

        /* Connect and start it finally: */
        connect(pTask, &UIProgressTaskReadCloudMachineList::sigProgressFinished,
                this, &UIChooserAbstractModel::sltHandleReadCloudMachineListTaskComplete);
        pTask->start();
    }
}

void UIChooserAbstractModel::sltSaveSettings()
{
    saveGroupSettings();
    saveGroupDefinitions();
}

void UIChooserAbstractModel::prepare()
{
    prepareConnections();
}

void UIChooserAbstractModel::prepareConnections()
{
    /* UICommon connections: */
    connect(&uiCommon(), &UICommon::sigAskToCommitData,
            this, &UIChooserAbstractModel::sltCommitData);
    connect(&uiCommon(), &UICommon::sigAskToDetachCOM,
            this, &UIChooserAbstractModel::sltDetachCOM);

    /* UICloudMachineManager connections: */
    connect(gpCloudMachineManager, &UICloudMachineManager::sigCloudMachineUnregistered,
            this, &UIChooserAbstractModel::sltCloudMachineUnregistered);
    connect(gpCloudMachineManager, &UICloudMachineManager::sigCloudMachineRegistered,
            this, &UIChooserAbstractModel::sltCloudMachineRegistered);

    /* Global connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIChooserAbstractModel::sltLocalMachineStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineDataChange,
            this, &UIChooserAbstractModel::sltLocalMachineDataChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineRegistered,
            this, &UIChooserAbstractModel::sltLocalMachineRegistrationChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineGroupsChange,
            this, &UIChooserAbstractModel::sltLocalMachineGroupsChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange,
            this, &UIChooserAbstractModel::sltSessionStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotTake,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotDelete,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotChange,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotRestore,
            this, &UIChooserAbstractModel::sltSnapshotChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProviderListChanged,
            this, &UIChooserAbstractModel::sltHandleCloudProfileManagerCumulativeChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileRegistered,
            this, &UIChooserAbstractModel::sltHandleCloudProfileManagerCumulativeChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProfileChanged,
            this, &UIChooserAbstractModel::sltHandleCloudProfileManagerCumulativeChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigCloudProviderUninstall,
            this, &UIChooserAbstractModel::sltHandleCloudProviderUninstall);

    /* Settings saving connections: */
    connect(this, &UIChooserAbstractModel::sigSaveSettings,
            this, &UIChooserAbstractModel::sltSaveSettings,
            Qt::QueuedConnection);

    /* Extra-data connections: */
    connect(gEDataManager, &UIExtraDataManager::sigCloudProfileManagerRestrictionChange,
            this, &UIChooserAbstractModel::sltHandleCloudProfileManagerCumulativeChange);
}

void UIChooserAbstractModel::cleanupConnections()
{
    /* Group saving connections: */
    disconnect(this, &UIChooserAbstractModel::sigSaveSettings,
               this, &UIChooserAbstractModel::sltSaveSettings);
}

void UIChooserAbstractModel::cleanup()
{
    cleanupConnections();
}

void UIChooserAbstractModel::reloadLocalTree()
{
    LogRelFlow(("UIChooserAbstractModel: Loading local VMs...\n"));

    /* Acquire VBox: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Acquire existing local machines: */
    const QVector<CMachine> machines = comVBox.GetMachines();
    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotAcquireVirtualBoxParameter(comVBox);
    else
    {
        /* Iterate through existing machines: */
        foreach (const CMachine &comMachine, machines)
        {
            /* Skip if we have nothing to populate (wtf happened?): */
            if (comMachine.isNull())
                continue;

            /* Get machine ID: */
            const QUuid uMachineID = comMachine.GetId();
            /* Show error message if necessary: */
            if (!comMachine.isOk())
            {
                UINotificationMessage::cannotAcquireMachineParameter(comMachine);
                continue;
            }

            /* Skip if we have nothing to show (wtf happened?): */
            if (uMachineID.isNull())
                continue;

            /* Skip if machine is restricted from being shown: */
            if (!gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineID))
                continue;

            /* Add machine into tree: */
            addLocalMachineIntoTheTree(comMachine);
        }
    }

    LogRelFlow(("UIChooserAbstractModel: Local VMs loaded.\n"));
}

void UIChooserAbstractModel::reloadCloudTree()
{
    LogRelFlow(("UIChooserAbstractModel: Loading cloud providers/profiles...\n"));

    /* Wipe out existing cloud providers first.
     * This is quite rude, in future we need to reimplement it more wise.. */
    foreach (UIChooserNode *pNode, invisibleRoot()->nodes(UIChooserNodeType_Group))
    {
        AssertPtrReturnVoid(pNode);
        UIChooserNodeGroup *pGroupNode = pNode->toGroupNode();
        AssertPtrReturnVoid(pGroupNode);
        if (pGroupNode->groupType() == UIChooserNodeGroupType_Provider)
            delete pNode;
    }

    /* Acquire Cloud Profile Manager restrictions: */
    const QStringList restrictions = gEDataManager->cloudProfileManagerRestrictions();

    /* Iterate through existing providers: */
    foreach (CCloudProvider comCloudProvider, listCloudProviders())
    {
        /* Skip if we have nothing to populate: */
        if (comCloudProvider.isNull())
            continue;

        /* Acquire provider id: */
        QUuid uProviderId;
        if (!cloudProviderId(comCloudProvider, uProviderId))
            continue;

        /* Acquire provider short name: */
        QString strProviderShortName;
        if (!cloudProviderShortName(comCloudProvider, strProviderShortName))
            continue;

        /* Make sure this provider isn't restricted: */
        const QString strProviderPath = QString("/%1").arg(strProviderShortName);
        if (restrictions.contains(strProviderPath))
            continue;

        /* Acquire list of profiles: */
        const QVector<CCloudProfile> profiles = listCloudProfiles(comCloudProvider);
        if (profiles.isEmpty())
            continue;

        /* Add provider group node: */
        UIChooserNodeGroup *pProviderNode =
            new UIChooserNodeGroup(invisibleRoot() /* parent */,
                                   getDesiredNodePosition(invisibleRoot(),
                                                          UIChooserNodeDataPrefixType_Provider,
                                                          strProviderShortName),
                                   uProviderId,
                                   strProviderShortName,
                                   UIChooserNodeGroupType_Provider,
                                   shouldGroupNodeBeOpened(invisibleRoot(),
                                                           UIChooserNodeDataPrefixType_Provider,
                                                           strProviderShortName));

        /* Iterate through provider's profiles: */
        foreach (CCloudProfile comCloudProfile, profiles)
        {
            /* Skip if we have nothing to populate: */
            if (comCloudProfile.isNull())
                continue;

            /* Acquire profile name: */
            QString strProfileName;
            if (!cloudProfileName(comCloudProfile, strProfileName))
                continue;

            /* Make sure this profile isn't restricted: */
            const QString strProfilePath = QString("/%1/%2").arg(strProviderShortName, strProfileName);
            if (restrictions.contains(strProfilePath))
                continue;

            /* Add profile sub-group node: */
            UIChooserNodeGroup *pProfileNode =
                new UIChooserNodeGroup(pProviderNode /* parent */,
                                       getDesiredNodePosition(pProviderNode,
                                                              UIChooserNodeDataPrefixType_Profile,
                                                              strProfileName),
                                       QUuid() /* id */,
                                       strProfileName,
                                       UIChooserNodeGroupType_Profile,
                                       shouldGroupNodeBeOpened(pProviderNode,
                                                               UIChooserNodeDataPrefixType_Profile,
                                                               strProfileName));

            /* Add fake cloud VM item: */
            createCloudMachineNode(pProfileNode, UIFakeCloudVirtualMachineItemState_Loading);

            /* Create read cloud machine list task: */
            const UICloudEntityKey guiCloudProfileKey = UICloudEntityKey(strProviderShortName, strProfileName);
            createReadCloudMachineListTask(guiCloudProfileKey, true /* with refresh? */);
        }
    }

    LogRelFlow(("UIChooserAbstractModel: Cloud providers/profiles loaded.\n"));
}

void UIChooserAbstractModel::addLocalMachineIntoTheTree(const CMachine &comMachine,
                                                        bool fMakeItVisible /* = false */)
{
    /* Make sure passed VM is not NULL: */
    if (comMachine.isNull())
        LogRelFlow(("UIChooserModel: ERROR: Passed local VM is NULL!\n"));
    AssertReturnVoid(!comMachine.isNull());

    /* Which VM we are loading: */
    const QUuid uId = comMachine.GetId();
    LogRelFlow(("UIChooserModel: Loading local VM with ID={%s}...\n",
                toOldStyleUuid(uId).toUtf8().constData()));

    /* Is that machine accessible? */
    if (comMachine.GetAccessible())
    {
        /* Acquire VM name: */
        const QString strName = comMachine.GetName();
        LogRelFlow(("UIChooserModel:  Local VM {%s} is accessible.\n", strName.toUtf8().constData()));
        /* Which groups passed machine attached to? */
        const QVector<QString> groups = comMachine.GetGroups();
        const QStringList groupList = groups.toList();
        const QString strGroups = groupList.join(", ");
        LogRelFlow(("UIChooserModel:  Local VM {%s} has groups: {%s}.\n",
                    strName.toUtf8().constData(), strGroups.toUtf8().constData()));
        foreach (QString strGroup, groups)
        {
            /* Remove last '/' if any: */
            if (strGroup.right(1) == "/")
                strGroup.truncate(strGroup.size() - 1);
            /* Create machine-item with found group-item as parent: */
            LogRelFlow(("UIChooserModel:   Creating node for local VM {%s} in group {%s}.\n",
                        strName.toUtf8().constData(), strGroup.toUtf8().constData()));
            createLocalMachineNode(getLocalGroupNode(strGroup, invisibleRoot(), fMakeItVisible), comMachine);
        }
        /* Update group settings: */
        m_groups[toOldStyleUuid(uId)] = groupList;
    }
    /* Inaccessible machine: */
    else
    {
        /* VM is accessible: */
        LogRelFlow(("UIChooserModel:  Local VM {%s} is inaccessible.\n",
                    toOldStyleUuid(uId).toUtf8().constData()));
        /* Create machine-item with main-root group-item as parent: */
        createLocalMachineNode(invisibleRoot(), comMachine);
    }
}

void UIChooserAbstractModel::addCloudMachineIntoTheTree(const QString &strGroup,
                                                        const CCloudMachine &comMachine,
                                                        bool fMakeItVisible /* = false */)
{
    /* Make sure passed VM is not NULL: */
    if (comMachine.isNull())
        LogRelFlow(("UIChooserModel: ERROR: Passed cloud VM is NULL!\n"));
    AssertReturnVoid(!comMachine.isNull());

    /* Which VM we are loading: */
    const QUuid uId = comMachine.GetId();
    LogRelFlow(("UIChooserModel: Loading cloud VM with ID={%s}...\n",
                toOldStyleUuid(uId).toUtf8().constData()));

    /* Acquire VM name: */
    QString strName = comMachine.GetName();
    if (strName.isEmpty())
        strName = uId.toString();
    LogRelFlow(("UIChooserModel:  Creating node for cloud VM {%s} in group {%s}.\n",
                strName.toUtf8().constData(), strGroup.toUtf8().constData()));
    /* Create machine-item with found group-item as parent: */
    createCloudMachineNode(getCloudGroupNode(strGroup, invisibleRoot(), fMakeItVisible), comMachine);
    /* Update group settings: */
    const QStringList groupList(strGroup);
    m_groups[toOldStyleUuid(uId)] = groupList;
}

UIChooserNode *UIChooserAbstractModel::getLocalGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened)
{
    /* Check passed stuff: */
    if (pParentNode->name() == strName)
        return pParentNode;

    /* Prepare variables: */
    const QString strFirstSubName = strName.section('/', 0, 0);
    const QString strFirstSuffix = strName.section('/', 1, -1);
    const QString strSecondSubName = strFirstSuffix.section('/', 0, 0);
    const QString strSecondSuffix = strFirstSuffix.section('/', 1, -1);

    /* Passed group name equal to first sub-name: */
    if (pParentNode->name() == strFirstSubName)
    {
        /* Make sure first-suffix is NOT empty: */
        AssertMsg(!strFirstSuffix.isEmpty(), ("Invalid group name!"));
        /* Trying to get group node among our children: */
        foreach (UIChooserNode *pNode, pParentNode->nodes(UIChooserNodeType_Group))
        {
            AssertPtrReturn(pNode, 0);
            UIChooserNodeGroup *pGroupNode = pNode->toGroupNode();
            AssertPtrReturn(pGroupNode, 0);
            if (   pGroupNode->groupType() == UIChooserNodeGroupType_Local
                && pNode->name() == strSecondSubName)
            {
                UIChooserNode *pFoundNode = getLocalGroupNode(strFirstSuffix, pNode, fAllGroupsOpened);
                if (UIChooserNodeGroup *pFoundGroupNode = pFoundNode->toGroupNode())
                    if (fAllGroupsOpened && pFoundGroupNode->isClosed())
                        pFoundGroupNode->open();
                return pFoundNode;
            }
        }
    }

    /* Found nothing? Creating: */
    UIChooserNodeGroup *pNewGroupNode =
        new UIChooserNodeGroup(pParentNode,
                               getDesiredNodePosition(pParentNode,
                                                      UIChooserNodeDataPrefixType_Local,
                                                      strSecondSubName),
                               QUuid() /* id */,
                               strSecondSubName,
                               UIChooserNodeGroupType_Local,
                               fAllGroupsOpened || shouldGroupNodeBeOpened(pParentNode,
                                                                           UIChooserNodeDataPrefixType_Local,
                                                                           strSecondSubName));
    return strSecondSuffix.isEmpty() ? pNewGroupNode : getLocalGroupNode(strFirstSuffix, pNewGroupNode, fAllGroupsOpened);
}

UIChooserNode *UIChooserAbstractModel::getCloudGroupNode(const QString &strName, UIChooserNode *pParentNode, bool fAllGroupsOpened)
{
    /* Check passed stuff: */
    if (pParentNode->name() == strName)
        return pParentNode;

    /* Prepare variables: */
    const QString strFirstSubName = strName.section('/', 0, 0);
    const QString strFirstSuffix = strName.section('/', 1, -1);
    const QString strSecondSubName = strFirstSuffix.section('/', 0, 0);

    /* Passed group name equal to first sub-name: */
    if (pParentNode->name() == strFirstSubName)
    {
        /* Make sure first-suffix is NOT empty: */
        AssertMsg(!strFirstSuffix.isEmpty(), ("Invalid group name!"));
        /* Trying to get group node among our children: */
        foreach (UIChooserNode *pNode, pParentNode->nodes(UIChooserNodeType_Group))
        {
            AssertPtrReturn(pNode, 0);
            UIChooserNodeGroup *pGroupNode = pNode->toGroupNode();
            AssertPtrReturn(pGroupNode, 0);
            if (   (   pGroupNode->groupType() == UIChooserNodeGroupType_Provider
                    || pGroupNode->groupType() == UIChooserNodeGroupType_Profile)
                && pNode->name() == strSecondSubName)
            {
                UIChooserNode *pFoundNode = getCloudGroupNode(strFirstSuffix, pNode, fAllGroupsOpened);
                if (UIChooserNodeGroup *pFoundGroupNode = pFoundNode->toGroupNode())
                    if (fAllGroupsOpened && pFoundGroupNode->isClosed())
                        pFoundGroupNode->open();
                return pFoundNode;
            }
        }
    }

    /* Found nothing? Returning parent: */
    AssertFailedReturn(pParentNode);
}

bool UIChooserAbstractModel::shouldGroupNodeBeOpened(UIChooserNode *pParentNode,
                                                     UIChooserNodeDataPrefixType enmDataType,
                                                     const QString &strName) const
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->machineGroupDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return false;

    /* Prepare required group definition reg-exp: */
    const QString strNodePrefix = prefixToString(enmDataType);
    const QString strNodeOptionOpened = optionToString(UIChooserNodeDataOptionType_GroupOpened);
    const QString strDefinitionTemplate = QString("%1(\\S)*=%2").arg(strNodePrefix, strName);
    const QRegularExpression re(strDefinitionTemplate);
    /* For each the group definition: */
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this is required definition: */
        const QRegularExpressionMatch mt = re.match(strDefinition);
        if (mt.capturedStart() == 0)
        {
            /* Get group descriptor: */
            const QString strDescriptor = mt.captured(1);
            if (strDescriptor.contains(strNodeOptionOpened))
                return true;
        }
    }

    /* Return 'false' by default: */
    return false;
}

bool UIChooserAbstractModel::shouldGlobalNodeBeFavorite(UIChooserNode *pParentNode) const
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->machineGroupDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return false;

    /* Prepare required group definition reg-exp: */
    const QString strNodePrefix = prefixToString(UIChooserNodeDataPrefixType_Global);
    const QString strNodeOptionFavorite = optionToString(UIChooserNodeDataOptionType_GlobalFavorite);
    const QString strNodeValueDefault = valueToString(UIChooserNodeDataValueType_GlobalDefault);
    const QString strDefinitionTemplate = QString("%1(\\S)*=%2").arg(strNodePrefix, strNodeValueDefault);
    const QRegularExpression re(strDefinitionTemplate);
    /* For each the group definition: */
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this is required definition: */
        const QRegularExpressionMatch mt = re.match(strDefinition);
        if (mt.capturedStart() == 0)
        {
            /* Get group descriptor: */
            const QString strDescriptor = mt.captured(1);
            if (strDescriptor.contains(strNodeOptionFavorite))
                return true;
        }
    }

    /* Return 'false' by default: */
    return false;
}

void UIChooserAbstractModel::wipeOutEmptyGroupsStartingFrom(UIChooserNode *pParent)
{
    /* Cleanup all the group children recursively first: */
    foreach (UIChooserNode *pNode, pParent->nodes(UIChooserNodeType_Group))
        wipeOutEmptyGroupsStartingFrom(pNode);
    /* If parent isn't root and has no nodes: */
    if (!pParent->isRoot() && !pParent->hasNodes())
    {
        /* Delete parent node and item: */
        delete pParent;
    }
}

int UIChooserAbstractModel::getDesiredNodePosition(UIChooserNode *pParentNode,
                                                   UIChooserNodeDataPrefixType enmDataType,
                                                   const QString &strName)
{
    /* End of list (by default)? */
    int iNewNodeDesiredPosition = -1;
    /* Which position should be new node placed by definitions: */
    const int iNewNodeDefinitionPosition = getDefinedNodePosition(pParentNode, enmDataType, strName);

    /* If some position defined: */
    if (iNewNodeDefinitionPosition != -1)
    {
        /* Start of list if some definition present: */
        iNewNodeDesiredPosition = 0;
        /* We have to check all the existing node positions: */
        UIChooserNodeType enmType = UIChooserNodeType_Any;
        switch (enmDataType)
        {
            case UIChooserNodeDataPrefixType_Global:   enmType = UIChooserNodeType_Global; break;
            case UIChooserNodeDataPrefixType_Machine:  enmType = UIChooserNodeType_Machine; break;
            case UIChooserNodeDataPrefixType_Local:
            case UIChooserNodeDataPrefixType_Provider:
            case UIChooserNodeDataPrefixType_Profile:  enmType = UIChooserNodeType_Group; break;
        }
        const QList<UIChooserNode*> nodes = pParentNode->nodes(enmType);
        for (int i = nodes.size() - 1; i >= 0; --i)
        {
            /* Get current node: */
            UIChooserNode *pNode = nodes.at(i);
            AssertPtrReturn(pNode, iNewNodeDesiredPosition);
            /* Which position should be current node placed by definitions? */
            UIChooserNodeDataPrefixType enmNodeDataType = UIChooserNodeDataPrefixType_Global;
            QString strDefinitionName;
            switch (pNode->type())
            {
                case UIChooserNodeType_Machine:
                {
                    enmNodeDataType = UIChooserNodeDataPrefixType_Machine;
                    strDefinitionName = toOldStyleUuid(pNode->toMachineNode()->id());
                    break;
                }
                case UIChooserNodeType_Group:
                {
                    /* Cast to group node: */
                    UIChooserNodeGroup *pGroupNode = pNode->toGroupNode();
                    AssertPtrReturn(pGroupNode, iNewNodeDesiredPosition);
                    switch (pGroupNode->groupType())
                    {
                        case UIChooserNodeGroupType_Local:    enmNodeDataType = UIChooserNodeDataPrefixType_Local; break;
                        case UIChooserNodeGroupType_Provider: enmNodeDataType = UIChooserNodeDataPrefixType_Provider; break;
                        case UIChooserNodeGroupType_Profile:  enmNodeDataType = UIChooserNodeDataPrefixType_Profile; break;
                        default: break;
                    }
                    strDefinitionName = pNode->name();
                    break;
                }
                default:
                    break;
            }
            /* If some position defined: */
            const int iNodeDefinitionPosition = getDefinedNodePosition(pParentNode, enmNodeDataType, strDefinitionName);
            if (iNodeDefinitionPosition != -1)
            {
                AssertReturn(iNodeDefinitionPosition != iNewNodeDefinitionPosition, iNewNodeDesiredPosition);
                if (iNodeDefinitionPosition < iNewNodeDefinitionPosition)
                {
                    iNewNodeDesiredPosition = i + 1;
                    break;
                }
            }
        }
    }

    /* Return desired node position: */
    return iNewNodeDesiredPosition;
}

int UIChooserAbstractModel::getDefinedNodePosition(UIChooserNode *pParentNode, UIChooserNodeDataPrefixType enmDataType, const QString &strName)
{
    /* Read group definitions: */
    const QStringList definitions = gEDataManager->machineGroupDefinitions(pParentNode->fullName());
    /* Return 'false' if no definitions found: */
    if (definitions.isEmpty())
        return -1;

    /* Prepare definition reg-exp: */
    QString strDefinitionTemplateShort;
    QString strDefinitionTemplateFull;
    const QString strNodePrefixLocal = prefixToString(UIChooserNodeDataPrefixType_Local);
    const QString strNodePrefixProvider = prefixToString(UIChooserNodeDataPrefixType_Provider);
    const QString strNodePrefixProfile = prefixToString(UIChooserNodeDataPrefixType_Profile);
    const QString strNodePrefixMachine = prefixToString(UIChooserNodeDataPrefixType_Machine);
    switch (enmDataType)
    {
        case UIChooserNodeDataPrefixType_Local:
        {
            strDefinitionTemplateShort = QString("^[%1%2%3](\\S)*=").arg(strNodePrefixLocal, strNodePrefixProvider, strNodePrefixProfile);
            strDefinitionTemplateFull = QString("^%1(\\S)*=%2$").arg(strNodePrefixLocal, strName);
            break;
        }
        case UIChooserNodeDataPrefixType_Provider:
        {
            strDefinitionTemplateShort = QString("^[%1%2%3](\\S)*=").arg(strNodePrefixLocal, strNodePrefixProvider, strNodePrefixProfile);
            strDefinitionTemplateFull = QString("^%1(\\S)*=%2$").arg(strNodePrefixProvider, strName);
            break;
        }
        case UIChooserNodeDataPrefixType_Profile:
        {
            strDefinitionTemplateShort = QString("^[%1%2%3](\\S)*=").arg(strNodePrefixLocal, strNodePrefixProvider, strNodePrefixProfile);
            strDefinitionTemplateFull = QString("^%1(\\S)*=%2$").arg(strNodePrefixProfile, strName);
            break;
        }
        case UIChooserNodeDataPrefixType_Machine:
        {
            strDefinitionTemplateShort = QString("^%1=").arg(strNodePrefixMachine);
            strDefinitionTemplateFull = QString("^%1=%2$").arg(strNodePrefixMachine, strName);
            break;
        }
        default:
            return -1;
    }
    const QRegularExpression definitionRegExpShort(strDefinitionTemplateShort);
    const QRegularExpression definitionRegExpFull(strDefinitionTemplateFull);

    /* For each the definition: */
    int iDefinitionIndex = -1;
    foreach (const QString &strDefinition, definitions)
    {
        /* Check if this definition is of required type: */
        const QRegularExpressionMatch mtShort = definitionRegExpShort.match(strDefinition);
        if (mtShort.capturedStart() == 0)
        {
            ++iDefinitionIndex;
            /* Check if this definition is exactly what we need: */
            const QRegularExpressionMatch mtFull = definitionRegExpFull.match(strDefinition);
            if (mtFull.capturedStart() == 0)
                return iDefinitionIndex;
        }
    }

    /* Return result: */
    return -1;
}

void UIChooserAbstractModel::createLocalMachineNode(UIChooserNode *pParentNode, const CMachine &comMachine)
{
    new UIChooserNodeMachine(pParentNode,
                             getDesiredNodePosition(pParentNode,
                                                    UIChooserNodeDataPrefixType_Machine,
                                                    toOldStyleUuid(comMachine.GetId())),
                             comMachine);
}

void UIChooserAbstractModel::createCloudMachineNode(UIChooserNode *pParentNode, UIFakeCloudVirtualMachineItemState enmState)
{
    new UIChooserNodeMachine(pParentNode,
                             0 /* position */,
                             enmState);
}

void UIChooserAbstractModel::createCloudMachineNode(UIChooserNode *pParentNode, const CCloudMachine &comMachine)
{
    UIChooserNodeMachine *pNode = new UIChooserNodeMachine(pParentNode,
                                                           getDesiredNodePosition(pParentNode,
                                                                                  UIChooserNodeDataPrefixType_Machine,
                                                                                  toOldStyleUuid(comMachine.GetId())),
                                                           comMachine);
    /* Request for async node update if necessary: */
    if (   m_fKeepCloudNodesUpdated
        || !comMachine.GetAccessible())
    {
        AssertReturnVoid(pNode && pNode->cacheType() == UIVirtualMachineItemType_CloudReal);
        UIVirtualMachineItemCloud *pCloudMachineItem = pNode->cache()->toCloud();
        pCloudMachineItem->setUpdateRequiredByGlobalReason(m_fKeepCloudNodesUpdated);
        pCloudMachineItem->updateInfoAsync(false /* delayed? */);
    }
}

QStringList UIChooserAbstractModel::gatherPossibleGroupNodeNames(UIChooserNode *pCurrentNode, QList<UIChooserNode*> exceptions) const
{
    /* Prepare result: */
    QStringList result;

    /* Walk through all the children and make sure there are no exceptions: */
    bool fAddCurrent = true;
    foreach (UIChooserNode *pChild, pCurrentNode->nodes(UIChooserNodeType_Any))
    {
        AssertPtrReturn(pChild, result);
        if (exceptions.contains(pChild))
            fAddCurrent = false;
        else
        {
            if (pChild->type() == UIChooserNodeType_Group)
            {
                UIChooserNodeGroup *pChildGroup = pChild->toGroupNode();
                AssertPtrReturn(pChildGroup, result);
                if (pChildGroup->groupType() == UIChooserNodeGroupType_Local)
                    result << gatherPossibleGroupNodeNames(pChild, exceptions);
            }
        }
    }

    /* Add current item if not overridden: */
    if (fAddCurrent)
        result.prepend(pCurrentNode->fullName());

    /* Return result: */
    return result;
}

bool UIChooserAbstractModel::checkIfNodeContainChildWithId(UIChooserNode *pParentNode, const QUuid &uId) const
{
    /* Check parent-node type: */
    AssertPtrReturn(pParentNode, false);
    switch (pParentNode->type())
    {
        case UIChooserNodeType_Machine:
        {
            /* Check if pParentNode has the passed uId itself: */
            UIChooserNodeMachine *pMachineNode = pParentNode->toMachineNode();
            AssertPtrReturn(pMachineNode, false);
            if (pMachineNode->id() == uId)
                return true;
            break;
        }
        case UIChooserNodeType_Group:
        {
            /* Recursively iterate through children: */
            foreach (UIChooserNode *pChildNode, pParentNode->nodes())
                if (checkIfNodeContainChildWithId(pChildNode, uId))
                    return true;
            break;
        }
        default:
            break;
    }

    /* False by default: */
    return false;
}

void UIChooserAbstractModel::saveGroupSettings()
{
    /* Make sure there is no group settings saving activity: */
    if (UIThreadGroupSettingsSave::instance())
        return;

    /* Prepare full group map: */
    QMap<QString, QStringList> groups;
    gatherGroupSettings(groups, invisibleRoot());

    /* Save information in other thread: */
    UIThreadGroupSettingsSave::prepare();
    emit sigGroupSavingStateChanged();
    connect(UIThreadGroupSettingsSave::instance(), &UIThreadGroupSettingsSave::sigReload,
            this, &UIChooserAbstractModel::sltReloadMachine);
    UIThreadGroupSettingsSave::instance()->configure(this, m_groups, groups);
    UIThreadGroupSettingsSave::instance()->start();
    m_groups = groups;
}

void UIChooserAbstractModel::saveGroupDefinitions()
{
    /* Make sure there is no group definitions save activity: */
    if (UIThreadGroupDefinitionsSave::instance())
        return;

    /* Prepare full group map: */
    QMap<QString, QStringList> groups;
    gatherGroupDefinitions(groups, invisibleRoot());

    /* Save information in other thread: */
    UIThreadGroupDefinitionsSave::prepare();
    emit sigGroupSavingStateChanged();
    UIThreadGroupDefinitionsSave::instance()->configure(this, groups);
    UIThreadGroupDefinitionsSave::instance()->start();
}

void UIChooserAbstractModel::gatherGroupSettings(QMap<QString, QStringList> &settings,
                                                 UIChooserNode *pParentGroup)
{
    /* Iterate over all the machine-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Machine))
    {
        /* Make sure it's really machine node: */
        AssertPtrReturnVoid(pNode);
        UIChooserNodeMachine *pMachineNode = pNode->toMachineNode();
        AssertPtrReturnVoid(pMachineNode);
        /* Make sure it's local machine node exactly and it's accessible: */
        if (   pMachineNode->cacheType() == UIVirtualMachineItemType_Local
            && pMachineNode->accessible())
            settings[toOldStyleUuid(pMachineNode->id())] << pParentGroup->fullName();
    }
    /* Iterate over all the group-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Group))
        gatherGroupSettings(settings, pNode);
}

void UIChooserAbstractModel::gatherGroupDefinitions(QMap<QString, QStringList> &definitions,
                                                    UIChooserNode *pParentGroup)
{
    /* Prepare extra-data key for current group: */
    const QString strExtraDataKey = pParentGroup->fullName();
    /* Iterate over all the global-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Global))
    {
        /* Append node definition: */
        AssertPtrReturnVoid(pNode);
        definitions[strExtraDataKey] << pNode->definition(true /* full */);
    }
    /* Iterate over all the group-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Group))
    {
        /* Append node definition: */
        AssertPtrReturnVoid(pNode);
        definitions[strExtraDataKey] << pNode->definition(true /* full */);
        /* Go recursively through children: */
        gatherGroupDefinitions(definitions, pNode);
    }
    /* Iterate over all the machine-nodes: */
    foreach (UIChooserNode *pNode, pParentGroup->nodes(UIChooserNodeType_Machine))
    {
        /* Make sure it's really machine node: */
        AssertPtrReturnVoid(pNode);
        UIChooserNodeMachine *pMachineNode = pNode->toMachineNode();
        AssertPtrReturnVoid(pMachineNode);
        /* Append node definition, make sure it's local or real cloud machine node only: */
        if (   pMachineNode->cacheType() == UIVirtualMachineItemType_Local
            || pMachineNode->cacheType() == UIVirtualMachineItemType_CloudReal)
            definitions[strExtraDataKey] << pNode->definition(true /* full */);
    }
}

void UIChooserAbstractModel::makeSureGroupSettingsSaveIsFinished()
{
    /* Cleanup if necessary: */
    if (UIThreadGroupSettingsSave::instance())
        UIThreadGroupSettingsSave::cleanup();
}

void UIChooserAbstractModel::makeSureGroupDefinitionsSaveIsFinished()
{
    /* Cleanup if necessary: */
    if (UIThreadGroupDefinitionsSave::instance())
        UIThreadGroupDefinitionsSave::cleanup();
}

UIChooserNode *UIChooserAbstractModel::searchProviderNode(const QUuid &uProviderId)
{
    /* Search for a list of nodes matching passed name: */
    QList<UIChooserNode*> providerNodes;
    invisibleRoot()->searchForNodes(uProviderId.toString(),
                                    UIChooserItemSearchFlag_CloudProvider | UIChooserItemSearchFlag_ExactId,
                                    providerNodes);

    /* Return 1st node if any: */
    return providerNodes.value(0);
}

UIChooserNode *UIChooserAbstractModel::searchProviderNode(const QString &strProviderShortName)
{
    /* Search for a list of nodes matching passed name: */
    QList<UIChooserNode*> providerNodes;
    invisibleRoot()->searchForNodes(strProviderShortName,
                                    UIChooserItemSearchFlag_CloudProvider | UIChooserItemSearchFlag_ExactName,
                                    providerNodes);

    /* Return 1st node if any: */
    return providerNodes.value(0);
}

UIChooserNode *UIChooserAbstractModel::searchProfileNode(UIChooserNode *pProviderNode, const QString &strProfileName)
{
    AssertPtrReturn(pProviderNode, 0);

    /* Search for a list of nodes matching passed name: */
    QList<UIChooserNode*> profileNodes;
    pProviderNode->searchForNodes(strProfileName,
                                  UIChooserItemSearchFlag_CloudProfile | UIChooserItemSearchFlag_ExactName,
                                  profileNodes);

    /* Return 1st node if any: */
    return profileNodes.value(0);
}

UIChooserNode *UIChooserAbstractModel::searchProfileNode(const QString &strProviderShortName, const QString &strProfileName)
{
    /* Wrap method above: */
    return searchProfileNode(searchProviderNode(strProviderShortName), strProfileName);
}

UIChooserNode *UIChooserAbstractModel::searchMachineNode(UIChooserNode *pProfileNode, const QUuid &uMachineId)
{
    AssertPtrReturn(pProfileNode, 0);

    /* Search for a list of nodes matching passed ID: */
    QList<UIChooserNode*> machineNodes;
    pProfileNode->searchForNodes(uMachineId.toString(),
                                 UIChooserItemSearchFlag_Machine | UIChooserItemSearchFlag_ExactId,
                                 machineNodes);

    /* Return 1st node if any: */
    return machineNodes.value(0);
}

UIChooserNode *UIChooserAbstractModel::searchMachineNode(const QString &strProviderShortName, const QString &strProfileName, const QUuid &uMachineId)
{
    /* Wrap method above: */
    return searchMachineNode(searchProfileNode(strProviderShortName, strProfileName), uMachineId);
}

UIChooserNode *UIChooserAbstractModel::searchFakeNode(UIChooserNode *pProfileNode)
{
    /* Wrap method above: */
    return searchMachineNode(pProfileNode, QUuid());
}

UIChooserNode *UIChooserAbstractModel::searchFakeNode(const QString &strProviderShortName, const QString &strProfileName)
{
    /* Wrap method above: */
    return searchMachineNode(strProviderShortName, strProfileName, QUuid());
}

QList<UIChooserNode*> UIChooserAbstractModel::enumerateCloudMachineNodes() const
{
    /* Search for a list of provider nodes: */
    QList<UIChooserNode*> providerNodes;
    invisibleRoot()->searchForNodes(QString(),
                                    UIChooserItemSearchFlag_CloudProvider,
                                    providerNodes);

    /* Search for a list of profile nodes: */
    QList<UIChooserNode*> profileNodes;
    foreach (UIChooserNode *pProviderNode, providerNodes)
        pProviderNode->searchForNodes(QString(),
                                      UIChooserItemSearchFlag_CloudProfile,
                                      profileNodes);

    /* Search for a list of machine nodes: */
    QList<UIChooserNode*> machineNodes;
    foreach (UIChooserNode *pProfileNode, profileNodes)
        pProfileNode->searchForNodes(QString(),
                                     UIChooserItemSearchFlag_Machine,
                                     machineNodes);

    return machineNodes;
}

void UIChooserAbstractModel::stopCloudUpdates(bool fForced /* = false */)
{
    /* Stop all cloud entity updates currently being performed: */
    foreach (const UICloudEntityKey &key, m_cloudEntityKeysBeingUpdated)
    {
        /* For profiles: */
        if (key.m_uMachineId.isNull())
        {
            /* Search task child by key: */
            UIProgressTaskReadCloudMachineList *pTask = findChild<UIProgressTaskReadCloudMachineList*>(key.toString());
            AssertPtrReturnVoid(pTask);

            /* Wait for cloud profile refresh task to complete,
             * then delete the task itself manually: */
            if (!fForced)
                pTask->cancel();
            delete pTask;
        }
        /* For machines: */
        else
        {
            /* Search machine node: */
            UIChooserNode *pNode = searchMachineNode(key.m_strProviderShortName, key.m_strProfileName, key.m_uMachineId);
            AssertPtrReturnVoid(pNode);
            /* Acquire cloud machine item: */
            UIVirtualMachineItemCloud *pCloudMachineItem = pNode->toMachineNode()->cache()->toCloud();
            AssertPtrReturnVoid(pCloudMachineItem);

            /* Wait for cloud machine refresh task to complete,
             * task itself will be deleted with the machine-node: */
            pCloudMachineItem->waitForAsyncInfoUpdateFinished();
        }
    }

    /* We haven't let tasks to unregister themselves
     * so we have to cleanup task set ourselves: */
    m_cloudEntityKeysBeingUpdated.clear();
}


#include "UIChooserAbstractModel.moc"
