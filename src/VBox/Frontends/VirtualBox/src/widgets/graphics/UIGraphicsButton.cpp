/* $Id: UIGraphicsButton.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsButton class implementation.
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
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QStyle>
#include <QTimerEvent>

/* GUI includes: */
#include "UIGraphicsButton.h"


UIGraphicsButton::UIGraphicsButton(QIGraphicsWidget *pParent, const QIcon &icon)
    : QIGraphicsWidget(pParent)
    , m_icon(icon)
    , m_enmClickPolicy(ClickPolicy_OnRelease)
    , m_iDelayId(0)
    , m_iRepeatId(0)
    , m_dIconScaleIndex(0)
{
    refresh();
}

void UIGraphicsButton::setIconScaleIndex(double dIndex)
{
    if (dIndex >= 0)
        m_dIconScaleIndex = dIndex;
}

double UIGraphicsButton::iconScaleIndex() const
{
    return m_dIconScaleIndex;
}

void UIGraphicsButton::setClickPolicy(ClickPolicy enmPolicy)
{
    m_enmClickPolicy = enmPolicy;
}

UIGraphicsButton::ClickPolicy UIGraphicsButton::clickPolicy() const
{
    return m_enmClickPolicy;
}

QVariant UIGraphicsButton::data(int iKey) const
{
    switch (iKey)
    {
        case GraphicsButton_Margin:
            return 0;
        case GraphicsButton_IconSize:
        {
            int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
            if (m_dIconScaleIndex > 0)
                iMetric *= m_dIconScaleIndex;
            return QSize(iMetric, iMetric);
        }
        case GraphicsButton_Icon:
            return m_icon;
        default:
            break;
    }
    return QVariant();
}

QSizeF UIGraphicsButton::sizeHint(Qt::SizeHint enmType, const QSizeF &constraint /* = QSizeF() */) const
{
    /* For minimum size-hint: */
    if (enmType == Qt::MinimumSize)
    {
        /* Prepare variables: */
        const int iMargin = data(GraphicsButton_Margin).toInt();
        const QSize iconSize = data(GraphicsButton_IconSize).toSize();
        /* Perform calculations: */
        int iWidth = 2 * iMargin + iconSize.width();
        int iHeight = 2 * iMargin + iconSize.height();
        return QSize(iWidth, iHeight);
    }

    /* Call to base-class: */
    return QIGraphicsWidget::sizeHint(enmType, constraint);
}

void UIGraphicsButton::paint(QPainter *pPainter, const QStyleOptionGraphicsItem* /* pOption */, QWidget* /* pWidget = 0 */)
{
    /* Prepare variables: */
    const int iMargin = data(GraphicsButton_Margin).toInt();
    const QIcon icon = data(GraphicsButton_Icon).value<QIcon>();
    const QSize expectedIconSize = data(GraphicsButton_IconSize).toSize();

    /* Determine which QWindow this QGraphicsWidget belongs to.
     * This is required for proper HiDPI-aware pixmap calculations. */
    QWindow *pWindow = 0;
    if (   scene()
        && !scene()->views().isEmpty()
        && scene()->views().first()
        && scene()->views().first()->window())
        pWindow = scene()->views().first()->window()->windowHandle();

    /* Acquire pixmap, adjust it to be in center of button if necessary: */
    const QPixmap pixmap = icon.pixmap(pWindow, expectedIconSize);
    const QSize actualIconSize = pixmap.size() / pixmap.devicePixelRatio();
    QPoint position = QPoint(iMargin, iMargin);
    if (actualIconSize != expectedIconSize)
    {
        const int iDx = (expectedIconSize.width() - actualIconSize.width()) / 2;
        const int iDy = (expectedIconSize.height() - actualIconSize.height()) / 2;
        position += QPoint(iDx, iDy);
    }

    /* Draw the pixmap finally: */
    pPainter->drawPixmap(position, pixmap);
}

void UIGraphicsButton::mousePressEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::mousePressEvent(pEvent);

    /* Accepting this event allows to get release-event: */
    pEvent->accept();

    /* For click-on-press policy: */
    if (m_enmClickPolicy == ClickPolicy_OnPress)
    {
        /* Notify listeners about button click: */
        emit sigButtonClicked();
        /* Start delay timer: */
        m_iDelayId = startTimer(500);
    }
}

void UIGraphicsButton::mouseReleaseEvent(QGraphicsSceneMouseEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsWidget::mouseReleaseEvent(pEvent);

    /* Depending on click policy: */
    switch (m_enmClickPolicy)
    {
        /* For click-on-release policy: */
        case ClickPolicy_OnRelease:
        {
            /* Notify listeners about button click: */
            emit sigButtonClicked();
            break;
        }
        /* For click-on-press policy: */
        case ClickPolicy_OnPress:
        {
            /* We should stop all timers: */
            killTimer(m_iDelayId);
            killTimer(m_iRepeatId);
            m_iDelayId = 0;
            m_iRepeatId = 0;
            break;
        }
    }
}

void UIGraphicsButton::timerEvent(QTimerEvent *pEvent)
{
    /* For click-on-press policy: */
    if (m_enmClickPolicy == ClickPolicy_OnPress)
    {
        /* We should auto-repeat button click: */
        emit sigButtonClicked();

        /* For delay timer: */
        if (pEvent->timerId() == m_iDelayId)
        {
            /* We should stop it and start repeat timer: */
            killTimer(m_iDelayId);
            m_iDelayId = 0;
            m_iRepeatId = startTimer(90);
        }
    }
}

void UIGraphicsButton::refresh()
{
    /* Refresh geometry: */
    updateGeometry();
    /* Resize to minimum size-hint: */
    resize(minimumSizeHint());
}
