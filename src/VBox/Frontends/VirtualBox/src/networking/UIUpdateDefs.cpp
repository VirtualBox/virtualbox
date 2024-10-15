/* $Id: UIUpdateDefs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Update routine related implementations.
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
#include <QCoreApplication>
#include <QLocale>
#include <QStringList>

/* GUI includes: */
#include "UINotificationCenter.h"
#include "UIUpdateDefs.h"
#include "UIVersion.h"

/* COM includes: */
#include "CUpdateAgent.h"


/* static: */
VBoxUpdateDayList VBoxUpdateData::s_days = VBoxUpdateDayList();

/* static */
void VBoxUpdateData::populate()
{
    /* Clear list initially: */
    s_days.clear();

    // WORKAROUND:
    // To avoid re-translation complexity
    // all values will be retranslated separately.

    /* Separately retranslate each day: */
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "1 day"),   "1 d",   86400);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "2 days"),  "2 d",  172800);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "3 days"),  "3 d",  259200);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "4 days"),  "4 d",  345600);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "5 days"),  "5 d",  432000);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "6 days"),  "6 d",  518400);

    /* Separately retranslate each week: */
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "1 week"),  "1 w",  604800);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "2 weeks"), "2 w", 1209600);
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "3 weeks"), "3 w", 1814400);

    /* Separately retranslate each month: */
    s_days << VBoxUpdateDay(QCoreApplication::translate("UIUpdateManager", "1 month"), "1 m", 2592000);
}

/* static */
QStringList VBoxUpdateData::list()
{
    QStringList result;
    foreach (const VBoxUpdateDay &day, s_days)
        result << day.val;
    return result;
}

VBoxUpdateData::VBoxUpdateData(const QString &strData /* = QString("never") */)
    : m_strData(strData)
    , m_fCheckEnabled(false)
    , m_fCheckRequired(false)
    , m_enmUpdatePeriod(UpdatePeriodType_Never)
    , m_enmUpdateChannel(KUpdateChannel_Invalid)
{
    /* Skip 'never' case: */
    if (m_strData == "never")
        return;

    /* Check is enabled in all cases besides 'never': */
    m_fCheckEnabled = true;

    /* Parse the data we have: */
    const QStringList parser = m_strData.split(", ", Qt::SkipEmptyParts);

    /* Parse 'period' value: */
    if (parser.size() > 0)
    {
        if (s_days.isEmpty())
            populate();
        m_enmUpdatePeriod = (UpdatePeriodType)s_days.indexOf(VBoxUpdateDay(QString(), parser.at(0), 0));
        if (m_enmUpdatePeriod == UpdatePeriodType_Never)
            m_enmUpdatePeriod = UpdatePeriodType_1Day;
    }

    /* Parse 'date' value: */
    if (parser.size() > 1)
    {
        QDate date = QDate::fromString(parser.at(1), Qt::ISODate);
        m_date = date.isValid() ? date : QDate::currentDate();
    }

    /* Parse 'update channel' value: */
    if (parser.size() > 2)
    {
        m_enmUpdateChannel = updateChannelFromInternalString(parser.at(2));
    }

    /* Parse 'version' value: */
    if (parser.size() > 3)
    {
        m_version = UIVersion(parser.at(3));
    }

    /* Decide whether we need to check: */
    m_fCheckRequired =    (QDate::currentDate() >= date())
                       && (   !version().isValid()
                           || version() != UIVersion(UIVersionInfo::vboxVersionStringNormalized()));
}

VBoxUpdateData::VBoxUpdateData(bool fCheckEnabled, UpdatePeriodType enmUpdatePeriod, KUpdateChannel enmUpdateChannel)
    : m_strData("never")
    , m_fCheckEnabled(fCheckEnabled)
    , m_fCheckRequired(false)
    , m_enmUpdatePeriod(enmUpdatePeriod)
    , m_enmUpdateChannel(enmUpdateChannel)
{
    /* Skip 'check disabled' case: */
    if (!m_fCheckEnabled)
        return;

    /* Encode 'period' value: */
    if (s_days.isEmpty())
        populate();
    const QString strRemindPeriod = s_days.at(m_enmUpdatePeriod).key;

    /* Encode 'date' value: */
    m_date = QDate::currentDate();
    QStringList parser(strRemindPeriod.split(' '));
    if (parser[1] == "d")
        m_date = m_date.addDays(parser[0].toInt());
    else if (parser[1] == "w")
        m_date = m_date.addDays(parser[0].toInt() * 7);
    else if (parser[1] == "m")
        m_date = m_date.addDays(parser[0].toInt() * 30);
    const QString strRemindDate = m_date.toString(Qt::ISODate);

    /* Encode 'update channel' value: */
    const QString strUpdateChannel = updateChannelName();

    /* Encode 'version' value: */
    m_version = UIVersion(UIVersionInfo::vboxVersionStringNormalized());
    const QString strVersionValue = m_version.toString();

    /* Compose m_strData: */
    m_strData = QString("%1, %2, %3, %4").arg(strRemindPeriod, strRemindDate, strUpdateChannel, strVersionValue);

    /* Decide whether we need to check: */
    m_fCheckRequired =    (QDate::currentDate() >= date())
                       && (   !version().isValid()
                           || version() != UIVersion(UIVersionInfo::vboxVersionStringNormalized()));
}

bool VBoxUpdateData::load(const CHost &comHost)
{
    /* Acquire update agent: */
    CUpdateAgent comAgent = comHost.GetUpdateHost();
    if (!comHost.isOk())
    {
        UINotificationMessage::cannotAcquireHostParameter(comHost);
        return false;
    }

    /* Fetch whether agent is enabled: */
    const BOOL fEnabled = comAgent.GetEnabled();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_fCheckEnabled = fEnabled;

    /* Fetch 'period' value: */
    const ULONG uFrequency = comAgent.GetCheckFrequency();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_enmUpdatePeriod = gatherSuitablePeriod(uFrequency);

    /* Fetch 'date' value: */
    const QString strLastDate = comAgent.GetLastCheckDate();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_date = QDate::fromString(strLastDate, Qt::ISODate);
    const ULONG uFrequencyInDays = (uFrequency / 86400) + 1;
    m_date = m_date.addDays(uFrequencyInDays);

    /* Fetch 'update channel' value: */
    KUpdateChannel enmUpdateChannel = comAgent.GetChannel();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_enmUpdateChannel = enmUpdateChannel;

    /* Fetch 'version' value: */
    const QString strVersion = comAgent.GetVersion();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_version = strVersion;

    /* Fetch whether we need to check: */
    const BOOL fNeedToCheck = comAgent.GetIsCheckNeeded();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_fCheckRequired = fNeedToCheck;

    /* Optional stuff goes last; Fetch supported update channels: */
    const QVector<KUpdateChannel> supportedUpdateChannels = comAgent.GetSupportedChannels();
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotAcquireUpdateAgentParameter(comAgent);
        return false;
    }
    m_supportedUpdateChannels = supportedUpdateChannels;

    /* Success finally: */
    return true;
}

bool VBoxUpdateData::save(const CHost &comHost) const
{
    /* Acquire update agent: */
    CUpdateAgent comAgent = comHost.GetUpdateHost();
    if (!comHost.isOk())
    {
        UINotificationMessage::cannotAcquireHostParameter(comHost);
        return false;
    }

    /* Save whether agent is enabled: */
    comAgent.SetEnabled(m_fCheckEnabled);
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotChangeUpdateAgentParameter(comAgent);
        return false;
    }

    /* Save 'period' value: */
    comAgent.SetCheckFrequency(s_days.at(m_enmUpdatePeriod).length);
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotChangeUpdateAgentParameter(comAgent);
        return false;
    }

    /* Save 'update channel' value: */
    comAgent.SetChannel(m_enmUpdateChannel);
    if (!comAgent.isOk())
    {
        UINotificationMessage::cannotChangeUpdateAgentParameter(comAgent);
        return false;
    }

    /* Success finally: */
    return true;
}

bool VBoxUpdateData::isCheckEnabled() const
{
    return m_fCheckEnabled;
}

bool VBoxUpdateData::isCheckRequired() const
{
    return m_fCheckRequired;
}

QString VBoxUpdateData::data() const
{
    return m_strData;
}

UpdatePeriodType VBoxUpdateData::updatePeriod() const
{
    return m_enmUpdatePeriod;
}

QDate VBoxUpdateData::date() const
{
    return m_date;
}

QString VBoxUpdateData::dateToString() const
{
    return   isCheckEnabled()
           ? QLocale::system().toString(m_date, QLocale::ShortFormat)
           : QCoreApplication::translate("UIUpdateManager", "Never");
}

KUpdateChannel VBoxUpdateData::updateChannel() const
{
    return m_enmUpdateChannel;
}

QString VBoxUpdateData::updateChannelName() const
{
    return updateChannelToInternalString(m_enmUpdateChannel);
}

UIVersion VBoxUpdateData::version() const
{
    return m_version;
}

QVector<KUpdateChannel> VBoxUpdateData::supportedUpdateChannels() const
{
    return m_supportedUpdateChannels;
}

bool VBoxUpdateData::isEqual(const VBoxUpdateData &another) const
{
    return    true
           && (m_fCheckEnabled == another.isCheckEnabled())
           && (m_enmUpdatePeriod == another.updatePeriod())
           && (m_enmUpdateChannel == another.updateChannel())
              ;
}

bool VBoxUpdateData::operator==(const VBoxUpdateData &another) const
{
    return isEqual(another);
}

bool VBoxUpdateData::operator!=(const VBoxUpdateData &another) const
{
    return !isEqual(another);
}

/* static */
QString VBoxUpdateData::updateChannelToInternalString(KUpdateChannel enmUpdateChannel)
{
    switch (enmUpdateChannel)
    {
        case KUpdateChannel_WithTesting: return "withtesting";
        case KUpdateChannel_WithBetas: return "withbetas";
        case KUpdateChannel_All: return "allrelease";
        default: return "stable";
    }
}

/* static */
KUpdateChannel VBoxUpdateData::updateChannelFromInternalString(const QString &strUpdateChannel)
{
    QMap<QString, KUpdateChannel> pairs;
    pairs["withtesting"] = KUpdateChannel_WithTesting;
    pairs["withbetas"] = KUpdateChannel_WithBetas;
    pairs["allrelease"] = KUpdateChannel_All;
    return pairs.value(strUpdateChannel, KUpdateChannel_Stable);
}

/* static */
UpdatePeriodType VBoxUpdateData::gatherSuitablePeriod(ULONG uFrequency)
{
    if (s_days.isEmpty())
        populate();

    UpdatePeriodType enmType = UpdatePeriodType_1Day;
    foreach (const VBoxUpdateDay &day, s_days)
    {
        if (uFrequency <= day.length)
            return enmType;
        enmType = (UpdatePeriodType)(enmType + 1);
    }

    return UpdatePeriodType_1Month;
}
