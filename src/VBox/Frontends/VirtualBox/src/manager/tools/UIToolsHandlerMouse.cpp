/* $Id: UIToolsHandlerMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsHandlerMouse class implementation.
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
#include "UIToolsHandlerMouse.h"
#include "UIToolsModel.h"


UIToolsHandlerMouse::UIToolsHandlerMouse(UIToolsModel *pParent)
    : QObject(pParent)
    , m_pModel(pParent)
{
}

bool UIToolsHandlerMouse::handle(QGraphicsSceneMouseEvent *pEvent, UIMouseEventType enmType) const
{
    /* Process passed event: */
    switch (enmType)
    {
        case UIMouseEventType_Press:   return handleMousePress(pEvent);
        case UIMouseEventType_Release: return handleMouseRelease(pEvent);
    }
    /* Pass event if unknown: */
    return false;
}

UIToolsModel *UIToolsHandlerMouse::model() const
{
    return m_pModel;
}

bool UIToolsHandlerMouse::handleMousePress(QGraphicsSceneMouseEvent *pEvent) const
{
    /* Get item under mouse cursor: */
    QPointF scenePos = pEvent->scenePos();
    if (QGraphicsItem *pItemUnderMouse = model()->itemAt(scenePos))
    {
        /* Which button it was? */
        switch (pEvent->button())
        {
            /* Both buttons: */
            case Qt::LeftButton:
            case Qt::RightButton:
            {
                /* Which item we just clicked? */
                UIToolsItem *pClickedItem = qgraphicsitem_cast<UIToolsItem*>(pItemUnderMouse);
                /* Make clicked item the current one: */
                if (pClickedItem && pClickedItem->isEnabled())
                {
                    model()->setCurrentItem(pClickedItem);
                    model()->close();
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

bool UIToolsHandlerMouse::handleMouseRelease(QGraphicsSceneMouseEvent *) const
{
    /* Pass all events: */
    return false;
}
