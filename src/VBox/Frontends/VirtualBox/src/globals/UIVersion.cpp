/* $Id: UIVersion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVersion class implementation.
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
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

/* GUI includes: */
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIVersion.h"

/* COM includes: */
#include "CVirtualBox.h"

/* Other VBox includes: */
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Class UIVersion implementation.                                                                                              *
*********************************************************************************************************************************/

UIVersion::UIVersion()
    : m_x(-1)
    , m_y(-1)
    , m_z(-1)
{
}

UIVersion::UIVersion(const QString &strFullVersionInfo)
    : m_x(-1)
    , m_y(-1)
    , m_z(-1)
{
    const QStringList fullVersionInfo = strFullVersionInfo.split('_');
    if (fullVersionInfo.size() > 0)
    {
        const QStringList versionIndexes = fullVersionInfo.at(0).split('.');
        if (versionIndexes.size() > 0)
            m_x = versionIndexes[0].toInt();
        if (versionIndexes.size() > 1)
            m_y = versionIndexes[1].toInt();
        if (versionIndexes.size() > 2)
            m_z = versionIndexes[2].toInt();
    }
    if (fullVersionInfo.size() > 1)
        m_strPostfix = fullVersionInfo.at(1);
}

bool UIVersion::isValid() const
{
    return    (m_x != -1)
           && (m_y != -1)
           && (m_z != -1);
}

bool UIVersion::equal(const UIVersion &other) const
{
    return    (m_x == other.m_x)
           && (m_y == other.m_y)
           && (m_z == other.m_z)
           && (m_strPostfix == other.m_strPostfix);
}

bool UIVersion::operator<(const UIVersion &other) const
{
    return RTStrVersionCompare(toString().toUtf8().constData(), other.toString().toUtf8().constData()) < 0;
}

bool UIVersion::operator<=(const UIVersion &other) const
{
    return RTStrVersionCompare(toString().toUtf8().constData(), other.toString().toUtf8().constData()) <= 0;
}

bool UIVersion::operator>(const UIVersion &other) const
{
    return RTStrVersionCompare(toString().toUtf8().constData(), other.toString().toUtf8().constData()) > 0;
}

bool UIVersion::operator>=(const UIVersion &other) const
{
    return RTStrVersionCompare(toString().toUtf8().constData(), other.toString().toUtf8().constData()) >= 0;
}

QString UIVersion::toString() const
{
    return m_strPostfix.isEmpty() ? QString("%1.%2.%3").arg(m_x).arg(m_y).arg(m_z)
                                  : QString("%1.%2.%3_%4").arg(m_x).arg(m_y).arg(m_z).arg(m_strPostfix);
}

UIVersion UIVersion::effectiveReleasedVersion() const
{
    /* First, we just copy the current one: */
    UIVersion version = *this;

    /* If this version being developed: */
    if (version.z() % 2 == 1)
    {
        /* If this version being developed on release branch (we guess the right one): */
        if (version.z() < 97)
            version.setZ(version.z() - 1);
        /* If this version being developed on trunk (we use hardcoded one for now): */
        else
            version.setZ(8); /* Current .z for 6.0.z */
    }

    /* Finally, we just return that we have:  */
    return version;
}


/*********************************************************************************************************************************
*   Class UIVersionInfo implementation.                                                                                          *
*********************************************************************************************************************************/

/* static */
QString UIVersionInfo::s_strBrandingConfigFilePath = QString();

/* static */
QString UIVersionInfo::qtRTVersionString()
{
    return QString::fromLatin1(qVersion());
}

/* static */
uint UIVersionInfo::qtRTVersion()
{
    const QString strVersionRT = qtRTVersionString();
    return (strVersionRT.section('.', 0, 0).toInt() << 16) +
           (strVersionRT.section('.', 1, 1).toInt() << 8) +
           strVersionRT.section('.', 2, 2).toInt();
}

/* static */
uint UIVersionInfo::qtRTMajorVersion()
{
    return qtRTVersionString().section('.', 0, 0).toInt();
}

/* static */
uint UIVersionInfo::qtRTMinorVersion()
{
    return qtRTVersionString().section('.', 1, 1).toInt();
}

/* static */
uint UIVersionInfo::qtRTRevisionNumber()
{
    return qtRTVersionString().section('.', 2, 2).toInt();
}

/* static */
QString UIVersionInfo::qtCTVersionString()
{
    return QString::fromLatin1(QT_VERSION_STR);
}

/* static */
uint UIVersionInfo::qtCTVersion()
{
    const QString strVersionCompiled = qtCTVersionString();
    return   (strVersionCompiled.section('.', 0, 0).toInt() << 16)
           + (strVersionCompiled.section('.', 1, 1).toInt() << 8)
           + strVersionCompiled.section('.', 2, 2).toInt();
}

/* static */
QString UIVersionInfo::vboxVersionString()
{
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    return comVBox.GetVersion();
}

/* static */
QString UIVersionInfo::vboxVersionStringNormalized()
{
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    return comVBox.GetVersionNormalized();
}

/* static */
bool UIVersionInfo::isBeta()
{
    return vboxVersionString().contains(QRegularExpression("BETA|ALPHA", QRegularExpression::CaseInsensitiveOption));
}

/* static */
bool UIVersionInfo::showBetaLabel()
{
    return    isBeta()
           && !gEDataManager->preventBetaBuildLabel();
}

/* static */
bool UIVersionInfo::brandingIsActive(bool fForce /* = false */)
{
    if (fForce)
        return true;

    if (s_strBrandingConfigFilePath.isEmpty())
    {
        s_strBrandingConfigFilePath = QDir(QApplication::applicationDirPath()).absolutePath();
        s_strBrandingConfigFilePath += "/custom/custom.ini";
    }

    return QFile::exists(s_strBrandingConfigFilePath);
}

/* static */
QString UIVersionInfo::brandingGetKey(QString strKey)
{
    QSettings settings(s_strBrandingConfigFilePath, QSettings::IniFormat);
    return settings.value(QString("%1").arg(strKey)).toString();
}
