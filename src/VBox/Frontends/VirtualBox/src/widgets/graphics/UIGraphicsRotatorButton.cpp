/* $Id: UIGraphicsRotatorButton.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsRotatorButton class definition.
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

/* Qt includes: */
#include <QStateMachine>
#include <QPropertyAnimation>
#include <QSignalTransition>
#include <QMouseEventTransition>

/* GUI includes: */
#include "UIGraphicsRotatorButton.h"
#include "UIIconPool.h"


UIGraphicsRotatorButton::UIGraphicsRotatorButton(QIGraphicsWidget *pParent,
                                                 const QString &strPropertyName,
                                                 bool fToggled,
                                                 bool fReflected /* = false */,
                                                 int iAnimationDuration /* = 300 */)
    : UIGraphicsButton(pParent, UIIconPool::iconSet(":/expanding_collapsing_16px.png"))
    , m_fReflected(fReflected)
    , m_state(fToggled ? UIGraphicsRotatorButtonState_Rotated : UIGraphicsRotatorButtonState_Default)
    , m_pAnimationMachine(0)
    , m_iAnimationDuration(iAnimationDuration)
    , m_pForwardButtonAnimation(0)
    , m_pBackwardButtonAnimation(0)
    , m_pForwardSubordinateAnimation(0)
    , m_pBackwardSubordinateAnimation(0)
{
    /* Configure: */
    setAutoHandleButtonClick(true);

    /* Create state machine: */
    m_pAnimationMachine = new QStateMachine(this);
    /* Create 'default' state: */
    QState *pStateDefault = new QState(m_pAnimationMachine);
    pStateDefault->assignProperty(this, "state", QVariant::fromValue(UIGraphicsRotatorButtonState_Default));
    pStateDefault->assignProperty(this, "rotation", m_fReflected ? 180 : 0);
    /* Create 'animating' state: */
    QState *pStateAnimating = new QState(m_pAnimationMachine);
    pStateAnimating->assignProperty(this, "state", QVariant::fromValue(UIGraphicsRotatorButtonState_Animating));
    /* Create 'rotated' state: */
    QState *pStateRotated = new QState(m_pAnimationMachine);
    pStateRotated->assignProperty(this, "state", QVariant::fromValue(UIGraphicsRotatorButtonState_Rotated));
    pStateRotated->assignProperty(this, "rotation", 90);

    /* Forward button animation: */
    m_pForwardButtonAnimation = new QPropertyAnimation(this, "rotation", this);
    m_pForwardButtonAnimation->setDuration(m_iAnimationDuration);
    m_pForwardButtonAnimation->setStartValue(m_fReflected ? 180 : 0);
    m_pForwardButtonAnimation->setEndValue(90);
    /* Backward button animation: */
    m_pBackwardButtonAnimation = new QPropertyAnimation(this, "rotation", this);
    m_pBackwardButtonAnimation->setDuration(m_iAnimationDuration);
    m_pBackwardButtonAnimation->setStartValue(90);
    m_pBackwardButtonAnimation->setEndValue(m_fReflected ? 180 : 0);

    /* Forward subordinate animation: */
    m_pForwardSubordinateAnimation = new QPropertyAnimation(pParent, strPropertyName.toLatin1(), this);
    m_pForwardSubordinateAnimation->setDuration(m_iAnimationDuration);
    m_pForwardSubordinateAnimation->setEasingCurve(QEasingCurve::InCubic);
    /* Backward subordinate animation: */
    m_pBackwardSubordinateAnimation = new QPropertyAnimation(pParent, strPropertyName.toLatin1(), this);
    m_pBackwardSubordinateAnimation->setDuration(m_iAnimationDuration);
    m_pBackwardSubordinateAnimation->setEasingCurve(QEasingCurve::InCubic);

    /* Default => Animating: */
    QSignalTransition *pDefaultToAnimating = pStateDefault->addTransition(this, SIGNAL(sigToAnimating()), pStateAnimating);
    pDefaultToAnimating->addAnimation(m_pForwardButtonAnimation);
    pDefaultToAnimating->addAnimation(m_pForwardSubordinateAnimation);
    /* Animating => Rotated: */
    connect(m_pForwardButtonAnimation, SIGNAL(finished()), this, SIGNAL(sigToRotated()), Qt::QueuedConnection);
    pStateAnimating->addTransition(this, SIGNAL(sigToRotated()), pStateRotated);

    /* Rotated => Animating: */
    QSignalTransition *pRotatedToAnimating = pStateRotated->addTransition(this, SIGNAL(sigToAnimating()), pStateAnimating);
    pRotatedToAnimating->addAnimation(m_pBackwardButtonAnimation);
    pRotatedToAnimating->addAnimation(m_pBackwardSubordinateAnimation);
    /* Animating => Default: */
    connect(m_pBackwardButtonAnimation, SIGNAL(finished()), this, SIGNAL(sigToDefault()), Qt::QueuedConnection);
    pStateAnimating->addTransition(this, SIGNAL(sigToDefault()), pStateDefault);

    /* Default => Rotated: */
    pStateDefault->addTransition(this, SIGNAL(sigToRotated()), pStateRotated);

    /* Rotated => Default: */
    pStateRotated->addTransition(this, SIGNAL(sigToDefault()), pStateDefault);

    /* Initial state is 'default': */
    m_pAnimationMachine->setInitialState(!fToggled ? pStateDefault : pStateRotated);
    /* Start state-machine: */
    m_pAnimationMachine->start();

    /* Refresh: */
    refresh();
}

void UIGraphicsRotatorButton::setAutoHandleButtonClick(bool fEnabled)
{
    /* Disconnect button-click signal: */
    disconnect(this, &UIGraphicsRotatorButton::sigButtonClicked, this, &UIGraphicsRotatorButton::sltButtonClicked);
    if (fEnabled)
    {
        /* Connect button-click signal: */
        connect(this, &UIGraphicsRotatorButton::sigButtonClicked, this, &UIGraphicsRotatorButton::sltButtonClicked);
    }
}

void UIGraphicsRotatorButton::setToggled(bool fToggled, bool fAnimated /* = true */)
{
    /* Not during animation: */
    if (isAnimationRunning())
        return;

    /* Make sure something has changed: */
    switch (state())
    {
        case UIGraphicsRotatorButtonState_Default:
        {
            if (!fToggled)
                return;
            break;
        }
        case UIGraphicsRotatorButtonState_Rotated:
        {
            if (fToggled)
                return;
            break;
        }
        default: break;
    }

    /* Should be animated? */
    if (fAnimated)
    {
        /* Rotation start: */
        emit sigRotationStart();
        emit sigToAnimating();
    }
    else
    {
        if (fToggled)
            emit sigToRotated();
        else
            emit sigToDefault();
    }
}

void UIGraphicsRotatorButton::setAnimationRange(int iStart, int iEnd)
{
    m_pForwardSubordinateAnimation->setStartValue(iStart);
    m_pForwardSubordinateAnimation->setEndValue(iEnd);
    m_pBackwardSubordinateAnimation->setStartValue(iEnd);
    m_pBackwardSubordinateAnimation->setEndValue(iStart);
}

bool UIGraphicsRotatorButton::isAnimationRunning() const
{
    return m_pForwardSubordinateAnimation->state() == QAbstractAnimation::Running ||
           m_pBackwardSubordinateAnimation->state() == QAbstractAnimation::Running;
}

void UIGraphicsRotatorButton::sltButtonClicked()
{
    /* Toggle state: */
    switch (state())
    {
        case UIGraphicsRotatorButtonState_Default: setToggled(true); break;
        case UIGraphicsRotatorButtonState_Rotated: setToggled(false); break;
        default: break;
    }
}

void UIGraphicsRotatorButton::refresh()
{
    /* Update rotation center: */
    QSizeF sh = minimumSizeHint();
    setTransformOriginPoint(sh.width() / 2, sh.height() / 2);
    /* Update rotation state: */
    updateRotationState();
    /* Call to base-class: */
    UIGraphicsButton::refresh();
}

void UIGraphicsRotatorButton::updateRotationState()
{
    switch (state())
    {
        case UIGraphicsRotatorButtonState_Default: setRotation(m_fReflected ? 180 : 0); break;
        case UIGraphicsRotatorButtonState_Rotated: setRotation(90); break;
        default: break;
    }
}

UIGraphicsRotatorButtonState UIGraphicsRotatorButton::state() const
{
    return m_state;
}

void UIGraphicsRotatorButton::setState(UIGraphicsRotatorButtonState state)
{
    m_state = state;
    switch (m_state)
    {
        case UIGraphicsRotatorButtonState_Default:
        {
            emit sigRotationFinish(false);
            break;
        }
        case UIGraphicsRotatorButtonState_Rotated:
        {
            emit sigRotationFinish(true);
            break;
        }
        default: break;
    }
}
