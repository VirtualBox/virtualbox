/* $Id: UICloudMachineManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudMachineManager class declaration.
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

#ifndef FEQT_INCLUDED_SRC_cloud_UICloudMachineManager_h
#define FEQT_INCLUDED_SRC_cloud_UICloudMachineManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* Other VBox includes: */
#include <iprt/cdefs.h> // for RT_OVERRIDE / RT_FINAL stuff

/* Forward declarations: */
class QString;
class QUuid;
class CCloudMachine;

/** QObject subclass processing various cloud signals and handlers. */
class UICloudMachineManager : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about cloud VM was unregistered.
      * @param  strProviderShortName  Brings provider short name.
      * @param  strProfileName        Brings profile name.
      * @param  uId                   Brings cloud VM id. */
    void sigCloudMachineUnregistered(const QString &strProviderShortName,
                                     const QString &strProfileName,
                                     const QUuid &uId);
    /** Notifies listeners about cloud VM was registered.
      * @param  strProviderShortName  Brings provider short name.
      * @param  strProfileName        Brings profile name.
      * @param  comMachine            Brings cloud VM. */
    void sigCloudMachineRegistered(const QString &strProviderShortName,
                                   const QString &strProfileName,
                                   const CCloudMachine &comMachine);

public:

    /** Returns UICloudMachineManager instance. */
    static UICloudMachineManager *instance() { return s_pInstance; }
    /** Creates UICloudMachineManager instance. */
    static void create();
    /** Destroys UICloudMachineManager instance. */
    static void destroy();

    /** Notifies listeners about cloud VM was unregistered.
      * @param  strProviderShortName  Brings provider short name.
      * @param  strProfileName        Brings profile name.
      * @param  uId                   Brings cloud VM id. */
    void notifyCloudMachineUnregistered(const QString &strProviderShortName,
                                        const QString &strProfileName,
                                        const QUuid &uId);
    /** Notifies listeners about cloud VM was registered.
      * @param  strProviderShortName  Brings provider short name.
      * @param  strProfileName        Brings profile name.
      * @param  comMachine            Brings cloud VM. */
    void notifyCloudMachineRegistered(const QString &strProviderShortName,
                                      const QString &strProfileName,
                                      const CCloudMachine &comMachine);

public slots:

    /** Handles signal about cloud machine was added. */
    void sltHandleCloudMachineAdded(const QString &strProviderShortName,
                                    const QString &strProfileName,
                                    const CCloudMachine &comMachine);

private:

    /** Construcs global UICloudMachineManager object. */
    UICloudMachineManager();
    /** Destrucs global UICloudMachineManager object. */
    virtual ~UICloudMachineManager() RT_OVERRIDE RT_FINAL;

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the singleton UICloudMachineManager instance. */
    static UICloudMachineManager *s_pInstance;
};

/** Singleton Cloud Machine Manager 'official' name. */
#define gpCloudMachineManager UICloudMachineManager::instance()

#endif /* !FEQT_INCLUDED_SRC_cloud_UICloudMachineManager_h */
