/* $Id: slirp.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - slirp glue.
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

/*
 * This code is based on:
 *
 * libslirp glue
 *
 * Copyright (c) 2004-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "slirp.h"
#ifdef RT_OS_OS2
# include <paths.h>
#endif

#include <iprt/errcore.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmdrv.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/path.h>
#ifndef RT_OS_WINDOWS
# include <sys/ioctl.h>
# include <poll.h>
# include <netinet/in.h>
#else
# include <Winnls.h>
# define _WINSOCK2API_
# include <iprt/win/iphlpapi.h>
#endif
#include <alias.h>

#ifndef RT_OS_WINDOWS
/**
 * XXX: It shouldn't be non-Windows specific.
 * resolv_conf_parser.h client's structure isn't OS specific, it's just need to be generalized a
 * a bit to replace slirp_state.h DNS server (domain) lists with rcp_state like structure.
 */
# include "resolv_conf_parser.h"
#endif

#ifndef RT_OS_WINDOWS
# define DO_ENGAGE_EVENT1(so, fdset, label)                        \
   do {                                                            \
       if (   so->so_poll_index != -1                              \
           && so->s == polls[so->so_poll_index].fd)                \
       {                                                           \
           polls[so->so_poll_index].events |= N_(fdset ## _poll);  \
           break;                                                  \
       }                                                           \
       AssertRelease(poll_index < (nfds));                         \
       AssertRelease(poll_index >= 0 && poll_index < (nfds));      \
       polls[poll_index].fd = (so)->s;                             \
       (so)->so_poll_index = poll_index;                           \
       polls[poll_index].events = N_(fdset ## _poll);              \
       polls[poll_index].revents = 0;                              \
       poll_index++;                                               \
   } while (0)

# define DO_ENGAGE_EVENT2(so, fdset1, fdset2, label)               \
   do {                                                            \
       if (   so->so_poll_index != -1                              \
           && so->s == polls[so->so_poll_index].fd)                \
       {                                                           \
           polls[so->so_poll_index].events |=                      \
               N_(fdset1 ## _poll) | N_(fdset2 ## _poll);          \
           break;                                                  \
       }                                                           \
       AssertRelease(poll_index < (nfds));                         \
       polls[poll_index].fd = (so)->s;                             \
       (so)->so_poll_index = poll_index;                           \
       polls[poll_index].events =                                  \
           N_(fdset1 ## _poll) | N_(fdset2 ## _poll);              \
       poll_index++;                                               \
   } while (0)

# define DO_POLL_EVENTS(rc, error, so, events, label) do {} while (0)

/*
 * DO_CHECK_FD_SET is used in dumping events on socket, including POLLNVAL.
 * gcc warns about attempts to log POLLNVAL so construction in a last to lines
 * used to catch POLLNVAL while logging and return false in case of error while
 * normal usage.
 */
#  define DO_CHECK_FD_SET(so, events, fdset)                        \
      (   ((so)->so_poll_index != -1)                               \
       && ((so)->so_poll_index <= ndfs)                             \
       && ((so)->s == polls[so->so_poll_index].fd)                  \
       && (polls[(so)->so_poll_index].revents & N_(fdset ## _poll)) \
       && (   N_(fdset ## _poll) == POLLNVAL                        \
           || !(polls[(so)->so_poll_index].revents & POLLNVAL)))

  /* specific for Windows Winsock API */
# define DO_WIN_CHECK_FD_SET(so, events, fdset) 0

# ifndef RT_OS_LINUX
#  define readfds_poll   (POLLRDNORM)
#  define writefds_poll  (POLLWRNORM)
# else
#  define readfds_poll   (POLLIN)
#  define writefds_poll  (POLLOUT)
# endif
# define xfds_poll       (POLLPRI)
# define closefds_poll   (POLLHUP)
# define rderr_poll      (POLLERR)
# if 0 /* unused yet */
#  define rdhup_poll      (POLLHUP)
#  define nval_poll       (POLLNVAL)
# endif

# define ICMP_ENGAGE_EVENT(so, fdset)              \
   do {                                            \
       if (pData->icmp_socket.s != -1)             \
           DO_ENGAGE_EVENT1((so), fdset, ICMP);    \
   } while (0)

#else /* RT_OS_WINDOWS */

/*
 * On Windows, we will be notified by IcmpSendEcho2() when the response arrives.
 * So no call to WSAEventSelect necessary.
 */
# define ICMP_ENGAGE_EVENT(so, fdset)                do {} while (0)

/*
 * On Windows we use FD_ALL_EVENTS to ensure that we don't miss any event.
 */
# define DO_ENGAGE_EVENT1(so, fdset1, label)                                                    \
    do {                                                                                        \
        rc = WSAEventSelect((so)->s, VBOX_SOCKET_EVENT, FD_ALL_EVENTS);                         \
        if (rc == SOCKET_ERROR)                                                                 \
        {                                                                                       \
            /* This should not happen */                                                        \
            error = WSAGetLastError();                                                          \
            LogRel(("WSAEventSelect (" #label ") error %d (so=%x, socket=%s, event=%x)\n",      \
                        error, (so), (so)->s, VBOX_SOCKET_EVENT));                              \
        }                                                                                       \
    } while (0);                                                                                \
    CONTINUE(label)

# define DO_ENGAGE_EVENT2(so, fdset1, fdset2, label) \
    DO_ENGAGE_EVENT1((so), (fdset1), label)

# define DO_POLL_EVENTS(rc, error, so, events, label)                                           \
    (rc) = WSAEnumNetworkEvents((so)->s, VBOX_SOCKET_EVENT, (events));                          \
    if ((rc) == SOCKET_ERROR)                                                                   \
    {                                                                                           \
        (error) = WSAGetLastError();                                                            \
        LogRel(("WSAEnumNetworkEvents %R[natsock] " #label " error %d\n", (so), (error)));      \
        LogFunc(("WSAEnumNetworkEvents %R[natsock] " #label " error %d\n", (so), (error)));     \
        CONTINUE(label);                                                                        \
    }

# define acceptds_win     FD_ACCEPT
# define acceptds_win_bit FD_ACCEPT_BIT
# define readfds_win      FD_READ
# define readfds_win_bit  FD_READ_BIT
# define writefds_win     FD_WRITE
# define writefds_win_bit FD_WRITE_BIT
# define xfds_win         FD_OOB
# define xfds_win_bit     FD_OOB_BIT
# define closefds_win     FD_CLOSE
# define closefds_win_bit FD_CLOSE_BIT
# define connectfds_win     FD_CONNECT
# define connectfds_win_bit FD_CONNECT_BIT

# define closefds_win FD_CLOSE
# define closefds_win_bit FD_CLOSE_BIT

# define DO_CHECK_FD_SET(so, events, fdset)  \
    ((events).lNetworkEvents & fdset ## _win)

# define DO_WIN_CHECK_FD_SET(so, events, fdset) DO_CHECK_FD_SET((so), (events), fdset)
# define DO_UNIX_CHECK_FD_SET(so, events, fdset) 1 /*specific for Unix API */

#endif /* RT_OS_WINDOWS */

#define TCP_ENGAGE_EVENT1(so, fdset) \
    DO_ENGAGE_EVENT1((so), fdset, tcp)

#define TCP_ENGAGE_EVENT2(so, fdset1, fdset2) \
    DO_ENGAGE_EVENT2((so), fdset1, fdset2, tcp)

#ifdef RT_OS_WINDOWS
# define WIN_TCP_ENGAGE_EVENT2(so, fdset, fdset2) TCP_ENGAGE_EVENT2(so, fdset1, fdset2)
#endif

#define UDP_ENGAGE_EVENT(so, fdset) \
    DO_ENGAGE_EVENT1((so), fdset, udp)

#define POLL_TCP_EVENTS(rc, error, so, events) \
    DO_POLL_EVENTS((rc), (error), (so), (events), tcp)

#define POLL_UDP_EVENTS(rc, error, so, events) \
    DO_POLL_EVENTS((rc), (error), (so), (events), udp)

#define CHECK_FD_SET(so, events, set) \
    (DO_CHECK_FD_SET((so), (events), set))

#define WIN_CHECK_FD_SET(so, events, set) \
    (DO_WIN_CHECK_FD_SET((so), (events), set))

/*
 * Loging macros
 */
#ifdef VBOX_WITH_DEBUG_NAT_SOCKETS
# if defined(RT_OS_WINDOWS)
#  define  DO_LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset)             \
   do {                                                                                \
       LogRel(("  " #proto " %R[natsock] %R[natwinnetevents]\n", (so), (winevent)));   \
   } while (0)
# else /* !RT_OS_WINDOWS */
#  define  DO_LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset)         \
   do {                                                                            \
           LogRel(("  " #proto " %R[natsock] %s %s %s er: %s, %s, %s\n", (so),     \
                    CHECK_FD_SET(so, ign ,r_fdset) ? "READ":"",                    \
                    CHECK_FD_SET(so, ign, w_fdset) ? "WRITE":"",                   \
                    CHECK_FD_SET(so, ign, x_fdset) ? "OOB":"",                     \
                    CHECK_FD_SET(so, ign, rderr) ? "RDERR":"",                     \
                    CHECK_FD_SET(so, ign, rdhup) ? "RDHUP":"",                     \
                    CHECK_FD_SET(so, ign, nval) ? "RDNVAL":""));                   \
   } while (0)
# endif /* !RT_OS_WINDOWS */
#else  /* !VBOX_WITH_DEBUG_NAT_SOCKETS */
# define DO_LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset) do {} while (0)
#endif /* !VBOX_WITH_DEBUG_NAT_SOCKETS */

#define LOG_NAT_SOCK(so, proto, winevent, r_fdset, w_fdset, x_fdset) \
    DO_LOG_NAT_SOCK((so), proto, (winevent), r_fdset, w_fdset, x_fdset)

static const uint8_t special_ethaddr[6] =
{
    0x52, 0x54, 0x00, 0x12, 0x35, 0x00
};

static const uint8_t broadcast_ethaddr[6] =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const uint8_t zerro_ethaddr[6] =
{
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};

/**
 * This helper routine do the checks in descriptions to
 * ''fUnderPolling'' and ''fShouldBeRemoved'' flags
 * @returns 1 if socket removed and 0 if no changes was made.
 */
static int slirpVerifyAndFreeSocket(PNATState pData, struct socket *pSocket)
{
    AssertPtrReturn(pData, 0);
    AssertPtrReturn(pSocket, 0);
    AssertReturn(pSocket->fUnderPolling, 0);
    if (pSocket->fShouldBeRemoved)
    {
        pSocket->fUnderPolling = 0;
        sofree(pData, pSocket);
        /* pSocket is PHANTOM, now */
        return 1;
    }
    return 0;
}

int slirp_init(PNATState *ppData, uint32_t u32NetAddr, uint32_t u32Netmask,
               bool fPassDomain, bool fUseHostResolver, int i32AliasMode,
               int iIcmpCacheLimit, bool fLocalhostReachable, void *pvUser)
{
    int rc;
    PNATState pData;
    if (u32Netmask & 0x1f)
    {
        /* CTL is x.x.x.15, bootp passes up to 16 IPs (15..31) */
        LogRel(("NAT: The last 5 bits of the netmask (%RTnaipv4) need to be unset\n", RT_BE2H_U32(u32Netmask)));
        return VERR_INVALID_PARAMETER;
    }
    pData = RTMemAllocZ(RT_ALIGN_Z(sizeof(NATState), sizeof(uint64_t)));
    *ppData = pData;
    if (!pData)
        return VERR_NO_MEMORY;
    pData->fPassDomain = !fUseHostResolver ? fPassDomain : false;
    pData->fUseHostResolver = fUseHostResolver;
    pData->fUseHostResolverPermanent = fUseHostResolver;
    pData->fLocalhostReachable = fLocalhostReachable;
    pData->pvUser = pvUser;
    pData->netmask = u32Netmask;

    rc = RTCritSectRwInit(&pData->CsRwHandlerChain);
    if (RT_FAILURE(rc))
        return rc;

    /* sockets & TCP defaults */
    pData->socket_rcv = 64 * _1K;
    pData->socket_snd = 64 * _1K;
    tcp_sndspace = 64 * _1K;
    tcp_rcvspace = 64 * _1K;

    /*
     * Use the same default here as in DevNAT.cpp (SoMaxConnection CFGM value)
     * to avoid release log noise.
     */
    pData->soMaxConn = 10;

#ifdef RT_OS_WINDOWS
    {
        WSADATA Data;
        RTLDRMOD hLdrMod;

        WSAStartup(MAKEWORD(2, 0), &Data);

        rc = RTLdrLoadSystem("Iphlpapi.dll", true /*fNoUnload*/, &hLdrMod);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(hLdrMod, "GetAdaptersAddresses", (void **)&pData->pfnGetAdaptersAddresses);
            if (RT_FAILURE(rc))
                LogRel(("NAT: Can't find GetAdapterAddresses in Iphlpapi.dll\n"));

            RTLdrClose(hLdrMod);
        }
    }
    pData->phEvents[VBOX_SOCKET_EVENT_INDEX] = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif

    rc = bootp_dhcp_init(pData);
    if (RT_FAILURE(rc))
    {
        Log(("NAT: DHCP server initialization failed\n"));
        RTMemFree(pData);
        *ppData = NULL;
        return rc;
    }
    debug_init(pData);
    if_init(pData);
    ip_init(pData);
    icmp_init(pData, iIcmpCacheLimit);

    /* Initialise mbufs *after* setting the MTU */
    mbuf_init(pData);

    pData->special_addr.s_addr = u32NetAddr;
    pData->slirp_ethaddr = &special_ethaddr[0];
    alias_addr.s_addr = pData->special_addr.s_addr | RT_H2N_U32_C(CTL_ALIAS);
    /** @todo add ability to configure this staff */

    /*
     * Some guests won't reacquire DHCP lease on link flap when VM is
     * restored.  Instead of forcing users to explicitly set CTL_GUEST
     * in port-forwarding rules, provide it as initial guess here.
     */
    slirp_update_guest_addr_guess(pData,
                                  pData->special_addr.s_addr | RT_H2N_U32_C(CTL_GUEST),
                                  "initialization");

    /* set default addresses */
    inet_aton("127.0.0.1", &loopback_addr);

    rc = slirpTftpInit(pData);
    AssertRCReturn(rc, rc);

    if (i32AliasMode & ~(PKT_ALIAS_LOG|PKT_ALIAS_SAME_PORTS|PKT_ALIAS_PROXY_ONLY))
    {
        LogRel(("NAT: bad alias mode 0x%x ignored\n", i32AliasMode));
        i32AliasMode = 0;
    }
    else if (i32AliasMode != 0)
    {
        LogRel(("NAT: alias mode 0x%x\n", i32AliasMode));
    }

    pData->i32AliasMode = i32AliasMode;
    getouraddr(pData);
    {
        int flags = 0;
        struct in_addr proxy_addr;
        pData->proxy_alias = LibAliasInit(pData, NULL);
        if (pData->proxy_alias == NULL)
        {
            Log(("NAT: LibAlias default rule wasn't initialized\n"));
            AssertMsgFailed(("NAT: LibAlias default rule wasn't initialized\n"));
        }
        flags = LibAliasSetMode(pData->proxy_alias, 0, 0);
#ifndef NO_FW_PUNCH
        flags |= PKT_ALIAS_PUNCH_FW;
#endif
        flags |= pData->i32AliasMode; /* do transparent proxying */
        flags = LibAliasSetMode(pData->proxy_alias, flags, ~0U);
        proxy_addr.s_addr = RT_H2N_U32(RT_N2H_U32(pData->special_addr.s_addr) | CTL_ALIAS);
        LibAliasSetAddress(pData->proxy_alias, proxy_addr);
        ftp_alias_load(pData);
        nbt_alias_load(pData);
    }
#ifdef VBOX_WITH_NAT_SEND2HOME
    /** @todo we should know all interfaces available on host. */
    pData->pInSockAddrHomeAddress = RTMemAllocZ(sizeof(struct sockaddr));
    pData->cInHomeAddressSize = 1;
    inet_aton("192.168.1.25", &pData->pInSockAddrHomeAddress[0].sin_addr);
    pData->pInSockAddrHomeAddress[0].sin_family = AF_INET;
# ifdef RT_OS_DARWIN
    pData->pInSockAddrHomeAddress[0].sin_len = sizeof(struct sockaddr_in);
# endif
#endif

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
    STAILQ_INIT(&pData->DNSMapNames);
    STAILQ_INIT(&pData->DNSMapPatterns);
#endif

    slirp_link_up(pData);
    return VINF_SUCCESS;
}

/**
 * Register statistics.
 */
void slirp_register_statistics(PNATState pData, PPDMDRVINS pDrvIns)
{
#ifdef VBOX_WITH_STATISTICS
# define PROFILE_COUNTER(name, dsc)     REGISTER_COUNTER(name, pData, STAMTYPE_PROFILE, STAMUNIT_TICKS_PER_CALL, dsc)
# define COUNTING_COUNTER(name, dsc)    REGISTER_COUNTER(name, pData, STAMTYPE_COUNTER, STAMUNIT_COUNT,          dsc)
# include "counters.h"
# undef COUNTER
/** @todo register statistics for the variables dumped by:
 *  ipstats(pData); tcpstats(pData); udpstats(pData); icmpstats(pData);
 *  mbufstats(pData); sockstats(pData); */
#else /* VBOX_WITH_STATISTICS */
    NOREF(pData);
    NOREF(pDrvIns);
#endif /* !VBOX_WITH_STATISTICS */
}

/**
 * Deregister statistics.
 */
void slirp_deregister_statistics(PNATState pData, PPDMDRVINS pDrvIns)
{
    if (pData == NULL)
        return;
#ifdef VBOX_WITH_STATISTICS
# define PROFILE_COUNTER(name, dsc)     DEREGISTER_COUNTER(name, pData)
# define COUNTING_COUNTER(name, dsc)    DEREGISTER_COUNTER(name, pData)
# include "counters.h"
#else /* VBOX_WITH_STATISTICS */
    NOREF(pData);
    NOREF(pDrvIns);
#endif /* !VBOX_WITH_STATISTICS */
}

/**
 * Marks the link as up, making it possible to establish new connections.
 */
void slirp_link_up(PNATState pData)
{
    if (link_up == 1)
        return;

    link_up = 1;

    if (!pData->fUseHostResolverPermanent)
        slirpInitializeDnsSettings(pData);
}

/**
 * Marks the link as down and cleans up the current connections.
 */
void slirp_link_down(PNATState pData)
{
    if (link_up == 0)
        return;

    slirpReleaseDnsSettings(pData);

    link_up = 0;
}

/**
 * Terminates the slirp component.
 */
void slirp_term(PNATState pData)
{
    struct socket *so;

    if (pData == NULL)
        return;

    icmp_finit(pData);

    while ((so = tcb.so_next) != &tcb)
    {
        /* Don't miss TCB releasing */
        if (   !sototcpcb(so)
            && (   so->so_state & SS_NOFDREF
                || so->s == -1))
             sofree(pData, so);
        else
            tcp_close(pData, sototcpcb(so));
    }

    while ((so = udb.so_next) != &udb)
        udp_detach(pData, so);

    slirp_link_down(pData);
    ftp_alias_unload(pData);
    nbt_alias_unload(pData);

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
    {
        DNSMAPPINGHEAD *heads[2];
        int i;

        heads[0] = &pData->DNSMapNames;
        heads[1] = &pData->DNSMapPatterns;
        for (i = 0; i < RT_ELEMENTS(heads); ++i)
        {
            while (!STAILQ_EMPTY(heads[i]))
            {
                PDNSMAPPINGENTRY pDnsEntry = STAILQ_FIRST(heads[i]);
                STAILQ_REMOVE_HEAD(heads[i], MapList);
                RTStrFree(pDnsEntry->pszName);
                RTMemFree(pDnsEntry);
            }
        }
    }
#endif

    while (!LIST_EMPTY(&instancehead))
    {
        struct libalias *la = LIST_FIRST(&instancehead);
        /* libalias do all clean up */
        LibAliasUninit(la);
    }
    while (!LIST_EMPTY(&pData->arp_cache))
    {
        struct arp_cache_entry *ac = LIST_FIRST(&pData->arp_cache);
        LIST_REMOVE(ac, list);
        RTMemFree(ac);
    }
    while (!LIST_EMPTY(&pData->port_forward_rule_head))
    {
        struct port_forward_rule *rule = LIST_FIRST(&pData->port_forward_rule_head);
        LIST_REMOVE(rule, list);
        RTMemFree(rule);
    }
    slirpTftpTerm(pData);
    bootp_dhcp_fini(pData);
    m_fini(pData);
#ifdef RT_OS_WINDOWS
    WSACleanup();
#endif
    if (tftp_prefix)
        RTStrFree((char *)tftp_prefix);
#ifdef LOG_ENABLED
    Log(("\n"
         "NAT statistics\n"
         "--------------\n"
         "\n"));
    ipstats(pData);
    tcpstats(pData);
    udpstats(pData);
    icmpstats(pData);
    mbufstats(pData);
    sockstats(pData);
    Log(("\n"
         "\n"
         "\n"));
#endif
    RTCritSectRwDelete(&pData->CsRwHandlerChain);
    RTMemFree(pData);
}


#define CONN_CANFSEND(so) (((so)->so_state & (SS_FCANTSENDMORE|SS_ISFCONNECTED)) == SS_ISFCONNECTED)
#define CONN_CANFRCV(so)  (((so)->so_state & (SS_FCANTRCVMORE|SS_ISFCONNECTED)) == SS_ISFCONNECTED)

/*
 * curtime kept to an accuracy of 1ms
 */
static void updtime(PNATState pData)
{
#ifdef RT_OS_WINDOWS
    struct _timeb tb;

    _ftime(&tb);
    curtime  = (u_int)tb.time * (u_int)1000;
    curtime += (u_int)tb.millitm;
#else
    gettimeofday(&tt, 0);

    curtime  = (u_int)tt.tv_sec  * (u_int)1000;
    curtime += (u_int)tt.tv_usec / (u_int)1000;

    if ((tt.tv_usec % 1000) >= 500)
        curtime++;
#endif
}

#ifdef RT_OS_WINDOWS
void slirp_select_fill(PNATState pData, int *pnfds)
#else /* RT_OS_WINDOWS */
void slirp_select_fill(PNATState pData, int *pnfds, struct pollfd *polls)
#endif /* !RT_OS_WINDOWS */
{
    struct socket *so, *so_next;
    int nfds;
#if defined(RT_OS_WINDOWS)
    int rc;
    int error;
#else
    int poll_index = 0;
#endif
    int i;

    STAM_PROFILE_START(&pData->StatFill, a);

    nfds = *pnfds;

    /*
     * First, TCP sockets
     */
    do_slowtimo = 0;
    if (!link_up)
        goto done;

    /*
     * *_slowtimo needs calling if there are IP fragments
     * in the fragment queue, or there are TCP connections active
     */
    /* XXX:
     * triggering of fragment expiration should be the same but use new macroses
     */
    do_slowtimo = (tcb.so_next != &tcb);
    if (!do_slowtimo)
    {
        for (i = 0; i < IPREASS_NHASH; i++)
        {
            if (!TAILQ_EMPTY(&ipq[i]))
            {
                do_slowtimo = 1;
                break;
            }
        }
    }
    /* always add the ICMP socket */
#ifndef RT_OS_WINDOWS
    pData->icmp_socket.so_poll_index = -1;
#endif
    ICMP_ENGAGE_EVENT(&pData->icmp_socket, readfds);

    STAM_COUNTER_RESET(&pData->StatTCP);
    STAM_COUNTER_RESET(&pData->StatTCPHot);

    QSOCKET_FOREACH(so, so_next, tcp)
    /* { */
        Assert(so->so_type == IPPROTO_TCP);
#if !defined(RT_OS_WINDOWS)
        so->so_poll_index = -1;
#endif
        STAM_COUNTER_INC(&pData->StatTCP);

        /*
         * See if we need a tcp_fasttimo
         */
        if (    time_fasttimo == 0
                && so->so_tcpcb != NULL
                && so->so_tcpcb->t_flags & TF_DELACK)
        {
            time_fasttimo = curtime; /* Flag when we want a fasttimo */
        }

        /*
         * NOFDREF can include still connecting to local-host,
         * newly socreated() sockets etc. Don't want to select these.
         */
        if (so->so_state & SS_NOFDREF || so->s == -1)
            CONTINUE(tcp);

        /*
         * Set for reading sockets which are accepting
         */
        if (so->so_state & SS_FACCEPTCONN)
        {
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT1(so, readfds);
            CONTINUE(tcp);
        }

        /*
         * Set for writing sockets which are connecting
         */
        if (so->so_state & SS_ISFCONNECTING)
        {
            Log2(("connecting %R[natsock] engaged\n",so));
            STAM_COUNTER_INC(&pData->StatTCPHot);
#ifdef RT_OS_WINDOWS
            WIN_TCP_ENGAGE_EVENT2(so, writefds, connectfds);
#else
            TCP_ENGAGE_EVENT1(so, writefds);
#endif
        }

        /*
         * Set for writing if we are connected, can send more, and
         * we have something to send
         */
        if (CONN_CANFSEND(so) && SBUF_LEN(&so->so_rcv))
        {
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT1(so, writefds);
        }

        /*
         * Set for reading (and urgent data) if we are connected, can
         * receive more, and we have room for it XXX /2 ?
         */
        /** @todo vvl - check which predicat here will be more useful here in rerm of new sbufs. */
        if (   CONN_CANFRCV(so)
            && (SBUF_LEN(&so->so_snd) < (SBUF_SIZE(&so->so_snd)/2))
#ifdef RT_OS_WINDOWS
            && !(so->so_state & SS_ISFCONNECTING)
#endif
        )
        {
            STAM_COUNTER_INC(&pData->StatTCPHot);
            TCP_ENGAGE_EVENT2(so, readfds, xfds);
        }
        LOOP_LABEL(tcp, so, so_next);
    }

    /*
     * UDP sockets
     */
    STAM_COUNTER_RESET(&pData->StatUDP);
    STAM_COUNTER_RESET(&pData->StatUDPHot);

    QSOCKET_FOREACH(so, so_next, udp)
    /* { */

        Assert(so->so_type == IPPROTO_UDP);
        STAM_COUNTER_INC(&pData->StatUDP);
#if !defined(RT_OS_WINDOWS)
        so->so_poll_index = -1;
#endif

        /*
         * See if it's timed out
         */
        if (so->so_expire)
        {
            if (so->so_expire <= curtime)
            {
                Log2(("NAT: %R[natsock] expired\n", so));
                if (so->so_timeout != NULL)
                {
                    /* so_timeout - might change the so_expire value or
                     * drop so_timeout* from so.
                     */
                    so->so_timeout(pData, so, so->so_timeout_arg);
                    /* on 4.2 so->
                     */
                    if (   so_next->so_prev != so /* so_timeout freed the socket */
                        || so->so_timeout)  /* so_timeout just freed so_timeout */
                      CONTINUE_NO_UNLOCK(udp);
                }
                UDP_DETACH(pData, so, so_next);
                CONTINUE_NO_UNLOCK(udp);
            }
        }

        /*
         * When UDP packets are received from over the link, they're
         * sendto()'d straight away, so no need for setting for writing
         * Limit the number of packets queued by this session to 4.
         * Note that even though we try and limit this to 4 packets,
         * the session could have more queued if the packets needed
         * to be fragmented.
         *
         * (XXX <= 4 ?)
         */
        if ((so->so_state & SS_ISFCONNECTED) && so->so_queued <= 4)
        {
            STAM_COUNTER_INC(&pData->StatUDPHot);
            UDP_ENGAGE_EVENT(so, readfds);
        }
        LOOP_LABEL(udp, so, so_next);
    }
done:

#if defined(RT_OS_WINDOWS)
    *pnfds = VBOX_EVENT_COUNT;
#else /* RT_OS_WINDOWS */
    AssertRelease(poll_index <= *pnfds);
    *pnfds = poll_index;
#endif /* !RT_OS_WINDOWS */

    STAM_PROFILE_STOP(&pData->StatFill, a);
}


/**
 * This function do Connection or sending tcp sequence to.
 * @returns if true operation completed
 * @note: functions call tcp_input that potentially could lead to tcp_drop
 */
static bool slirpConnectOrWrite(PNATState pData, struct socket *so, bool fConnectOnly)
{
    int ret;
    LogFlowFunc(("ENTER: so:%R[natsock], fConnectOnly:%RTbool\n", so, fConnectOnly));
    /*
     * Check for non-blocking, still-connecting sockets
     */
    if (so->so_state & SS_ISFCONNECTING)
    {
        Log2(("connecting %R[natsock] catched\n", so));
        /* Connected */
        so->so_state &= ~SS_ISFCONNECTING;

        /*
         * This should be probably guarded by PROBE_CONN too. Anyway,
         * we disable it on OS/2 because the below send call returns
         * EFAULT which causes the opened TCP socket to close right
         * after it has been opened and connected.
         */
#ifndef RT_OS_OS2
    ret = send(so->s, (const char *)&ret, 0, 0);
    if (ret < 0)
    {
        /* XXXXX Must fix, zero bytes is a NOP */
        if (   soIgnorableErrorCode(errno)
            || errno == ENOTCONN)
        {
            LogFlowFunc(("LEAVE: false\n"));
            return false;
        }

        /* else failed */
        so->so_state = SS_NOFDREF;
    }
    /* else so->so_state &= ~SS_ISFCONNECTING; */
#endif

        /*
         * Continue tcp_input
         */
        TCP_INPUT(pData, (struct mbuf *)NULL, sizeof(struct ip), so);
        /* continue; */
    }
    else if (!fConnectOnly)
    {
        SOWRITE(ret, pData, so);
        if (RT_LIKELY(ret > 0))
        {
            /*
             * Make sure we will send window update to peer.  This is
             * a moral equivalent of calling tcp_output() for PRU_RCVD
             * in tcp_usrreq() of the real stack.
             */
            struct tcpcb *tp = sototcpcb(so);
            if (RT_LIKELY(tp != NULL))
                tp->t_flags |= TF_DELACK;
        }
    }

    LogFlowFunc(("LEAVE: true\n"));
    return true;
}

#if defined(RT_OS_WINDOWS)
void slirp_select_poll(PNATState pData, int fTimeout)
#else /* RT_OS_WINDOWS */
void slirp_select_poll(PNATState pData, struct pollfd *polls, int ndfs)
#endif /* !RT_OS_WINDOWS */
{
    struct socket *so, *so_next;
    int ret;
#if defined(RT_OS_WINDOWS)
    WSANETWORKEVENTS NetworkEvents;
    int rc;
    int error;
#endif

    STAM_PROFILE_START(&pData->StatPoll, a);

    /* Update time */
    updtime(pData);

    /*
     * See if anything has timed out
     */
    if (link_up)
    {
        if (time_fasttimo && ((curtime - time_fasttimo) >= 2))
        {
            STAM_PROFILE_START(&pData->StatFastTimer, b);
            tcp_fasttimo(pData);
            time_fasttimo = 0;
            STAM_PROFILE_STOP(&pData->StatFastTimer, b);
        }
        if (do_slowtimo && ((curtime - last_slowtimo) >= 499))
        {
            STAM_PROFILE_START(&pData->StatSlowTimer, c);
            ip_slowtimo(pData);
            tcp_slowtimo(pData);
            last_slowtimo = curtime;
            STAM_PROFILE_STOP(&pData->StatSlowTimer, c);
        }
    }
#if defined(RT_OS_WINDOWS)
    if (fTimeout)
        return; /* only timer update */
#endif

    /*
     * Check sockets
     */
    if (!link_up)
        goto done;
#if defined(RT_OS_WINDOWS)
    icmpwin_process(pData);
#else
    if (   (pData->icmp_socket.s != -1)
        && CHECK_FD_SET(&pData->icmp_socket, ignored, readfds))
        sorecvfrom(pData, &pData->icmp_socket);
#endif
    /*
     * Check TCP sockets
     */
    QSOCKET_FOREACH(so, so_next, tcp)
    /* { */
        Assert(!so->fUnderPolling);
        so->fUnderPolling = 1;
        if (slirpVerifyAndFreeSocket(pData, so))
            CONTINUE(tcp);
        /*
         * FD_ISSET is meaningless on these sockets
         * (and they can crash the program)
         */
        if (so->so_state & SS_NOFDREF || so->s == -1)
        {
            so->fUnderPolling = 0;
            CONTINUE(tcp);
        }

        POLL_TCP_EVENTS(rc, error, so, &NetworkEvents);

        LOG_NAT_SOCK(so, TCP, &NetworkEvents, readfds, writefds, xfds);

        if (so->so_state & SS_ISFCONNECTING)
        {
            int sockerr = 0;
#if !defined(RT_OS_WINDOWS)
            {
                int revents = 0;

                /*
                 * Failed connect(2) is reported by poll(2) on
                 * different OSes with different combinations of
                 * POLLERR, POLLHUP, and POLLOUT.
                 */
                if (   CHECK_FD_SET(so, NetworkEvents, closefds) /* POLLHUP */
                    || CHECK_FD_SET(so, NetworkEvents, rderr))   /* POLLERR */
                {
                    revents = POLLHUP; /* squash to single "failed" flag */
                }
#if defined(RT_OS_SOLARIS) || defined(RT_OS_NETBSD)
                /* Solaris and NetBSD report plain POLLOUT even on error */
                else if (CHECK_FD_SET(so, NetworkEvents, writefds)) /* POLLOUT */
                {
                    revents = POLLOUT;
                }
#endif

                if (revents != 0)
                {
                    socklen_t optlen = (socklen_t)sizeof(sockerr);
                    ret = getsockopt(so->s, SOL_SOCKET, SO_ERROR, &sockerr, &optlen);

                    if (   RT_UNLIKELY(ret < 0)
                        || (   (revents & POLLHUP)
                            && RT_UNLIKELY(sockerr == 0)))
                        sockerr = ETIMEDOUT;
                }
            }
#else  /* RT_OS_WINDOWS */
            {
                if (NetworkEvents.lNetworkEvents & FD_CONNECT)
                    sockerr = NetworkEvents.iErrorCode[FD_CONNECT_BIT];
            }
#endif
            if (sockerr != 0)
            {
                tcp_fconnect_failed(pData, so, sockerr);
                ret = slirpVerifyAndFreeSocket(pData, so);
                Assert(ret == 1); /* freed */
                CONTINUE(tcp);
            }

            /*
             * XXX: For now just fall through to the old code to
             * handle successful connect(2).
             */
        }

        /*
         * Check for URG data
         * This will soread as well, so no need to
         * test for readfds below if this succeeds
         */

        /* out-of-band data */
        if (    CHECK_FD_SET(so, NetworkEvents, xfds)
#ifdef RT_OS_DARWIN
            /* Darwin and probably BSD hosts generates POLLPRI|POLLHUP event on receiving TCP.flags.{ACK|URG|FIN} this
             * combination on other Unixs hosts doesn't enter to this branch
             */
            &&  !CHECK_FD_SET(so, NetworkEvents, closefds)
#endif
#ifdef RT_OS_WINDOWS
            /**
             * In some cases FD_CLOSE comes with FD_OOB, that confuse tcp processing.
             */
            && !WIN_CHECK_FD_SET(so, NetworkEvents, closefds)
#endif
        )
        {
            sorecvoob(pData, so);
            if (slirpVerifyAndFreeSocket(pData, so))
                CONTINUE(tcp);
        }

        /*
         * Check sockets for reading
         */
        else if (   CHECK_FD_SET(so, NetworkEvents, readfds)
                 || WIN_CHECK_FD_SET(so, NetworkEvents, acceptds))
        {

#ifdef RT_OS_WINDOWS
            if (WIN_CHECK_FD_SET(so, NetworkEvents, connectfds))
            {
                /* Finish connection first */
                /* should we ignore return value? */
                bool fRet = slirpConnectOrWrite(pData, so, true);
                LogFunc(("fRet:%RTbool\n", fRet)); NOREF(fRet);
                if (slirpVerifyAndFreeSocket(pData, so))
                    CONTINUE(tcp);
            }
#endif
            /*
             * Check for incoming connections
             */
            if (so->so_state & SS_FACCEPTCONN)
            {
                TCP_CONNECT(pData, so);
                if (slirpVerifyAndFreeSocket(pData, so))
                    CONTINUE(tcp);
                if (!CHECK_FD_SET(so, NetworkEvents, closefds))
                {
                    so->fUnderPolling = 0;
                    CONTINUE(tcp);
                }
            }

            ret = soread(pData, so);
            if (slirpVerifyAndFreeSocket(pData, so))
                CONTINUE(tcp);
            /* Output it if we read something */
            if (RT_LIKELY(ret > 0))
                TCP_OUTPUT(pData, sototcpcb(so));

            if (slirpVerifyAndFreeSocket(pData, so))
                CONTINUE(tcp);
        }

        /*
         * Check for FD_CLOSE events.
         * in some cases once FD_CLOSE engaged on socket it could be flashed latter (for some reasons)
         */
        if (   CHECK_FD_SET(so, NetworkEvents, closefds)
            || (so->so_close == 1))
        {
            /*
             * drain the socket
             */
            for (;   so_next->so_prev == so
                  && !slirpVerifyAndFreeSocket(pData, so);)
            {
                ret = soread(pData, so);
                if (slirpVerifyAndFreeSocket(pData, so))
                    break;

                if (ret > 0)
                    TCP_OUTPUT(pData, sototcpcb(so));
                else if (so_next->so_prev == so)
                {
                    Log2(("%R[natsock] errno %d (%s)\n", so, errno, strerror(errno)));
                    break;
                }
            }

            /* if socket freed ''so'' is PHANTOM and next socket isn't points on it */
            if (so_next->so_prev != so)
            {
                CONTINUE(tcp);
            }
            else
            {
                /* mark the socket for termination _after_ it was drained */
                so->so_close = 1;
                /* No idea about Windows but on Posix, POLLHUP means that we can't send more.
                 * Actually in the specific error scenario, POLLERR is set as well. */
#ifndef RT_OS_WINDOWS
                if (CHECK_FD_SET(so, NetworkEvents, rderr))
                    sofcantsendmore(so);
#endif
            }
        }

        /*
         * Check sockets for writing
         */
        if (    CHECK_FD_SET(so, NetworkEvents, writefds)
#ifdef RT_OS_WINDOWS
            ||  WIN_CHECK_FD_SET(so, NetworkEvents, connectfds)
#endif
            )
        {
            int fConnectOrWriteSuccess = slirpConnectOrWrite(pData, so, false);
            /* slirpConnectOrWrite could return true even if tcp_input called tcp_drop,
             * so we should be ready to such situations.
             */
            if (slirpVerifyAndFreeSocket(pData, so))
                CONTINUE(tcp);
            else if (!fConnectOrWriteSuccess)
            {
                so->fUnderPolling = 0;
                CONTINUE(tcp);
            }
            /* slirpConnectionOrWrite succeeded and socket wasn't dropped */
        }

        /*
         * Probe a still-connecting, non-blocking socket
         * to check if it's still alive
         */
#ifdef PROBE_CONN
        if (so->so_state & SS_ISFCONNECTING)
        {
            ret = recv(so->s, (char *)&ret, 0, 0);

            if (ret < 0)
            {
                /* XXX */
                if (   soIgnorableErrorCode(errno)
                    || errno == ENOTCONN)
                {
                    CONTINUE(tcp); /* Still connecting, continue */
                }

                /* else failed */
                so->so_state = SS_NOFDREF;

                /* tcp_input will take care of it */
            }
            else
            {
                ret = send(so->s, &ret, 0, 0);
                if (ret < 0)
                {
                    /* XXX */
                    if (   soIgnorableErrorCode(errno)
                        || errno == ENOTCONN)
                    {
                        CONTINUE(tcp);
                    }
                    /* else failed */
                    so->so_state = SS_NOFDREF;
                }
                else
                    so->so_state &= ~SS_ISFCONNECTING;

            }
            TCP_INPUT((struct mbuf *)NULL, sizeof(struct ip),so);
        } /* SS_ISFCONNECTING */
#endif
        if (!slirpVerifyAndFreeSocket(pData, so))
            so->fUnderPolling = 0;
        LOOP_LABEL(tcp, so, so_next);
    }

    /*
     * Now UDP sockets.
     * Incoming packets are sent straight away, they're not buffered.
     * Incoming UDP data isn't buffered either.
     */
     QSOCKET_FOREACH(so, so_next, udp)
     /* { */
#if 0
        so->fUnderPolling = 1;
        if(slirpVerifyAndFreeSocket(pData, so));
            CONTINUE(udp);
        so->fUnderPolling = 0;
#endif

        POLL_UDP_EVENTS(rc, error, so, &NetworkEvents);

        LOG_NAT_SOCK(so, UDP, &NetworkEvents, readfds, writefds, xfds);

        if (so->s != -1 && CHECK_FD_SET(so, NetworkEvents, readfds))
        {
            SORECVFROM(pData, so);
        }
        LOOP_LABEL(udp, so, so_next);
    }

done:

    STAM_PROFILE_STOP(&pData->StatPoll, a);
}


struct arphdr
{
    unsigned short  ar_hrd;             /* format of hardware address   */
#define ARPHRD_ETHER    1               /* ethernet hardware format     */
    unsigned short  ar_pro;             /* format of protocol address   */
    unsigned char   ar_hln;             /* length of hardware address   */
    unsigned char   ar_pln;             /* length of protocol address   */
    unsigned short  ar_op;              /* ARP opcode (command)         */
#define ARPOP_REQUEST   1               /* ARP request                  */
#define ARPOP_REPLY     2               /* ARP reply                    */

    /*
     *      Ethernet looks like this : This bit is variable sized however...
     */
    unsigned char   ar_sha[ETH_ALEN];   /* sender hardware address      */
    unsigned char   ar_sip[4];          /* sender IP address            */
    unsigned char   ar_tha[ETH_ALEN];   /* target hardware address      */
    unsigned char   ar_tip[4];          /* target IP address            */
};
AssertCompileSize(struct arphdr, 28);

static void arp_output(PNATState pData, const uint8_t *pcu8EtherSource, const struct arphdr *pcARPHeaderSource, uint32_t ip4TargetAddress)
{
    struct ethhdr *pEtherHeaderResponse;
    struct arphdr *pARPHeaderResponse;
    uint32_t ip4TargetAddressInHostFormat;
    struct mbuf *pMbufResponse;

    Assert((pcu8EtherSource));
    if (!pcu8EtherSource)
        return;
    ip4TargetAddressInHostFormat = RT_N2H_U32(ip4TargetAddress);

    pMbufResponse = m_getcl(pData, M_NOWAIT, MT_HEADER, M_PKTHDR);
    if (!pMbufResponse)
        return;
    pEtherHeaderResponse = mtod(pMbufResponse, struct ethhdr *);
    /* @note: if_encap will swap src and dst*/
    memcpy(pEtherHeaderResponse->h_source, pcu8EtherSource, ETH_ALEN);
    pMbufResponse->m_data += ETH_HLEN;
    pARPHeaderResponse = mtod(pMbufResponse, struct arphdr *);
    pMbufResponse->m_len = sizeof(struct arphdr);

    pARPHeaderResponse->ar_hrd = RT_H2N_U16_C(1);
    pARPHeaderResponse->ar_pro = RT_H2N_U16_C(ETH_P_IP);
    pARPHeaderResponse->ar_hln = ETH_ALEN;
    pARPHeaderResponse->ar_pln = 4;
    pARPHeaderResponse->ar_op = RT_H2N_U16_C(ARPOP_REPLY);
    memcpy(pARPHeaderResponse->ar_sha, special_ethaddr, ETH_ALEN);

    if (!slirpMbufTagService(pData, pMbufResponse, (uint8_t)(ip4TargetAddressInHostFormat & ~pData->netmask)))
    {
        static bool fTagErrorReported;
        if (!fTagErrorReported)
        {
            LogRel(("NAT: Couldn't add the tag(PACKET_SERVICE:%d)\n",
                        (uint8_t)(ip4TargetAddressInHostFormat & ~pData->netmask)));
            fTagErrorReported = true;
        }
    }
    pARPHeaderResponse->ar_sha[5] = (uint8_t)(ip4TargetAddressInHostFormat & ~pData->netmask);

    memcpy(pARPHeaderResponse->ar_sip, pcARPHeaderSource->ar_tip, 4);
    memcpy(pARPHeaderResponse->ar_tha, pcARPHeaderSource->ar_sha, ETH_ALEN);
    memcpy(pARPHeaderResponse->ar_tip, pcARPHeaderSource->ar_sip, 4);
    if_encap(pData, ETH_P_ARP, pMbufResponse, ETH_ENCAP_URG);
}

/**
 * @note This function will free m!
 */
static void arp_input(PNATState pData, struct mbuf *m)
{
    struct ethhdr *pEtherHeader;
    struct arphdr *pARPHeader;
    int ar_op;
    uint32_t ip4TargetAddress;

    /* drivers never return runt packets, so this should never happen */
    if (RT_UNLIKELY((size_t)m->m_len
                    < sizeof(struct ethhdr) + sizeof(struct arphdr)))
        goto done;

    pEtherHeader = mtod(m, struct ethhdr *);
    pARPHeader = (struct arphdr *)&pEtherHeader[1];

    if (RT_UNLIKELY(   pARPHeader->ar_hrd != RT_H2N_U16_C(ARPHRD_ETHER)
                    || pARPHeader->ar_pro != RT_H2N_U16_C(ETH_P_IP)
                    || pARPHeader->ar_hln != ETH_ALEN
                    || pARPHeader->ar_pln != sizeof(RTNETADDRIPV4)))
        goto done;

    ar_op = RT_N2H_U16(pARPHeader->ar_op);
    ip4TargetAddress = *(uint32_t*)pARPHeader->ar_tip;

    switch (ar_op)
    {
        case ARPOP_REQUEST:
            if (   CTL_CHECK(ip4TargetAddress, CTL_DNS)
                || CTL_CHECK(ip4TargetAddress, CTL_ALIAS)
                || CTL_CHECK(ip4TargetAddress, CTL_TFTP))
            {
#if 0 /* Dropping ARP requests destined for CTL_ALIAS breaks all outgoing traffic completely, so don't do that... */
                /* Don't reply to ARP requests for the hosts loopback interface if it is disabled. */
                if (   CTL_CHECK(ip4TargetAddress, CTL_ALIAS)
                    && !pData->fLocalhostReachable)
                    break;
#endif
                slirp_update_guest_addr_guess(pData, *(uint32_t *)pARPHeader->ar_sip, "arp request");
                arp_output(pData, pEtherHeader->h_source, pARPHeader, ip4TargetAddress);
                break;
            }

            /* Gratuitous ARP */
            if (   *(uint32_t *)pARPHeader->ar_sip == *(uint32_t *)pARPHeader->ar_tip
                && (   memcmp(pARPHeader->ar_tha, zerro_ethaddr, ETH_ALEN) == 0
                    || memcmp(pARPHeader->ar_tha, broadcast_ethaddr, ETH_ALEN) == 0)
                && memcmp(pEtherHeader->h_dest, broadcast_ethaddr, ETH_ALEN) == 0)
            {
                LogRel2(("NAT: Gratuitous ARP from %RTnaipv4 at %RTmac\n",
                         *(uint32_t *)pARPHeader->ar_sip, pARPHeader->ar_sha));
                slirp_update_guest_addr_guess(pData, *(uint32_t *)pARPHeader->ar_sip, "gratuitous arp");
                slirp_arp_cache_update_or_add(pData, *(uint32_t *)pARPHeader->ar_sip, &pARPHeader->ar_sha[0]);
            }
            break;

        case ARPOP_REPLY:
            slirp_arp_cache_update_or_add(pData, *(uint32_t *)pARPHeader->ar_sip, &pARPHeader->ar_sha[0]);
            break;

        default:
            break;
    }

  done:
    m_freem(pData, m);
}

/**
 * Feed a packet into the slirp engine.
 *
 * @param   m               Data buffer, m_len is not valid.
 * @param   cbBuf           The length of the data in m.
 */
void slirp_input(PNATState pData, struct mbuf *m, size_t cbBuf)
{
    int proto;
    static bool fWarnedIpv6;
    struct ethhdr *eh;

    m->m_len = (int)cbBuf; Assert((size_t)m->m_len == cbBuf);
    if (cbBuf < ETH_HLEN)
    {
        Log(("NAT: packet having size %d has been ignored\n", m->m_len));
        m_freem(pData, m);
        return;
    }

    eh = mtod(m, struct ethhdr *);
    proto = RT_N2H_U16(eh->h_proto);
    switch(proto)
    {
        case ETH_P_ARP:
            arp_input(pData, m);
            break;

        case ETH_P_IP:
            /* Update time. Important if the network is very quiet, as otherwise
             * the first outgoing connection gets an incorrect timestamp. */
            updtime(pData);
            m_adj(m, ETH_HLEN);
            M_ASSERTPKTHDR(m);
            m->m_pkthdr.header = mtod(m, void *);
            ip_input(pData, m);
            break;

        case ETH_P_IPV6:
            m_freem(pData, m);
            if (!fWarnedIpv6)
            {
                LogRel(("NAT: IPv6 not supported\n"));
                fWarnedIpv6 = true;
            }
            break;

        default:
            Log(("NAT: Unsupported protocol %x\n", proto));
            m_freem(pData, m);
            break;
    }
}

/**
 * Output the IP packet to the ethernet device.
 *
 * @note This function will free m!
 */
void if_encap(PNATState pData, uint16_t eth_proto, struct mbuf *m, int flags)
{
    struct ethhdr *eh;
    uint8_t *mbuf = NULL;
    int mlen;
    STAM_PROFILE_START(&pData->StatIF_encap, a);
    LogFlowFunc(("ENTER: pData:%p, eth_proto:%RX16, m:%p, flags:%d\n",
                pData, eth_proto, m, flags));

    M_ASSERTPKTHDR(m);

    Assert(M_LEADINGSPACE(m) >= ETH_HLEN);
    m->m_data -= ETH_HLEN;
    m->m_len += ETH_HLEN;
    eh = mtod(m, struct ethhdr *);
    mlen = m->m_len;

    if (memcmp(eh->h_source, special_ethaddr, ETH_ALEN) != 0)
    {
        struct m_tag *t = m_tag_first(m);
        uint8_t u8ServiceId = CTL_ALIAS;
        memcpy(eh->h_dest, eh->h_source, ETH_ALEN);
        memcpy(eh->h_source, special_ethaddr, ETH_ALEN);
        Assert(memcmp(eh->h_dest, special_ethaddr, ETH_ALEN) != 0);
        if (memcmp(eh->h_dest, zerro_ethaddr, ETH_ALEN) == 0)
        {
            /* don't do anything */
            m_freem(pData, m);
            goto done;
        }
        if (   t
            && (t = m_tag_find(m, PACKET_SERVICE, NULL)))
        {
            Assert(t);
            u8ServiceId = *(uint8_t *)&t[1];
        }
        eh->h_source[5] = u8ServiceId;
    }
    /*
     * we're processing the chain, that isn't not expected.
     */
    Assert((!m->m_next));
    if (m->m_next)
    {
        Log(("NAT: if_encap's recived the chain, dropping...\n"));
        m_freem(pData, m);
        goto done;
    }
    mbuf = mtod(m, uint8_t *);
    eh->h_proto = RT_H2N_U16(eth_proto);
    LogFunc(("eh(dst:%RTmac, src:%RTmac)\n", eh->h_dest, eh->h_source));
    if (flags & ETH_ENCAP_URG)
        slirp_urg_output(pData->pvUser, m, mbuf, mlen);
    else
        slirp_output(pData->pvUser, m, mbuf, mlen);
done:
    STAM_PROFILE_STOP(&pData->StatIF_encap, a);
    LogFlowFuncLeave();
}


void
slirp_update_guest_addr_guess(PNATState pData, uint32_t guess, const char *msg)
{
    Assert(msg != NULL);

    if (pData->guest_addr_guess.s_addr == guess)
    {
        LogRel2(("NAT: Guest address guess %RTnaipv4 re-confirmed by %s\n",
                 pData->guest_addr_guess.s_addr, msg));
        return;
    }

    if (pData->guest_addr_guess.s_addr == INADDR_ANY)
    {
        pData->guest_addr_guess.s_addr = guess;
        LogRel(("NAT: Guest address guess set to %RTnaipv4 by %s\n",
                pData->guest_addr_guess.s_addr, msg));
        return;
    }
    else
    {
        LogRel(("NAT: Guest address guess changed from %RTnaipv4 to %RTnaipv4 by %s\n",
                pData->guest_addr_guess.s_addr, guess, msg));
        pData->guest_addr_guess.s_addr = guess;
        return;
    }
}


static struct port_forward_rule *
slirp_find_redirect(PNATState pData,
                    int is_udp,
                    struct in_addr host_addr, int host_port,
                    struct in_addr guest_addr, int guest_port)
{
    struct port_forward_rule *rule;
    uint16_t proto = (is_udp ? IPPROTO_UDP : IPPROTO_TCP);

    LIST_FOREACH(rule, &pData->port_forward_rule_head, list)
    {
        if (   rule->proto == proto
            && rule->host_port == host_port
            && rule->bind_ip.s_addr == host_addr.s_addr
            && rule->guest_port == guest_port
            && rule->guest_addr.s_addr == guest_addr.s_addr)
        {
            return rule;
        }
    }

    return NULL;
}


int slirp_add_redirect(PNATState pData, int is_udp, struct in_addr host_addr, int host_port,
                struct in_addr guest_addr, int guest_port)
{
    struct port_forward_rule *rule;

    rule = slirp_find_redirect(pData, is_udp, host_addr, host_port, guest_addr, guest_port);
    if (rule != NULL) /* rule has been already registered */
    {
        /* XXX: this shouldn't happen */
        return 0;
    }

    rule = RTMemAllocZ(sizeof(struct port_forward_rule));
    if (rule == NULL)
        return 1;

    rule->proto = (is_udp ? IPPROTO_UDP : IPPROTO_TCP);
    rule->bind_ip.s_addr = host_addr.s_addr;
    rule->host_port = host_port;
    rule->guest_addr.s_addr = guest_addr.s_addr;
    rule->guest_port = guest_port;

    if (rule->proto == IPPROTO_UDP)
        rule->so = udp_listen(pData, rule->bind_ip.s_addr, RT_H2N_U16(rule->host_port),
                              rule->guest_addr.s_addr, RT_H2N_U16(rule->guest_port), 0);
    else
        rule->so = solisten(pData, rule->bind_ip.s_addr, RT_H2N_U16(rule->host_port),
                            rule->guest_addr.s_addr, RT_H2N_U16(rule->guest_port), 0);

    if (rule->so == NULL)
    {
        LogRel(("NAT: Failed to redirect %s %RTnaipv4:%d -> %RTnaipv4:%d (%s)\n",
                rule->proto == IPPROTO_UDP ? "UDP" : "TCP",
                rule->bind_ip.s_addr, rule->host_port,
                guest_addr, rule->guest_port, strerror(errno)));
        RTMemFree(rule);
        return 1;
    }

    LogRel(("NAT: Set redirect %s %RTnaipv4:%d -> %RTnaipv4:%d\n",
            rule->proto == IPPROTO_UDP ? "UDP" : "TCP",
            rule->bind_ip.s_addr, rule->host_port,
            guest_addr, rule->guest_port));

    LIST_INSERT_HEAD(&pData->port_forward_rule_head, rule, list);
    return 0;
}


int slirp_remove_redirect(PNATState pData, int is_udp, struct in_addr host_addr, int host_port,
                struct in_addr guest_addr, int guest_port)
{
    struct port_forward_rule *rule;

    rule = slirp_find_redirect(pData, is_udp, host_addr, host_port, guest_addr, guest_port);
    if (rule == NULL)
    {
        LogRel(("NAT: Unable to find redirect %s %RTnaipv4:%d -> %RTnaipv4:%d\n",
                is_udp ? "UDP" : "TCP",
                host_addr.s_addr, host_port,
                guest_addr.s_addr, guest_port));
        return 0;
    }

    LogRel(("NAT: Remove redirect %s %RTnaipv4:%d -> %RTnaipv4:%d\n",
            rule->proto == IPPROTO_UDP ? "UDP" : "TCP",
            rule->bind_ip.s_addr, rule->host_port,
            guest_addr.s_addr, rule->guest_port));

    if (rule->so != NULL)
    {
        if (is_udp)
            udp_detach(pData, rule->so);
        else
            tcp_close(pData, sototcpcb(rule->so));
    }

    LIST_REMOVE(rule, list);
    RTMemFree(rule);
    return 0;
}


#if defined(RT_OS_WINDOWS)
HANDLE *slirp_get_events(PNATState pData)
{
        return pData->phEvents;
}
void slirp_register_external_event(PNATState pData, HANDLE hEvent, int index)
{
        pData->phEvents[index] = hEvent;
}
#endif

unsigned int slirp_get_timeout_ms(PNATState pData)
{
    if (link_up)
    {
        if (time_fasttimo)
            return 2;
        if (do_slowtimo)
            return 500; /* see PR_SLOWHZ */
    }
    return 3600*1000;   /* one hour */
}

#ifndef RT_OS_WINDOWS
int slirp_get_nsock(PNATState pData)
{
    return pData->nsock;
}
#endif

/*
 * this function called from NAT thread
 */
void slirp_post_sent(PNATState pData, void *pvArg)
{
    struct mbuf *m = (struct mbuf *)pvArg;
    m_freem(pData, m);
}

void slirp_set_dhcp_TFTP_prefix(PNATState pData, const char *tftpPrefix)
{
    Log2(("tftp_prefix: %s\n", tftpPrefix));
    if (tftp_prefix)
        RTStrFree((char *)tftp_prefix);
    tftp_prefix = RTPathAbsDup(tftpPrefix);
}

void slirp_set_dhcp_TFTP_bootfile(PNATState pData, const char *bootFile)
{
    Log2(("bootFile: %s\n", bootFile));
    bootp_filename = bootFile;
}

void slirp_set_dhcp_next_server(PNATState pData, const char *next_server)
{
    Log2(("next_server: %s\n", next_server));
    if (next_server == NULL)
        pData->tftp_server.s_addr = RT_H2N_U32(RT_N2H_U32(pData->special_addr.s_addr) | CTL_TFTP);
    else
        inet_aton(next_server, &pData->tftp_server);
}

int slirp_set_binding_address(PNATState pData, char *addr)
{
    int ok;

    pData->bindIP.s_addr = INADDR_ANY;

    if (addr == NULL || *addr == '\0')
        return VINF_SUCCESS;

    ok = inet_aton(addr, &pData->bindIP);
    if (!ok)
    {
        LogRel(("NAT: Unable to parse binding address: %s\n", addr));
        return VERR_INVALID_PARAMETER;
    }

    if (pData->bindIP.s_addr == INADDR_ANY)
        return VINF_SUCCESS;

    if ((pData->bindIP.s_addr & RT_N2H_U32_C(0xe0000000)) == RT_N2H_U32_C(0xe0000000))
    {
        LogRel(("NAT: Ignoring multicast binding address %RTnaipv4\n", pData->bindIP.s_addr));
        pData->bindIP.s_addr = INADDR_ANY;
        return VERR_INVALID_PARAMETER;
    }

    LogRel(("NAT: Binding address %RTnaipv4\n", pData->bindIP.s_addr));
    return VINF_SUCCESS;
}

void slirp_set_dhcp_dns_proxy(PNATState pData, bool fDNSProxy)
{
    if (!pData->fUseHostResolver)
    {
        Log2(("NAT: DNS proxy switched %s\n", (fDNSProxy ? "on" : "off")));
        pData->fUseDnsProxy = fDNSProxy;
    }
    else if (fDNSProxy)
        LogRel(("NAT: Host Resolver conflicts with DNS proxy, the last one was forcely ignored\n"));
}

#define CHECK_ARG(name, val, lim_min, lim_max)                                      \
    do {                                                                            \
        if ((val) < (lim_min) || (val) > (lim_max))                                 \
        {                                                                           \
            LogRel(("NAT: (" #name ":%d) has been ignored, "                        \
                "because out of range (%d, %d)\n", (val), (lim_min), (lim_max)));   \
            return;                                                                 \
        }                                                                           \
        else                                                                        \
            LogRel(("NAT: (" #name ":%d)\n", (val)));                               \
    } while (0)

void slirp_set_somaxconn(PNATState pData, int iSoMaxConn)
{
    LogFlowFunc(("iSoMaxConn:%d\n", iSoMaxConn));
    /* Conditions */
    if (iSoMaxConn > SOMAXCONN)
    {
        LogRel(("NAT: value of somaxconn(%d) bigger than SOMAXCONN(%d)\n", iSoMaxConn, SOMAXCONN));
        iSoMaxConn = SOMAXCONN;
    }

    if (iSoMaxConn < 1)
    {
        LogRel(("NAT: proposed value(%d) of somaxconn is invalid, default value is used (%d)\n", iSoMaxConn, pData->soMaxConn));
        LogFlowFuncLeave();
        return;
    }

    /* Asignment */
    if (pData->soMaxConn != iSoMaxConn)
    {
        LogRel(("NAT: value of somaxconn has been changed from %d to %d\n",
                pData->soMaxConn, iSoMaxConn));
        pData->soMaxConn = iSoMaxConn;
    }
    LogFlowFuncLeave();
}
/* don't allow user set less 8kB and more than 1M values */
#define _8K_1M_CHECK_ARG(name, val) CHECK_ARG(name, (val), 8, 1024)
void slirp_set_rcvbuf(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("SOCKET_RCVBUF", kilobytes);
    pData->socket_rcv = kilobytes;
}
void slirp_set_sndbuf(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("SOCKET_SNDBUF", kilobytes);
    pData->socket_snd = kilobytes * _1K;
}
void slirp_set_tcp_rcvspace(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("TCP_RCVSPACE", kilobytes);
    tcp_rcvspace = kilobytes * _1K;
}
void slirp_set_tcp_sndspace(PNATState pData, int kilobytes)
{
    _8K_1M_CHECK_ARG("TCP_SNDSPACE", kilobytes);
    tcp_sndspace = kilobytes * _1K;
}

/*
 * Looking for Ether by ip in ARP-cache
 * Note: it´s responsible of caller to allocate buffer for result
 * @returns iprt status code
 */
int slirp_arp_lookup_ether_by_ip(PNATState pData, uint32_t ip, uint8_t *ether)
{
    struct arp_cache_entry *ac;

    if (ether == NULL)
        return VERR_INVALID_PARAMETER;

    if (LIST_EMPTY(&pData->arp_cache))
        return VERR_NOT_FOUND;

    LIST_FOREACH(ac, &pData->arp_cache, list)
    {
        if (   ac->ip == ip
            && memcmp(ac->ether, broadcast_ethaddr, ETH_ALEN) != 0)
        {
            memcpy(ether, ac->ether, ETH_ALEN);
            return VINF_SUCCESS;
        }
    }
    return VERR_NOT_FOUND;
}

/*
 * Looking for IP by Ether in ARP-cache
 * Note: it´s responsible of caller to allocate buffer for result
 * @returns 0 - if found, 1 - otherwise
 */
int slirp_arp_lookup_ip_by_ether(PNATState pData, const uint8_t *ether, uint32_t *ip)
{
    struct arp_cache_entry *ac;
    *ip = INADDR_ANY;

    if (LIST_EMPTY(&pData->arp_cache))
        return VERR_NOT_FOUND;

    LIST_FOREACH(ac, &pData->arp_cache, list)
    {
        if (memcmp(ether, ac->ether, ETH_ALEN) == 0)
        {
            *ip = ac->ip;
            return VINF_SUCCESS;
        }
    }
    return VERR_NOT_FOUND;
}

void slirp_arp_who_has(PNATState pData, uint32_t dst)
{
    struct mbuf *m;
    struct ethhdr *ehdr;
    struct arphdr *ahdr;
    static bool   fWarned = false;
    LogFlowFunc(("ENTER: %RTnaipv4\n", dst));

    /* ARP request WHO HAS 0.0.0.0 is one of the signals
     * that something has been broken at Slirp. Investigating
     * pcap dumps it's easy to miss warning ARP requests being
     * focused on investigation of other protocols flow.
     */
#ifdef DEBUG_vvl
    Assert((dst != INADDR_ANY));
    NOREF(fWarned);
#else
    if (   dst == INADDR_ANY
        && !fWarned)
    {
        LogRel(("NAT: ARP: \"WHO HAS INADDR_ANY\" request has been detected\n"));
        fWarned = true;
    }
#endif /* !DEBUG_vvl */

    m = m_getcl(pData, M_NOWAIT, MT_HEADER, M_PKTHDR);
    if (m == NULL)
    {
        Log(("NAT: Can't alloc mbuf for ARP request\n"));
        LogFlowFuncLeave();
        return;
    }
    ehdr = mtod(m, struct ethhdr *);
    memset(ehdr->h_source, 0xff, ETH_ALEN);
    ahdr = (struct arphdr *)&ehdr[1];
    ahdr->ar_hrd = RT_H2N_U16_C(1);
    ahdr->ar_pro = RT_H2N_U16_C(ETH_P_IP);
    ahdr->ar_hln = ETH_ALEN;
    ahdr->ar_pln = 4;
    ahdr->ar_op = RT_H2N_U16_C(ARPOP_REQUEST);
    memcpy(ahdr->ar_sha, special_ethaddr, ETH_ALEN);
    /* we assume that this request come from gw, but not from DNS or TFTP */
    ahdr->ar_sha[5] = CTL_ALIAS;
    *(uint32_t *)ahdr->ar_sip = RT_H2N_U32(RT_N2H_U32(pData->special_addr.s_addr) | CTL_ALIAS);
    memset(ahdr->ar_tha, 0xff, ETH_ALEN); /*broadcast*/
    *(uint32_t *)ahdr->ar_tip = dst;
    /* warn!!! should falls in mbuf minimal size */
    m->m_len = sizeof(struct arphdr) + ETH_HLEN;
    m->m_data += ETH_HLEN;
    m->m_len -= ETH_HLEN;
    if_encap(pData, ETH_P_ARP, m, ETH_ENCAP_URG);
    LogFlowFuncLeave();
}


/* updates the arp cache
 * @note: this is helper function, slirp_arp_cache_update_or_add should be used.
 * @returns 0 - if has found and updated
 *          1 - if hasn't found.
 */
static inline int slirp_arp_cache_update(PNATState pData, uint32_t dst, const uint8_t *mac)
{
    struct arp_cache_entry *ac;
    Assert((   memcmp(mac, broadcast_ethaddr, ETH_ALEN)
            && memcmp(mac, zerro_ethaddr, ETH_ALEN)));
    LIST_FOREACH(ac, &pData->arp_cache, list)
    {
        if (ac->ip == dst)
        {
            memcpy(ac->ether, mac, ETH_ALEN);
            return 0;
        }
    }
    return 1;
}

/**
 * add entry to the arp cache
 * @note: this is helper function, slirp_arp_cache_update_or_add should be used.
 */
static inline void slirp_arp_cache_add(PNATState pData, uint32_t ip, const uint8_t *ether)
{
    struct arp_cache_entry *ac = NULL;
    Assert((   memcmp(ether, broadcast_ethaddr, ETH_ALEN)
            && memcmp(ether, zerro_ethaddr, ETH_ALEN)));
    ac = RTMemAllocZ(sizeof(struct arp_cache_entry));
    if (ac == NULL)
    {
        Log(("NAT: Can't allocate arp cache entry\n"));
        return;
    }
    ac->ip = ip;
    memcpy(ac->ether, ether, ETH_ALEN);
    LIST_INSERT_HEAD(&pData->arp_cache, ac, list);
}

/* updates or adds entry to the arp cache
 * @returns 0 - if has found and updated
 *          1 - if hasn't found.
 */
int slirp_arp_cache_update_or_add(PNATState pData, uint32_t dst, const uint8_t *mac)
{
    if (   !memcmp(mac, broadcast_ethaddr, ETH_ALEN)
        || !memcmp(mac, zerro_ethaddr, ETH_ALEN))
    {
        static bool fBroadcastEtherAddReported;
        if (!fBroadcastEtherAddReported)
        {
            LogRel(("NAT: Attempt to add pair [%RTmac:%RTnaipv4] in ARP cache was ignored\n",
                    mac, dst));
            fBroadcastEtherAddReported = true;
        }
        return 1;
    }
    if (slirp_arp_cache_update(pData, dst, mac))
        slirp_arp_cache_add(pData, dst, mac);

    return 0;
}


void slirp_set_mtu(PNATState pData, int mtu)
{
    if (mtu < 20 || mtu >= 16000)
    {
        LogRel(("NAT: MTU(%d) is out of range (20;16000] mtu forcely assigned to 1500\n", mtu));
        mtu = 1500;
    }
    /* MTU is maximum transition unit on */
    if_mtu =
    if_mru = mtu;
}

/**
 * Info handler.
 */
void slirp_info(PNATState pData, const void *pvArg, const char *pszArgs)
{
    struct socket *so, *so_next;
    struct arp_cache_entry *ac;
    struct port_forward_rule *rule;
    PCDBGFINFOHLP pHlp = (PCDBGFINFOHLP)pvArg;
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "NAT parameters: MTU=%d\n", if_mtu);
    pHlp->pfnPrintf(pHlp, "NAT TCP ports:\n");
    QSOCKET_FOREACH(so, so_next, tcp)
    /* { */
        pHlp->pfnPrintf(pHlp, " %R[natsock]\n", so);
    }

    pHlp->pfnPrintf(pHlp, "NAT UDP ports:\n");
    QSOCKET_FOREACH(so, so_next, udp)
    /* { */
        pHlp->pfnPrintf(pHlp, " %R[natsock]\n", so);
    }

    pHlp->pfnPrintf(pHlp, "NAT ARP cache:\n");
    LIST_FOREACH(ac, &pData->arp_cache, list)
    {
        pHlp->pfnPrintf(pHlp, " %RTnaipv4 %RTmac\n", ac->ip, &ac->ether);
    }

    pHlp->pfnPrintf(pHlp, "NAT rules:\n");
    LIST_FOREACH(rule, &pData->port_forward_rule_head, list)
    {
        pHlp->pfnPrintf(pHlp, " %s %d => %RTnaipv4:%d %c\n",
                        rule->proto == IPPROTO_UDP ? "UDP" : "TCP",
                        rule->host_port, rule->guest_addr.s_addr, rule->guest_port,
                        rule->activated ? ' ' : '*');
    }
}

/**
 * @note: NATState::fUseHostResolver could be changed in bootp.c::dhcp_decode
 * @note: this function is executed on GUI/VirtualBox or main/VBoxHeadless thread.
 * @note: this function can potentially race with bootp.c::dhcp_decode (except Darwin)
 */
int slirp_host_network_configuration_change_strategy_selector(const PNATState pData)
{
    if (pData->fUseHostResolverPermanent)
        return VBOX_NAT_DNS_HOSTRESOLVER;

    if (pData->fUseDnsProxy) {
#if HAVE_NOTIFICATION_FOR_DNS_UPDATE /* XXX */ && !defined(RT_OS_WINDOWS)
        /* We dont conflict with bootp.c::dhcp_decode */
        struct rcp_state rcp_state;
        int rc;

        rcp_state.rcps_flags = RCPSF_IGNORE_IPV6;
        rc = rcp_parse(&rcp_state, RESOLV_CONF_FILE);
        LogRelFunc(("NAT: rcp_parse:%Rrc old domain:%s new domain:%s\n",
                    rc, LIST_EMPTY(&pData->pDomainList)
                      ? "(null)"
                      : LIST_FIRST(&pData->pDomainList)->dd_pszDomain,
                    rcp_state.rcps_domain));
        if (   RT_FAILURE(rc)
            || LIST_EMPTY(&pData->pDomainList))
            return VBOX_NAT_DNS_DNSPROXY;

        if (   rcp_state.rcps_domain
            && strcmp(rcp_state.rcps_domain, LIST_FIRST(&pData->pDomainList)->dd_pszDomain) == 0)
            return VBOX_NAT_DNS_DNSPROXY;
        else
            return VBOX_NAT_DNS_EXTERNAL;
#else
        /* copy domain name */
        /* domain only compare with coy version */
        return VBOX_NAT_DNS_DNSPROXY;
#endif
    }
    return VBOX_NAT_DNS_EXTERNAL;
}
