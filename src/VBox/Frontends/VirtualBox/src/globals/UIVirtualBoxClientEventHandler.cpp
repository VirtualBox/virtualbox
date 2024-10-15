/* $Id: UIVirtualBoxClientEventHandler.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVirtualBoxClientEventHandler class implementation.
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
#include "UIVirtualBoxClientEventHandler.h"

/* COM includes: */
#include "CEventListener.h"
#include "CEventSource.h"
#include "CVirtualBoxClient.h"


/** Private QObject extension providing UIVirtualBoxClientEventHandler with CVirtualBoxClient event-source. */
class UIVirtualBoxClientEventHandlerProxy : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about the VBoxSVC become @a fAvailable. */
    void sigVBoxSVCAvailabilityChange(bool fAvailable);

public:

    /** Constructs event proxy object on the basis of passed @a pParent. */
    UIVirtualBoxClientEventHandlerProxy(QObject *pParent);
    /** Destructs event proxy object. */
    ~UIVirtualBoxClientEventHandlerProxy();

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
*   Class UIVirtualBoxClientEventHandlerProxy implementation.                                                                    *
*********************************************************************************************************************************/

UIVirtualBoxClientEventHandlerProxy::UIVirtualBoxClientEventHandlerProxy(QObject *pParent)
    : QObject(pParent)
{
    /* Prepare: */
    prepare();
}

UIVirtualBoxClientEventHandlerProxy::~UIVirtualBoxClientEventHandlerProxy()
{
    /* Cleanup: */
    cleanup();
}

void UIVirtualBoxClientEventHandlerProxy::prepare()
{
    /* Prepare: */
    prepareListener();
    prepareConnections();
}

void UIVirtualBoxClientEventHandlerProxy::prepareListener()
{
    /* Create Main event listener instance: */
    m_pQtListener.createObject();
    m_pQtListener->init(new UIMainEventListener, this);
    m_comEventListener = CEventListener(m_pQtListener);

    /* Get VirtualBoxClient: */
    const CVirtualBoxClient comVBoxClient = gpGlobalSession->virtualBoxClient();
    /* Get VirtualBoxClient event source: */
    m_comEventSource = comVBoxClient.GetEventSource();
    Assert(comVBoxClient.isOk());

    /* Enumerate all the required event-types: */
    QVector<KVBoxEventType> eventTypes;
    eventTypes
        << KVBoxEventType_OnVBoxSVCAvailabilityChanged;

    /* Register event listener for event source aggregator: */
    m_comEventSource.RegisterListener(m_comEventListener, eventTypes, FALSE /* active? */);
    Assert(m_comEventSource.isOk());

    /* Register event sources in their listeners as well: */
    m_pQtListener->getWrapped()->registerSource(m_comEventSource, m_comEventListener);
}

void UIVirtualBoxClientEventHandlerProxy::prepareConnections()
{
    /* Create direct (sync) connections for signals of main event listener.
     * Keep in mind that the abstract Qt4 connection notation should be used here. */
    connect(m_pQtListener->getWrapped(), SIGNAL(sigVBoxSVCAvailabilityChange(bool)),
            this, SIGNAL(sigVBoxSVCAvailabilityChange(bool)),
            Qt::DirectConnection);
}

void UIVirtualBoxClientEventHandlerProxy::cleanupConnections()
{
    /* Nothing for now. */
}

void UIVirtualBoxClientEventHandlerProxy::cleanupListener()
{
    /* Unregister everything: */
    m_pQtListener->getWrapped()->unregisterSources();

    /* Unregister event listener for event source aggregator: */
    m_comEventSource.UnregisterListener(m_comEventListener);
    m_comEventSource.detach();
}

void UIVirtualBoxClientEventHandlerProxy::cleanup()
{
    /* Cleanup: */
    cleanupConnections();
    cleanupListener();
}


/*********************************************************************************************************************************
*   Class UIVirtualBoxClientEventHandler implementation.                                                                         *
*********************************************************************************************************************************/

/* static */
UIVirtualBoxClientEventHandler *UIVirtualBoxClientEventHandler::s_pInstance = 0;

/* static */
UIVirtualBoxClientEventHandler *UIVirtualBoxClientEventHandler::instance()
{
    if (!s_pInstance)
        s_pInstance = new UIVirtualBoxClientEventHandler;
    return s_pInstance;
}

/* static */
void UIVirtualBoxClientEventHandler::destroy()
{
    if (s_pInstance)
    {
        delete s_pInstance;
        s_pInstance = 0;
    }
}

UIVirtualBoxClientEventHandler::UIVirtualBoxClientEventHandler()
    : m_pProxy(new UIVirtualBoxClientEventHandlerProxy(this))
{
    /* Prepare: */
    prepare();
}

void UIVirtualBoxClientEventHandler::prepare()
{
    /* Prepare connections: */
    prepareConnections();
}

void UIVirtualBoxClientEventHandler::prepareConnections()
{
    /* Create queued (async) connections for signals of event proxy object.
     * Keep in mind that the abstract Qt4 connection notation should be used here. */
    connect(m_pProxy, SIGNAL(sigVBoxSVCAvailabilityChange(bool)),
            this, SIGNAL(sigVBoxSVCAvailabilityChange(bool)),
            Qt::QueuedConnection);
}


#include "UIVirtualBoxClientEventHandler.moc"

