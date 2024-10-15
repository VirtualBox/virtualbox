/* $Id: dnsproxy.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*
 * Copyright (c) 2003,2004,2005 Armin Wolfermann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _DNSPROXY_H_
#define _DNSPROXY_H_

/* LONGLONG */
#include <sys/types.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifndef VBOX
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <stdarg.h>

#include <event.h>

#ifdef DEBUG
#define DPRINTF(x) do { printf x ; } while (0)
#else
#define DPRINTF(x)
#endif

#ifdef GLOBALS
#define GLOBAL(a) a
#define GLOBAL_INIT(a,b) a = b
#else
#define GLOBAL(a) extern a
#define GLOBAL_INIT(a,b) extern a
#endif
#endif

struct request {
    unsigned short      id;

    struct sockaddr_in  client;
    unsigned short      clientid;
    unsigned char       recursion;

#ifndef VBOX
    struct event        timeout;
#endif

    struct request      **prev;
    struct request      *next;
#ifdef VBOX
    /* this field used for saving last attempt
     * to connect server, timeout function should change
     * it's value on next server. And dnsproxy_query should
     * initializate with first server in the list
     *
     * dnsgen is a generation number - a copy of pData->dnsgen at the
     * time of request creation (poor man's weak reference).
     * dns_server must not be used if pData->dnsgen changed.
     */
    struct dns_entry    *dns_server;
    uint32_t            dnsgen;
    int nbyte; /* length of dns request */
    char byte[1]; /* copy of original request */
#endif
};

#ifndef VBOX
GLOBAL_INIT(unsigned int authoritative_port, 53);
GLOBAL_INIT(unsigned int authoritative_timeout, 10);
GLOBAL_INIT(unsigned int recursive_port, 53);
GLOBAL_INIT(unsigned int recursive_timeout, 90);
GLOBAL_INIT(unsigned int stats_timeout, 3600);
GLOBAL_INIT(unsigned int port, 53);

GLOBAL(char *authoritative);
GLOBAL(char *chrootdir);
GLOBAL(char *listenat);
GLOBAL(char *recursive);
GLOBAL(char *user);

GLOBAL(unsigned long active_queries);
GLOBAL(unsigned long all_queries);
GLOBAL(unsigned long authoritative_queries);
GLOBAL(unsigned long recursive_queries);
GLOBAL(unsigned long removed_queries);
GLOBAL(unsigned long dropped_queries);
GLOBAL(unsigned long answered_queries);
GLOBAL(unsigned long dropped_answers);
GLOBAL(unsigned long late_answers);
GLOBAL(unsigned long hash_collisions);

/* dnsproxy.c */
RETSIGTYPE signal_handler(int);
int signal_event(void);

/* daemon.c */
int daemon(int, int);
#endif

/* hash.c */
void hash_add_request(PNATState, struct request *);
void hash_remove_request(PNATState, struct request *);
struct request *hash_find_request(PNATState, unsigned short);

/* internal.c */
int add_internal(PNATState, char *);
int is_internal(PNATState, struct in_addr);

#ifndef VBOX
/* log.c */
void log_syslog(const char *);
void info(const char *, ...);
void error(const char *, ...);
void fatal(const char *, ...);

/* parse.c */
int parse(const char *);

/* statistics.c */
void statistics_start(void);
#else
# define DPRINTF Log2
int dnsproxy_init(PNATState pData);
void dnsproxy_query(PNATState pData, struct socket *so, struct mbuf *m, int iphlen);
void dnsproxy_answer(PNATState pData, struct socket *so, struct mbuf *m);
#endif

#endif /* _DNSPROXY_H_ */
