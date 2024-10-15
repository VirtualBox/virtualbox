/* $Id: UINewVersionChecker.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINewVersionChecker class implementation.
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
#include <QRegularExpression>
#include <QUrlQuery>

/* GUI includes: */
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UINetworkReply.h"
#include "UINewVersionChecker.h"
#include "UINotificationCenter.h"
#include "UIUpdateDefs.h"
#include "UIVersion.h"
#ifdef Q_OS_LINUX
# include "QIProcess.h"
#endif

/* Other VBox includes: */
#include <iprt/system.h>
#ifdef Q_OS_LINUX
# include <iprt/path.h>
#endif


UINewVersionChecker::UINewVersionChecker(bool fForcedCall)
    : m_fForcedCall(fForcedCall)
    , m_url("https://update.virtualbox.org/query.php")
{
}

void UINewVersionChecker::start()
{
    /* Compose query: */
    QUrlQuery url;
    url.addQueryItem("platform", gpGlobalSession->virtualBox().GetPackageType());
    /* Check if branding is active: */
    if (UIVersionInfo::brandingIsActive())
    {
        /* Branding: Check whether we have a local branding file which tells us our version suffix "FOO"
                     (e.g. 3.06.54321_FOO) to identify this installation: */
        url.addQueryItem("version", QString("%1_%2_%3").arg(gpGlobalSession->virtualBox().GetVersion())
                                                       .arg(gpGlobalSession->virtualBox().GetRevision())
                                                       .arg(UIVersionInfo::brandingGetKey("VerSuffix")));
    }
    else
    {
        /* Use hard coded version set by VBOX_VERSION_STRING: */
        url.addQueryItem("version", QString("%1_%2").arg(gpGlobalSession->virtualBox().GetVersion())
                                                    .arg(gpGlobalSession->virtualBox().GetRevision()));
    }
    url.addQueryItem("count", QString::number(gEDataManager->applicationUpdateCheckCounter()));
    url.addQueryItem("branch", VBoxUpdateData(gEDataManager->applicationUpdateData()).updateChannelName());
    const QString strUserAgent(QString("VirtualBox %1 <%2>").arg(gpGlobalSession->virtualBox().GetVersion()).arg(platformInfo()));

    /* Send GET request: */
    UserDictionary headers;
    headers["User-Agent"] = strUserAgent;
    QUrl fullUrl(m_url);
    fullUrl.setQuery(url);
    createNetworkRequest(UINetworkRequestType_GET, QList<QUrl>() << fullUrl, QString(), headers);
}

void UINewVersionChecker::cancel()
{
    cancelNetworkRequest();
}

void UINewVersionChecker::processNetworkReplyProgress(qint64, qint64)
{
}

void UINewVersionChecker::processNetworkReplyFailed(const QString &strError)
{
    emit sigProgressFailed(strError);
}

void UINewVersionChecker::processNetworkReplyCanceled(UINetworkReply *)
{
    emit sigProgressCanceled();
}

void UINewVersionChecker::processNetworkReplyFinished(UINetworkReply *pReply)
{
    /* Deserialize incoming data: */
    const QString strResponseData(pReply->readAll());

#ifdef VBOX_NEW_VERSION_TEST
    strResponseData = VBOX_NEW_VERSION_TEST;
#endif
    /* Newer version of necessary package found: */
    if (strResponseData.indexOf(QRegularExpression("^\\d+\\.\\d+\\.\\d+(_[0-9A-Z]+)? \\S+$")) == 0)
    {
        const QStringList response = strResponseData.split(" ", Qt::SkipEmptyParts);
        UINotificationMessage::showUpdateSuccess(response[0], response[1]);
    }
    /* No newer version of necessary package found: */
    else
    {
        if (isItForcedCall())
            UINotificationMessage::showUpdateNotFound();
    }

    /* Increment update check counter: */
    gEDataManager->incrementApplicationUpdateCheckCounter();

    /* Notify about completion: */
    emit sigProgressFinished();
}

/* static */
QString UINewVersionChecker::platformInfo()
{
    /* Prepare platform report: */
    QString strPlatform;

#if defined (Q_OS_WIN)
    strPlatform = "win";
#elif defined (Q_OS_LINUX)
    strPlatform = "linux";
#elif defined (Q_OS_MACX)
    strPlatform = "macosx";
#elif defined (Q_OS_OS2)
    strPlatform = "os2";
#elif defined (Q_OS_FREEBSD)
    strPlatform = "freebsd";
#elif defined (Q_OS_SOLARIS)
    strPlatform = "solaris";
#else
    strPlatform = "unknown";
#endif

    /* The format is <system>.<bitness>: */
    strPlatform += QString(".%1").arg(ARCH_BITS);

    /* Add more system information: */
    int vrc;
#ifdef Q_OS_LINUX
    // WORKAROUND:
    // On Linux we try to generate information using script first of all..

    /* Get script path: */
    char szAppPrivPath[RTPATH_MAX];
    vrc = RTPathAppPrivateNoArch(szAppPrivPath, sizeof(szAppPrivPath));
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
    {
        /* Run script: */
        QByteArray result = QIProcess::singleShot(QString(szAppPrivPath) + "/VBoxSysInfo.sh");
        if (!result.isNull())
            strPlatform += QString(" [%1]").arg(QString(result).trimmed());
        else
            vrc = VERR_TRY_AGAIN; /* (take the fallback path) */
    }
    if (RT_FAILURE(vrc))
#endif /* Q_OS_LINUX */
    {
        /* Use RTSystemQueryOSInfo: */
        char szTmp[256];
        QStringList components;

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            components << QString("Product: %1").arg(szTmp);

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            components << QString("Release: %1").arg(szTmp);

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            components << QString("Version: %1").arg(szTmp);

        vrc = RTSystemQueryOSInfo(RTSYSOSINFO_SERVICE_PACK, szTmp, sizeof(szTmp));
        if ((RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW) && szTmp[0] != '\0')
            components << QString("SP: %1").arg(szTmp);

        if (!components.isEmpty())
            strPlatform += QString(" [%1]").arg(components.join(" | "));
    }

    return strPlatform;
}
