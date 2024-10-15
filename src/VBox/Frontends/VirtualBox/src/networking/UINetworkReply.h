/* $Id: UINetworkReply.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkReply stuff declaration.
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

#ifndef FEQT_INCLUDED_SRC_networking_UINetworkReply_h
#define FEQT_INCLUDED_SRC_networking_UINetworkReply_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>
#include <QPointer>
#include <QUrl>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UINetworkDefs.h"

/* Forward declarations: */
class UINetworkReplyPrivate;

/** QObject extension
  * used as network-reply interface. */
class SHARED_LIBRARY_STUFF UINetworkReply : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about reply progress change.
      * @param  iBytesReceived  Holds the current amount of bytes received.
      * @param  iBytesTotal     Holds the total amount of bytes to be received. */
    void downloadProgress(qint64 iBytesReceived, qint64 iBytesTotal);

    /** Notifies listeners about reply has finished processing. */
    void finished();

public:

    /** Known error codes.
      * Came from QtNetwork module.
      * More to go on demand when necessary. */
    enum NetworkError
    {
        NoError,
        ConnectionRefusedError,
        RemoteHostClosedError,
        UrlNotFoundError,
        HostNotFoundError,
        OperationCanceledError,
        SslHandshakeFailedError,
        ProxyNotFoundError,
        ContentAccessDenied,
        AuthenticationRequiredError,
        ContentReSendError,
        UnknownNetworkError,
        ProtocolFailure,
    };

    /** Known header types.
      * Came from QtNetwork module.
      * More to go on demand when necessary. */
    enum KnownHeader
    {
        ContentTypeHeader,
        ContentLengthHeader,
        LastModifiedHeader,
        LocationHeader,
    };

    /** Constructs network-reply of the passed @a type for the passed @a url, @a strTarget and @a requestHeaders. */
    UINetworkReply(UINetworkRequestType type, const QUrl &url, const QString &strTarget, const UserDictionary &requestHeaders);
    /** Destructs reply. */
    ~UINetworkReply();

    /** Aborts reply. */
    void abort();

    /** Returns the URL of the reply. */
    QUrl url() const;

    /** Returns the last cached error of the reply. */
    NetworkError error() const;
    /** Returns the user-oriented string corresponding to the last cached error of the reply. */
    QString errorString() const;

    /** Returns binary content of the reply. */
    QByteArray readAll() const;
    /** Returns value for the cached reply header of the passed @a type. */
    QVariant header(UINetworkReply::KnownHeader header) const;

private:

    /** Holds the reply private data instance. */
    UINetworkReplyPrivate *m_pReply;
};

#endif /* !FEQT_INCLUDED_SRC_networking_UINetworkReply_h */

