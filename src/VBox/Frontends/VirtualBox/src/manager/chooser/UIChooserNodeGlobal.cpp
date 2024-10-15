/* $Id: UIChooserNodeGlobal.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserNodeGlobal class implementation.
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
#include "UIChooserNodeGlobal.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIChooserNodeGlobal::UIChooserNodeGlobal(UIChooserNode *pParent,
                                         int iPosition,
                                         bool fFavorite,
                                         const QString &)
    : UIChooserNode(pParent, fFavorite)
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeGlobal::sltRetranslateUI);
}

UIChooserNodeGlobal::UIChooserNodeGlobal(UIChooserNode *pParent,
                                         int iPosition,
                                         UIChooserNodeGlobal *pCopyFrom)
    : UIChooserNode(pParent, pCopyFrom->isFavorite())
{
    /* Add to parent: */
    if (parentNode())
        parentNode()->addNode(this, iPosition);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserNodeGlobal::sltRetranslateUI);
}

UIChooserNodeGlobal::~UIChooserNodeGlobal()
{
    /* Delete item: */
    delete item();

    /* Remove from parent: */
    if (parentNode())
        parentNode()->removeNode(this);
}

QString UIChooserNodeGlobal::name() const
{
    return m_strName;
}

QString UIChooserNodeGlobal::fullName() const
{
    return name();
}

QString UIChooserNodeGlobal::description() const
{
    return m_strDescription;
}

QString UIChooserNodeGlobal::definition(bool fFull /* = false */) const
{
    const QString strNodePrefix = UIChooserAbstractModel::prefixToString(UIChooserNodeDataPrefixType_Global);
    const QString strNodeOptionFavorite = UIChooserAbstractModel::optionToString(UIChooserNodeDataOptionType_GlobalFavorite);
    const QString strNodeValueDefault = UIChooserAbstractModel::valueToString(UIChooserNodeDataValueType_GlobalDefault);
    return   fFull
           ? QString("%1%2=%3").arg(strNodePrefix).arg(isFavorite() ? strNodeOptionFavorite : "").arg(strNodeValueDefault)
           : QString("%1=%2").arg(strNodePrefix).arg(strNodeValueDefault);
}

bool UIChooserNodeGlobal::hasNodes(UIChooserNodeType enmType /* = UIChooserNodeType_Any */) const
{
    Q_UNUSED(enmType);
    AssertFailedReturn(false);
}

QList<UIChooserNode*> UIChooserNodeGlobal::nodes(UIChooserNodeType enmType /* = UIChooserNodeType_Any */) const
{
    Q_UNUSED(enmType);
    AssertFailedReturn(QList<UIChooserNode*>());
}

void UIChooserNodeGlobal::addNode(UIChooserNode *pNode, int iPosition)
{
    Q_UNUSED(pNode);
    Q_UNUSED(iPosition);
    AssertFailedReturnVoid();
}

void UIChooserNodeGlobal::removeNode(UIChooserNode *pNode)
{
    Q_UNUSED(pNode);
    AssertFailedReturnVoid();
}

void UIChooserNodeGlobal::removeAllNodes(const QUuid &)
{
    // Nothing to remove for global-node..
}

void UIChooserNodeGlobal::updateAllNodes(const QUuid &)
{
    // Nothing to update for global-node..

    /* Update global-item: */
    item()->updateItem();
}

int UIChooserNodeGlobal::positionOf(UIChooserNode *pNode)
{
    Q_UNUSED(pNode);
    AssertFailedReturn(0);
}

void UIChooserNodeGlobal::searchForNodes(const QString &strSearchTerm, int iSearchFlags, QList<UIChooserNode*> &matchedItems)
{
    /* Ignore if we are not searching for the global-node: */
    if (!(iSearchFlags & UIChooserItemSearchFlag_Global))
        return;

    /* If the search term is empty we just add the node to the matched list: */
    if (strSearchTerm.isEmpty())
        matchedItems << this;
    else
    {
        /* If exact name flag specified => check full node name: */
        if (iSearchFlags & UIChooserItemSearchFlag_ExactName)
        {
            if (name() == strSearchTerm)
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

void UIChooserNodeGlobal::sortNodes()
{
    AssertFailedReturnVoid();
}

void UIChooserNodeGlobal::sltRetranslateUI()
{
    /* Translate name & description: */
    m_strName = tr("Tools");
    m_strDescription = tr("Item");

    /* Update global-item: */
    if (item())
        item()->updateItem();
}
