/* $Id: UIToolsHandlerMouse.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsHandlerMouse class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_tools_UIToolsHandlerMouse_h
#define FEQT_INCLUDED_SRC_manager_tools_UIToolsHandlerMouse_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* Forward declarations: */
class QGraphicsSceneMouseEvent;
class UIToolsModel;
class UIToolsItem;


/** Mouse event type. */
enum UIMouseEventType
{
    UIMouseEventType_Press,
    UIMouseEventType_Release
};


/** QObject extension used as mouse handler for graphics tools selector. */
class UIToolsHandlerMouse : public QObject
{
    Q_OBJECT;

public:

    /** Constructs mouse handler passing @a pParent to the base-class. */
    UIToolsHandlerMouse(UIToolsModel *pParent);

    /** Handles mouse @a pEvent of certain @a enmType. */
    bool handle(QGraphicsSceneMouseEvent *pEvent, UIMouseEventType enmType) const;

private:

    /** Returns the parent model reference. */
    UIToolsModel *model() const;

    /** Handles mouse press @a pEvent. */
    bool handleMousePress(QGraphicsSceneMouseEvent *pEvent) const;
    /** Handles mouse release @a pEvent. */
    bool handleMouseRelease(QGraphicsSceneMouseEvent *pEvent) const;

    /** Holds the parent model reference. */
    UIToolsModel *m_pModel;
};


#endif /* !FEQT_INCLUDED_SRC_manager_tools_UIToolsHandlerMouse_h */
