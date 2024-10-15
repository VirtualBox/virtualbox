/* $Id: UIGraphicsZoomButton.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsZoomButton class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsZoomButton_h
#define FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsZoomButton_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIGraphicsButton.h"

/* Other VBox includes: */
#include "iprt/cdefs.h" // for RT_BIT stuff

/* Forward declarations: */
class QStateMachine;
class QPropertyAnimation;

/* Zoom direction: */
enum UIGraphicsZoomDirection
{
    UIGraphicsZoomDirection_Top    = RT_BIT(0),
    UIGraphicsZoomDirection_Bottom = RT_BIT(1),
    UIGraphicsZoomDirection_Left   = RT_BIT(2),
    UIGraphicsZoomDirection_Right  = RT_BIT(3)
};

/* Zoom graphics-button representation: */
class UIGraphicsZoomButton : public UIGraphicsButton
{
    Q_OBJECT;
    Q_PROPERTY(bool stateDefault READ stateDefault WRITE setStateDefault);

signals:

    /* Notify listeners about hover events: */
    void sigHoverEnter();
    void sigHoverLeave();

public:

    /* Constructor: */
    UIGraphicsZoomButton(QIGraphicsWidget *pParent, const QIcon &icon, int iDirection);

    /* API: Zoom stuff: */
    int indent() const;
    void setIndent(int iIndent);

    /* API: Animation stuff: */
    void updateAnimation();

protected:

    /* Data provider: */
    QVariant data(int iKey) const RT_OVERRIDE RT_FINAL;

    /* Handler: Mouse hover: */
    void hoverEnterEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE RT_FINAL;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /* Paint stuff: */
    void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOption, QWidget *pWidget = 0) RT_OVERRIDE RT_FINAL;

private:

    /* Animation stuff: */
    bool isAnimationRunning() const;

    /* Property stuff: */
    bool stateDefault() const;
    void setStateDefault(bool fStateDefault);

    /* Variables: */
    int m_iIndent;
    int m_iDirection;
    int m_iAnimationDuration;
    QStateMachine *m_pStateMachine;
    QPropertyAnimation *m_pForwardAnimation;
    QPropertyAnimation *m_pBackwardAnimation;
    bool m_fStateDefault;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsZoomButton_h */
