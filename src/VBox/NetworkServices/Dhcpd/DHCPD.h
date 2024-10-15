/* $Id: DHCPD.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef VBOX_INCLUDED_SRC_Dhcpd_DHCPD_h
#define VBOX_INCLUDED_SRC_Dhcpd_DHCPD_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DhcpdInternal.h"
#include <iprt/cpp/ministring.h>
#include "Config.h"
#include "DhcpMessage.h"
#include "Db.h"


/**
 * The core of the DHCP server.
 *
 * This class is feed DhcpClientMessages that VBoxNetDhcpd has picked up from
 * the network.  After processing a message it returns the appropriate response
 * (if any) which VBoxNetDhcpd sends out.
 */
class DHCPD
{
    /** The DHCP configuration. */
    const Config   *m_pConfig;
    /** The lease database. */
    Db              m_db;

public:
    DHCPD();

    int init(const Config *) RT_NOEXCEPT;

    DhcpServerMessage *process(const std::unique_ptr<DhcpClientMessage> &req) RT_NOEXCEPT
    {
        if (req.get() != NULL)
            return process(*req.get());
        return NULL;
    }

    DhcpServerMessage *process(DhcpClientMessage &req) RT_NOEXCEPT;

private:
    /** @name DHCP message processing methods
     * @{ */
    DhcpServerMessage *i_doDiscover(const DhcpClientMessage &req);
    DhcpServerMessage *i_doRequest(const DhcpClientMessage &req);
    DhcpServerMessage *i_doInform(const DhcpClientMessage &req);
    DhcpServerMessage *i_doDecline(const DhcpClientMessage &req) RT_NOEXCEPT;
    DhcpServerMessage *i_doRelease(const DhcpClientMessage &req) RT_NOEXCEPT;

    DhcpServerMessage *i_createMessage(int type, const DhcpClientMessage &req);
    /** @} */

    /** @name Lease database handling
     * @{ */
    int                i_loadLeases() RT_NOEXCEPT;
    void               i_saveLeases() RT_NOEXCEPT;
    /** @} */
};

#endif /* !VBOX_INCLUDED_SRC_Dhcpd_DHCPD_h */
