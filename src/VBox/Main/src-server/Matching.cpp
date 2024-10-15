/* $Id: Matching.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * @todo r=bird: brief description, please.
 *
 * Definition of template classes that provide simple API to
 * do matching between values and value filters constructed from strings.
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

#define LOG_GROUP LOG_GROUP_MAIN
#include "Matching.h"

#include "LoggingNew.h"

#include <stdlib.h>

#include <iprt/errcore.h>

namespace matching
{

// static
void ParsedIntervalFilter_base::parse (const char *aFilter,
                                       ParsedIntervalFilter_base *that)
{
    // initially null and valid
    that->mNull = true;
    that->mValid = true;
    that->mErrorPosition = 0;

    if (!aFilter || strncmp(aFilter, RT_STR_TUPLE("int:")) != 0)
        return;

    that->mNull = false;

    size_t len = strlen (aFilter);

    Mode mode = Single; // what's expected next
    size_t start = 4, end = 4;
    size_t err = 0; // less than 4 indicates success

    do
    {
        end = strcspn(aFilter + start, ",-");
        end += start;

        char delim = aFilter[end];

        if (delim == '-')
        {
            if (mode == End)
            {
                err = end;
                break;
            }
            else
                mode = Start;
        }

        // skip spaces around numbers
        size_t s = start;
        while (s < end && aFilter[s] == ' ') ++s;
        size_t e = end - 1;
        while (e > s && aFilter[e] == ' ') --e;
        ++e;

        that->parseValue(aFilter, s, e, mode);
        if (!that->mValid)
            return;

        if (mode == Start)
            mode = End;
        else if (mode == End)
            mode = Single;

        start = end + 1;
    }
    while (start <= len);

    if (err >= 4)
    {
        that->mValid = false;
        that->mErrorPosition = err;
    }
}

// static
size_t ParsedIntervalFilter_base::parseValue (
    const char *aFilter, size_t aStart, size_t aEnd,
    bool aIsSigned, const Limits &aLimits,
    Widest &val)
{
    char *endptr = NULL;

    int vrc = 0;
    if (aIsSigned)
        vrc = RTStrToInt64Ex(aFilter + aStart, &endptr, 0, &val.ll);
    else
        vrc = RTStrToUInt64Ex(aFilter + aStart, &endptr, 0, &val.ull);

    AssertReturn(endptr, 0);

    size_t parsed = (size_t)(endptr - aFilter);

    // return parsed if not able to parse to the end
    if (parsed != aEnd)
        return parsed;

    // return aStart if out if range
    if (vrc == VWRN_NUMBER_TOO_BIG ||
        (aIsSigned &&
         (val.ll < aLimits.min.ll ||
          val.ll > aLimits.max.ll)) ||
        (!aIsSigned &&
         (val.ull < aLimits.min.ull ||
          val.ull > aLimits.max.ull)))
        return aStart;

    return parsed;
}

void ParsedBoolFilter::parse (const Bstr &aFilter)
{
    mNull = false;
    mValid = true;
    mErrorPosition = 0;

    if (aFilter.isEmpty())
    {
        mValueAny = true;
        mValue = false;
    }
    else
    {
        mValueAny = false;
        if (aFilter == L"true" || aFilter == L"yes" || aFilter == L"1")
            mValue = true;
        else
        if (aFilter == L"false" || aFilter == L"no" || aFilter == L"0")
            mValue = false;
        else
            mValid = false;
    }
}

void ParsedRegexpFilter_base::parse (const Bstr &aFilter)
{
    /// @todo (dmik) parse "rx:<regexp>" string
    //  note, that min/max checks must not be done, when the string
    //  begins with "rx:". These limits are for exact matching only!

    // empty or null string means any match (see #isMatch() below),
    // so we don't apply Min/Max restrictions in this case

    if (!aFilter.isEmpty())
    {
        size_t len = aFilter.length();

        if (mMinLen > 0 && len < mMinLen)
        {
            mNull = mValid = false;
            mErrorPosition = len;
            return;
        }

        if (mMaxLen > 0 && len > mMaxLen)
        {
            mNull = mValid = false;
            mErrorPosition = mMaxLen;
            return;
        }
    }

    mSimple = aFilter;
    mNull = false;
    mValid = true;
    mErrorPosition = 0;
}

bool ParsedRegexpFilter_base::isMatch (const Bstr &aValue) const
{
    /// @todo (dmik) do regexp matching

    // empty or null mSimple matches any match
    return     mSimple.isEmpty()
            || (mIgnoreCase && mSimple.compare(aValue, Bstr::CaseInsensitive) == 0)
            || (!mIgnoreCase && mSimple.compare(aValue) == 0);
}

} /* namespace matching */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
