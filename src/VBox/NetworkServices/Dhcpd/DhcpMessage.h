/* $Id: DhcpMessage.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP Message and its de/serialization.
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_DhcpMessage_h
#define VBOX_INCLUDED_SRC_Dhcpd_DhcpMessage_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DhcpdInternal.h"
#include <iprt/net.h>
#include <iprt/cpp/ministring.h>
#include "ClientId.h"
#include "DhcpOptions.h"


/**
 * Base class for internal DHCP client and server message representations.
 */
class DhcpMessage
{
protected:
    uint32_t        m_xid;
    uint16_t        m_flags;

    RTMAC           m_mac;

    RTNETADDRIPV4   m_ciaddr;
    RTNETADDRIPV4   m_yiaddr;
    RTNETADDRIPV4   m_siaddr;
    RTNETADDRIPV4   m_giaddr;

#if 0 /* not currently unused, so avoid wasting time on them for now.  */
    RTCString       m_sname;  /**< @note Not necessarily UTF-8 clean. */
    RTCString       m_file;   /**< @note Not necessarily UTF-8 clean. */
#endif

    OptMessageType  m_optMessageType;

protected:
    DhcpMessage();

public:
    /** @name Accessors
     * @{ */
    uint32_t        xid() const RT_NOEXCEPT                     { return m_xid; }

    uint16_t        flags() const RT_NOEXCEPT                   { return m_flags; }
    bool            broadcast() const RT_NOEXCEPT               { return (m_flags & RTNET_DHCP_FLAG_BROADCAST) != 0; }

    const RTMAC     &mac() const RT_NOEXCEPT                    { return m_mac; }

    RTNETADDRIPV4   ciaddr() const RT_NOEXCEPT                  { return m_ciaddr; }
    RTNETADDRIPV4   yiaddr() const RT_NOEXCEPT                  { return m_yiaddr; }
    RTNETADDRIPV4   siaddr() const RT_NOEXCEPT                  { return m_siaddr; }
    RTNETADDRIPV4   giaddr() const RT_NOEXCEPT                  { return m_giaddr; }

    void            setCiaddr(RTNETADDRIPV4 addr) RT_NOEXCEPT   { m_ciaddr = addr; }
    void            setYiaddr(RTNETADDRIPV4 addr) RT_NOEXCEPT   { m_yiaddr = addr; }
    void            setSiaddr(RTNETADDRIPV4 addr) RT_NOEXCEPT   { m_siaddr = addr; }
    void            setGiaddr(RTNETADDRIPV4 addr) RT_NOEXCEPT   { m_giaddr = addr; }

    uint8_t         messageType() const RT_NOEXCEPT
    {
        Assert(m_optMessageType.present());
        return m_optMessageType.value();
    }
    /** @} */

    void            dump() const RT_NOEXCEPT;
};


/**
 * Decoded DHCP client message.
 *
 * This is the internal decoded representation of a DHCP message picked up from
 * the wire.
 */
class DhcpClientMessage
    : public DhcpMessage
{
protected:
    rawopts_t       m_rawopts;
    ClientId        m_id;
    bool            m_broadcasted;

public:
    static DhcpClientMessage *parse(bool broadcasted, const void *buf, size_t buflen);

    /** @name Getters
     * @{ */
    bool            broadcasted() const RT_NOEXCEPT             { return m_broadcasted; }
    const rawopts_t &rawopts() const RT_NOEXCEPT                { return m_rawopts; }
    const ClientId &clientId() const RT_NOEXCEPT                { return m_id; }
    /** @} */

    void            dump() const RT_NOEXCEPT;

protected:
    int             i_parseOptions(const uint8_t *pbBuf, size_t cbBuf) RT_NOEXCEPT;
};



/**
 * DHCP server message for encoding.
 */
class DhcpServerMessage
    : public DhcpMessage
{
protected:
    RTNETADDRIPV4   m_dst;
    OptServerId     m_optServerId;
    optmap_t        m_optmap;

public:
    DhcpServerMessage(const DhcpClientMessage &req, uint8_t messageType, RTNETADDRIPV4 serverAddr);

    /** @name Accessors
     * @{ */
    RTNETADDRIPV4   dst() const RT_NOEXCEPT                     { return m_dst; }
    void            setDst(RTNETADDRIPV4 aDst) RT_NOEXCEPT      { m_dst = aDst; }

    void            maybeUnicast(const DhcpClientMessage &req) RT_NOEXCEPT;

    void            addOption(DhcpOption *opt);
    void            addOption(const DhcpOption &opt)            { addOption(opt.clone()); }

    void            addOptions(const optmap_t &optmap);
    /** @} */

    int             encode(octets_t &data);
    void            dump() const RT_NOEXCEPT;
};

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_DhcpMessage_h */
