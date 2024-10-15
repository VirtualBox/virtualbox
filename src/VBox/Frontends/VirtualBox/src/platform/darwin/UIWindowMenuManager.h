/* $Id: UIWindowMenuManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWindowMenuManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_UIWindowMenuManager_h
#define FEQT_INCLUDED_SRC_platform_darwin_UIWindowMenuManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QHash>
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QMenu;
class UIMenuHelper;

/** Singleton QObject extension
  * used as Mac OS X 'Window' menu Manager. */
class SHARED_LIBRARY_STUFF UIWindowMenuManager : public QObject
{
    Q_OBJECT;

public:

    /** Creates instance. */
    static void create();
    /** Destroys instance. */
    static void destroy();
    /** Returns current instance. */
    static UIWindowMenuManager *instance() { return s_pInstance; }

    /** Creates 'Window' menu for passed @a pWindow. */
    QMenu *createMenu(QWidget *pWindow);
    /** Destroys 'Window' menu for passed @a pWindow. */
    void destroyMenu(QWidget *pWindow);

    /** Adds @a pWindow to all 'Window' menus. */
    void addWindow(QWidget *pWindow);
    /** Removes @a pWindow from all 'Window' menus. */
    void removeWindow(QWidget *pWindow);

protected:

    /** Constructs 'Window' menu Manager. */
    UIWindowMenuManager();
    /** Destructs 'Window' menu Manager. */
    ~UIWindowMenuManager();

    /** Preprocesses any Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI();

private:

    /** Holds the static instance. */
    static UIWindowMenuManager *s_pInstance;

    /** Holds the list of the registered window references. */
    QList<QWidget*>  m_windows;

    /** Holds the hash of the registered menu-helper instances. */
    QHash<QWidget*, UIMenuHelper*>  m_helpers;
};

/** Singleton 'Window' menu Manager 'official' name. */
#define gpWindowMenuManager UIWindowMenuManager::instance()

#endif /* !FEQT_INCLUDED_SRC_platform_darwin_UIWindowMenuManager_h */
