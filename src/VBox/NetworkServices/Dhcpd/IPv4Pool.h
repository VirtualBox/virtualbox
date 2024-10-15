/* $Id: IPv4Pool.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - a pool of IPv4 addresses
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_IPv4Pool_h
#define VBOX_INCLUDED_SRC_Dhcpd_IPv4Pool_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/asm.h>
#include <iprt/stdint.h>
#include <iprt/net.h>
#include <set>


/** Host order IPv4 address. */
typedef uint32_t IPV4HADDR;


/**
 * A range of IPv4 addresses (in host order).
 */
struct IPv4Range
{
    IPV4HADDR FirstAddr;       /**< Lowest address. */
    IPV4HADDR LastAddr;        /**< Higest address (inclusive). */

    IPv4Range() RT_NOEXCEPT
        : FirstAddr(0), LastAddr(0)
    {}

    explicit IPv4Range(IPV4HADDR aSingleAddr) RT_NOEXCEPT
        : FirstAddr(aSingleAddr), LastAddr(aSingleAddr)
    {}

    IPv4Range(IPV4HADDR aFirstAddr, IPV4HADDR aLastAddr) RT_NOEXCEPT
        : FirstAddr(aFirstAddr), LastAddr(aLastAddr)
    {}

    explicit IPv4Range(RTNETADDRIPV4 aSingleAddr) RT_NOEXCEPT
        : FirstAddr(RT_N2H_U32(aSingleAddr.u)), LastAddr(RT_N2H_U32(aSingleAddr.u))
    {}

    IPv4Range(RTNETADDRIPV4 aFirstAddr, RTNETADDRIPV4 aLastAddr) RT_NOEXCEPT
        : FirstAddr(RT_N2H_U32(aFirstAddr.u)), LastAddr(RT_N2H_U32(aLastAddr.u))
    {}

    bool isValid() const RT_NOEXCEPT
    {
        return FirstAddr <= LastAddr;
    }

    bool contains(IPV4HADDR addr) const RT_NOEXCEPT
    {
        return FirstAddr <= addr && addr <= LastAddr;
    }

    bool contains(RTNETADDRIPV4 addr) const RT_NOEXCEPT
    {
        return contains(RT_N2H_U32(addr.u));
    }

    /** Checks if this range includes the @a a_rRange. */
    bool contains(const IPv4Range &a_rRange) const RT_NOEXCEPT
    {
        return a_rRange.isValid()
            && FirstAddr <= a_rRange.FirstAddr
            && a_rRange.LastAddr <= LastAddr;
    }
};


inline bool operator==(const IPv4Range &l, const IPv4Range &r) RT_NOEXCEPT
{
    return l.FirstAddr == r.FirstAddr && l.LastAddr == r.LastAddr;
}


inline bool operator<(const IPv4Range &l, const IPv4Range &r)  RT_NOEXCEPT
{
    return l.LastAddr < r.FirstAddr;
}


/**
 * IPv4 address pool.
 *
 * This manages a single range of IPv4 addresses (m_range).   Unallocated
 * addresses are tracked as a set of sub-ranges in the m_pool set.
 *
 */
class IPv4Pool
{
    typedef std::set<IPv4Range> set_t;
    typedef set_t::iterator it_t;

    /** The IPv4 range of this pool. */
    IPv4Range   m_range;
    /** Pool of available IPv4 ranges. */
    set_t       m_pool;

public:
    IPv4Pool()
    {}

    int init(const IPv4Range &aRange) RT_NOEXCEPT;
    int init(RTNETADDRIPV4 aFirstAddr, RTNETADDRIPV4 aLastAddr) RT_NOEXCEPT;

    RTNETADDRIPV4 allocate();
    bool          allocate(RTNETADDRIPV4);

    /**
     * Checks if the pool range includes @a a_Addr (allocation status not considered).
     */
    bool contains(RTNETADDRIPV4 a_Addr) const RT_NOEXCEPT
    {
        return m_range.contains(a_Addr);
    }

private:
    int i_insert(const IPv4Range &range) RT_NOEXCEPT;
#if 0
    int i_insert(IPV4HADDR a_Single) RT_NOEXCEPT                          { return i_insert(IPv4Range(a_Single)); }
#endif
    int i_insert(IPV4HADDR a_First, IPV4HADDR a_Last) RT_NOEXCEPT         { return i_insert(IPv4Range(a_First, a_Last)); }
    int i_insert(RTNETADDRIPV4 a_Single) RT_NOEXCEPT                      { return i_insert(IPv4Range(a_Single)); }
    int i_insert(RTNETADDRIPV4 a_First, RTNETADDRIPV4 a_Last) RT_NOEXCEPT { return i_insert(IPv4Range(a_First, a_Last)); }
};

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_IPv4Pool_h */
