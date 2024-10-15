/* $Id: UIGraphicsScrollArea.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsScrollArea class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsScrollArea_h
#define FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsScrollArea_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIGraphicsWidget.h"

/* Forward declarations: */
class QGraphicsScene;
class UIGraphicsScrollBar;

/** QIGraphicsWidget subclass providing GUI with graphics scroll-area. */
class UIGraphicsScrollArea : public QIGraphicsWidget
{
    Q_OBJECT;

public:

    /** Constructs graphics scroll-area of requested @a enmOrientation, embedding it directly to passed @a pScene. */
    UIGraphicsScrollArea(Qt::Orientation enmOrientation, QGraphicsScene *pScene);

    /** Constructs graphics scroll-area of requested @a enmOrientation passing @a pParent to the base-class. */
    UIGraphicsScrollArea(Qt::Orientation enmOrientation, QIGraphicsWidget *pParent = 0);

    /** Returns minimum size-hint. */
    virtual QSizeF minimumSizeHint() const RT_OVERRIDE;

    /** Defines scroll-area @a pViewport. */
    void setViewport(QIGraphicsWidget *pViewport);
    /** Returns scroll-area viewport. */
    QIGraphicsWidget *viewport() const;

    /** Returns scrolling location value in pixels. */
    int scrollingValue() const;
    /** Defines scrolling location @a iValue in pixels. */
    void setScrollingValue(int iValue);
    /** Performs scrolling by @a iDelta pixels. */
    void scrollBy(int iDelta);

    /** Makes sure passed @a rect is visible. */
    void makeSureRectIsVisible(const QRectF &rect);

protected:

    /** Preprocesses any Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QGraphicsSceneResizeEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles scroll-bar @a iValue change. */
    void sltHandleScrollBarValueChange(int iValue);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares all. */
    void prepareWidgets();

    /** Layout widgets. */
    void layoutWidgets();

    /** Holds the orientation. */
    Qt::Orientation  m_enmOrientation;
    /** Holds whether scroll-bar is in auto-hide mode. */
    bool             m_fAutoHideMode;

    /** Holds the scroll-bar instance. */
    UIGraphicsScrollBar *m_pScrollBar;
    /** Holds the viewport instance. */
    QIGraphicsWidget    *m_pViewport;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_graphics_UIGraphicsScrollArea_h */
