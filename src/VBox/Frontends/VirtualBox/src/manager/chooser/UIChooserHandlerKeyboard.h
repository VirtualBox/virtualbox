/* $Id: UIChooserHandlerKeyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserHandlerKeyboard class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserHandlerKeyboard_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserHandlerKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QMap>

/* Forward declarations: */
class UIChooserModel;
class QKeyEvent;

/** Keyboard event type. */
enum UIKeyboardEventType
{
    UIKeyboardEventType_Press,
    UIKeyboardEventType_Release
};

/** Item shift direction. */
enum UIItemShiftDirection
{
    UIItemShiftDirection_Up,
    UIItemShiftDirection_Down
};

/** Item shift types. */
enum UIItemShiftType
{
    UIItemShiftSize_Item,
    UIItemShiftSize_Full
};

/** Keyboard handler for graphics selector. */
class UIChooserHandlerKeyboard : public QObject
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIChooserHandlerKeyboard(UIChooserModel *pParent);

    /** API: Model keyboard-event handler delegate. */
    bool handle(QKeyEvent *pEvent, UIKeyboardEventType type) const;

private:

    /** API: Model wrapper. */
    UIChooserModel* model() const;

    /** Helpers: Model keyboard-event handler delegates. */
    bool handleKeyPress(QKeyEvent *pEvent) const;
    bool handleKeyRelease(QKeyEvent *pEvent) const;

    /** Helper: Item shift delegate. */
    void shift(UIItemShiftDirection enmDirection, UIItemShiftType enmShiftType) const;

    /** Variables. */
    UIChooserModel *m_pModel;
    QMap<int, UIItemShiftType> m_shiftMap;
};

#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserHandlerKeyboard_h */
