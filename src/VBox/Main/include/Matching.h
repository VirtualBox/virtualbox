/* $Id: Matching.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Declaration of template classes that provide simple API to
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

#ifndef MAIN_INCLUDED_Matching_h
#define MAIN_INCLUDED_Matching_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/string.h>

#include <list>
#include <limits>
#include <algorithm>

// min and max don't allow us to use numeric_limits::min() and max()
#if defined (_MSC_VER)
#undef min
#undef max
#endif

namespace matching
{

using namespace std;
using namespace com;

class ParsedFilter_base
{
public:

    ParsedFilter_base() : mValid (false), mNull (true), mErrorPosition (0) {};

    /**
     * Returns @c true if the filter is valid, @c false otherwise.
     */
    bool isValid() const { return mNull || mValid; }
    bool isNull() const { return mNull; }

    /**
     *  Returns the error position from the beginning of the filter
     *  string if #isValid() is false. Positions are zero-based.
     */
    size_t errorPosition() const { return mErrorPosition; }

protected:

    /**
     *  Returns @c true if current isNull() and isValid() values make further
     *  detailed matching meaningful, otherwise returns @c false.
     *  Must be called as a first method of every isMatch() implementation,
     *  so that isMatch() will immediately return @c false if isPreMatch() returns
     *  false.
     */
    bool isPreMatch() const
    {
        if (isNull() || !isValid())
            return false;
        return true;
    }

    bool mValid : 1;
    bool mNull : 1;
    size_t mErrorPosition;
};

class ParsedIntervalFilter_base : public ParsedFilter_base
{
public:
    virtual ~ParsedIntervalFilter_base() { /* Make VC++ 14.2 happy */ }

protected:

    enum Mode { Single, Start, End };

    union Widest
    {
        int64_t ll;
        uint64_t ull;
    };

    struct Limits
    {
        Widest min;
        Widest max;
    };

    ParsedIntervalFilter_base() {}

    /**
     *  Called by #parse when a value token is encountered.
     *  This method can modify mNull, mValid and mErrorPosition when
     *  appropriate. Parsing stops if mValid is false after this method
     *  returns (mErrorPosition most point to the error position in this case).
     */
    virtual void parseValue (const char *aFilter, size_t aStart, size_t aEnd,
                             Mode aMode) = 0;

    static void parse (const char *aFilter,
                       ParsedIntervalFilter_base *that);

    static size_t parseValue (const char *aFilter, size_t aStart, size_t aEnd,
                              bool aIsSigned, const Limits &aLimits,
                              Widest &val);
};

/**
 *  Represents a parsed interval filter.
 *  The string format is:
 *      "int:(\<m\>|([\<m\>]-[\<n\>]))|(\<m\>|([\<m\>]-[\<n\>]))+"
 *  where \<m\> and \<n\> are numbers in the decimal, hex (0xNNN) or octal
 *  (0NNN) form, and \<m\> \< \<n\>. Spaces are allowed around \<m\> and \<n\>.
 *
 *  @tparam T    type of values to match. Must be a fundamental integer type.
 */
template <class T>
class ParsedIntervalFilter : public ParsedIntervalFilter_base
{
    typedef ParsedIntervalFilter_base Base;
    typedef numeric_limits <T> Lim;

    typedef std::list <T> List;
    typedef std::pair <T, T> Pair;
    typedef std::list <Pair> PairList;

public:

    ParsedIntervalFilter() {}

    ParsedIntervalFilter (const Bstr &aFilter) { Base::parse (Utf8Str (aFilter), this); }

    ParsedIntervalFilter &operator= (const Bstr &aFilter)
    {
        mValues.clear();
        mIntervals.clear();
        Base::parse (Utf8Str (aFilter), this);
        return *this;
    }

    bool isMatch (const T &aValue) const
    {
        if (!isPreMatch())
            return false;

        {
            typename List::const_iterator it =
                std::find (mValues.begin(), mValues.end(), aValue);
            if (it != mValues.end())
                return true;
        }

        for (typename PairList::const_iterator it = mIntervals.begin();
             it != mIntervals.end(); ++ it)
        {
            if ((*it).first <= aValue &&
                aValue <= (*it).second)
                return true;
        }

        return false;
    }

protected:

    struct Limits : public Base::Limits
    {
        Limits()
        {
            if (Lim::is_signed)
            {
                min.ll = (int64_t) Lim::min();
                max.ll = (int64_t) Lim::max();
            }
            else
            {
                min.ull = (uint64_t) Lim::min();
                max.ull = (uint64_t) Lim::max();
            }
        }

        static T toValue (const Widest &aWidest)
        {
            if (Lim::is_signed)
                return (T) aWidest.ll;
            else
                return (T) aWidest.ull;
        }
    };

    virtual void parseValue (const char *aFilter, size_t aStart, size_t aEnd,
                             Mode aMode)
    {
        AssertReturn (Lim::is_integer, (void) 0);
        AssertReturn (
            (Lim::is_signed && Lim::digits <= numeric_limits <int64_t>::digits) ||
            (!Lim::is_signed && Lim::digits <= numeric_limits <uint64_t>::digits),
            (void) 0);

        Limits limits;
        Widest val;
        size_t parsed = aEnd;

        if (aStart != aEnd)
            parsed = Base::parseValue (aFilter, aStart, aEnd,
                                       Lim::is_signed, limits, val);

        if (parsed != aEnd)
        {
            mValid = false;
            mErrorPosition = parsed;
            return;
        }

        switch (aMode)
        {
            /// @todo (dmik): future optimizations:
            //  1) join intervals when they overlap
            //  2) ignore single values that are within any existing interval
            case Base::Single:
            {
                if (aStart == aEnd)
                {
                    // an empty string (contains only spaces after "int:")
                    mValid = false;
                    mErrorPosition = aEnd;
                    AssertReturn (!mValues.size() && !mIntervals.size(), (void) 0);
                    break;
                }
                mValues.push_back (limits.toValue (val));
                break;
            }
            case Base::Start:
            {
                // aStart == aEnd means smth. like "-[NNN]"
                T m = aStart == aEnd ? limits.toValue (limits.min)
                                     : limits.toValue (val);
                mIntervals.push_back (Pair (m, m));
                break;
            }
            case Base::End:
            {
                // aStart == aEnd means smth. like "[NNN]-"
                T n = aStart == aEnd ? limits.toValue (limits.max)
                                     : limits.toValue (val);
                if (n < mIntervals.back().first)
                {
                    // error at the beginning of N
                    mValid = false;
                    mErrorPosition = aStart;
                    break;
                }
                mIntervals.back().second = n;
                break;
            }
        }
    }

    std::list <T> mValues;
    std::list <std::pair <T, T> > mIntervals;
};

/**
 *  Represents a boolean filter.
 *  The string format is: "true|false|yes|no|1|0" or an empty string (any match).
 */

class ParsedBoolFilter : public ParsedFilter_base
{
public:

    ParsedBoolFilter() : mValue (false), mValueAny (false) {}

    ParsedBoolFilter (const Bstr &aFilter) { parse (aFilter); }

    ParsedBoolFilter &operator= (const Bstr &aFilter)
    {
        parse (aFilter);
        return *this;
    }

    bool isMatch (const bool aValue) const
    {
        if (!isPreMatch())
            return false;

        return mValueAny || mValue == aValue;
    }

    bool isMatch (const BOOL aValue) const
    {
        return isMatch (bool (aValue == TRUE));
    }

private:

    void parse (const Bstr &aFilter);

    bool mValue : 1;
    bool mValueAny : 1;
};

class ParsedRegexpFilter_base : public ParsedFilter_base
{
protected:

    ParsedRegexpFilter_base (bool aDefIgnoreCase = false,
                             size_t aMinLen = 0, size_t aMaxLen = 0)
        : mIgnoreCase (aDefIgnoreCase)
        , mMinLen (aMinLen)
        , mMaxLen (aMaxLen)
        {}

    ParsedRegexpFilter_base (const Bstr &aFilter, bool aDefIgnoreCase = false,
                             size_t aMinLen = 0, size_t aMaxLen = 0)
        : mIgnoreCase (aDefIgnoreCase)
        , mMinLen (aMinLen)
        , mMaxLen (aMaxLen)
    {
        parse (aFilter);
    }

    ParsedRegexpFilter_base &operator= (const Bstr &aFilter)
    {
        parse (aFilter);
        return *this;
    }

    bool isMatch (const Bstr &aValue) const;

private:

    void parse (const Bstr &aFilter);

    bool mIgnoreCase : 1;

    size_t mMinLen;
    size_t mMaxLen;

    Bstr mSimple;
};

/**
 *  Represents a parsed regexp filter.
 *
 *  The string format is: "rx:\<regexp\>" or "\<string\>"
 *  where \<regexp\> is a valid regexp and \<string\> is the exact match.
 *
 *  @tparam Conv
 *      class that must define a public static function
 *      <tt>Bstr toBstr (T aValue)</tt>, where T is the
 *      type of values that should be accepted by #isMatch().
 *      This function is used to get the string representation of T
 *      for regexp matching.
 *  @tparam aIgnoreCase
 *      true if the case insensitive comparison should be done by default
 *      and false otherwise
 *  @tparam aMinLen
 *      minimum string length, or 0 if not limited.
 *      Used only when the filter string represents the exact match.
 *  @tparam aMaxLen
 *      maximum string length, or 0 if not limited.
 *      Used only when the filter string represents the exact match.
 */
template <class Conv, bool aIgnoreCase, size_t aMinLen = 0, size_t aMaxLen = 0>
class ParsedRegexpFilter : public ParsedRegexpFilter_base
{
public:

    enum { IgnoreCase = aIgnoreCase, MinLen = aMinLen, MaxLen = aMaxLen };

    ParsedRegexpFilter() : ParsedRegexpFilter_base (IgnoreCase, MinLen, MaxLen) {}

    ParsedRegexpFilter (const Bstr &aFilter)
        : ParsedRegexpFilter_base (aFilter, IgnoreCase, MinLen, MaxLen) {}

    ParsedRegexpFilter &operator= (const Bstr &aFilter)
    {
        ParsedRegexpFilter_base::operator= (aFilter);
        return *this;
    }

    template <class T>
    bool isMatch (const T &aValue) const
    {
        if (!this->isPreMatch())
            return false;

        return ParsedRegexpFilter_base::isMatch (Conv::toBstr (aValue));
    }

protected:
};

/**
 *  Joins two filters into one.
 *  Only one filter is active (i.e. used for matching or for error reporting)
 *  at any given time. The active filter is chosen every time when a new
 *  filter string is assigned to an instance of this class -- the filter
 *  for which isNull() = false after parsing the string becomes the active
 *  one (F1 is tried first).
 *
 *  Both filters must have <tt>bool isMatch(const T&)</tt> methods where T is
 *  the same type as used in #isMatch().
 *
 *  @tparam F1  first filter class
 *  @tparam F2  second filter class
 */
template <class F1, class F2>
class TwoParsedFilters
{
public:

    TwoParsedFilters() {}

    TwoParsedFilters (const Bstr &aFilter)
    {
        mFilter1 = aFilter;
        if (mFilter1.isNull())
            mFilter2 = aFilter;
    }

    TwoParsedFilters &operator= (const Bstr &aFilter)
    {
        mFilter1 = aFilter;
        if (mFilter1.isNull())
            mFilter2 = aFilter;
        else
            mFilter2 = F2(); // reset to null
        return *this;
    }

    template <class T>
    bool isMatch (const T &aValue) const
    {
        return mFilter1.isMatch (aValue) || mFilter2.isMatch (aValue);
    }

    bool isValid() const { return isNull() || (mFilter1.isValid() && mFilter2.isValid()); }

    bool isNull() const { return mFilter1.isNull() && mFilter2.isNull(); }

    size_t errorPosition() const
    {
        return !mFilter1.isValid() ? mFilter1.errorPosition() :
               !mFilter2.isValid() ? mFilter2.errorPosition() : 0;
    }

    const F1 &first() const { return mFilter1; }
    const F2 &second() const { return mFilter2; }

private:

    F1 mFilter1;
    F2 mFilter2;
};

/**
 *  Inherits from the given parsed filter class and keeps the string used to
 *  construct the filter as a member.
 *
 *  @tparam F   parsed filter class
 */
template <class F>
class Matchable : public F
{
public:

    Matchable() {}

    /**
     *  Creates a new parsed filter from the given filter string.
     *  If the string format is invalid, #isValid() will return false.
     */
    Matchable (const Bstr &aString)
        : F (aString), mString (aString) {}

    Matchable (CBSTR aString)
        : F (Bstr (aString)), mString (aString) {}

    /**
     *  Assigns a new filter string to this object and recreates the parser.
     *  If the string format is invalid, #isValid() will return false.
     */
    Matchable &operator= (const Bstr &aString)
    {
        F::operator= (aString);
        mString = aString;
        return *this;
    }

    Matchable &operator= (CBSTR aString)
    {
        F::operator= (Bstr (aString));
        mString = aString;
        return *this;
    }

    /**
     *  Returns the filter string allowing to use the instance where
     *  Str can be used.
     */
    operator const Bstr&() const { return mString; }

    /** Returns the filter string */
    const Bstr& string() const { return mString; }

private:

    Bstr mString;
};

} /* namespace matching */

#endif /* !MAIN_INCLUDED_Matching_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
