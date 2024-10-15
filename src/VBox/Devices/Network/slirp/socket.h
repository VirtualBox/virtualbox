/* $Id: socket.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - socket handling (declarations/defines).
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
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

/* MINE */

#ifndef _SLIRP_SOCKET_H_
#define _SLIRP_SOCKET_H_

#define SO_EXPIRE 240000
#define SO_EXPIREFAST 10000

/*
 * Our socket structure
 */

struct socket
{
    struct socket   *so_next;
    struct socket   *so_prev;    /* For a linked list of sockets */

#if !defined(RT_OS_WINDOWS)
    int s;                       /* The actual socket */
#else
    union {
            int s;
            HANDLE sh;
    };
    uint64_t so_icmp_id; /* XXX: hack */
    uint64_t so_icmp_seq; /* XXX: hack */
#endif

    /* XXX union these with not-yet-used sbuf params */
    struct mbuf     *so_m;       /* Pointer to the original SYN packet,
                                  * for non-blocking connect()'s, and
                                  * PING reply's */
    struct tcpiphdr *so_ti;      /* Pointer to the original ti within
                                  * so_mconn, for non-blocking connections */
    uint8_t         *so_ohdr;    /* unmolested IP header of the datagram in so_m */
    caddr_t         so_optp;     /* tcp options in so_m */
    int             so_optlen;   /* length of options in so_m */
    int             so_urgc;
    struct in_addr  so_faddr;    /* foreign host table entry */
    struct in_addr  so_laddr;    /* local host table entry */
    u_int16_t       so_fport;    /* foreign port */
    u_int16_t       so_lport;    /* local port */
    u_int16_t       so_hlport; /* host local port */
    struct in_addr  so_hladdr;    /* local host addr */

    u_int8_t        so_iptos;    /* Type of service */

    uint8_t         so_sottl;    /* cached socket's IP_TTL option */
    uint8_t         so_sotos;    /* cached socket's IP_TOS option */
    int8_t          so_sodf;     /* cached socket's DF option */

    u_char          so_type;     /* Type of socket, UDP or TCP */
    int             so_state;    /* internal state flags SS_*, below */

    struct tcpcb    *so_tcpcb;   /* pointer to TCP protocol control block */
    u_int           so_expire;   /* When the socket will expire */

    int             so_queued;   /* Number of packets queued from this socket */
    int             so_nqueued;  /* Number of packets queued in a row
                                  * Used to determine when to "downgrade" a session
                                  * from fastq to batchq */

    struct sbuf     so_rcv;      /* Receive buffer */
    struct sbuf     so_snd;      /* Send buffer */
#ifndef RT_OS_WINDOWS
    int so_poll_index;
#endif /* !RT_OS_WINDOWS */
    /*
     * FD_CLOSE/POLLHUP event has been occurred on socket
     */
    int so_close;

    void (* so_timeout)(PNATState pData, struct socket *so, void *arg);
    void *so_timeout_arg;

    /** These flags (''fUnderPolling'' and ''fShouldBeRemoved'') introduced to
     *  to let polling routine gain control over freeing socket whatever level of
     *  TCP/IP initiated socket releasing.
     *  So polling routine when start processing socket alter it's state to
     *  ''fUnderPolling'' to 1, and clean (set to 0) when it finish.
     *  When polling routine calls functions it should be ensure on return,
     *  whether ''fShouldBeRemoved'' set or not, and depending on state call
     *  ''sofree'' or continue socket processing.
     *  On ''fShouldBeRemoved'' equal to 1, polling routine should call ''sofree'',
     *  clearing ''fUnderPolling'' to do real freeng of the socket and removing from
     *  the queue.
     *  @todo: perhaps, to simplefy the things we need some helper function.
     *  @note: it's used like a bool, I use 'int' to avoid compiler warnings
     *  appearing if [-Wc++-compat] used.
     */
    int fUnderPolling;
    /** This flag used by ''sofree'' function in following manner
     *
     *  fUnderPolling = 1, then we don't remove socket from the queue, just
     *  alter value ''fShouldBeRemoved'' to 1, else we do removal.
     */
    int fShouldBeRemoved;
};

# define SOCKET_LOCK(so) do {} while (0)
# define SOCKET_UNLOCK(so) do {} while (0)
# define SOCKET_LOCK_CREATE(so) do {} while (0)
# define SOCKET_LOCK_DESTROY(so) do {} while (0)

/*
 * Socket state bits. (peer means the host on the Internet,
 * local host means the host on the other end of the modem)
 */
#define SS_NOFDREF              0x001   /* No fd reference */

#define SS_ISFCONNECTING        0x002   /* Socket is connecting to peer (non-blocking connect()'s) */
#define SS_ISFCONNECTED         0x004   /* Socket is connected to peer */
#define SS_FCANTRCVMORE         0x008   /* Socket can't receive more from peer (for half-closes) */
#define SS_FCANTSENDMORE        0x010   /* Socket can't send more to peer (for half-closes) */
/* #define SS_ISFDISCONNECTED   0x020*/ /* Socket has disconnected from peer, in 2MSL state */
#define SS_FWDRAIN              0x040   /* We received a FIN, drain data and set SS_FCANTSENDMORE */

/* #define SS_CTL               0x080 */
#define SS_FACCEPTCONN          0x100   /* Socket is accepting connections from a host on the internet */
#define SS_FACCEPTONCE          0x200   /* If set, the SS_FACCEPTCONN socket will die after one accept */

extern struct socket tcb;

#if defined(DECLARE_IOVEC) && !defined(HAVE_READV)
# if !defined(RT_OS_WINDOWS)
struct iovec
{
    char *iov_base;
    size_t iov_len;
};
# else
/* make it congruent with WSABUF */
struct iovec
{
    ULONG iov_len;
    char *iov_base;
};
# endif
#endif

void so_init (void);
struct socket * solookup (struct socket *, struct in_addr, u_int, struct in_addr, u_int);
struct socket * socreate (void);
void sofree (PNATState, struct socket *);
int sobind(PNATState, struct socket *);
int soread (PNATState, struct socket *);
void sorecvoob (PNATState, struct socket *);
int sosendoob (struct socket *);
int sowrite (PNATState, struct socket *);
void sorecvfrom (PNATState, struct socket *);
int sosendto (PNATState, struct socket *, struct mbuf *);
struct socket * solisten (PNATState, u_int32_t, u_int, u_int32_t, u_int, int);
void sorwakeup (struct socket *);
void sowwakeup (struct socket *);
void soisfconnecting (register struct socket *);
void soisfconnected (register struct socket *);
int sofcantrcvmore (struct  socket *);
void sofcantsendmore (struct socket *);
void soisfdisconnected (struct socket *);
void sofwdrain (struct socket *);

static inline int soIgnorableErrorCode(int iErrorCode)
{
    return (   iErrorCode == EINPROGRESS
            || iErrorCode == EAGAIN
            || iErrorCode == EWOULDBLOCK);
}

#endif /* _SOCKET_H_ */
