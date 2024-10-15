/* $Id: UIUpdateManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUpdateManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networking_UIUpdateManager_h
#define FEQT_INCLUDED_SRC_networking_UIUpdateManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class UIExecutionQueue;

/** Singleton to perform new version checks
  * and update of various VirtualBox parts. */
class SHARED_LIBRARY_STUFF UIUpdateManager : public QObject
{
    Q_OBJECT;

    /** Constructs Update Manager. */
    UIUpdateManager();
    /** Destructs Update Manager. */
    ~UIUpdateManager();

public:

    /** Schedules manager. */
    static void schedule();
    /** Shutdowns manager. */
    static void shutdown();
    /** Returns manager instance. */
    static UIUpdateManager *instance() { return s_pInstance; }

    /** Returns whether the Extension Pack installation is requested. */
    bool isEPInstallationRequested() const { return m_fEPInstallationRequested; }
    /** Defines whether the Extension Pack installation is @a fRequested. */
    void setEPInstallationRequested(bool fRequested) { m_fEPInstallationRequested = fRequested; }

public slots:

    /** Performs forced new version check. */
    void sltForceCheck();

private slots:

    /** Checks whether update is necessary.
      * @param  fForcedCall  Brings whether this customer has forced privelegies. */
    void sltCheckIfUpdateIsNecessary(bool fForcedCall = false);

    /** Handles update finishing. */
    void sltHandleUpdateFinishing();

private:

    /** Holds the singleton instance. */
    static UIUpdateManager *s_pInstance;

    /** Holds the execution queue instance. */
    UIExecutionQueue *m_pQueue;
    /** Holds whether Update Manager is running. */
    bool              m_fIsRunning;
    /** Holds the refresh period. */
    quint64           m_uTime;

    /** Holds whether the Extension Pack installation is requested. */
    bool  m_fEPInstallationRequested;
};

/** Singleton Update Manager 'official' name. */
#define gUpdateManager UIUpdateManager::instance()

#endif /* !FEQT_INCLUDED_SRC_networking_UIUpdateManager_h */
