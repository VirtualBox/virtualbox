/* $Id: UIGraphicsScrollBar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsScrollBar class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSignalTransition>
#include <QState>
#include <QStateMachine>
#include <QStyle>
#include <QStyleOptionGraphicsItem>
#include <QTimer>
#include <QTimerEvent>

/* GUI includes: */
#include "UIGraphicsButton.h"
#include "UIGraphicsScrollBar.h"
#include "UIIconPool.h"


/** Small functor for QTimer::singleShot worker to perform delayed scrolling. */
class ScrollFunctor
{
public:

    /** Performs delayed scrolling of specified @a pScrollBar to certain @a pos. */
    ScrollFunctor(UIGraphicsScrollBar *pScrollBar, const QPointF &pos)
        : m_pScrollBar(pScrollBar)
        , m_pos(pos)
    {}

    /** Contains functor's body. */
    void operator()()
    {
        m_pScrollBar->scrollTo(m_pos, 100);
    }

private:

    /** Holds the scroll-bar reference to scroll. */
    UIGraphicsScrollBar *m_pScrollBar;
    /** Holds the position to scroll to. */
    QPointF              m_pos;
};


/** QIGraphicsWidget subclass providing GUI with graphics scroll-bar taken. */
class UIGraphicsScrollBarToken : public QIGraphicsWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about mouse moved to certain @a pos. */
    void sigMouseMoved(const QPointF &pos);

public:

    /** Constructs graphics scroll-bar token passing @a pParent to the base-class. */
    UIGraphicsScrollBarToken(Qt::Orientation enmOrientation, QIGraphicsWidget *pParent = 0);

    /** Returns minimum size-hint. */
    virtual QSizeF minimumSizeHint() const RT_OVERRIDE;

    /** Returns whether token is hovered. */
    bool isHovered() const { return m_fHovered; }

protected:

    /** Performs painting using passed @a pPainter, @a pOptions and optionally specified @a pWidget. */
    virtual void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *pWidget = 0) RT_OVERRIDE;

    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-release @a pEvent. */
    virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *pEvent) RT_OVERRIDE;

    /** Handles hover enter @a pEvent. */
    virtual void hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;
    /** Handles hover leave @a pEvent. */
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent) RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();
    /** Updates scroll-bar extent value. */
    void updateExtent();

    /** Holds the orientation. */
    const Qt::Orientation  m_enmOrientation;

    /** Holds the scroll-bar extent. */
    int  m_iExtent;

    /** Holds whether item is hovered. */
    bool  m_fHovered;
};


/*********************************************************************************************************************************
*   Class UIGraphicsScrollBarToken implementation.                                                                               *
*********************************************************************************************************************************/

UIGraphicsScrollBarToken::UIGraphicsScrollBarToken(Qt::Orientation enmOrientation, QIGraphicsWidget *pParent /* = 0 */)
    : QIGraphicsWidget(pParent)
    , m_enmOrientation(enmOrientation)
    , m_iExtent(0)
    , m_fHovered(false)
{
    prepare();
}

QSizeF UIGraphicsScrollBarToken::minimumSizeHint() const
{
    /* Calculate minimum size-hint depending on orientation: */
    switch (m_enmOrientation)
    {
#ifdef VBOX_WS_MAC
        case Qt::Horizontal: return QSizeF(2 * m_iExtent, m_iExtent);
        case Qt::Vertical:   return QSizeF(m_iExtent, 2 * m_iExtent);
#else
        case Qt::Horizontal: return QSizeF(m_iExtent, m_iExtent);
        case Qt::Vertical:   return QSizeF(m_iExtent, m_iExtent);
#endif
    }

    /* Call to base-class: */
    return QIGraphicsWidget::minimumSizeHint();
}

void UIGraphicsScrollBarToken::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *)
{
    /* Save painter: */
    pPainter->save();

    /* Prepare color: */
    const QPalette pal = QApplication::palette();

#ifdef VBOX_WS_MAC

    /* Draw background: */
    QColor backgroundColor = pal.color(QPalette::Active, QPalette::Window).darker(190);
    QRectF actualRectangle = pOptions->rect;
    actualRectangle.setLeft(pOptions->rect.left() + .22 * pOptions->rect.width());
    actualRectangle.setRight(pOptions->rect.right() - .22 * pOptions->rect.width());
    const double dRadius = actualRectangle.width() / 2;
    QPainterPath painterPath = QPainterPath(QPoint(actualRectangle.x(), actualRectangle.y() + dRadius));
    painterPath.arcTo(QRectF(actualRectangle.x(), actualRectangle.y(), 2 * dRadius, 2 * dRadius), 180, -180);
    painterPath.lineTo(actualRectangle.x() + 2 * dRadius, actualRectangle.y() + actualRectangle.height() - dRadius);
    painterPath.arcTo(QRectF(actualRectangle.x(), actualRectangle.y() + actualRectangle.height() - 2 * dRadius, 2 * dRadius, 2 * dRadius), 0, -180);
    painterPath.closeSubpath();
    pPainter->setClipPath(painterPath);
    pPainter->fillRect(actualRectangle, backgroundColor);

#else

    /* Draw background: */
    QColor backgroundColor = pal.color(QPalette::Active, QPalette::Window).darker(140);
    pPainter->fillRect(pOptions->rect, backgroundColor);

#endif

    /* Restore painter: */
    pPainter->restore();
}

void UIGraphicsScrollBarToken::mousePressEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::mousePressEvent(pEvent);

    /* Accept event to be able to receive mouse move events: */
    pEvent->accept();
}

void UIGraphicsScrollBarToken::mouseMoveEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::mouseMoveEvent(pEvent);

    /* Let listeners know about our mouse move events. */
    emit sigMouseMoved(mapToParent(pEvent->pos()));
}

void UIGraphicsScrollBarToken::hoverMoveEvent(QGraphicsSceneHoverEvent *)
{
    if (!m_fHovered)
        m_fHovered = true;
}

void UIGraphicsScrollBarToken::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    if (m_fHovered)
        m_fHovered = false;
}

void UIGraphicsScrollBarToken::prepare()
{
    setAcceptHoverEvents(true);

    updateExtent();
    resize(minimumSizeHint());
}

void UIGraphicsScrollBarToken::updateExtent()
{
    m_iExtent = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    updateGeometry();
}


/*********************************************************************************************************************************
*   Class UIGraphicsScrollBar implementation.                                                                                    *
*********************************************************************************************************************************/

UIGraphicsScrollBar::UIGraphicsScrollBar(Qt::Orientation enmOrientation, bool fAutoHideMode, QGraphicsScene *pScene)
    : m_enmOrientation(enmOrientation)
    , m_fAutoHideMode(fAutoHideMode)
    , m_iExtent(-1)
    , m_iMinimum(0)
    , m_iMaximum(100)
    , m_iValue(0)
    , m_pButton1(0)
    , m_pButton2(0)
    , m_pToken(0)
    , m_fHovered(false)
    , m_iHoverOnTimerId(0)
    , m_iHoverOffTimerId(0)
    , m_iHoveringValue(0)
    , m_fScrollInProgress(false)
#ifdef VBOX_WS_MAC
    , m_fRevealed(false)
    , m_iRevealingValue(m_fAutoHideMode ? 0 : 50)
    , m_iRevealOnTimerId(0)
    , m_iRevealOffTimerId(0)
#endif
{
    pScene->addItem(this);
    prepare();
}

UIGraphicsScrollBar::UIGraphicsScrollBar(Qt::Orientation enmOrientation, bool fAutoHideMode, QIGraphicsWidget *pParent /* = 0 */)
    : QIGraphicsWidget(pParent)
    , m_enmOrientation(enmOrientation)
    , m_fAutoHideMode(fAutoHideMode)
    , m_iExtent(-1)
    , m_iMinimum(0)
    , m_iMaximum(100)
    , m_iValue(0)
    , m_pButton1(0)
    , m_pButton2(0)
    , m_pToken(0)
    , m_fHovered(false)
    , m_iHoverOnTimerId(0)
    , m_iHoverOffTimerId(0)
    , m_iHoveringValue(0)
    , m_fScrollInProgress(false)
#ifdef VBOX_WS_MAC
    , m_fRevealed(false)
    , m_iRevealingValue(m_fAutoHideMode ? 0 : 50)
    , m_iRevealOnTimerId(0)
    , m_iRevealOffTimerId(0)
#endif
{
    prepare();
}

QSizeF UIGraphicsScrollBar::minimumSizeHint() const
{
    /* Calculate minimum size-hint depending on orientation: */
    switch (m_enmOrientation)
    {
        case Qt::Horizontal: return QSizeF(3 * m_iExtent, m_iExtent);
        case Qt::Vertical:   return QSizeF(m_iExtent, 3 * m_iExtent);
    }

    /* Call to base-class: */
    return QIGraphicsWidget::minimumSizeHint();
}

int UIGraphicsScrollBar::step() const
{
    return 2 * QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
}

int UIGraphicsScrollBar::pageStep() const
{
    return 3 * step();
}

void UIGraphicsScrollBar::setMinimum(int iMinimum)
{
    m_iMinimum = iMinimum;
    if (m_iMaximum < m_iMinimum)
        m_iMaximum = m_iMinimum;
    if (m_iValue < m_iMinimum)
    {
        m_iValue = m_iMinimum;
        emit sigValueChanged(m_iValue);
    }
    layoutToken();
}

int UIGraphicsScrollBar::minimum() const
{
    return m_iMinimum;
}

void UIGraphicsScrollBar::setMaximum(int iMaximum)
{
    m_iMaximum = iMaximum;
    if (m_iMinimum > m_iMaximum)
        m_iMinimum = m_iMaximum;
    if (m_iValue > m_iMaximum)
    {
        m_iValue = m_iMaximum;
        emit sigValueChanged(m_iValue);
    }
    layoutToken();
}

int UIGraphicsScrollBar::maximum() const
{
    return m_iMaximum;
}

void UIGraphicsScrollBar::setValue(int iValue)
{
    if (iValue > m_iMaximum)
        iValue = m_iMaximum;
    if (iValue < m_iMinimum)
        iValue = m_iMinimum;
    m_iValue = iValue;
    emit sigValueChanged(m_iValue);
    layoutToken();
}

int UIGraphicsScrollBar::value() const
{
    return m_iValue;
}

void UIGraphicsScrollBar::scrollTo(const QPointF &desiredPos, int iDelay /* = 500 */)
{
    /* Prepare current, desired and intermediate positions: */
    const QPointF currentPos = actualTokenPosition();
    const int iCurrentY = currentPos.y();
    const int iCurrentX = currentPos.x();
    const int iDesiredY = desiredPos.y();
    const int iDesiredX = desiredPos.x();
    QPointF intermediatePos;

    /* Calculate intermediate position depending on orientation: */
    switch (m_enmOrientation)
    {
        case Qt::Horizontal:
        {
            if (iCurrentX < iDesiredX)
            {
                intermediatePos.setY(desiredPos.y());
                intermediatePos.setX(iCurrentX + pageStep() < iDesiredX ? iCurrentX + pageStep() : iDesiredX);
            }
            else if (iCurrentX > iDesiredX)
            {
                intermediatePos.setY(desiredPos.y());
                intermediatePos.setX(iCurrentX - pageStep() > iDesiredX ? iCurrentX - pageStep() : iDesiredX);
            }
            break;
        }
        case Qt::Vertical:
        {
            if (iCurrentY < iDesiredY)
            {
                intermediatePos.setX(desiredPos.x());
                intermediatePos.setY(iCurrentY + pageStep() < iDesiredY ? iCurrentY + pageStep() : iDesiredY);
            }
            else if (iCurrentY > iDesiredY)
            {
                intermediatePos.setX(desiredPos.x());
                intermediatePos.setY(iCurrentY - pageStep() > iDesiredY ? iCurrentY - pageStep() : iDesiredY);
            }
            break;
        }
    }

    /* Move token to intermediate position: */
    if (!intermediatePos.isNull())
        sltTokenMoved(intermediatePos);

    /* Continue, if we haven't reached required position: */
    if ((intermediatePos != desiredPos) && m_fScrollInProgress)
        QTimer::singleShot(iDelay, ScrollFunctor(this, desiredPos));
}

void UIGraphicsScrollBar::resizeEvent(QGraphicsSceneResizeEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::resizeEvent(pEvent);

    /* Layout widgets: */
    layoutWidgets();
}

void UIGraphicsScrollBar::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *)
{
    /* Acquire rectangle: */
    const QRect rectangle = pOptions->rect;
    /* Paint background: */
    paintBackground(pPainter, rectangle);
}

void UIGraphicsScrollBar::mousePressEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::mousePressEvent(pEvent);

    /* Mark event accepted so that it couldn't
     * influence underlying widgets: */
    pEvent->accept();

    /* Start scrolling sequence: */
    m_fScrollInProgress = true;
    scrollTo(pEvent->pos());
}

void UIGraphicsScrollBar::mouseReleaseEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::mousePressEvent(pEvent);

    /* Mark event accepted so that it couldn't
     * influence underlying widgets: */
    pEvent->accept();

    /* Stop scrolling if any: */
    m_fScrollInProgress = false;
}

void UIGraphicsScrollBar::hoverMoveEvent(QGraphicsSceneHoverEvent *)
{
    /* Only if not yet hovered, that way we
     * make sure trigger emitted just once: */
    if (!m_fHovered)
    {
        /* Start hover-on timer, handled in timerEvent() below: */
        m_iHoverOnTimerId = startTimer(m_fAutoHideMode ? 400 : 100);
        m_fHovered = true;
    }
    /* Update in any case: */
    update();
}

void UIGraphicsScrollBar::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    /* Only if it's still hovered, that way we
     * make sure trigger emitted just once: */
    if (m_fHovered)
    {
        /* Start hover-off timer, handled in timerEvent() below: */
        m_iHoverOffTimerId = startTimer(m_fAutoHideMode ? 1000 : 100);
        m_fHovered = false;
    }
    /* Update in any case: */
    update();
}

void UIGraphicsScrollBar::timerEvent(QTimerEvent *pEvent)
{
    /* Kill timer in any case: */
    const int iTimerId = pEvent->timerId();
    killTimer(iTimerId);

    /* If that is hover-on timer: */
    if (m_iHoverOnTimerId != 0 && iTimerId == m_iHoverOnTimerId)
    {
        /* Wait for timer no more: */
        m_iHoverOnTimerId = 0;
        /* Emit hover-on trigger if hovered: */
        if (m_fHovered)
            emit sigHoverEnter();
        /* Update in any case: */
        update();
    }

    else

    /* If that is hover-off timer: */
    if (m_iHoverOffTimerId != 0 && iTimerId == m_iHoverOffTimerId)
    {
        /* Wait for timer no more: */
        m_iHoverOffTimerId = 0;
        /* Emit hover-off trigger if not hovered: */
        if (!m_fHovered && !m_pToken->isHovered())
            emit sigHoverLeave();
        /* Update in any case: */
        update();
    }

#ifdef VBOX_WS_MAC

    else

    /* If that is reveal-in timer: */
    if (m_iRevealOnTimerId != 0 && iTimerId == m_iRevealOnTimerId)
    {
        /* Wait for timer no more: */
        m_iRevealOnTimerId = 0;
    }

    else

    /* If that is reveal-out timer: */
    if (m_iRevealOffTimerId != 0 && iTimerId == m_iRevealOffTimerId)
    {
        /* Wait for timer no more: */
        m_iRevealOffTimerId = 0;
        /* Emit reveal-out signal if not hovered or was reinvoked not long time ago: */
        if (!m_fHovered && !m_pToken->isHovered() && m_iRevealOnTimerId == 0)
            emit sigRevealLeave();
        /* Restart timer otherwise: */
        else
            m_iRevealOffTimerId = startTimer(m_fAutoHideMode ? 2000 : 100);
        /* Update in any case: */
        update();
    }

#endif
}

void UIGraphicsScrollBar::sltButton1Clicked()
{
    setValue(value() - step());
}

void UIGraphicsScrollBar::sltButton2Clicked()
{
    setValue(value() + step());
}

void UIGraphicsScrollBar::sltTokenMoved(const QPointF &pos)
{
    /* Depending on orientation: */
    double dRatio = 0;
    switch (m_enmOrientation)
    {
        case Qt::Horizontal:
        {
            /* We have to calculate the X coord of the token, leaving Y untouched: */
#ifdef VBOX_WS_MAC
            const int iMin = 0;
            const int iMax = size().width() - 2 * m_iExtent;
#else
            const int iMin = m_iExtent;
            const int iMax = size().width() - 2 * m_iExtent;
#endif
            int iX = pos.x() - m_iExtent / 2;
            iX = qMax(iX, iMin);
            iX = qMin(iX, iMax);
            /* And then calculate new ratio to update value finally: */
            dRatio = iMax > iMin ? (double)(iX - iMin) / (iMax - iMin) : 0;
            break;
        }
        case Qt::Vertical:
        {
            /* We have to calculate the Y coord of the token, leaving X untouched: */
#ifdef VBOX_WS_MAC
            const int iMin = 0;
            const int iMax = size().height() - 2 * m_iExtent;
#else
            const int iMin = m_iExtent;
            const int iMax = size().height() - 2 * m_iExtent;
#endif
            int iY = pos.y() - m_iExtent / 2;
            iY = qMax(iY, iMin);
            iY = qMin(iY, iMax);
            /* And then calculate new ratio to update value finally: */
            dRatio = iMax > iMin ? (double)(iY - iMin) / (iMax - iMin) : 0;
            break;
        }
    }

    /* Update value according to calculated ratio: */
    m_iValue = dRatio * (m_iMaximum - m_iMinimum) + m_iMinimum;
    emit sigValueChanged(m_iValue);
    layoutToken();
}

void UIGraphicsScrollBar::sltStateLeftDefault()
{
    if (m_pButton1)
        m_pButton1->hide();
    if (m_pButton2)
        m_pButton2->hide();
    if (m_pToken)
        m_pToken->hide();
}

void UIGraphicsScrollBar::sltStateLeftHovered()
{
    if (m_pButton1)
        m_pButton1->hide();
    if (m_pButton2)
        m_pButton2->hide();
    if (m_pToken)
        m_pToken->hide();
}

void UIGraphicsScrollBar::sltStateEnteredDefault()
{
    if (m_pButton1)
        m_pButton1->hide();
    if (m_pButton2)
        m_pButton2->hide();
    if (m_pToken)
        m_pToken->hide();
}

void UIGraphicsScrollBar::sltStateEnteredHovered()
{
    if (m_pButton1)
        m_pButton1->show();
    if (m_pButton2)
        m_pButton2->show();
    if (m_pToken)
        m_pToken->show();
}

#ifdef VBOX_WS_MAC
void UIGraphicsScrollBar::sltHandleRevealingStart()
{
    /* Only if not yet revealed, that way we
     * make sure trigger emitted just once: */
    if (!m_fRevealed)
    {
        /* Mark token revealed: */
        m_fRevealed = true;
        /* Emit reveal signal immediately: */
        emit sigRevealEnter();
    }

    /* Restart fresh sustain timer: */
    m_iRevealOnTimerId = startTimer(m_fAutoHideMode ? 1000 : 100);
}

void UIGraphicsScrollBar::sltStateEnteredFaded()
{
    /* Mark token faded: */
    m_fRevealed = false;
}

void UIGraphicsScrollBar::sltStateEnteredRevealed()
{
    /* Start reveal-out timer: */
    m_iRevealOffTimerId = startTimer(m_fAutoHideMode ? 2000 : 100);
}
#endif /* VBOX_WS_MAC */

void UIGraphicsScrollBar::prepare()
{
    /* Configure self: */
    setAcceptHoverEvents(true);

    /* Prepare/layout widgets: */
    prepareWidgets();
    updateExtent();
    layoutWidgets();

    /* Prepare animation: */
    prepareAnimation();
}

void UIGraphicsScrollBar::prepareWidgets()
{
    prepareButtons();
    prepareToken();
}

void UIGraphicsScrollBar::prepareButtons()
{
#ifndef VBOX_WS_MAC
    /* Create buttons depending on orientation: */
    switch (m_enmOrientation)
    {
        case Qt::Horizontal:
        {
            m_pButton1 = new UIGraphicsButton(this, UIIconPool::iconSet(":/arrow_left_10px.png"));
            m_pButton2 = new UIGraphicsButton(this, UIIconPool::iconSet(":/arrow_right_10px.png"));
            break;
        }
        case Qt::Vertical:
        {
            m_pButton1 = new UIGraphicsButton(this, UIIconPool::iconSet(":/arrow_up_10px.png"));
            m_pButton2 = new UIGraphicsButton(this, UIIconPool::iconSet(":/arrow_down_10px.png"));
            break;
        }
    }

    if (m_pButton1)
    {
        /* We use 10px icons, not 16px, let buttons know that: */
        m_pButton1->setIconScaleIndex((double)10 / 16);
        /* Also we want to have buttons react on mouse presses for auto-repeat feature: */
        m_pButton1->setClickPolicy(UIGraphicsButton::ClickPolicy_OnPress);
        connect(m_pButton1, &UIGraphicsButton::sigButtonClicked,
                this, &UIGraphicsScrollBar::sltButton1Clicked);
    }
    if (m_pButton2)
    {
        /* We use 10px icons, not 16px, let buttons know that: */
        m_pButton2->setIconScaleIndex((double)10 / 16);
        /* Also we want to have buttons react on mouse presses for auto-repeat feature: */
        m_pButton2->setClickPolicy(UIGraphicsButton::ClickPolicy_OnPress);
        connect(m_pButton2, &UIGraphicsButton::sigButtonClicked,
                this, &UIGraphicsScrollBar::sltButton2Clicked);
    }
#endif
}

void UIGraphicsScrollBar::prepareToken()
{
    /* Create token: */
    m_pToken = new UIGraphicsScrollBarToken(m_enmOrientation, this);
    if (m_pToken)
        connect(m_pToken, &UIGraphicsScrollBarToken::sigMouseMoved,
                this, &UIGraphicsScrollBar::sltTokenMoved);
}

void UIGraphicsScrollBar::prepareAnimation()
{
    prepareHoveringAnimation();
#ifdef VBOX_WS_MAC
    prepareRevealingAnimation();
#endif
}

void UIGraphicsScrollBar::prepareHoveringAnimation()
{
    /* Create hovering animation machine: */
    QStateMachine *pHoveringMachine = new QStateMachine(this);
    if (pHoveringMachine)
    {
        /* Create 'default' state: */
        QState *pStateDefault = new QState(pHoveringMachine);
        /* Create 'hovered' state: */
        QState *pStateHovered = new QState(pHoveringMachine);

        /* Configure 'default' state: */
        if (pStateDefault)
        {
            /* When we entering default state => we assigning hoveringValue to 0: */
            pStateDefault->assignProperty(this, "hoveringValue", 0);
            connect(pStateDefault, &QState::propertiesAssigned, this, &UIGraphicsScrollBar::sltStateEnteredDefault);

            /* Add state transitions: */
            QSignalTransition *pDefaultToHovered = pStateDefault->addTransition(this, SIGNAL(sigHoverEnter()), pStateHovered);
            if (pDefaultToHovered)
            {
                connect(pDefaultToHovered, &QSignalTransition::triggered, this, &UIGraphicsScrollBar::sltStateLeftDefault);

                /* Create forward animation: */
                QPropertyAnimation *pHoveringAnimationForward = new QPropertyAnimation(this, "hoveringValue", this);
                if (pHoveringAnimationForward)
                {
                    pHoveringAnimationForward->setDuration(200);
                    pHoveringAnimationForward->setStartValue(0);
                    pHoveringAnimationForward->setEndValue(100);

                    /* Add to transition: */
                    pDefaultToHovered->addAnimation(pHoveringAnimationForward);
                }
            }
        }

        /* Configure 'hovered' state: */
        if (pStateHovered)
        {
            /* When we entering hovered state => we assigning hoveringValue to 100: */
            pStateHovered->assignProperty(this, "hoveringValue", 100);
            connect(pStateHovered, &QState::propertiesAssigned, this, &UIGraphicsScrollBar::sltStateEnteredHovered);

            /* Add state transitions: */
            QSignalTransition *pHoveredToDefault = pStateHovered->addTransition(this, SIGNAL(sigHoverLeave()), pStateDefault);
            if (pHoveredToDefault)
            {
                connect(pHoveredToDefault, &QSignalTransition::triggered, this, &UIGraphicsScrollBar::sltStateLeftHovered);

                /* Create backward animation: */
                QPropertyAnimation *pHoveringAnimationBackward = new QPropertyAnimation(this, "hoveringValue", this);
                if (pHoveringAnimationBackward)
                {
                    pHoveringAnimationBackward->setDuration(200);
                    pHoveringAnimationBackward->setStartValue(100);
                    pHoveringAnimationBackward->setEndValue(0);

                    /* Add to transition: */
                    pHoveredToDefault->addAnimation(pHoveringAnimationBackward);
                }
            }
        }

        /* Initial state is 'default': */
        pHoveringMachine->setInitialState(pStateDefault);
        /* Start state-machine: */
        pHoveringMachine->start();
    }
}

#ifdef VBOX_WS_MAC
void UIGraphicsScrollBar::prepareRevealingAnimation()
{
    /* Create revealing animation machine: */
    QStateMachine *pRevealingMachine = new QStateMachine(this);
    if (pRevealingMachine)
    {
        /* Create 'faded' state: */
        QState *pStateFaded = new QState(pRevealingMachine);
        /* Create 'revealed' state: */
        QState *pStateRevealed = new QState(pRevealingMachine);

        /* Configure 'faded' state: */
        if (pStateFaded)
        {
            /* When we entering fade state => we assigning revealingValue to 0: */
            pStateFaded->assignProperty(this, "revealingValue", m_fAutoHideMode ? 0 : 50);
            connect(pStateFaded, &QState::propertiesAssigned, this, &UIGraphicsScrollBar::sltStateEnteredFaded);

            /* Add state transitions: */
            QSignalTransition *pFadeToRevealed = pStateFaded->addTransition(this, SIGNAL(sigRevealEnter()), pStateRevealed);
            if (pFadeToRevealed)
            {
                /* Create forward animation: */
                QPropertyAnimation *pRevealingAnimationForward = new QPropertyAnimation(this, "revealingValue", this);
                if (pRevealingAnimationForward)
                {
                    pRevealingAnimationForward->setDuration(200);
                    pRevealingAnimationForward->setStartValue(m_fAutoHideMode ? 0 : 50);
                    pRevealingAnimationForward->setEndValue(100);

                    /* Add to transition: */
                    pFadeToRevealed->addAnimation(pRevealingAnimationForward);
                }
            }
        }

        /* Configure 'revealed' state: */
        if (pStateRevealed)
        {
            /* When we entering revealed state => we assigning revealingValue to 100: */
            pStateRevealed->assignProperty(this, "revealingValue", 100);
            connect(pStateRevealed, &QState::propertiesAssigned, this, &UIGraphicsScrollBar::sltStateEnteredRevealed);

            /* Add state transitions: */
            QSignalTransition *pRevealedToFaded = pStateRevealed->addTransition(this, SIGNAL(sigRevealLeave()), pStateFaded);
            if (pRevealedToFaded)
            {
                /* Create backward animation: */
                QPropertyAnimation *pRevealingAnimationBackward = new QPropertyAnimation(this, "revealingValue", this);
                if (pRevealingAnimationBackward)
                {
                    pRevealingAnimationBackward->setDuration(200);
                    pRevealingAnimationBackward->setStartValue(100);
                    pRevealingAnimationBackward->setEndValue(m_fAutoHideMode ? 0 : 50);

                    /* Add to transition: */
                    pRevealedToFaded->addAnimation(pRevealingAnimationBackward);
                }
            }
        }

        /* Initial state is 'fade': */
        pRevealingMachine->setInitialState(pStateFaded);
        /* Start state-machine: */
        pRevealingMachine->start();
    }

    /* Install self-listener: */
    connect(this, &UIGraphicsScrollBar::sigValueChanged, this, &UIGraphicsScrollBar::sltHandleRevealingStart);
}
#endif /* VBOX_WS_MAC */

void UIGraphicsScrollBar::updateExtent()
{
    /* Make sure extent value is not smaller than the button size: */
    m_iExtent = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
#ifndef VBOX_WS_MAC
    m_iExtent = qMax(m_iExtent, (int)m_pButton1->minimumSizeHint().width());
    m_iExtent = qMax(m_iExtent, (int)m_pButton2->minimumSizeHint().width());
#endif
    updateGeometry();
}

void UIGraphicsScrollBar::layoutWidgets()
{
    layoutButtons();
    layoutToken();
}

void UIGraphicsScrollBar::layoutButtons()
{
#ifndef VBOX_WS_MAC
    // WORKAROUND:
    // We are calculating proper button shift delta, because
    // button size can be smaller than scroll-bar extent value.

    int iDelta1 = 0;
    if (m_iExtent > m_pButton1->minimumSizeHint().width())
        iDelta1 = (m_iExtent - m_pButton1->minimumSizeHint().width() + 1) / 2;
    m_pButton1->setPos(iDelta1, iDelta1);

    int iDelta2 = 0;
    if (m_iExtent > m_pButton2->minimumSizeHint().width())
        iDelta2 = (m_iExtent - m_pButton2->minimumSizeHint().width() + 1) / 2;
    m_pButton2->setPos(size().width() - m_iExtent + iDelta2, size().height() - m_iExtent + iDelta2);
#endif
}

void UIGraphicsScrollBar::layoutToken()
{
    m_pToken->setPos(actualTokenPosition());
    update();
}

QPoint UIGraphicsScrollBar::actualTokenPosition() const
{
    /* We calculating ratio on the basis of current/minimum/maximum values: */
    const double dRatio = m_iMaximum > m_iMinimum ? (double)(m_iValue - m_iMinimum) / (m_iMaximum - m_iMinimum) : 0;

    /* Prepare result: */
    QPoint position;

    /* Depending on orientation: */
    switch (m_enmOrientation)
    {
        case Qt::Horizontal:
        {
            /* We have to adjust the X coord of the token, leaving Y unchanged: */
#ifdef VBOX_WS_MAC
            const int iMin = 0;
            const int iMax = size().width() - 2 * m_iExtent;
#else
            const int iMin = m_iExtent;
            const int iMax = size().width() - 2 * m_iExtent;
#endif
            int iX = dRatio * (iMax - iMin) + iMin;
            position = QPoint(iX, 0);
            break;
        }
        case Qt::Vertical:
        {
            /* We have to adjust the Y coord of the token, leaving X unchanged: */
#ifdef VBOX_WS_MAC
            const int iMin = 0;
            const int iMax = size().height() - 2 * m_iExtent;
#else
            const int iMin = m_iExtent;
            const int iMax = size().height() - 2 * m_iExtent;
#endif
            int iY = dRatio * (iMax - iMin) + iMin;
            position = QPoint(0, iY);
            break;
        }
    }

    /* Return result: */
    return position;
}

void UIGraphicsScrollBar::paintBackground(QPainter *pPainter, const QRect &rectangle) const
{
    /* Save painter: */
    pPainter->save();

    /* Prepare color: */
    const QPalette pal = QApplication::palette();

#ifdef VBOX_WS_MAC

    /* Draw background if necessary: */
    pPainter->save();
    QColor windowColor = pal.color(QPalette::Active, QPalette::Window);
    if (m_fAutoHideMode)
        windowColor.setAlpha(255 * ((double)m_iHoveringValue / 100));
    pPainter->fillRect(rectangle, windowColor);
    pPainter->restore();

    /* Draw frame if necessary: */
    pPainter->save();
    QColor frameColor = pal.color(QPalette::Active, QPalette::Window);
    if (m_fAutoHideMode)
        frameColor.setAlpha(255 * ((double)m_iHoveringValue / 100));
    frameColor = frameColor.darker(120);
    pPainter->setPen(frameColor);
    pPainter->drawLine(rectangle.topLeft(), rectangle.bottomLeft());
    pPainter->restore();

    /* Emulate token when necessary: */
    if (m_iHoveringValue < 100)
    {
        QColor tokenColor = pal.color(QPalette::Active, QPalette::Window);
        tokenColor.setAlpha(255 * ((double)m_iRevealingValue / 100));
        tokenColor = tokenColor.darker(190);
        QRectF tokenRectangle = QRect(actualTokenPosition(), QSize(m_iExtent, 2 * m_iExtent));
        QRectF actualRectangle = tokenRectangle;
        if (m_fAutoHideMode)
        {
            actualRectangle.setLeft(tokenRectangle.left() + .22 * tokenRectangle.width() + .22 * tokenRectangle.width() * ((double)100 - m_iHoveringValue) / 100);
            actualRectangle.setRight(tokenRectangle.right() - .22 * tokenRectangle.width() + .22 * tokenRectangle.width() * ((double)100 - m_iHoveringValue) / 100 - 1);
        }
        else
        {
            actualRectangle.setLeft(tokenRectangle.left() + .22 * tokenRectangle.width());
            actualRectangle.setRight(tokenRectangle.right() - .22 * tokenRectangle.width() - 1);
        }
        const double dRadius = actualRectangle.width() / 2;
        QPainterPath painterPath = QPainterPath(QPoint(actualRectangle.x(), actualRectangle.y() + dRadius));
        painterPath.arcTo(QRectF(actualRectangle.x(), actualRectangle.y(), 2 * dRadius, 2 * dRadius), 180, -180);
        painterPath.lineTo(actualRectangle.x() + 2 * dRadius, actualRectangle.y() + actualRectangle.height() - dRadius);
        painterPath.arcTo(QRectF(actualRectangle.x(), actualRectangle.y() + actualRectangle.height() - 2 * dRadius, 2 * dRadius, 2 * dRadius), 0, -180);
        painterPath.closeSubpath();
        pPainter->setClipPath(painterPath);
        pPainter->fillRect(actualRectangle, tokenColor);
    }

#else

    /* Draw background: */
    QColor backgroundColor = pal.color(QPalette::Active, QPalette::Window);
    backgroundColor.setAlpha(50 + (double)m_iHoveringValue / 100 * 150);
    QRect actualRectangle = rectangle;
    actualRectangle.setLeft(actualRectangle.left() + .85 * actualRectangle.width() * ((double)100 - m_iHoveringValue) / 100);
    pPainter->fillRect(actualRectangle, backgroundColor);

    /* Emulate token when necessary: */
    if (m_iHoveringValue < 100)
    {
        QColor tokenColor = pal.color(QPalette::Active, QPalette::Window).darker(140);
        QRect tokenRectangle = QRect(actualTokenPosition(), QSize(m_iExtent, m_iExtent));
        tokenRectangle.setLeft(tokenRectangle.left() + .85 * tokenRectangle.width() * ((double)100 - m_iHoveringValue) / 100);
        pPainter->fillRect(tokenRectangle, tokenColor);
    }

#endif

    /* Restore painter: */
    pPainter->restore();
}


#include "UIGraphicsScrollBar.moc"
