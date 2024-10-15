/* $Id: UINetworkRequestManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkRequestManager stuff declaration.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_networking_UINetworkRequestManager_h
#define FEQT_INCLUDED_SRC_networking_UINetworkRequestManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QUuid>

/* GUI inludes: */
#include "UILibraryDefs.h"
#include "UINetworkDefs.h"

/* Forward declarations: */
class QUrl;
class UINetworkCustomer;
class UINetworkRequest;

/** QObject class extension.
  * Providing network access for VirtualBox application purposes. */
class SHARED_LIBRARY_STUFF UINetworkRequestManager : public QObject
{
    Q_OBJECT;

public:

    /** Creates singleton instance. */
    static void create();
    /** Destroys singleton instance. */
    static void destroy();
    /** Returns the singleton instance. */
    static UINetworkRequestManager *instance();

    /** Creates network-request returning request ID.
      * @param  enmType         Brings request type.
      * @param  urls            Brings request urls, there can be few of them.
      * @param  strTarget       Brings request target path.
      * @param  requestHeaders  Brings request headers in dictionary form.
      * @param  pCustomer       Brings customer this request being ordered by. */
    QUuid createNetworkRequest(UINetworkRequestType enmType,
                               const QList<QUrl> &urls,
                               const QString &strTarget,
                               const UserDictionary &requestHeaders,
                               UINetworkCustomer *pCustomer);

    /** Aborts network-request. */
    void cancelNetworkRequest(const QUuid &uId);

protected:

    /** Constructs network manager. */
    UINetworkRequestManager();
    /** Destructs network manager. */
    virtual ~UINetworkRequestManager() RT_OVERRIDE RT_FINAL;

private slots:

    /** Handles progress for @a iReceived amount of bytes among @a iTotal. */
    void sltHandleNetworkRequestProgress(qint64 iReceived, qint64 iTotal);
    /** Handles request @a strError. */
    void sltHandleNetworkRequestFailure(const QString &strError);
    /** Handles request canceling. */
    void sltHandleNetworkRequestCancel();
    /** Handles request finishing. */
    void sltHandleNetworkRequestFinish();

    /** Handles signal about @a pNetworkCustomer being destroyed. */
    void sltHandleNetworkCustomerBeingDestroyed(UINetworkCustomer *pNetworkCustomer);

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups network-request with passed @a uId. */
    void cleanupNetworkRequest(const QUuid &uId);
    /** Cleanups all network-requests. */
    void cleanupNetworkRequests();
    /** Cleanups all. */
    void cleanup();

    /** Holds the singleton instance. */
    static UINetworkRequestManager *s_pInstance;

    /** Holds the map of current requests. */
    QMap<QUuid, UINetworkRequest*>   m_requests;
    /** Holds the map of current customers. */
    QMap<QUuid, UINetworkCustomer*>  m_customers;
};

/** Singleton Network Manager 'official' name. */
#define gNetworkManager UINetworkRequestManager::instance()

#endif /* !FEQT_INCLUDED_SRC_networking_UINetworkRequestManager_h */
