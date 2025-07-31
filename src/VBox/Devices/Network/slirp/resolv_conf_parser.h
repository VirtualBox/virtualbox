/* $Id: resolv_conf_parser.h 110275 2025-07-16 18:34:13Z jack.doherty@oracle.com $ */
/** @file
 * resolv_conf_parser.h - interface to parser of resolv.conf resolver(5)
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef __RESOLV_CONF_PARSER_H__
#define __RESOLV_CONF_PARSER_H__

#include <iprt/cdefs.h>
#include <iprt/net.h>

RT_C_DECLS_BEGIN

#define RCPS_MAX_NAMESERVERS 3
#define RCPS_MAX_SEARCHLIST 10
#define RCPS_BUFFER_SIZE 256
#define RCPS_IPVX_SIZE 47

/**
 * RESOLV_CONF_FILE can be defined in external tests for verification of Slirp behaviour.
 */
#ifndef RESOLV_CONF_FILE
# ifndef RT_OS_OS2
#  define RESOLV_CONF_FILE "/etc/resolv.conf"
# else
#  define RESOLV_CONF_FILE "\\MPTN\\ETC\\RESOLV2"
# endif
#endif

/**
 * In Slirp we don't need IPv6 for general case (only for dnsproxy mode
 * it's potentially acceptable)
 */
#define RCPSF_IGNORE_IPV6 RT_BIT(0)

/**
 * This flag used to request just the strings in rcps_str_nameserver,
 * but no addresses in rcps_nameserver.  This is not very useful,
 * since we need to validate addresses anyway.  This flag is ignored
 * now.
 */
#define RCPSF_NO_STR2IPCONV RT_BIT(1)


struct rcp_state
{
    uint16_t rcps_port;
    /**
     * Filling of this array ommited iff RCPSF_NO_STR2IPCONF in rcp_state::rcps_flags set.
     */
    RTNETADDR rcps_nameserver[RCPS_MAX_NAMESERVERS];
    /**
     * this array contains non-NULL (pointing to rcp_state::rcps_nameserver_str_buffer) iff
     * RCPSF_NO_STR2IPCONF in rcp_state::rcps_flags set.
     */
    char *rcps_str_nameserver[RCPS_MAX_NAMESERVERS];
    unsigned rcps_num_nameserver;
    /**
     * Shortcuts to storage, note that domain is optional
     * and if it's missed in resolv.conf rcps_domain should be equal
     * to rcps_search_list[0]
     */
    char *rcps_domain;
    char *rcps_searchlist[RCPS_MAX_SEARCHLIST];
    unsigned rcps_num_searchlist;

    uint32_t rcps_flags;

    char rcps_domain_buffer[RCPS_BUFFER_SIZE];
    char rcps_searchlist_buffer[RCPS_BUFFER_SIZE];
    char rcps_nameserver_str_buffer[RCPS_MAX_NAMESERVERS * RCPS_IPVX_SIZE];
};


/**
 *  This function parses specified file (expected to conform resolver (5) Mac OSX or resolv.conf (3) Linux)
 *  and fills the structure.
 * @return 0 - on success
 *         -1 - on fail.
 *   <code>
 *   struct rcp_state state;
 *   int rc;
 *
 *   rc = rcp_parse(&state, "/etc/resolv.conf");
 *   for(i = 0; rc == 0 && i != state.rcps_num_nameserver; ++i)
 *   {
 *      if ((state.rcps_flags & RCPSF_NO_STR2IPCONV) == 0)
 *      {
 *          const RTNETADDR *addr = &state.rcps_nameserver[i];
 *
 *          switch (state.rcps_nameserver[i].enmType)
 *          {
 *              case RTNETADDRTYPE_IPV4:
 *                  RTPrintf("nameserver[%d]: [%RTnaipv4]:%d\n", i, addr->uAddr.IPv4, addr->uPort);
 *                  break;
 *              case RTNETADDRTYPE_IPV6:
 *                  RTPrintf("nameserver[%d]: [%RTnaipv6]:%d\n", i, &addr->uAddr.IPv6, addr->uPort);
 *                  break;
 *              default:
 *                   break;
 *          }
 *      }
 *      else
 *          RTPrintf("nameserver[%d]: %s\n", i, state.rcps_str_nameserver[i]);
 *  }
 *   </code>
 *
 */
int rcp_parse(struct rcp_state *, const char *);

RT_C_DECLS_END

#endif
