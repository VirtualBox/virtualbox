/* $Id: UIGraphicsButton.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsButton class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsButton_h
#define FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsButton_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>

/* GUI includes: */
#include "QIGraphicsWidget.h"

/* Forward declarations: */
class QGraphicsSceneMouseEvent;
class QGraphicsSceneHoverEvent;
class QPropertyAnimation;

/** QIGraphicsWidget subclass providing GUI with graphics-button representation. */
class UIGraphicsButton : public QIGraphicsWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about button was clicked. */
    void sigButtonClicked();

public:

    /** Click policy. */
    enum ClickPolicy { ClickPolicy_OnRelease, ClickPolicy_OnPress };

    /** Constructs graphics button passing @a pParent to the base-class.
      * @param  icon  Brings the button icon. */
    UIGraphicsButton(QIGraphicsWidget *pParent, const QIcon &icon);

    /** Defines icon scale @a dIndex. */
    void setIconScaleIndex(double dIndex);
    /** Returns icon scale index. */
    double iconScaleIndex() const;

    /** Defines click @a enmPolicy. */
    void setClickPolicy(ClickPolicy enmPolicy);
    /** Returns click policy. */
    ClickPolicy clickPolicy() const;

protected:

    /** Data enumerator. */
    enum GraphicsButton
    {
        GraphicsButton_Margin,
        GraphicsButton_IconSize,
        GraphicsButton_Icon
    };

    /** Returns data stored for certain @a iKey: */
    virtual QVariant data(int iKey) const;

    /** Returns size-hint of certain @a enmType, restricted by passed @a constraint. */
    virtual QSizeF sizeHint(Qt::SizeHint enmType, const QSizeF &constraint = QSizeF()) const RT_OVERRIDE;

    /** Performs painting using passed @a pPainter, @a pOptions and optionally specified @a pWidget. */
    virtual void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOption, QWidget *pWidget = 0) RT_OVERRIDE;

    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-release @a pEvent. */
    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;

    /** Handles timer @a pEvent. */
    virtual void timerEvent(QTimerEvent *pEvent) RT_OVERRIDE;

    /** Updates button.
      * @todo rename to prepare() */
    virtual void refresh();

private:

    /** Holds the button icon. */
    QIcon m_icon;

    /** Holds the click policy. */
    ClickPolicy  m_enmClickPolicy;

    /** Holds the delay timer ID. */
    int  m_iDelayId;
    /** Holds the repeat timer ID. */
    int  m_iRepeatId;

    /** Holds the icon scale index. */
    double  m_dIconScaleIndex;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsButton_h */
