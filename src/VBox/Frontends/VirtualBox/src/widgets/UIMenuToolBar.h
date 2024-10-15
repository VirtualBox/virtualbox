/* $Id: UIMenuToolBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMenuToolBar class declaration.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIMenuToolBar_h
#define FEQT_INCLUDED_SRC_widgets_UIMenuToolBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* Forward declarations: */
class UIMenuToolBarPrivate;


/** QWidget wrapper for QIToolBar extension
  * holding single drop-down menu of actions. */
class UIMenuToolBar : public QWidget
{
    Q_OBJECT;

public:

    /** Menu toolbar alignment types. */
    enum AlignmentType
    {
        AlignmentType_TopLeft,
        AlignmentType_TopRight,
        AlignmentType_BottomLeft,
        AlignmentType_BottomRight,
    };

    /** Constructs menu-toolbar wrapper. */
    UIMenuToolBar(QWidget *pParent = 0);

    /** Defines toolbar alignment @a enmType. */
    void setAlignmentType(AlignmentType enmType);

    /** Defines toolbar icon @a size. */
    void setIconSize(const QSize &size);

    /** Defines toolbar menu action. */
    void setMenuAction(QAction *pAction);

    /** Defines toolbar tool button @a enmStyle. */
    void setToolButtonStyle(Qt::ToolButtonStyle enmStyle);

    /** Returns toolbar widget for passed @a pAction. */
    QWidget *widgetForAction(QAction *pAction) const;

private:

    /** Prepares all. */
    void prepare();

    /** Holds the menu-toolbar instance. */
    UIMenuToolBarPrivate *m_pToolbar;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIMenuToolBar_h */

