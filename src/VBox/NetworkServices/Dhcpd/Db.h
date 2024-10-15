/* $Id: Db.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - address database
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_Db_h
#define VBOX_INCLUDED_SRC_Dhcpd_Db_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DhcpdInternal.h"
#include <iprt/net.h>

#include <iprt/cpp/ministring.h>
#include <iprt/cpp/xml.h>

#include <list>

#include "Timestamp.h"
#include "ClientId.h"
#include "IPv4Pool.h"
#include "Config.h"
#include "DhcpMessage.h"


/**
 * An address binding in the lease database.
 *
 * This is how an allocated IPv4 address is mananged.
 */
class Binding
{
    friend class Db;

public:
    enum State { FREE, RELEASED, EXPIRED, OFFERED, ACKED };

private:
    const RTNETADDRIPV4 m_addr;
    State               m_state;
    ClientId            m_id;
    Timestamp           m_issued;
    uint32_t            m_secLease;
    /** Set if this is a fixed assignment. */
    bool                m_fFixed;

public:
    Binding();
    Binding(const Binding &);

    explicit Binding(RTNETADDRIPV4 a_Addr)
        : m_addr(a_Addr), m_state(FREE), m_issued(), m_secLease(0), m_fFixed(false)
    {}

    Binding(RTNETADDRIPV4 a_Addr, const ClientId &a_id)
        : m_addr(a_Addr), m_state(FREE), m_id(a_id), m_issued(), m_secLease(0), m_fFixed(false)
    {}

    Binding(RTNETADDRIPV4 a_Addr, const RTMAC &a_MACAddress, bool a_fFixed)
        : m_addr(a_Addr)
        , m_state(ACKED)
        , m_id(ClientId(a_MACAddress, OptClientId()))
        , m_issued(Timestamp::now())
        , m_secLease(UINT32_MAX - 1)
        , m_fFixed(a_fFixed)
    {}


    /** @name Attribute accessors
     * @{ */
    RTNETADDRIPV4   addr() const RT_NOEXCEPT        { return m_addr; }

    const ClientId &id() const RT_NOEXCEPT          { return m_id; }
    void            idUpdate(const ClientId &a_ridClient);

    uint32_t        leaseTime() const RT_NOEXCEPT   { return m_secLease; }
    Timestamp       issued() const RT_NOEXCEPT      { return m_issued; }

    State           state() const RT_NOEXCEPT       { return m_state; }
    const char     *stateName() const RT_NOEXCEPT;
    Binding        &setState(const char *pszStateName) RT_NOEXCEPT;
    Binding        &setState(State stateParam) RT_NOEXCEPT
    {
        m_state = stateParam;
        return *this;
    }

    bool            isFixed() const RT_NOEXCEPT     { return m_fFixed; }
    /** @} */


    Binding &setLeaseTime(uint32_t secLease) RT_NOEXCEPT
    {
        m_issued = Timestamp::now();
        m_secLease = secLease;
        return *this;
    }

    /** Reassigns the binding to the given client.   */
    Binding &giveTo(const ClientId &a_id) RT_NOEXCEPT
    {
        m_id = a_id;
        m_state = FREE;
        return *this;
    }

    void free()
    {
        m_id = ClientId();
        m_state = FREE;
    }

    bool expire(Timestamp tsDeadline) RT_NOEXCEPT;
    bool expire() RT_NOEXCEPT
    {
        return expire(Timestamp::now());
    }

    /** @name Serialization
     * @{ */
    static Binding *fromXML(const xml::ElementNode *pElmLease);
    void            toXML(xml::ElementNode *pElmParent) const;
    /** @} */

    /** @name String formatting of %R[binding].
     * @{ */
    static void registerFormat() RT_NOEXCEPT;
private:
    static DECLCALLBACK(size_t) rtStrFormat(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput, const char *pszType,
                                            void const *pvValue, int cchWidth, int cchPrecision, unsigned fFlags, void *pvUser);
    static bool g_fFormatRegistered;
    /** @} */

    Binding &operator=(const Binding &); /**< Shuts up warning C4626 (incorrect warning?). */
};


/**
 * The lease database.
 *
 * There is currently just one instance of this class in a running DHCP server
 * residing in Dhcpd::m_db.  It covers one single range of IPv4 addresses, which
 * currently unbound addressed are managed by m_pool.  The allocated addresses
 * are kept in the m_bindings list.  Once an address has been allocated, it will
 * stay in the m_bindings list even after released or expired.
 */
class Db
{
private:
    typedef std::list<Binding *> bindings_t;

    /** Configuration (set at init).
     * @note Currently not used.  */
    const Config   *m_pConfig;
    /** The lease database.
     * @note Since fixed assignments are added during initialization, they will
     *       always be first.  The allocateBinding() code depends on this.  */
    bindings_t      m_bindings;
    /** Address allocation pool. */
    IPv4Pool        m_pool;

public:
    Db();
    ~Db();

    int      init(const Config *pConfig);

    /** Check if @a addr belonges to this lease database. */
    bool     addressBelongs(RTNETADDRIPV4 addr) const RT_NOEXCEPT { return m_pool.contains(addr); }

    Binding *allocateBinding(const DhcpClientMessage &req, Config::ConfigVec const &rConfigVec);
    bool     releaseBinding(const DhcpClientMessage &req) RT_NOEXCEPT;

    void     cancelOffer(const DhcpClientMessage &req) RT_NOEXCEPT;

    void     expire() RT_NOEXCEPT;

    /** @name Database serialization methods
     * @{ */
    int      loadLeases(const RTCString &strFilename) RT_NOEXCEPT;
private:
    int      i_loadLease(const xml::ElementNode *pElmLease) RT_NOEXCEPT;
public:
    int      writeLeases(const RTCString &strFilename) const RT_NOEXCEPT;
    /** @} */

private:
    int      i_enterFixedAddressAssignment(RTNETADDRIPV4 const &a_rAddress, RTMAC const &a_rMACAddress) RT_NOEXCEPT;
    Binding *i_createBinding(const ClientId &id = ClientId());
    Binding *i_createBinding(RTNETADDRIPV4 addr, const ClientId &id = ClientId());

    Binding *i_allocateAddress(const ClientId &id, RTNETADDRIPV4 addr);

    /* add binding e.g. from the leases file */
    int      i_addBinding(Binding *pNewBinding) RT_NOEXCEPT;
};

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_Db_h */
