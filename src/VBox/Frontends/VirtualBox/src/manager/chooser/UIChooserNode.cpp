/* $Id: UIChooserNode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserNode class definition.
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
#include "UIChooserNode.h"
#include "UIChooserNodeGroup.h"
#include "UIChooserNodeGlobal.h"
#include "UIChooserNodeMachine.h"

/* Other VBox includes: */
#include "iprt/cpp/utils.h"


UIChooserNode::UIChooserNode(UIChooserNode *pParent /* = 0 */, bool fFavorite /* = false */)
    : QObject(pParent)
    , m_pParent(pParent)
    , m_fFavorite(fFavorite)
    , m_pModel(0)
    , m_fDisabled(false)
{
}

UIChooserNode::~UIChooserNode()
{
    if (!m_pItem.isNull())
        delete m_pItem.data();
}

UIChooserNodeGroup *UIChooserNode::toGroupNode()
{
    return static_cast<UIChooserNodeGroup*>(this);
}

UIChooserNodeGlobal *UIChooserNode::toGlobalNode()
{
    return static_cast<UIChooserNodeGlobal*>(this);
}

UIChooserNodeMachine *UIChooserNode::toMachineNode()
{
    return static_cast<UIChooserNodeMachine*>(this);
}

UIChooserNode *UIChooserNode::rootNode() const
{
    return isRoot() ? unconst(this) : parentNode()->rootNode();
}

UIChooserAbstractModel *UIChooserNode::model() const
{
    return m_pModel ? m_pModel : rootNode()->model();
}

int UIChooserNode::position()
{
    return parentNode() ? parentNode()->positionOf(this) : 0;
}

bool UIChooserNode::isDisabled() const
{
    return m_fDisabled;
}

void UIChooserNode::setDisabled(bool fDisabled)
{
    if (fDisabled == m_fDisabled)
        return;
    m_fDisabled = fDisabled;
    if (m_pItem)
        m_pItem->setDisabledEffect(m_fDisabled);
}
