/* $Id: UIUpdateDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Update routine related declarations.
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

#ifndef FEQT_INCLUDED_SRC_networking_UIUpdateDefs_h
#define FEQT_INCLUDED_SRC_networking_UIUpdateDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDate>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIVersion.h"

/* COM includes: */
#include "CHost.h"
#include "KUpdateChannel.h"

/** Update period types. */
enum UpdatePeriodType
{
    UpdatePeriodType_Never  = -1,
    UpdatePeriodType_1Day   =  0,
    UpdatePeriodType_2Days  =  1,
    UpdatePeriodType_3Days  =  2,
    UpdatePeriodType_4Days  =  3,
    UpdatePeriodType_5Days  =  4,
    UpdatePeriodType_6Days  =  5,
    UpdatePeriodType_1Week  =  6,
    UpdatePeriodType_2Weeks =  7,
    UpdatePeriodType_3Weeks =  8,
    UpdatePeriodType_1Month =  9
};


/** Structure to store retranslated period type values. */
struct VBoxUpdateDay
{
    VBoxUpdateDay(const QString &strVal, const QString &strKey, ULONG uLength)
        : val(strVal)
        , key(strKey)
        , length(uLength)
    {}

    bool operator==(const VBoxUpdateDay &other) const
    {
        return    val == other.val
               || key == other.key
               || length == other.length;
    }

    QString  val;
    QString  key;
    ULONG    length;
};
typedef QList<VBoxUpdateDay> VBoxUpdateDayList;


/** Class used to encode/decode update data. */
class SHARED_LIBRARY_STUFF VBoxUpdateData
{
public:

    /** Populates a set of update options. */
    static void populate();
    /** Returns a list of update options. */
    static QStringList list();

    /** Constructs update description on the basis of passed @a strData. */
    VBoxUpdateData(const QString &strData = QString("never"));
    /** Constructs update description on the basis of passed @a fCheckEnabled, @a enmUpdatePeriod and @a enmUpdateChannel. */
    VBoxUpdateData(bool fCheckEnabled, UpdatePeriodType enmUpdatePeriod, KUpdateChannel enmUpdateChannel);

    /** Loads data from IHost. */
    bool load(const CHost &comHost);
    /** Saves data to IHost. */
    bool save(const CHost &comHost) const;

    /** Returns whether check is enabled. */
    bool isCheckEnabled() const;
    /** Returns whether check is required. */
    bool isCheckRequired() const;

    /** Returns update data. */
    QString data() const;

    /** Returns update period. */
    UpdatePeriodType updatePeriod() const;
    /** Returns update date. */
    QDate date() const;
    /** Returns update date as string. */
    QString dateToString() const;
    /** Returns update channel. */
    KUpdateChannel updateChannel() const;
    /** Returns update channel name. */
    QString updateChannelName() const;
    /** Returns version. */
    UIVersion version() const;

    /** Returns supported update chennels. */
    QVector<KUpdateChannel> supportedUpdateChannels() const;

    /** Returns whether this item equals to @a another one. */
    bool isEqual(const VBoxUpdateData &another) const;
    /** Returns whether this item equals to @a another one. */
    bool operator==(const VBoxUpdateData &another) const;
    /** Returns whether this item isn't equal to @a another one. */
    bool operator!=(const VBoxUpdateData &another) const;

    /** Converts passed @a enmUpdateChannel to internal QString value.
      * @note This isn't a member of UIConverter since it's used for
      *       legacy extra-data settings saving routine only. */
    static QString updateChannelToInternalString(KUpdateChannel enmUpdateChannel);
    /** Converts passed @a strUpdateChannel to KUpdateChannel value.
      * @note This isn't a member of UIConverter since it's used for
      *       legacy extra-data settings saving routine only. */
    static KUpdateChannel updateChannelFromInternalString(const QString &strUpdateChannel);

private:

    /** Gathers period suitable to passed @a uFrequency rounding up. */
    static UpdatePeriodType gatherSuitablePeriod(ULONG uFrequency);

    /** Holds the populated list of update period options. */
    static VBoxUpdateDayList s_days;

    /** Holds the update data. */
    QString  m_strData;

    /** Holds whether check is enabled. */
    bool  m_fCheckEnabled;
    /** Holds whether it's need to check for update. */
    bool  m_fCheckRequired;

    /** Holds the update period. */
    UpdatePeriodType  m_enmUpdatePeriod;
    /** Holds the update date. */
    QDate             m_date;
    /** Holds the update channel. */
    KUpdateChannel    m_enmUpdateChannel;
    /** Holds the update version. */
    UIVersion         m_version;

    /** Holds the supported update chennels. */
    QVector<KUpdateChannel>  m_supportedUpdateChannels;
};


#endif /* !FEQT_INCLUDED_SRC_networking_UIUpdateDefs_h */
