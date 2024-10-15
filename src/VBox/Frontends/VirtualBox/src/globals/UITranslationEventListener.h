/* $Id: UITranslationEventListener.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITranslationEventListener class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UITranslationEventListener_h
#define FEQT_INCLUDED_SRC_globals_UITranslationEventListener_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

#include "UILibraryDefs.h"

class SHARED_LIBRARY_STUFF UITranslationEventListener : public QObject
{
    Q_OBJECT;

signals:

    void sigRetranslateUI();

public:

    /** Creates message-center singleton. */
    static void create();
    /** Destroys message-center singleton. */
    static void destroy();

protected:

    bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    UITranslationEventListener(QObject *pParent = 0);
    ~UITranslationEventListener();

    static UITranslationEventListener *s_pInstance;
    /** Returns the singleton instance. */
    static UITranslationEventListener *instance();
    /** Allows for shortcut access. */
    friend UITranslationEventListener &translationEventListener();

};

inline UITranslationEventListener &translationEventListener() { return *UITranslationEventListener::instance(); }

#endif /* !FEQT_INCLUDED_SRC_globals_UITranslationEventListener_h */
