/* $Id: UIVirtualBoxEventHandler.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxEventHandler class implementation.
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

/* GUI includes: */
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIMainEventListener.h"
#include "UIVirtualBoxEventHandler.h"

/* COM includes: */
#include "CEventListener.h"
#include "CEventSource.h"
#include "CVirtualBox.h"


/** Private QObject extension providing UIVirtualBoxEventHandler with CVirtualBox event-source. */
class UIVirtualBoxEventHandlerProxy : public QObject
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
    /** Notifies extension pack. unnstall.
     *  @param  strName      Passes extension pack name. */
    void sigExtensionPackUninstalled(const QString &strName);

public:

    /** Constructs event proxy object on the basis of passed @a pParent. */
    UIVirtualBoxEventHandlerProxy(QObject *pParent);
    /** Destructs event proxy object. */
    ~UIVirtualBoxEventHandlerProxy();

protected:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares listener. */
        void prepareListener();
        /** Prepares connections. */
        void prepareConnections();

        /** Cleanups connections. */
        void cleanupConnections();
        /** Cleanups listener. */
        void cleanupListener();
        /** Cleanups all. */
        void cleanup();
    /** @} */

private:

    /** Holds the COM event source instance. */
    CEventSource m_comEventSource;

    /** Holds the Qt event listener instance. */
    ComObjPtr<UIMainEventListenerImpl> m_pQtListener;
    /** Holds the COM event listener instance. */
    CEventListener m_comEventListener;
};


/*********************************************************************************************************************************
*   Class UIVirtualBoxEventHandlerProxy implementation.                                                                          *
*********************************************************************************************************************************/

UIVirtualBoxEventHandlerProxy::UIVirtualBoxEventHandlerProxy(QObject *pParent)
    : QObject(pParent)
{
    /* Prepare: */
    prepare();
}

UIVirtualBoxEventHandlerProxy::~UIVirtualBoxEventHandlerProxy()
{
    /* Cleanup: */
    cleanup();
}

void UIVirtualBoxEventHandlerProxy::prepare()
{
    /* Prepare: */
    prepareListener();
    prepareConnections();
}

void UIVirtualBoxEventHandlerProxy::prepareListener()
{
    /* Create Main event listener instance: */
    m_pQtListener.createObject();
    m_pQtListener->init(new UIMainEventListener, this);
    m_comEventListener = CEventListener(m_pQtListener);

    /* Get VirtualBox: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    /* Get VirtualBox event source: */
    m_comEventSource = comVBox.GetEventSource();
    Assert(comVBox.isOk());

    /* Enumerate all the required event-types: */
    QVector<KVBoxEventType> eventTypes;
    eventTypes
        << KVBoxEventType_OnMachineStateChanged
        << KVBoxEventType_OnMachineDataChanged
        << KVBoxEventType_OnMachineRegistered
        << KVBoxEventType_OnMachineGroupsChanged
        << KVBoxEventType_OnSessionStateChanged
        << KVBoxEventType_OnSnapshotTaken
        << KVBoxEventType_OnSnapshotDeleted
        << KVBoxEventType_OnSnapshotChanged
        << KVBoxEventType_OnSnapshotRestored
        << KVBoxEventType_OnCloudProviderListChanged
        << KVBoxEventType_OnCloudProviderUninstall
        << KVBoxEventType_OnCloudProfileRegistered
        << KVBoxEventType_OnCloudProfileChanged
        << KVBoxEventType_OnStorageControllerChanged
        << KVBoxEventType_OnStorageDeviceChanged
        << KVBoxEventType_OnMediumChanged
        << KVBoxEventType_OnMediumConfigChanged
        << KVBoxEventType_OnMediumRegistered
        << KVBoxEventType_OnExtPackInstalled;

    /* Register event listener for event source aggregator: */
    m_comEventSource.RegisterListener(m_comEventListener, eventTypes, FALSE /* active? */);
    Assert(m_comEventSource.isOk());

    /* Register event sources in their listeners as well: */
    m_pQtListener->getWrapped()->registerSource(m_comEventSource, m_comEventListener);
}

void UIVirtualBoxEventHandlerProxy::prepareConnections()
{
    /* Create direct (sync) connections for signals of main event listener.
     * Keep in mind that the abstract Qt4 connection notation should be used here. */
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMachineStateChange(QUuid, KMachineState)),
            this, SIGNAL(sigMachineStateChange(QUuid, KMachineState)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMachineDataChange(QUuid)),
            this, SIGNAL(sigMachineDataChange(QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMachineRegistered(QUuid, bool)),
            this, SIGNAL(sigMachineRegistered(QUuid, bool)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMachineGroupsChange(QUuid)),
            this, SIGNAL(sigMachineGroupsChange(QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigSessionStateChange(QUuid, KSessionState)),
            this, SIGNAL(sigSessionStateChange(QUuid, KSessionState)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigSnapshotTake(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotTake(QUuid, QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigSnapshotDelete(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotDelete(QUuid, QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigSnapshotChange(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotChange(QUuid, QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigSnapshotRestore(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotRestore(QUuid, QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigCloudProviderListChanged()),
            this, SIGNAL(sigCloudProviderListChanged()),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigCloudProviderUninstall(QUuid)),
            this, SIGNAL(sigCloudProviderUninstall(QUuid)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigCloudProfileRegistered(QUuid, QString, bool)),
            this, SIGNAL(sigCloudProfileRegistered(QUuid, QString, bool)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigCloudProfileChanged(QUuid, QString)),
            this, SIGNAL(sigCloudProfileChanged(QUuid, QString)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigStorageControllerChange(QUuid, QString)),
            this, SIGNAL(sigStorageControllerChange(QUuid, QString)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigStorageDeviceChange(CMediumAttachment, bool, bool)),
            this, SIGNAL(sigStorageDeviceChange(CMediumAttachment, bool, bool)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMediumChange(CMediumAttachment)),
            this, SIGNAL(sigMediumChange(CMediumAttachment)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMediumConfigChange(CMedium)),
            this, SIGNAL(sigMediumConfigChange(CMedium)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigMediumRegistered(QUuid, KDeviceType, bool)),
            this, SIGNAL(sigMediumRegistered(QUuid, KDeviceType, bool)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigExtensionPackInstalled(QString)),
            this, SIGNAL(sigExtensionPackInstalled(QString)),
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), SIGNAL(sigExtensionPackUninstalled(QString)),
            this, SIGNAL(sigExtensionPackUninstalled(QString)),
            Qt::DirectConnection);
}

void UIVirtualBoxEventHandlerProxy::cleanupConnections()
{
    /* Nothing for now. */
}

void UIVirtualBoxEventHandlerProxy::cleanupListener()
{
    /* Unregister everything: */
    m_pQtListener->getWrapped()->unregisterSources();

    /* Unregister event listener for event source aggregator: */
    m_comEventSource.UnregisterListener(m_comEventListener);
    m_comEventSource.detach();
}

void UIVirtualBoxEventHandlerProxy::cleanup()
{
    /* Cleanup: */
    cleanupConnections();
    cleanupListener();
}


/*********************************************************************************************************************************
*   Class UIVirtualBoxEventHandler implementation.                                                                               *
*********************************************************************************************************************************/

/* static */
UIVirtualBoxEventHandler *UIVirtualBoxEventHandler::s_pInstance = 0;

/* static */
UIVirtualBoxEventHandler *UIVirtualBoxEventHandler::instance()
{
    if (!s_pInstance)
        s_pInstance = new UIVirtualBoxEventHandler;
    return s_pInstance;
}

/* static */
void UIVirtualBoxEventHandler::destroy()
{
    if (s_pInstance)
    {
        delete s_pInstance;
        s_pInstance = 0;
    }
}

UIVirtualBoxEventHandler::UIVirtualBoxEventHandler()
    : m_pProxy(new UIVirtualBoxEventHandlerProxy(this))
{
    /* Prepare: */
    prepare();
}

void UIVirtualBoxEventHandler::prepare()
{
    /* Prepare connections: */
    prepareConnections();
}

void UIVirtualBoxEventHandler::prepareConnections()
{
    /* Create queued (async) connections for signals of event proxy object.
     * Keep in mind that the abstract Qt4 connection notation should be used here. */
    connect(m_pProxy, SIGNAL(sigMachineStateChange(QUuid, KMachineState)),
            this, SIGNAL(sigMachineStateChange(QUuid, KMachineState)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigMachineDataChange(QUuid)),
            this, SIGNAL(sigMachineDataChange(QUuid)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigMachineRegistered(QUuid, bool)),
            this, SIGNAL(sigMachineRegistered(QUuid, bool)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigMachineGroupsChange(QUuid)),
            this, SIGNAL(sigMachineGroupsChange(QUuid)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigSessionStateChange(QUuid, KSessionState)),
            this, SIGNAL(sigSessionStateChange(QUuid, KSessionState)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigSnapshotTake(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotTake(QUuid, QUuid)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigSnapshotDelete(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotDelete(QUuid, QUuid)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigSnapshotChange(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotChange(QUuid, QUuid)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigSnapshotRestore(QUuid, QUuid)),
            this, SIGNAL(sigSnapshotRestore(QUuid, QUuid)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigCloudProviderListChanged()),
            this, SIGNAL(sigCloudProviderListChanged()),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigCloudProviderUninstall(QUuid)),
            this, SIGNAL(sigCloudProviderUninstall(QUuid)),
            Qt::BlockingQueuedConnection);
    connect(m_pProxy, SIGNAL(sigCloudProfileRegistered(QUuid, QString, bool)),
            this, SIGNAL(sigCloudProfileRegistered(QUuid, QString, bool)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigCloudProfileChanged(QUuid, QString)),
            this, SIGNAL(sigCloudProfileChanged(QUuid, QString)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigStorageControllerChange(QUuid, QString)),
            this, SIGNAL(sigStorageControllerChange(QUuid, QString)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigStorageDeviceChange(CMediumAttachment, bool, bool)),
            this, SIGNAL(sigStorageDeviceChange(CMediumAttachment, bool, bool)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigMediumChange(CMediumAttachment)),
            this, SIGNAL(sigMediumChange(CMediumAttachment)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigMediumConfigChange(CMedium)),
            this, SIGNAL(sigMediumConfigChange(CMedium)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigMediumRegistered(QUuid, KDeviceType, bool)),
            this, SIGNAL(sigMediumRegistered(QUuid, KDeviceType, bool)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigExtensionPackInstalled(QString)),
            this, SIGNAL(sigExtensionPackInstalled(QString)),
            Qt::QueuedConnection);
    connect(m_pProxy, SIGNAL(sigExtensionPackUninstalled(QString)),
            this, SIGNAL(sigExtensionPackUninstalled(QString)),
            Qt::QueuedConnection);
}


#include "UIVirtualBoxEventHandler.moc"
