/* $Id: QIDialogButtonBox.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIDialogButtonBox class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIDialogButtonBox_h
#define FEQT_INCLUDED_SRC_extensions_QIDialogButtonBox_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialogButtonBox>
#include <QPointer>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QBoxLayout;
class QPushButton;
class UIHelpButton;

/** QDialogButtonBox subclass extending standard functionality. */
class SHARED_LIBRARY_STUFF QIDialogButtonBox : public QDialogButtonBox
{
    Q_OBJECT;

public:

    /** Constructs dialog-button-box passing @a pParent to the base-class. */
    QIDialogButtonBox(QWidget *pParent = 0);
    /** Constructs dialog-button-box passing @a pParent to the base-class.
      * @param  enmOrientation  Brings the button-box orientation. */
    QIDialogButtonBox(Qt::Orientation enmOrientation, QWidget *pParent = 0);
    /** Constructs dialog-button-box passing @a pParent to the base-class.
      * @param  enmButtonTypes  Brings the set of button types.
      * @param  enmOrientation  Brings the button-box orientation. */
    QIDialogButtonBox(StandardButtons enmButtonTypes, Qt::Orientation enmOrientation = Qt::Horizontal, QWidget *pParent = 0);

    /** Returns the button of requested @a enmButtonType. */
    QPushButton *button(StandardButton enmButtonType) const;

    /** Adds button with passed @a strText for specified @a enmRole. */
    QPushButton *addButton(const QString &strText, ButtonRole enmRole);
    /** Adds standard button of passed @a enmButtonType. */
    QPushButton *addButton(StandardButton enmButtonType);

    /** Defines a set of standard @a enmButtonTypes. */
    void setStandardButtons(StandardButtons enmButtonTypes);

    /** Adds extra @a pWidget. */
    void addExtraWidget(QWidget *pWidget);
    /** Adds extra @a pLayout. */
    void addExtraLayout(QLayout *pLayout);

    /** Defines whether button-box should avoid picking default button. */
    void setDoNotPickDefaultButton(bool fDoNotPickDefaultButton);

public slots:

    /** Handles help request. */
    void sltHandleHelpRequest();

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Returns button layout. */
    QBoxLayout *boxLayout() const;

    /** Searchs for empty @a pLayout space. */
    int findEmptySpace(QBoxLayout *pLayout) const;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Holds the Help button reference. */
    QPointer<UIHelpButton> m_pHelpButton;

    /** Holds whether button-box should avoid picking default button. */
    bool  m_fDoNotPickDefaultButton;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIDialogButtonBox_h */
