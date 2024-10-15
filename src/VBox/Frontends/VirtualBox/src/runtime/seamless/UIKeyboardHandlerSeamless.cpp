/* $Id: UIKeyboardHandlerSeamless.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIKeyboardHandlerSeamless class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_WS_MAC
/* Qt includes: */
# include <QKeyEvent>
# include <QTimer>
#endif /* !VBOX_WS_MAC */

/* GUI includes: */
#include "UIKeyboardHandlerSeamless.h"
#ifndef VBOX_WS_MAC
# include "UIMachineLogic.h"
# include "UIShortcutPool.h"
#endif /* !VBOX_WS_MAC */


#ifndef VBOX_WS_MAC
/* Namespaces: */
using namespace UIExtraDataDefs;
#endif /* !VBOX_WS_MAC */

UIKeyboardHandlerSeamless::UIKeyboardHandlerSeamless(UIMachineLogic* pMachineLogic)
    : UIKeyboardHandler(pMachineLogic)
{
}

UIKeyboardHandlerSeamless::~UIKeyboardHandlerSeamless()
{
}

#ifndef VBOX_WS_MAC
bool UIKeyboardHandlerSeamless::eventFilter(QObject *pWatchedObject, QEvent *pEvent)
{
    /* Check if pWatchedObject object is view: */
    if (UIMachineView *pWatchedView = isItListenedView(pWatchedObject))
    {
        /* Get corresponding screen index: */
        ulong uScreenId = m_views.key(pWatchedView);
        NOREF(uScreenId);
        /* Handle view events: */
        switch (pEvent->type())
        {
            case QEvent::KeyPress:
            {
                /* Get key-event: */
                QKeyEvent *pKeyEvent = static_cast<QKeyEvent*>(pEvent);
                /* Process Host+Home for menu popup: */
                if (   isHostKeyPressed()
                    && gShortcutPool->shortcut(GUI_Input_MachineShortcuts, QString("PopupMenu")).sequences().contains(QKeySequence(pKeyEvent->key())))
                {
                    /* Post request to show popup-menu: */
                    QTimer::singleShot(0, m_pMachineLogic, SLOT(sltInvokePopupMenu()));
                    /* Filter-out this event: */
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    /* Else just propagate to base-class: */
    return UIKeyboardHandler::eventFilter(pWatchedObject, pEvent);
}
#endif /* !VBOX_WS_MAC */
