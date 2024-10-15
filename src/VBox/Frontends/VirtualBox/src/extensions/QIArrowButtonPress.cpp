/* $Id: QIArrowButtonPress.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIArrowButtonPress class implementation.
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
#include "QIArrowButtonPress.h"
#include "UITranslationEventListener.h"

QIArrowButtonPress::QIArrowButtonPress(QIArrowButtonPress::ButtonType enmButtonType,
                                       QWidget *pParent /* = 0 */)
    : QIRichToolButton(pParent)
    , m_enmButtonType(enmButtonType)
{
    /* Retranslate UI: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &QIArrowButtonPress::sltRetranslateUI);
}

void QIArrowButtonPress::sltRetranslateUI()
{
    /* Retranslate: */
    switch (m_enmButtonType)
    {
        case ButtonType_Back: setText(tr("&Back")); break;
        case ButtonType_Next: setText(tr("&Next")); break;
        default: break;
    }
}

void QIArrowButtonPress::keyPressEvent(QKeyEvent *pEvent)
{
    /* Handle different keys: */
    switch (pEvent->key())
    {
        /* Animate-click for the Space key: */
        case Qt::Key_PageUp:   if (m_enmButtonType == ButtonType_Next) return animateClick(); break;
        case Qt::Key_PageDown: if (m_enmButtonType == ButtonType_Back) return animateClick(); break;
        default: break;
    }
    /* Call to base-class: */
    QIRichToolButton::keyPressEvent(pEvent);
}
