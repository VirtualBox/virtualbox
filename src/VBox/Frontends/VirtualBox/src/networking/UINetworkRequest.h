/* $Id: UINetworkRequest.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkRequest class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networking_UINetworkRequest_h
#define FEQT_INCLUDED_SRC_networking_UINetworkRequest_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QPointer>

/* GUI inludes: */
#include "UILibraryDefs.h"
#include "UINetworkDefs.h"
#include "UINetworkReply.h"

/** QObject extension used as network-request container. */
class SHARED_LIBRARY_STUFF UINetworkRequest : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listener about progress started. */
    void sigStarted();
    /** Notifies listener about progress changed.
      * @param  iReceived  Brings the amount of bytes received.
      * @param  iTotal     Brings the amount of total bytes to receive. */
    void sigProgress(qint64 iReceived, qint64 iTotal);
    /** Notifies listener about progress failed.
      * @param  strError  Brings the error progress failed with. */
    void sigFailed(const QString &strError);
    /** Notifies listener about progress canceled. */
    void sigCanceled();
    /** Notifies listener about progress finished. */
    void sigFinished();

public:

    /** Constructs network-request.
      * @param  enmType         Brings request type.
      * @param  urls            Brings request urls, there can be few of them.
      * @param  strTarget       Brings request target path.
      * @param  requestHeaders  Brings request headers in dictionary form. */
    UINetworkRequest(UINetworkRequestType enmType,
                     const QList<QUrl> &urls,
                     const QString &strTarget,
                     const UserDictionary &requestHeaders);
    /** Destructs network-request. */
    virtual ~UINetworkRequest() RT_OVERRIDE RT_FINAL;

    /** Returns the request reply. */
    UINetworkReply *reply() { return m_pReply; }

public slots:

    /** Initiates request cancelling. */
    void sltCancel();

private slots:

    /** Handles reply about progress changed.
      * @param  iReceived  Brings the amount of bytes received.
      * @param  iTotal     Brings the amount of total bytes to receive. */
    void sltHandleNetworkReplyProgress(qint64 iReceived, qint64 iTotal);
    /** Handles reply about progress finished. */
    void sltHandleNetworkReplyFinish();

private:

    /** Prepares request. */
    void prepare();
    /** Prepares request's reply. */
    void prepareNetworkReply();

    /** Cleanups request's reply. */
    void cleanupNetworkReply();
    /** Cleanups request. */
    void cleanup();

    /** Holds the request type. */
    const UINetworkRequestType  m_enmType;
    /** Holds the request urls. */
    const QList<QUrl>           m_urls;
    /** Holds the request target. */
    const QString               m_strTarget;
    /** Holds the request headers. */
    const UserDictionary        m_requestHeaders;

    /** Holds current request url. */
    QUrl  m_url;
    /** Holds index of current request url. */
    int   m_iUrlIndex;
    /** Holds whether current request url is in progress. */
    bool  m_fRunning;

    /** Holds the request reply. */
    QPointer<UINetworkReply>  m_pReply;
};

#endif /* !FEQT_INCLUDED_SRC_networking_UINetworkRequest_h */
