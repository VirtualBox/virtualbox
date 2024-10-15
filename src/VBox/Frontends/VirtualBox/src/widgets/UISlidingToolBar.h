/* $Id: UISlidingToolBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISlidingToolBar class declaration.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UISlidingToolBar_h
#define FEQT_INCLUDED_SRC_widgets_UISlidingToolBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QCloseEvent;
class QEvent;
class QHBoxLayout;
class QRect;
class QShowEvent;
class QWidget;
class UIAnimation;

/** QWidget subclass
  * providing GUI with slideable tool-bar. */
class SHARED_LIBRARY_STUFF UISlidingToolBar : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(QRect widgetGeometry READ widgetGeometry WRITE setWidgetGeometry);
    Q_PROPERTY(QRect startWidgetGeometry READ startWidgetGeometry);
    Q_PROPERTY(QRect finalWidgetGeometry READ finalWidgetGeometry);

signals:

    /** Notifies about window shown. */
    void sigShown();
    /** Commands window to expand. */
    void sigExpand();
    /** Commands window to collapse. */
    void sigCollapse();

public:

    /** Possible positions. */
    enum Position
    {
        Position_Top,
        Position_Bottom
    };

    /** Constructs sliding tool-bar passing @a pParentWidget to the base-class.
      * @param  pParentWidget  Brings the parent-widget geometry.
      * @param  pIndentWidget  Brings the indent-widget geometry.
      * @param  pChildWidget   Brings the child-widget to be injected into tool-bar.
      * @param  enmPosition    Brings the tool-bar position. */
    UISlidingToolBar(QWidget *pParentWidget, QWidget *pIndentWidget, QWidget *pChildWidget, Position enmPosition);

public slots:

    /** Performs window activation. */
    void sltActivateWindow() { activateWindow(); }

protected:

#ifdef VBOX_WS_MAC
    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;
#endif
    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** Handles close @a pEvent. */
    virtual void closeEvent(QCloseEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Marks window as expanded. */
    void sltMarkAsExpanded() { m_fExpanded = true; }
    /** Marks window as collapsed. */
    void sltMarkAsCollapsed() { close(); m_fExpanded = false; }

    /** Handles parent geometry change. */
    void sltParentGeometryChanged(const QRect &parentRect);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares contents. */
    void prepareContents();
    /** Prepares geometry. */
    void prepareGeometry();
    /** Prepares animation. */
    void prepareAnimation();

    /** Updates geometry. */
    void adjustGeometry();
    /** Updates animation. */
    void updateAnimation();

    /** Defines sub-window geometry. */
    void setWidgetGeometry(const QRect &rect);
    /** Returns sub-window geometry. */
    QRect widgetGeometry() const;
    /** Returns sub-window start-geometry. */
    QRect startWidgetGeometry() const { return m_startWidgetGeometry; }
    /** Returns sub-window final-geometry. */
    QRect finalWidgetGeometry() const { return m_finalWidgetGeometry; }

    /** @name Geometry
      * @{ */
        /** Holds the tool-bar position. */
        const Position  m_enmPosition;
        /** Holds the cached parent-widget geometry. */
        QRect           m_parentRect;
        /** Holds the cached indent-widget geometry. */
        QRect           m_indentRect;
    /** @} */

    /** @name Geometry: Animation
      * @{ */
        /** Holds the expand/collapse animation instance. */
        UIAnimation *m_pAnimation;
        /** Holds whether window is expanded. */
        bool         m_fExpanded;
        /** Holds sub-window start-geometry. */
        QRect        m_startWidgetGeometry;
        /** Holds sub-window final-geometry. */
        QRect        m_finalWidgetGeometry;
    /** @} */

    /** @name Contents
      * @{ */
        /** Holds the main-layout instance. */
        QHBoxLayout *m_pMainLayout;
        /** Holds the area instance. */
        QWidget     *m_pArea;
        /** Holds the child-widget reference. */
        QWidget     *m_pWidget;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UISlidingToolBar_h */
