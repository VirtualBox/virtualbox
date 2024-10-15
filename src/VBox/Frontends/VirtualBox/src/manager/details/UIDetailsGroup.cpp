/* $Id: UIDetailsGroup.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsGroup class implementation.
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

/* Qt include: */
#include <QGraphicsLinearLayout>
#include <QGraphicsScene>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionGraphicsItem>

/* GUI includes: */
#include "UIGraphicsScrollArea.h"
#include "UIDetailsGroup.h"
#include "UIDetailsModel.h"
#include "UIDetailsSet.h"
#include "UIDetailsView.h"
#include "UIExtraDataManager.h"
#include "UIVirtualMachineItem.h"


UIDetailsGroup::UIDetailsGroup(QGraphicsScene *pParent)
    : UIDetailsItem(0)
    , m_pBuildStep(0)
    , m_pScrollArea(0)
    , m_pContainer(0)
    , m_pLayout(0)
    , m_iPreviousMinimumWidthHint(0)
{
    /* Prepare scroll-area: */
    m_pScrollArea = new UIGraphicsScrollArea(Qt::Vertical, this);
    if (m_pScrollArea)
    {
        /* Prepare container: */
        m_pContainer = new QIGraphicsWidget;
        if (m_pContainer)
        {
            /* Prepare layout: */
            m_pLayout = new QGraphicsLinearLayout(Qt::Vertical, m_pContainer);
            if (m_pLayout)
            {
                m_pLayout->setContentsMargins(0, 0, 0, 0);
                m_pLayout->setSpacing(0);
            }

            /* Assign to scroll-area: */
            m_pScrollArea->setViewport(m_pContainer);
        }
    }

    /* Add group to the parent scene: */
    pParent->addItem(this);

    /* Prepare connections: */
    prepareConnections();
}

UIDetailsGroup::~UIDetailsGroup()
{
    /* Cleanup items: */
    clearItems();
}

void UIDetailsGroup::buildGroup(const QList<UIVirtualMachineItem*> &machineItems)
{
    /* Filter out cloud VM items for now: */
    QList<UIVirtualMachineItem*> filteredItems;
    foreach (UIVirtualMachineItem *pItem, machineItems)
        if (   pItem->itemType() == UIVirtualMachineItemType_Local
            || pItem->itemType() == UIVirtualMachineItemType_CloudReal)
            filteredItems << pItem;

    /* Remember passed machine-items: */
    m_machineItems = filteredItems;

    /* Cleanup superflous items: */
    const bool fCleanupPerformed = m_items.size() > m_machineItems.size();
    while (m_items.size() > m_machineItems.size())
        delete m_items.last();
    foreach (UIDetailsItem *pItem, m_items)
        pItem->toSet()->clearSet();
    if (fCleanupPerformed)
        updateGeometry();

    /* Start building group: */
    rebuildGroup();
}

void UIDetailsGroup::rebuildGroup()
{
    /* Cleanup build-step: */
    delete m_pBuildStep;
    m_pBuildStep = 0;

    /* Generate new group-id: */
    m_uGroupId = QUuid::createUuid();

    /* Request to build first step: */
    emit sigBuildStep(m_uGroupId, 0);
}

void UIDetailsGroup::stopBuildingGroup()
{
    /* Generate new group-id: */
    m_uGroupId = QUuid::createUuid();
}

void UIDetailsGroup::installEventFilterHelper(QObject *pSource)
{
    /* The only object which need's that filter for now is scroll-area: */
    pSource->installEventFilter(m_pScrollArea);
}

QList<UIDetailsItem*> UIDetailsGroup::items(UIDetailsItemType enmType /* = UIDetailsItemType_Set */) const
{
    switch (enmType)
    {
        case UIDetailsItemType_Set: return m_items;
        case UIDetailsItemType_Any: return items(UIDetailsItemType_Set);
        default: AssertMsgFailed(("Invalid item type!")); break;
    }
    return QList<UIDetailsItem*>();
}

void UIDetailsGroup::updateLayout()
{
    /* Acquire view: */
    UIDetailsView *pView = model()->view();

    /* Adjust children scroll-area: */
    m_pScrollArea->resize(pView->size());
    m_pScrollArea->setPos(0, 0);

    /* Layout all the sets: */
    foreach (UIDetailsItem *pItem, items())
        pItem->updateLayout();
}

int UIDetailsGroup::minimumWidthHint() const
{
    return m_pContainer->minimumSizeHint().width();
}

int UIDetailsGroup::minimumHeightHint() const
{
    return m_pContainer->minimumSizeHint().height();
}

void UIDetailsGroup::sltBuildStep(const QUuid &uStepId, int iStepNumber)
{
    /* Cleanup build-step: */
    delete m_pBuildStep;
    m_pBuildStep = 0;

    /* Is step id valid? */
    if (uStepId != m_uGroupId)
        return;

    /* Step number feats the bounds: */
    if (iStepNumber >= 0 && iStepNumber < m_machineItems.size())
    {
        /* Should we create a new set for this step? */
        UIDetailsSet *pSet = 0;
        if (iStepNumber > m_items.size() - 1)
            pSet = new UIDetailsSet(this);
        /* Or use existing? */
        else
            pSet = m_items.at(iStepNumber)->toSet();

        /* Create next build-step: */
        m_pBuildStep = new UIPrepareStep(this, pSet, uStepId, iStepNumber + 1);

        /* Build set: */
        pSet->buildSet(m_machineItems[iStepNumber], m_machineItems.size() == 1, model()->categories());
    }
    else
    {
        /* Notify listener about build done: */
        emit sigBuildDone();
    }
}

void UIDetailsGroup::addItem(UIDetailsItem *pItem)
{
    switch (pItem->type())
    {
        case UIDetailsItemType_Set:
        {
            m_pLayout->addItem(pItem);
            m_items.append(pItem);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid item type!"));
            break;
        }
    }
}

void UIDetailsGroup::removeItem(UIDetailsItem *pItem)
{
    switch (pItem->type())
    {
        case UIDetailsItemType_Set: m_items.removeAt(m_items.indexOf(pItem)); break;
        default: AssertMsgFailed(("Invalid item type!")); break;
    }
}

bool UIDetailsGroup::hasItems(UIDetailsItemType enmType /* = UIDetailsItemType_Set */) const
{
    switch (enmType)
    {
        case UIDetailsItemType_Set: return !m_items.isEmpty();
        case UIDetailsItemType_Any: return hasItems(UIDetailsItemType_Set);
        default: AssertMsgFailed(("Invalid item type!")); break;
    }
    return false;
}

void UIDetailsGroup::clearItems(UIDetailsItemType enmType /* = UIDetailsItemType_Set */)
{
    switch (enmType)
    {
        case UIDetailsItemType_Set: while (!m_items.isEmpty()) { delete m_items.last(); } break;
        case UIDetailsItemType_Any: clearItems(UIDetailsItemType_Set); break;
        default: AssertMsgFailed(("Invalid item type!")); break;
    }
}

void UIDetailsGroup::updateGeometry()
{
    /* Update/activate children layout: */
    m_pLayout->updateGeometry();
    m_pLayout->activate();

    /* Call to base class: */
    UIDetailsItem::updateGeometry();

    /* Group-item should notify details-view if minimum-width-hint was changed: */
    int iMinimumWidthHint = minimumWidthHint();
    if (m_iPreviousMinimumWidthHint != iMinimumWidthHint)
    {
        /* Save new minimum-width-hint, notify listener: */
        m_iPreviousMinimumWidthHint = iMinimumWidthHint;
        emit sigMinimumWidthHintChanged(m_iPreviousMinimumWidthHint);
    }
}

void UIDetailsGroup::prepareConnections()
{
    /* Prepare group-item connections: */
    connect(this, &UIDetailsGroup::sigMinimumWidthHintChanged,
            model(), &UIDetailsModel::sigRootItemMinimumWidthHintChanged);
}
