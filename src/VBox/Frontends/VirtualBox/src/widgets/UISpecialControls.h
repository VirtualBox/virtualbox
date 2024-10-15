/* $Id: UISpecialControls.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISpecialControls declarations.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UISpecialControls_h
#define FEQT_INCLUDED_SRC_widgets_UISpecialControls_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QPushButton>
#ifndef VBOX_DARWIN_USE_NATIVE_CONTROLS
# include <QLineEdit>
#endif

/* GUI includes: */
#include "UILibraryDefs.h"
#ifdef VBOX_DARWIN_USE_NATIVE_CONTROLS
# include "UICocoaSpecialControls.h"
#else
# include "QIToolButton.h"
#endif

/* Forward declarations: */
#ifdef VBOX_DARWIN_USE_NATIVE_CONTROLS
class UICocoaButton;
#endif


#ifdef VBOX_DARWIN_USE_NATIVE_CONTROLS

/** QAbstractButton subclass, used as mini cancel button. */
class SHARED_LIBRARY_STUFF UIMiniCancelButton : public QAbstractButton
{
    Q_OBJECT;

public:

    /** Constructs mini cancel-button passing @a pParent to the base-class. */
    UIMiniCancelButton(QWidget *pParent = 0);

    /** Defines button @a strText. */
    void setText(const QString &strText) { m_pButton->setText(strText); }
    /** Defines button @a strToolTip. */
    void setToolTip(const QString &strToolTip) { m_pButton->setToolTip(strToolTip); }
    /** Removes button border. */
    void removeBorder() {}

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE { Q_UNUSED(pEvent); }
    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private:

    /** Holds the wrapped cocoa button instance. */
    UICocoaButton *m_pButton;
};


/** QAbstractButton subclass, used as mini cancel button. */
class SHARED_LIBRARY_STUFF UIHelpButton : public QPushButton
{
    Q_OBJECT;

public:

    /** Constructs help-button passing @a pParent to the base-class. */
    UIHelpButton(QWidget *pParent = 0);

    /** Defines button @a strToolTip. */
    void setToolTip(const QString &strToolTip) { m_pButton->setToolTip(strToolTip); }

    /** Inits this button from pOther. */
    void initFrom(QPushButton *pOther) { Q_UNUSED(pOther); }

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE { Q_UNUSED(pEvent); }

private:

    /** Holds the wrapped cocoa button instance. */
    UICocoaButton *m_pButton;
};

#else /* !VBOX_DARWIN_USE_NATIVE_CONTROLS */

/** QAbstractButton subclass, used as mini cancel button. */
class SHARED_LIBRARY_STUFF UIMiniCancelButton : public QIToolButton
{
    Q_OBJECT;

public:

    /** Constructs mini cancel-button passing @a pParent to the base-class. */
    UIMiniCancelButton(QWidget *pParent = 0);

private slots:

    /** Handles translation event. */
    void sltRetranslateUI() {};
};


/** QAbstractButton subclass, used as mini cancel button. */
class SHARED_LIBRARY_STUFF UIHelpButton : public QPushButton
{
    Q_OBJECT;

public:

    /** Constructs help-button passing @a pParent to the base-class. */
    UIHelpButton(QWidget *pParent = 0);

# ifdef VBOX_WS_MAC
    /** Destructs help-button. */
    ~UIHelpButton();

    /** Returns size-hint. */
    QSize sizeHint() const RT_OVERRIDE;
# endif /* VBOX_WS_MAC */

    /** Inits this button from pOther. */
    void initFrom(QPushButton *pOther);

protected:

# ifdef VBOX_WS_MAC
    /** Handles button hit as certain @a position. */
    bool hitButton(const QPoint &position) const RT_OVERRIDE;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

    /** Handles mouse-press @a pEvent. */
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-release @a pEvent. */
    virtual void mouseReleaseEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    /** Handles mouse-leave @a pEvent. */
    virtual void leaveEvent(QEvent *pEvent) RT_OVERRIDE;

private:

    /** Holds the pressed button instance. */
    bool m_pButtonPressed;

    /** Holds the button size. */
    QSize m_size;

    /** Holds the normal pixmap instance. */
    QPixmap *m_pNormalPixmap;
    /** Holds the pressed pixmap instance. */
    QPixmap *m_pPressedPixmap;

    /** Holds the button mask instance. */
    QImage *m_pMask;

    /** Holds the button rect. */
    QRect m_BRect;
# endif /* VBOX_WS_MAC */

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();
};

#endif /* !VBOX_DARWIN_USE_NATIVE_CONTROLS */


#endif /* !FEQT_INCLUDED_SRC_widgets_UISpecialControls_h */
