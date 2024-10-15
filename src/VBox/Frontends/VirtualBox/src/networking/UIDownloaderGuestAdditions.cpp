/* $Id: UIDownloaderGuestAdditions.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDownloaderGuestAdditions class implementation.
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

/* Qt includes: */
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QVariant>

/* GUI includes: */
#include "QIFileDialog.h"
#include "UIDownloaderGuestAdditions.h"
#include "UIGlobalSession.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINetworkReply.h"
#include "UINotificationCenter.h"
#include "UIVersion.h"

/* Other VBox includes: */
#include <iprt/sha.h>


UIDownloaderGuestAdditions::UIDownloaderGuestAdditions()
{
    /* Get version number and adjust it for test and trunk builds. The server only has official releases. */
    const QString strVersion = UIVersion(UIVersionInfo::vboxVersionStringNormalized()).effectiveReleasedVersion().toString();

    /* Prepare source/target: */
    const QString strSourceName = QString("%1_%2.iso").arg(GUI_GuestAdditionsName, strVersion);
    const QString strSourcePath = QString("https://download.virtualbox.org/virtualbox/%1/").arg(strVersion);
    const QString strSource = strSourcePath + strSourceName;
    const QString strPathSHA256SumsFile = QString("https://www.virtualbox.org/download/hashes/%1/SHA256SUMS").arg(strVersion);
    const QString strTarget = QDir(gpGlobalSession->homeFolder()).absoluteFilePath(QString("%1.tmp").arg(strSourceName));

    /* Set source/target: */
    setSource(strSource);
    setTarget(strTarget);
    setPathSHA256SumsFile(strPathSHA256SumsFile);
}

QString UIDownloaderGuestAdditions::description() const
{
    return UIDownloader::description().arg(tr("VirtualBox Guest Additions"));
}

bool UIDownloaderGuestAdditions::askForDownloadingConfirmation(UINetworkReply *pReply)
{
    return msgCenter().confirmDownloadGuestAdditions(source().toString(), pReply->header(UINetworkReply::ContentLengthHeader).toInt());
}

void UIDownloaderGuestAdditions::handleDownloadedObject(UINetworkReply *pReply)
{
    m_receivedData = pReply->readAll();
}

void UIDownloaderGuestAdditions::handleVerifiedObject(UINetworkReply *pReply)
{
    /* Try to verify the SHA-256 checksum: */
    QString strCalculatedSumm;
    bool fSuccess = false;
    do
    {
        /* Read received data into the buffer: */
        const QByteArray receivedData(pReply->readAll());
        /* Make sure it's not empty: */
        if (receivedData.isEmpty())
            break;

        /* Parse buffer contents to dictionary: */
        const QStringList dictionary(QString(receivedData).split("\n", Qt::SkipEmptyParts));
        /* Make sure it's not empty: */
        if (dictionary.isEmpty())
            break;

        /* Parse each record to tags, look for the required one: */
        foreach (const QString &strRecord, dictionary)
        {
            const QString strFileName = strRecord.section(" *", 1);
            const QString strDownloadedSumm = strRecord.section(" *", 0, 0);
            if (strFileName == source().fileName())
            {
                /* Calc the SHA-256 on the bytes, creating a string: */
                uint8_t abHash[RTSHA256_HASH_SIZE];
                RTSha256(m_receivedData.constData(), m_receivedData.length(), abHash);
                char szDigest[RTSHA256_DIGEST_LEN + 1];
                int rc = RTSha256ToString(abHash, szDigest, sizeof(szDigest));
                if (RT_FAILURE(rc))
                {
                    AssertRC(rc);
                    szDigest[0] = '\0';
                }
                strCalculatedSumm = szDigest;
                //printf("Downloaded SHA-256 summ: [%s]\n", strDownloadedSumm.toUtf8().constData());
                //printf("Calculated SHA-256 summ: [%s]\n", strCalculatedSumm.toUtf8().constData());
                /* Make sure checksum is valid: */
                fSuccess = strDownloadedSumm == strCalculatedSumm;
                break;
            }
        }
    }
    while (false);

    /* If SHA-256 checksum verification failed: */
    if (!fSuccess)
    {
        /* Warn the user about additions-image was downloaded and saved but checksum is invalid: */
        UINotificationMessage::cannotValidateGuestAdditionsSHA256Sum(source().toString(), QDir::toNativeSeparators(target()));
        return;
    }

    /* Make sure temporary file exists.  If we have
     * reached this place, it's already written and verified. */
    const QString strTempFilename = target();
    if (!QFile::exists(strTempFilename))
    {
        /* But still we are providing a failsafe.
         * Since we can try to write it again. */
        QFile file(strTempFilename);
        if (!file.open(QIODevice::WriteOnly))
            AssertFailedReturnVoid();
        file.write(m_receivedData);
    }

    /* Rename temporary file to target one.  This can require a number
     * of tries to let user choose the place to save file to. */
    QString strNetTarget = target();
    strNetTarget.remove(QRegularExpression("\\.tmp$"));
    setTarget(strNetTarget);
    while (true)
    {
        /* Make sure target file doesn't exist: */
        bool fTargetFileExists = QFile::exists(target());
        if (fTargetFileExists)
        {
            /* We should ask user about file rewriting (or exit otherwise): */
            if (!msgCenter().confirmOverridingFile(QDir::toNativeSeparators(target())))
                break;
            /* And remove file if rewriting confirmed: */
            if (QFile::remove(target()))
                fTargetFileExists = false;
        }

        /* Try to rename temporary file to target one (would fail if target file still exists): */
        const bool fFileRenamed = !fTargetFileExists && QFile::rename(strTempFilename, target());

        /* If file renamed: */
        if (fFileRenamed)
        {
            /* Warn the user about additions-image downloaded and saved, propose to mount it (and/or exit in any case): */
            if (msgCenter().proposeMountGuestAdditions(source().toString(), QDir::toNativeSeparators(target())))
                emit sigDownloadFinished(target());
            break;
        }
        else
        {
            /* Warn the user about additions-image was downloaded but was NOT saved: */
            msgCenter().cannotSaveGuestAdditions(source().toString(), QDir::toNativeSeparators(target()));
            /* Ask the user for another location for the additions-image file: */
            const QString strTarget = QIFileDialog::getExistingDirectory(QFileInfo(target()).absolutePath(),
                                                                         windowManager().mainWindowShown(),
                                                                         tr("Select folder to save Guest Additions image to"), true);

            /* Check if user had really set a new target (and exit in opposite case): */
            if (!strTarget.isNull())
                setTarget(QDir(strTarget).absoluteFilePath(QFileInfo(target()).fileName()));
            else
                break;
        }
    }
}
