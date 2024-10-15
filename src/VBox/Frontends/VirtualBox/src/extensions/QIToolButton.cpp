/* $Id: QIToolButton.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIToolButton class implementation.
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

/* GUI includes: */
#include "QIToolButton.h"


QIToolButton::QIToolButton(QWidget *pParent /* = 0 */)
    : QToolButton(pParent)
{
#ifdef VBOX_WS_MAC
    /* Keep size-hint alive: */
    const QSize sh = sizeHint();
    setStyleSheet("QToolButton { border: 0px none black; margin: 0px 0px 0px 0px; } QToolButton::menu-indicator {image: none;}");
    setFixedSize(sh);
#else /* !VBOX_WS_MAC */
    setAutoRaise(true);
#endif /* !VBOX_WS_MAC */
}

void QIToolButton::setAutoRaise(bool fEnabled)
{
#ifdef VBOX_WS_MAC
    /* Ignore for macOS: */
    Q_UNUSED(fEnabled);
#else /* !VBOX_WS_MAC */
    /* Call to base-class: */
    QToolButton::setAutoRaise(fEnabled);
#endif /* !VBOX_WS_MAC */
}

void QIToolButton::removeBorder()
{
    setStyleSheet("QToolButton { border: 0px }");
}
