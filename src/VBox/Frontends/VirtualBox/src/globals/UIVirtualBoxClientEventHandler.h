/* $Id: UIVirtualBoxClientEventHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxClientEventHandler class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIVirtualBoxClientEventHandler_h
#define FEQT_INCLUDED_SRC_globals_UIVirtualBoxClientEventHandler_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"

/* COM includes: */
#include "CMedium.h"
#include "CMediumAttachment.h"

/* Forward declarations: */
class UIVirtualBoxClientEventHandlerProxy;

/** Singleton QObject extension providing GUI with CVirtualBoxClient event-source. */
class SHARED_LIBRARY_STUFF UIVirtualBoxClientEventHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about the VBoxSVC become @a fAvailable. */
    void sigVBoxSVCAvailabilityChange(bool fAvailable);

public:

    /** Returns singleton instance. */
    static UIVirtualBoxClientEventHandler *instance();
    /** Destroys singleton instance. */
    static void destroy();

protected:

    /** Constructs VirtualBoxClient event handler. */
    UIVirtualBoxClientEventHandler();

    /** Prepares all. */
    void prepare();
    /** Prepares connections. */
    void prepareConnections();

private:

    /** Holds the singleton instance. */
    static UIVirtualBoxClientEventHandler *s_pInstance;

    /** Holds the VirtualBoxClient event proxy instance. */
    UIVirtualBoxClientEventHandlerProxy *m_pProxy;
};

/** Singleton VirtualBoxClient Event Handler 'official' name. */
#define gVBoxClientEvents UIVirtualBoxClientEventHandler::instance()

#endif /* !FEQT_INCLUDED_SRC_globals_UIVirtualBoxClientEventHandler_h */
