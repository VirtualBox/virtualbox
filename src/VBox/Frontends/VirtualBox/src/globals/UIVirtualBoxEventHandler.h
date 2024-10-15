/* $Id: UIVirtualBoxEventHandler.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxEventHandler class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIVirtualBoxEventHandler_h
#define FEQT_INCLUDED_SRC_globals_UIVirtualBoxEventHandler_h
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
#include "KMachineState.h"
#include "KSessionState.h"

/* Forward declarations: */
class UIVirtualBoxEventHandlerProxy;

/** Singleton QObject extension providing GUI with CVirtualBox event-source. */
class SHARED_LIBRARY_STUFF UIVirtualBoxEventHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about @a state change event for the machine with @a uId. */
    void sigMachineStateChange(const QUuid &uId, const KMachineState state);
    /** Notifies about data change event for the machine with @a uId. */
    void sigMachineDataChange(const QUuid &uId);
    /** Notifies about machine with @a uId was @a fRegistered. */
    void sigMachineRegistered(const QUuid &uId, const bool fRegistered);
    /** Notifies about machine with @a uId has groups changed. */
    void sigMachineGroupsChange(const QUuid &uId);
    /** Notifies about @a state change event for the session of the machine with @a uId. */
    void sigSessionStateChange(const QUuid &uId, const KSessionState state);
    /** Notifies about snapshot with @a uSnapshotId was taken for the machine with @a uId. */
    void sigSnapshotTake(const QUuid &uId, const QUuid &uSnapshotId);
    /** Notifies about snapshot with @a uSnapshotId was deleted for the machine with @a uId. */
    void sigSnapshotDelete(const QUuid &uId, const QUuid &uSnapshotId);
    /** Notifies about snapshot with @a uSnapshotId was changed for the machine with @a uId. */
    void sigSnapshotChange(const QUuid &uId, const QUuid &uSnapshotId);
    /** Notifies about snapshot with @a uSnapshotId was restored for the machine with @a uId. */
    void sigSnapshotRestore(const QUuid &uId, const QUuid &uSnapshotId);
    /** Notifies about request to uninstall cloud provider with @a uId. */
    void sigCloudProviderUninstall(const QUuid &uId);
    /** Notifies about cloud provider list changed. */
    void sigCloudProviderListChanged();
    /** Notifies about cloud profile with specified @a strName of provider with specified @a uProviderId is @a fRegistered. */
    void sigCloudProfileRegistered(const QUuid &uProviderId, const QString &strName, bool fRegistered);
    /** Notifies about cloud profile with specified @a strName of provider with specified @a uProviderId is changed. */
    void sigCloudProfileChanged(const QUuid &uProviderId, const QString &strName);

    /** Notifies about storage controller change.
      * @param  uMachineId         Brings the ID of machine corresponding controller belongs to.
      * @param  strControllerName  Brings the name of controller this event is related to. */
    void sigStorageControllerChange(const QUuid &uMachineId, const QString &strControllerName);
    /** Notifies about storage device change.
      * @param  comAttachment  Brings corresponding attachment.
      * @param  fRemoved       Brings whether medium is removed or added.
      * @param  fSilent        Brings whether this change has gone silent for guest. */
    void sigStorageDeviceChange(CMediumAttachment comAttachment, bool fRemoved, bool fSilent);
    /** Notifies about storage medium @a comAttachment state change. */
    void sigMediumChange(CMediumAttachment comAttachment);
    /** Notifies about storage @a comMedium config change. */
    void sigMediumConfigChange(CMedium comMedium);
    /** Notifies about storage medium is (un)registered.
      * @param  uMediumId      Brings corresponding medium ID.
      * @param  enmMediumType  Brings corresponding medium type.
      * @param  fRegistered    Brings whether medium is registered or unregistered. */
    void sigMediumRegistered(const QUuid &uMediumId, KDeviceType enmMediumType, bool fRegistered);
    /** Notifies extension pack. install.
     *  @param  strName      Passes extension pack name. */
    void sigExtensionPackInstalled(const QString &strName);
    /** Notifies extension pack. uninstall.
     *  @param  strName      Passes extension pack name. */
    void sigExtensionPackUninstalled(const QString &strName);

public:

    /** Returns singleton instance. */
    static UIVirtualBoxEventHandler *instance();
    /** Destroys singleton instance. */
    static void destroy();

protected:

    /** Constructs VirtualBox event handler. */
    UIVirtualBoxEventHandler();

    /** Prepares all. */
    void prepare();
    /** Prepares connections. */
    void prepareConnections();

private:

    /** Holds the singleton instance. */
    static UIVirtualBoxEventHandler *s_pInstance;

    /** Holds the VirtualBox event proxy instance. */
    UIVirtualBoxEventHandlerProxy *m_pProxy;
};

/** Singleton VirtualBox Event Handler 'official' name. */
#define gVBoxEvents UIVirtualBoxEventHandler::instance()

#endif /* !FEQT_INCLUDED_SRC_globals_UIVirtualBoxEventHandler_h */
