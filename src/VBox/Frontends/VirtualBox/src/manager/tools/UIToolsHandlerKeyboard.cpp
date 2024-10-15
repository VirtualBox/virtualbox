/* $Id: UIToolsHandlerKeyboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsHandlerKeyboard class implementation.
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
#include <QKeyEvent>

/* GUI incluedes: */
#include "UIToolsHandlerKeyboard.h"
#include "UIToolsModel.h"
#include "UIToolsItem.h"


UIToolsHandlerKeyboard::UIToolsHandlerKeyboard(UIToolsModel *pParent)
    : QObject(pParent)
    , m_pModel(pParent)
{
}

bool UIToolsHandlerKeyboard::handle(QKeyEvent *pEvent, UIKeyboardEventType enmType) const
{
    /* Process passed event: */
    switch (enmType)
    {
        case UIKeyboardEventType_Press:   return handleKeyPress(pEvent);
        case UIKeyboardEventType_Release: return handleKeyRelease(pEvent);
    }
    /* Pass event if unknown: */
    return false;
}

UIToolsModel *UIToolsHandlerKeyboard::model() const
{
    return m_pModel;
}

bool UIToolsHandlerKeyboard::handleKeyPress(QKeyEvent *pEvent) const
{
    /* Which key it was? */
    switch (pEvent->key())
    {
        /* Key UP? */
        case Qt::Key_Up:
        /* Key HOME? */
        case Qt::Key_Home:
        {
            /* Determine focus item position: */
            const int iPosition = model()->navigationList().indexOf(model()->focusItem());
            /* Determine 'previous' item: */
            UIToolsItem *pPreviousItem = 0;
            if (iPosition > 0)
            {
                if (pEvent->key() == Qt::Key_Up)
                    for (int i = iPosition - 1; i >= 0; --i)
                    {
                        UIToolsItem *pIteratedItem = model()->navigationList().at(i);
                        if (pIteratedItem->isEnabled())
                        {
                            pPreviousItem = pIteratedItem;
                            break;
                        }
                    }
                else if (pEvent->key() == Qt::Key_Home)
                    pPreviousItem = model()->navigationList().first();
            }
            if (pPreviousItem)
            {
                /* Make 'previous' item the current one: */
                model()->setCurrentItem(pPreviousItem);
                /* Filter-out this event: */
                return true;
            }
            /* Pass this event: */
            return false;
        }
        /* Key DOWN? */
        case Qt::Key_Down:
        /* Key END? */
        case Qt::Key_End:
        {
            /* Determine focus item position: */
            int iPosition = model()->navigationList().indexOf(model()->focusItem());
            /* Determine 'next' item: */
            UIToolsItem *pNextItem = 0;
            if (iPosition < model()->navigationList().size() - 1)
            {
                if (pEvent->key() == Qt::Key_Down)
                    for (int i = iPosition + 1; i < model()->navigationList().size(); ++i)
                    {
                        UIToolsItem *pIteratedItem = model()->navigationList().at(i);
                        if (pIteratedItem->isEnabled())
                        {
                            pNextItem = pIteratedItem;
                            break;
                        }
                    }
                else if (pEvent->key() == Qt::Key_End)
                    pNextItem = model()->navigationList().last();
            }
            if (pNextItem)
            {
                /* Make 'next' item the current one: */
                model()->setCurrentItem(pNextItem);
                /* Filter-out this event: */
                return true;
            }
            /* Pass this event: */
            return false;
        }
        default:
            break;
    }
    /* Pass all other events: */
    return false;
}

bool UIToolsHandlerKeyboard::handleKeyRelease(QKeyEvent *) const
{
    /* Pass all events: */
    return false;
}
