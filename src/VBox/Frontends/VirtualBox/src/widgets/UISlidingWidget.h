/* $Id: UISlidingWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISlidingWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UISlidingWidget_h
#define FEQT_INCLUDED_SRC_widgets_UISlidingWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* Other VBox includes: */
#include <iprt/cdefs.h> // for RT_OVERRIDE stuff

/* Forward declarations: */
class QBoxLayout;
class QRect;
class QWidget;
class UIAnimation;


/** Some kind of splitter which allows to switch between
  * two widgets using horizontal sliding animation. */
class UISlidingWidget : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(QRect widgetGeometry READ widgetGeometry WRITE setWidgetGeometry);
    Q_PROPERTY(QRect startWidgetGeometry READ startWidgetGeometry);
    Q_PROPERTY(QRect finalWidgetGeometry READ finalWidgetGeometry);

signals:

    /** Commands to move animation forward. */
    void sigForward();
    /** Commands to move animation backward. */
    void sigBackward();

public:

    /** Sliding state. */
    enum State
    {
        State_Start,
        State_GoingForward,
        State_Final,
        State_GoingBackward
    };

    /** Constructs sliding widget passing @a pParent to the base-class.
      * @param  enmOrientation  Brings the widget orientation. */
    UISlidingWidget(Qt::Orientation enmOrientation, QWidget *pParent = 0);

    /** Holds the minimum widget size. */
    virtual QSize minimumSizeHint() const RT_OVERRIDE;

    /** Defines @a pWidget1 and @a pWidget2. */
    void setWidgets(QWidget *pWidget1, QWidget *pWidget2);

    /** Returns sliding state. */
    State state() const { return m_enmState; }

    /** Moves animation forward. */
    void moveForward() { m_enmState = State_GoingForward; emit sigForward(); }
    /** Moves animation backward. */
    void moveBackward() { m_enmState = State_GoingBackward; emit sigBackward(); }

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Marks state as start. */
    void sltSetStateToStart() { m_enmState = State_Start; }
    /** Marks state as final. */
    void sltSetStateToFinal() { m_enmState = State_Final; }

private:

    /** Prepares all. */
    void prepare();

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

    /** Holds the widget orientation. */
    Qt::Orientation  m_enmOrientation;

    /** Holds whether we are in animation final state. */
    State        m_enmState;
    /** Holds the shift left/right animation instance. */
    UIAnimation *m_pAnimation;
    /** Holds sub-window start-geometry. */
    QRect        m_startWidgetGeometry;
    /** Holds sub-window final-geometry. */
    QRect        m_finalWidgetGeometry;

    /** Holds the private sliding widget instance. */
    QWidget     *m_pWidget;
    /** Holds the widget layout instance. */
    QBoxLayout  *m_pLayout;
    /** Holds the 1st widget reference. */
    QWidget     *m_pWidget1;
    /** Holds the 2nd widget reference. */
    QWidget     *m_pWidget2;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UISlidingWidget_h */

