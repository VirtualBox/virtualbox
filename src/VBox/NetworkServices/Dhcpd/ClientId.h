/* $Id: ClientId.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - client identifier
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_ClientId_h
#define VBOX_INCLUDED_SRC_Dhcpd_ClientId_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DhcpdInternal.h"
#include <iprt/net.h>
#include "DhcpOptions.h"

/**
 * A client is identified by either the Client ID option it sends or its chaddr,
 * i.e. MAC address.
 */
class ClientId
{
    /** The mac address of the client. */
    RTMAC       m_mac;
    /** The client ID. */
    OptClientId m_id;

public:
    ClientId()
        : m_mac(), m_id()
    {}
    /** @throws std::bad_alloc */
    ClientId(const RTMAC &a_mac, const OptClientId &a_id)
        : m_mac(a_mac), m_id(a_id)
    {}
    /** @throws std::bad_alloc */
    ClientId(const ClientId &a_rThat)
        : m_mac(a_rThat.m_mac), m_id(a_rThat.m_id)
    {}
    /** @throws std::bad_alloc */
    ClientId &operator=(const ClientId &a_rThat)
    {
        m_mac = a_rThat.m_mac;
        m_id  = a_rThat.m_id;
        return *this;
    }

    const RTMAC       &mac() const RT_NOEXCEPT  { return m_mac; }
    const OptClientId &id() const RT_NOEXCEPT   { return m_id; }

    /** @name String formatting of %R[id].
     * @{ */
    static void registerFormat() RT_NOEXCEPT;
private:
    static DECLCALLBACK(size_t) rtStrFormat(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, const char *pszType,
                                            void const *pvValue, int cchWidth, int cchPrecision, unsigned fFlags, void *pvUser);
    static bool g_fFormatRegistered;
    /** @} */

    friend bool operator==(const ClientId &l, const ClientId &r) RT_NOEXCEPT;
    friend bool operator<(const ClientId &l, const ClientId &r) RT_NOEXCEPT;
};

bool operator==(const ClientId &l, const ClientId &r) RT_NOEXCEPT;
bool operator<(const ClientId &l, const ClientId &r) RT_NOEXCEPT;

inline bool operator!=(const ClientId &l, const ClientId &r) RT_NOEXCEPT
{
    return !(l == r);
}

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_ClientId_h */
