/* $Id: UIGraphicsToolBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsToolBar class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsToolBar_h
#define FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsToolBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIGraphicsWidget.h"

/* Forward declarations: */
class UIGraphicsButton;

/* Graphics tool-bar: */
class UIGraphicsToolBar : public QIGraphicsWidget
{
    Q_OBJECT;

public:

    /* Constructor: */
    UIGraphicsToolBar(QIGraphicsWidget *pParent, int iRows, int iColumns);

    /* API: Margin stuff: */
    int toolBarMargin() const;
    void setToolBarMargin(int iMargin);

    /* API: Children stuff: */
    void insertItem(UIGraphicsButton *pButton, int iRow, int iColumn);

    /* API: Layout stuff: */
    void updateLayout();

protected:

    /* Typedefs: */
    typedef QPair<int, int> UIGraphicsToolBarIndex;

    /* Helpers: Layout stuff: */
    QSizeF sizeHint(Qt::SizeHint which, const QSizeF &constraint = QSizeF()) const  RT_OVERRIDE RT_FINAL;

private:

    /* Variables: */
    int m_iMargin;
    int m_iRows;
    int m_iColumns;
    QMap<UIGraphicsToolBarIndex, UIGraphicsButton*> m_buttons;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsToolBar_h */
