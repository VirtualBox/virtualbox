/* $Id: DhcpMessage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "DhcpdInternal.h"
#include "DhcpMessage.h"
#include "DhcpOptions.h"

#include <iprt/ctype.h>
#include <iprt/string.h>



DhcpMessage::DhcpMessage()
    : m_xid(0)
    , m_flags(0)
    , m_ciaddr()
    , m_yiaddr()
    , m_siaddr()
    , m_giaddr()
#if 0 /* not currently unused  */
    , m_sname()
    , m_file()
#endif
    , m_optMessageType()
{
}


/**
 * Does common message dumping.
 */
void DhcpMessage::dump() const RT_NOEXCEPT
{
    switch (m_optMessageType.value())
    {
        case RTNET_DHCP_MT_DISCOVER: LogRel(("DISCOVER")); break;
        case RTNET_DHCP_MT_OFFER:    LogRel(("OFFER")); break;
        case RTNET_DHCP_MT_REQUEST:  LogRel(("REQUEST")); break;
        case RTNET_DHCP_MT_DECLINE:  LogRel(("DECLINE")); break;
        case RTNET_DHCP_MT_ACK:      LogRel(("ACK")); break;
        case RTNET_DHCP_MT_NAC:      LogRel(("NAC")); break;
        case RTNET_DHCP_MT_RELEASE:  LogRel(("RELEASE")); break;
        case RTNET_DHCP_MT_INFORM:   LogRel(("INFORM")); break;
        default:
            LogRel(("<Unknown Mesage Type %d>", m_optMessageType.value()));
            break;
    }

    LogRel((" xid 0x%08x", m_xid));
    LogRel((" chaddr %RTmac\n", &m_mac));
    LogRel((" ciaddr %RTnaipv4", m_ciaddr.u));
    if (m_yiaddr.u != 0)
        LogRel((" yiaddr %RTnaipv4", m_yiaddr.u));
    if (m_siaddr.u != 0)
        LogRel((" siaddr %RTnaipv4", m_siaddr.u));
    if (m_giaddr.u != 0)
        LogRel((" giaddr %RTnaipv4", m_giaddr.u));
    if (broadcast())
        LogRel((" broadcast\n"));
    else
        LogRel(("\n"));
}


/*********************************************************************************************************************************
*   DhcpClientMessage Implementation                                                                                             *
*********************************************************************************************************************************/

/* static */
DhcpClientMessage *DhcpClientMessage::parse(bool broadcasted, const void *buf, size_t buflen)
{
    /*
     * Validate the request.
     */
    if (buflen < RT_OFFSETOF(RTNETBOOTP, bp_vend.Dhcp.dhcp_opts))
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: %zu bytes datagram is too short\n", buflen));

    PCRTNETBOOTP bp = (PCRTNETBOOTP)buf;

    if (bp->bp_op != RTNETBOOTP_OP_REQUEST)
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: bad opcode: %d\n", bp->bp_op));

    if (bp->bp_htype != RTNET_ARP_ETHER)
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: unsupported htype %d\n", bp->bp_htype));

    if (bp->bp_hlen != sizeof(RTMAC))
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: unexpected hlen %d\n", bp->bp_hlen));

    if (   (bp->bp_chaddr.Mac.au8[0] & 0x01) != 0
        && (bp->bp_flags & RTNET_DHCP_FLAG_BROADCAST) == 0)
        LogRel2(("DhcpClientMessage::parse: multicast chaddr %RTmac without broadcast flag\n", &bp->bp_chaddr.Mac));

    /* we don't want to deal with forwarding */
    if (bp->bp_giaddr.u != 0)
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: giaddr %RTnaipv4\n", bp->bp_giaddr.u));

    if (bp->bp_hops != 0)
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: non-zero hops %d\n", bp->bp_hops));

    if (bp->bp_vend.Dhcp.dhcp_cookie != RT_H2N_U32_C(RTNET_DHCP_COOKIE))
        DHCP_LOG2_RET_NULL(("DhcpClientMessage::parse: bad cookie %#RX32\n", bp->bp_vend.Dhcp.dhcp_cookie));

    /*
     * Convert it into a DhcpClientMessage instance.
     */
    std::unique_ptr<DhcpClientMessage> msg(new DhcpClientMessage());

    msg->m_broadcasted = broadcasted;
    msg->m_xid         = bp->bp_xid;
    msg->m_flags       = bp->bp_flags;
    msg->m_mac         = bp->bp_chaddr.Mac;
    msg->m_ciaddr      = bp->bp_ciaddr;
    msg->m_yiaddr      = bp->bp_yiaddr;
    msg->m_siaddr      = bp->bp_siaddr;
    msg->m_giaddr      = bp->bp_giaddr;

    int fOptOverload = msg->i_parseOptions(&bp->bp_vend.Dhcp.dhcp_opts[0],
                                           buflen - RT_OFFSETOF(RTNETBOOTP, bp_vend.Dhcp.dhcp_opts));
    if (fOptOverload < 0)
        return NULL;

    /* "The 'file' field MUST be interpreted next ..." */
    if (fOptOverload & RTNET_DHCP_OPTION_OVERLOAD_FILE)
    {
        int status = msg->i_parseOptions(bp->bp_file, sizeof(bp->bp_file));
        if (status != 0)
            return NULL;
    }
#if 0 /* not currently unused  */
    else if (bp->bp_file[0] != '\0')
    {
        /* must be zero terminated, ignore if not */
        const char *pszFile = (const char *)bp->bp_file;
        size_t len = RTStrNLen(pszFile, sizeof(bp->bp_file));
        if (len < sizeof(bp->bp_file))
            msg->m_file.assign(pszFile, len);
    }
#endif

    /* "... followed by the 'sname' field." */
    if (fOptOverload & RTNET_DHCP_OPTION_OVERLOAD_SNAME)
    {
        int status = msg->i_parseOptions(bp->bp_sname, sizeof(bp->bp_sname));
        if (status != 0) /* NB: this includes "nested" Option Overload */
            return NULL;
    }
#if 0 /* not currently unused  */
    else if (bp->bp_sname[0] != '\0')
    {
        /* must be zero terminated, ignore if not */
        const char *pszSName = (const char *)bp->bp_sname;
        size_t len = RTStrNLen(pszSName, sizeof(bp->bp_sname));
        if (len < sizeof(bp->bp_sname))
            msg->m_sname.assign(pszSName, len);
    }
#endif

    msg->m_optMessageType = OptMessageType(*msg);
    if (!msg->m_optMessageType.present())
        return NULL;

    msg->m_id = ClientId(msg->m_mac, OptClientId(*msg));

    return msg.release();
}


int DhcpClientMessage::i_parseOptions(const uint8_t *pbBuf, size_t cbBuf) RT_NOEXCEPT
{
    int fOptOverload = 0;
    while (cbBuf > 0)
    {
        uint8_t const bOpt = *pbBuf++;
        --cbBuf;

        if (bOpt == RTNET_DHCP_OPT_PAD)
            continue;

        if (bOpt == RTNET_DHCP_OPT_END)
            break;

        if (cbBuf == 0)
            DHCP_LOG_RET(-1, ("option %d has no length field\n", bOpt));

        uint8_t const cbOpt = *pbBuf++;
        --cbBuf;

        if (cbOpt > cbBuf)
            DHCP_LOG_RET(-1, ("option %d truncated (length %d, but only %zu bytes left)\n", bOpt, cbOpt, cbBuf));

#if 0
        rawopts_t::const_iterator it(m_optmap.find(bOpt));
        if (it != m_optmap.cend())
            return -1;
#endif
        if (bOpt == RTNET_DHCP_OPT_OPTION_OVERLOAD)
        {
            if (cbOpt != 1)
                DHCP_LOG_RET(-1, ("Overload Option (option %d) has invalid length %d\n", bOpt, cbOpt));

            fOptOverload = *pbBuf;

            if ((fOptOverload & ~RTNET_DHCP_OPTION_OVERLOAD_MASK) != 0)
                DHCP_LOG_RET(-1, ("Overload Option (option %d) has invalid value 0x%x\n", bOpt, fOptOverload));
        }
        else
            m_rawopts.insert(std::make_pair(bOpt, octets_t(pbBuf, pbBuf + cbOpt)));

        pbBuf += cbOpt;
        cbBuf -= cbOpt;
    }

    return fOptOverload;
}


/**
 * Dumps the message.
 */
void DhcpClientMessage::dump() const RT_NOEXCEPT
{
    DhcpMessage::dump();

    if (OptRapidCommit(*this).present())
        LogRel((" (rapid commit)"));

    try
    {
        const OptServerId sid(*this);
        if (sid.present())
            LogRel((" for server %RTnaipv4", sid.value().u));

        const OptClientId cid(*this);
        if (cid.present())
        {
            if (cid.value().size() > 0)
                LogRel((" client id: %.*Rhxs\n", cid.value().size(), &cid.value().front()));
            else
                LogRel((" client id: <empty>\n"));
        }

        const OptRequestedAddress reqAddr(*this);
        if (reqAddr.present())
            LogRel((" requested address %RTnaipv4", reqAddr.value().u));
        const OptLeaseTime reqLeaseTime(*this);
        if (reqLeaseTime.present())
            LogRel((" requested lease time %d", reqAddr.value()));
        if (reqAddr.present() || reqLeaseTime.present())
            LogRel(("\n"));

        const OptParameterRequest params(*this);
        if (params.present())
        {
            LogRel((" params {"));
            typedef OptParameterRequest::value_t::const_iterator it_t;
            for (it_t it = params.value().begin(); it != params.value().end(); ++it)
                LogRel((" %d", *it));
            LogRel((" }\n"));
        }
    }
    catch (std::bad_alloc &)
    {
        LogRel(("bad_alloc during dumping\n"));
    }

    for (rawopts_t::const_iterator it = m_rawopts.begin(); it != m_rawopts.end(); ++it)
    {
        const uint8_t optcode = (*it).first;
        switch (optcode)
        {
            case OptMessageType::optcode:      /* FALLTHROUGH */
            case OptClientId::optcode:         /* FALLTHROUGH */
            case OptRequestedAddress::optcode: /* FALLTHROUGH */
            case OptLeaseTime::optcode:        /* FALLTHROUGH */
            case OptParameterRequest::optcode: /* FALLTHROUGH */
            case OptRapidCommit::optcode:
                break;

            default:
            {
                size_t const   cbBytes = it->second.size();
                uint8_t const *pbBytes = &it->second.front();
                bool fAllPrintable = true;
                for (size_t off = 0; off < cbBytes; off++)
                    if (!RT_C_IS_PRINT((char )pbBytes[off]))
                    {
                        fAllPrintable = false;
                        break;
                    }
                if (fAllPrintable)
                    LogRel(("  %2d: '%.*s'\n", optcode, cbBytes, pbBytes));
                else
                    LogRel(("  %2d: %.*Rhxs\n", optcode, cbBytes, pbBytes));
            }
        }
    }
}



/*********************************************************************************************************************************
*   DhcpServerMessage Implementation                                                                                             *
*********************************************************************************************************************************/

DhcpServerMessage::DhcpServerMessage(const DhcpClientMessage &req, uint8_t messageTypeParam, RTNETADDRIPV4 serverAddr)
    : DhcpMessage()
    , m_optServerId(serverAddr)
{
    m_dst.u = 0xffffffff;       /* broadcast */

    m_optMessageType = OptMessageType(messageTypeParam);

    /* copy values from the request (cf. RFC2131 Table 3) */
    m_xid = req.xid();
    m_flags = req.flags();
    m_giaddr = req.giaddr();
    m_mac = req.mac();

    if (req.messageType() == RTNET_DHCP_MT_REQUEST)
        m_ciaddr = req.ciaddr();
}


void DhcpServerMessage::maybeUnicast(const DhcpClientMessage &req) RT_NOEXCEPT
{
    if (!req.broadcast() && req.ciaddr().u != 0)
        setDst(req.ciaddr());
}


/**
 * @throws  std::bad_alloc
 */
void DhcpServerMessage::addOption(DhcpOption *opt)
{
    m_optmap << opt;
}


/**
 * @throws  std::bad_alloc
 */
void DhcpServerMessage::addOptions(const optmap_t &optmap)
{
    for (optmap_t::const_iterator it = optmap.begin(); it != optmap.end(); ++it)
        m_optmap << it->second;
}


/**
 * @throws  std::bad_alloc
 */
int DhcpServerMessage::encode(octets_t &data)
{
    /*
     * Header, including DHCP cookie.
     */
    RTNETBOOTP bp;
    RT_ZERO(bp);

    bp.bp_op = RTNETBOOTP_OP_REPLY;
    bp.bp_htype = RTNET_ARP_ETHER;
    bp.bp_hlen = sizeof(RTMAC);

    bp.bp_xid = m_xid;

    bp.bp_ciaddr = m_ciaddr;
    bp.bp_yiaddr = m_yiaddr;
    bp.bp_siaddr = m_siaddr;
    bp.bp_giaddr = m_giaddr;

    bp.bp_chaddr.Mac = m_mac;

    bp.bp_vend.Dhcp.dhcp_cookie = RT_H2N_U32_C(RTNET_DHCP_COOKIE);

    data.insert(data.end(), (uint8_t *)&bp, (uint8_t *)&bp.bp_vend.Dhcp.dhcp_opts);

    /** @todo TFTP, bootfile name, etc. pick from extended options if no
     *        override in effect? */

    /*
     * Options
     */
    data << m_optServerId
         << m_optMessageType;

    for (optmap_t::const_iterator it = m_optmap.begin(); it != m_optmap.end(); ++it)
    {
        LogRel3(("encoding option %d (%s)\n", it->first, DhcpOption::name(it->first)));
        DhcpOption &opt = *it->second;
        data << opt;
    }

    data << OptEnd();

    AssertCompile(RTNET_DHCP_NORMAL_SIZE == 548);
    if (data.size() < RTNET_DHCP_NORMAL_SIZE)
        data.resize(RTNET_DHCP_NORMAL_SIZE);

    /** @todo dump it */
    if ((LogRelIs4Enabled() && LogRelIsEnabled()) || LogIsEnabled())
        dump();
    if ((LogRelIs5Enabled() && LogRelIsEnabled()) || LogIs5Enabled())
        LogRel5(("encoded message: %u bytes\n%.*Rhxd\n", data.size(), data.size(), &data.front()));

    return VINF_SUCCESS;
}


/**
 * Dumps a server message to the log.
 */
void DhcpServerMessage::dump() const RT_NOEXCEPT
{
    DhcpMessage::dump();

    /** @todo dump option details. */
}

