/* $Id: Db.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "DhcpdInternal.h"
#include <iprt/errcore.h>

#include "Db.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Indicates whether has been called successfully yet. */
bool Binding::g_fFormatRegistered = false;


/**
 * Registers the ClientId format type callback ("%R[binding]").
 */
void Binding::registerFormat() RT_NOEXCEPT
{
    if (!g_fFormatRegistered)
    {
        int rc = RTStrFormatTypeRegister("binding", rtStrFormat, NULL);
        AssertRC(rc);
        g_fFormatRegistered = true;
    }
}


/**
 * @callback_method_impl{FNRTSTRFORMATTYPE, Formats ClientId via "%R[binding]".}
 */
DECLCALLBACK(size_t)
Binding::rtStrFormat(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                     const char *pszType, void const *pvValue,
                     int cchWidth, int cchPrecision, unsigned fFlags,
                     void *pvUser)
{

    AssertReturn(strcmp(pszType, "binding") == 0, 0);
    RT_NOREF(pszType);

    RT_NOREF(cchWidth, cchPrecision, fFlags);
    RT_NOREF(pvUser);

    const Binding *b = static_cast<const Binding *>(pvValue);
    if (b == NULL)
        return pfnOutput(pvArgOutput, RT_STR_TUPLE("<NULL>"));

    size_t cb = RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "%RTnaipv4", b->m_addr.u);
    if (b->m_state == Binding::FREE)
        cb += pfnOutput(pvArgOutput, RT_STR_TUPLE(" free"));
    else if (b->m_fFixed)
        cb += pfnOutput(pvArgOutput, RT_STR_TUPLE(" fixed"));
    else
    {
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, " to %R[id], %s, valid from ", &b->m_id, b->stateName());

        Timestamp tsIssued = b->issued();
        cb += tsIssued.strFormatHelper(pfnOutput, pvArgOutput);

        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, " for %ds until ", b->leaseTime());

        Timestamp tsValid = b->issued();
        tsValid.addSeconds(b->leaseTime());
        cb += tsValid.strFormatHelper(pfnOutput, pvArgOutput);
    }

    return cb;
}


/**
 * Used to update the client ID of a fixed address assignment.
 *
 * We only have the MAC address when prepraring the binding, so the full client
 * ID must be supplied when the client requests it.
 *
 * @param   a_ridClient         The client ID.
 * @throws  std::bad_alloc
 */
void Binding::idUpdate(const ClientId &a_ridClient)
{
    AssertReturnVoid(isFixed());
    m_id = a_ridClient;
}


/**
 * Get the state as a string for the XML lease database.
 */
const char *Binding::stateName() const RT_NOEXCEPT
{
    switch (m_state)
    {
        case FREE:
            return "free";
        case RELEASED:
            return "released";
        case EXPIRED:
            return "expired";
        case OFFERED:
            return "offered";
        case ACKED:
            return "acked";
        default:
            AssertMsgFailed(("%d\n", m_state));
            return "released";
    }
}


/**
 * Sets the state by name (reverse of Binding::stateName()).
 */
Binding &Binding::setState(const char *pszStateName) RT_NOEXCEPT
{
    if (strcmp(pszStateName, "free") == 0)
        m_state = Binding::FREE;
    else if (strcmp(pszStateName, "released") == 0)
        m_state = Binding::RELEASED;
    else if (strcmp(pszStateName, "expired") == 0)
        m_state = Binding::EXPIRED;
    else if (strcmp(pszStateName, "offered") == 0)
        m_state = Binding::OFFERED;
    else if (strcmp(pszStateName, "acked") == 0)
        m_state = Binding::ACKED;
    else
    {
        AssertMsgFailed(("%d\n", m_state));
        m_state = Binding::RELEASED;
    }

    return *this;
}


/**
 * Expires the binding if it's past the specified deadline.
 *
 * @returns False if already expired, released or freed, otherwise true (i.e.
 *          does not indicate whether action was taken or not).
 * @param   tsDeadline          The expiry deadline to use.
 */
bool Binding::expire(Timestamp tsDeadline) RT_NOEXCEPT
{
    if (m_state <= Binding::EXPIRED || m_fFixed)
        return false;

    Timestamp tsExpire = m_issued;
    tsExpire.addSeconds(m_secLease);

    if (tsExpire < tsDeadline)
    {
        if (m_state == Binding::OFFERED)
            setState(Binding::FREE);
        else
            setState(Binding::EXPIRED);
    }
    return true;
}


/**
 * Serializes the binding to XML for the lease database.
 *
 * @throw   std::bad_alloc
 * @note    DHCPServerImpl.cpp contains a reader, keep it in sync.
 */
void Binding::toXML(xml::ElementNode *pElmParent) const
{
    /*
     * Lease
     */
    xml::ElementNode *pElmLease = pElmParent->createChild("Lease");

    pElmLease->setAttribute("mac", RTCStringFmt("%RTmac", &m_id.mac()));
    if (m_id.id().present())
    {
        /* I'd prefer RTSTRPRINTHEXBYTES_F_SEP_COLON but there's no decoder */
        size_t cbStrId = m_id.id().value().size() * 2 + 1;
        char *pszId = new char[cbStrId];
        int rc = RTStrPrintHexBytes(pszId, cbStrId,
                                    &m_id.id().value().front(), m_id.id().value().size(),
                                    0);
        AssertRC(rc);
        pElmLease->setAttribute("id", pszId);
        delete[] pszId;
    }

    /* unused but we need it to keep the old code happy */
    pElmLease->setAttribute("network", "0.0.0.0");
    pElmLease->setAttribute("state", stateName());

    /*
     * Lease/Address
     */
    xml::ElementNode *pElmAddr = pElmLease->createChild("Address");
    pElmAddr->setAttribute("value", RTCStringFmt("%RTnaipv4", m_addr.u));

    /*
     * Lease/Time
     */
    xml::ElementNode *pElmTime = pElmLease->createChild("Time");
    pElmTime->setAttribute("issued", m_issued.getAbsSeconds());
    pElmTime->setAttribute("expiration", m_secLease);
}


/**
 * Deserializes the binding from the XML lease database.
 *
 * @param   pElmLease   The "Lease" element to serialize into.
 * @return  Pointer to the resulting binding, NULL on failure.
 * @throw   std::bad_alloc
 * @note    DHCPServerImpl.cpp contains a similar reader, keep it in sync.
 */
Binding *Binding::fromXML(const xml::ElementNode *pElmLease)
{
    /* Note! Lease/@network seems to always have bogus value, ignore it. */
    /* Note! We parse the mandatory attributes and elements first, then
             the optional ones.  This means things appear a little jumbled. */

    /*
     * Lease/@mac - mandatory.
     */
    const char *pszMacAddress = pElmLease->findAttributeValue("mac");
    if (!pszMacAddress)
        DHCP_LOG_RET_NULL(("Binding::fromXML: <Lease> element without 'mac' attribute! Skipping lease.\n"));

    RTMAC mac;
    int rc = RTNetStrToMacAddr(pszMacAddress, &mac);
    if (RT_FAILURE(rc))
        DHCP_LOG_RET_NULL(("Binding::fromXML: Malformed mac address attribute value '%s': %Rrc - Skipping lease.\n",
                           pszMacAddress, rc));

    /*
     * Lease/Address/@value - mandatory.
     */
    const char *pszAddress = pElmLease->findChildElementAttributeValue("Address", "value");
    if (!pszAddress)
        DHCP_LOG_RET_NULL(("Binding::fromXML: Could not find <Address> with a 'value' attribute! Skipping lease.\n"));

    RTNETADDRIPV4 addr;
    rc = RTNetStrToIPv4Addr(pszAddress, &addr);
    if (RT_FAILURE(rc))
        DHCP_LOG_RET_NULL(("Binding::fromXML: Malformed IPv4 address value '%s': %Rrc - Skipping lease.\n", pszAddress, rc));

    /*
     * Lease/Time - mandatory.
     */
    const xml::ElementNode *pElmTime = pElmLease->findChildElement("Time");
    if (pElmTime == NULL)
        DHCP_LOG_RET_NULL(("Binding::fromXML: No <Time> element under <Lease mac=%RTmac>! Skipping lease.\n", &mac));

    /*
     * Lease/Time/@issued - mandatory.
     */
    int64_t secIssued;
    if (!pElmTime->getAttributeValue("issued", &secIssued))
        DHCP_LOG_RET_NULL(("Binding::fromXML: <Time> element for %RTmac has no valid 'issued' attribute! Skipping lease.\n", &mac));

    /*
     * Lease/Time/@expiration - mandatory.
     */
    uint32_t cSecToLive;
    if (!pElmTime->getAttributeValue("expiration", &cSecToLive))
        DHCP_LOG_RET_NULL(("Binding::fromXML: <Time> element for %RTmac has no valid 'expiration' attribute! Skipping lease.\n", &mac));

    std::unique_ptr<Binding> b(new Binding(addr));

    /*
     * Lease/@state - mandatory but not present in old leases file, so pretent
     *                we're loading an expired one if absent.
     */
    const char *pszState = pElmLease->findAttributeValue("state");
    if (pszState)
    {
        b->m_issued = Timestamp::absSeconds(secIssued);
        b->setState(pszState);
    }
    else
    {   /** @todo XXX: old code wrote timestamps instead of absolute time. */
        /* pretend that lease has just ended */
        LogRel(("Binding::fromXML: No 'state' attribute for <Lease mac=%RTmac> (ts=%RI64 ttl=%RU32)! Assuming EXPIRED.\n",
                &mac, secIssued, cSecToLive));
        b->m_issued = Timestamp::now().subSeconds(cSecToLive);
        b->m_state  = Binding::EXPIRED;
    }
    b->m_secLease   = cSecToLive;


    /*
     * Lease/@id - optional, ignore if bad.
     * Value format: "deadbeef..." or "de:ad:be:ef...".
     */
    const char *pszClientId = pElmLease->findAttributeValue("id");
    if (pszClientId)
    {
        uint8_t abBytes[255];
        size_t  cbActual;
        rc = RTStrConvertHexBytesEx(pszClientId, abBytes, sizeof(abBytes), RTSTRCONVERTHEXBYTES_F_SEP_COLON, NULL, &cbActual);
        if (RT_SUCCESS(rc))
        {
            b->m_id = ClientId(mac, OptClientId(std::vector<uint8_t>(&abBytes[0], &abBytes[cbActual]))); /* throws bad_alloc */
            if (rc != VINF_BUFFER_UNDERFLOW && rc != VINF_SUCCESS)
                LogRel(("Binding::fromXML: imperfect 'id' attribute: rc=%Rrc, cbActual=%u, '%s'\n", rc, cbActual, pszClientId));
        }
        else
        {
            LogRel(("Binding::fromXML: ignoring malformed 'id' attribute: rc=%Rrc, cbActual=%u, '%s'\n",
                    rc, cbActual, pszClientId));
            b->m_id = ClientId(mac, OptClientId());
        }
    }
    else
        b->m_id = ClientId(mac, OptClientId());

    return b.release();
}



/*********************************************************************************************************************************
*   Class Db Implementation                                                                                                      *
*********************************************************************************************************************************/

Db::Db()
    : m_pConfig(NULL)
{
}


Db::~Db()
{
    /** @todo free bindings */
}


int Db::init(const Config *pConfig)
{
    Binding::registerFormat();

    m_pConfig = pConfig;

    int rc = m_pool.init(pConfig->getIPv4PoolFirst(), pConfig->getIPv4PoolLast());
    if (RT_SUCCESS(rc))
    {
        /*
         * If the server IP is in the dynamic range, preallocate it like a fixed assignment.
         */
        rc = i_enterFixedAddressAssignment(pConfig->getIPv4Address(), pConfig->getMacAddress());
        if (RT_SUCCESS(rc))
        {
            /*
             * Preallocate any fixed address assignments:
             */
            Config::HostConfigVec vecHostConfigs;
            rc = pConfig->getFixedAddressConfigs(vecHostConfigs);
            for (Config::HostConfigVec::const_iterator it = vecHostConfigs.begin();
                 it != vecHostConfigs.end() && RT_SUCCESS(rc); ++it)
                rc = i_enterFixedAddressAssignment((*it)->getFixedAddress(), (*it)->getMACAddress());
        }
    }

    return rc;
}


/**
 * Used by Db::init() to register a fixed address assignment.
 *
 * @returns IPRT status code.
 * @param   a_rAddress      The IPv4 address assignment.
 * @param   a_rMACAddress   The MAC address.
 */
int Db::i_enterFixedAddressAssignment(RTNETADDRIPV4 const &a_rAddress, RTMAC const &a_rMACAddress) RT_NOEXCEPT
{
    LogRelFunc(("%RTmac: %RTnaipv4\n", &a_rMACAddress, a_rAddress));
    Assert(m_pConfig->isInIPv4Network(a_rAddress)); /* should've been checked elsewhere already */

    /*
     * If the address is part of the pool, we have to allocate it to
     * prevent it from being used again.
     */
    if (m_pool.contains(a_rAddress))
    {
        if (!m_pool.allocate(a_rAddress))
        {
            LogRelFunc(("%RTnaipv4 already allocated?\n", a_rAddress));
            return VERR_ADDRESS_CONFLICT;
        }
    }

    /*
     * Create the binding.
     */
    Binding *pBinding = NULL;
    try
    {
        pBinding = new Binding(a_rAddress, a_rMACAddress, true /*fFixed*/);
        m_bindings.push_front(pBinding);
    }
    catch (std::bad_alloc &)
    {
        if (pBinding)
            delete pBinding;
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


/**
 * Expire old binding (leases).
 */
void Db::expire() RT_NOEXCEPT
{
    const Timestamp now = Timestamp::now();
    for (bindings_t::iterator it = m_bindings.begin(); it != m_bindings.end(); ++it)
    {
        Binding *b = *it;
        b->expire(now);
    }
}


/**
 * Internal worker that creates a binding for the given client, allocating new
 * IPv4 address for it.
 *
 * @returns Pointer to the binding.
 * @param   id          The client ID.
 */
Binding *Db::i_createBinding(const ClientId &id)
{
    Binding      *pBinding = NULL;
    RTNETADDRIPV4 addr = m_pool.allocate();
    if (addr.u != 0)
    {
        try
        {
            pBinding = new Binding(addr, id);
            m_bindings.push_front(pBinding);
        }
        catch (std::bad_alloc &)
        {
            if (pBinding)
                delete pBinding;
            /** @todo free address (no pool method for that)  */
        }
    }
    return pBinding;
}


/**
 * Internal worker that creates a binding to the specified IPv4 address for the
 * given client.
 *
 * @returns Pointer to the binding.
 *          NULL if the address is in use or we ran out of memory.
 * @param   addr        The IPv4 address.
 * @param   id          The client.
 */
Binding *Db::i_createBinding(RTNETADDRIPV4 addr, const ClientId &id)
{
    bool fAvailable = m_pool.allocate(addr);
    if (!fAvailable)
    {
        /** @todo
         * XXX: this should not happen.  If the address is from the
         * pool, which we have verified before, then either it's in
         * the free pool or there's an binding (possibly free) for it.
         */
        return NULL;
    }

    Binding *b = new Binding(addr, id);
    m_bindings.push_front(b);
    return b;
}


/**
 * Internal worker that allocates an IPv4 address for the given client, taking
 * the preferred address (@a addr) into account when possible and if non-zero.
 */
Binding *Db::i_allocateAddress(const ClientId &id, RTNETADDRIPV4 addr)
{
    Assert(addr.u == 0 || addressBelongs(addr));

    if (addr.u != 0)
        LogRel(("> allocateAddress %RTnaipv4 to client %R[id]\n", addr.u, &id));
    else
        LogRel(("> allocateAddress to client %R[id]\n", &id));

    /*
     * Allocate existing address if client has one.  Ignore requested
     * address in that case.  While here, look for free addresses and
     * addresses that can be reused.
     */
    Binding        *addrBinding  = NULL;
    Binding        *freeBinding  = NULL;
    Binding        *reuseBinding = NULL;
    const Timestamp now          = Timestamp::now();
    for (bindings_t::iterator it = m_bindings.begin(); it != m_bindings.end(); ++it)
    {
        Binding *b = *it;
        b->expire(now);

        /*
         * We've already seen this client, give it its old binding.
         *
         * If the client's MAC address is configured with a fixed
         * address, give its preconfigured binding.  Fixed bindings
         * are always at the head of the m_bindings list, so we
         * won't be confused by any old leases of the client.
         */
        if (b->m_id == id)
        {
            LogRel(("> ... found existing binding %R[binding]\n", b));
            return b;
        }
        if (b->isFixed() && b->id().mac() == id.mac())
        {
            b->idUpdate(id);
            LogRel(("> ... found fixed binding %R[binding]\n", b));
            return b;
        }

        if (addr.u != 0 && b->m_addr.u == addr.u)
        {
            Assert(addrBinding == NULL);
            addrBinding = b;
            LogRel(("> .... noted existing binding %R[binding]\n", addrBinding));
        }

        /* if we haven't found a free binding yet, keep looking */
        if (freeBinding == NULL)
        {
            if (b->m_state == Binding::FREE)
            {
                freeBinding = b;
                LogRel(("> .... noted free binding %R[binding]\n", freeBinding));
                continue;
            }

            /* still no free binding, can this one be reused? */
            if (b->m_state == Binding::RELEASED)
            {
                if (   reuseBinding == NULL
                    /* released binding is better than an expired one */
                    || reuseBinding->m_state == Binding::EXPIRED)
                {
                    reuseBinding = b;
                    LogRel(("> .... noted released binding %R[binding]\n", reuseBinding));
                }
            }
            else if (b->m_state == Binding::EXPIRED)
            {
                if (   reuseBinding == NULL
                    /* long expired binding is bettern than a recent one */
                    /* || (reuseBinding->m_state == Binding::EXPIRED && b->olderThan(reuseBinding)) */)
                {
                    reuseBinding = b;
                    LogRel(("> .... noted expired binding %R[binding]\n", reuseBinding));
                }
            }
        }
    }

    /*
     * Allocate requested address if we can.
     */
    if (addr.u != 0)
    {
        if (addrBinding == NULL)
        {
            addrBinding = i_createBinding(addr, id);
            Assert(addrBinding != NULL);
            LogRel(("> .... creating new binding for this address %R[binding]\n", addrBinding));
            return addrBinding;
        }

        if (addrBinding->m_state <= Binding::EXPIRED) /* not in use */
        {
            LogRel(("> .... reusing %s binding for this address\n", addrBinding->stateName()));
            addrBinding->giveTo(id);
            return addrBinding;
        }
        LogRel(("> .... cannot reuse %s binding for this address\n", addrBinding->stateName()));
    }

    /*
     * Allocate new (or reuse).
     */
    Binding *idBinding = NULL;
    if (freeBinding != NULL)
    {
        idBinding = freeBinding;
        LogRel(("> .... reusing free binding\n"));
    }
    else
    {
        idBinding = i_createBinding();
        if (idBinding != NULL)
            LogRel(("> .... creating new binding\n"));
        else
        {
            idBinding = reuseBinding;
            if (idBinding != NULL)
                LogRel(("> .... reusing %s binding %R[binding]\n", reuseBinding->stateName(), reuseBinding));
            else
                DHCP_LOG_RET_NULL(("> .... failed to allocate binding\n"));
        }
    }

    idBinding->giveTo(id);
    LogRel(("> .... allocated %R[binding]\n", idBinding));

    return idBinding;
}



/**
 * Called by DHCPD to allocate a binding for the specified request.
 *
 * @returns Pointer to the binding, NULL on failure.
 * @param   req                 The DHCP request being served.
 * @param   rConfigVec          The configurations that applies to the client.
 *                              Used for lease time calculation.
 */
Binding *Db::allocateBinding(const DhcpClientMessage &req, Config::ConfigVec const &rConfigVec)
{
    const ClientId &id(req.clientId());

    /*
     * Get and validate the requested address (if present).
     *
     * Fixed assignments are often outside the dynamic range, so we much detect
     * those to make sure they aren't rejected based on IP range.  ASSUMES fixed
     * assignments are at the head of the binding list.
     */
    OptRequestedAddress reqAddr(req);
    if (reqAddr.present() && !addressBelongs(reqAddr.value()))
    {
        bool fIsFixed = false;
        for (bindings_t::iterator it = m_bindings.begin(); it != m_bindings.end() && (*it)->isFixed(); ++it)
            if (reqAddr.value().u == (*it)->addr().u)
            {
                if (   (*it)->id() == id
                    || (*it)->id().mac() == id.mac())
                {
                    fIsFixed = true;
                    break;
                }
            }
        if (fIsFixed)
            reqAddr = OptRequestedAddress();
        else if (req.messageType() == RTNET_DHCP_MT_DISCOVER)
        {
            LogRel(("DISCOVER: ignoring invalid requested address\n"));
            reqAddr = OptRequestedAddress();
        }
        else
            DHCP_LOG_RET_NULL(("rejecting invalid requested address\n"));
    }

    /*
     * Allocate the address.
     */
    Binding *b = i_allocateAddress(id, reqAddr.value());
    if (b != NULL)
    {
        Assert(b->id() == id);

        /*
         * Figure out the lease time.
         */
        uint32_t secMin = 0;
        uint32_t secDfl = 0;
        uint32_t secMax = 0;
        for (Config::ConfigVec::const_iterator it = rConfigVec.begin(); it != rConfigVec.end(); ++it)
        {
            ConfigLevelBase const *pConfig = *it;
            if (secMin == 0)
                secMin = pConfig->getMinLeaseTime();
            if (secDfl == 0)
                secDfl = pConfig->getDefaultLeaseTime();
            if (secMax == 0)
                secMax = pConfig->getMaxLeaseTime();
        }
        Assert(secMin); Assert(secMax); Assert(secDfl); /* global config always have non-defaults set */
        if (secMin > secMax)
            secMin = secMax;

        OptLeaseTime reqLeaseTime(req);
        if (!reqLeaseTime.present())
        {
            b->setLeaseTime(secDfl);
            LogRel2(("Lease time %u secs (default)\n", b->leaseTime()));
        }
        else if (reqLeaseTime.value() < secMin)
        {
            b->setLeaseTime(secMin);
            LogRel2(("Lease time %u secs (min)\n", b->leaseTime()));
        }
        else if (reqLeaseTime.value() > secMax)
        {
            b->setLeaseTime(secMax);
            LogRel2(("Lease time %u secs (max)\n", b->leaseTime()));
        }
        else
        {
            b->setLeaseTime(reqLeaseTime.value());
            LogRel2(("Lease time %u secs (requested)\n", b->leaseTime()));
        }
    }
    return b;
}


/**
 * Internal worker used by loadLease().
 *
 * @returns IPRT status code.
 * @param   pNewBinding     The new binding to add.
 */
int Db::i_addBinding(Binding *pNewBinding) RT_NOEXCEPT
{
    /*
     * Validate the binding against the range and existing bindings.
     */
    if (!addressBelongs(pNewBinding->m_addr))
    {
        LogRel(("Binding for out of range address %RTnaipv4 ignored\n", pNewBinding->m_addr.u));
        return VERR_OUT_OF_RANGE;
    }

    for (bindings_t::iterator it = m_bindings.begin(); it != m_bindings.end(); ++it)
    {
        Binding *b = *it;

        if (pNewBinding->m_addr.u == b->m_addr.u)
        {
            LogRel(("> ADD: %R[binding]\n", pNewBinding));
            LogRel(("> .... duplicate ip: %R[binding]\n", b));
            return VERR_DUPLICATE;
        }

        if (pNewBinding->m_id == b->m_id)
        {
            LogRel(("> ADD: %R[binding]\n", pNewBinding));
            LogRel(("> .... duplicate id: %R[binding]\n", b));
            return VERR_DUPLICATE;
        }
    }

    /*
     * Allocate the address and add the binding to the list.
     */
    AssertLogRelMsgReturn(m_pool.allocate(pNewBinding->m_addr),
                          ("> ADD: failed to claim IP %R[binding]\n", pNewBinding),
                          VERR_INTERNAL_ERROR);
    try
    {
        m_bindings.push_back(pNewBinding);
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


/**
 * Called by DHCP to cancel an offset.
 *
 * @param   req                 The DHCP request.
 */
void Db::cancelOffer(const DhcpClientMessage &req) RT_NOEXCEPT
{
    const OptRequestedAddress reqAddr(req);
    if (!reqAddr.present())
        return;

    const RTNETADDRIPV4 addr = reqAddr.value();
    const ClientId     &id(req.clientId());

    for (bindings_t::iterator it = m_bindings.begin(); it != m_bindings.end(); ++it)
    {
        Binding *b = *it;

        if (b->addr().u == addr.u && b->id() == id)
        {
            if (b->state() == Binding::OFFERED)
            {
                LogRel2(("Db::cancelOffer: cancelling %R[binding]\n", b));
                if (!b->isFixed())
                {
                    b->setLeaseTime(0);
                    b->setState(Binding::RELEASED);
                }
                else
                    b->setState(Binding::ACKED);
            }
            else
                LogRel2(("Db::cancelOffer: not offered state: %R[binding]\n", b));
            return;
        }
    }
    LogRel2(("Db::cancelOffer: not found (%RTnaipv4, %R[id])\n", addr.u, &id));
}


/**
 * Called by DHCP to cancel an offset.
 *
 * @param   req                 The DHCP request.
 * @returns true if found and released, otherwise false.
 * @throws  nothing
 */
bool Db::releaseBinding(const DhcpClientMessage &req) RT_NOEXCEPT
{
    const RTNETADDRIPV4 addr = req.ciaddr();
    const ClientId     &id(req.clientId());

    for (bindings_t::iterator it = m_bindings.begin(); it != m_bindings.end(); ++it)
    {
        Binding *b = *it;

        if (b->addr().u == addr.u && b->id() == id)
        {
            LogRel2(("Db::releaseBinding: releasing %R[binding]\n", b));
            if (!b->isFixed())
            {
                b->setState(Binding::RELEASED);
                return true;
            }
            b->setState(Binding::ACKED);
            return false;
        }
    }

    LogRel2(("Db::releaseBinding: not found (%RTnaipv4, %R[id])\n", addr.u, &id));
    return false;
}


/**
 * Called by DHCPD to write out the lease database to @a strFilename.
 *
 * @returns IPRT status code.
 * @param   strFilename         The file to write it to.
 */
int Db::writeLeases(const RTCString &strFilename) const RT_NOEXCEPT
{
    LogRel(("writing leases to %s\n", strFilename.c_str()));

    /** @todo This could easily be written directly to the file w/o going thru
     *        a xml::Document, xml::XmlFileWriter, hammering the heap and being
     *        required to catch a lot of different exceptions at various points.
     *        (RTStrmOpen, bunch of RTStrmPrintf using \%RMas and \%RMes.,
     *        RTStrmClose closely followed by a couple of renames.)
     */

    /*
     * Create the document and root element.
     */
    xml::Document doc;
    try
    {
        xml::ElementNode *pElmRoot = doc.createRootElement("Leases");
        pElmRoot->setAttribute("version", "1.0");

        /*
         * Add the leases.
         */
        for (bindings_t::const_iterator it = m_bindings.begin(); it != m_bindings.end(); ++it)
        {
            const Binding *b = *it;
            if (!b->isFixed())
                b->toXML(pElmRoot);
        }
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }

    /*
     * Write the document to the specified file in a safe manner (written to temporary
     * file, renamed to destination on success)
     */
    try
    {
        xml::XmlFileWriter writer(doc);
        writer.write(strFilename.c_str(), true /*fSafe*/);
    }
    catch (const xml::EIPRTFailure &e)
    {
        LogRel(("%s\n", e.what()));
        return e.rc();
    }
    catch (const RTCError &e)
    {
        LogRel(("%s\n", e.what()));
        return VERR_GENERAL_FAILURE;
    }
    catch (...)
    {
        LogRel(("Unknown exception while writing '%s'\n", strFilename.c_str()));
        return VERR_UNEXPECTED_EXCEPTION;
    }

    return VINF_SUCCESS;
}


/**
 * Called by DHCPD to load the lease database to @a strFilename.
 *
 * @note Does not clear the database state before doing the load.
 *
 * @returns IPRT status code.
 * @param   strFilename         The file to load it from.
 * @throws  nothing
 */
int Db::loadLeases(const RTCString &strFilename) RT_NOEXCEPT
{
    LogRel(("loading leases from %s\n", strFilename.c_str()));

    /*
     * Load the file into an XML document.
     */
    xml::Document doc;
    try
    {
        xml::XmlFileParser parser;
        parser.read(strFilename.c_str(), doc);
    }
    catch (const xml::EIPRTFailure &e)
    {
        LogRel(("%s\n", e.what()));
        return e.rc();
    }
    catch (const RTCError &e)
    {
        LogRel(("%s\n", e.what()));
        return VERR_GENERAL_FAILURE;
    }
    catch (...)
    {
        LogRel(("Unknown exception while reading and parsing '%s'\n", strFilename.c_str()));
        return VERR_UNEXPECTED_EXCEPTION;
    }

    /*
     * Check that the root element is "Leases" and process its children.
     */
    xml::ElementNode *pElmRoot = doc.getRootElement();
    if (!pElmRoot)
    {
        LogRel(("No root element in '%s'\n", strFilename.c_str()));
        return VERR_NOT_FOUND;
    }
    if (!pElmRoot->nameEquals("Leases"))
    {
        LogRel(("No root element is not 'Leases' in '%s', but '%s'\n", strFilename.c_str(), pElmRoot->getName()));
        return VERR_NOT_FOUND;
    }

    int                     rc = VINF_SUCCESS;
    xml::NodesLoop          it(*pElmRoot);
    const xml::ElementNode *pElmLease;
    while ((pElmLease = it.forAllNodes()) != NULL)
    {
        if (pElmLease->nameEquals("Lease"))
        {
            int rc2 = i_loadLease(pElmLease);
            if (RT_SUCCESS(rc2))
            { /* likely */ }
            else if (rc2 == VERR_NO_MEMORY)
                return rc2;
            else
                rc = -rc2;
        }
        else
            LogRel(("Ignoring unexpected element '%s' under 'Leases'...\n", pElmLease->getName()));
    }

    return rc;
}


/**
 * Internal worker for loadLeases() that handles one 'Lease' element.
 *
 * @param   pElmLease           The 'Lease' element to handle.
 * @return  IPRT status code.
 */
int Db::i_loadLease(const xml::ElementNode *pElmLease) RT_NOEXCEPT
{
    Binding *pBinding = NULL;
    try
    {
        pBinding = Binding::fromXML(pElmLease);
    }
    catch (std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }
    if (pBinding)
    {
        bool fExpired = pBinding->expire();
        if (!fExpired)
            LogRel(("> LOAD:         lease %R[binding]\n", pBinding));
        else
            LogRel(("> LOAD: EXPIRED lease %R[binding]\n", pBinding));

        int rc = i_addBinding(pBinding);
        if (RT_FAILURE(rc))
            delete pBinding;
        return rc;
    }
    LogRel(("> LOAD: failed to load lease!\n"));
    return VERR_PARSE_ERROR;
}
