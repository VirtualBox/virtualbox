/* $Id: UIDownloaderUserManual.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDownloaderUserManual class implementation.
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
#include <QVariant>

/* GUI includes: */
#include "QIFileDialog.h"
#include "UICommon.h"
#include "UIDownloaderUserManual.h"
#include "UIGlobalSession.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINetworkReply.h"
#include "UINotificationCenter.h"
#include "UIVersion.h"


UIDownloaderUserManual::UIDownloaderUserManual()
{
    /* Get version number and adjust it for test and trunk builds. The server only has official releases. */
    const QString strVersion = UIVersion(UIVersionInfo::vboxVersionStringNormalized()).effectiveReleasedVersion().toString();

    /* Compose User Guide filename: */
    QString strUserManualFullFileName = uiCommon().helpFile();
    QString strUserManualShortFileName = QFileInfo(strUserManualFullFileName).fileName();

    /* Add sources: */
    QString strSource1 = QString("https://download.virtualbox.org/virtualbox/%1/").arg(strVersion) + strUserManualShortFileName;
    QString strSource2 = QString("https://download.virtualbox.org/virtualbox/") + strUserManualShortFileName;
    addSource(strSource1);
    addSource(strSource2);

    /* Set target: */
    QString strUserManualDestination = QDir(gpGlobalSession->homeFolder()).absoluteFilePath(strUserManualShortFileName);
    setTarget(strUserManualDestination);
}

QString UIDownloaderUserManual::description() const
{
    return UIDownloader::description().arg(tr("VirtualBox User Guide"));
}

bool UIDownloaderUserManual::askForDownloadingConfirmation(UINetworkReply *pReply)
{
    return msgCenter().confirmDownloadUserManual(source().toString(), pReply->header(UINetworkReply::ContentLengthHeader).toInt());
}

void UIDownloaderUserManual::handleDownloadedObject(UINetworkReply *pReply)
{
    /* Read received data into the buffer: */
    QByteArray receivedData(pReply->readAll());
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
            file.write(receivedData);
            file.close();
            fSuccess = true;
        }
        /* If the file already exists or was just written: */
        if (fSuccess)
        {
            /* Warn the user about user-manual loaded and saved: */
            UINotificationMessage::warnAboutUserManualDownloaded(source().toString(), QDir::toNativeSeparators(target()));
            /* Warn the listener about user-manual was downloaded: */
            emit sigDownloadFinished(target());
            break;
        }

        /* Warn user about user-manual was downloaded but was NOT saved: */
        msgCenter().cannotSaveUserManual(source().toString(), QDir::toNativeSeparators(target()));

        /* Ask the user for another location for the user-manual file: */
        QString strTarget = QIFileDialog::getExistingDirectory(QFileInfo(target()).absolutePath(),
                                                               windowManager().mainWindowShown(),
                                                               tr("Select folder to save User Guide to"), true);

        /* Check if user had really set a new target: */
        if (!strTarget.isNull())
            setTarget(QDir(strTarget).absoluteFilePath(QFileInfo(target()).fileName()));
        else
            break;
    }
}
