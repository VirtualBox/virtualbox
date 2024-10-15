/* $Id: UIGraphicsScrollBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsScrollBar class declaration.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsScrollBar_h
#define FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsScrollBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIGraphicsWidget.h"

/* Forward declarations: */
class QGraphicsScene;
class UIGraphicsButton;
class UIGraphicsScrollBarToken;

/** QIGraphicsWidget subclass providing GUI with graphics scroll-bar. */
class UIGraphicsScrollBar : public QIGraphicsWidget
{
    Q_OBJECT;
    Q_PROPERTY(int hoveringValue READ hoveringValue WRITE setHoveringValue);
#ifdef VBOX_WS_MAC
    Q_PROPERTY(int revealingValue READ revealingValue WRITE setRevealingValue);
#endif

signals:

    /** Notifies listeners about hover enter. */
    void sigHoverEnter();
    /** Notifies listeners about hover leave. */
    void sigHoverLeave();

#ifdef VBOX_WS_MAC
    /** Notifies listeners about token should be revealed. */
    void sigRevealEnter();
    /** Notifies listeners about token should be faded. */
    void sigRevealLeave();
#endif

    /** Notifies listeners about @a iValue has changed. */
    void sigValueChanged(int iValue);

public:

    /** Constructs graphics scroll-bar of requested @a enmOrientation, embedding it directly to passed @a pScene.
      * @param  fAutoHideMode  Brings whether scroll-bar should be created in auto-hide mode. */
    UIGraphicsScrollBar(Qt::Orientation enmOrientation, bool fAutoHideMode, QGraphicsScene *pScene);

    /** Constructs graphics scroll-bar of requested @a enmOrientation passing @a pParent to the base-class.
      * @param  fAutoHideMode  Brings whether scroll-bar should be created in auto-hide mode. */
    UIGraphicsScrollBar(Qt::Orientation enmOrientation, bool fAutoHideMode, QIGraphicsWidget *pParent = 0);

    /** Returns minimum size-hint. */
    virtual QSizeF minimumSizeHint() const RT_OVERRIDE;

    /** Returns scrolling step. */
    int step() const;
    /** Returns page scrolling step. */
    int pageStep() const;

    /** Defines @a iMinimum scroll-bar value. */
    void setMinimum(int iMinimum);
    /** Returns minimum scroll-bar value. */
    int minimum() const;

    /** Defines @a iMaximum scroll-bar value. */
    void setMaximum(int iMaximum);
    /** Returns minimum scroll-bar value. */
    int maximum() const;

    /** Defines current scroll-bar @a iValue. */
    void setValue(int iValue);
    /** Returns current scroll-bar value. */
    int value() const;

    /** Performs scrolling to certain @a desiredPos with certain @a iDelay. */
    void scrollTo(const QPointF &desiredPos, int iDelay = 500);

protected:

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QGraphicsSceneResizeEvent *pEvent) RT_OVERRIDE;

    /** Performs painting using passed @a pPainter, @a pOptions and optionally specified @a pWidget. */
    virtual void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *pWidget = 0) RT_OVERRIDE;

    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-release @a pEvent. */
    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;

    /** Handles hover enter @a pEvent. */
    virtual void hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;
    /** Handles hover leave @a pEvent. */
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;

    /** Handles timer @a pEvent. */
    virtual void timerEvent(QTimerEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles button 1 click signal. */
    void sltButton1Clicked();
    /** Handles button 2 click signal. */
    void sltButton2Clicked();

    /** Handles token being moved to cpecified @a pos. */
    void sltTokenMoved(const QPointF &pos);

    /** Handles default state leaving. */
    void sltStateLeftDefault();
    /** Handles hovered state leaving. */
    void sltStateLeftHovered();
    /** Handles default state entering. */
    void sltStateEnteredDefault();
    /** Handles hovered state entering. */
    void sltStateEnteredHovered();

#ifdef VBOX_WS_MAC
    /** Handles signals to start revealing. */
    void sltHandleRevealingStart();
    /** Handles faded state entering. */
    void sltStateEnteredFaded();
    /** Handles revealed state entering. */
    void sltStateEnteredRevealed();
#endif

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares buttons. */
    void prepareButtons();
    /** Prepares token. */
    void prepareToken();
    /** Prepares animation. */
    void prepareAnimation();
    /** Prepares hovering animation. */
    void prepareHoveringAnimation();
#ifdef VBOX_WS_MAC
    /** Prepares revealing animation. */
    void prepareRevealingAnimation();
#endif

    /** Updates scroll-bar extent value. */
    void updateExtent();
    /** Layout widgets. */
    void layoutWidgets();
    /** Layout buttons. */
    void layoutButtons();
    /** Layout token. */
    void layoutToken();

    /** Returns actual token position. */
    QPoint actualTokenPosition() const;

    /** Paints background using specified @a pPainter and certain @a rectangle. */
    void paintBackground(QPainter *pPainter, const QRect &rectangle) const;

    /** Defines hovering animation @a iValue. */
    void setHoveringValue(int iValue) { m_iHoveringValue = iValue; update(); }
    /** Returns hovering animation value. */
    int hoveringValue() const { return m_iHoveringValue; }

#ifdef VBOX_WS_MAC
    /** Defines revealing animation @a iValue. */
    void setRevealingValue(int iValue) { m_iRevealingValue = iValue; update(); }
    /** Returns revealing animation value. */
    int revealingValue() const { return m_iRevealingValue; }
#endif

    /** Holds the orientation. */
    const Qt::Orientation  m_enmOrientation;
    /** Holds whether scroll-bar is in auto-hide mode. */
    bool                   m_fAutoHideMode;

    /** Holds the scroll-bar extent. */
    int  m_iExtent;

    /** Holds the minimum scroll-bar value. */
    int  m_iMinimum;
    /** Holds the maximum scroll-bar value. */
    int  m_iMaximum;
    /** Holds the current scroll-bar value. */
    int  m_iValue;

    /** Holds the 1st arrow button instance. */
    UIGraphicsButton         *m_pButton1;
    /** Holds the 2nd arrow button instance. */
    UIGraphicsButton         *m_pButton2;
    /** Holds the scroll-bar token instance. */
    UIGraphicsScrollBarToken *m_pToken;

    /** Holds whether item is hovered. */
    bool  m_fHovered;
    /** Holds the hover-on timer id. */
    int   m_iHoverOnTimerId;
    /** Holds the hover-off timer id. */
    int   m_iHoverOffTimerId;
    /** Holds the hovering animation value. */
    int   m_iHoveringValue;

    /** Holds whether we are scrolling. */
    bool  m_fScrollInProgress;

#ifdef VBOX_WS_MAC
    /** Holds whether token is revealed. */
    bool  m_fRevealed;
    /** Holds the revealing animation value. */
    int   m_iRevealingValue;
    /** Holds the reveal-out timer id. */
    int   m_iRevealOnTimerId;
    /** Holds the reveal-out timer id. */
    int   m_iRevealOffTimerId;
#endif
};

#endif /* !FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsScrollBar_h */
