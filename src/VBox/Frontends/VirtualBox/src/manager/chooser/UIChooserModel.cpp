/* $Id: UIChooserModel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserModel class implementation.
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
#include <QDrag>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsView>
#include <QScrollBar>
#include <QTimer>

/* GUI includes: */
#include "QIMessageBox.h"
#include "UIActionPoolManager.h"
#include "UIChooser.h"
#include "UIChooserHandlerMouse.h"
#include "UIChooserHandlerKeyboard.h"
#include "UIChooserItemGroup.h"
#include "UIChooserItemGlobal.h"
#include "UIChooserItemMachine.h"
#include "UIChooserModel.h"
#include "UIChooserNode.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeGlobal.h"
#include "UIChooserNodeMachine.h"
#include "UIChooserView.h"
#include "UICloudNetworkingStuff.h"
#include "UIExtraDataManager.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UIVirtualBoxManagerWidget.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"

/* Type defs: */
typedef QSet<QString> UIStringSet;


UIChooserModel::UIChooserModel(UIChooser *pParent, UIActionPool *pActionPool)
    : UIChooserAbstractModel(pParent)
    , m_pActionPool(pActionPool)
    , m_pScene(0)
    , m_pMouseHandler(0)
    , m_pKeyboardHandler(0)
    , m_fSelectionSaveAllowed(false)
    , m_iCurrentSearchResultIndex(-1)
    , m_iScrollingTokenSize(30)
    , m_fIsScrollingInProgress(false)
    , m_iGlobalItemHeightHint(0)
    , m_pTimerCloudProfileUpdate(0)
{
    prepare();
}

UIChooserModel::~UIChooserModel()
{
    cleanup();
}

void UIChooserModel::init()
{
    /* Call to base-class: */
    UIChooserAbstractModel::init();

    /* Build tree for main root: */
    buildTreeForMainRoot();
    /* Load settings: */
    loadSettings();
}

UIActionPool *UIChooserModel::actionPool() const
{
    return m_pActionPool;
}

QGraphicsScene *UIChooserModel::scene() const
{
    return m_pScene;
}

UIChooserView *UIChooserModel::view() const
{
    return scene() && !scene()->views().isEmpty() ? qobject_cast<UIChooserView*>(scene()->views().first()) : 0;
}

QPaintDevice *UIChooserModel::paintDevice() const
{
    return scene() && !scene()->views().isEmpty() ? scene()->views().first() : 0;
}

QGraphicsItem *UIChooserModel::itemAt(const QPointF &position, const QTransform &deviceTransform /* = QTransform() */) const
{
    return scene() ? scene()->itemAt(position, deviceTransform) : 0;
}

void UIChooserModel::handleToolButtonClick(UIChooserItem *pItem)
{
    emit sigToolMenuRequested(pItem->mapToScene(QPointF(pItem->size().width(), 0)).toPoint(),
                              pItem->type() == UIChooserNodeType_Machine ? pItem->toMachineItem()->cache() : 0);
}

void UIChooserModel::handlePinButtonClick(UIChooserItem *pItem)
{
    switch (pItem->type())
    {
        case UIChooserNodeType_Global:
            pItem->setFavorite(!pItem->isFavorite());
            break;
        default:
            break;
    }
}

void UIChooserModel::setSelectedItems(const QList<UIChooserItem*> &items)
{
    /* Is there something changed? */
    if (m_selectedItems == items)
        return;

    /* Remember old selected-item list: */
    const QList<UIChooserItem*> oldCurrentItems = m_selectedItems;

    /* Clear current selected-item list: */
    m_selectedItems.clear();

    /* Iterate over all the passed items: */
    foreach (UIChooserItem *pItem, items)
    {
        /* Add item to current selected-item list if navigation list contains it: */
        if (pItem && navigationItems().contains(pItem))
            m_selectedItems << pItem;
        else
            AssertMsgFailed(("Passed item is not in navigation list!"));
    }

    /* Make sure selection list is never empty if current-item present: */
    if (m_selectedItems.isEmpty() && currentItem() && navigationItems().contains(currentItem()))
        m_selectedItems << currentItem();

    /* Is there something really changed? */
    if (oldCurrentItems == m_selectedItems)
        return;

    /* Update all the old items (they are no longer selected): */
    foreach (UIChooserItem *pItem, oldCurrentItems)
    {
        pItem->setSelected(false);
        pItem->update();
    }
    /* Update all the new items (they are selected now): */
    foreach (UIChooserItem *pItem, m_selectedItems)
    {
        pItem->setSelected(true);
        pItem->update();
    }

    /* Should the selection changes be saved? */
    if (m_fSelectionSaveAllowed)
    {
        /* Acquire first selected item: */
        UIChooserItem *pFirstSelectedItem = m_selectedItems.value(0);
        /* If this item is of machine type: */
        if (   pFirstSelectedItem
            && pFirstSelectedItem->type() == UIChooserNodeType_Machine)
        {
            /* Cast to machine item: */
            UIChooserItemMachine *pMachineItem = pFirstSelectedItem->toMachineItem();
            /* If this machine item is of cloud type =>
             * Choose the parent (profile) group item as the last one selected: */
            if (   pMachineItem
                && (   pMachineItem->cacheType() == UIVirtualMachineItemType_CloudFake
                    || pMachineItem->cacheType() == UIVirtualMachineItemType_CloudReal))
                pFirstSelectedItem = pMachineItem->parentItem();
        }
        /* Save last selected-item: */
        gEDataManager->setSelectorWindowLastItemChosen(pFirstSelectedItem ? pFirstSelectedItem->definition() : QString());
    }

    /* Notify about selection changes: */
    emit sigSelectionChanged();
}

void UIChooserModel::setSelectedItem(UIChooserItem *pItem)
{
    /* Call for wrapper above: */
    QList<UIChooserItem*> items;
    if (pItem)
        items << pItem;
    setSelectedItems(items);

    /* Make selected-item current one as well: */
    setCurrentItem(firstSelectedItem());
}

void UIChooserModel::setSelectedItem(const QString &strDefinition)
{
    /* Search an item by definition: */
    UIChooserItem *pItem = searchItemByDefinition(strDefinition);

    /* Make sure found item is in navigation list: */
    if (!pItem || !navigationItems().contains(pItem))
        return;

    /* Call for wrapper above: */
    setSelectedItem(pItem);
}

void UIChooserModel::clearSelectedItems()
{
    /* Call for wrapper above: */
    setSelectedItem(0);
}

const QList<UIChooserItem*> &UIChooserModel::selectedItems() const
{
    return m_selectedItems;
}

void UIChooserModel::addToSelectedItems(UIChooserItem *pItem)
{
    /* Prepare updated list: */
    QList<UIChooserItem*> list(selectedItems());
    list << pItem;
    /* Call for wrapper above: */
    setSelectedItems(list);
}

void UIChooserModel::removeFromSelectedItems(UIChooserItem *pItem)
{
    /* Prepare updated list: */
    QList<UIChooserItem*> list(selectedItems());
    list.removeAll(pItem);
    /* Call for wrapper above: */
    setSelectedItems(list);
}

UIChooserItem *UIChooserModel::firstSelectedItem() const
{
    /* Return first of selected-items, if any: */
    return selectedItems().value(0);
}

UIVirtualMachineItem *UIChooserModel::firstSelectedMachineItem() const
{
    /* Return first machine-item of the selected-item: */
    return      firstSelectedItem()
             && firstSelectedItem()->firstMachineItem()
             && firstSelectedItem()->firstMachineItem()->toMachineItem()
           ? firstSelectedItem()->firstMachineItem()->toMachineItem()->cache()
           : 0;
}

QList<UIVirtualMachineItem*> UIChooserModel::selectedMachineItems() const
{
    /* Gather list of selected unique machine-items: */
    QList<UIChooserItemMachine*> currentMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(selectedItems(), currentMachineItemList,
                                                UIChooserItemMachineEnumerationFlag_Unique);

    /* Reintegrate machine-items into valid format: */
    QList<UIVirtualMachineItem*> currentMachineList;
    foreach (UIChooserItemMachine *pItem, currentMachineItemList)
        currentMachineList << pItem->cache();
    return currentMachineList;
}

bool UIChooserModel::isGroupItemSelected() const
{
    return firstSelectedItem() && firstSelectedItem()->type() == UIChooserNodeType_Group;
}

bool UIChooserModel::isGlobalItemSelected() const
{
    return firstSelectedItem() && firstSelectedItem()->type() == UIChooserNodeType_Global;
}

bool UIChooserModel::isMachineItemSelected() const
{
    return firstSelectedItem() && firstSelectedItem()->type() == UIChooserNodeType_Machine;
}

bool UIChooserModel::isLocalMachineItemSelected() const
{
    return    isMachineItemSelected()
           && firstSelectedItem()->toMachineItem()->cacheType() == UIVirtualMachineItemType_Local;
}

bool UIChooserModel::isCloudMachineItemSelected() const
{
    return    isMachineItemSelected()
           && firstSelectedItem()->toMachineItem()->cacheType() == UIVirtualMachineItemType_CloudReal;
}

bool UIChooserModel::isSingleGroupSelected() const
{
    return    selectedItems().size() == 1
           && firstSelectedItem()->type() == UIChooserNodeType_Group;
}

bool UIChooserModel::isSingleLocalGroupSelected() const
{
    return    isSingleGroupSelected()
           && firstSelectedItem()->toGroupItem()->groupType() == UIChooserNodeGroupType_Local;
}

bool UIChooserModel::isSingleCloudProviderGroupSelected() const
{
    return    isSingleGroupSelected()
           && firstSelectedItem()->toGroupItem()->groupType() == UIChooserNodeGroupType_Provider;
}

bool UIChooserModel::isSingleCloudProfileGroupSelected() const
{
    return    isSingleGroupSelected()
           && firstSelectedItem()->toGroupItem()->groupType() == UIChooserNodeGroupType_Profile;
}

bool UIChooserModel::isAllItemsOfOneGroupSelected() const
{
    /* Make sure at least one item selected: */
    if (selectedItems().isEmpty())
        return false;

    /* Determine the parent group of the first item: */
    UIChooserItem *pFirstParent = firstSelectedItem()->parentItem();

    /* Make sure this parent is not main root-item: */
    if (pFirstParent == root())
        return false;

    /* Enumerate selected-item set: */
    QSet<UIChooserItem*> currentItemSet;
    foreach (UIChooserItem *pCurrentItem, selectedItems())
        currentItemSet << pCurrentItem;

    /* Enumerate first parent children set: */
    QSet<UIChooserItem*> firstParentItemSet;
    foreach (UIChooserItem *pFirstParentItem, pFirstParent->items())
        firstParentItemSet << pFirstParentItem;

    /* Check if both sets contains the same: */
    return currentItemSet == firstParentItemSet;
}

QString UIChooserModel::fullGroupName() const
{
    return isSingleGroupSelected() ? firstSelectedItem()->fullName() : firstSelectedItem()->parentItem()->fullName();
}

UIChooserItem *UIChooserModel::findClosestUnselectedItem() const
{
    /* Take the current-item (if any) as a starting point
     * and find the closest non-selected-item. */
    UIChooserItem *pItem = currentItem();
    if (!pItem)
        pItem = firstSelectedItem();
    if (pItem)
    {
        int idxBefore = navigationItems().indexOf(pItem) - 1;
        int idxAfter  = idxBefore + 2;
        while (idxBefore >= 0 || idxAfter < navigationItems().size())
        {
            if (idxAfter < navigationItems().size())
            {
                pItem = navigationItems().at(idxAfter);
                if (   !selectedItems().contains(pItem)
                    && (   pItem->type() == UIChooserNodeType_Machine
                        || pItem->type() == UIChooserNodeType_Global))
                    return pItem;
                ++idxAfter;
            }
            if (idxBefore >= 0)
            {
                pItem = navigationItems().at(idxBefore);
                if (   !selectedItems().contains(pItem)
                    && (   pItem->type() == UIChooserNodeType_Machine
                        || pItem->type() == UIChooserNodeType_Global))
                    return pItem;
                --idxBefore;
            }
        }
    }
    return 0;
}

void UIChooserModel::makeSureNoItemWithCertainIdSelected(const QUuid &uId)
{
    /* Look for all nodes with passed uId: */
    QList<UIChooserNode*> matchedNodes;
    invisibleRoot()->searchForNodes(uId.toString(),
                                    UIChooserItemSearchFlag_Machine |
                                    UIChooserItemSearchFlag_ExactId,
                                    matchedNodes);

    /* Compose a set of items with passed uId: */
    QSet<UIChooserItem*> matchedItems;
    foreach (UIChooserNode *pNode, matchedNodes)
        if (pNode && pNode->item())
            matchedItems << pNode->item();

    /* If we have at least one of those items currently selected: */
    const QList<UIChooserItem*> selectedItemsList = selectedItems();
    const QSet<UIChooserItem*> selectedItemsSet(selectedItemsList.begin(), selectedItemsList.end());
    if (selectedItemsSet.intersects(matchedItems))
        setSelectedItem(findClosestUnselectedItem());

    /* If global item is currently chosen, selection should be invalidated: */
    if (firstSelectedItem() && firstSelectedItem()->type() == UIChooserNodeType_Global)
        emit sigSelectionInvalidated();
}

void UIChooserModel::makeSureAtLeastOneItemSelected()
{
    /* If we have no item selected but
     * at least one in the navigation list (global item): */
    if (!firstSelectedItem() && !navigationItems().isEmpty())
    {
        /* We are choosing it, selection should be invalidated: */
        setSelectedItem(navigationItems().first());
        emit sigSelectionInvalidated();
    }
}

void UIChooserModel::setCurrentItem(UIChooserItem *pItem)
{
    /* Make sure real focus unset: */
    clearRealFocus();

    /* Is there something changed? */
    if (m_pCurrentItem == pItem)
        return;

    /* Remember old current-item: */
    UIChooserItem *pOldCurrentItem = m_pCurrentItem;

    /* Set new current-item: */
    m_pCurrentItem = pItem;

    /* Disconnect old current-item (if any): */
    if (pOldCurrentItem)
        disconnect(pOldCurrentItem, &UIChooserItem::destroyed, this, &UIChooserModel::sltCurrentItemDestroyed);
    /* Connect new current-item (if any): */
    if (m_pCurrentItem)
        connect(m_pCurrentItem.data(), &UIChooserItem::destroyed, this, &UIChooserModel::sltCurrentItemDestroyed);

    /* If dialog is visible and item exists => make it visible as well: */
    if (view() && view()->window() && root())
        if (view()->window()->isVisible() && pItem)
            root()->toGroupItem()->makeSureItemIsVisible(pItem);

    /* Make sure selection list is never empty if current-item present: */
    if (!firstSelectedItem() && m_pCurrentItem)
        setSelectedItem(m_pCurrentItem);
}

UIChooserItem *UIChooserModel::currentItem() const
{
    return m_pCurrentItem;
}

const QList<UIChooserItem*> &UIChooserModel::navigationItems() const
{
    return m_navigationItems;
}

void UIChooserModel::removeFromNavigationItems(UIChooserItem *pItem)
{
    AssertMsg(pItem, ("Passed item is invalid!"));
    m_navigationItems.removeAll(pItem);
}

void UIChooserModel::updateNavigationItemList()
{
    m_navigationItems.clear();
    m_navigationItems = createNavigationItemList(root());
}

UIChooserItem *UIChooserModel::searchItemByDefinition(const QString &strDefinition) const
{
    /* Null if empty definition passed: */
    if (strDefinition.isEmpty())
        return 0;

    /* Parse definition: */
    UIChooserItem *pItem = 0;
    const QString strItemType = strDefinition.section('=', 0, 0);
    const QString strItemDescriptor = strDefinition.section('=', 1, -1);
    /* Its a local group-item definition? */
    if (strItemType == prefixToString(UIChooserNodeDataPrefixType_Local))
    {
        /* Search for group-item with passed descriptor (name): */
        pItem = root()->searchForItem(strItemDescriptor,
                                      UIChooserItemSearchFlag_LocalGroup |
                                      UIChooserItemSearchFlag_FullName);
    }
    /* Its a provider group-item definition? */
    else if (strItemType == prefixToString(UIChooserNodeDataPrefixType_Provider))
    {
        /* Search for group-item with passed descriptor (name): */
        pItem = root()->searchForItem(strItemDescriptor,
                                      UIChooserItemSearchFlag_CloudProvider |
                                      UIChooserItemSearchFlag_FullName);
    }
    /* Its a profile group-item definition? */
    else if (strItemType == prefixToString(UIChooserNodeDataPrefixType_Profile))
    {
        /* Search for group-item with passed descriptor (name): */
        pItem = root()->searchForItem(strItemDescriptor,
                                      UIChooserItemSearchFlag_CloudProfile |
                                      UIChooserItemSearchFlag_FullName);
    }
    /* Its a global-item definition? */
    else if (strItemType == prefixToString(UIChooserNodeDataPrefixType_Global))
    {
        /* Search for global-item with required name: */
        pItem = root()->searchForItem(strItemDescriptor,
                                      UIChooserItemSearchFlag_Global |
                                      UIChooserItemSearchFlag_ExactName);
    }
    /* Its a machine-item definition? */
    else if (strItemType == prefixToString(UIChooserNodeDataPrefixType_Machine))
    {
        /* Search for machine-item with required ID: */
        pItem = root()->searchForItem(strItemDescriptor,
                                      UIChooserItemSearchFlag_Machine |
                                      UIChooserItemSearchFlag_ExactId);
    }

    /* Return result: */
    return pItem;
}

void UIChooserModel::performSearch(const QString &strSearchTerm, int iSearchFlags)
{
    /* Call to base-class: */
    UIChooserAbstractModel::performSearch(strSearchTerm, iSearchFlags);

    /* Select 1st found item: */
    selectSearchResult(true);
}

QList<UIChooserNode*> UIChooserModel::resetSearch()
{
    /* Reset search result index: */
    m_iCurrentSearchResultIndex = -1;

    /* Call to base-class: */
    return UIChooserAbstractModel::resetSearch();
}

void UIChooserModel::selectSearchResult(bool fIsNext)
{
    /* If nothing was found: */
    if (searchResult().isEmpty())
    {
        /* Reset search result index: */
        m_iCurrentSearchResultIndex = -1;
    }
    /* If something was found: */
    else
    {
        /* Advance index forward: */
        if (fIsNext)
        {
            if (++m_iCurrentSearchResultIndex >= searchResult().size())
                m_iCurrentSearchResultIndex = 0;
        }
        /* Advance index backward: */
        else
        {
            if (--m_iCurrentSearchResultIndex < 0)
                m_iCurrentSearchResultIndex = searchResult().size() - 1;
        }

        /* If found item exists: */
        if (searchResult().at(m_iCurrentSearchResultIndex))
        {
            /* Select curresponding found item, make sure it's visible, scroll if necessary: */
            UIChooserItem *pItem = searchResult().at(m_iCurrentSearchResultIndex)->item();
            if (pItem)
            {
                pItem->makeSureItsVisible();
                setSelectedItem(pItem);
            }
        }
    }

    /* Update the search widget's match count(s): */
    if (view())
        view()->setSearchResultsCount(searchResult().size(), m_iCurrentSearchResultIndex);
}

void UIChooserModel::setSearchWidgetVisible(bool fVisible)
{
    if (view())
        view()->setSearchWidgetVisible(fVisible);
}

UIChooserItem *UIChooserModel::root() const
{
    return m_pRoot.data();
}

void UIChooserModel::startEditingSelectedGroupItemName()
{
    /* Only for single selected local group: */
    if (!isSingleLocalGroupSelected())
        return;

    /* Start editing first selected item name: */
    firstSelectedItem()->startEditing();
}

void UIChooserModel::disbandSelectedGroupItem()
{
    /* Only for single selected local group: */
    if (!isSingleLocalGroupSelected())
        return;

    /* Check if we have collisions between disbandable group children and their potential siblings: */
    UIChooserItem *pCurrentItem = currentItem();
    UIChooserNode *pCurrentNode = pCurrentItem->node();
    UIChooserItem *pParentItem = pCurrentItem->parentItem();
    UIChooserNode *pParentNode = pParentItem->node();
    QList<UIChooserNode*> childrenToBeRenamed;
    foreach (UIChooserNode *pChildNode, pCurrentNode->nodes())
    {
        /* Acquire disbandable group child name to check for collision with group siblings: */
        const QString strChildName = pChildNode->name();
        UIChooserNode *pCollisionSibling = 0;
        /* And then compare this child name with all the sibling names: */
        foreach (UIChooserNode *pSiblingNode, pParentNode->nodes())
        {
            /* There can't be a collision between local child and cloud provider sibling: */
            if (   pSiblingNode->type() == UIChooserNodeType_Group
                && pSiblingNode->toGroupNode()->groupType() == UIChooserNodeGroupType_Provider)
                continue;
            /* If sibling isn't disbandable group itself and has name similar to one of group children: */
            if (pSiblingNode != pCurrentNode && pSiblingNode->name() == strChildName)
            {
                /* We have a collision sibling: */
                pCollisionSibling = pSiblingNode;
                break;
            }
        }
        /* If there is a collision sibling: */
        if (pCollisionSibling)
        {
            switch (pChildNode->type())
            {
                /* We can't resolve collision automatically for VMs: */
                case UIChooserNodeType_Machine:
                {
                    UINotificationMessage::cannotResolveCollisionAutomatically(strChildName, pParentNode->name());
                    return;
                }
                /* But we can do it for VM groups: */
                case UIChooserNodeType_Group:
                {
                    if (!msgCenter().confirmAutomaticCollisionResolve(strChildName, pParentNode->name()))
                        return;
                    childrenToBeRenamed << pChildNode;
                    break;
                }
                default:
                    break;
            }
        }
    }

    /* Copy all the children into our parent: */
    QList<UIChooserItem*> ungroupedItems;
    foreach (UIChooserNode *pNode, pCurrentNode->nodes())
    {
        switch (pNode->type())
        {
            case UIChooserNodeType_Group:
            {
                UIChooserNodeGroup *pGroupNode = new UIChooserNodeGroup(pParentNode,
                                                                        pParentNode->nodes().size(),
                                                                        pNode->toGroupNode());
                UIChooserItemGroup *pGroupItem = new UIChooserItemGroup(pParentItem, pGroupNode);
                if (childrenToBeRenamed.contains(pNode))
                    pGroupNode->setName(uniqueGroupName(pParentNode));
                ungroupedItems << pGroupItem;
                break;
            }
            case UIChooserNodeType_Machine:
            {
                UIChooserNodeMachine *pMachineNode = new UIChooserNodeMachine(pParentNode,
                                                                              pParentNode->nodes().size(),
                                                                              pNode->toMachineNode());
                UIChooserItemMachine *pMachineItem = new UIChooserItemMachine(pParentItem, pMachineNode);
                ungroupedItems << pMachineItem;
                break;
            }
            default:
                break;
        }
    }

    /* Delete current group: */
    delete pCurrentNode;

    /* And update model: */
    updateTreeForMainRoot();

    /* Choose ungrouped items if present: */
    if (!ungroupedItems.isEmpty())
    {
        setSelectedItems(ungroupedItems);
        setCurrentItem(firstSelectedItem());
    }
    makeSureAtLeastOneItemSelected();

    /* Save groups finally: */
    saveGroups();
}

void UIChooserModel::removeSelectedMachineItems()
{
    /* Enumerate all the selected machine-items: */
    QList<UIChooserItemMachine*> selectedMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(selectedItems(), selectedMachineItemList);
    /* Enumerate all the existing machine-items: */
    QList<UIChooserItemMachine*> existingMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(root()->items(), existingMachineItemList);

    /* Prepare arrays: */
    QMap<QUuid, bool> verdicts;
    QList<UIChooserItemMachine*> localMachineItemsToRemove;
    QList<CMachine> localMachinesToUnregister;
    QList<UIChooserItemMachine*> cloudMachineItemsToUnregister;

    /* For each selected machine-item: */
    foreach (UIChooserItemMachine *pMachineItem, selectedMachineItemList)
    {
        /* Get machine-item id: */
        AssertPtrReturnVoid(pMachineItem);
        const QUuid uId = pMachineItem->id();

        /* We already decided for that machine? */
        if (verdicts.contains(uId))
        {
            /* To remove similar machine items? */
            if (!verdicts.value(uId))
                localMachineItemsToRemove << pMachineItem;
            continue;
        }

        /* Selected copy count: */
        int iSelectedCopyCount = 0;
        foreach (UIChooserItemMachine *pSelectedItem, selectedMachineItemList)
        {
            AssertPtrReturnVoid(pSelectedItem);
            if (pSelectedItem->id() == uId)
                ++iSelectedCopyCount;
        }
        /* Existing copy count: */
        int iExistingCopyCount = 0;
        foreach (UIChooserItemMachine *pExistingItem, existingMachineItemList)
        {
            AssertPtrReturnVoid(pExistingItem);
            if (pExistingItem->id() == uId)
                ++iExistingCopyCount;
        }
        /* If selected copy count equal to existing copy count,
         * we will propose ro unregister machine fully else
         * we will just propose to remove selected-items: */
        const bool fVerdict = iSelectedCopyCount == iExistingCopyCount;
        verdicts.insert(uId, fVerdict);
        if (fVerdict)
        {
            if (pMachineItem->cacheType() == UIVirtualMachineItemType_Local)
                localMachinesToUnregister.append(pMachineItem->cache()->toLocal()->machine());
            else if (pMachineItem->cacheType() == UIVirtualMachineItemType_CloudReal)
                cloudMachineItemsToUnregister.append(pMachineItem);
        }
        else
            localMachineItemsToRemove << pMachineItem;
    }

    /* If we have something to remove: */
    if (!localMachineItemsToRemove.isEmpty())
        removeLocalMachineItems(localMachineItemsToRemove);
    /* If we have something local to unregister: */
    if (!localMachinesToUnregister.isEmpty())
        unregisterLocalMachines(localMachinesToUnregister);
    /* If we have something cloud to unregister: */
    if (!cloudMachineItemsToUnregister.isEmpty())
        unregisterCloudMachineItems(cloudMachineItemsToUnregister);
}

void UIChooserModel::moveSelectedMachineItemsToGroupItem(const QString &strName /* = QString() */)
{
    /* Prepare target group pointers: */
    UIChooserNodeGroup *pTargetGroupNode = 0;
    UIChooserItemGroup *pTargetGroupItem = 0;
    if (strName.isNull())
    {
        /* Create new group node in the current root: */
        pTargetGroupNode = new UIChooserNodeGroup(invisibleRoot(),
                                                  invisibleRoot()->nodes().size() /* position */,
                                                  QUuid() /* id */,
                                                  uniqueGroupName(invisibleRoot()),
                                                  UIChooserNodeGroupType_Local,
                                                  true /* opened */);
        pTargetGroupItem = new UIChooserItemGroup(root(), pTargetGroupNode);
    }
    else
    {
        /* Search for existing group with certain name: */
        UIChooserItem *pTargetItem = root()->searchForItem(strName,
                                                           UIChooserItemSearchFlag_LocalGroup |
                                                           UIChooserItemSearchFlag_FullName);
        AssertPtrReturnVoid(pTargetItem);
        pTargetGroupItem = pTargetItem->toGroupItem();
        UIChooserNode *pTargetNode = pTargetItem->node();
        AssertPtrReturnVoid(pTargetNode);
        pTargetGroupNode = pTargetNode->toGroupNode();
    }
    AssertPtrReturnVoid(pTargetGroupNode);
    AssertPtrReturnVoid(pTargetGroupItem);

    /* For each of currently selected-items: */
    QStringList busyGroupNames;
    QStringList busyMachineNames;
    QList<UIChooserItem*> copiedItems;
    foreach (UIChooserItem *pItem, selectedItems())
    {
        /* For each of known types: */
        switch (pItem->type())
        {
            case UIChooserNodeType_Group:
            {
                /* Avoid name collisions: */
                if (busyGroupNames.contains(pItem->name()))
                    break;
                /* Add name to busy: */
                busyGroupNames << pItem->name();
                /* Copy or move group-item: */
                UIChooserNodeGroup *pNewGroupSubNode = new UIChooserNodeGroup(pTargetGroupNode,
                                                                              pTargetGroupNode->nodes().size(),
                                                                              pItem->node()->toGroupNode());
                copiedItems << new UIChooserItemGroup(pTargetGroupItem, pNewGroupSubNode);
                delete pItem->node();
                break;
            }
            case UIChooserNodeType_Machine:
            {
                /* Avoid name collisions: */
                if (busyMachineNames.contains(pItem->name()))
                    break;
                /* Add name to busy: */
                busyMachineNames << pItem->name();
                /* Copy or move machine-item: */
                UIChooserNodeMachine *pNewMachineSubNode = new UIChooserNodeMachine(pTargetGroupNode,
                                                                                    pTargetGroupNode->nodes().size(),
                                                                                    pItem->node()->toMachineNode());
                copiedItems << new UIChooserItemMachine(pTargetGroupItem, pNewMachineSubNode);
                delete pItem->node();
                break;
            }
        }
    }

    /* Update model: */
    wipeOutEmptyGroups();
    updateTreeForMainRoot();

    /* Check if we can select copied items: */
    QList<UIChooserItem*> itemsToSelect;
    foreach (UIChooserItem *pCopiedItem, copiedItems)
        if (navigationItems().contains(pCopiedItem))
            itemsToSelect << pCopiedItem;
    if (!itemsToSelect.isEmpty())
    {
        setSelectedItems(itemsToSelect);
        setCurrentItem(firstSelectedItem());
    }
    else
    {
        /* Otherwise check if we can select one of our parents: */
        UIChooserItem *pItemToSelect = pTargetGroupItem;
        while (   !navigationItems().contains(pItemToSelect)
               && pItemToSelect->parentItem() != root())
            pItemToSelect = pItemToSelect->parentItem();
        if (navigationItems().contains(pItemToSelect))
            setSelectedItem(pItemToSelect);
    }

    /* Save groups finally: */
    saveGroups();
}

void UIChooserModel::startOrShowSelectedItems()
{
    emit sigStartOrShowRequest();
}

void UIChooserModel::refreshSelectedMachineItems()
{
    /* Gather list of current unique inaccessible machine-items: */
    QList<UIChooserItemMachine*> inaccessibleMachineItemList;
    UIChooserItemMachine::enumerateMachineItems(selectedItems(), inaccessibleMachineItemList,
                                                UIChooserItemMachineEnumerationFlag_Unique |
                                                UIChooserItemMachineEnumerationFlag_Inaccessible);

    /* Prepare item to be selected: */
    UIChooserItem *pSelectedItem = 0;

    /* For each machine-item: */
    foreach (UIChooserItemMachine *pItem, inaccessibleMachineItemList)
    {
        AssertPtrReturnVoid(pItem);
        switch (pItem->cacheType())
        {
            case UIVirtualMachineItemType_Local:
            {
                /* Recache: */
                pItem->recache();

                /* Became accessible? */
                if (pItem->accessible())
                {
                    /* Acquire machine ID: */
                    const QUuid uId = pItem->id();
                    /* Reload this machine: */
                    sltReloadMachine(uId);
                    /* Select first of reloaded items: */
                    if (!pSelectedItem)
                        pSelectedItem = root()->searchForItem(uId.toString(),
                                                              UIChooserItemSearchFlag_Machine |
                                                              UIChooserItemSearchFlag_ExactId);
                }

                break;
            }
            case UIVirtualMachineItemType_CloudFake:
            {
                /* Compose cloud entity key: */
                UIChooserItem *pParent = pItem->parentItem();
                AssertPtrReturnVoid(pParent);
                UIChooserItem *pParentOfParent = pParent->parentItem();
                AssertPtrReturnVoid(pParentOfParent);

                /* Create read cloud machine list task: */
                const UICloudEntityKey guiCloudProfileKey = UICloudEntityKey(pParentOfParent->name(), pParent->name());
                createReadCloudMachineListTask(guiCloudProfileKey, true /* with refresh? */);

                break;
            }
            case UIVirtualMachineItemType_CloudReal:
            {
                /* Much more simple than for local items, we are not reloading them, just refreshing: */
                pItem->cache()->toCloud()->updateInfoAsync(false /* delayed */);

                break;
            }
            default:
                break;
        }
    }

    /* Some item to be selected? */
    if (pSelectedItem)
    {
        pSelectedItem->makeSureItsVisible();
        setSelectedItem(pSelectedItem);
    }
}

void UIChooserModel::sortSelectedGroupItem()
{
    /* For single selected group, sort first selected item children: */
    if (isSingleGroupSelected())
        firstSelectedItem()->node()->sortNodes();
    /* Otherwise, sort first selected item neighbors: */
    else
        firstSelectedItem()->parentItem()->node()->sortNodes();

    /* Rebuild tree for main root: */
    buildTreeForMainRoot(true /* preserve selection */);
}

void UIChooserModel::setCurrentMachineItem(const QUuid &uId)
{
    /* Look whether we have such item at all: */
    UIChooserItem *pItem = root()->searchForItem(uId.toString(),
                                                 UIChooserItemSearchFlag_Machine |
                                                 UIChooserItemSearchFlag_ExactId);

    /* Select item if exists: */
    if (pItem)
        setSelectedItem(pItem);
}

void UIChooserModel::setCurrentGlobalItem()
{
    /* Look whether we have such item at all: */
    UIChooserItem *pItem = root()->searchForItem(QString(),
                                                 UIChooserItemSearchFlag_Global);

    /* Select item if exists: */
    if (pItem)
        setSelectedItem(pItem);
}

void UIChooserModel::setCurrentDragObject(QDrag *pDragObject)
{
    /* Make sure real focus unset: */
    clearRealFocus();

    /* Remember new drag-object: */
    m_pCurrentDragObject = pDragObject;
    connect(m_pCurrentDragObject.data(), &QDrag::destroyed,
            this, &UIChooserModel::sltCurrentDragObjectDestroyed);
}

void UIChooserModel::lookFor(const QString &strLookupText)
{
    if (view())
    {
        view()->setSearchWidgetVisible(true);
        view()->appendToSearchString(strLookupText);
    }
}

void UIChooserModel::updateLayout()
{
    /* Sanity check.  This method can be called when invisible root is
     * temporary deleted.  We should ignore request in such case. */
    if (!view() || !root())
        return;

    /* Initialize variables: */
    const QSize viewportSize = view()->size();
    const int iViewportWidth = viewportSize.width();
    const int iViewportHeight = root()->minimumSizeHint().toSize().height();

    /* Move root: */
    root()->setPos(0, 0);
    /* Resize root: */
    root()->resize(iViewportWidth, iViewportHeight);
    /* Layout root content: */
    root()->updateLayout();
}

void UIChooserModel::setGlobalItemHeightHint(int iHint)
{
    /* Save and apply global item height hint: */
    m_iGlobalItemHeightHint = iHint;
    applyGlobalItemHeightHint();
}

void UIChooserModel::sltHandleViewResized()
{
    /* Relayout: */
    updateLayout();

    /* Make current item visible asynchronously: */
    QMetaObject::invokeMethod(this, "sltMakeSureCurrentItemVisible", Qt::QueuedConnection);
}

bool UIChooserModel::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Process only scene events: */
    if (pWatched != scene())
        return QObject::eventFilter(pWatched, pEvent);

    /* Process only item focused by model: */
    if (scene()->focusItem())
        return QObject::eventFilter(pWatched, pEvent);

    /* Checking event-type: */
    switch (pEvent->type())
    {
        /* Keyboard handler: */
        case QEvent::KeyPress:
            return m_pKeyboardHandler->handle(static_cast<QKeyEvent*>(pEvent), UIKeyboardEventType_Press);
        case QEvent::KeyRelease:
            return m_pKeyboardHandler->handle(static_cast<QKeyEvent*>(pEvent), UIKeyboardEventType_Release);
        /* Mouse handler: */
        case QEvent::GraphicsSceneMousePress:
            return m_pMouseHandler->handle(static_cast<QGraphicsSceneMouseEvent*>(pEvent), UIMouseEventType_Press);
        case QEvent::GraphicsSceneMouseRelease:
            return m_pMouseHandler->handle(static_cast<QGraphicsSceneMouseEvent*>(pEvent), UIMouseEventType_Release);
        case QEvent::GraphicsSceneMouseDoubleClick:
            return m_pMouseHandler->handle(static_cast<QGraphicsSceneMouseEvent*>(pEvent), UIMouseEventType_DoubleClick);
        /* Context-menu handler: */
        case QEvent::GraphicsSceneContextMenu:
            return processContextMenuEvent(static_cast<QGraphicsSceneContextMenuEvent*>(pEvent));
        /* Drag&drop scroll-event (drag-move) handler: */
        case QEvent::GraphicsSceneDragMove:
            return processDragMoveEvent(static_cast<QGraphicsSceneDragDropEvent*>(pEvent));
        /* Drag&drop scroll-event (drag-leave) handler: */
        case QEvent::GraphicsSceneDragLeave:
            return processDragLeaveEvent(static_cast<QGraphicsSceneDragDropEvent*>(pEvent));
        default: break; /* Shut up MSC */
    }

    /* Call to base-class: */
    return QObject::eventFilter(pWatched, pEvent);
}

void UIChooserModel::sltLocalMachineRegistrationChanged(const QUuid &uMachineId, const bool fRegistered)
{
    /* Existing VM unregistered => make sure no item with passed uMachineId is selected: */
    if (!fRegistered)
        makeSureNoItemWithCertainIdSelected(uMachineId);

    /* Call to base-class: */
    UIChooserAbstractModel::sltLocalMachineRegistrationChanged(uMachineId, fRegistered);

    /* Existing VM unregistered? */
    if (!fRegistered)
    {
        /* Update tree for main root: */
        updateTreeForMainRoot();
    }
    /* New VM registered? */
    else
    {
        /* Should we show this VM? */
        if (gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
        {
            /* Rebuild tree for main root: */
            buildTreeForMainRoot(true /* preserve selection */);
            /* Search for newly added item: */
            UIChooserItem *pNewItem = root()->searchForItem(uMachineId.toString(),
                                                            UIChooserItemSearchFlag_Machine |
                                                            UIChooserItemSearchFlag_ExactId);
            /* Select newly added item if any: */
            if (pNewItem)
                setSelectedItem(pNewItem);
        }
    }
}

void UIChooserModel::sltHandleCloudProviderUninstall(const QUuid &uProviderId)
{
    /* Call to base-class: */
    UIChooserAbstractModel::sltHandleCloudProviderUninstall(uProviderId);

    /* Notify about selection invalidated: */
    emit sigSelectionInvalidated();
}

void UIChooserModel::sltReloadMachine(const QUuid &uMachineId)
{
    /* Call to base-class: */
    UIChooserAbstractModel::sltReloadMachine(uMachineId);

    /* Should we show this VM? */
    if (gEDataManager->showMachineInVirtualBoxManagerChooser(uMachineId))
    {
        /* Rebuild tree for main root: */
        buildTreeForMainRoot(false /* preserve selection */);
        /* Select newly added item: */
        setSelectedItem(root()->searchForItem(uMachineId.toString(),
                                              UIChooserItemSearchFlag_Machine |
                                              UIChooserItemSearchFlag_ExactId));
    }
    makeSureAtLeastOneItemSelected();

    /* Notify listeners about selection change: */
    emit sigSelectionChanged();
}

void UIChooserModel::sltDetachCOM()
{
    /* Clean tree for main root: */
    clearTreeForMainRoot();
    emit sigSelectionInvalidated();

    /* Call to base-class: */
    UIChooserAbstractModel::sltDetachCOM();
}

void UIChooserModel::sltCloudMachineUnregistered(const QString &strProviderShortName,
                                                 const QString &strProfileName,
                                                 const QUuid &uId)
{
    /* Make sure no item with passed uId is selected: */
    makeSureNoItemWithCertainIdSelected(uId);

    /* Call to base-class: */
    UIChooserAbstractModel::sltCloudMachineUnregistered(strProviderShortName, strProfileName, uId);

    /* Rebuild tree for main root: */
    buildTreeForMainRoot(true /* preserve selection */);
}

void UIChooserModel::sltCloudMachinesUnregistered(const QString &strProviderShortName,
                                                  const QString &strProfileName,
                                                  const QList<QUuid> &ids)
{
    /* Make sure no item with one of passed ids is selected: */
    foreach (const QUuid &uId, ids)
        makeSureNoItemWithCertainIdSelected(uId);

    /* Call to base-class: */
    UIChooserAbstractModel::sltCloudMachinesUnregistered(strProviderShortName, strProfileName, ids);

    /* Rebuild tree for main root: */
    buildTreeForMainRoot(true /* preserve selection */);
}

void UIChooserModel::sltCloudMachineRegistered(const QString &strProviderShortName,
                                               const QString &strProfileName,
                                               const CCloudMachine &comMachine)
{
    /* Call to base-class: */
    UIChooserAbstractModel::sltCloudMachineRegistered(strProviderShortName, strProfileName, comMachine);

    /* Rebuild tree for main root: */
    buildTreeForMainRoot(false /* preserve selection */);

    /* Select newly added item: */
    QUuid uMachineId;
    if (cloudMachineId(comMachine, uMachineId))
        setSelectedItem(root()->searchForItem(uMachineId.toString(),
                                              UIChooserItemSearchFlag_Machine |
                                              UIChooserItemSearchFlag_ExactId));
}

void UIChooserModel::sltCloudMachinesRegistered(const QString &strProviderShortName,
                                                const QString &strProfileName,
                                                const QVector<CCloudMachine> &machines)
{
    /* Call to base-class: */
    UIChooserAbstractModel::sltCloudMachinesRegistered(strProviderShortName, strProfileName, machines);

    /* Rebuild tree for main root: */
    buildTreeForMainRoot(true /* preserve selection */);
}

void UIChooserModel::sltHandleReadCloudMachineListTaskComplete()
{
    /* Call to base-class: */
    UIChooserAbstractModel::sltHandleReadCloudMachineListTaskComplete();

    /* Postpone update since we have just updated: */
    if (m_pTimerCloudProfileUpdate)
        m_pTimerCloudProfileUpdate->start(10000);
}

void UIChooserModel::sltHandleCloudProfileManagerCumulativeChange()
{
    /* Call to base-class: */
    UIChooserAbstractModel::sltHandleCloudProfileManagerCumulativeChange();

    /* Build tree for main root: */
    buildTreeForMainRoot(true /* preserve selection */);
}

void UIChooserModel::sltMakeSureCurrentItemVisible()
{
    root()->toGroupItem()->makeSureItemIsVisible(currentItem());
}

void UIChooserModel::sltCurrentItemDestroyed()
{
    AssertMsgFailed(("Current-item destroyed!"));
}

void UIChooserModel::sltStartScrolling()
{
    /* Make sure view exists: */
    AssertPtrReturnVoid(view());

    /* Should we scroll? */
    if (!m_fIsScrollingInProgress)
        return;

    /* Reset scrolling progress: */
    m_fIsScrollingInProgress = false;

    /* Convert mouse position to view co-ordinates: */
    const QPoint mousePos = view()->mapFromGlobal(QCursor::pos());
    /* Mouse position is at the top of view? */
    if (mousePos.y() < m_iScrollingTokenSize && mousePos.y() > 0)
    {
        int iValue = mousePos.y();
        if (!iValue)
            iValue = 1;
        const int iDelta = m_iScrollingTokenSize / iValue;
        /* Backward scrolling: */
        root()->toGroupItem()->scrollBy(- 2 * iDelta);
        m_fIsScrollingInProgress = true;
        QTimer::singleShot(10, this, SLOT(sltStartScrolling()));
    }
    /* Mouse position is at the bottom of view? */
    else if (mousePos.y() > view()->height() - m_iScrollingTokenSize && mousePos.y() < view()->height())
    {
        int iValue = view()->height() - mousePos.y();
        if (!iValue)
            iValue = 1;
        const int iDelta = m_iScrollingTokenSize / iValue;
        /* Forward scrolling: */
        root()->toGroupItem()->scrollBy(2 * iDelta);
        m_fIsScrollingInProgress = true;
        QTimer::singleShot(10, this, SLOT(sltStartScrolling()));
    }
}

void UIChooserModel::sltCurrentDragObjectDestroyed()
{
    root()->resetDragToken();
}

void UIChooserModel::sltHandleCloudMachineRemoved(const QString &strProviderShortName,
                                                  const QString &strProfileName,
                                                  const QString &strName)
{
    Q_UNUSED(strName);

    /* Update profile to make sure it has no stale instances: */
    const UICloudEntityKey cloudEntityKeyForProfile = UICloudEntityKey(strProviderShortName, strProfileName);
    createReadCloudMachineListTask(cloudEntityKeyForProfile, false /* with refresh? */);
}

void UIChooserModel::sltUpdateSelectedCloudProfiles()
{
    /* Postpone update since we are already working on it: */
    if (m_pTimerCloudProfileUpdate)
        m_pTimerCloudProfileUpdate->start(10000);

    /* Compose a list of items to update: */
    QList<UIChooserItem*> itemsToUpdate;

    /* For the case if requested - just update everything: */
    if (isKeepCloudNodesUpdated())
    {
        /* Search for a list of provider nodes having items: */
        QList<UIChooserNode*> providerNodes;
        invisibleRoot()->searchForNodes(QString(),
                                        UIChooserItemSearchFlag_CloudProvider,
                                        providerNodes);
        foreach (UIChooserNode *pNode, providerNodes)
            if (UIChooserItem *pItem = pNode->item())
                itemsToUpdate << pItem;

        /* Have at least something as a fallback: */
        if (itemsToUpdate.isEmpty())
            itemsToUpdate << selectedItems();
    }
    /* Otherwise update selected items only: */
    else
        itemsToUpdate << selectedItems();

    /* For every selected item: */
    QSet<UICloudEntityKey> selectedCloudProfileKeys;
    foreach (UIChooserItem *pSelectedItem, itemsToUpdate)
    {
        /* Enumerate cloud profile keys to update: */
        switch (pSelectedItem->type())
        {
            case UIChooserNodeType_Group:
            {
                UIChooserItemGroup *pGroupItem = pSelectedItem->toGroupItem();
                AssertPtrReturnVoid(pGroupItem);
                switch (pGroupItem->groupType())
                {
                    case UIChooserNodeGroupType_Provider:
                    {
                        const QString strProviderShortName = pSelectedItem->name();
                        foreach (UIChooserItem *pChildItem, pSelectedItem->items(UIChooserNodeType_Group))
                        {
                            const QString strProfileName = pChildItem->name();
                            const UICloudEntityKey guiCloudProfileKey = UICloudEntityKey(strProviderShortName, strProfileName);
                            if (!selectedCloudProfileKeys.contains(guiCloudProfileKey))
                                selectedCloudProfileKeys.insert(guiCloudProfileKey);
                        }
                        break;
                    }
                    case UIChooserNodeGroupType_Profile:
                    {
                        const QString strProviderShortName = pSelectedItem->parentItem()->name();
                        const QString strProfileName = pSelectedItem->name();
                        const UICloudEntityKey guiCloudProfileKey = UICloudEntityKey(strProviderShortName, strProfileName);
                        if (!selectedCloudProfileKeys.contains(guiCloudProfileKey))
                            selectedCloudProfileKeys.insert(guiCloudProfileKey);
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            case UIChooserNodeType_Machine:
            {
                UIChooserItemMachine *pMachineItem = pSelectedItem->toMachineItem();
                AssertPtrReturnVoid(pMachineItem);
                if (   pMachineItem->cacheType() == UIVirtualMachineItemType_CloudFake
                    || pMachineItem->cacheType() == UIVirtualMachineItemType_CloudReal)
                {
                    const QString strProviderShortName = pMachineItem->parentItem()->parentItem()->name();
                    const QString strProfileName = pMachineItem->parentItem()->name();
                    const UICloudEntityKey guiCloudProfileKey = UICloudEntityKey(strProviderShortName, strProfileName);
                    if (!selectedCloudProfileKeys.contains(guiCloudProfileKey))
                        selectedCloudProfileKeys.insert(guiCloudProfileKey);
                }
                break;
            }
        }
    }

    /* Restart List Cloud Machines task for selected profile keys: */
    foreach (const UICloudEntityKey &guiCloudProfileKey, selectedCloudProfileKeys)
        createReadCloudMachineListTask(guiCloudProfileKey, false /* with refresh? */);
}

void UIChooserModel::prepare()
{
    prepareScene();
    prepareContextMenu();
    prepareHandlers();
    prepareCloudUpdateTimer();
    prepareConnections();
}

void UIChooserModel::prepareScene()
{
    m_pScene = new QGraphicsScene(this);
    if (m_pScene)
        m_pScene->installEventFilter(this);
}

void UIChooserModel::prepareContextMenu()
{
    /* Context menu for global(s): */
    m_localMenus[UIChooserNodeType_Global] = new QMenu;
    if (QMenu *pMenuGlobal = m_localMenus.value(UIChooserNodeType_Global))
    {
#ifdef VBOX_WS_MAC
        pMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_About));
        pMenuGlobal->addSeparator();
        pMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_Preferences));
        pMenuGlobal->addSeparator();
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ImportAppliance));
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ExportAppliance));
# ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ShowExtraDataManager));
        pMenuGlobal->addSeparator();
# endif
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_M_Tools));

#else /* !VBOX_WS_MAC */

        pMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_Preferences));
        pMenuGlobal->addSeparator();
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ImportAppliance));
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ExportAppliance));
        pMenuGlobal->addSeparator();
# ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_S_ShowExtraDataManager));
        pMenuGlobal->addSeparator();
# endif
        pMenuGlobal->addAction(actionPool()->action(UIActionIndexMN_M_File_M_Tools));
        pMenuGlobal->addSeparator();
# ifdef VBOX_GUI_WITH_NETWORK_MANAGER
        if (gEDataManager->applicationUpdateEnabled())
            pMenuGlobal->addAction(actionPool()->action(UIActionIndex_M_Application_S_CheckForUpdates));
# endif
#endif /* !VBOX_WS_MAC */
    }

    /* Context menu for local group(s): */
    m_localMenus[UIChooserNodeType_Group] = new QMenu;
    if (QMenu *pMenuGroup = m_localMenus.value(UIChooserNodeType_Group))
    {
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_New));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Add));
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Rename));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Remove));
        pMenuGroup->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_MoveToGroup)->menu());
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_StartOrShow));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_T_Pause));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Reset));
        // pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Detach));
        pMenuGroup->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Stop)->menu());
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Discard));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Refresh));
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_ShowInFileManager));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_CreateShortcut));
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Sort));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_T_Search));
    }

    /* Context menu for local machine(s): */
    m_localMenus[UIChooserNodeType_Machine] = new QMenu;
    if (QMenu *pMenuMachine = m_localMenus.value(UIChooserNodeType_Machine))
    {
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Clone));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Move));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_ExportToOCI));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Remove));
        pMenuMachine->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_MoveToGroup)->menu());
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_T_Pause));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Reset));
        // pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Detach));
        pMenuMachine->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop)->menu());
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Discard));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh));
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_ShowInFileManager));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_CreateShortcut));
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_SortParent));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_T_Search));
    }

    /* Context menu for cloud group(s): */
    m_cloudMenus[UIChooserNodeType_Group] = new QMenu;
    if (QMenu *pMenuGroup = m_cloudMenus.value(UIChooserNodeType_Group))
    {
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_New));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Add));
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_M_StartOrShow));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Reset));
        pMenuGroup->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Console)->menu());
        pMenuGroup->addMenu(actionPool()->action(UIActionIndexMN_M_Group_M_Stop)->menu());
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Refresh));
        pMenuGroup->addSeparator();
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_S_Sort));
        pMenuGroup->addAction(actionPool()->action(UIActionIndexMN_M_Group_T_Search));
    }

    /* Context menu for cloud machine(s): */
    m_cloudMenus[UIChooserNodeType_Machine] = new QMenu;
    if (QMenu *pMenuMachine = m_cloudMenus.value(UIChooserNodeType_Machine))
    {
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Settings));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Clone));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Remove));
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_M_StartOrShow));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Reset));
        pMenuMachine->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Console)->menu());
        pMenuMachine->addMenu(actionPool()->action(UIActionIndexMN_M_Machine_M_Stop)->menu());
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_Refresh));
        pMenuMachine->addSeparator();
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_S_SortParent));
        pMenuMachine->addAction(actionPool()->action(UIActionIndexMN_M_Machine_T_Search));
    }
}

void UIChooserModel::prepareHandlers()
{
    m_pMouseHandler = new UIChooserHandlerMouse(this);
    m_pKeyboardHandler = new UIChooserHandlerKeyboard(this);
}

void UIChooserModel::prepareCloudUpdateTimer()
{
    m_pTimerCloudProfileUpdate = new QTimer;
    if (m_pTimerCloudProfileUpdate)
        m_pTimerCloudProfileUpdate->start(10000);
}

void UIChooserModel::prepareConnections()
{
    connect(this, &UIChooserModel::sigSelectionChanged,
            this, &UIChooserModel::sltUpdateSelectedCloudProfiles);
    connect(m_pTimerCloudProfileUpdate, &QTimer::timeout,
            this, &UIChooserModel::sltUpdateSelectedCloudProfiles);
}

void UIChooserModel::loadSettings()
{
    /* Load last selected-item (choose first if unable to load): */
    setSelectedItem(gEDataManager->selectorWindowLastItemChosen());
    makeSureAtLeastOneItemSelected();
}

void UIChooserModel::cleanupConnections()
{
    disconnect(this, &UIChooserModel::sigSelectionChanged,
               this, &UIChooserModel::sltUpdateSelectedCloudProfiles);
    disconnect(m_pTimerCloudProfileUpdate, &QTimer::timeout,
               this, &UIChooserModel::sltUpdateSelectedCloudProfiles);
}

void UIChooserModel::cleanupCloudUpdateTimer()
{
    delete m_pTimerCloudProfileUpdate;
    m_pTimerCloudProfileUpdate = 0;
}

void UIChooserModel::cleanupHandlers()
{
    delete m_pKeyboardHandler;
    m_pKeyboardHandler = 0;
    delete m_pMouseHandler;
    m_pMouseHandler = 0;
}

void UIChooserModel::cleanupContextMenu()
{
    qDeleteAll(m_localMenus);
    m_localMenus.clear();
    qDeleteAll(m_cloudMenus);
    m_cloudMenus.clear();
}

void UIChooserModel::cleanupScene()
{
    delete m_pScene;
    m_pScene = 0;
}

void UIChooserModel::cleanup()
{
    cleanupConnections();
    cleanupCloudUpdateTimer();
    cleanupHandlers();
    cleanupContextMenu();
    cleanupScene();
}

bool UIChooserModel::processContextMenuEvent(QGraphicsSceneContextMenuEvent *pEvent)
{
    /* Whats the reason? */
    switch (pEvent->reason())
    {
        case QGraphicsSceneContextMenuEvent::Mouse:
        {
            /* Look for an item under cursor: */
            if (QGraphicsItem *pItem = itemAt(pEvent->scenePos()))
            {
                switch (pItem->type())
                {
                    case UIChooserNodeType_Global:
                    {
                        /* Global context menu for all global item cases: */
                        m_localMenus.value(UIChooserNodeType_Global)->exec(pEvent->screenPos());
                        break;
                    }
                    case UIChooserNodeType_Group:
                    {
                        /* Get group-item: */
                        UIChooserItemGroup *pGroupItem = qgraphicsitem_cast<UIChooserItemGroup*>(pItem);
                        /* Don't show context menu for root-item: */
                        if (pGroupItem->isRoot())
                            break;
                        /* Make sure we have group-item selected exclusively: */
                        if (selectedItems().contains(pGroupItem) && selectedItems().size() == 1)
                        {
                            /* Group context menu in that case: */
                            if (pGroupItem->groupType() == UIChooserNodeGroupType_Local)
                                m_localMenus.value(UIChooserNodeType_Group)->exec(pEvent->screenPos());
                            else if (   pGroupItem->groupType() == UIChooserNodeGroupType_Provider
                                     || pGroupItem->groupType() == UIChooserNodeGroupType_Profile)
                                m_cloudMenus.value(UIChooserNodeType_Group)->exec(pEvent->screenPos());
                            break;
                        }
                        /* Otherwise we have to find a first child machine-item: */
                        else
                            pItem = qobject_cast<UIChooserItem*>(pGroupItem)->firstMachineItem();
                    }
                    RT_FALL_THRU();
                    case UIChooserNodeType_Machine:
                    {
                        /* Get machine-item: */
                        UIChooserItemMachine *pMachineItem = qgraphicsitem_cast<UIChooserItemMachine*>(pItem);
                        /* Machine context menu for other Group/Machine cases: */
                        if (pMachineItem->cacheType() == UIVirtualMachineItemType_Local)
                            m_localMenus.value(UIChooserNodeType_Machine)->exec(pEvent->screenPos());
                        else if (pMachineItem->cacheType() == UIVirtualMachineItemType_CloudReal)
                            m_cloudMenus.value(UIChooserNodeType_Machine)->exec(pEvent->screenPos());
                        break;
                    }
                    default:
                        break;
                }
            }
            /* Filter out by default: */
            return true;
        }
        case QGraphicsSceneContextMenuEvent::Keyboard:
        {
            /* Get first selected-item: */
            if (UIChooserItem *pItem = firstSelectedItem())
            {
                switch (pItem->type())
                {
                    case UIChooserNodeType_Global:
                    {
                        /* Global context menu for all global item cases: */
                        m_localMenus.value(UIChooserNodeType_Global)->exec(pEvent->screenPos());
                        break;
                    }
                    case UIChooserNodeType_Group:
                    {
                        /* Get group-item: */
                        UIChooserItemGroup *pGroupItem = qgraphicsitem_cast<UIChooserItemGroup*>(pItem);
                        /* Make sure we have group-item selected exclusively: */
                        if (selectedItems().contains(pGroupItem) && selectedItems().size() == 1)
                        {
                            /* Group context menu in that case: */
                            if (pGroupItem->groupType() == UIChooserNodeGroupType_Local)
                                m_localMenus.value(UIChooserNodeType_Group)->exec(pEvent->screenPos());
                            else if (   pGroupItem->groupType() == UIChooserNodeGroupType_Provider
                                     || pGroupItem->groupType() == UIChooserNodeGroupType_Profile)
                                m_cloudMenus.value(UIChooserNodeType_Group)->exec(pEvent->screenPos());
                            break;
                        }
                        /* Otherwise we have to find a first child machine-item: */
                        else
                            pItem = qobject_cast<UIChooserItem*>(pGroupItem)->firstMachineItem();
                    }
                    RT_FALL_THRU();
                    case UIChooserNodeType_Machine:
                    {
                        /* Get machine-item: */
                        UIChooserItemMachine *pMachineItem = qgraphicsitem_cast<UIChooserItemMachine*>(pItem);
                        /* Machine context menu for other Group/Machine cases: */
                        if (pMachineItem->cacheType() == UIVirtualMachineItemType_Local)
                            m_localMenus.value(UIChooserNodeType_Machine)->exec(pEvent->screenPos());
                        else if (pMachineItem->cacheType() == UIVirtualMachineItemType_CloudReal)
                            m_cloudMenus.value(UIChooserNodeType_Machine)->exec(pEvent->screenPos());
                        break;
                    }
                    default:
                        break;
                }
            }
            /* Filter out by default: */
            return true;
        }
        default:
            break;
    }
    /* Pass others context menu events: */
    return false;
}

void UIChooserModel::clearRealFocus()
{
    /* Set the real focus to null: */
    scene()->setFocusItem(0);
}

QList<UIChooserItem*> UIChooserModel::createNavigationItemList(UIChooserItem *pItem)
{
    /* Prepare navigation list: */
    QList<UIChooserItem*> navigationItems;

    /* Iterate over all the global-items: */
    foreach (UIChooserItem *pGlobalItem, pItem->items(UIChooserNodeType_Global))
        navigationItems << pGlobalItem;
    /* Iterate over all the group-items: */
    foreach (UIChooserItem *pGroupItem, pItem->items(UIChooserNodeType_Group))
    {
        navigationItems << pGroupItem;
        if (pGroupItem->toGroupItem()->isOpened())
            navigationItems << createNavigationItemList(pGroupItem);
    }
    /* Iterate over all the machine-items: */
    foreach (UIChooserItem *pMachineItem, pItem->items(UIChooserNodeType_Machine))
        navigationItems << pMachineItem;

    /* Return navigation list: */
    return navigationItems;
}

void UIChooserModel::clearTreeForMainRoot()
{
    /* Forbid to save selection changes: */
    m_fSelectionSaveAllowed = false;

    /* Cleanup tree if exists: */
    delete m_pRoot;
    m_pRoot = 0;
}

void UIChooserModel::buildTreeForMainRoot(bool fPreserveSelection /* = false */)
{
    /* This isn't safe if dragging is started and needs to be fixed properly,
     * but for now we will just ignore build request: */
    /// @todo Make sure D&D is safe on tree rebuild
    if (m_pCurrentDragObject)
        return;

    /* Remember scrolling location: */
    const int iScrollLocation = m_pRoot ? m_pRoot->toGroupItem()->scrollingValue() : 0;

    /* Remember all selected items if requested: */
    QStringList selectedItemDefinitions;
    if (fPreserveSelection && !selectedItems().isEmpty())
    {
        foreach (UIChooserItem *pSelectedItem, selectedItems())
            selectedItemDefinitions << pSelectedItem->definition();
    }

    /* Clean tree for main root: */
    clearTreeForMainRoot();

    /* Build whole tree for invisible root item: */
    m_pRoot = new UIChooserItemGroup(scene(), invisibleRoot()->toGroupNode());

    /* Install root as event-filter for scene view,
     * we need QEvent::Scroll events from it: */
    root()->installEventFilterHelper(view());

    /* Update tree for main root: */
    updateTreeForMainRoot();

    /* Apply current global item height hint: */
    applyGlobalItemHeightHint();

    /* Restore all selected items if requested: */
    if (fPreserveSelection)
    {
        QList<UIChooserItem*> selectedItems;
        foreach (const QString &strSelectedItemDefinition, selectedItemDefinitions)
        {
            UIChooserItem *pSelectedItem = searchItemByDefinition(strSelectedItemDefinition);
            if (pSelectedItem)
                selectedItems << pSelectedItem;
        }
        setSelectedItems(selectedItems);
        setCurrentItem(firstSelectedItem());
        makeSureAtLeastOneItemSelected();
    }

    /* Restore scrolling location: */
    m_pRoot->toGroupItem()->setScrollingValue(iScrollLocation);

    /* Repeat search if search widget is visible: */
    if (view() && view()->isSearchWidgetVisible())
        view()->redoSearch();

    /* Allow to save selection changes: */
    m_fSelectionSaveAllowed = true;
}

void UIChooserModel::updateTreeForMainRoot()
{
    updateNavigationItemList();
    updateLayout();
}

void UIChooserModel::removeLocalMachineItems(const QList<UIChooserItemMachine*> &machineItems)
{
    /* Confirm machine-items removal: */
    QStringList names;
    foreach (UIChooserItemMachine *pItem, machineItems)
        names << pItem->name();
    if (!msgCenter().confirmMachineItemRemoval(names))
        return;

    /* Find and select closest unselected item: */
    setSelectedItem(findClosestUnselectedItem());

    /* Remove nodes of all the passed items: */
    foreach (UIChooserItemMachine *pItem, machineItems)
        delete pItem->node();

    /* And update model: */
    wipeOutEmptyGroups();
    updateTreeForMainRoot();

    /* Save groups finally: */
    saveGroups();
}

void UIChooserModel::unregisterLocalMachines(const QList<CMachine> &machines)
{
    /* Confirm machine removal: */
    const int iResultCode = msgCenter().confirmMachineRemoval(machines);
    if (iResultCode == AlertButton_Cancel)
        return;

    /* For every selected machine: */
    foreach (CMachine comMachine, machines)
    {
        if (iResultCode == AlertButton_Choice1)
        {
            /* Unregister machine first: */
            CMediumVector media = comMachine.Unregister(KCleanupMode_DetachAllReturnHardDisksAndVMRemovable);
            if (!comMachine.isOk())
            {
                UINotificationMessage::cannotRemoveMachine(comMachine);
                continue;
            }
            /* Removing machine: */
            UINotificationProgressMachineMediaRemove *pNotification = new UINotificationProgressMachineMediaRemove(comMachine, media);
            gpNotificationCenter->append(pNotification);
        }
        else if (iResultCode == AlertButton_Choice2 || iResultCode == AlertButton_Ok)
        {
            /* Unregister machine first: */
            CMediumVector media = comMachine.Unregister(KCleanupMode_DetachAllReturnHardDisksAndVMRemovable);
            if (!comMachine.isOk())
            {
                UINotificationMessage::cannotRemoveMachine(comMachine);
                continue;
            }
            /* Finally close all media, deliberately ignoring errors: */
            foreach (CMedium comMedium, media)
            {
                if (!comMedium.isNull())
                    comMedium.Close();
            }
        }
    }
}

void UIChooserModel::unregisterCloudMachineItems(const QList<UIChooserItemMachine*> &machineItems)
{
    /* Compose a list of machines: */
    QList<CCloudMachine> machines;
    foreach (UIChooserItemMachine *pMachineItem, machineItems)
        machines << pMachineItem->cache()->toCloud()->machine();

    /* Stop cloud profile update prematurely: */
    if (m_pTimerCloudProfileUpdate)
        m_pTimerCloudProfileUpdate->stop();

    /* Confirm machine removal: */
    const int iResultCode = msgCenter().confirmCloudMachineRemoval(machines);
    if (iResultCode == AlertButton_Cancel)
    {
        /* Resume cloud profile update if cancelled: */
        if (m_pTimerCloudProfileUpdate)
            m_pTimerCloudProfileUpdate->start(10000);
        return;
    }

    /* For every selected machine-item: */
    foreach (UIChooserItemMachine *pMachineItem, machineItems)
    {
        /* Compose cloud entity keys for profile and machine: */
        const QString strProviderShortName = pMachineItem->parentItem()->parentItem()->name();
        const QString strProfileName = pMachineItem->parentItem()->name();
        const QUuid uMachineId = pMachineItem->id();
        const UICloudEntityKey cloudEntityKeyForMachine = UICloudEntityKey(strProviderShortName, strProfileName, uMachineId);

        /* Stop refreshing machine being deleted: */
        if (containsCloudEntityKey(cloudEntityKeyForMachine))
            pMachineItem->cache()->toCloud()->waitForAsyncInfoUpdateFinished();

        /* Acquire cloud machine: */
        CCloudMachine comMachine = pMachineItem->cache()->toCloud()->machine();

        /* Removing cloud machine: */
        UINotificationProgressCloudMachineRemove *pNotification =
            new UINotificationProgressCloudMachineRemove(comMachine,
                                                         iResultCode == AlertButton_Choice1,
                                                         strProviderShortName,
                                                         strProfileName);
        connect(pNotification, &UINotificationProgressCloudMachineRemove::sigCloudMachineRemoved,
                this, &UIChooserModel::sltHandleCloudMachineRemoved);
        gpNotificationCenter->append(pNotification);
    }

    /* Resume cloud profile update after all: */
    if (m_pTimerCloudProfileUpdate)
        m_pTimerCloudProfileUpdate->start(10000);
}

bool UIChooserModel::processDragMoveEvent(QGraphicsSceneDragDropEvent *pEvent)
{
    /* Make sure view exists: */
    AssertPtrReturn(view(), false);

    /* Do we scrolling already? */
    if (m_fIsScrollingInProgress)
        return false;

    /* Check scroll-area: */
    const QPoint eventPoint = view()->mapFromGlobal(pEvent->screenPos());
    if (   (eventPoint.y() < m_iScrollingTokenSize)
        || (eventPoint.y() > view()->height() - m_iScrollingTokenSize))
    {
        /* Set scrolling in progress: */
        m_fIsScrollingInProgress = true;
        /* Start scrolling: */
        QTimer::singleShot(200, this, SLOT(sltStartScrolling()));
    }

    /* Pass event: */
    return false;
}

bool UIChooserModel::processDragLeaveEvent(QGraphicsSceneDragDropEvent *pEvent)
{
    /* Event object is not required here: */
    Q_UNUSED(pEvent);

    /* Make sure to stop scrolling as drag-leave event happened: */
    if (m_fIsScrollingInProgress)
        m_fIsScrollingInProgress = false;

    /* Pass event: */
    return false;
}

void UIChooserModel::applyGlobalItemHeightHint()
{
    /* Make sure there is something to apply: */
    if (m_iGlobalItemHeightHint == 0)
        return;

    /* Walk thrugh all the items of navigation list: */
    foreach (UIChooserItem *pItem, navigationItems())
    {
        /* And for each global item: */
        if (pItem->type() == UIChooserNodeType_Global)
        {
            /* Apply the height hint we have: */
            UIChooserItemGlobal *pGlobalItem = pItem->toGlobalItem();
            if (pGlobalItem)
                pGlobalItem->setHeightHint(m_iGlobalItemHeightHint);
        }
    }
}
