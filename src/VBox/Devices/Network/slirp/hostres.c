/* $Id: hostres.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host resolver
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef RT_OS_WINDOWS
# include <netdb.h>
#endif
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <slirp.h>

#define isdigit(ch)    RT_C_IS_DIGIT(ch)
#define isalpha(ch)    RT_C_IS_ALPHA(ch)

#define DNS_CONTROL_PORT_NUMBER 53
/* see RFC 1035(4.1.1) */
struct dnsmsg_header
{
    uint16_t id;

#ifdef RT_OS_WINDOWS
  /* size of the type forces alignment */
# define U16_BIT_FIELD_T uint16_t
#else
  /* gcc -pedantic complains about implementaion-defined types */
# define U16_BIT_FIELD_T unsigned int
#endif

    /* XXX: endianness */
    U16_BIT_FIELD_T rd:1;
    U16_BIT_FIELD_T tc:1;
    U16_BIT_FIELD_T aa:1;
    U16_BIT_FIELD_T opcode:4;
    U16_BIT_FIELD_T qr:1;
    U16_BIT_FIELD_T rcode:4;
    U16_BIT_FIELD_T Z:3;
    U16_BIT_FIELD_T ra:1;

    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};
AssertCompileSize(struct dnsmsg_header, 12);

#define QR_Query                0
#define QR_Response             1

#define OpCode_Query            0

#define RCode_NoError           0
#define RCode_FormErr           1
#define RCode_ServFail          2
#define RCode_NXDomain          3
#define RCode_NotImp            4
#define RCode_Refused           5

#define Type_A                  1
#define Type_CNAME              5
#define Type_PTR                12
#define Type_ANY                255

#define Class_IN                1
#define Class_ANY               255

/* compressed label encoding */
#define DNS_LABEL_PTR           0xc0

#define DNS_MAX_UDP_LEN         512
#define DNS_MAX_LABEL_LEN       63
#define DNS_MAX_NAME_LEN        255


/*
 * A tree of labels.
 *
 * rfc1035#section-3.1
 * rfc1035#section-4.1.4
 */
struct label
{
    const uint8_t *buf;
    ssize_t off;
    struct label *children;
    struct label *sibling;
};


/*
 * A structure to build DNS response.
 */
struct response
{
    PNATState pData;

    uint32_t src;
    uint16_t sport;

    struct label *labels;       /* already encoded in buf */
    size_t qlen;                /* original question */
    size_t end;                 /* of data in buf */

    /* continuous buffer to build the response */
    uint8_t buf[DNS_MAX_UDP_LEN];
};


static int verify_header(PNATState pData, struct mbuf **pMBuf);
static struct mbuf *refuse_mbuf(struct mbuf *m, unsigned int rcode);

static int respond(struct response *res);
static int resolve(struct response *res, uint16_t qtype, size_t qname);
static int resolve_reverse(struct response *res, uint16_t qtype, size_t qname,
                            struct in_addr addr);

static int refuse(struct response *res, unsigned int rcode);


static ssize_t append_a(struct response *res, const char *name, struct in_addr addr);
static ssize_t append_cname(struct response *res, const char *name, const char *cname);
static ssize_t append_ptr(struct response *res, const char *inaddrname, const char *name);
static ssize_t append_name_rr(struct response *res, const char *question, int type, const char *answer);
static ssize_t append_rrhdr(struct response *res, const char *name, uint16_t type, uint32_t ttl);
static ssize_t append_name(struct response *res, const char *name);
static ssize_t append_u32(struct response *res, uint32_t value);
static ssize_t append_u16(struct response *res, uint16_t value);
static ssize_t append_u8(struct response *res, uint8_t value);
static ssize_t append_bytes(struct response *res, uint8_t *p, size_t size);
static ssize_t check_space(struct response *res, size_t size);

static int get_in_addr_arpa(struct in_addr *paddr, struct label *root);
static int labelstrcmp(struct label *l, const char *s);
static void strnlabels(char *namebuf, size_t nbuflen, const uint8_t *msg, size_t off);

/*static void LogLabelsTree(const char *before, struct label *l, const char *after); - unused */
static void free_labels(struct label *root);

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
static void alterHostentWithDataFromDNSMap(PNATState pData, struct hostent *h);
static PDNSMAPPINGENTRY getDNSMapByName(PNATState pData, const char *name);
static PDNSMAPPINGENTRY getDNSMapByAddr(PNATState pData, const uint32_t *pu32IpAddress);
#endif

#if 1 /* XXX */
# define LogErr(args) Log2(args)
# define LogDbg(args) Log3(args)
#else
# define LogErr(args) LogRel(args)
# define LogDbg(args) LogRel(args)
#endif


static DECLCALLBACK(void) hostres_async(struct response *res);
static DECLCALLBACK(void) hostres_slirp_reply(struct response *res);


/*
 * Host resolver is called on slirp thread from udp.c
 */
struct mbuf *
hostresolver(PNATState pData, struct mbuf *m, uint32_t src, uint16_t sport)
{
    struct response *res;
    u_int mlen;
    int rc;

    rc = verify_header(pData, &m);
    if (RT_FAILURE(rc))
        return m;

    res = RTMemAllocZ(sizeof(*res));
    if (res == NULL)
        return refuse_mbuf(m, RCode_ServFail);

    res->pData = pData;
    res->src = src;
    res->sport = sport;

    mlen = m_length(m, NULL);
    m_copydata(m, 0, mlen, (char *)res->buf);
    res->end = res->qlen = mlen;

    rc = slirp_call_hostres(pData->pvUser, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT, (PFNRT)hostres_async, 1, res);
    if (RT_FAILURE(rc))
    {
        LogErr(("NAT: hostres: failed to post async request: %Rrc\n", rc));
        RTMemFree(res);
        return refuse_mbuf(m, RCode_ServFail);
    }

    m_freem(pData, m);
    return NULL;
}


/*
 * Do quick sanity-checks on the request before doing async
 * resolution.  If we don't like it, immediately drop or convert to
 * response in place and bounce back the mbuf.
 */
static int
verify_header(PNATState pData, struct mbuf **pMBuf)
{
    struct mbuf *m;
    struct dnsmsg_header *pHdr;
    size_t mlen;

    m = *pMBuf;
    mlen = m_length(m, NULL);

    /*
     * In theory we should have called
     *
     *   m = m_pullup(m, sizeof(struct dnsmsg_header));
     *
     * here first (which should have been a nop), but the way mbufs
     * are used in NAT will always cause a copy that will have no
     * leading space.  We can use m_copyup() instead, but if we are
     * peeking under the hood anyway, we might as well just rely on
     * the fact that this header will be contiguous.
     */
    pHdr = mtod(m, struct dnsmsg_header *);

    if (RT_UNLIKELY(mlen < sizeof(*pHdr)))
    {
        LogErr(("NAT: hostres: packet too small: %zu bytes\n", mlen));
        goto drop;              /* can't even refuse it */
    }

    if (RT_UNLIKELY(mlen > DNS_MAX_UDP_LEN))
    {
        LogErr(("NAT: hostres: packet too large: %zu bytes\n", mlen));
        goto drop;              /* don't echo back huge packets */
    }

    if (RT_UNLIKELY(pHdr->qr != QR_Query))
    {
        LogErr(("NAT: hostres: unexpected response\n"));
        goto drop;              /* ignore */
    }

    if (RT_UNLIKELY(pHdr->opcode != OpCode_Query))
    {
        LogErr(("NAT: hostres: unsupported opcode %d\n", pHdr->opcode));
        refuse_mbuf(m, RCode_NotImp);
        return VERR_PARSE_ERROR;
    }

    if (RT_UNLIKELY(pHdr->qdcount != RT_H2N_U16_C(1)))
    {
        LogErr(("NAT: hostres: multiple questions\n"));
        refuse_mbuf(m, RCode_FormErr);
        return VERR_PARSE_ERROR;
    }

    if (RT_UNLIKELY(pHdr->ancount != 0))
    {
        LogErr(("NAT: hostres: answers in query\n"));
        refuse_mbuf(m, RCode_FormErr);
        return VERR_PARSE_ERROR;
    }

    /* XXX: let it fail when we parse it? */
    if (RT_UNLIKELY(mlen < sizeof(*pHdr)
                             + /* qname  */ 1
                             + /* qtype  */ 2
                             + /* qclass */ 2))
    {
        LogErr(("NAT: hostres: packet too small: %zu bytes\n", mlen));
        refuse_mbuf(m, RCode_FormErr);
        return VERR_PARSE_ERROR;
    }

    return VINF_SUCCESS;

  drop:
    if (m != NULL)
        m_freem(pData, m);
    *pMBuf = NULL;
    return VERR_PARSE_ERROR;
}


/*
 * Turn the request in mbuf into an error response.  This is used on
 * slirp thread for pre-checks before we do async resolution.
 */
static struct mbuf *
refuse_mbuf(struct mbuf *m, unsigned int rcode)
{
    struct dnsmsg_header *pHdr;

    pHdr = mtod(m, struct dnsmsg_header *);
    pHdr->qr = QR_Response;
    pHdr->rcode = rcode;
    pHdr->ra = 1;
    pHdr->aa = 0;

    return m;
}


/*
 * Actuall resolution runs on the dedicated host resolver thread.
 */
static void
hostres_async(struct response *res)
{
    int rc;

    /* build reply in res->buf[] */
    respond(res);

    free_labels(res->labels);

    rc = slirp_call(res->pData->pvUser, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT, (PFNRT)hostres_slirp_reply, 1, res);
    if (RT_FAILURE(rc))
    {
        LogErr(("NAT: hostres: failed to post async reply: %Rrc\n", rc));
        RTMemFree(res);
    }
}


/*
 * We are  back to the slirp thread to send the reply.
 */
static void
hostres_slirp_reply(struct response *res)
{
    PNATState pData = res->pData;
    struct sockaddr_in src, dst;
    struct mbuf *m = NULL;
    size_t mlen;
    int ok;

    mlen = if_maxlinkhdr + sizeof(struct ip) + sizeof(struct udphdr);
    mlen += res->end;

    if (mlen <= MHLEN)
    {
        m = m_gethdr(pData, M_NOWAIT, MT_HEADER);
    }
    else
    {
        void *pvBuf;            /* ignored */
        size_t cbBuf;

        m = slirp_ext_m_get(pData, mlen, &pvBuf, &cbBuf);
    }

    if (m == NULL)
        goto out;

    /* reserve leading space for ethernet header */
    m->m_data += if_maxlinkhdr;

    /* reserve leading space for protocol headers */
    m->m_pkthdr.header = mtod(m, void *);
    m->m_data += sizeof(struct ip) + sizeof(struct udphdr);

    m->m_len = 0;
    ok = m_append(pData, m, (int)res->end, (c_caddr_t)res->buf);
    if (!ok)
    {
        m_freem(pData, m);
        goto out;
    }

    src.sin_addr.s_addr = RT_H2N_U32(RT_N2H_U32(pData->special_addr.s_addr) | CTL_DNS);
    src.sin_port = RT_H2N_U16_C(53);
    dst.sin_addr.s_addr = res->src;
    dst.sin_port = res->sport;

    udp_output2(pData, NULL, m, &src, &dst, IPTOS_LOWDELAY);

  out:
    RTMemFree(res);
}


static int
respond(struct response *res)
{
    struct dnsmsg_header *pHdr;
    size_t off;
    size_t qname;
    uint16_t qtype, qclass;
    struct in_addr in_addr_arpa;
    struct label *l;

    /* convert header to response */
    pHdr = (struct dnsmsg_header *)res->buf;
    pHdr->qr = QR_Response;
    pHdr->rcode = RCode_NoError;
    pHdr->ra = 1;               /* the host provides recursion */
    pHdr->aa = 0;               /* we are not authoritative */
    pHdr->Z = 0;                /* clear rfc2535 dnssec bits */

    off = sizeof(*pHdr);
    qname = off;

    /*
     * Parse/verify QNAME and collect the suffixes to be used for
     * compression in the answer.
     */
    while (off < res->qlen) {
        size_t loff, llen;
        uint8_t c;

        c = res->buf[off];

        /*
         * There's just one question with just one name, so there are
         * no other labels it can point to.  Thus all well-formed
         * names with a pointer can only be infinite loops.
         */
        if ((c & DNS_LABEL_PTR) == DNS_LABEL_PTR)
        {
            LogErr(("NAT: hostres: label pointer in the qname\n"));
            return refuse(res, RCode_FormErr);
        }

        if ((c & DNS_LABEL_PTR) != 0)
        {
            LogErr(("NAT: hostres: unexpected high bits\n"));
            return refuse(res, RCode_FormErr);
        }

        /*
         * label of "llen" chars starts at offset "loff".
         */
        loff = off;
        llen = c;
        ++off;

        if (loff + 1 + llen > res->qlen)
        {
            LogErr(("NAT: hostres: length byte points beyound packet boundary\n"));
            return refuse(res, RCode_FormErr);
        }

        if (llen == 0)             /* end of the label list */
        {
            break;
        }

        /* do only minimal verification of the label */
        while (off < loff + 1 + llen)
        {
            c = res->buf[off];
            ++off;

            if (c == '.')
            {
                LogErr(("NAT: hostres: dot inside label\n"));
                return refuse(res, RCode_FormErr);
            }

            if (c == '\0')
            {
                LogErr(("NAT: hostres: nul byte inside label\n"));
                return refuse(res, RCode_FormErr);
            }
        }

        l = RTMemAllocZ(sizeof(*l));
        l->buf = res->buf;
        l->off = loff;
        l->children = res->labels;
        res->labels = l;
    }

    /*
     * QTYPE and QCLASS
     */
    if (RT_UNLIKELY(off + 4 > res->qlen))
    {
        LogErr(("NAT: hostres: question too short\n"));
        return refuse(res, RCode_FormErr);
    }

    memcpy(&qtype, &res->buf[off], sizeof(qtype));
    qtype = RT_N2H_U16(qtype);
    off += sizeof(qtype);

    memcpy(&qclass, &res->buf[off], sizeof(qclass));
    qclass = RT_N2H_U16(qclass);
    off += sizeof(qclass);

    if (   qclass != Class_IN
        && qclass != Class_ANY)
    {
        LogErr(("NAT: hostres: unsupported qclass %d\n", qclass));
        return refuse(res, RCode_NoError);
    }

    if (   qtype != Type_A
        && qtype != Type_CNAME
        && qtype != Type_PTR
        && qtype != Type_ANY)
    {
        LogErr(("NAT: hostres: unsupported qtype %d\n", qtype));
        return refuse(res, RCode_NoError);
    }


    /**
     * Check if there's anything after the question.  If query says it
     * has authority or additional records, ignore and drop them
     * without parsing.
     *
     * We have already rejected queries with answer(s) before.  We
     * have ensured that qname in the question doesn't contain
     * pointers, so truncating the buffer is safe.
     */
    if (off < res->qlen)
    {
        ssize_t trailer = res->qlen - off;

        LogDbg(("NAT: hostres: question %zu < mlen %zu\n", off, res->qlen));

        if (pHdr->nscount == 0 && pHdr->arcount == 0)
        {
            LogErr(("NAT: hostres: unexpected %d bytes after the question\n", trailer));
            return refuse(res, RCode_FormErr);
        }

        LogDbg(("NAT: hostres: ignoring %d bytes of %s%s%s records\n",
                trailer,
                pHdr->nscount != 0 ? "authority" : "",
                pHdr->nscount != 0 && pHdr->arcount != 0 ? " and " : "",
                pHdr->arcount != 0 ? "additional" : ""));

        res->qlen -= trailer;
        res->end = res->qlen;

        pHdr->nscount = 0;
        pHdr->arcount = 0;
    }


    /*
     * Check for IN-ADDR.ARPA.  Use the fact that res->labels at this
     * point contains only the qname, so we have easy top-down access
     * to its components.
     */
    if (get_in_addr_arpa(&in_addr_arpa, res->labels))
        return resolve_reverse(res, qtype, qname, in_addr_arpa);
    else
        return resolve(res, qtype, qname);
}


static int
resolve(struct response *res, uint16_t qtype, size_t qname)
{
    struct dnsmsg_header *pHdr;
    struct hostent *h;
    struct hostent hostent;
    char *h_aliases[1];
    char *h_addr_list[2];
    size_t oend;
    size_t nanswers;
    ssize_t nbytes;
    int i;

    char name[DNS_MAX_NAME_LEN+1];

    pHdr = (struct dnsmsg_header *)res->buf;
    nanswers = 0;
    oend = res->end;

    strnlabels(name, sizeof(name), res->buf, qname);
    LogDbg(("NAT: hostres: qname=\"%s\"\n", name));

    if (qtype != Type_A && qtype != Type_CNAME && qtype != Type_ANY)
    {
        goto out; /* NB: RCode_NoError without an answer, not RCode_NXDomain */
    }

    h = NULL;
#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
    {
        PDNSMAPPINGENTRY pDNSMapingEntry = getDNSMapByName(res->pData, name);
        if (pDNSMapingEntry != NULL)
        {
            LogDbg(("NAT: hostres: %s resolved from %s%s\n",
                    name,
                    pDNSMapingEntry->fPattern ? "pattern " : "mapping",
                    pDNSMapingEntry->fPattern ? pDNSMapingEntry->pszName : ""));

            if (qtype == Type_CNAME)
            {
                goto out;
            }

            hostent.h_name = name;
            hostent.h_aliases = h_aliases;
            h_aliases[0] = NULL;
            hostent.h_addrtype = AF_INET;
            hostent.h_length = sizeof(RTNETADDRIPV4);
            hostent.h_addr_list = h_addr_list;
            h_addr_list[0] = (char *)&pDNSMapingEntry->u32IpAddress;
            h_addr_list[1] = NULL;

            h = &hostent;
        }
    }
#endif

    if (h == NULL)
    {
        h = gethostbyname(name);
    }

    if (h == NULL)
    {
        /* LogErr: h_errno */
        return refuse(res, RCode_NXDomain);
    }

    if (h->h_length != sizeof(RTNETADDRIPV4))
    {
        /* Log: what kind of address did we get?! */
        goto out;
    }

    if (   h->h_addr_list == NULL
        || h->h_addr_list[0] == NULL)
    {
        /* Log: shouldn't happen */
        goto out;
    }

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
    alterHostentWithDataFromDNSMap(res->pData, h);
#endif

    /*
     * Emit CNAME record if canonical name differs from the qname.
     */
    if (   h->h_name != NULL
        && RTStrICmp(h->h_name, name) != 0)
    {
        LogDbg(("NAT: hostres: %s CNAME %s\n", name, h->h_name));
        nbytes = append_cname(res, name, h->h_name);
        if (nbytes > 0)
        {
            ++nanswers;
        }
        else
        {
            LogErr(("NAT: hostres: failed to add %s CNAME %s\n",
                    name, h->h_name));
            if (nbytes < 0)
                return refuse(res, RCode_ServFail);
            else
            {
                pHdr->tc = 1;
                goto out;
            }
        }

        /*
         * rfc1034#section-3.6.2 - ... a type CNAME or * query should
         * return just the CNAME.
         */
        if (qtype == Type_CNAME || qtype == Type_ANY)
            goto out;
    }
    else if (qtype == Type_CNAME)
    {
        LogDbg(("NAT: hostres: %s is already canonical\n", name));
        goto out; /* NB: RCode_NoError without an answer, not RCode_NXDomain */
    }

    /*
     * Emit A records.
     */
    for (i = 0; h->h_addr_list[i] != NULL; ++i)
    {
        const char *cname = h->h_name ? h->h_name : name;
        struct in_addr addr;

        addr.s_addr = *(uint32_t *)h->h_addr_list[i];
        nbytes = append_a(res, cname, addr);

        if (nbytes > 0)
        {
            ++nanswers;
        }
        else
        {
            LogErr(("NAT: hostres: failed to add %s A %RTnaipv4\n",
                    cname, addr.s_addr));
            if (nbytes < 0)
                return refuse(res, RCode_ServFail);
            else
            {
                pHdr->tc = 1;
                goto out;
            }
        }
    }

#if 0
    /*
     * It's not clear what to do with h_aliases.
     *
     * For names from the DNS it seems to contain the chain of CNAMEs,
     * starting with the original qname from the question.  So for
     * them we'd need to reply with a chain of:
     *
     *     h_aliases[i] CNAME h_aliases[i+1]
     *
     * OTOH, for the names from the hosts file it seems to contain all
     * the names except the first one (which is considered primary and
     * is reported as h_name).  In which case the reply should be:
     *
     *     h_aliases[i] CNAME h_name
     *
     * Obviously, we have no idea how the name was resolved, so we
     * generate at most one CNAME for h_host (if differs) and ignore
     * aliases altogehter.
     */
    for (i = 0; h->h_aliases[i] != NULL; ++i)
    {
        LogDbg(("NAT: hostres: ... %s\n", h->h_aliases[i]));
    }
#endif

  out:
    pHdr->ancount = RT_H2N_U16((uint16_t)nanswers);
    return VINF_SUCCESS;
}


static int
resolve_reverse(struct response *res, uint16_t qtype, size_t qname,
                struct in_addr in_addr_arpa)
{
    struct dnsmsg_header *pHdr;
    struct hostent *h;
    struct hostent hostent;
    char *h_aliases[1];
    char *h_addr_list[2];
    size_t oend;
    size_t nanswers;
    ssize_t nbytes;

    pHdr = (struct dnsmsg_header *)res->buf;
    nanswers = 0;
    oend = res->end;

    LogDbg(("NAT: hostres: %RTnaipv4\n", in_addr_arpa.s_addr));

    if (qtype != Type_PTR && qtype != Type_ANY)
    {
        /* can't answer CNAME to PTR queries using gethostby* */
        goto out; /* NB: RCode_NoError without an answer, not RCode_NXDomain */
    }

    h = NULL;
#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
    /*
     * If the address in the question is unknown to the real resolver
     * but has a mapping, and if we do the real lookup first, then the
     * guest will time out before our lookup times out and even though
     * we reply with the answer from the map, the answer will be lost.
     */
    {
        PDNSMAPPINGENTRY pReverseMapping = getDNSMapByAddr(res->pData, (const uint32_t *)&in_addr_arpa.s_addr);
        if (pReverseMapping != NULL)
        {
            LogDbg(("NAT: hostres: %RTnaipv4 resolved from mapping\n",
                    in_addr_arpa.s_addr));

            hostent.h_name = pReverseMapping->pszName;
            hostent.h_aliases = h_aliases;
            h_aliases[0] = NULL;
            hostent.h_addrtype = AF_INET;
            hostent.h_length = sizeof(RTNETADDRIPV4);
            hostent.h_addr_list = h_addr_list;
            h_addr_list[0] = (char *)&in_addr_arpa.s_addr;
            h_addr_list[1] = NULL;

            h = &hostent;
        }
    }
#endif

    if (h == NULL)
    {
#ifdef RT_OS_WINDOWS
        h = gethostbyaddr((const char *)&in_addr_arpa, sizeof(struct in_addr), AF_INET);
#else
        h = gethostbyaddr(&in_addr_arpa, sizeof(struct in_addr), AF_INET);
#endif
    }

    if (h == NULL)
    {
        /* LogErr: h_errno */
        return refuse(res, RCode_NXDomain);
    }

    if (h->h_name != NULL)
    {
        char name[DNS_MAX_NAME_LEN+1];
        strnlabels(name, sizeof(name), res->buf, qname);

        LogDbg(("NAT: hostres: %s PTR %s\n", name, h->h_name));
        nbytes = append_ptr(res, name, h->h_name);
        if (nbytes > 0)
        {
            ++nanswers;
        }
        else
        {
            LogErr(("NAT: hostres: failed to add %s PTR %s\n",
                    name, h->h_name));
            if (nbytes < 0)
                return refuse(res, RCode_ServFail);
            else
            {
                pHdr->tc = 1;
                goto out;
            }
        }
    }

  out:
    pHdr->ancount = RT_H2N_U16((uint16_t)nanswers);
    return VINF_SUCCESS;
}


static int
refuse(struct response *res, unsigned int rcode)
{
    struct dnsmsg_header *pHdr = (struct dnsmsg_header *)res->buf;
    pHdr->rcode = rcode;

    return VINF_SUCCESS;
}


#define APPEND_PROLOGUE()                       \
    ssize_t size = -1;                          \
    size_t oend = res->end;                     \
    ssize_t nbytes;                             \
    do {} while (0)

#define CHECKED(_append)                        \
    do {                                        \
        nbytes = (_append);                     \
        if (RT_UNLIKELY(nbytes <= 0))           \
        {                                       \
            if (nbytes == 0)                    \
                size = 0;                       \
            goto out;                           \
        }                                       \
    } while (0)

#define APPEND_EPILOGUE()                       \
    do {                                        \
        size = res->end - oend;                 \
      out:                                      \
        if (RT_UNLIKELY(size <= 0))             \
            res->end = oend;                    \
        return size;                            \
    } while (0)


/*
 * A RR - rfc1035#section-3.4.1
 */
static ssize_t
append_a(struct response *res, const char *name, struct in_addr addr)
{
    APPEND_PROLOGUE();

    CHECKED( append_rrhdr(res, name, Type_A, 3600) );
    CHECKED( append_u16(res, RT_H2N_U16_C(sizeof(addr))) );
    CHECKED( append_u32(res, addr.s_addr) );

    APPEND_EPILOGUE();
}


/*
 * CNAME RR - rfc1035#section-3.3.1
 */
static ssize_t
append_cname(struct response *res, const char *name, const char *cname)
{
    return append_name_rr(res, name, Type_CNAME, cname);
}


/*
 * PTR RR - rfc1035#section-3.3.12
 */
static ssize_t
append_ptr(struct response *res, const char *inaddrname, const char *name)
{
    return append_name_rr(res, inaddrname, Type_PTR, name);
}


static ssize_t
append_name_rr(struct response *res, const char *question,
               int type, const char *answer)
{
    size_t rdlpos;
    uint16_t rdlength;

    APPEND_PROLOGUE();

    CHECKED( append_rrhdr(res, question, type, 3600) );

    rdlpos = res->end;
    CHECKED( append_u16(res, 0) ); /* RDLENGTH placeholder */

    CHECKED( append_name(res, answer) );

    rdlength = RT_H2N_U16(nbytes);
    memcpy(&res->buf[rdlpos], &rdlength, sizeof(rdlength));

    APPEND_EPILOGUE();
}


/*
 * Append common RR header, up to but not including RDLENGTH and RDATA
 * proper (rfc1035#section-3.2.1).
 */
static ssize_t
append_rrhdr(struct response *res, const char *name, uint16_t type, uint32_t ttl)
{
    APPEND_PROLOGUE();

    CHECKED( append_name(res, name) );
    CHECKED( append_u16(res, RT_H2N_U16(type)) );
    CHECKED( append_u16(res, RT_H2N_U16_C(Class_IN)) );
    CHECKED( append_u32(res, RT_H2N_U32(ttl)) );

    APPEND_EPILOGUE();
}


static ssize_t
append_name(struct response *res, const char *name)
{
    ssize_t size, nbytes;
    struct label *root;
    struct label *haystack, *needle;
    struct label *head, **neck;
    struct label *tail, **graft;
    uint8_t *buf;
    size_t wr, oend;
    const char *s;

    size = -1;
    oend = res->end;

    /**
     * Split new name into a list of labels encoding it into the
     * temporary buffer.
     */
    root = NULL;

    buf = RTMemAllocZ(strlen(name) + 1);
    if (buf == NULL)
        return -1;
    wr = 0;

    s = name;
    while (*s != '\0') {
        const char *part;
        size_t poff, plen;
        struct label *l;

        part = s;
        while (*s != '\0' && *s != '.')
            ++s;

        plen = s - part;

        if (plen > DNS_MAX_LABEL_LEN)
        {
            LogErr(("NAT: hostres: name component too long\n"));
            goto out;
        }

        if (*s == '.')
        {
            if (plen == 0)
            {
                LogErr(("NAT: hostres: empty name component\n"));
                goto out;
            }

            ++s;
        }

        poff = wr;

        buf[poff] = (uint8_t)plen; /* length byte */
        ++wr;

        memcpy(&buf[wr], part, plen); /* label text */
        wr += plen;

        l = RTMemAllocZ(sizeof(*l));
        if (l == NULL)
            goto out;

        l->buf = buf;
        l->off = poff;
        l->children = root;
        root = l;
    }


    /**
     * Search for a tail that is already encoded in the message.
     */
    neck = &root;               /* where needle head is connected */
    needle = root;

    tail = NULL;                /* tail in the haystack */
    graft = &res->labels;
    haystack = res->labels;

    while (needle != NULL && haystack != NULL)
    {
        size_t nlen, hlen;

        nlen = needle->buf[needle->off];
        Assert((nlen & DNS_LABEL_PTR) == 0);

        hlen = haystack->buf[haystack->off];
        Assert((hlen & DNS_LABEL_PTR) == 0);

        if (   nlen == hlen
            && RTStrNICmp((char *)&needle->buf[needle->off+1],
                          (char *)&haystack->buf[haystack->off+1],
                          nlen) == 0)
        {
            neck = &needle->children;
            needle = needle->children;

            tail = haystack;
            graft = &haystack->children;
            haystack = haystack->children;
        }
        else
        {
            haystack = haystack->sibling;
        }
    }


    /**
     * Head contains (in reverse) the prefix that needs to be encoded
     * and added to the haystack.  Tail points to existing suffix that
     * can be compressed to a pointer into the haystack.
     */
    head = *neck;
    if (head != NULL)
    {
        struct label *l;
        size_t nlen, pfxlen, pfxdst;

        nlen = needle->buf[head->off]; /* last component */
        pfxlen = head->off + 1 + nlen; /* all prefix */
        pfxdst = res->end;             /* in response buffer */

        /* copy new prefix into response buffer */
        nbytes = append_bytes(res, buf, pfxlen);
        if (nbytes <= 0)
        {
            if (nbytes == 0)
                size = 0;
            goto out;
        }

        /* adjust labels to point to the response */
        for (l = head; l != NULL; l = l->children)
        {
            l->buf = res->buf;
            l->off += pfxdst;
        }

        *neck = NULL;           /* decapitate */

        l = *graft;             /* graft to the labels tree */
        *graft = head;
        head->sibling = l;
    }

    if (tail == NULL)
        nbytes = append_u8(res, 0);
    else
        nbytes = append_u16(res, RT_H2N_U16((DNS_LABEL_PTR << 8) | tail->off));
    if (nbytes <= 0)
    {
        if (nbytes == 0)
            size = 0;
        goto out;
    }

    size = res->end - oend;
  out:
    if (RT_UNLIKELY(size <= 0))
        res->end = oend;
    free_labels(root);
    RTMemFree(buf);
    return size;
}


static ssize_t
append_u32(struct response *res, uint32_t value)
{
    return append_bytes(res, (uint8_t *)&value, sizeof(value));
}


static ssize_t
append_u16(struct response *res, uint16_t value)
{
    return append_bytes(res, (uint8_t *)&value, sizeof(value));
}


static ssize_t
append_u8(struct response *res, uint8_t value)
{
    return append_bytes(res, &value, sizeof(value));
}


static ssize_t
append_bytes(struct response *res, uint8_t *p, size_t size)
{
    if (check_space(res, size) == 0)
        return 0;

    memcpy(&res->buf[res->end], p, size);
    res->end += size;
    return size;
}


static ssize_t
check_space(struct response *res, size_t size)
{
    if (   size > sizeof(res->buf)
        || res->end > sizeof(res->buf) - size)
        return 0;

    return size;
}


static int
get_in_addr_arpa(struct in_addr *paddr, struct label *root)
{
    RTNETADDRIPV4 addr;
    struct label *l;
    int i;
    RT_ZERO(addr); /* makes MSC happy*/

    l = root;
    if (l == NULL || labelstrcmp(l, "arpa") != 0)
        return 0;

    l = l->children;
    if (l == NULL || labelstrcmp(l, "in-addr") != 0)
        return 0;

    for (i = 0; i < 4; ++i)
    {
        char buf[4];
        size_t llen;
        int rc;
        uint8_t octet;

        l = l->children;
        if (l == NULL)
            return 0;

        llen = l->buf[l->off];
        Assert((llen & DNS_LABEL_PTR) == 0);

        /* valid octet values are at most 3 digits */
        if (llen > 3)
            return 0;

        /* copy to avoid dealing with trailing bytes */
        memcpy(buf, &l->buf[l->off + 1], llen);
        buf[llen] = '\0';

        rc = RTStrToUInt8Full(buf, 10, &octet);
        if (rc != VINF_SUCCESS)
            return 0;

        addr.au8[i] = octet;
    }

    if (l->children != NULL)
        return 0;               /* too many components */

    if (paddr != NULL)
        paddr->s_addr = addr.u;

    return 1;
}


/*
 * Compare label with string.
 */
static int
labelstrcmp(struct label *l, const char *s)
{
    size_t llen;

    llen = l->buf[l->off];
    Assert((llen & DNS_LABEL_PTR) == 0);

    return RTStrNICmp((char *)&l->buf[l->off + 1], s, llen);
}


/*
 * Convert a chain of labels to a C string.
 *
 * I'd rather use a custom formatter for e.g. %R[label] , but it needs
 * two arguments and microsoft VC doesn't support compound literals.
 */
static void
strnlabels(char *namebuf, size_t nbuflen, const uint8_t *msg, size_t off)
{
    size_t cb;
    size_t llen;

    namebuf[0] = '\0';
    cb = 0;

    llen = 0;

    while (cb < nbuflen - 1) {
        llen = msg[off];
        if ((llen & DNS_LABEL_PTR) == DNS_LABEL_PTR)
        {
            off = ((llen & ~DNS_LABEL_PTR) << 8) | msg[off + 1];
            llen = msg[off];
        }

        /* pointers to pointers should not happen */
        if ((llen & DNS_LABEL_PTR) != 0)
        {
            cb += RTStrPrintf(namebuf + cb, nbuflen - cb, "[???]");
            return;
        }

        if (llen == 0)
        {
            if (namebuf[0] == '\0')
                cb += RTStrPrintf(namebuf + cb, nbuflen - cb, ".");
            break;
        }

        if (namebuf[0] != '\0')
            cb += RTStrPrintf(namebuf + cb, nbuflen - cb, ".");

        cb += RTStrPrintf(namebuf + cb, nbuflen - cb,
                          "%.*s", llen, (char *)&msg[off+1]);
        off = off + 1 + llen;
    }
}


#if 0 /* unused */
static void
LogLabelsTree(const char *before, struct label *l, const char *after)
{
    size_t llen;

    if (before != NULL)
        LogDbg(("%s", before));

    if (l == NULL)
    {
        LogDbg(("NULL%s", after ? after : ""));
        return;
    }

    if (l->children)
        LogDbg(("("));

    if (l->buf != NULL)
    {
        llen = l->buf[l->off];
        if ((llen & DNS_LABEL_PTR) == 0)
        {
            LogDbg(("\"%.*s\"@%zu", llen, &l->buf[l->off+1], l->off));
        }
        else
        {
            LogDbg(("<invalid byte 0t%zu/0x%zf at offset %zd>",
                    llen, llen, l->off));
        }
    }
    else
    {
        LogDbg(("<*>"));
    }

    if (l->children)
        LogLabelsTree(" ", l->children, ")");

    if (l->sibling)
        LogLabelsTree(" ", l->sibling, NULL);

    if (after != NULL)
        LogDbg(("%s", after));
}
#endif /* unused */


static void
free_labels(struct label *root)
{
    struct label TOP;    /* traverse the tree with pointer reversal */
    struct label *b, *f;

    if (root == NULL)
        return;

    RT_ZERO(TOP);

    b = &TOP;
    f = root;

    while (f != &TOP) {
        if (f->children) {      /* recurse left */
            struct label *oldf = f;
            struct label *newf = f->children;
            oldf->children = b; /* reverse the pointer */
            b = oldf;
            f = newf;
        }
        else if (f->sibling) { /* turn right */
            f->children = f->sibling;
            f->sibling = NULL;
        }
        else {                  /* backtrack */
            struct label *oldf = f; /* garbage */
            struct label *oldb = b;
            b = oldb->children;
            oldb->children = NULL; /* oldf, but we are g/c'ing it */
            f = oldb;

            RTMemFree(oldf);
        }
    }
}

#ifdef VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER
void
slirp_add_host_resolver_mapping(PNATState pData,
                                const char *pszHostName, bool fPattern,
                                uint32_t u32HostIP)
{
    LogRel(("ENTER: pszHostName:%s%s, u32HostIP:%RTnaipv4\n",
                 pszHostName ? pszHostName : "(null)",
                 fPattern ? " (pattern)" : "",
                 u32HostIP));

    if (   pszHostName != NULL
        && u32HostIP != INADDR_ANY
        && u32HostIP != INADDR_BROADCAST)
    {
        PDNSMAPPINGENTRY pDnsMapping = RTMemAllocZ(sizeof(DNSMAPPINGENTRY));
        if (!pDnsMapping)
        {
            LogFunc(("Can't allocate DNSMAPPINGENTRY\n"));
            LogFlowFuncLeave();
            return;
        }

        pDnsMapping->u32IpAddress = u32HostIP;
        pDnsMapping->fPattern = fPattern;
        pDnsMapping->pszName = RTStrDup(pszHostName);

        if (pDnsMapping->pszName == NULL)
        {
            LogFunc(("Can't allocate enough room for host name\n"));
            RTMemFree(pDnsMapping);
            LogFlowFuncLeave();
            return;
        }

        if (fPattern) /* there's no case-insensitive pattern-match function */
            RTStrToLower(pDnsMapping->pszName);

        STAILQ_INSERT_TAIL(fPattern ? &pData->DNSMapPatterns : &pData->DNSMapNames,
                           pDnsMapping, MapList);

        LogRel(("NAT: User-defined mapping %s%s = %RTnaipv4 is registered\n",
                pDnsMapping->pszName,
                pDnsMapping->fPattern ? " (pattern)" : "",
                pDnsMapping->u32IpAddress));
    }
    LogFlowFuncLeave();
}


static PDNSMAPPINGENTRY
getDNSMapByName(PNATState pData, const char *pszName)
{
    PDNSMAPPINGENTRY pDNSMapingEntry;
    char *pszNameLower;

    pszNameLower = RTStrDup(pszName);
    if (RT_UNLIKELY(pszNameLower == NULL))
        return NULL;
    RTStrToLower(pszNameLower);

    STAILQ_FOREACH(pDNSMapingEntry, &pData->DNSMapNames, MapList)
    {
        if (RTStrICmp(pDNSMapingEntry->pszName, pszNameLower) == 0)
            goto done;
    }

    STAILQ_FOREACH(pDNSMapingEntry, &pData->DNSMapPatterns, MapList)
    {
        if (RTStrSimplePatternMultiMatch(pDNSMapingEntry->pszName, RTSTR_MAX,
                                         pszNameLower, RTSTR_MAX, NULL))
            goto done;
    }

  done:
    RTStrFree(pszNameLower);
    return pDNSMapingEntry;
}


static PDNSMAPPINGENTRY
getDNSMapByAddr(PNATState pData, const uint32_t *pu32IpAddress)
{
    PDNSMAPPINGENTRY pDNSMapingEntry;

    if (pu32IpAddress == NULL)
        return NULL;

    STAILQ_FOREACH(pDNSMapingEntry, &pData->DNSMapNames, MapList)
    {
        if (pDNSMapingEntry->u32IpAddress == *pu32IpAddress)
            return pDNSMapingEntry;
    }

    return NULL;
}


static void
alterHostentWithDataFromDNSMap(PNATState pData, struct hostent *h)
{
    PDNSMAPPINGENTRY pDNSMapingEntry = NULL;
    char **ppszAlias;

    if (h->h_name != NULL)
    {
        pDNSMapingEntry = getDNSMapByName(pData, h->h_name);
        if (pDNSMapingEntry != NULL)
            goto done;
    }

    for (ppszAlias = h->h_aliases; *ppszAlias != NULL; ++ppszAlias)
    {
        pDNSMapingEntry = getDNSMapByName(pData, *ppszAlias);
        if (pDNSMapingEntry != NULL)
            goto done;
    }

  done:
    if (pDNSMapingEntry != NULL)
    {
        *(uint32_t *)h->h_addr_list[0] = pDNSMapingEntry->u32IpAddress;
        h->h_addr_list[1] = NULL;
    }
}
#endif /* VBOX_WITH_DNSMAPPING_IN_HOSTRESOLVER */
