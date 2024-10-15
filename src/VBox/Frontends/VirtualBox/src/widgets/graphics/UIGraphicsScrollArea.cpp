/* $Id: UIGraphicsScrollArea.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsScrollArea class implementation.
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
#include <QGraphicsScene>
#include <QGraphicsView>

/* GUI includes: */
#include "UIGraphicsScrollArea.h"
#include "UIGraphicsScrollBar.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils.h"
#endif


UIGraphicsScrollArea::UIGraphicsScrollArea(Qt::Orientation enmOrientation, QGraphicsScene *pScene /* = 0 */)
    : m_enmOrientation(enmOrientation)
    , m_fAutoHideMode(true)
    , m_pScrollBar(0)
    , m_pViewport(0)
{
    pScene->addItem(this);
    prepare();
}

UIGraphicsScrollArea::UIGraphicsScrollArea(Qt::Orientation enmOrientation, QIGraphicsWidget *pParent /* = 0 */)
    : QIGraphicsWidget(pParent)
    , m_enmOrientation(enmOrientation)
    , m_fAutoHideMode(true)
    , m_pScrollBar(0)
    , m_pViewport(0)
{
    prepare();
}

QSizeF UIGraphicsScrollArea::minimumSizeHint() const
{
    /* Minimum size-hint of scroll-bar by default: */
    QSizeF msh = m_pScrollBar->minimumSizeHint();
    if (m_pViewport)
    {
        switch (m_enmOrientation)
        {
            case Qt::Horizontal:
            {
                /* Expand it with viewport height: */
                const int iWidgetHeight = m_pViewport->size().height();
                if (m_fAutoHideMode)
                {
                    if (msh.height() < iWidgetHeight)
                        msh.setHeight(iWidgetHeight);
                }
                else
                    msh.setHeight(msh.height() + iWidgetHeight);
                break;
            }
            case Qt::Vertical:
            {
                /* Expand it with viewport width: */
                const int iWidgetWidth = m_pViewport->size().width();
                if (m_fAutoHideMode)
                {
                    if (msh.width() < iWidgetWidth)
                        msh.setWidth(iWidgetWidth);
                }
                else
                    msh.setWidth(msh.width() + iWidgetWidth);
                break;
            }
        }
    }
    return msh;
}

void UIGraphicsScrollArea::setViewport(QIGraphicsWidget *pViewport)
{
    /* Forget previous widget: */
    if (m_pViewport)
    {
        m_pViewport->removeEventFilter(this);
        m_pViewport->setParentItem(0);
        m_pViewport = 0;
    }

    /* Remember passed widget: */
    if (pViewport)
    {
        m_pViewport = pViewport;
        m_pViewport->setParentItem(this);
        m_pViewport->installEventFilter(this);
    }

    /* Layout widgets: */
    layoutWidgets();
}

QIGraphicsWidget *UIGraphicsScrollArea::viewport() const
{
    return m_pViewport;
}

int UIGraphicsScrollArea::scrollingValue() const
{
    return m_pScrollBar->value();
}

void UIGraphicsScrollArea::setScrollingValue(int iValue)
{
    iValue = qMax(iValue, 0);
    iValue = qMin(iValue, m_pScrollBar->maximum());
    m_pScrollBar->setValue(iValue);
}

void UIGraphicsScrollArea::scrollBy(int iDelta)
{
    m_pScrollBar->setValue(m_pScrollBar->value() + iDelta);
}

void UIGraphicsScrollArea::makeSureRectIsVisible(const QRectF &rect)
{
    /* Make sure rect size is bound by the scroll-area size: */
    QRectF actualRect = rect;
    QSizeF actualRectSize = actualRect.size();
    actualRectSize = actualRectSize.boundedTo(size());
    actualRect.setSize(actualRectSize);

    /* Acquire scroll-area scene position: */
    const QPointF saPos = mapToScene(QPointF(0, 0));

    switch (m_enmOrientation)
    {
        /* Scroll viewport horizontally: */
        case Qt::Horizontal:
        {
            /* If rectangle is at least partially right of visible area: */
            if (actualRect.x() + actualRect.width() - saPos.x() > size().width())
                m_pScrollBar->setValue(m_pScrollBar->value() + actualRect.x() + actualRect.width() - saPos.x() - size().width());
            /* If rectangle is at least partially left of visible area: */
            else if (actualRect.x() - saPos.x() < 0)
                m_pScrollBar->setValue(m_pScrollBar->value() + actualRect.x() - saPos.x());
            break;
        }
        /* Scroll viewport vertically: */
        case Qt::Vertical:
        {
            /* If rectangle is at least partially under visible area: */
            if (actualRect.y() + actualRect.height() - saPos.y() > size().height())
                m_pScrollBar->setValue(m_pScrollBar->value() + actualRect.y() + actualRect.height() - saPos.y() - size().height());
            /* If rectangle is at least partially above visible area: */
            else if (actualRect.y() - saPos.y() < 0)
                m_pScrollBar->setValue(m_pScrollBar->value() + actualRect.y() - saPos.y());
            break;
        }
    }
}

bool UIGraphicsScrollArea::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Handle layout requests for m_pViewport if set: */
    if (   m_pViewport
        && pObject == m_pViewport
        && pEvent->type() == QEvent::LayoutRequest)
        layoutWidgets();

    /* Handle wheel events for first scene view if set: */
    if (   scene()
        && !scene()->views().isEmpty()
        && pObject == scene()->views().first()
        && pEvent->type() == QEvent::Wheel)
    {
        QWheelEvent *pWheelEvent = static_cast<QWheelEvent*>(pEvent);
        const QPoint angleDelta = pWheelEvent->angleDelta();
#ifdef VBOX_WS_MAC
        const QPoint pixelDelta = pWheelEvent->pixelDelta();
#endif
        switch (m_enmOrientation)
        {
            /* Scroll viewport horizontally: */
            case Qt::Horizontal:
            {
                if (angleDelta.x() > 0)
#ifdef VBOX_WS_MAC
                    m_pScrollBar->setValue(m_pScrollBar->value() - pixelDelta.y());
#else
                    m_pScrollBar->setValue(m_pScrollBar->value() - m_pScrollBar->step());
#endif
                else if (angleDelta.x() < 0)
#ifdef VBOX_WS_MAC
                    m_pScrollBar->setValue(m_pScrollBar->value() - pixelDelta.y());
#else
                    m_pScrollBar->setValue(m_pScrollBar->value() + m_pScrollBar->step());
#endif
                break;
            }
            /* Scroll viewport vertically: */
            case Qt::Vertical:
            {
                if (angleDelta.y() > 0)
#ifdef VBOX_WS_MAC
                    m_pScrollBar->setValue(m_pScrollBar->value() - pixelDelta.y());
#else
                    m_pScrollBar->setValue(m_pScrollBar->value() - m_pScrollBar->step());
#endif
                else if (angleDelta.y() < 0)
#ifdef VBOX_WS_MAC
                    m_pScrollBar->setValue(m_pScrollBar->value() - pixelDelta.y());
#else
                    m_pScrollBar->setValue(m_pScrollBar->value() + m_pScrollBar->step());
#endif
                break;
            }
        }
    }

    /* Call to base-class: */
    return QIGraphicsWidget::eventFilter(pObject, pEvent);
}

void UIGraphicsScrollArea::resizeEvent(QGraphicsSceneResizeEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::resizeEvent(pEvent);

    /* Layout widgets: */
    layoutWidgets();
}

void UIGraphicsScrollArea::sltHandleScrollBarValueChange(int iValue)
{
    switch (m_enmOrientation)
    {
        /* Shift viewport horizontally: */
        case Qt::Horizontal: m_pViewport->setPos(-iValue, 0); break;
        /* Shift viewport vertically: */
        case Qt::Vertical:   m_pViewport->setPos(0, -iValue); break;
    }
}

void UIGraphicsScrollArea::prepare()
{
    /* Prepare/layout widgets: */
    prepareWidgets();
    layoutWidgets();
}

void UIGraphicsScrollArea::prepareWidgets()
{
#ifdef VBOX_WS_MAC
    /* Check whether scroll-bar is in auto-hide (overlay) mode: */
    m_fAutoHideMode = darwinIsScrollerStyleOverlay();
#endif

    /* Create scroll-bar: */
    m_pScrollBar = new UIGraphicsScrollBar(m_enmOrientation, m_fAutoHideMode, this);
    if (m_pScrollBar)
    {
        m_pScrollBar->setZValue(1);
        connect(m_pScrollBar, &UIGraphicsScrollBar::sigValueChanged,
                this, &UIGraphicsScrollArea::sltHandleScrollBarValueChange);
    }
}

void UIGraphicsScrollArea::layoutWidgets()
{
    switch (m_enmOrientation)
    {
        case Qt::Horizontal:
        {
            /* Align scroll-bar horizontally: */
            m_pScrollBar->resize(size().width(), m_pScrollBar->minimumSizeHint().height());
            m_pScrollBar->setPos(0, size().height() - m_pScrollBar->size().height());
            if (m_pViewport)
            {
                /* Adjust scroll-bar maximum value according to viewport width: */
                const int iWidth = size().width();
                const int iWidgetWidth = m_pViewport->size().width();
                if (iWidgetWidth > iWidth)
                    m_pScrollBar->setMaximum(iWidgetWidth - iWidth);
                else
                    m_pScrollBar->setMaximum(0);
            }
            break;
        }
        case Qt::Vertical:
        {
            /* Align scroll-bar vertically: */
            m_pScrollBar->resize(m_pScrollBar->minimumSizeHint().width(), size().height());
            m_pScrollBar->setPos(size().width() - m_pScrollBar->size().width(), 0);
            if (m_pViewport)
            {
                /* Adjust scroll-bar maximum value according to viewport height: */
                const int iHeight = size().height();
                const int iWidgetHeight = m_pViewport->size().height();
                if (iWidgetHeight > iHeight)
                    m_pScrollBar->setMaximum(iWidgetHeight - iHeight);
                else
                    m_pScrollBar->setMaximum(0);
            }
            break;
        }
    }

    /* Make scroll-bar visible only when there is viewport and maximum more than minimum: */
    m_pScrollBar->setVisible(m_pViewport && m_pScrollBar->maximum() > m_pScrollBar->minimum());

    if (m_pViewport)
    {
        switch (m_enmOrientation)
        {
            case Qt::Horizontal:
            {
                /* Calculate geometry deduction: */
                const int iDeduction = !m_fAutoHideMode && m_pScrollBar->isVisible() ? m_pScrollBar->minimumSizeHint().height() : 0;
                /* Align viewport and shift it horizontally: */
                m_pViewport->resize(m_pViewport->minimumSizeHint().width(), size().height() - iDeduction);
                m_pViewport->setPos(-m_pScrollBar->value(), 0);
                break;
            }
            case Qt::Vertical:
            {
                /* Calculate geometry deduction: */
                const int iDeduction = !m_fAutoHideMode && m_pScrollBar->isVisible() ? m_pScrollBar->minimumSizeHint().width() : 0;
                /* Align viewport and shift it vertically: */
                m_pViewport->resize(size().width() - iDeduction, m_pViewport->minimumSizeHint().height());
                m_pViewport->setPos(0, -m_pScrollBar->value());
                break;
            }
        }
    }
}
