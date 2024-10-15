/* $Id: QIArrowButtonSwitch.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIArrowButtonSwitch class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIArrowButtonSwitch_h
#define FEQT_INCLUDED_SRC_extensions_QIArrowButtonSwitch_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>

/* GUI includes: */
#include "QIRichToolButton.h"
#include "UILibraryDefs.h"

/** QIRichToolButton extension
  * representing arrow tool-button with text-label,
  * can be used as collaps/expand switch in various places. */
class SHARED_LIBRARY_STUFF QIArrowButtonSwitch : public QIRichToolButton
{
    Q_OBJECT;

public:

    /** Constructs button passing @a pParent to the base-class. */
    QIArrowButtonSwitch(QWidget *pParent = 0);

    /** Defines the @a iconCollapsed and the @a iconExpanded. */
    void setIcons(const QIcon &iconCollapsed, const QIcon &iconExpanded);

    /** Defines whether the button is @a fExpanded. */
    void setExpanded(bool fExpanded);
    /** Returns whether the button is expanded. */
    bool isExpanded() const { return m_fExpanded; }

protected slots:

    /** Handles button-click. */
    virtual void sltButtonClicked() RT_OVERRIDE RT_FINAL;

protected:

    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    /** Updates icon according button-state. */
    void updateIcon() { setIcon(m_fExpanded ? m_iconExpanded : m_iconCollapsed); }

    /** Holds whether the button is expanded. */
    bool m_fExpanded;

    /** Holds the icon for collapsed button. */
    QIcon m_iconCollapsed;
    /** Holds the icon for expanded button. */
    QIcon m_iconExpanded;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIArrowButtonSwitch_h */
