/* $Id: DHCPD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - protocol logic
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
#include "DHCPD.h"
#include "DhcpOptions.h"

#include <iprt/message.h>


DHCPD::DHCPD()
  : m_pConfig(NULL), m_db()
{
}


/**
 * Initializes the DHCPD with the given config.
 *
 * @returns VBox status code.
 * @param   pConfig     The configuration to use.
 */
int DHCPD::init(const Config *pConfig) RT_NOEXCEPT
{
    Assert(pConfig);
    AssertReturn(!m_pConfig, VERR_INVALID_STATE);
    m_pConfig = pConfig;

    /* Load the lease database, ignoring most issues except being out of memory: */
    int rc = m_db.init(pConfig);
    if (RT_SUCCESS(rc))
    {
        rc = i_loadLeases();
        if (rc != VERR_NO_MEMORY)
            return VINF_SUCCESS;

        DHCP_LOG_MSG_ERROR(("Ran out of memory loading leases from '%s'.  Try rename or delete the file.\n",
                            pConfig->getLeasesFilename().c_str()));
    }
    return rc;
}


/**
 * Load leases from pConfig->getLeasesFilename().
 */
int DHCPD::i_loadLeases() RT_NOEXCEPT
{
    return m_db.loadLeases(m_pConfig->getLeasesFilename());
}


/**
 * Save the current leases to pConfig->getLeasesFilename(), doing expiry first.
 *
 * This is called after m_db is updated during a client request, so the on disk
 * database is always up-to-date.   This means it doesn't matter if we're
 * terminated with extreme prejudice, and it allows Main to look up IP addresses
 * for VMs.
 *
 * @throws nothing
 */
void DHCPD::i_saveLeases() RT_NOEXCEPT
{
    m_db.expire();
    m_db.writeLeases(m_pConfig->getLeasesFilename());
}


/**
 * Process a DHCP client message.
 *
 * Called by VBoxNetDhcpd::dhcp4Recv().
 *
 * @returns Pointer to DHCP reply (caller deletes this).  NULL if no reply
 *          warranted or we're out of memory.
 * @param   req                 The client message.
 * @throws  nothing
 */
DhcpServerMessage *DHCPD::process(DhcpClientMessage &req) RT_NOEXCEPT
{
    /*
     * Dump the package if release log level 3+1 are enable or if debug logging is
     * enabled.  We don't normally want to do this at the default log level, of course.
     */
    if ((LogRelIs3Enabled() && LogRelIsEnabled()) || LogIsEnabled())
        req.dump();

    /*
     * Fend off requests that are not for us.
     */
    OptServerId sid(req);
    if (sid.present() && sid.value().u != m_pConfig->getIPv4Address().u)
    {
        if (req.broadcasted() && req.messageType() == RTNET_DHCP_MT_REQUEST)
        {
            LogRel2(("Message is not for us, canceling any pending offer.\n"));
            m_db.cancelOffer(req);
        }
        else
            LogRel2(("Message is not for us.\n"));
        return NULL;
    }

    /*
     * Process it.
     */
    DhcpServerMessage *reply = NULL;

    switch (req.messageType())
    {
        /*
         * Requests that require server's reply.
         */
        case RTNET_DHCP_MT_DISCOVER:
            try
            {
                reply = i_doDiscover(req);
            }
            catch (std::bad_alloc &)
            {
                LogRelFunc(("i_doDiscover threw bad_alloc\n"));
            }
            break;

        case RTNET_DHCP_MT_REQUEST:
            try
            {
                reply = i_doRequest(req);
            }
            catch (std::bad_alloc &)
            {
                LogRelFunc(("i_doRequest threw bad_alloc\n"));
            }
            break;

        case RTNET_DHCP_MT_INFORM:
            try
            {
                reply = i_doInform(req);
            }
            catch (std::bad_alloc &)
            {
                LogRelFunc(("i_doInform threw bad_alloc\n"));
            }
            break;

        /*
         * Requests that don't have a reply.
         */
        case RTNET_DHCP_MT_DECLINE:
            i_doDecline(req);
            break;

        case RTNET_DHCP_MT_RELEASE:
            i_doRelease(req);
            break;

        /*
         * Unexpected or unknown message types.
         */
        case RTNET_DHCP_MT_OFFER:
            LogRel2(("Ignoring unexpected message of type RTNET_DHCP_MT_OFFER!\n"));
            break;
        case RTNET_DHCP_MT_ACK:
            LogRel2(("Ignoring unexpected message of type RTNET_DHCP_MT_ACK!\n"));
            break;
        case RTNET_DHCP_MT_NAC:
            LogRel2(("Ignoring unexpected message of type RTNET_DHCP_MT_NAC!\n"));
            break;
        default:
            LogRel2(("Ignoring unexpected message of unknown type: %d (%#x)!\n", req.messageType(), req.messageType()));
            break;
    }

    return reply;
}


/**
 * Internal helper.
 *
 * @throws  std::bad_alloc
 */
DhcpServerMessage *DHCPD::i_createMessage(int type, const DhcpClientMessage &req)
{
    return new DhcpServerMessage(req, type, m_pConfig->getIPv4Address());
}


/**
 * 4.3.1 DHCPDISCOVER message
 *
 * When a server receives a DHCPDISCOVER message from a client, the server
 * chooses a network address for the requesting client. If no address is
 * available, the server may choose to report the problem to the system
 * administrator. If an address is available, the new address SHOULD be chosen
 * as follows:
 *  - The client's current address as recorded in the client's current binding,
 *    ELSE
 *  - The client's previous address as recorded in the client's (now expired or
 *    released) binding, if that address is in the server's pool of available
 *    addresses and not already allocated, ELSE
 *  - The address requested in the 'Requested IP Address' option, if that
 *    address is valid and not already allocated, ELSE
 *  - A new address allocated from the server's pool of available addresses;
 *    the address is selected based on the subnet from which the message was
 *    received (if 'giaddr' is 0) or on the address of the relay agent that
 *    forwarded the message ('giaddr' when not 0).
 *
 * ...
 *
 * @throws  std::bad_alloc
 */
DhcpServerMessage *DHCPD::i_doDiscover(const DhcpClientMessage &req)
{
    /** @todo
     * XXX: TODO: Windows iSCSI initiator sends DHCPDISCOVER first and
     * it has ciaddr filled.  Shouldn't let it screw up the normal
     * lease we already have for that client, but we should probably
     * reply with a pro-forma offer.
     */
    if (req.ciaddr().u != 0)
        return NULL;

    Config::ConfigVec vecConfigs;
    m_pConfig->getConfigsForClient(vecConfigs, req.clientId(), OptVendorClassId(req), OptUserClassId(req));

    Binding *b = m_db.allocateBinding(req, vecConfigs);
    if (b == NULL)
        return NULL;

    std::unique_ptr<DhcpServerMessage> reply;

    bool fRapidCommit = OptRapidCommit(req).present();
    if (!fRapidCommit)
    {
        reply.reset(i_createMessage(RTNET_DHCP_MT_OFFER, req));

        if (b->state() < Binding::OFFERED)
            b->setState(Binding::OFFERED);

        /** @todo use small lease time internally to quickly free unclaimed offers? */
    }
    else
    {
        reply.reset(i_createMessage(RTNET_DHCP_MT_ACK, req));
        reply->addOption(OptRapidCommit(true));

        b->setState(Binding::ACKED);
        if (!b->isFixed())
            i_saveLeases();
    }

    reply->setYiaddr(b->addr());
    reply->addOption(OptLeaseTime(b->leaseTime()));

    OptParameterRequest optlist(req);
    optmap_t replyOptions;
    reply->addOptions(m_pConfig->getOptionsForClient(replyOptions, optlist, vecConfigs));

    // reply->maybeUnicast(req); /** @todo XXX: we reject ciaddr != 0 above */
    return reply.release();
}


/**
 * 4.3.2 DHCPREQUEST message
 *
 * A DHCPREQUEST message may come from a client responding to a DHCPOFFER
 * message from a server, from a client verifying a previously allocated IP
 * address or from a client extending the lease on a network address. If the
 * DHCPREQUEST message contains a 'server identifier' option, the message is in
 * response to a DHCPOFFER message. Otherwise, the message is a request to
 * verify or extend an existing lease. If the client uses a 'client identifier'
 * in a DHCPREQUEST message, it MUST use that same 'client identifier' in all
 * subsequent messages. If the client included a list of requested parameters in
 * a DHCPDISCOVER message, it MUST include that list in all subsequent messages.
 *
 * ...
 *
 * @throws  std::bad_alloc
 */
DhcpServerMessage *DHCPD::i_doRequest(const DhcpClientMessage &req)
{
    OptRequestedAddress reqAddr(req);
    if (req.ciaddr().u != 0 && reqAddr.present() && reqAddr.value().u != req.ciaddr().u)
    {
        std::unique_ptr<DhcpServerMessage> nak(i_createMessage(RTNET_DHCP_MT_NAC, req));
        nak->addOption(OptMessage("Requested address does not match ciaddr"));
        return nak.release();
    }

    Config::ConfigVec vecConfigs;
    m_pConfig->getConfigsForClient(vecConfigs, req.clientId(), OptVendorClassId(req), OptUserClassId(req));

    Binding *b = m_db.allocateBinding(req, vecConfigs);
    if (b == NULL)
    {
        return i_createMessage(RTNET_DHCP_MT_NAC, req);
    }

    std::unique_ptr<DhcpServerMessage> ack(i_createMessage(RTNET_DHCP_MT_ACK, req));

    b->setState(Binding::ACKED);
    if (!b->isFixed())
        i_saveLeases();

    ack->setYiaddr(b->addr());
    ack->addOption(OptLeaseTime(b->leaseTime()));

    OptParameterRequest optlist(req);
    optmap_t replyOptions;
    ack->addOptions(m_pConfig->getOptionsForClient(replyOptions, optlist, vecConfigs));

    ack->maybeUnicast(req);
    return ack.release();
}


/**
 * 4.3.5 DHCPINFORM message
 *
 * The server responds to a DHCPINFORM message by sending a DHCPACK message
 * directly to the address given in the 'ciaddr' field of the DHCPINFORM
 * message.  The server MUST NOT send a lease expiration time to the client and
 * SHOULD NOT fill in 'yiaddr'.  The server includes other parameters in the
 * DHCPACK message as defined in section 4.3.1.
 *
 * @throws  std::bad_alloc
 */
DhcpServerMessage *DHCPD::i_doInform(const DhcpClientMessage &req)
{
    if (req.ciaddr().u == 0)
        return NULL;

    OptParameterRequest optlist(req);
    if (!optlist.present())
        return NULL;

    Config::ConfigVec vecConfigs;
    optmap_t info;
    m_pConfig->getOptionsForClient(info, optlist, m_pConfig->getConfigsForClient(vecConfigs, req.clientId(),
                                                                                 OptVendorClassId(req), OptUserClassId(req)));
    if (info.empty())
        return NULL;

    std::unique_ptr<DhcpServerMessage> ack(i_createMessage(RTNET_DHCP_MT_ACK, req));
    ack->addOptions(info);
    ack->maybeUnicast(req);
    return ack.release();
}


/**
 * 4.3.3 DHCPDECLINE message
 *
 * If the server receives a DHCPDECLINE message, the client has discovered
 * through some other means that the suggested network address is already in
 * use.  The server MUST mark the network address as not available and SHOULD
 * notify the local system administrator of a possible configuration problem.
 *
 * @throws  nothing
 */
DhcpServerMessage *DHCPD::i_doDecline(const DhcpClientMessage &req) RT_NOEXCEPT
{
    RT_NOREF(req);
    return NULL;
}


/**
 * 4.3.4 DHCPRELEASE message
 *
 * Upon receipt of a DHCPRELEASE message, the server marks the network address
 * as not allocated.  The server SHOULD retain a record of the client's
 * initialization parameters for possible reuse in response to subsequent
 * requests from the client.
 *
 * @throws  nothing
 */
DhcpServerMessage *DHCPD::i_doRelease(const DhcpClientMessage &req) RT_NOEXCEPT
{
    if (req.ciaddr().u != 0)
    {
        bool fReleased = m_db.releaseBinding(req);
        if (fReleased)
            i_saveLeases();
    }

    return NULL;
}
