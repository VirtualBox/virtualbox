/* $Id: proxy_tftpd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT Network - TFTP server.
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

#define LOG_GROUP LOG_GROUP_NAT_SERVICE

#include "winutils.h"

#include "proxy.h"
#include "tftp.h"

#ifndef RT_OS_WINDOWS
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

# define O_RDONLY _O_RDONLY
# define S_ISREG(x) ((x) & _S_IFREG)
#endif

#include "lwip/timers.h"
#include "lwip/udp.h"

#include <iprt/string.h>

struct xfer {
    struct udp_pcb *pcb;
    int fd;
    unsigned int ack;
    struct pbuf *pbuf;

    struct pbuf *oack;

    int rexmit;

    ipX_addr_t peer_ip;
    u16_t peer_port;

    char *filename;
    int octet;

    /* options */
    unsigned int blksize;
    int blksize_from_opt;

    unsigned int timeout;
    int timeout_from_opt;

    off_t tsize;
    int tsize_from_opt;
};

struct tftpd {
    struct udp_pcb *pcb;
    char *root;

#define TFTP_MAX_XFERS 3
    struct xfer xfers[TFTP_MAX_XFERS];
};

struct tftp_option {
    const char *name;
    int (*getopt)(struct xfer *, const char *);
    int (*ackopt)(struct xfer *, char **, size_t *);
};


static void tftpd_recv(void *, struct udp_pcb *, struct pbuf *, ip_addr_t *, u16_t);

static void tftpd_rrq(struct pbuf *, ip_addr_t *, u16_t);

static void tftp_xfer_recv(void *, struct udp_pcb *, struct pbuf *, ip_addr_t *, u16_t);

static void tftp_recv_ack(struct xfer *, u16_t);
static void tftp_fillbuf(struct xfer *);
static void tftp_send(struct xfer *);
static void tftp_timeout(void *);

static struct xfer *tftp_xfer_alloc(ip_addr_t *, u16_t);
static int tftp_xfer_create_pcb(struct xfer *);
static void tftp_xfer_free(struct xfer *);

static int tftp_parse_filename(struct xfer *, char **, size_t *);
static int tftp_parse_mode(struct xfer *, char **, size_t *);
static int tftp_parse_option(struct xfer *, char **, size_t *);

static int tftp_opt_blksize(struct xfer *, const char *);
static int tftp_opt_timeout(struct xfer *, const char *);
static int tftp_opt_tsize(struct xfer *, const char *);

static char *tftp_getstr(struct xfer *, const char *, char **, size_t *);

static int tftp_ack_blksize(struct xfer *, char **, size_t *);
static int tftp_ack_timeout(struct xfer *, char **, size_t *);
static int tftp_ack_tsize(struct xfer *, char **, size_t *);

static int tftp_add_oack(char **, size_t *, const char *, const char *, ...) __attribute__((format(printf, 4, 5)));

static ssize_t tftp_strnlen(char *, size_t);

static int tftp_internal_error(struct xfer *);
static int tftp_error(struct xfer *, u16_t, const char *, ...) __attribute__((format(printf, 3, 4)));
static void tftpd_error(ip_addr_t *, u16_t, u16_t, const char *, ...) __attribute__((format(printf, 4, 5)));
static struct pbuf *tftp_verror(u16_t, const char *, va_list);


/* const */ int report_transient_errors = 1;
static struct tftpd tftpd;

static struct tftp_option tftp_options[] = {
    { "blksize", tftp_opt_blksize, tftp_ack_blksize }, /* RFC 2348  */
    { "timeout", tftp_opt_timeout, tftp_ack_timeout }, /* RFC 2349 */
    { "tsize",   tftp_opt_tsize,   tftp_ack_tsize   }, /* RFC 2349 */
    { NULL,      NULL,             NULL             }
};


err_t
tftpd_init(struct netif *proxy_netif, const char *tftproot)
{
    size_t len;
    err_t error;

    tftpd.root = strdup(tftproot);
    if (tftpd.root == NULL) {
        DPRINTF0(("%s: failed to allocate tftpd.root\n", __func__));
        return ERR_MEM;
    }

    len = strlen(tftproot);
    if (tftpd.root[len - 1] == '/') {
        tftpd.root[len - 1] = '\0';
    }

    tftpd.pcb = udp_new();
    if (tftpd.pcb == NULL) {
        DPRINTF0(("%s: failed to allocate PCB\n", __func__));
        return ERR_MEM;
    }

    udp_recv(tftpd.pcb, tftpd_recv, NULL);

    error = udp_bind(tftpd.pcb, &proxy_netif->ip_addr, TFTP_SERVER_PORT);
    if (error != ERR_OK) {
        DPRINTF0(("%s: failed to bind PCB\n", __func__));
        return error;
    }

    return ERR_OK;
}


static void
tftpd_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
           ip_addr_t *addr, u16_t port)
{
    u16_t op;

    LWIP_ASSERT1(pcb == tftpd.pcb);

    LWIP_UNUSED_ARG(pcb);       /* only in assert */
    LWIP_UNUSED_ARG(arg);

    if (pbuf_clen(p) > 1) { /* this code assumes contiguous aligned payload */
        pbuf_free(p);
        return;
    }

    op = ntohs(*(u16_t *)p->payload);
    switch (op) {
    case TFTP_RRQ:
        tftpd_rrq(p, addr, port);
        break;

    case TFTP_WRQ:
        tftpd_error(addr, port, TFTP_EACCESS, "Permission denied");
        break;

    default:
        tftpd_error(addr, port, TFTP_ENOSYS, "Bad opcode %d", op);
        break;
    }

    pbuf_free(p);
}


/**
 * Parse Read Request packet and start new transfer.
 */
static void
tftpd_rrq(struct pbuf *p, ip_addr_t *addr, u16_t port)
{
    struct xfer *xfer;
    char *s;
    size_t len;
    int has_options;
    int status;

    xfer = tftp_xfer_alloc(addr, port);
    if (xfer == NULL) {
        return;
    }

    /* skip opcode */
    s = (char *)p->payload + sizeof(u16_t);
    len = p->len - sizeof(u16_t);


    /*
     * Parse RRQ:
     *   filename, mode, [opt1, value1, [...] ]
     */
    status = tftp_parse_filename(xfer, &s, &len);
    if (status < 0) {
        goto terminate;
    }

    status = tftp_parse_mode(xfer, &s, &len);
    if (status < 0) {
        goto terminate;
    }

    has_options = 0;
    while (len > 0) {
        status = tftp_parse_option(xfer, &s, &len);
        if (status < 0) {
            goto terminate;
        }
        has_options += status;
    }


    /*
     * Create OACK packet if necessary.
     */
    if (has_options) {
        xfer->oack = pbuf_alloc(PBUF_RAW, 128, PBUF_RAM);
        if (xfer->oack != NULL) {
            struct tftp_option *o;

            ((u16_t *)xfer->oack->payload)[0] = PP_HTONS(TFTP_OACK);

            s = (char *)xfer->oack->payload + sizeof(u16_t);
            len = xfer->oack->len - sizeof(u16_t);

            for (o = &tftp_options[0]; o->name != NULL; ++o) {
                status = (*o->ackopt)(xfer, &s, &len);
                if (status < 0) {
                    pbuf_free(xfer->oack);
                    xfer->oack = NULL;
                    break;
                }
            }

            if (xfer->oack != NULL) {
                Assert((u16_t)(xfer->oack->len - len) == xfer->oack->len - len);
                pbuf_realloc(xfer->oack, (u16_t)(xfer->oack->len - len));
            }
        }
    }


    /*
     * Create static pbuf that will be used for all data packets.
     */
    xfer->pbuf = pbuf_alloc(PBUF_RAW, xfer->blksize + 4, PBUF_RAM);
    if (xfer->pbuf == NULL) {
        tftp_internal_error(xfer);
        goto terminate;
    }
    ((u16_t *)xfer->pbuf->payload)[0] = PP_HTONS(TFTP_DATA);


    /*
     * Finally, create PCB.  Before this point any error was reported
     * from the server port (see tftp_error() for the reason).
     */
    status = tftp_xfer_create_pcb(xfer);
    if (status < 0) {
        goto terminate;
    }

    if (xfer->oack) {
        tftp_send(xfer);
    }
    else {
        /* trigger send of the first data packet */
        tftp_recv_ack(xfer, 0);
    }

    return;

  terminate:
    DPRINTF(("%s: terminated", __func__));
    tftp_xfer_free(xfer);
}


static void
tftp_xfer_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
               ip_addr_t *addr, u16_t port)
{
    struct xfer *xfer = (struct xfer *)arg;
    u16_t op;

    LWIP_UNUSED_ARG(pcb);       /* assert only */
    LWIP_UNUSED_ARG(addr);
    LWIP_UNUSED_ARG(port);

    LWIP_ASSERT1(xfer->pcb == pcb);

    if (p->len < 2) {
        tftp_error(xfer, TFTP_ENOSYS, "Short packet");
        tftp_xfer_free(xfer);
        pbuf_free(p);
        return;
    }

    op = ntohs(*(u16_t *)p->payload);
    if (op == TFTP_ACK) {
        u16_t ack;

        if (p->len < 4) {
            tftp_error(xfer, TFTP_ENOSYS, "Short packet");
            tftp_xfer_free(xfer);
            pbuf_free(p);
            return;
        }

        ack = ntohs(((u16_t *)p->payload)[1]);
        tftp_recv_ack(xfer, ack);
    }
    else if (op == TFTP_ERROR) {
        tftp_xfer_free(xfer);
    }
    else {
        tftp_error(xfer, TFTP_ENOSYS, "Unexpected opcode %d", op);
        tftp_xfer_free(xfer);
    }

    pbuf_free(p);
}


static void
tftp_recv_ack(struct xfer *xfer, u16_t ack)
{
    if (ack != (u16_t)xfer->ack) {
        DPRINTF2(("%s: expect %u (%u), got %u\n",
                  __func__, (u16_t)xfer->ack, xfer->ack, ack));
        return;
    }

    sys_untimeout(tftp_timeout, xfer);
    xfer->rexmit = 0;

    if (xfer->pbuf->len < xfer->blksize) {
        DPRINTF(("%s: got final ack %u (%u)\n",
                 __func__, (u16_t)xfer->ack, xfer->ack));
        tftp_xfer_free(xfer);
        return;
    }

    if (xfer->oack != NULL) {
        pbuf_free(xfer->oack);
        xfer->oack = NULL;
    }

    ++xfer->ack;
    tftp_fillbuf(xfer);
    tftp_send(xfer);
}


static void
tftp_send(struct xfer *xfer)
{
    struct pbuf *pbuf;

    pbuf = xfer->oack ? xfer->oack : xfer->pbuf;
    udp_send(xfer->pcb, pbuf);
    sys_timeout(xfer->timeout * 1000, tftp_timeout, xfer);
}


static void
tftp_timeout(void *arg)
{
    struct xfer *xfer = (struct xfer *)arg;
    int maxrexmit;

    maxrexmit = xfer->timeout < 60 ? 5 : 3;
    if (++xfer->rexmit < maxrexmit) {
        tftp_send(xfer);
    }
    else {
        tftp_xfer_free(xfer);
    }
}


static void
tftp_fillbuf(struct xfer *xfer)
{
    ssize_t nread;

    DPRINTF2(("%s: reading block %u\n", __func__, xfer->ack));

    ((u16_t *)xfer->pbuf->payload)[1] = htons(xfer->ack);
    nread = read(xfer->fd, (char *)xfer->pbuf->payload + 4, xfer->blksize);

    if (nread < 0) {
        tftp_error(xfer, TFTP_EUNDEF, "Read failed");
        return;
    }

    pbuf_realloc(xfer->pbuf, nread + 4);
}


/**
 * Find a free transfer slot (without a pcb).  Record peer's IP
 * address and port, but don't allocate a pcb yet.
 *
 * We delay creation of the pcb in response to the original request
 * until the request is verified and accepted.  This makes using
 * tcpdump(8) easier, since tcpdump does not track TFTP transfers, so
 * an error reply from a new pcb is not recognized as such and is not
 * decoded as TFTP (see tftp_error()).
 *
 * If the request is rejected, the pcb remains NULL and the transfer
 * slot remains unallocated.  Since all TFTP processing happens on the
 * lwIP thread, there's no concurrent processing, so we don't need to
 * "lock" the transfer slot until the pcb is allocated.
 */
static struct xfer *
tftp_xfer_alloc(ip_addr_t *addr, u16_t port)
{
    struct xfer *xfer;
    int i;

    /* Find free xfer slot */
    xfer = NULL;
    for (i = 0; i < TFTP_MAX_XFERS; ++i) {
        if (tftpd.xfers[i].pcb == NULL) {
            xfer = &tftpd.xfers[i];
            break;
        }
    }

    if (xfer == NULL) {
        if (report_transient_errors) {
            tftpd_error(addr, port, TFTP_EUNDEF,
                        "Maximum number of simultaneous connections exceeded");
        }
        return NULL;
    }

    ipX_addr_copy(0, xfer->peer_ip, *ip_2_ipX(addr));
    xfer->peer_port = port;

    xfer->ack = 0;

    xfer->pbuf = NULL;
    xfer->oack = NULL;
    xfer->rexmit = 0;

    xfer->blksize = 512;
    xfer->blksize_from_opt = 0;

    xfer->timeout = 1;
    xfer->timeout_from_opt = 0;

    xfer->tsize = -1;
    xfer->tsize_from_opt = 0;

    return xfer;
}


static int
tftp_xfer_create_pcb(struct xfer *xfer)
{
    struct udp_pcb *pcb;
    err_t error;

    pcb = udp_new();

    /* Bind */
    if (pcb != NULL) {
        error = udp_bind(pcb, ipX_2_ip(&tftpd.pcb->local_ip), 0);
        if (error != ERR_OK) {
            udp_remove(pcb);
            pcb = NULL;
        }
    }

    /* Connect */
    if (pcb != NULL) {
        error = udp_connect(pcb, ipX_2_ip(&xfer->peer_ip), xfer->peer_port);
        if (error != ERR_OK) {
            udp_remove(pcb);
            pcb = NULL;
        }
    }

    if (pcb == NULL) {
        if (report_transient_errors) {
            tftp_error(xfer, TFTP_EUNDEF, "Failed to create connection");
        }
        return -1;
    }

    xfer->pcb = pcb;
    udp_recv(xfer->pcb, tftp_xfer_recv, xfer);

    return 0;
}


static void
tftp_xfer_free(struct xfer *xfer)
{
    sys_untimeout(tftp_timeout, xfer);

    if (xfer->pcb != NULL) {
        udp_remove(xfer->pcb);
        xfer->pcb = NULL;
    }

    if (xfer->fd > 0) {
        close(xfer->fd);
        xfer->fd = -1;
    }

    if (xfer->oack != NULL) {
        pbuf_free(xfer->oack);
        xfer->oack = NULL;
    }

    if (xfer->pbuf != NULL) {
        pbuf_free(xfer->pbuf);
        xfer->pbuf = NULL;
    }

    if (xfer->filename != NULL) {
        free(xfer->filename);
        xfer->filename = NULL;
    }
}


static int
tftp_parse_filename(struct xfer *xfer, char **ps, size_t *plen)
{
    const char *filename;
    struct stat st;
    char *pathname;
    char *s;
    size_t len;
    int status;

    filename = tftp_getstr(xfer, "filename", ps, plen);
    if (filename == NULL) {
        return -1;
    }

    DPRINTF(("%s: requested file name: %s\n", __func__, filename));
    xfer->filename = strdup(filename);
    if (xfer->filename == NULL) {
        return tftp_internal_error(xfer);
    }

    /* replace backslashes with forward slashes */
    s = xfer->filename;
    while ((s = strchr(s, '\\')) != NULL) {
        *s++ = '/';
    }

    /* deny attempts to break out of tftp dir */
    if (strncmp(xfer->filename, "../", 3) == 0
        || strstr(xfer->filename, "/../") != NULL)
    {
        return tftp_error(xfer, TFTP_ENOENT, "Permission denied");
    }

    len = strlen(tftpd.root) + 1 /*slash*/ + strlen(xfer->filename) + 1 /*nul*/;
    pathname = (char *)malloc(len);
    if (pathname == NULL) {
        return tftp_internal_error(xfer);
    }

    RTStrPrintf(pathname, len, "%s/%s", tftpd.root, xfer->filename);
/** @todo fix RTStrPrintf because this does not currently work:
 *   status = RTStrPrintf(pathname, len, "%s/%s", tftpd.root, xfer->filename);
 *   if (status < 0) {
 *       return tftp_internal_error(xfer);
 *   }
 */

    DPRINTF(("%s: full pathname: %s\n", __func__, pathname));
    xfer->fd = open(pathname, O_RDONLY);
    if (xfer->fd < 0) {
        if (errno == EPERM) {
            return tftp_error(xfer, TFTP_ENOENT, "Permission denied");
        }
        else {
            return tftp_error(xfer, TFTP_ENOENT, "File not found");
        }
    }

    status = fstat(xfer->fd, &st);
    if (status < 0) {
        return tftp_internal_error(xfer);
    }

    if (!S_ISREG(st.st_mode)) {
        return tftp_error(xfer, TFTP_ENOENT, "File not found");
    }

    xfer->tsize = st.st_size;
    return 0;
}


static int
tftp_parse_mode(struct xfer *xfer, char **ps, size_t *plen)
{
    const char *modename;

    modename = tftp_getstr(xfer, "mode", ps, plen);
    if (modename == NULL) {
        return -1;
    }

    if (RTStrICmp(modename, "octet") == 0) {
        xfer->octet = 1;
    }
    else if (RTStrICmp(modename, "netascii") == 0) {
        xfer->octet = 0;
        /* XXX: not (yet?) */
        return tftp_error(xfer, TFTP_ENOSYS, "Mode \"netascii\" not supported");
    }
    else if (RTStrICmp(modename, "mail") == 0) {
        return tftp_error(xfer, TFTP_ENOSYS, "Mode \"mail\" not supported");
    }
    else {
        return tftp_error(xfer, TFTP_ENOSYS, "Unknown mode \"%s\"", modename);
    }

    return 0;
}


static int
tftp_parse_option(struct xfer *xfer, char **ps, size_t *plen)
{
    const char *opt;
    const char *val;
    struct tftp_option *o;

    opt = tftp_getstr(xfer, "option name", ps, plen);
    if (opt == NULL) {
        return -1;
    }

    if (*plen == 0) {
        return tftp_error(xfer, TFTP_EUNDEF, "Missing option value");
    }

    val = tftp_getstr(xfer, "option value", ps, plen);
    if (val == NULL) {
        return -1;
    }

    /* handle option if known, ignore otherwise */
    for (o = &tftp_options[0]; o->name != NULL; ++o) {
        if (RTStrICmp(o->name, opt) == 0) {
            return (*o->getopt)(xfer, val);
        }
    }

    return 0; /* unknown option */
}


static int
tftp_opt_blksize(struct xfer *xfer, const char *optval)
{
    char *end;
    long blksize;

    errno = 0;
    blksize = strtol(optval, &end, 10);
    if (errno != 0 || *end != '\0') {
        return 0;
    }

    if (blksize < 8) {
        return 0;
    }

    if (blksize > 1428) {       /* exceeds ethernet mtu */
        blksize = 1428;
    }

    xfer->blksize = blksize;
    xfer->blksize_from_opt = 1;
    return 1;
}


static int
tftp_opt_timeout(struct xfer *xfer, const char *optval)
{
    LWIP_UNUSED_ARG(xfer);
    LWIP_UNUSED_ARG(optval);
    return 0;
}


static int
tftp_opt_tsize(struct xfer *xfer, const char *optval)
{
    LWIP_UNUSED_ARG(optval);  /* must be "0", but we don't check it */

    if (xfer->tsize < 0) {
        return 0;
    }

    xfer->tsize_from_opt = 1;
    return 1;
}


static char *
tftp_getstr(struct xfer *xfer, const char *msg, char **ps, size_t *plen)
{
    char *s;
    ssize_t slen;

    s = *ps;
    slen = tftp_strnlen(s, *plen);
    if (slen < 0) {
        tftp_error(xfer, TFTP_EUNDEF, "Unterminated %s", msg);
        return NULL;
    }

    *ps += slen + 1;
    *plen -= slen + 1;

    return s;
}


static int
tftp_ack_blksize(struct xfer *xfer, char **ps, size_t *plen)
{
    if (!xfer->blksize_from_opt) {
        return 0;
    }

    return tftp_add_oack(ps, plen, "blksize", "%u", xfer->blksize);
}


static int
tftp_ack_timeout(struct xfer *xfer, char **ps, size_t *plen)
{
    if (!xfer->timeout_from_opt) {
        return 0;
    }

    return tftp_add_oack(ps, plen, "timeout", "%u", xfer->timeout);
}


static int
tftp_ack_tsize(struct xfer *xfer, char **ps, size_t *plen)
{
    if (!xfer->tsize_from_opt) {
        return 0;
    }

    LWIP_ASSERT1(xfer->tsize >= 0);
    return tftp_add_oack(ps, plen, "tsize",
                         /* XXX: FIXME: want 64 bit */
                         "%lu", (unsigned long)xfer->tsize);
}


static int
tftp_add_oack(char **ps, size_t *plen,
              const char *optname, const char *fmt, ...)
{
    va_list ap;
    int sz;

/** @todo Fix RTStrPrintf because this doesn't really work.
 *   sz = RTStrPrintf(*ps, *plen, "%s", optname);
 *   if (sz < 0 || (size_t)sz >= *plen) {
 *       return -1;
 *   } */
    sz = (int)RTStrPrintf(*ps, *plen, "%s", optname);
    if (/*sz < 0 ||*/ (size_t)sz >= *plen) {
        return -1;
    }

    ++sz;                       /* for nul byte */
    *ps += sz;
    *plen -= sz;

    va_start(ap, fmt);
    sz = vsnprintf(*ps, *plen, fmt, ap);
    va_end(ap);
    if (sz < 0 || (size_t)sz >= *plen) {
        return -1;
    }

    ++sz;                       /* for nul byte */
    *ps += sz;
    *plen -= sz;

    return 0;
}


static ssize_t
tftp_strnlen(char *buf, size_t bufsize)
{
    void *end;

    end = memchr(buf, '\0', bufsize);
    if (end == NULL) {
        return -1;
    }

    return (char *)end - buf;
}


static int
tftp_internal_error(struct xfer *xfer)
{
    if (report_transient_errors) {
        tftp_error(xfer, TFTP_EUNDEF, "Internal error");
    }
    return -1;
}


/**
 * Send an error packet to the peer.
 *
 * PCB may not be created yet in which case send the error packet from
 * the TFTP server port (*).
 *
 * (*) We delay creation of the PCB in response to the original
 * request until the request is verified and accepted.  This makes
 * using tcpdump(8) easier, since tcpdump does not track TFTP
 * transfers, so an error reply from a new PCB is not recognized as
 * such and is not decoded as TFTP.
 *
 * Always returns -1 for callers to reuse.
 */
static int
tftp_error(struct xfer *xfer, u16_t error, const char *fmt, ...)
{
    va_list ap;
    struct pbuf *q;

    LWIP_ASSERT1(xfer != NULL);

    va_start(ap, fmt);
    q = tftp_verror(error, fmt, ap);
    va_end(ap);

    if (q == NULL) {
        return -1;
    }

    if (xfer->pcb != NULL) {
        udp_send(xfer->pcb, q);
    }
    else {
        udp_sendto(tftpd.pcb, q, ipX_2_ip(&xfer->peer_ip), xfer->peer_port);
    }

    pbuf_free(q);
    return -1;
}


/**
 * Send an error packet from TFTP server port to the specified peer.
 */
static void
tftpd_error(ip_addr_t *addr, u16_t port, u16_t error, const char *fmt, ...)
{
    va_list ap;
    struct pbuf *q;

    va_start(ap, fmt);
    q = tftp_verror(error, fmt, ap);
    va_end(ap);

    if (q != NULL) {
        udp_sendto(tftpd.pcb, q, addr, port);
        pbuf_free(q);
    }
}


/**
 * Create ERROR pbuf with formatted error message.
 */
static struct pbuf *
tftp_verror(u16_t error, const char *fmt, va_list ap)
{
    struct tftp_error {
        u16_t opcode;           /* TFTP_ERROR */
        u16_t errcode;
        char errmsg[512];
    };

    struct pbuf *p;
    struct tftp_error *errpkt;
    int msgsz;

    p = pbuf_alloc(PBUF_TRANSPORT, sizeof(*errpkt), PBUF_RAM);
    if (p == NULL) {
        return NULL;
    }

    errpkt = (struct tftp_error *)p->payload;
    errpkt->opcode = PP_HTONS(TFTP_ERROR);
    errpkt->errcode = htons(error);

    msgsz = vsnprintf(errpkt->errmsg, sizeof(errpkt->errmsg), fmt, ap);
    if (msgsz < 0) {
        errpkt->errmsg[0] = '\0';
        msgsz = 1;
    }
    else if ((size_t)msgsz < sizeof(errpkt->errmsg)) {
        ++msgsz;                /* for nul byte */
    }
    else {
        msgsz = sizeof(errpkt->errmsg); /* truncated, includes nul byte */
    }

    pbuf_realloc(p, sizeof(*errpkt) - sizeof(errpkt->errmsg) + msgsz);
    return p;
}
