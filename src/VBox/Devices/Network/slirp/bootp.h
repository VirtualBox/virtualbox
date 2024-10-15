/* $Id: bootp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - BOOTP/DHCP server emulation (declarations/defines).
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/* bootp/dhcp defines */

#define BOOTP_SERVER    67
#define BOOTP_CLIENT    68

#define BOOTP_REQUEST   1
#define BOOTP_REPLY     2

#define RFC1533_COOKIE          99, 130, 83, 99
#define RFC1533_PAD             0
#define RFC1533_NETMASK         1
#define RFC1533_TIMEOFFSET      2
#define RFC1533_GATEWAY         3
#define RFC1533_TIMESERVER      4
#define RFC1533_IEN116NS        5
#define RFC1533_DNS             6
#define RFC1533_LOGSERVER       7
#define RFC1533_COOKIESERVER    8
#define RFC1533_LPRSERVER       9
#define RFC1533_IMPRESSSERVER   10
#define RFC1533_RESOURCESERVER  11
#define RFC1533_HOSTNAME        12
#define RFC1533_BOOTFILESIZE    13
#define RFC1533_MERITDUMPFILE   14
#define RFC1533_DOMAINNAME      15
#define RFC1533_SWAPSERVER      16
#define RFC1533_ROOTPATH        17
#define RFC1533_EXTENSIONPATH   18
#define RFC1533_IPFORWARDING    19
#define RFC1533_IPSOURCEROUTING 20
#define RFC1533_IPPOLICYFILTER  21
#define RFC1533_IPMAXREASSEMBLY 22
#define RFC1533_IPTTL           23
#define RFC1533_IPMTU           24
#define RFC1533_IPMTUPLATEAU    25
#define RFC1533_INTMTU          26
#define RFC1533_INTLOCALSUBNETS 27
#define RFC1533_INTBROADCAST    28
#define RFC1533_INTICMPDISCOVER 29
#define RFC1533_INTICMPRESPOND  30
#define RFC1533_INTROUTEDISCOVER 31
#define RFC1533_INTROUTESOLICIT 32
#define RFC1533_INTSTATICROUTES 33
#define RFC1533_LLTRAILERENCAP  34
#define RFC1533_LLARPCACHETMO   35
#define RFC1533_LLETHERNETENCAP 36
#define RFC1533_TCPTTL          37
#define RFC1533_TCPKEEPALIVETMO 38
#define RFC1533_TCPKEEPALIVEGB  39
#define RFC1533_NISDOMAIN       40
#define RFC1533_NISSERVER       41
#define RFC1533_NTPSERVER       42
#define RFC1533_VENDOR          43
#define RFC1533_NBNS            44
#define RFC1533_NBDD            45
#define RFC1533_NBNT            46
#define RFC1533_NBSCOPE         47
#define RFC1533_XFS             48
#define RFC1533_XDM             49

#define RFC2132_REQ_ADDR        50
#define RFC2132_LEASE_TIME      51
#define RFC2132_MSG_TYPE        53
#define RFC2132_SRV_ID          54
#define RFC2132_PARAM_LIST      55
#define RFC2132_MAX_SIZE        57
#define RFC2132_RENEWAL_TIME    58
#define RFC2132_REBIND_TIME     59

#define DHCPDISCOVER            1
#define DHCPOFFER               2
#define DHCPREQUEST             3
#define DHCPDECLINE             4
#define DHCPACK                 5
#define DHCPNAK                 6
#define DHCPRELEASE             7
#define DHCPINFORM              8

#define RFC1533_VENDOR_MAJOR    0
#define RFC1533_VENDOR_MINOR    0

#define RFC1533_VENDOR_MAGIC    128
#define RFC1533_VENDOR_ADDPARM  129
#define RFC1533_VENDOR_ETHDEV   130
#define RFC1533_VENDOR_HOWTO    132
#define RFC1533_VENDOR_MNUOPTS  160
#define RFC1533_VENDOR_SELECTION 176
#define RFC1533_VENDOR_MOTD     184
#define RFC1533_VENDOR_NUMOFMOTD 8
#define RFC1533_VENDOR_IMG      192
#define RFC1533_VENDOR_NUMOFIMG 16

#define RFC1533_END             255
#define BOOTP_VENDOR_LEN        64
#define DHCP_OPT_LEN            312

/* RFC 2131 */
struct bootp_t
{
    struct ip      ip;                          /**< header: IP header */
    struct udphdr  udp;                         /**< header: UDP header */
    uint8_t        bp_op;                       /**< opcode (BOOTP_REQUEST, BOOTP_REPLY) */
    uint8_t        bp_htype;                    /**< hardware type */
    uint8_t        bp_hlen;                     /**< hardware address length */
    uint8_t        bp_hops;                     /**< hop count */
    uint32_t       bp_xid;                      /**< transaction ID */
    uint16_t       bp_secs;                     /**< numnber of seconds */
    uint16_t       bp_flags;                    /**< flags (DHCP_FLAGS_B) */
    struct in_addr bp_ciaddr;                   /**< client IP address */
    struct in_addr bp_yiaddr;                   /**< your IP address */
    struct in_addr bp_siaddr;                   /**< server IP address */
    struct in_addr bp_giaddr;                   /**< gateway IP address */
    uint8_t        bp_hwaddr[16];               /** client hardware address */
    uint8_t        bp_sname[64];                /** server host name */
    uint8_t        bp_file[128];                /** boot filename */
    uint8_t        bp_vend[DHCP_OPT_LEN];       /**< vendor specific info */
};


#define DHCP_FLAGS_B (1<<15)                    /**< B, broadcast */
struct bootp_ext
{
    uint8_t bpe_tag;
    uint8_t bpe_len;
};

void bootp_input(PNATState, struct mbuf *m);
int bootp_cache_lookup_ip_by_ether(PNATState, const uint8_t *, uint32_t *);
int bootp_cache_lookup_ether_by_ip(PNATState, uint32_t, uint8_t *);
int bootp_dhcp_init(PNATState);
int bootp_dhcp_fini(PNATState);
