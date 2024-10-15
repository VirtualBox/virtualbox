/* $Id: Timestamp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - timestamps
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_Timestamp_h
#define VBOX_INCLUDED_SRC_Dhcpd_Timestamp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/time.h>


/**
 * Wrapper around RTTIMESPEC.
 *
 * @note Originally wanting to use RTTimeNanoTS rather than RTTimeNow.  The term
 *       "absolute" was used for when the RTTimeNanoTS() value was converted to
 *       something approximating unix epoch relative time with help of
 *       RTTimeNow().  Code was later changed to just wrap RTTIMESPEC and drop
 *       all usage of RTTimeNanoTS, ASSUMING that system time is stable.
 */
class Timestamp
{
    RTTIMESPEC m_TimeSpec;

public:
    Timestamp() RT_NOEXCEPT
    {
        RTTimeSpecSetNano(&m_TimeSpec, 0);
    }

    Timestamp(PCRTTIMESPEC a_pTimeSpec) RT_NOEXCEPT
    {
        m_TimeSpec = *a_pTimeSpec;
    }

    /** Get a timestamp initialized to current time. */
    static Timestamp now() RT_NOEXCEPT
    {
        RTTIMESPEC Tmp;
        return Timestamp(RTTimeNow(&Tmp));
    }

    /** Get a timestamp with the given value in seconds since unix epoch. */
    static Timestamp absSeconds(int64_t secTimestamp) RT_NOEXCEPT
    {
        RTTIMESPEC Tmp;
        return Timestamp(RTTimeSpecSetSeconds(&Tmp, secTimestamp));
    }

    Timestamp &addSeconds(int64_t cSecs) RT_NOEXCEPT
    {
        RTTimeSpecAddSeconds(&m_TimeSpec, cSecs);
        return *this;
    }

    Timestamp &subSeconds(int64_t cSecs) RT_NOEXCEPT
    {
        RTTimeSpecSubSeconds(&m_TimeSpec, cSecs);
        return *this;
    }

    RTTIMESPEC *getAbsTimeSpec(RTTIMESPEC *pTime) const RT_NOEXCEPT
    {
        *pTime = m_TimeSpec;
        return pTime;
    }

    int64_t getAbsSeconds() const RT_NOEXCEPT
    {
        return RTTimeSpecGetSeconds(&m_TimeSpec);
    }

    /** Only for log formatting. */
    size_t strFormatHelper(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput) const RT_NOEXCEPT;

    int compare(const Timestamp &a_rRight) const RT_NOEXCEPT
    {
        return RTTimeSpecCompare(&m_TimeSpec, &a_rRight.m_TimeSpec);
    }

    friend bool operator<( const Timestamp &, const Timestamp &) RT_NOEXCEPT;
    friend bool operator>( const Timestamp &, const Timestamp &) RT_NOEXCEPT;
    friend bool operator==(const Timestamp &, const Timestamp &) RT_NOEXCEPT;
    friend bool operator!=(const Timestamp &, const Timestamp &) RT_NOEXCEPT;
    friend bool operator<=(const Timestamp &, const Timestamp &) RT_NOEXCEPT;
    friend bool operator>=(const Timestamp &, const Timestamp &) RT_NOEXCEPT;
};


inline bool operator<( const Timestamp &l, const Timestamp &r) RT_NOEXCEPT { return l.compare(r) < 0; }
inline bool operator>( const Timestamp &l, const Timestamp &r) RT_NOEXCEPT { return l.compare(r) > 0; }
inline bool operator==(const Timestamp &l, const Timestamp &r) RT_NOEXCEPT { return l.compare(r) == 0; }
inline bool operator!=(const Timestamp &l, const Timestamp &r) RT_NOEXCEPT { return l.compare(r) != 0; }
inline bool operator<=(const Timestamp &l, const Timestamp &r) RT_NOEXCEPT { return l.compare(r) <= 0; }
inline bool operator>=(const Timestamp &l, const Timestamp &r) RT_NOEXCEPT { return l.compare(r) >= 0; }

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_Timestamp_h */
