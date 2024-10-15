/* $Id: UIPopupPaneButtonPane.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupPaneButtonPane class declaration.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIPopupPaneButtonPane_h
#define FEQT_INCLUDED_SRC_widgets_UIPopupPaneButtonPane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>
#include <QMap>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QHBoxLayout;
class QIcon;
class QKeyEvent;
class QString;
class QIToolButton;

/** QWidget extension providing GUI with popup-pane button-pane prototype class. */
class SHARED_LIBRARY_STUFF UIPopupPaneButtonPane : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about button with @a iButtonID being clicked. */
    void sigButtonClicked(int iButtonID);

public:

    /** Constructs popup-button pane passing @a pParent to the base-class. */
    UIPopupPaneButtonPane(QWidget *pParent = 0);

    /** Defines @a buttonDescriptions. */
    void setButtons(const QMap<int, QString> &buttonDescriptions);
    /** Returns default button. */
    int defaultButton() const { return m_iDefaultButton; }
    /** Returns escape button. */
    int escapeButton() const { return m_iEscapeButton; }

private slots:

    /** Handles button click. */
    void sltButtonClicked();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares layouts. */
    void prepareLayouts();
    /** Prepares buttons. */
    void prepareButtons();
    /** Cleanups buttons. */
    void cleanupButtons();

    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;

    /** Adds button with @a iButtonID and @a strToolTip. */
    static QIToolButton *addButton(int iButtonID, const QString &strToolTip);
    /** Returns default tool-tip for button @a iButtonID. */
    static QString defaultToolTip(int iButtonID);
    /** Returns default icon for button @a iButtonID. */
    static QIcon defaultIcon(int iButtonID);

    /** Holds the button layout. */
    QHBoxLayout *m_pButtonLayout;

    /** Holds the button descriptions. */
    QMap<int, QString>       m_buttonDescriptions;
    /** Holds the button instances. */
    QMap<int, QIToolButton*> m_buttons;

    /** Holds default button. */
    int m_iDefaultButton;
    /** Holds escape button. */
    int m_iEscapeButton;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIPopupPaneButtonPane_h */

