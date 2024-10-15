/* $Id: UIGuestOSTypeSelectionButton.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestOSTypeSelectionButton class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIGuestOSTypeSelectionButton_h
#define FEQT_INCLUDED_SRC_widgets_UIGuestOSTypeSelectionButton_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QPushButton>

/* Forward declarations: */
class QMenu;
class QSignalMapper;

/** QPushButton sub-class for choosing guest OS family/type inside appliance editor widget. */
class UIGuestOSTypeSelectionButton : public QPushButton
{
    Q_OBJECT;

public:

    /** Constructs a button passing @a pParent to the base-class. */
    UIGuestOSTypeSelectionButton(QWidget *pParent);

    /** Returns whether the menu is shown. */
    bool isMenuShown() const;

    /** Returns current guest OS type ID. */
    QString osTypeId() const { return m_strOSTypeId; }

public slots:

    /** Defines current guest @a strOSTypeId. */
    void setOSTypeId(const QString &strOSTypeId);

private:

    /** Handles translation event. */
    void sltRetranslateUI();
    /** Populates menu. */
    void populateMenu();
    /** A help fundtion for populateMenu(). @p is the list of os type and @p pMenu is the menu to populate. */
    void createOSTypeMenu(const QVector<QPair<QString, QString> > &typeList, QMenu *pMenu);
    /** Holds the current guest OS type ID. */
    QString  m_strOSTypeId;

    /** Holds the menu instance. */
    QMenu         *m_pMainMenu;
    /** Holds the signal mapper instance. */
    QSignalMapper *m_pSignalMapper;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIGuestOSTypeSelectionButton_h */
