/* $Id: QIGraphicsView.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIGraphicsView class implementation.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include <QScrollBar>
#include <QTouchEvent>

/* GUI includes: */
#include "QIGraphicsView.h"

/* Other VBox includes: */
#include "iprt/assert.h"


QIGraphicsView::QIGraphicsView(QWidget *pParent /* = 0 */)
    : QGraphicsView(pParent)
    , m_iVerticalScrollBarPosition(0)
{
    /* Enable multi-touch support: */
    setAttribute(Qt::WA_AcceptTouchEvents);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
}

bool QIGraphicsView::event(QEvent *pEvent)
{
    /* Handle known event types: */
    switch (pEvent->type())
    {
        case QEvent::TouchBegin:
        {
            /* Parse the touch event: */
            QTouchEvent *pTouchEvent = static_cast<QTouchEvent*>(pEvent);
            AssertPtrReturn(pTouchEvent, QGraphicsView::event(pEvent));
            /* For touch-screen event we have something special: */
            if (pTouchEvent->device()->type() == QInputDevice::DeviceType::TouchScreen)
            {
                /* Remember where the scrolling was started: */
                m_iVerticalScrollBarPosition = verticalScrollBar()->value();
                /* Allow further touch events: */
                pEvent->accept();
                /* Mark event handled: */
                return true;
            }
            break;
        }
        case QEvent::TouchUpdate:
        {
            /* Parse the touch-event: */
            QTouchEvent *pTouchEvent = static_cast<QTouchEvent*>(pEvent);
            AssertPtrReturn(pTouchEvent, QGraphicsView::event(pEvent));
            /* For touch-screen event we have something special: */
            if (pTouchEvent->device()->type() == QInputDevice::DeviceType::TouchScreen)
            {
                /* Determine vertical shift (inverted): */
                const QEventPoint point = pTouchEvent->points().first();
                const int iShift = (int)(point.pressPosition().y() - point.position().y());
                /* Calculate new scroll-bar value according calculated shift: */
                int iNewScrollBarValue = m_iVerticalScrollBarPosition + iShift;
                /* Make sure new scroll-bar value is within the minimum/maximum bounds: */
                iNewScrollBarValue = qMax(verticalScrollBar()->minimum(), iNewScrollBarValue);
                iNewScrollBarValue = qMin(verticalScrollBar()->maximum(), iNewScrollBarValue);
                /* Apply calculated scroll-bar shift finally: */
                verticalScrollBar()->setValue(iNewScrollBarValue);
                /* Mark event handled: */
                return true;
            }
            break;
        }
        case QEvent::TouchEnd:
        {
            /* Parse the touch event: */
            QTouchEvent *pTouchEvent = static_cast<QTouchEvent*>(pEvent);
            AssertPtrReturn(pTouchEvent, QGraphicsView::event(pEvent));
            /* For touch-screen event we have something special: */
            if (pTouchEvent->device()->type() == QInputDevice::DeviceType::TouchScreen)
            {
                /* Reset the scrolling start position: */
                m_iVerticalScrollBarPosition = 0;
                /* Mark event handled: */
                return true;
            }
            break;
        }
        default:
            break;
    }
    /* Call to base-class: */
    return QGraphicsView::event(pEvent);
}
