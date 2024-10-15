/* $Id: UIChooserHandlerMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserHandlerMouse class implementation.
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
#include <QGraphicsSceneMouseEvent>

/* GUI incluedes: */
#include "UIChooserHandlerMouse.h"
#include "UIChooserModel.h"
#include "UIChooserItemGroup.h"
#include "UIChooserItemGlobal.h"
#include "UIChooserItemMachine.h"


UIChooserHandlerMouse::UIChooserHandlerMouse(UIChooserModel *pParent)
    : QObject(pParent)
    , m_pModel(pParent)
{
}

bool UIChooserHandlerMouse::handle(QGraphicsSceneMouseEvent *pEvent, UIMouseEventType type) const
{
    /* Process passed event: */
    switch (type)
    {
        case UIMouseEventType_Press: return handleMousePress(pEvent);
        case UIMouseEventType_Release: return handleMouseRelease(pEvent);
        case UIMouseEventType_DoubleClick: return handleMouseDoubleClick(pEvent);
    }
    /* Pass event if unknown: */
    return false;
}

UIChooserModel* UIChooserHandlerMouse::model() const
{
    return m_pModel;
}

bool UIChooserHandlerMouse::handleMousePress(QGraphicsSceneMouseEvent *pEvent) const
{
    /* Get item under mouse cursor: */
    QPointF scenePos = pEvent->scenePos();
    if (QGraphicsItem *pItemUnderMouse = model()->itemAt(scenePos))
    {
        /* Which button it was? */
        switch (pEvent->button())
        {
            /* Left one? */
            case Qt::LeftButton:
            {
                /* Which item we just clicked? */
                UIChooserItem *pClickedItem = 0;
                /* Was that a group item? */
                if (UIChooserItemGroup *pGroupItem = qgraphicsitem_cast<UIChooserItemGroup*>(pItemUnderMouse))
                    pClickedItem = pGroupItem;
                /* Or a global one? */
                else if (UIChooserItemGlobal *pGlobalItem = qgraphicsitem_cast<UIChooserItemGlobal*>(pItemUnderMouse))
                {
                    const QPoint itemCursorPos = pGlobalItem->mapFromScene(scenePos).toPoint();
                    if (   pGlobalItem->isToolButtonArea(itemCursorPos)
                        && (   model()->firstSelectedItem() == pGlobalItem
                            || pGlobalItem->isHovered()))
                    {
                        model()->handleToolButtonClick(pGlobalItem);
                        if (model()->firstSelectedItem() != pGlobalItem)
                            pClickedItem = pGlobalItem;
                    }
                    else
                    if (   pGlobalItem->isPinButtonArea(itemCursorPos)
                        && (   model()->firstSelectedItem() == pGlobalItem
                            || pGlobalItem->isHovered()))
                        model()->handlePinButtonClick(pGlobalItem);
                    else
                        pClickedItem = pGlobalItem;
                }
                /* Or a machine one? */
                else if (UIChooserItemMachine *pMachineItem = qgraphicsitem_cast<UIChooserItemMachine*>(pItemUnderMouse))
                {
                    const QPoint itemCursorPos = pMachineItem->mapFromScene(scenePos).toPoint();
                    if (   pMachineItem->isToolButtonArea(itemCursorPos)
                        && (   model()->firstSelectedItem() == pMachineItem
                            || pMachineItem->isHovered()))
                    {
                        model()->handleToolButtonClick(pMachineItem);
                        if (model()->firstSelectedItem() != pMachineItem)
                            pClickedItem = pMachineItem;
                    }
                    else
                        pClickedItem = pMachineItem;
                }
                /* If we had clicked one of required item types: */
                if (pClickedItem && !pClickedItem->isRoot())
                {
                    /* Was 'shift' modifier pressed? */
                    if (pEvent->modifiers() == Qt::ShiftModifier)
                    {
                        /* Calculate positions: */
                        UIChooserItem *pFirstItem = model()->firstSelectedItem();
                        AssertPtrReturn(pFirstItem, false); // is failure possible?
                        const int iFirstPosition = model()->navigationItems().indexOf(pFirstItem);
                        const int iClickedPosition = model()->navigationItems().indexOf(pClickedItem);
                        /* Populate list of items from 'first' to 'clicked': */
                        QList<UIChooserItem*> items;
                        if (iFirstPosition <= iClickedPosition)
                            for (int i = iFirstPosition; i <= iClickedPosition; ++i)
                                items << model()->navigationItems().at(i);
                        else
                            for (int i = iFirstPosition; i >= iClickedPosition; --i)
                                items << model()->navigationItems().at(i);
                        /* Wipe out items of inconsistent types: */
                        QList<UIChooserItem*> filteredItems;
                        foreach (UIChooserItem *pIteratedItem, items)
                        {
                            /* So, the logic is to add intermediate item if
                             * - first and intermediate selected items are global or
                             * - first and intermediate selected items are NOT global. */
                            if (   (   pFirstItem->type() == UIChooserNodeType_Global
                                    && pIteratedItem->type() == UIChooserNodeType_Global)
                                || (   pFirstItem->type() != UIChooserNodeType_Global
                                    && pIteratedItem->type() != UIChooserNodeType_Global))
                                filteredItems << pIteratedItem;
                        }
                        /* Make that list selected: */
                        model()->setSelectedItems(filteredItems);
                        /* Make item closest to clicked the current one: */
                        if (!filteredItems.isEmpty())
                            model()->setCurrentItem(filteredItems.last());
                    }
                    /* Was 'control' modifier pressed? */
                    else if (pEvent->modifiers() == Qt::ControlModifier)
                    {
                        /* Invert selection state for clicked item: */
                        if (model()->selectedItems().contains(pClickedItem))
                            model()->removeFromSelectedItems(pClickedItem);
                        else
                        {
                            /* So, the logic is to add newly clicked item if
                             * - previously and newly selected items are global or
                             * - previously and newly selected items are NOT global. */
                            UIChooserItem *pFirstItem = model()->firstSelectedItem();
                            AssertPtrReturn(pFirstItem, false); // is failure possible?
                            if (   (   pFirstItem->type() == UIChooserNodeType_Global
                                    && pClickedItem->type() == UIChooserNodeType_Global)
                                || (   pFirstItem->type() != UIChooserNodeType_Global
                                    && pClickedItem->type() != UIChooserNodeType_Global))
                                model()->addToSelectedItems(pClickedItem);
                        }
                        /* Make clicked item current one: */
                        model()->setCurrentItem(pClickedItem);
                    }
                    /* Was no modifiers pressed? */
                    else if (pEvent->modifiers() == Qt::NoModifier)
                    {
                        /* Make clicked item the only selected one: */
                        model()->setSelectedItem(pClickedItem);
                    }
                }
                break;
            }
            /* Right one? */
            case Qt::RightButton:
            {
                /* Which item we just clicked? */
                UIChooserItem *pClickedItem = 0;
                /* Was that a group item? */
                if (UIChooserItemGroup *pGroupItem = qgraphicsitem_cast<UIChooserItemGroup*>(pItemUnderMouse))
                    pClickedItem = pGroupItem;
                /* Or a global one? */
                else if (UIChooserItemGlobal *pGlobalItem = qgraphicsitem_cast<UIChooserItemGlobal*>(pItemUnderMouse))
                    pClickedItem = pGlobalItem;
                /* Or a machine one? */
                else if (UIChooserItemMachine *pMachineItem = qgraphicsitem_cast<UIChooserItemMachine*>(pItemUnderMouse))
                    pClickedItem = pMachineItem;
                /* If we had clicked one of required item types: */
                if (pClickedItem && !pClickedItem->isRoot())
                {
                    /* Select clicked item if not selected yet: */
                    if (!model()->selectedItems().contains(pClickedItem))
                        model()->setSelectedItem(pClickedItem);
                }
                break;
            }
            default:
                break;
        }
    }
    /* Pass all other events: */
    return false;
}

bool UIChooserHandlerMouse::handleMouseRelease(QGraphicsSceneMouseEvent*) const
{
    /* Pass all events: */
    return false;
}

bool UIChooserHandlerMouse::handleMouseDoubleClick(QGraphicsSceneMouseEvent *pEvent) const
{
    /* Get item under mouse cursor: */
    QPointF scenePos = pEvent->scenePos();
    if (QGraphicsItem *pItemUnderMouse = model()->itemAt(scenePos))
    {
        /* Which button it was? */
        switch (pEvent->button())
        {
            /* Left one? */
            case Qt::LeftButton:
            {
                /* Was that a group item? */
                if (UIChooserItemGroup *pGroupItem = qgraphicsitem_cast<UIChooserItemGroup*>(pItemUnderMouse))
                {
                    /* If it was not root: */
                    if (!pGroupItem->isRoot())
                    {
                        /* Toggle it: */
                        if (pGroupItem->isClosed())
                            pGroupItem->open();
                        else if (pGroupItem->isOpened())
                            pGroupItem->close();
                    }
                    /* Filter that event out: */
                    return true;
                }
                /* Or a machine one? */
                else if (pItemUnderMouse->type() == UIChooserNodeType_Machine)
                {
                    /* Start or show selected items: */
                    model()->startOrShowSelectedItems();
                }
                break;
            }
            default:
                break;
        }
    }
    /* Pass all other events: */
    return false;
}

