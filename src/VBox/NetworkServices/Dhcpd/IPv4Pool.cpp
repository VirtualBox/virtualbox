/* $Id: IPv4Pool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - A pool of IPv4 addresses.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "DhcpdInternal.h"
#include <iprt/errcore.h>

#include "IPv4Pool.h"


int IPv4Pool::init(const IPv4Range &aRange) RT_NOEXCEPT
{
    AssertReturn(aRange.isValid(), VERR_INVALID_PARAMETER);

    m_range = aRange;
    try
    {
        m_pool.insert(m_range);
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


int IPv4Pool::init(RTNETADDRIPV4 aFirstAddr, RTNETADDRIPV4 aLastAddr) RT_NOEXCEPT
{
    return init(IPv4Range(aFirstAddr, aLastAddr));
}


/**
 * Internal worker for inserting a range into the pool of available addresses.
 *
 * @returns IPRT status code (asserted).
 * @param   a_Range         The range to insert.
 */
int IPv4Pool::i_insert(const IPv4Range &a_Range) RT_NOEXCEPT
{
    /*
     * Check preconditions. Asserting because nobody checks the return code.
     */
    AssertReturn(m_range.isValid(), VERR_INVALID_STATE);
    AssertReturn(a_Range.isValid(), VERR_INVALID_PARAMETER);
    AssertReturn(m_range.contains(a_Range), VERR_INVALID_PARAMETER);

    /*
     * Check that the incoming range doesn't overlap with existing ranges in the pool.
     */
    it_t itHint = m_pool.upper_bound(IPv4Range(a_Range.LastAddr)); /* successor, insertion hint */
#if 0 /** @todo r=bird: This code is wrong.  It has no end() check for starters.  Since the method is
       *                only for internal consumption, I've replaced it with a strict build assertion. */
    if (itHint != m_pool.begin())
    {
        it_t prev(itHint);
        --prev;
        if (a_Range.FirstAddr <= prev->LastAddr)
        {
            LogRel(("%08x-%08x conflicts with %08x-%08x\n",
                     a_Range.FirstAddr, a_Range.LastAddr,
                     prev->FirstAddr, prev->LastAddr));
            return VERR_INVALID_PARAMETER;
        }
    }
#endif
#ifdef VBOX_STRICT
    for (it_t it2 = m_pool.begin(); it2 != m_pool.end(); ++it2)
        AssertMsg(it2->LastAddr < a_Range.FirstAddr || it2->FirstAddr > a_Range.LastAddr,
                  ("%08RX32-%08RX32 conflicts with %08RX32-%08RX32\n",
                   a_Range.FirstAddr, a_Range.LastAddr, it2->FirstAddr, it2->LastAddr));
#endif

    /*
     * No overlaps, insert it.
     */
    try
    {
        m_pool.insert(itHint, a_Range);
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


/**
 * Allocates an available IPv4 address from the pool.
 *
 * @returns Non-zero network order IPv4 address on success, zero address
 *          (0.0.0.0) on failure.
 */
RTNETADDRIPV4 IPv4Pool::allocate()
{
    RTNETADDRIPV4 RetAddr;
    if (!m_pool.empty())
    {
        /* Grab the first address in the pool: */
        it_t itBeg = m_pool.begin();
        RetAddr.u = RT_H2N_U32(itBeg->FirstAddr);

        if (itBeg->FirstAddr == itBeg->LastAddr)
            m_pool.erase(itBeg);
        else
        {
            /* Trim the entry (re-inserting it): */
            IPv4Range trimmed = *itBeg;
            trimmed.FirstAddr += 1;
            Assert(trimmed.FirstAddr <= trimmed.LastAddr);
            m_pool.erase(itBeg);
            try
            {
                m_pool.insert(trimmed);
            }
            catch (std::bad_alloc &)
            {
                /** @todo r=bird: Theortically the insert could fail with a bad_alloc and we'd
                 * drop a range of IP address.  It would be nice if we could safely modify itBit
                 * without having to re-insert it.  The author of this code (not bird) didn't
                 * seem to think this safe?
                 *
                 * If we want to play safe and all that, just use a AVLRU32TREE (or AVLRU64TREE
                 * if lazy) AVL tree from IPRT.  Since we know exactly how it's implemented and
                 * works, there will be no uncertanties like this when using it (both here
                 * and in the i_insert validation logic). */
                LogRelFunc(("Caught bad_alloc! We're truely buggered now!\n"));
            }
        }
    }
    else
        RetAddr.u = 0;
    return RetAddr;
}


/**
 * Allocate the given address.
 *
 * @returns Success indicator.
 * @param   a_Addr      The IP address to allocate (network order).
 */
bool IPv4Pool::allocate(RTNETADDRIPV4 a_Addr)
{
    /*
     * Find the range containing a_Addr.
     */
    it_t it = m_pool.lower_bound(IPv4Range(a_Addr)); /* candidate range */
    if (it != m_pool.end())
    {
        Assert(RT_N2H_U32(a_Addr.u) <= it->LastAddr); /* by definition of < and lower_bound */

        if (it->contains(a_Addr))
        {
            /*
             * Remove a_Addr from the range by way of re-insertion.
             */
            const IPV4HADDR haddr = RT_N2H_U32(a_Addr.u);
            IPV4HADDR       first = it->FirstAddr;
            IPV4HADDR       last  = it->LastAddr;

            m_pool.erase(it);
            if (first != last)
            {
                if (haddr == first)
                    i_insert(++first, last);
                else if (haddr == last)
                    i_insert(first, --last);
                else
                {
                    i_insert(first, haddr - 1);
                    i_insert(haddr + 1, last);
                }
            }

            return true;
        }
    }
    return false;
}
