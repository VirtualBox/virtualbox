/* $Id: UIWelcomePane.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWelcomePane class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_UIWelcomePane_h
#define FEQT_INCLUDED_SRC_manager_UIWelcomePane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>
#include <QMap>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QAbstractButton;
class QLabel;
class QIRichTextLabel;

/** QWidget subclass holding Welcome information about VirtualBox. */
class UIWelcomePane : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs Welcome pane passing @a pParent to the base-class. */
    UIWelcomePane(QWidget *pParent = 0);

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles activated @a urlLink. */
    void sltHandleLinkActivated(const QUrl &urlLink);

    /** Handles @a pButton click. */
    void sltHandleButtonClicked(QAbstractButton *pButton);

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** Updates text labels. */
    void updateTextLabels();
    /** Updates pixmap. */
    void updatePixmap();

    /** Holds the icon instance. */
    QIcon  m_icon;

    /** Holds the greetings label instance. */
    QIRichTextLabel              *m_pLabelGreetings;
    /** Holds the mode label instance. */
    QIRichTextLabel              *m_pLabelMode;
    /** Holds a list of experience mode button instances. */
    QMap<bool, QAbstractButton*>  m_buttons;
    /** Holds the icon label instance. */
    QLabel                       *m_pLabelIcon;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UIWelcomePane_h */
