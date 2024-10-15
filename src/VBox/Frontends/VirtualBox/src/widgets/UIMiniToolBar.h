/* $Id: UIMiniToolBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMiniToolBar class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIMiniToolBar_h
#define FEQT_INCLUDED_SRC_widgets_UIMiniToolBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIToolBar.h"

/* Forward declarations: */
class QMenu;
class QTimer;
class QLabel;
class UIAnimation;
class UIMiniToolBarPrivate;

/** Geometry types. */
enum GeometryType
{
    GeometryType_Available,
    GeometryType_Full
};


/** QWidget reimplementation
  * providing GUI with slideable mini-toolbar used in full-screen/seamless modes. */
class UIMiniToolBar : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(QPoint toolbarPosition READ toolbarPosition WRITE setToolbarPosition);
    Q_PROPERTY(QPoint hiddenToolbarPosition READ hiddenToolbarPosition);
    Q_PROPERTY(QPoint shownToolbarPosition READ shownToolbarPosition);

signals:

    /** Notifies listeners about action triggered to minimize. */
    void sigMinimizeAction();
    /** Notifies listeners about action triggered to exit. */
    void sigExitAction();
    /** Notifies listeners about action triggered to close. */
    void sigCloseAction();

    /** Notifies listeners about we are hovered. */
    void sigHoverEnter();
    /** Notifies listeners about we are unhovered. */
    void sigHoverLeave();

    /** Notifies listeners about we stole window activation. */
    void sigNotifyAboutWindowActivationStolen();

    /** Notifies listeners about auto-hide toggled.
      * @param  fEnabled  Brings whether auto-hide is enabled. */
    void sigAutoHideToggled(bool fEnabled);

public:

    /** Proposes default set of window flags for particular platform. */
    static Qt::WindowFlags defaultWindowFlags(GeometryType geometryType);

    /** Constructor, passes @a pParent to the QWidget constructor.
      * @param geometryType determines the geometry type,
      * @param alignment    determines the alignment type,
      * @param fAutoHide    determines whether we should auto-hide.
      * @param iWindowIndex determines the parent window index. */
    UIMiniToolBar(QWidget *pParent,
                  GeometryType geometryType,
                  Qt::Alignment alignment,
                  bool fAutoHide = true,
                  int iWindowIndex = -1);
    /** Destructor. */
    ~UIMiniToolBar();

    /** Defines @a alignment. */
    void setAlignment(Qt::Alignment alignment);

    /** Returns whether internal widget do auto-hide. */
    bool autoHide() const { return m_fAutoHide; }
    /** Defines whether internal widget do @a fAutoHide.
      * @param fPropagateToChild determines should we propagate defined
      *                          option value to internal widget. */
    void setAutoHide(bool fAutoHide, bool fPropagateToChild = true);

    /** Defines @a strText for internal widget. */
    void setText(const QString &strText);

    /** Adds @a menus to internal widget. */
    void addMenus(const QList<QMenu*> &menus);

    /** Adjusts geometry. */
    void adjustGeometry();

protected:

    /** Filters @a pEvent if <i>this</i> object has been
      * installed as an event-filter for the @a pWatched. */
    virtual bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;

    /** Resize @a pEvent handler. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

    /** Mouse enter @a pEvent handler. */
    virtual void enterEvent(QEnterEvent *pEvent) RT_OVERRIDE;
    /** Mouse leave @a pEvent handler. */
    virtual void leaveEvent(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles internal widget resize event. */
    void sltHandleToolbarResize();

    /** Handles internal widget auto-hide toggling. */
    void sltAutoHideToggled();

    /** Handles hovering. */
    void sltHoverEnter();
    /** Handles unhovering. */
    void sltHoverLeave();

    /** Check whether we still have window activation token. */
    void sltCheckWindowActivationSanity();

    /** Hides window. */
    void sltHide();
    /** Shows and adjusts window according to parent. */
    void sltShow();
    /** Adjusts window according to parent. */
    void sltAdjust();
    /** Adjusts window transience according to parent. */
    void sltAdjustTransience();

private:

    /** Prepare routine. */
    void prepare();
    /** Cleanup routine. */
    void cleanup();

    /** Simulates auto-hide animation. */
    void simulateToolbarAutoHiding();

    /** Defines internal widget @a position. */
    void setToolbarPosition(QPoint position);
    /** Returns internal widget position. */
    QPoint toolbarPosition() const;
    /** Returns internal widget position when it's hidden. */
    QPoint hiddenToolbarPosition() const { return m_hiddenToolbarPosition; }
    /** Returns internal widget position when it's shown. */
    QPoint shownToolbarPosition() const { return m_shownToolbarPosition; }

    /** Returns whether the parent is currently minimized. */
    bool isParentMinimized() const;

    /** Holds the parent reference. */
    QWidget *m_pParent;

    /** Holds the geometry type. */
    const GeometryType m_geometryType;
    /** Holds the alignment type. */
    Qt::Alignment m_alignment;
    /** Holds whether we should auto-hide. */
    bool m_fAutoHide;
    /** Holds the parent window index. */
    int m_iWindowIndex;

    /** Holds the area. */
    QWidget *m_pArea;
    /** Holds the internal widget. */
    UIMiniToolBarPrivate *m_pToolbar;

    /** Holds whether we are hovered. */
    bool m_fHovered;
    /** Holds the hover timer. */
    QTimer *m_pHoverEnterTimer;
    /** Holds the unhover timer. */
    QTimer *m_pHoverLeaveTimer;
    /** Holds the internal widget position when it's hidden. */
    QPoint m_hiddenToolbarPosition;
    /** Holds the internal widget position when it's shown. */
    QPoint m_shownToolbarPosition;
    /** Holds the animation framework object. */
    UIAnimation *m_pAnimation;

#ifdef VBOX_WS_NIX
    /** X11: Holds whether the parent is currently minimized.
      * Used to restore the full-screen/maximized state
      * when the parent restored again. */
    bool m_fIsParentMinimized;
#endif
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIMiniToolBar_h */

