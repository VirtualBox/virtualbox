/* $Id: debug.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - debug helpers.
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
 * Copyright (c) 1995 Danny Gasparovski.
 * Portions copyright (c) 2000 Kelly Price.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#include <slirp.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/critsect.h>
#include "zone.h"

#ifdef DEBUG
void dump_packet(void *, int);
#endif

#ifndef STRINGIFY
# define STRINGIFY(x) #x
#endif

static char *g_apszTcpStates[TCP_NSTATES] =
{
    STRINGIFY(TCPS_CLOSED),
    STRINGIFY(TCPS_LISTEN),
    STRINGIFY(TCPS_SYN_SENT),
    STRINGIFY(TCPS_SYN_RECEIVED),
    STRINGIFY(TCPS_ESTABLISHED),
    STRINGIFY(TCPS_CLOSE_WAIT),
    STRINGIFY(TCPS_FIN_WAIT_1),
    STRINGIFY(TCPS_CLOSING),
    STRINGIFY(TCPS_LAST_ACK),
    STRINGIFY(TCPS_FIN_WAIT_2),
    STRINGIFY(TCPS_TIME_WAIT)
};

typedef struct DEBUGSTRSOCKETSTATE
{
    uint32_t u32SocketState;
    const char *pcszSocketStateName;
} DEBUGSTRSOCKETSTATE;

#define DEBUGSTRSOCKETSTATE_HELPER(x) {(x), #x}

static DEBUGSTRSOCKETSTATE g_apszSocketStates[8] =
{
    DEBUGSTRSOCKETSTATE_HELPER(SS_NOFDREF),
    DEBUGSTRSOCKETSTATE_HELPER(SS_ISFCONNECTING),
    DEBUGSTRSOCKETSTATE_HELPER(SS_ISFCONNECTED),
    DEBUGSTRSOCKETSTATE_HELPER(SS_FCANTRCVMORE),
    DEBUGSTRSOCKETSTATE_HELPER(SS_FCANTSENDMORE),
    DEBUGSTRSOCKETSTATE_HELPER(SS_FWDRAIN),
    DEBUGSTRSOCKETSTATE_HELPER(SS_FACCEPTCONN),
    DEBUGSTRSOCKETSTATE_HELPER(SS_FACCEPTONCE),
};

static DEBUGSTRSOCKETSTATE g_aTcpFlags[] =
{
    DEBUGSTRSOCKETSTATE_HELPER(TH_FIN),
    DEBUGSTRSOCKETSTATE_HELPER(TH_SYN),
    DEBUGSTRSOCKETSTATE_HELPER(TH_RST),
    DEBUGSTRSOCKETSTATE_HELPER(TH_PUSH),
    DEBUGSTRSOCKETSTATE_HELPER(TH_ACK),
    DEBUGSTRSOCKETSTATE_HELPER(TH_URG),
};

/*
 * Dump a packet in the same format as tcpdump -x
 */
#ifdef DEBUG
void
dump_packet(void *dat, int n)
{
    Log(("nat: PACKET DUMPED:\n%.*Rhxd\n", n, dat));
}
#endif

#ifdef LOG_ENABLED
static void
lprint(const char *pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    RTLogPrintfV(pszFormat, args);
    va_end(args);
}

void
ipstats(PNATState pData)
{
    lprint("\n");

    lprint("IP stats:\n");
    lprint("  %6d total packets received (%d were unaligned)\n",
           ipstat.ips_total, ipstat.ips_unaligned);
    lprint("  %6d with incorrect version\n", ipstat.ips_badvers);
    lprint("  %6d with bad header checksum\n", ipstat.ips_badsum);
    lprint("  %6d with length too short (len < sizeof(iphdr))\n", ipstat.ips_tooshort);
    lprint("  %6d with length too small (len < ip->len)\n", ipstat.ips_toosmall);
    lprint("  %6d with bad header length\n", ipstat.ips_badhlen);
    lprint("  %6d with bad packet length\n", ipstat.ips_badlen);
    lprint("  %6d fragments received\n", ipstat.ips_fragments);
    lprint("  %6d fragments dropped\n", ipstat.ips_fragdropped);
    lprint("  %6d fragments timed out\n", ipstat.ips_fragtimeout);
    lprint("  %6d packets reassembled ok\n", ipstat.ips_reassembled);
    lprint("  %6d outgoing packets fragmented\n", ipstat.ips_fragmented);
    lprint("  %6d total outgoing fragments\n", ipstat.ips_ofragments);
    lprint("  %6d with bad protocol field\n", ipstat.ips_noproto);
    lprint("  %6d total packets delivered\n", ipstat.ips_delivered);
}

void
tcpstats(PNATState pData)
{
    lprint("\n");

    lprint("TCP stats:\n");

    lprint("  %6d packets sent\n", tcpstat.tcps_sndtotal);
    lprint("          %6d data packets (%d bytes)\n",
            tcpstat.tcps_sndpack, tcpstat.tcps_sndbyte);
    lprint("          %6d data packets retransmitted (%d bytes)\n",
            tcpstat.tcps_sndrexmitpack, tcpstat.tcps_sndrexmitbyte);
    lprint("          %6d ack-only packets (%d delayed)\n",
            tcpstat.tcps_sndacks, tcpstat.tcps_delack);
    lprint("          %6d URG only packets\n", tcpstat.tcps_sndurg);
    lprint("          %6d window probe packets\n", tcpstat.tcps_sndprobe);
    lprint("          %6d window update packets\n", tcpstat.tcps_sndwinup);
    lprint("          %6d control (SYN/FIN/RST) packets\n", tcpstat.tcps_sndctrl);
    lprint("          %6d times tcp_output did nothing\n", tcpstat.tcps_didnuttin);

    lprint("  %6d packets received\n", tcpstat.tcps_rcvtotal);
    lprint("          %6d acks (for %d bytes)\n",
            tcpstat.tcps_rcvackpack, tcpstat.tcps_rcvackbyte);
    lprint("          %6d duplicate acks\n", tcpstat.tcps_rcvdupack);
    lprint("          %6d acks for unsent data\n", tcpstat.tcps_rcvacktoomuch);
    lprint("          %6d packets received in sequence (%d bytes)\n",
            tcpstat.tcps_rcvpack, tcpstat.tcps_rcvbyte);
    lprint("          %6d completely duplicate packets (%d bytes)\n",
            tcpstat.tcps_rcvduppack, tcpstat.tcps_rcvdupbyte);

    lprint("          %6d packets with some duplicate data (%d bytes duped)\n",
            tcpstat.tcps_rcvpartduppack, tcpstat.tcps_rcvpartdupbyte);
    lprint("          %6d out-of-order packets (%d bytes)\n",
            tcpstat.tcps_rcvoopack, tcpstat.tcps_rcvoobyte);
    lprint("          %6d packets of data after window (%d bytes)\n",
            tcpstat.tcps_rcvpackafterwin, tcpstat.tcps_rcvbyteafterwin);
    lprint("          %6d window probes\n", tcpstat.tcps_rcvwinprobe);
    lprint("          %6d window update packets\n", tcpstat.tcps_rcvwinupd);
    lprint("          %6d packets received after close\n", tcpstat.tcps_rcvafterclose);
    lprint("          %6d discarded for bad checksums\n", tcpstat.tcps_rcvbadsum);
    lprint("          %6d discarded for bad header offset fields\n",
            tcpstat.tcps_rcvbadoff);

    lprint("  %6d connection requests\n", tcpstat.tcps_connattempt);
    lprint("  %6d connection accepts\n", tcpstat.tcps_accepts);
    lprint("  %6d connections established (including accepts)\n", tcpstat.tcps_connects);
    lprint("  %6d connections closed (including %d drop)\n",
            tcpstat.tcps_closed, tcpstat.tcps_drops);
    lprint("  %6d embryonic connections dropped\n", tcpstat.tcps_conndrops);
    lprint("  %6d segments we tried to get rtt (%d succeeded)\n",
            tcpstat.tcps_segstimed, tcpstat.tcps_rttupdated);
    lprint("  %6d retransmit timeouts\n", tcpstat.tcps_rexmttimeo);
    lprint("          %6d connections dropped by rxmt timeout\n",
            tcpstat.tcps_timeoutdrop);
    lprint("  %6d persist timeouts\n", tcpstat.tcps_persisttimeo);
    lprint("  %6d keepalive timeouts\n", tcpstat.tcps_keeptimeo);
    lprint("          %6d keepalive probes sent\n", tcpstat.tcps_keepprobe);
    lprint("          %6d connections dropped by keepalive\n", tcpstat.tcps_keepdrops);
    lprint("  %6d correct ACK header predictions\n", tcpstat.tcps_predack);
    lprint("  %6d correct data packet header predictions\n", tcpstat.tcps_preddat);
    lprint("  %6d TCP cache misses\n", tcpstat.tcps_socachemiss);

/*  lprint("    Packets received too short:         %d\n", tcpstat.tcps_rcvshort); */
/*  lprint("    Segments dropped due to PAWS:       %d\n", tcpstat.tcps_pawsdrop); */

}

void
udpstats(PNATState pData)
{
    lprint("\n");

    lprint("UDP stats:\n");
    lprint("  %6d datagrams received\n", udpstat.udps_ipackets);
    lprint("  %6d with packets shorter than header\n", udpstat.udps_hdrops);
    lprint("  %6d with bad checksums\n", udpstat.udps_badsum);
    lprint("  %6d with data length larger than packet\n", udpstat.udps_badlen);
    lprint("  %6d UDP socket cache misses\n", udpstat.udpps_pcbcachemiss);
    lprint("  %6d datagrams sent\n", udpstat.udps_opackets);
}

void
icmpstats(PNATState pData)
{
    lprint("\n");
    lprint("ICMP stats:\n");
    lprint("  %6d ICMP packets received\n", icmpstat.icps_received);
    lprint("  %6d were too short\n", icmpstat.icps_tooshort);
    lprint("  %6d with bad checksums\n", icmpstat.icps_checksum);
    lprint("  %6d with type not supported\n", icmpstat.icps_notsupp);
    lprint("  %6d with bad type feilds\n", icmpstat.icps_badtype);
    lprint("  %6d ICMP packets sent in reply\n", icmpstat.icps_reflect);
}

void
mbufstats(PNATState pData)
{
    /*
     * (vvl) this static code can't work with mbuf zone anymore
     * @todo: make statistic correct
     */
    NOREF(pData);
}

void
sockstats(PNATState pData)
{
    char buff[256];
    size_t n;
    struct socket *so, *so_next;

    lprint("\n");

    lprint(
           "Proto[state]     Sock     Local Address, Port  Remote Address, Port RecvQ SendQ\n");

    QSOCKET_FOREACH(so, so_next, tcp)
    /* { */
        n = RTStrPrintf(buff, sizeof(buff), "tcp[%s]", so->so_tcpcb?tcpstates[so->so_tcpcb->t_state]:"NONE");
        while (n < 17)
            buff[n++] = ' ';
        buff[17] = 0;
        lprint("%s %3d   %15s %5d ",
               buff, so->s, inet_ntoa(so->so_laddr), RT_N2H_U16(so->so_lport));
        lprint("%15s %5d %5d %5d\n",
                inet_ntoa(so->so_faddr), RT_N2H_U16(so->so_fport),
                SBUF_LEN(&so->so_rcv), SBUF_LEN(&so->so_snd));
    LOOP_LABEL(tcp, so, so_next);
    }

    QSOCKET_FOREACH(so, so_next, udp)
    /* { */
        n = RTStrPrintf(buff, sizeof(buff), "udp[%d sec]", (so->so_expire - curtime) / 1000);
        while (n < 17)
            buff[n++] = ' ';
        buff[17] = 0;
        lprint("%s %3d  %15s %5d  ",
                buff, so->s, inet_ntoa(so->so_laddr), RT_N2H_U16(so->so_lport));
        lprint("%15s %5d %5d %5d\n",
                inet_ntoa(so->so_faddr), RT_N2H_U16(so->so_fport),
                SBUF_LEN(&so->so_rcv), SBUF_LEN(&so->so_snd));
    LOOP_LABEL(udp, so, so_next);
    }
}
#endif

static DECLCALLBACK(size_t)
printSocket(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
             const char *pszType, void const *pvValue,
             int cchWidth, int cchPrecision, unsigned fFlags,
             void *pvUser)
{
    struct socket *so = (struct socket*)pvValue;
    PNATState pData = (PNATState)pvUser;
    size_t cb = 0;

    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    Assert(pData);

    AssertReturn(strcmp(pszType, "natsock") == 0, 0);

    if (so == NULL)
        return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                "socket is null");
    if (so->s == -1)
        return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                "socket(%d)", so->s);

    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
            "socket %d", so->s);

    if (so->so_type == IPPROTO_TCP)
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                " (tcp)");
    else if (so->so_type == IPPROTO_UDP)
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                " (udp)");
    else
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                " (proto %u)", so->so_type);

    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
            " exp. in %d"
            " state=%R[natsockstate]"
            "%s" /* fUnderPolling */
            "%s" /* fShouldBeRemoved */
            " f_(addr:port)=%RTnaipv4:%d"
            " l_(addr:port)=%RTnaipv4:%d",
            so->so_expire ? so->so_expire - curtime : 0,
            so->so_state,
            so->fUnderPolling ? " fUnderPolling" : "",
            so->fShouldBeRemoved ? " fShouldBeRemoved" : "",
            so->so_faddr.s_addr,
            RT_N2H_U16(so->so_fport),
            so->so_laddr.s_addr,
            RT_N2H_U16(so->so_lport));

    if (so->s != -1)
    {
        struct sockaddr addr;
        socklen_t socklen;
        int status;

        socklen = sizeof(addr);
        status = getsockname(so->s, &addr, &socklen);

        if (status != 0)
        {
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                    " (getsockname failed)");
        }
        else if (addr.sa_family != AF_INET)
        {
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                    " (unexpected address family %d)",
                    addr.sa_family);
        }
        else
        {
            struct sockaddr_in *in_addr = (struct sockaddr_in *)&addr;
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,
                    " name=%RTnaipv4:%d",
                    in_addr->sin_addr.s_addr,
                    RT_N2H_U16(in_addr->sin_port));
        }
    }
    return cb;
}

static DECLCALLBACK(size_t)
printNATSocketState(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
             const char *pszType, void const *pvValue,
             int cchWidth, int cchPrecision, unsigned fFlags,
             void *pvUser)
{
    uint32_t u32SocketState = (uint32_t)(uintptr_t)pvValue;
    int idxNATState = 0;
    bool fFirst = true;
    size_t cbReturn = 0;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(strcmp(pszType, "natsockstate") == 0, 0);

    for (idxNATState = 0; idxNATState < RT_ELEMENTS(g_apszSocketStates); ++idxNATState)
    {
        if (u32SocketState & g_apszSocketStates[idxNATState].u32SocketState)
        {
            if (fFirst)
            {
                cbReturn += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, g_apszSocketStates[idxNATState].pcszSocketStateName);
                fFirst = false;
            }
            else
                cbReturn += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "|%s", g_apszSocketStates[idxNATState].pcszSocketStateName);
        }
    }

    if (!cbReturn)
        return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "[unknown state %RX32]", u32SocketState);

    return cbReturn;
}

/**
 *  Print callback dumping TCP Control Block in terms of RFC 793.
 */
static DECLCALLBACK(size_t)
printTcpcbRfc793(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cb = 0;
    const struct tcpcb *tp = (const struct tcpcb *)pvValue;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "tcpcb793") == 0, 0);
    if (tp)
    {
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "TCB793[ state:%R[tcpstate] SND(UNA: %x, NXT: %x, UP: %x, WND: %x, WL1:%x, WL2:%x, ISS:%x), ",
                          tp->t_state, tp->snd_una, tp->snd_nxt, tp->snd_up, tp->snd_wnd, tp->snd_wl1, tp->snd_wl2, tp->iss);
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "RCV(WND: %x, NXT: %x, UP: %x, IRS:%x)]", tp->rcv_wnd, tp->rcv_nxt, tp->rcv_up, tp->irs);
    }
    else
    {
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "TCB793[ NULL ]");
    }
    return cb;
}
/*
 * Prints TCP segment in terms of RFC 793.
 */
static DECLCALLBACK(size_t)
printTcpSegmentRfc793(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cb = 0;
    const struct tcpiphdr *ti = (const struct tcpiphdr *)pvValue;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "tcpseg793") == 0 && ti, 0);
    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "SEG[ACK: %x, SEQ: %x, LEN: %x, WND: %x, UP: %x]",
                      ti->ti_ack, ti->ti_seq, ti->ti_len, ti->ti_win, ti->ti_urp);
    return cb;
}

/*
 * Prints TCP state
 */
static DECLCALLBACK(size_t)
printTcpState(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cb = 0;
    const int idxTcpState = (int)(uintptr_t)pvValue;
    char *pszTcpStateName = (idxTcpState >= 0 && idxTcpState < TCP_NSTATES) ? g_apszTcpStates[idxTcpState] : "TCPS_INVALIDE_STATE";
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "tcpstate") == 0, 0);
    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "%s", pszTcpStateName);
    return cb;
}

/*
 * Prints TCP flags
 */
static DECLCALLBACK(size_t)
printTcpFlags(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cbPrint = 0;
    uint32_t u32TcpFlags = (uint32_t)(uintptr_t)pvValue;
    bool fSingleValue = true;
    int idxTcpFlags = 0;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "tcpflags") == 0, 0);
    cbPrint += RTStrFormat(pfnOutput,
                           pvArgOutput,
                           NULL,
                           0,
                           "tcpflags: %RX8 [", (uint8_t)u32TcpFlags);
    for (idxTcpFlags = 0; idxTcpFlags < RT_ELEMENTS(g_aTcpFlags); ++idxTcpFlags)
    {
        if (u32TcpFlags & g_aTcpFlags[idxTcpFlags].u32SocketState)
        {
            cbPrint += RTStrFormat(pfnOutput,
                                   pvArgOutput,
                                   NULL,
                                   0,
                                   fSingleValue ? "%s(%RX8)" : "|%s(%RX8)",
                                   g_aTcpFlags[idxTcpFlags].pcszSocketStateName,
                                   (uint8_t)g_aTcpFlags[idxTcpFlags].u32SocketState);
            fSingleValue = false;
        }
    }
    cbPrint += RTStrFormat(pfnOutput,
                           pvArgOutput,
                           NULL,
                           0,
                           "]");
    return cbPrint;
}

/*
 * Prints sbuf state
 */
static DECLCALLBACK(size_t)
printSbuf(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cb = 0;
    const struct sbuf *sb = (struct sbuf *)pvValue;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "sbuf") == 0, 0);
    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "[sbuf:%p cc:%d, datalen:%d, wprt:%p, rptr:%p data:%p]",
                      sb, sb->sb_cc, sb->sb_datalen, sb->sb_wptr, sb->sb_rptr, sb->sb_data);
    return cb;
}

/*
 * Prints zone state
 */
static DECLCALLBACK(size_t)
printMbufZone(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cb = 0;
    const uma_zone_t zone = (const uma_zone_t)pvValue;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "mzone") == 0, 0);
    if (!zone)
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "[zone:NULL]");
    else
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "[zone:%p name:%s, master_zone:%R[mzone]]",
                          zone, zone->name, zone->master_zone);
    return cb;
}

/*
 * Prints zone's item state
 */
static DECLCALLBACK(size_t)
printMbufZoneItem(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                 const char *pszType, void const *pvValue,
                 int cchWidth, int cchPrecision, unsigned fFlags,
                 void *pvUser)
{
    size_t cb = 0;
    const struct item *it = (const struct item *)pvValue;
    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);
    AssertReturn(RTStrCmp(pszType, "mzoneitem") == 0, 0);
    if (!it)
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "[item:NULL]");
    else
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "[iptem:%p ref_count:%d, zone:%R[mzone]]",
                          it, it->ref_count, it->zone);
    return cb;
}

static DECLCALLBACK(size_t)
print_networkevents(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                    const char *pszType, void const *pvValue,
                    int cchWidth, int cchPrecision, unsigned fFlags,
                    void *pvUser)
{
    size_t cb = 0;
#ifdef RT_OS_WINDOWS
    WSANETWORKEVENTS *pNetworkEvents = (WSANETWORKEVENTS*)pvValue;
    bool fDelim = false;
#endif

    NOREF(cchWidth);
    NOREF(cchPrecision);
    NOREF(fFlags);
    NOREF(pvUser);

#ifdef RT_OS_WINDOWS
    AssertReturn(strcmp(pszType, "natwinnetevents") == 0, 0);

    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "events=%02x (",
            pNetworkEvents->lNetworkEvents);
# define DO_BIT(bit) \
    if (pNetworkEvents->lNetworkEvents & FD_ ## bit)                        \
    {                                                                       \
        cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0,                  \
              "%s" #bit "(%d)", fDelim ? "," : "",                          \
              pNetworkEvents->iErrorCode[FD_ ## bit ## _BIT]);              \
        fDelim = true;                                                      \
    }
    DO_BIT(READ);
    DO_BIT(WRITE);
    DO_BIT(OOB);
    DO_BIT(ACCEPT);
    DO_BIT(CONNECT);
    DO_BIT(CLOSE);
    DO_BIT(QOS);
# undef DO_BIT
    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, ")");
#else
    NOREF(pfnOutput);
    NOREF(pvArgOutput);
    NOREF(pszType);
    NOREF(pvValue);
#endif
    return cb;
}

#if 0
/*
 * Debugging
 */
int errno_func(const char *file, int line)
{
    int err = WSAGetLastError();
    LogRel(("errno=%d (%s:%d)\n", err, file, line));
    return err;
}
#endif

int
debug_init(PNATState pData)
{
    int rc = VINF_SUCCESS;

    static int g_fFormatRegistered;

    if (!g_fFormatRegistered)
    {

        rc = RTStrFormatTypeRegister("natsock", printSocket, pData);            AssertRC(rc);
        rc = RTStrFormatTypeRegister("natsockstate", printNATSocketState, NULL);            AssertRC(rc);
        rc = RTStrFormatTypeRegister("natwinnetevents",
                                     print_networkevents, NULL);                AssertRC(rc);
        rc = RTStrFormatTypeRegister("tcpcb793", printTcpcbRfc793, NULL);       AssertRC(rc);
        rc = RTStrFormatTypeRegister("tcpseg793", printTcpSegmentRfc793, NULL); AssertRC(rc);
        rc = RTStrFormatTypeRegister("tcpstate", printTcpState, NULL);          AssertRC(rc);
        rc = RTStrFormatTypeRegister("tcpflags", printTcpFlags, NULL);          AssertRC(rc);
        rc = RTStrFormatTypeRegister("sbuf", printSbuf, NULL);                  AssertRC(rc);
        rc = RTStrFormatTypeRegister("mzone", printMbufZone, NULL);             AssertRC(rc);
        rc = RTStrFormatTypeRegister("mzoneitem", printMbufZoneItem, NULL);     AssertRC(rc);
        g_fFormatRegistered = 1;
    }

    return rc;
}
