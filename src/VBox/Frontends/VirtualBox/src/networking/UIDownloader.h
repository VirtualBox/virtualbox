/* $Id: UIDownloader.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDownloader class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networking_UIDownloader_h
#define FEQT_INCLUDED_SRC_networking_UIDownloader_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUrl>
#include <QList>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UINetworkCustomer.h"
#include "UINetworkDefs.h"

/* Forward declarations: */
class QString;
class UINetworkReply;

/** Downloader interface.
  * UINetworkCustomer class extension which allows background http downloading. */
class SHARED_LIBRARY_STUFF UIDownloader : public UINetworkCustomer
{
    Q_OBJECT;

signals:

    /** Signals to start acknowledging. */
    void sigToStartAcknowledging();
    /** Signals to start downloading. */
    void sigToStartDownloading();
    /** Signals to start verifying. */
    void sigToStartVerifying();

    /** Notifies listeners about progress change to @a iPercent. */
    void sigProgressChange(ulong uPercent);
    /** Notifies listeners about progress failed with @a strError. */
    void sigProgressFailed(const QString &strError);
    /** Notifies listeners about progress canceled. */
    void sigProgressCanceled();
    /** Notifies listeners about progress finished. */
    void sigProgressFinished();

public:

    /** Constructs downloader. */
    UIDownloader();

public slots:

    /** Starts the sequence. */
    void start() { startDelayedAcknowledging(); }
    /** Cancels the sequence. */
    void cancel() { cancelNetworkRequest(); }

protected slots:

    /** Performs acknowledging part. */
    void sltStartAcknowledging();
    /** Performs downloading part. */
    void sltStartDownloading();
    /** Performs verifying part. */
    void sltStartVerifying();

protected:

    /** Appends subsequent source to try to download from. */
    void addSource(const QString &strSource) { m_sources << QUrl(strSource); }
    /** Defines the only one source to try to download from. */
    void setSource(const QString &strSource) { m_sources.clear(); addSource(strSource); }
    /** Returns a list of sources to try to download from. */
    const QList<QUrl> &sources() const { return m_sources; }
    /** Returns a current source to try to download from. */
    const QUrl &source() const { return m_source; }

    /** Defines the @a strTarget file-path used to save downloaded file to. */
    void setTarget(const QString &strTarget) { m_strTarget = strTarget; }
    /** Returns the target file-path used to save downloaded file to. */
    const QString &target() const { return m_strTarget; }

    /** Defines the @a strPathSHA256SumsFile. */
    void setPathSHA256SumsFile(const QString &strPathSHA256SumsFile) { m_strPathSHA256SumsFile = strPathSHA256SumsFile; }
    /** Returns the SHA-256 sums file-path. */
    QString pathSHA256SumsFile() const { return m_strPathSHA256SumsFile; }

    /** Returns description of the current network operation. */
    virtual QString description() const RT_OVERRIDE;

    /** Handles network-reply progress for @a iReceived bytes of @a iTotal. */
    virtual void processNetworkReplyProgress(qint64 iReceived, qint64 iTotal) RT_OVERRIDE;
    /** Handles network-reply failed with specified @a strError. */
    virtual void processNetworkReplyFailed(const QString &strError) RT_OVERRIDE;
    /** Handles network-reply cancel request for @a pReply. */
    virtual void processNetworkReplyCanceled(UINetworkReply *pReply) RT_OVERRIDE;
    /** Handles network-reply finish for @a pReply. */
    virtual void processNetworkReplyFinished(UINetworkReply *pReply) RT_OVERRIDE;

    /** Asks user for downloading confirmation for passed @a pReply. */
    virtual bool askForDownloadingConfirmation(UINetworkReply *pReply) = 0;
    /** Handles downloaded object for passed @a pReply. */
    virtual void handleDownloadedObject(UINetworkReply *pReply) = 0;
    /** Handles verified object for passed @a pReply. */
    virtual void handleVerifiedObject(UINetworkReply *pReply) { Q_UNUSED(pReply); }

private:

    /** UIDownloader states. */
    enum UIDownloaderState
    {
        UIDownloaderState_Null,
        UIDownloaderState_Acknowledging,
        UIDownloaderState_Downloading,
        UIDownloaderState_Verifying
    };

    /** Starts delayed acknowledging. */
    void startDelayedAcknowledging() { emit sigToStartAcknowledging(); }
    /** Starts delayed downloading. */
    void startDelayedDownloading() { emit sigToStartDownloading(); }
    /** Starts delayed verifying. */
    void startDelayedVerifying() { emit sigToStartVerifying(); }

    /** Handles acknowledging result. */
    void handleAcknowledgingResult(UINetworkReply *pReply);
    /** Handles downloading result. */
    void handleDownloadingResult(UINetworkReply *pReply);
    /** Handles verifying result. */
    void handleVerifyingResult(UINetworkReply *pReply);

    /** Holds the downloader state. */
    UIDownloaderState m_state;

    /** Holds the downloading sources. */
    QList<QUrl> m_sources;
    /** Holds the current downloading source. */
    QUrl        m_source;

    /** Holds the downloading target path. */
    QString m_strTarget;

    /** Holds the SHA-256 sums file path. */
    QString m_strPathSHA256SumsFile;
};

#endif /* !FEQT_INCLUDED_SRC_networking_UIDownloader_h */

