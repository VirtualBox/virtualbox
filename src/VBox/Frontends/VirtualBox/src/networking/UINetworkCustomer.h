/* $Id: UINetworkCustomer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkCustomer class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_networking_UINetworkCustomer_h
#define FEQT_INCLUDED_SRC_networking_UINetworkCustomer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QUrl>
#include <QUuid>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UINetworkDefs.h"

/* Forward declarations: */
class UINetworkReply;

/** Interface to access UINetworkRequestManager protected functionality. */
class SHARED_LIBRARY_STUFF UINetworkCustomer : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a pNetworkCustomer being destroyed. */
    void sigBeingDestroyed(UINetworkCustomer *pNetworkCustomer);

public:

    /** Constructs network customer passing @a pParent to the base-class. */
    UINetworkCustomer();
    /** Destructs network customer. */
    virtual ~UINetworkCustomer() RT_OVERRIDE;

    /** Returns description of the current network operation. */
    virtual QString description() const { return QString(); }

    /** Handles network reply progress for @a iReceived amount of bytes among @a iTotal. */
    virtual void processNetworkReplyProgress(qint64 iReceived, qint64 iTotal) = 0;
    /** Handles network reply failed with specified @a strError. */
    virtual void processNetworkReplyFailed(const QString &strError) = 0;
    /** Handles network reply canceling for a passed @a pReply. */
    virtual void processNetworkReplyCanceled(UINetworkReply *pReply) = 0;
    /** Handles network reply finishing for a passed @a pReply. */
    virtual void processNetworkReplyFinished(UINetworkReply *pReply) = 0;

protected:

    /** Creates network-request.
      * @param  enmType         Brings request type.
      * @param  urls            Brings request urls, there can be few of them.
      * @param  strTarget       Brings request target path.
      * @param  requestHeaders  Brings request headers in dictionary form. */
    void createNetworkRequest(UINetworkRequestType enmType,
                              const QList<QUrl> urls,
                              const QString &strTarget = QString(),
                              const UserDictionary requestHeaders = UserDictionary());

    /** Aborts network-request. */
    void cancelNetworkRequest();

private:

    /** Holds the network-request ID. */
    QUuid  m_uId;
};

#endif /* !FEQT_INCLUDED_SRC_networking_UINetworkCustomer_h */
