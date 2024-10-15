/* $Id: UISlidingWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISlidingWidget class implementation.
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

/* Qt includes: */
#include <QEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIAnimationFramework.h"
#include "UISlidingWidget.h"


UISlidingWidget::UISlidingWidget(Qt::Orientation enmOrientation, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmOrientation(enmOrientation)
    , m_enmState(State_Start)
    , m_pAnimation(0)
    , m_pWidget(0)
    , m_pLayout(0)
    , m_pWidget1(0)
    , m_pWidget2(0)
{
    /* Prepare: */
    prepare();
}

QSize UISlidingWidget::minimumSizeHint() const
{
    /* Return maximum of minimum size-hints: */
    QSize msh = QSize();
    if (m_pWidget1)
        msh = msh.expandedTo(m_pWidget1->minimumSizeHint());
    if (m_pWidget2)
        msh = msh.expandedTo(m_pWidget2->minimumSizeHint());
    return msh;
}

void UISlidingWidget::setWidgets(QWidget *pWidget1, QWidget *pWidget2)
{
    /* Clear animation/widgets if any: */
    delete m_pAnimation;
    delete m_pWidget1;
    delete m_pWidget2;

    /* Remember widgets: */
    m_pWidget1 = pWidget1;
    m_pWidget2 = pWidget2;
    m_pLayout->addWidget(m_pWidget1);
    m_pLayout->addWidget(m_pWidget2);

    /* Install new animation: */
    m_pAnimation = UIAnimation::installPropertyAnimation(this,
                                                         "widgetGeometry",
                                                         "startWidgetGeometry", "finalWidgetGeometry",
                                                         SIGNAL(sigForward()), SIGNAL(sigBackward()));
    connect(m_pAnimation, &UIAnimation::sigStateEnteredStart, this, &UISlidingWidget::sltSetStateToStart);
    connect(m_pAnimation, &UIAnimation::sigStateEnteredFinal, this, &UISlidingWidget::sltSetStateToFinal);

    /* Update animation: */
    updateAnimation();
    /* Update widget geometry: */
    m_pWidget->setGeometry(m_enmState == State_Final ? m_finalWidgetGeometry : m_startWidgetGeometry);
}

bool UISlidingWidget::event(QEvent *pEvent)
{
    /* Process desired events: */
    switch (pEvent->type())
    {
        case QEvent::LayoutRequest:
        {
            // WORKAROUND:
            // Since we are not connected to
            // our children LayoutRequest,
            // we should update geometry
            // ourselves.
            updateGeometry();
            break;
        }
        default:
            break;
    }

    /* Call to base class: */
    return QWidget::event(pEvent);
}

void UISlidingWidget::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::resizeEvent(pEvent);

    /* Update animation: */
    updateAnimation();
    /* Update widget geometry: */
    m_pWidget->setGeometry(m_enmState == State_Final ? m_finalWidgetGeometry : m_startWidgetGeometry);
}

void UISlidingWidget::prepare()
{
    /* Create private sliding widget: */
    m_pWidget = new QWidget(this);
    if (m_pWidget)
    {
        /* Create layout: */
        switch (m_enmOrientation)
        {
            case Qt::Horizontal: m_pLayout = new QHBoxLayout(m_pWidget); break;
            case Qt::Vertical:   m_pLayout = new QVBoxLayout(m_pWidget); break;
        }
        if (m_pLayout)
        {
            /* Configure layout: */
            m_pLayout->setSpacing(0);
            m_pLayout->setContentsMargins(0, 0, 0, 0);
        }
    }

    /* Update animation: */
    updateAnimation();
    /* Update widget geometry: */
    m_pWidget->setGeometry(m_enmState == State_Final ? m_finalWidgetGeometry : m_startWidgetGeometry);
}

void UISlidingWidget::updateAnimation()
{
    /* Recalculate sub-window geometry animation boundaries based on size-hint: */
    switch (m_enmOrientation)
    {
        case Qt::Horizontal:
        {
            m_startWidgetGeometry = QRect(  0,           0,
                                            2 * width(), height());
            m_finalWidgetGeometry = QRect(- width(),     0,
                                            2 * width(), height());
            break;
        }
        case Qt::Vertical:
        {
            m_startWidgetGeometry = QRect(0,         0,
                                          width(),   2 * height());
            m_finalWidgetGeometry = QRect(0,       - height(),
                                          width(),   2 * height());
            break;
        }
    }

    /* Update animation finally: */
    if (m_pAnimation)
        m_pAnimation->update();
}

void UISlidingWidget::setWidgetGeometry(const QRect &rect)
{
    /* Apply widget geometry: */
    m_pWidget->setGeometry(rect);
}

QRect UISlidingWidget::widgetGeometry() const
{
    /* Return widget geometry: */
    return m_pWidget->geometry();
}
