/* $Id: UIChooserNodeMachine.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserNodeMachine class implementation.
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

/* GUI includes: */
#include "UIChooserAbstractModel.h"
#include "UIChooserNodeMachine.h"
#include "UITranslationEventListener.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"


UIChooserNodeMachine::UIChooserNodeMachine(UIChooserNode *pParent,
                                           int iPosition,
                                           const CMachine &comMachine)
    : UIChooserNode(pParent, false /* favorite */)
    , m_pCache(new UIVirtualMachineItemLocal(comMachine))
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeMachine::sltRetranslateUI);
}

UIChooserNodeMachine::UIChooserNodeMachine(UIChooserNode *pParent,
                                           int iPosition,
                                           const CCloudMachine &comCloudMachine)
    : UIChooserNode(pParent, false /* favorite */)
    , m_pCache(new UIVirtualMachineItemCloud(comCloudMachine))
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Cloud VM item can notify machine node only directly (no console), we have to setup listener: */
    connect(static_cast<UIVirtualMachineItemCloud*>(m_pCache), &UIVirtualMachineItemCloud::sigRefreshFinished,
            this, &UIChooserNodeMachine::sltHandleStateChange);
    connect(static_cast<UIVirtualMachineItemCloud*>(m_pCache), &UIVirtualMachineItemCloud::sigRefreshStarted,
            static_cast<UIChooserAbstractModel*>(model()), &UIChooserAbstractModel::sltHandleCloudMachineRefreshStarted);
    connect(static_cast<UIVirtualMachineItemCloud*>(m_pCache), &UIVirtualMachineItemCloud::sigRefreshFinished,
            static_cast<UIChooserAbstractModel*>(model()), &UIChooserAbstractModel::sltHandleCloudMachineRefreshFinished);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeMachine::sltRetranslateUI);
}

UIChooserNodeMachine::UIChooserNodeMachine(UIChooserNode *pParent,
                                           int iPosition,
                                           UIFakeCloudVirtualMachineItemState enmState)
    : UIChooserNode(pParent, false /* favorite */)
    , m_pCache(new UIVirtualMachineItemCloud(enmState))
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeMachine::sltRetranslateUI);
}

UIChooserNodeMachine::UIChooserNodeMachine(UIChooserNode *pParent,
                                           int iPosition,
                                           UIChooserNodeMachine *pCopyFrom)
    : UIChooserNode(pParent, pCopyFrom->isFavorite())
{
    /* Prepare cache of corresponding type: */
    switch (pCopyFrom->cacheType())
    {
        case UIVirtualMachineItemType_Local:
            m_pCache = new UIVirtualMachineItemLocal(pCopyFrom->cache()->toLocal()->machine());
            break;
        case UIVirtualMachineItemType_CloudFake:
            m_pCache = new UIVirtualMachineItemCloud(pCopyFrom->cache()->toCloud()->fakeCloudItemState());
            break;
        case UIVirtualMachineItemType_CloudReal:
            m_pCache = new UIVirtualMachineItemCloud(pCopyFrom->cache()->toCloud()->machine());
            break;
        default:
            break;
    }

    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeMachine::sltRetranslateUI);
}

UIChooserNodeMachine::~UIChooserNodeMachine()
{
    /* Delete item: */
    delete item();

    /* Remove from parent: */
    if (parentNode())
        parentNode()->removeNode(this);

    /* Cleanup cache: */
    delete m_pCache;
}

QString UIChooserNodeMachine::name() const
{
    return m_pCache->name();
}

QString UIChooserNodeMachine::fullName() const
{
    /* Get full parent name, append with '/' if not yet appended: */
    AssertReturn(parentNode(), name());
    QString strFullParentName = parentNode()->fullName();
    if (!strFullParentName.endsWith('/'))
        strFullParentName.append('/');
    /* Return full item name based on parent prefix: */
    return strFullParentName + name();
}

QString UIChooserNodeMachine::description() const
{
    return m_strDescription;
}

QString UIChooserNodeMachine::definition(bool) const
{
    const QString strNodePrefix = UIChooserAbstractModel::prefixToString(UIChooserNodeDataPrefixType_Machine);
    return QString("%1=%2").arg(strNodePrefix).arg(UIChooserAbstractModel::toOldStyleUuid(id()));
}

bool UIChooserNodeMachine::hasNodes(UIChooserNodeType enmType /* = UIChooserNodeType_Any */) const
{
    Q_UNUSED(enmType);
    AssertFailedReturn(false);
}

QList<UIChooserNode*> UIChooserNodeMachine::nodes(UIChooserNodeType enmType /* = UIChooserNodeType_Any */) const
{
    Q_UNUSED(enmType);
    AssertFailedReturn(QList<UIChooserNode*>());
}

void UIChooserNodeMachine::addNode(UIChooserNode *pNode, int iPosition)
{
    Q_UNUSED(pNode);
    Q_UNUSED(iPosition);
    AssertFailedReturnVoid();
}

void UIChooserNodeMachine::removeNode(UIChooserNode *pNode)
{
    Q_UNUSED(pNode);
    AssertFailedReturnVoid();
}

void UIChooserNodeMachine::removeAllNodes(const QUuid &uId)
{
    /* Skip other ids: */
    if (id() != uId)
        return;

    /* Remove this node: */
    delete this;
}

void UIChooserNodeMachine::updateAllNodes(const QUuid &uId)
{
    /* Skip other ids: */
    if (id() != uId)
        return;

    /* Update cache: */
    m_pCache->recache();

    /* Update machine-item: */
    if (item())
        item()->updateItem();
}

int UIChooserNodeMachine::positionOf(UIChooserNode *pNode)
{
    Q_UNUSED(pNode);
    AssertFailedReturn(0);
}

void UIChooserNodeMachine::searchForNodes(const QString &strSearchTerm, int iSearchFlags, QList<UIChooserNode*> &matchedItems)
{
    /* Ignore if we are not searching for the machine-node: */
    if (!(iSearchFlags & UIChooserItemSearchFlag_Machine))
        return;

    /* If the search term is empty we just add the node to the matched list: */
    if (strSearchTerm.isEmpty())
        matchedItems << this;
    else
    {
        /* If exact ID flag specified => check node ID:  */
        if (iSearchFlags & UIChooserItemSearchFlag_ExactId)
        {
            if (id() == QUuid(strSearchTerm))
                matchedItems << this;
        }
        /* If exact name flag specified => check full node name: */
        else if (iSearchFlags & UIChooserItemSearchFlag_ExactName)
        {
            if (name() == strSearchTerm)
                matchedItems << this;
        }
        /* Otherwise check if name contains search term, including wildcards: */
        else
        {
            QRegularExpression searchRegEx = QRegularExpression::fromWildcard(strSearchTerm, Qt::CaseInsensitive,
                                                                              QRegularExpression::UnanchoredWildcardConversion);
            if (name().contains(searchRegEx))
                matchedItems << this;
        }
    }
}

void UIChooserNodeMachine::sortNodes()
{
    AssertFailedReturnVoid();
}

UIVirtualMachineItem *UIChooserNodeMachine::cache() const
{
    return m_pCache;
}

UIVirtualMachineItemType UIChooserNodeMachine::cacheType() const
{
    return cache() ? cache()->itemType() : UIVirtualMachineItemType_Invalid;
}

QUuid UIChooserNodeMachine::id() const
{
    return cache() ? cache()->id() : QUuid();
}

bool UIChooserNodeMachine::accessible() const
{
    return cache() ? cache()->accessible() : false;
}

void UIChooserNodeMachine::sltRetranslateUI()
{
    /* Update internal stuff: */
    m_strDescription = tr("Virtual Machine");

    /* Update machine-item: */
    if (item())
        item()->updateItem();
}

void UIChooserNodeMachine::sltHandleStateChange()
{
    /* Update machine-item: */
    if (item())
        item()->updateItem();
}
