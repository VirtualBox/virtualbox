/* $Id: UIDownloaderExtensionPack.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDownloaderExtensionPack class implementation.
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

/* Qt includes: */
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QVariant>

/* GUI includes: */
#include "QIFileDialog.h"
#include "UIDownloaderExtensionPack.h"
#include "UIGlobalSession.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINetworkReply.h"
#include "UINotificationCenter.h"
#include "UIVersion.h"

/* Other VBox includes: */
#include <iprt/sha.h>


UIDownloaderExtensionPack::UIDownloaderExtensionPack()
{
    /* Get version number and adjust it for test and trunk builds. The server only has official releases. */
    const QString strVersion = UIVersion(UIVersionInfo::vboxVersionStringNormalized()).effectiveReleasedVersion().toString();

    /* Prepare source/target: */
    const QString strUnderscoredName = QString(GUI_ExtPackName).replace(' ', '_');
    const QString strSourceName = QString("%1-%2.vbox-extpack").arg(strUnderscoredName, strVersion);
    const QString strSourcePath = QString("https://download.virtualbox.org/virtualbox/%1/").arg(strVersion);
    const QString strSource = strSourcePath + strSourceName;
    const QString strPathSHA256SumsFile = QString("https://www.virtualbox.org/download/hashes/%1/SHA256SUMS").arg(strVersion);
    const QString strTarget = QDir(gpGlobalSession->homeFolder()).absoluteFilePath(strSourceName);

    /* Set source/target: */
    setSource(strSource);
    setTarget(strTarget);
    setPathSHA256SumsFile(strPathSHA256SumsFile);
}

QString UIDownloaderExtensionPack::description() const
{
    return UIDownloader::description().arg(tr("VirtualBox Extension Pack"));
}

bool UIDownloaderExtensionPack::askForDownloadingConfirmation(UINetworkReply *pReply)
{
    return msgCenter().confirmDownloadExtensionPack(GUI_ExtPackName, source().toString(), pReply->header(UINetworkReply::ContentLengthHeader).toInt());
}

void UIDownloaderExtensionPack::handleDownloadedObject(UINetworkReply *pReply)
{
    m_receivedData = pReply->readAll();
}

void UIDownloaderExtensionPack::handleVerifiedObject(UINetworkReply *pReply)
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
            QRegularExpression separator(" \\*|  ");
            const QString strFileName = strRecord.section(separator, 1);
            const QString strDownloadedSumm = strRecord.section(separator, 0, 0);
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
        UINotificationMessage::cannotValidateExtentionPackSHA256Sum(GUI_ExtPackName, source().toString(), QDir::toNativeSeparators(target()));
        return;
    }

    /* Serialize that buffer into the file: */
    while (true)
    {
        /* Make sure the file already exists.  If we reached
         * this place, it's already written and checked. */
        QFile file(target());
        bool fSuccess = false;
        /* Check step. Try to open file for reading first. */
        if (file.open(QIODevice::ReadOnly))
            fSuccess = true;
        /* Failsafe step. Try to open file for writing otherwise. */
        if (!fSuccess && file.open(QIODevice::WriteOnly))
        {
            /* Write buffer into the file: */
            file.write(m_receivedData);
            file.close();
            fSuccess = true;
        }
        /* If the file already exists or was just written: */
        if (fSuccess)
        {
            /* Warn the listener about extension-pack was downloaded: */
            emit sigDownloadFinished(source().toString(), target(), strCalculatedSumm);
            break;
        }

        /* Warn the user about extension-pack was downloaded but was NOT saved: */
        msgCenter().cannotSaveExtensionPack(GUI_ExtPackName, source().toString(), QDir::toNativeSeparators(target()));

        /* Ask the user for another location for the extension-pack file: */
        QString strTarget = QIFileDialog::getExistingDirectory(QFileInfo(target()).absolutePath(),
                                                               windowManager().mainWindowShown(),
                                                               tr("Select folder to save %1 to").arg(GUI_ExtPackName), true);

        /* Check if user had really set a new target: */
        if (!strTarget.isNull())
            setTarget(QDir(strTarget).absoluteFilePath(QFileInfo(target()).fileName()));
        else
            break;
    }
}
