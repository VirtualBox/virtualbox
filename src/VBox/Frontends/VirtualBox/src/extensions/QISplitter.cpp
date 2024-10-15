/* $Id: QISplitter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QISplitter class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>

/* GUI includes: */
#include "QISplitter.h"


/** QSplitterHandle subclass representing flat line. */
class QIFlatSplitterHandle : public QSplitterHandle
{
    Q_OBJECT;

public:

    /** Constructs flat splitter handle passing @a enmOrientation and @a pParent to the base-class. */
    QIFlatSplitterHandle(Qt::Orientation enmOrientation, QISplitter *pParent);

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;
};


/*********************************************************************************************************************************
*   Class QIFlatSplitterHandle implementation.                                                                                   *
*********************************************************************************************************************************/

QIFlatSplitterHandle::QIFlatSplitterHandle(Qt::Orientation enmOrientation, QISplitter *pParent)
    : QSplitterHandle(enmOrientation, pParent)
{
}

void QIFlatSplitterHandle::paintEvent(QPaintEvent *pEvent)
{
    /* Configure painter: */
    QPainter painter(this);
    painter.setClipRect(pEvent->rect());

    /* Choose color: */
    const bool fActive = window() && window()->isActiveWindow();
    QColor clr = QApplication::palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).darker(130);

    /* Paint flat rect: */
    painter.fillRect(rect(), clr);
}


/*********************************************************************************************************************************
*   Class QISplitter  implementation.                                                                                            *
*********************************************************************************************************************************/

QISplitter::QISplitter(Qt::Orientation enmOrientation /* = Qt::Horizontal */, QWidget *pParent /* = 0 */)
    : QSplitter(enmOrientation, pParent)
    , m_fPolished(false)
{
    qApp->installEventFilter(this);
    setHandleWidth(1);
}

bool QISplitter::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Handles events for handle: */
    if (pWatched == handle(1))
    {
        switch (pEvent->type())
        {
            /* Restore default position on double-click: */
            case QEvent::MouseButtonDblClick:
                restoreState(m_baseState);
                break;
            default:
                break;
        }
    }

    /* Call to base-class: */
    return QSplitter::eventFilter(pWatched, pEvent);
}

void QISplitter::showEvent(QShowEvent *pEvent)
{
    /* Remember default position: */
    if (!m_fPolished)
    {
        m_fPolished = true;
        m_baseState = saveState();
    }

    /* Call to base-class: */
    return QSplitter::showEvent(pEvent);
}

QSplitterHandle *QISplitter::createHandle()
{
    return new QIFlatSplitterHandle(orientation(), this);
}


#include "QISplitter.moc"
