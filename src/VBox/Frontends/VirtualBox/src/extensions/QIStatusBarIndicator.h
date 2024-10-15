/* $Id: QIStatusBarIndicator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIStatusBarIndicator interface declaration.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIStatusBarIndicator_h
#define FEQT_INCLUDED_SRC_extensions_QIStatusBarIndicator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>
#include <QMap>
#include <QSize>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;


/** QWidget extension used as status-bar indicator. */
class SHARED_LIBRARY_STUFF QIStatusBarIndicator : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about mouse-double-click-event: */
    void sigMouseDoubleClick(QIStatusBarIndicator *pIndicator, QMouseEvent *pEvent);
    /** Notifies about context-menu-request-event: */
    void sigContextMenuRequest(QIStatusBarIndicator *pIndicator, QContextMenuEvent *pEvent);

public:

    /** Constructs status-bar indicator passing @a pParent to the base-class. */
    QIStatusBarIndicator(QWidget *pParent = 0);

    /** Returns size-hint. */
    virtual QSize sizeHint() const RT_OVERRIDE { return m_size.isValid() ? m_size : QWidget::sizeHint(); }

protected:

#ifdef VBOX_WS_MAC
    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
#endif /* VBOX_WS_MAC */
    /** Handles mouse-double-click @a pEvent. */
    virtual void mouseDoubleClickEvent(QMouseEvent *pEvent) RT_OVERRIDE;

    /** Handles context-menu @a pEvent. */
    virtual void contextMenuEvent(QContextMenuEvent *pEvent) RT_OVERRIDE;

    /** Holds currently cached size. */
    QSize  m_size;
};


/** QIStatusBarIndicator extension used as status-bar state indicator. */
class SHARED_LIBRARY_STUFF QIStateStatusBarIndicator : public QIStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs state status-bar indicator passing @a pParent to the base-class. */
    QIStateStatusBarIndicator(QWidget *pParent = 0);

    /** Returns current state. */
    int state() const { return m_iState; }

    /** Returns state-icon for passed @a iState. */
    QIcon stateIcon(int iState) const;
    /** Defines state-icon for passed @a iState as @a icon. */
    void setStateIcon(int iState, const QIcon &icon);

public slots:

    /** Defines int @a iState. */
    virtual void setState(int iState) { m_iState = iState; repaint(); }

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

    /** Draws contents using passed @a pPainter. */
    virtual void drawContents(QPainter *pPainter);

private:

    /** Holds current state. */
    int               m_iState;
    /** Holds cached state icons. */
    QMap<int, QIcon>  m_icons;
};


/** QIStatusBarIndicator extension used as status-bar text indicator. */
class SHARED_LIBRARY_STUFF QITextStatusBarIndicator : public QIStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs text status-bar indicator passing @a pParent to the base-class. */
    QITextStatusBarIndicator(QWidget *pParent = 0);

    /** Returns text. */
    QString text() const;
    /** Defines @a strText. */
    void setText(const QString &strText);

private:

    /** Holds the label instance. */
    QLabel *m_pLabel;
};


#endif /* !FEQT_INCLUDED_SRC_extensions_QIStatusBarIndicator_h */
