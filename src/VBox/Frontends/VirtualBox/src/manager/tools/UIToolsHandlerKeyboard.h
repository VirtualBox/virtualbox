/* $Id: UIToolsHandlerKeyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsHandlerKeyboard class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_tools_UIToolsHandlerKeyboard_h
#define FEQT_INCLUDED_SRC_manager_tools_UIToolsHandlerKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QObject>

/* Forward declarations: */
class QKeyEvent;
class UIToolsModel;


/** Keyboard event types. */
enum UIKeyboardEventType
{
    UIKeyboardEventType_Press,
    UIKeyboardEventType_Release
};


/** QObject extension used as keyboard handler for graphics tools selector. */
class UIToolsHandlerKeyboard : public QObject
{
    Q_OBJECT;

public:

    /** Constructs keyboard handler passing @a pParent to the base-class. */
    UIToolsHandlerKeyboard(UIToolsModel *pParent);

    /** Handles keyboard @a pEvent of certain @a enmType. */
    bool handle(QKeyEvent *pEvent, UIKeyboardEventType enmType) const;

private:

    /** Returns the parent model reference. */
    UIToolsModel *model() const;

    /** Handles keyboard press @a pEvent. */
    bool handleKeyPress(QKeyEvent *pEvent) const;
    /** Handles keyboard release @a pEvent. */
    bool handleKeyRelease(QKeyEvent *pEvent) const;

    /** Holds the parent model reference. */
    UIToolsModel *m_pModel;
};


#endif /* !FEQT_INCLUDED_SRC_manager_tools_UIToolsHandlerKeyboard_h */
