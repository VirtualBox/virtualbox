/* $Id: QIArrowButtonSwitch.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIArrowButtonSwitch class implementation.
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

/* Qt includes: */
#include <QKeyEvent>

/* GUI includes: */
#include "QIArrowButtonSwitch.h"


QIArrowButtonSwitch::QIArrowButtonSwitch(QWidget *pParent /* = 0 */)
    : QIRichToolButton(pParent)
    , m_fExpanded(false)
{
    /* Update icon: */
    updateIcon();
}

void QIArrowButtonSwitch::setIcons(const QIcon &iconCollapsed, const QIcon &iconExpanded)
{
    /* Assign icons: */
    m_iconCollapsed = iconCollapsed;
    m_iconExpanded = iconExpanded;
    /* Update icon: */
    updateIcon();
}

void QIArrowButtonSwitch::setExpanded(bool fExpanded)
{
    /* Set button state: */
    m_fExpanded = fExpanded;
    /* Update icon: */
    updateIcon();
}

void QIArrowButtonSwitch::sltButtonClicked()
{
    /* Toggle button state: */
    m_fExpanded = !m_fExpanded;
    /* Update icon: */
    updateIcon();
}

void QIArrowButtonSwitch::keyPressEvent(QKeyEvent *pEvent)
{
    /* Handle different keys: */
    switch (pEvent->key())
    {
        /* Animate-click for the Space key: */
        case Qt::Key_Minus: if (m_fExpanded) return animateClick(); break;
        case Qt::Key_Plus: if (!m_fExpanded) return animateClick(); break;
        default: break;
    }
    /* Call to base-class: */
    QIRichToolButton::keyPressEvent(pEvent);
}
