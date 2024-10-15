/* $Id: UIGraphicsRotatorButton.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsRotatorButton class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsRotatorButton_h
#define FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsRotatorButton_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIGraphicsButton.h"

/* Forward declarations: */
class QStateMachine;
class QPropertyAnimation;

/* Rotator graphics-button states: */
enum UIGraphicsRotatorButtonState
{
    UIGraphicsRotatorButtonState_Default,
    UIGraphicsRotatorButtonState_Animating,
    UIGraphicsRotatorButtonState_Rotated
};
Q_DECLARE_METATYPE(UIGraphicsRotatorButtonState);

/* Rotator graphics-button representation: */
class UIGraphicsRotatorButton : public UIGraphicsButton
{
    Q_OBJECT;
    Q_PROPERTY(UIGraphicsRotatorButtonState state READ state WRITE setState);

signals:

    /* Rotation internal stuff: */
    void sigToAnimating();
    void sigToRotated();
    void sigToDefault();

    /* Rotation external stuff: */
    void sigRotationStart();
    void sigRotationFinish(bool fRotated);

public:

    /* Constructor: */
    UIGraphicsRotatorButton(QIGraphicsWidget *pParent,
                            const QString &strPropertyName,
                            bool fToggled,
                            bool fReflected = false,
                            int iAnimationDuration = 300);

    /* API: Button-click stuff: */
    void setAutoHandleButtonClick(bool fEnabled);

    /* API: Toggle stuff: */
    void setToggled(bool fToggled, bool fAnimated = true);

    /* API: Subordinate animation stuff: */
    void setAnimationRange(int iStart, int iEnd);
    bool isAnimationRunning() const;

protected slots:

    /* Handler: Button-click stuff: */
    void sltButtonClicked();

protected:

    /* Helpers: Update stuff: */
    void refresh() RT_OVERRIDE RT_FINAL;

private:

    /* Helpers: Rotate stuff: */
    void updateRotationState();

    /* Property stiff: */
    UIGraphicsRotatorButtonState state() const;
    void setState(UIGraphicsRotatorButtonState state);

    /* Variables: */
    bool m_fReflected;
    UIGraphicsRotatorButtonState m_state;
    QStateMachine *m_pAnimationMachine;
    int m_iAnimationDuration;
    QPropertyAnimation *m_pForwardButtonAnimation;
    QPropertyAnimation *m_pBackwardButtonAnimation;
    QPropertyAnimation *m_pForwardSubordinateAnimation;
    QPropertyAnimation *m_pBackwardSubordinateAnimation;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsRotatorButton_h */
