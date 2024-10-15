/* $Id: UIDownloaderExtensionPack.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDownloaderExtensionPack class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networking_UIDownloaderExtensionPack_h
#define FEQT_INCLUDED_SRC_networking_UIDownloaderExtensionPack_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIDownloader.h"

/* Forward declarations: */
class QByteArray;

/** UIDownloader extension for background extension-pack downloading. */
class SHARED_LIBRARY_STUFF UIDownloaderExtensionPack : public UIDownloader
{
    Q_OBJECT;

signals:

    /** Notifies listeners about downloading finished.
      * @param  strSource  Brings the downloading source.
      * @param  strTarget  Brings the downloading target.
      * @param  strHash    Brings the downloaded file hash. */
    void sigDownloadFinished(const QString &strSource, const QString &strTarget, const QString &strHash);

public:

    /** Constructs downloader. */
    UIDownloaderExtensionPack();

private:

    /** Returns description of the current network operation. */
    virtual QString description() const RT_OVERRIDE;

    /** Asks user for downloading confirmation for passed @a pReply. */
    virtual bool askForDownloadingConfirmation(UINetworkReply *pReply) RT_OVERRIDE;
    /** Handles downloaded object for passed @a pReply. */
    virtual void handleDownloadedObject(UINetworkReply *pReply) RT_OVERRIDE;
    /** Handles verified object for passed @a pReply. */
    virtual void handleVerifiedObject(UINetworkReply *pReply) RT_OVERRIDE;

    /** Holds the cached received data awaiting for verification. */
    QByteArray m_receivedData;
};

#endif /* !FEQT_INCLUDED_SRC_networking_UIDownloaderExtensionPack_h */
