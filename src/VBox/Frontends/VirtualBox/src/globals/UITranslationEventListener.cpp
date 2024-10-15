/* $Id: UITranslationEventListener.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITranslationEventListener class implementation.
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
#include <QApplication>

/* GUI includes: */
#include "UITranslationEventListener.h"
#include "UITranslator.h"

/* static */
UITranslationEventListener *UITranslationEventListener::s_pInstance = 0;

UITranslationEventListener *UITranslationEventListener::instance()
{
    return s_pInstance;
}

/* static */
void UITranslationEventListener::create()
{
    /* Make sure instance is NOT created yet: */
    if (s_pInstance)
        return;

    /* Create instance: */
    new UITranslationEventListener;
}

/* static */
void UITranslationEventListener::destroy()
{
    /* Make sure instance is NOT destroyed yet: */
    if (!s_pInstance)
        return;

    /* Destroy instance: */
    delete s_pInstance;
}


UITranslationEventListener::UITranslationEventListener(QObject *pParent /* = 0 */)
    :QObject(pParent)
{
    qApp->installEventFilter(this);
    s_pInstance = this;
}

UITranslationEventListener::~UITranslationEventListener()
{
    s_pInstance = 0;
}

bool UITranslationEventListener::eventFilter(QObject *pObject, QEvent *pEvent)
{
    if (   !UITranslator::isTranslationInProgress()
           && pEvent->type() == QEvent::LanguageChange
           && pObject == qApp)
    {
        emit sigRetranslateUI();
    }
    /* Call to base-class: */
    return QObject::eventFilter(pObject, pEvent);
}
