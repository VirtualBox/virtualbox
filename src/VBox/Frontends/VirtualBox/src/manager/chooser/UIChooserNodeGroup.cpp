/* $Id: UIChooserNodeGroup.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserNodeGroup class implementation.
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

/* GUI includes: */
#include "UIChooserAbstractModel.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeGlobal.h"
#include "UIChooserNodeMachine.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIChooserNodeGroup::UIChooserNodeGroup(UIChooserNode *pParent,
                                       int iPosition,
                                       const QUuid &uId,
                                       const QString &strName,
                                       UIChooserNodeGroupType enmGroupType,
                                       bool fOpened)
    : UIChooserNode(pParent, false /* favorite */)
    , m_uId(uId)
    , m_strName(strName)
    , m_enmGroupType(enmGroupType)
    , m_fOpened(fOpened)
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeGroup::sltRetranslateUI);
}

UIChooserNodeGroup::UIChooserNodeGroup(UIChooserNode *pParent,
                                       int iPosition,
                                       UIChooserNodeGroup *pCopyFrom)
    : UIChooserNode(pParent, false /* favorite */)
    , m_uId(pCopyFrom->id())
    , m_strName(pCopyFrom->name())
    , m_enmGroupType(pCopyFrom->groupType())
    , m_fOpened(pCopyFrom->isOpened())
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Copy internal stuff: */
    copyContents(pCopyFrom);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeGroup::sltRetranslateUI);
}

UIChooserNodeGroup::~UIChooserNodeGroup()
{
    /* Cleanup groups first, that
     * gives us proper recursion: */
    while (!m_nodesGroup.isEmpty())
        delete m_nodesGroup.last();
    while (!m_nodesGlobal.isEmpty())
        delete m_nodesGlobal.last();
    while (!m_nodesMachine.isEmpty())
        delete m_nodesMachine.last();

    /* Delete item: */
    delete item();

    /* Remove from parent: */
    if (parentNode())
        parentNode()->removeNode(this);
}

QString UIChooserNodeGroup::name() const
{
    return m_strName;
}

QString UIChooserNodeGroup::fullName() const
{
    /* Return "/" for root item: */
    if (isRoot())
        return "/";
    /* Get full parent name, append with '/' if not yet appended: */
    QString strFullParentName = parentNode()->fullName();
    if (!strFullParentName.endsWith('/'))
        strFullParentName.append('/');
    /* Return full item name based on parent prefix: */
    return strFullParentName + name();
}

QString UIChooserNodeGroup::description() const
{
    return name();
}

QString UIChooserNodeGroup::definition(bool fFull /* = false */) const
{
    QString strNodePrefix;
    switch (groupType())
    {
        case UIChooserNodeGroupType_Local:
            strNodePrefix = UIChooserAbstractModel::prefixToString(UIChooserNodeDataPrefixType_Local);
            break;
        case UIChooserNodeGroupType_Provider:
            strNodePrefix = UIChooserAbstractModel::prefixToString(UIChooserNodeDataPrefixType_Provider);
            break;
        case UIChooserNodeGroupType_Profile:
            strNodePrefix = UIChooserAbstractModel::prefixToString(UIChooserNodeDataPrefixType_Profile);
            break;
        default:
            AssertFailedReturn(QString());
    }
    const QString strNodeOptionOpened = UIChooserAbstractModel::optionToString(UIChooserNodeDataOptionType_GroupOpened);
    return   fFull
           ? QString("%1%2=%3").arg(strNodePrefix).arg(isOpened() ? strNodeOptionOpened : "").arg(name())
           : QString("%1=%2").arg(strNodePrefix).arg(fullName());
}

bool UIChooserNodeGroup::hasNodes(UIChooserNodeType enmType /* = UIChooserNodeType_Any */) const
{
    switch (enmType)
    {
        case UIChooserNodeType_Any:
            return hasNodes(UIChooserNodeType_Group) || hasNodes(UIChooserNodeType_Global) || hasNodes(UIChooserNodeType_Machine);
        case UIChooserNodeType_Group:
            return !m_nodesGroup.isEmpty();
        case UIChooserNodeType_Global:
            return !m_nodesGlobal.isEmpty();
        case UIChooserNodeType_Machine:
            return !m_nodesMachine.isEmpty();
    }
    return false;
}

QList<UIChooserNode*> UIChooserNodeGroup::nodes(UIChooserNodeType enmType /* = UIChooserNodeType_Any */) const
{
    switch (enmType)
    {
        case UIChooserNodeType_Any:     return m_nodesGlobal + m_nodesGroup + m_nodesMachine;
        case UIChooserNodeType_Group:   return m_nodesGroup;
        case UIChooserNodeType_Global:  return m_nodesGlobal;
        case UIChooserNodeType_Machine: return m_nodesMachine;
    }
    AssertFailedReturn(QList<UIChooserNode*>());
}

void UIChooserNodeGroup::addNode(UIChooserNode *pNode, int iPosition)
{
    switch (pNode->type())
    {
        case UIChooserNodeType_Group:   m_nodesGroup.insert(iPosition < 0 || iPosition > m_nodesGroup.size() ? m_nodesGroup.size() : iPosition, pNode); return;
        case UIChooserNodeType_Global:  m_nodesGlobal.insert(iPosition < 0 || iPosition > m_nodesGlobal.size() ? m_nodesGlobal.size() : iPosition, pNode); return;
        case UIChooserNodeType_Machine: m_nodesMachine.insert(iPosition < 0 || iPosition > m_nodesMachine.size() ? m_nodesMachine.size() : iPosition, pNode); return;
        default: break;
    }
    AssertFailedReturnVoid();
}

void UIChooserNodeGroup::removeNode(UIChooserNode *pNode)
{
    switch (pNode->type())
    {
        case UIChooserNodeType_Group:   m_nodesGroup.removeAll(pNode); return;
        case UIChooserNodeType_Global:  m_nodesGlobal.removeAll(pNode); return;
        case UIChooserNodeType_Machine: m_nodesMachine.removeAll(pNode); return;
        default: break;
    }
    AssertFailedReturnVoid();
}

void UIChooserNodeGroup::removeAllNodes(const QUuid &uId)
{
    foreach (UIChooserNode *pNode, nodes())
        pNode->removeAllNodes(uId);
}

void UIChooserNodeGroup::updateAllNodes(const QUuid &uId)
{
    // Nothing to update for group-node..

    /* Update group-item: */
    item()->updateItem();

    /* Update all the children recursively: */
    foreach (UIChooserNode *pNode, nodes())
        pNode->updateAllNodes(uId);
}

int UIChooserNodeGroup::positionOf(UIChooserNode *pNode)
{
    switch (pNode->type())
    {
        case UIChooserNodeType_Group:   return m_nodesGroup.indexOf(pNode->toGroupNode());
        case UIChooserNodeType_Global:  return m_nodesGlobal.indexOf(pNode->toGlobalNode());
        case UIChooserNodeType_Machine: return m_nodesMachine.indexOf(pNode->toMachineNode());
        default: break;
    }
    AssertFailedReturn(0);
}

void UIChooserNodeGroup::setName(const QString &strName)
{
    /* Make sure something changed: */
    if (m_strName == strName)
        return;

    /* Save name: */
    m_strName = strName;

    /* Update group-item: */
    if (item())
        item()->updateItem();
}

void UIChooserNodeGroup::searchForNodes(const QString &strSearchTerm, int iSearchFlags, QList<UIChooserNode*> &matchedItems)
{
    /* If we are searching for the group-node: */
    if (   (   iSearchFlags & UIChooserItemSearchFlag_LocalGroup
            && groupType() == UIChooserNodeGroupType_Local)
        || (   iSearchFlags & UIChooserItemSearchFlag_CloudProvider
            && groupType() == UIChooserNodeGroupType_Provider)
        || (   iSearchFlags & UIChooserItemSearchFlag_CloudProfile
            && groupType() == UIChooserNodeGroupType_Profile))
    {
        /* If the search term is empty we just add the node to the matched list: */
        if (strSearchTerm.isEmpty())
            matchedItems << this;
        else
        {
            /* If exact ID flag specified => check node ID: */
            if (iSearchFlags & UIChooserItemSearchFlag_ExactId)
            {
                if (id().toString() == strSearchTerm)
                    matchedItems << this;
            }
            /* If exact name flag specified => check node name: */
            else if (iSearchFlags & UIChooserItemSearchFlag_ExactName)
            {
                if (name() == strSearchTerm)
                    matchedItems << this;
            }
            /* If full name flag specified => check full node name: */
            else if (iSearchFlags & UIChooserItemSearchFlag_FullName)
            {
                if (fullName() == strSearchTerm)
                    matchedItems << this;
            }
            /* Otherwise check if name contains search term: */
            else
            {
                if (name().contains(strSearchTerm, Qt::CaseInsensitive))
                    matchedItems << this;
            }
        }
    }

    /* Search among all the children: */
    foreach (UIChooserNode *pNode, m_nodesGroup)
        pNode->searchForNodes(strSearchTerm, iSearchFlags, matchedItems);
    foreach (UIChooserNode *pNode, m_nodesGlobal)
        pNode->searchForNodes(strSearchTerm, iSearchFlags, matchedItems);
    foreach (UIChooserNode *pNode, m_nodesMachine)
        pNode->searchForNodes(strSearchTerm, iSearchFlags, matchedItems);
}

void UIChooserNodeGroup::sortNodes()
{
    QMap<QString, UIChooserNode*> mapGroup;
    foreach (UIChooserNode *pNode, m_nodesGroup)
        mapGroup[pNode->name()] = pNode;
    m_nodesGroup = mapGroup.values();

    QMap<QString, UIChooserNode*> mapGlobal;
    foreach (UIChooserNode *pNode, m_nodesGlobal)
        mapGlobal[pNode->name()] = pNode;
    m_nodesGlobal = mapGlobal.values();

    QMap<QString, UIChooserNode*> mapMachine;
    foreach (UIChooserNode *pNode, m_nodesMachine)
        mapMachine[pNode->name()] = pNode;
    m_nodesMachine = mapMachine.values();
}

QUuid UIChooserNodeGroup::id() const
{
    return m_uId;
}

void UIChooserNodeGroup::sltRetranslateUI()
{
    /* Update group-item: */
    if (item())
        item()->updateItem();
}

void UIChooserNodeGroup::copyContents(UIChooserNodeGroup *pCopyFrom)
{
    foreach (UIChooserNode *pNode, pCopyFrom->nodes(UIChooserNodeType_Group))
        new UIChooserNodeGroup(this, m_nodesGroup.size(), pNode->toGroupNode());
    foreach (UIChooserNode *pNode, pCopyFrom->nodes(UIChooserNodeType_Global))
        new UIChooserNodeGlobal(this, m_nodesGlobal.size(), pNode->toGlobalNode());
    foreach (UIChooserNode *pNode, pCopyFrom->nodes(UIChooserNodeType_Machine))
        new UIChooserNodeMachine(this, m_nodesMachine.size(), pNode->toMachineNode());
}
