/* $Id: slirp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - slirp (declarations/defines).
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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <VBox/vmm/stam.h>

#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
# include <iprt/win/ws2tcpip.h>
typedef int socklen_t;
#endif
#ifdef RT_OS_OS2 /* temporary workaround, see ticket #127 */
# define mbstat mbstat_os2
# include <sys/socket.h>
# undef mbstat
typedef int socklen_t;
#endif

#define CONFIG_QEMU

#ifdef DEBUG
# undef  DEBUG
# define DEBUG 1
#endif

#ifndef CONFIG_QEMU
# include "version.h"
#endif
#define LOG_GROUP LOG_GROUP_DRV_NAT
#include <VBox/log.h>
#include <iprt/mem.h>
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <io.h>
#endif
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/dir.h>
#include <iprt/rand.h>
#include <iprt/net.h>
#include <VBox/types.h>

#undef malloc
#define malloc          dont_use_malloc
#undef free
#define free            dont_use_free
#undef realloc
#define realloc         dont_use_realloc
#undef strdup
#define strdup          dont_use_strdup

#include "slirp_config.h"

#ifdef RT_OS_WINDOWS

# ifndef _MSC_VER
#  include <inttypes.h>
# endif


# include <sys/timeb.h>
# include <iprt/win/iphlpapi.h>

/* We don't want the errno.h versions of these error defines. */
# if defined(_MSC_VER) && _MSC_VER >= 1600
#  include <errno.h>
#  undef ECONNREFUSED
#  undef ECONNRESET
#  undef EHOSTDOWN
#  undef EHOSTUNREACH
#  undef EINPROGRESS
#  undef ENETDOWN
#  undef ENETUNREACH
#  undef ENOTCONN
#  undef ESHUTDOWN
#  undef EWOULDBLOCK
# endif
# define ECONNREFUSED WSAECONNREFUSED
# define ECONNRESET WSAECONNRESET
# define EHOSTDOWN WSAEHOSTDOWN
# define EHOSTUNREACH WSAEHOSTUNREACH
# define EINPROGRESS WSAEINPROGRESS
# define ENETDOWN WSAENETDOWN
# define ENETUNREACH WSAENETUNREACH
# define ENOTCONN WSAENOTCONN
# define ESHUTDOWN WSAESHUTDOWN
# define EWOULDBLOCK WSAEWOULDBLOCK

/* standard names for the shutdown() "how" argument */
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;

#else /* !RT_OS_WINDOWS */

# define ioctlsocket ioctl
# define closesocket(s) close(s)
# define O_BINARY 0

#endif /* !RT_OS_WINDOWS */

#if defined(RT_OS_WINDOWS) || defined (RT_OS_SOLARIS)
typedef uint64_t u_int64_t;
typedef char *caddr_t;
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_BITYPES_H
# include <sys/bitypes.h>
#endif

#ifdef _MSC_VER
# include <time.h>
#else /* !_MSC_VER */
# include <sys/time.h>
#endif /* !_MSC_VER */

#ifdef NEED_TYPEDEFS
typedef char int8_t;
typedef unsigned char u_int8_t;

# if SIZEOF_SHORT == 2
    typedef short int16_t;
    typedef unsigned short u_int16_t;
# else
#  if SIZEOF_INT == 2
    typedef int int16_t;
    typedef unsigned int u_int16_t;
#  else
    #error Cannot find a type with sizeof() == 2
#  endif
# endif

# if SIZEOF_SHORT == 4
   typedef short int32_t;
   typedef unsigned short u_int32_t;
# else
#  if SIZEOF_INT == 4
    typedef int int32_t;
    typedef unsigned int u_int32_t;
#  else
    #error Cannot find a type with sizeof() == 4
#  endif
# endif
#endif /* NEED_TYPEDEFS */

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#include <errno.h>


#ifndef HAVE_MEMMOVE
# define memmove(x, y, z) bcopy(y, x, z)
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifndef HAVE_SYS_TIME_H
#  define HAVE_SYS_TIME_H 0
# endif
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#else
# include <strings.h>
#endif

#ifndef RT_OS_WINDOWS
# include <sys/uio.h>
#endif

#ifndef RT_OS_WINDOWS
# include <netinet/in.h>
# include <arpa/inet.h>
#endif

#ifdef GETTIMEOFDAY_ONE_ARG
# define gettimeofday(x, y) gettimeofday(x)
#endif

#ifndef HAVE_INET_ATON
int inet_aton (const char *cp, struct in_addr *ia);
#endif

#include <fcntl.h>
#ifndef NO_UNIX_SOCKETS
# include <sys/un.h>
#endif
#include <signal.h>
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif
#ifndef RT_OS_WINDOWS
# include <sys/socket.h>
#endif

#if defined(HAVE_SYS_IOCTL_H)
# include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

#if defined(__STDC__) || defined(_MSC_VER)
# include <stdarg.h>
#else
# include <varargs.h>
#endif

#include <sys/stat.h>

/* Avoid conflicting with the libc insque() and remque(), which
 * have different prototypes. */
#define insque slirp_insque
#define remque slirp_remque

#ifdef HAVE_SYS_STROPTS_H
# include <sys/stropts.h>
#endif

#include "libslirp.h"

#include "debug.h"

#include "ip.h"
#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_var.h"
#include "tcpip.h"
#include "udp.h"
#include "icmp_var.h"
#include "mbuf.h"
#include "if.h"
#include "sbuf.h"
#include "socket.h"
#include "main.h"
#include "misc.h"
#include "ctl.h"
#include "bootp.h"
#include "tftp.h"

#include "slirp_state.h"
#include "slirp_dns.h"

#undef PVM /* XXX Mac OS X hack */

#ifndef NULL
# define NULL (void *)0
#endif

void if_start (PNATState);

#ifndef HAVE_INDEX
 char *index (const char *, int);
#endif

#ifndef HAVE_GETHOSTID
 long gethostid (void);
#endif

#ifndef RT_OS_WINDOWS
#include <netdb.h>
#endif

#include "dnsproxy/dnsproxy.h"

#define DEFAULT_BAUD 115200

int get_dns_addr(PNATState pData);

/* cksum.c */
typedef uint16_t u_short;
typedef unsigned int u_int;
#include "in_cksum.h"

/* if.c */
void if_init (PNATState);
void if_output (PNATState, struct socket *, struct mbuf *);

/* ip_input.c */
void ip_init (PNATState);
void ip_input (PNATState, struct mbuf *);
struct mbuf * ip_reass (PNATState, register struct mbuf *);
void ip_freef (PNATState, struct ipqhead *, struct ipq_t *);
void ip_slowtimo (PNATState);
void ip_stripoptions (register struct mbuf *, struct mbuf *);

/* ip_output.c */
int ip_output (PNATState, struct socket *, struct mbuf *);
int ip_output0 (PNATState, struct socket *, struct mbuf *, int urg);

/* tcp_input.c */
int tcp_reass (PNATState, struct tcpcb *, struct tcphdr *, int *, struct mbuf *);
void tcp_input (PNATState, register struct mbuf *, int, struct socket *);
void tcp_fconnect_failed(PNATState, struct socket *, int);
void tcp_dooptions (PNATState, struct tcpcb *, u_char *, int, struct tcpiphdr *);
void tcp_xmit_timer (PNATState, register struct tcpcb *, int);
int tcp_mss (PNATState, register struct tcpcb *, u_int);

/* tcp_output.c */
int tcp_output (PNATState, register struct tcpcb *);
void tcp_setpersist (register struct tcpcb *);

/* tcp_subr.c */
void tcp_init (PNATState);
void tcp_template (struct tcpcb *);
void tcp_respond (PNATState, struct tcpcb *, register struct tcpiphdr *, register struct mbuf *, tcp_seq, tcp_seq, int);
struct tcpcb * tcp_newtcpcb (PNATState, struct socket *);
struct tcpcb * tcp_close (PNATState, register struct tcpcb *);
void tcp_drain (void);
void tcp_sockclosed (PNATState, struct tcpcb *);
int tcp_fconnect (PNATState, struct socket *);
void tcp_connect (PNATState, struct socket *);
int tcp_attach (PNATState, struct socket *);
u_int8_t tcp_tos (struct socket *);
int tcp_ctl (PNATState, struct socket *);
struct tcpcb *tcp_drop(PNATState, struct tcpcb *tp, int err);

/* hostres.c */
struct mbuf *hostresolver(PNATState, struct mbuf *, uint32_t src, uint16_t sport);

/*slirp.c*/
void slirp_arp_who_has(PNATState pData, uint32_t dst);
int slirp_arp_cache_update_or_add(PNATState pData, uint32_t dst, const uint8_t *mac);
int slirp_init_dns_list(PNATState pData);
void slirp_release_dns_list(PNATState pData);
#define MIN_MRU 128
#define MAX_MRU 16384

#ifndef RT_OS_WINDOWS
# define min(x, y) ((x) < (y) ? (x) : (y))
# define max(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifdef RT_OS_WINDOWS
# undef errno
# if 0 /* debugging */
int errno_func(const char *file, int line);
#  define errno (errno_func(__FILE__, __LINE__))
# else
#  define errno (WSAGetLastError())
# endif
#endif

# define ETH_ALEN        6
# define ETH_HLEN        14

struct ethhdr
{
    unsigned char   h_dest[ETH_ALEN];           /* destination eth addr */
    unsigned char   h_source[ETH_ALEN];         /* source ether addr    */
    unsigned short  h_proto;                    /* packet type ID field */
};
AssertCompileSize(struct ethhdr, 14);

/*
 * (vvl) externing of sscanf.
 */
int sscanf(const char *s, const char *format, ...);

#if defined(VBOX_SLIRP_ALIAS) || defined(VBOX_SLIRP_BSD)

# define ip_next(ip) (void *)((uint8_t *)(ip) + ((ip)->ip_hl << 2))
# define udp_next(udp) (void *)((uint8_t *)&((struct udphdr *)(udp))[1])
# undef  bcopy
# define bcopy(src, dst, len) memcpy((dst), (src), (len))
# undef  bcmp
# define bcmp(a1, a2, len) memcmp((a1), (a2), (len))
# define NO_FW_PUNCH
/* Two wrongs don't make a right, but this at least averts harm. */
# define NO_USE_SOCKETS

# ifdef alias_addr
#  ifndef VBOX_SLIRP_BSD
#   error alias_addr has already defined!!!
#  else
#   undef alias_addr
#  endif
# endif

# define arc4random() RTRandU32()
# undef malloc
# undef calloc
# undef free
# define malloc(x)    RTMemAlloc((x))
# define calloc(x, n) RTMemAllocZ((x)*(n))
# define free(x)      RTMemFree((x))
# ifndef __unused
#  define __unused
# endif

# define strncasecmp RTStrNICmp
# define stderr NULL
# define stdout NULL

# ifdef VBOX_WITH_DEBUG_LIBALIAS
#  define LIBALIAS_DEBUG
# endif

# define fflush(x) do{} while(0)
# include "ext.h"
#endif /*VBOX_SLIRP_ALIAS*/

/**
 * @todo might be useful to make it configurable, especially in terms of Intnet behind NAT
 */
# define maxusers 32
# define max_protohdr 0
/**
 * @todo (vvl) for now ignore these values, later perhaps initialize tuning parameters
 */
# define TUNABLE_INT_FETCH(name, pval) do { } while (0)
# define SYSCTL_PROC(a0, a1, a2, a3, a4, a5, a6, a7, a8) const int dummy_ ## a6 = 0
# define SYSCTL_STRUCT(a0, a1, a2, a3, a4, a5, a6) const int dummy_ ## a5 = 0
# define SYSINIT(a0, a1, a2, a3, a4) const int dummy_ ## a3 = 0
# define sysctl_handle_int(a0, a1, a2, a3) 0
# define EVENTHANDLER_INVOKE(a) do{}while(0)
# define EVENTHANDLER_REGISTER(a0, a1, a2, a3) do{}while(0)
# define KASSERT AssertMsg

struct dummy_req
{
    void *newptr;
};

#define SYSCTL_HANDLER_ARGS PNATState pData, void *oidp, struct dummy_req *req

void mbuf_init(void *);
# define cksum(m, len) in_cksum_skip((m), (len), 0)

int ftp_alias_load(PNATState);
int ftp_alias_unload(PNATState);
int nbt_alias_load(PNATState);
int nbt_alias_unload(PNATState);
int slirp_arp_lookup_ip_by_ether(PNATState, const uint8_t *, uint32_t *);
int slirp_arp_lookup_ether_by_ip(PNATState, uint32_t, uint8_t *);

DECLINLINE(unsigned) slirp_size(PNATState pData)
{
        if (if_mtu < MSIZE)
            return MCLBYTES;
        else if (if_mtu < MCLBYTES)
            return MCLBYTES;
        else if (if_mtu < MJUM9BYTES)
            return MJUM9BYTES;
        else if (if_mtu < MJUM16BYTES)
            return MJUM16BYTES;
        else
            AssertMsgFailed(("Unsupported size"));
        return 0;
}

static inline bool slirpMbufTagService(PNATState pData, struct mbuf *m, uint8_t u8ServiceId)
{
    struct m_tag * t = NULL;
    NOREF(pData);
    /* if_encap assumes that all packets goes through aliased address(gw) */
    if (u8ServiceId == CTL_ALIAS)
        return true;
    t = m_tag_get(PACKET_SERVICE, sizeof(uint8_t), 0);
    if (!t)
        return false;
    *(uint8_t *)&t[1] = u8ServiceId;
    m_tag_prepend(m, t);
    return true;
}

/**
 * This function tags mbuf allocated for special services.
 * @todo: add service id verification.
 */
static inline struct mbuf *slirpServiceMbufAlloc(PNATState pData, uint8_t u8ServiceId)
{
    struct mbuf *m = NULL;
    m = m_getcl(pData, M_DONTWAIT, MT_HEADER, M_PKTHDR);
    if (!m)
        return m;
    if(!slirpMbufTagService(pData, m, u8ServiceId))
    {
        m_freem(pData, m);
        return NULL;
    }
    return m;
}

static inline struct mbuf *slirpDnsMbufAlloc(PNATState pData)
{
    return slirpServiceMbufAlloc(pData, CTL_DNS);
}

DECLINLINE(bool) slirpIsWideCasting(PNATState pData, uint32_t u32Addr)
{
    bool fWideCasting;
    LogFlowFunc(("Enter: u32Addr:%RTnaipv4\n", u32Addr));
    fWideCasting =  (   u32Addr == INADDR_BROADCAST
                    || (u32Addr & RT_H2N_U32_C(~pData->netmask)) == RT_H2N_U32_C(~pData->netmask));
    LogFlowFunc(("Leave: %RTbool\n", fWideCasting));
    return fWideCasting;
}
#endif

