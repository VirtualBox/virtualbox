/* $Id: pxtcp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - TCP proxy, internal interface declarations.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_NAT_pxtcp_h
#define VBOX_INCLUDED_SRC_NAT_pxtcp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "lwip/err.h"
#include "lwip/ip_addr.h"

struct pbuf;
struct tcp_pcb;
struct pxtcp;
struct fwspec;

err_t pxtcp_pcb_accept_outbound(struct tcp_pcb *, struct pbuf *, int, ipX_addr_t *, u16_t);

struct pxtcp *pxtcp_create_forwarded(SOCKET);
void pxtcp_cancel_forwarded(struct pxtcp *);

void pxtcp_pcb_connect(struct pxtcp *, const struct fwspec *);

int pxtcp_pmgr_add(struct pxtcp *);
void pxtcp_pmgr_del(struct pxtcp *);

#endif /* !VBOX_INCLUDED_SRC_NAT_pxtcp_h */
