/* $Id: libslirp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - slirp interface.
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

#ifndef _LIBSLIRP_H
#define _LIBSLIRP_H

#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
# ifdef __cplusplus
extern "C" {
# endif
int inet_aton(const char *cp, struct in_addr *ia);
# ifdef __cplusplus
}
# endif
#else
# ifdef RT_OS_OS2 /* temporary workaround, see ticket #127 */
#  include <sys/time.h>
# endif
# include <sys/select.h>
# include <poll.h>
# include <arpa/inet.h>
#endif

#include <VBox/types.h>
#include <iprt/req.h>

typedef struct NATState *PNATState;
struct mbuf;

#ifdef __cplusplus
extern "C" {
#endif

int slirp_init(PNATState *, uint32_t, uint32_t, bool, bool, int, int, bool, void *);
void slirp_register_statistics(PNATState pData, PPDMDRVINS pDrvIns);
void slirp_deregister_statistics(PNATState pData, PPDMDRVINS pDrvIns);
void slirp_term(PNATState);
void slirp_link_up(PNATState);
void slirp_link_down(PNATState);

#if defined(RT_OS_WINDOWS)
void slirp_select_fill(PNATState pData, int *pndfs);

void slirp_select_poll(PNATState pData, int fTimeout);
#else /* RT_OS_WINDOWS */
void slirp_select_fill(PNATState pData, int *pnfds, struct pollfd *polls);
void slirp_select_poll(PNATState pData, struct pollfd *polls, int ndfs);
#endif /* !RT_OS_WINDOWS */

void slirp_input(PNATState pData, struct mbuf *m, size_t cbBuf);

/* you must provide the following functions: */
void slirp_arm_fast_timer(void *pvUser);
int slirp_can_output(void * pvUser);
void slirp_output(void * pvUser, struct mbuf *m, const uint8_t *pkt, int pkt_len);
void slirp_output_pending(void * pvUser);
void slirp_urg_output(void *pvUser, struct mbuf *, const uint8_t *pu8Buf, int cb);
void slirp_post_sent(PNATState pData, void *pvArg);

int slirp_call(void *pvUser, PRTREQ *ppReq, RTMSINTERVAL cMillies,
               unsigned fFlags, PFNRT pfnFunction, unsigned cArgs, ...) RT_IPRT_CALLREQ_ATTR(5, 6, 7);

int slirp_call_hostres(void *pvUser, PRTREQ *ppReq, RTMSINTERVAL cMillies,
                       unsigned fFlags, PFNRT pfnFunction, unsigned cArgs, ...) RT_IPRT_CALLREQ_ATTR(5, 6, 7);


void slirp_update_guest_addr_guess(PNATState pData, uint32_t guess, const char *msg);

int slirp_add_redirect(PNATState pData, int is_udp, struct in_addr host_addr,
                int host_port, struct in_addr guest_addr,
                int guest_port);
int slirp_remove_redirect(PNATState pData, int is_udp, struct in_addr host_addr,
                int host_port, struct in_addr guest_addr,
                int guest_port);
int slirp_add_exec(PNATState pData, int do_pty, const char *args, int addr_low_byte,
                   int guest_port);

void slirp_set_dhcp_TFTP_prefix(PNATState pData, const char *tftpPrefix);
void slirp_set_dhcp_TFTP_bootfile(PNATState pData, const char *bootFile);
void slirp_set_dhcp_next_server(PNATState pData, const char *nextServer);
void slirp_set_dhcp_dns_proxy(PNATState pData, bool fDNSProxy);
void slirp_set_rcvbuf(PNATState pData, int kilobytes);
void slirp_set_sndbuf(PNATState pData, int kilobytes);
void slirp_set_tcp_rcvspace(PNATState pData, int kilobytes);
void slirp_set_tcp_sndspace(PNATState pData, int kilobytes);

int  slirp_set_binding_address(PNATState, char *addr);
void slirp_set_mtu(PNATState, int);
void slirp_info(PNATState pData, const void *pvArg, const char *pszArgs);
void slirp_set_somaxconn(PNATState pData, int iSoMaxConn);

/**
 * This macrodefinition is shortcut for check of hosts where Slirp,
 * receives notifications from host. For now it's Darwin only. But
 * Main API has primitives for listening DNS change event since 4.3.
 */
#if defined(RT_OS_DARWIN) || defined(RT_OS_WINDOWS)
# define HAVE_NOTIFICATION_FOR_DNS_UPDATE 1
#else
# define HAVE_NOTIFICATION_FOR_DNS_UPDATE 0
#endif


/**
 * This method help DrvNAT to select strategy: about VMRESUMEREASON_HOST_RESUME:
 * - proceed with link termination (we let guest track host DNS settings)
 *    VBOX_NAT_DNS_EXTERNAL
 * - enforce internal DNS update (we are using dnsproxy and track but don't export DNS host settings)
 *    VBOX_NAT_DNS_DNSPROXY
 * - ignore (NAT configured to use hostresolver - we aren't track any host DNS changes)
 *    VBOX_NAT_DNS_HOSTRESOLVER
 * @note: It's safe to call this method from any thread, because settings we're checking
 * are immutable at runtime.
 */
#define VBOX_NAT_DNS_EXTERNAL 0
#define VBOX_NAT_DNS_DNSPROXY 1
#define VBOX_NAT_DNS_HOSTRESOLVER 2
int slirp_host_network_configuration_change_strategy_selector(const PNATState);
#if defined(RT_OS_WINDOWS)


/*
 * ICMP handle state change
 */
# define VBOX_ICMP_EVENT_INDEX           0

/**
 * This event is for
 *  - slirp_input
 *  - slirp_link_up
 *  - slirp_link_down
 *  - wakeup
 */
# define VBOX_WAKEUP_EVENT_INDEX       1

/*
 * UDP/TCP socket state change (socket ready to receive, to send, ...)
 */
# define VBOX_SOCKET_EVENT_INDEX       2

/*
 * The number of events for WSAWaitForMultipleEvents().
 */
# define VBOX_EVENT_COUNT              3

HANDLE *slirp_get_events(PNATState pData);
void slirp_register_external_event(PNATState pData, HANDLE hEvent, int index);
#endif /* RT_OS_WINDOWS */

struct mbuf *slirp_ext_m_get(PNATState pData, size_t cbMin, void **ppvBuf, size_t *pcbBuf);
void slirp_ext_m_free(PNATState pData, struct mbuf *, uint8_t *pu8Buf);

/*
 * Returns the timeout.
 */
unsigned int slirp_get_timeout_ms(PNATState pData);

# ifndef RT_OS_WINDOWS
/*
 * Returns the number of sockets.
 */
int slirp_get_nsock(PNATState pData);
# endif

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
void slirp_add_host_resolver_mapping(PNATState pData,
                                     const char *pszHostName, bool fPattern,
                                     uint32_t u32HostIP);
#endif

#ifdef __cplusplus
}
#endif

#endif
