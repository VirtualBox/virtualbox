/* $Id: resolv_conf_parser.c 110275 2025-07-16 18:34:13Z jack.doherty@oracle.com $ */
/** @file
 * resolv_conf_parser.c - parser of resolv.conf resolver(5)
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifdef RCP_STANDALONE
#define IN_RING3
#endif

#ifndef LOG_GROUP
# define LOG_GROUP LOG_GROUP_DRV_NAT
#endif

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/net.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/thread.h>

#include <VBox/log.h>

#ifdef RT_OS_FREEBSD
# include <sys/socket.h>
#endif

#include <arpa/inet.h>

#include "resolv_conf_parser.h"

#if !defined(RCP_ACCEPT_PORT)
# if defined(RT_OS_DARWIN)
#  define RCP_ACCEPT_PORT
# endif
#endif

static int rcp_address_trailer(char **ppszNext, PRTNETADDR pNetAddr, RTNETADDRTYPE enmType);
static char *getToken(char *psz, char **ppszSavePtr);

#if 0
#undef  Log2
#define Log2 LogRel
#endif

#ifdef RCP_STANDALONE
#undef  LogRel
#define LogRel(a) RTPrintf a
#endif


#ifdef RCP_STANDALONE
int main(int argc, char **argv)
{
    struct rcp_state state;
    int i;
    int rc;

    rc = rcp_parse(&state, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf(">>> Failed: %Rrc\n", rc);
        return 1;
    }

    RTPrintf(">>> Success:\n");

    RTPrintf("rcps_num_nameserver = %u\n", state.rcps_num_nameserver);
    for (i = 0; i < state.rcps_num_nameserver; ++i)
    {
        if (state.rcps_str_nameserver[i] == NULL)
            LogRel(("  nameserver %RTnaddr\n",
                    &state.rcps_nameserver[i]));
        else
            LogRel(("  nameserver %RTnaddr (from \"%s\")\n",
                    &state.rcps_nameserver[i], state.rcps_str_nameserver[i]));
    }

    if (state.rcps_domain != NULL)
        RTPrintf("domain %s\n", state.rcps_domain);

    RTPrintf("rcps_num_searchlist = %u\n", state.rcps_num_searchlist);
    for (i = 0; i < state.rcps_num_searchlist; ++i)
    {
        RTPrintf("... %s\n", state.rcps_searchlist[i] ? state.rcps_searchlist[i] : "(null)");
    }

    return 0;
}
#endif


int rcp_parse(struct rcp_state *state, const char *filename)
{
    PRTSTREAM stream;
#   define RCP_BUFFER_SIZE 256
    char buf[RCP_BUFFER_SIZE];
    char *pszAddrBuf;
    size_t cbAddrBuf;
    char *pszSearchBuf;
    size_t cbSearchBuf;
    uint32_t flags;
#ifdef RCP_ACCEPT_PORT /* OS X extention */
    uint32_t default_port = RTNETADDR_PORT_NA;
#endif
    unsigned i;
    int rc;

    AssertPtrReturn(state, VERR_INVALID_PARAMETER);
    flags = state->rcps_flags;

    RT_ZERO(*state);
    state->rcps_flags = flags;

    if (RT_UNLIKELY(filename == NULL))
    {
#ifdef RCP_STANDALONE
        stream = g_pStdIn;      /* for testing/debugging */
#else
        return VERR_INVALID_PARAMETER;
#endif
    }
    else
    {
        rc = RTStrmOpen(filename, "r", &stream);
        if (RT_FAILURE(rc))
            return rc;
    }


    pszAddrBuf = state->rcps_nameserver_str_buffer;
    cbAddrBuf = sizeof(state->rcps_nameserver_str_buffer);

    pszSearchBuf = state->rcps_searchlist_buffer;
    cbSearchBuf = sizeof(state->rcps_searchlist_buffer);

    for (;;)
    {
        char *s, *tok;

        rc = RTStrmGetLine(stream, buf, sizeof(buf));
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_EOF)
                rc = VINF_SUCCESS;
            break;
        }

        /*
         * Strip comment if present.
         *
         * This is not how ad-hoc parser in bind's res_init.c does it,
         * btw, so this code will accept more input as valid compared
         * to res_init.  (e.g. "nameserver 1.1.1.1; comment" is
         * misparsed by res_init).
         */
        for (s = buf; *s != '\0'; ++s)
        {
            if (*s == '#' || *s == ';')
            {
                *s = '\0';
                break;
            }
        }

        tok = getToken(buf, &s);
        if (tok == NULL)
            continue;


        /*
         * NAMESERVER
         */
        if (RTStrCmp(tok, "nameserver") == 0)
        {
            RTNETADDR NetAddr;
            const char *pszAddr;
            char *pszNext;

            if (RT_UNLIKELY(state->rcps_num_nameserver >= RCPS_MAX_NAMESERVERS))
            {
                LogRel(("NAT: resolv.conf: too many nameserver lines, ignoring %s\n", s));
                continue;
            }

            /* XXX: TODO: don't save strings unless asked to */
            if (RT_UNLIKELY(cbAddrBuf == 0))
            {
                LogRel(("NAT: resolv.conf: no buffer space, ignoring %s\n", s));
                continue;
            }


            /*
             * parse next token as an IP address
             */
            tok = getToken(NULL, &s);
            if (tok == NULL)
            {
                LogRel(("NAT: resolv.conf: nameserver line without value\n"));
                continue;
            }

            pszAddr = tok;
            RT_ZERO(NetAddr);
            NetAddr.uPort = RTNETADDR_PORT_NA;

            /* if (NetAddr.enmType == RTNETADDRTYPE_INVALID) */
            {
                rc = RTNetStrToIPv4AddrEx(tok, &NetAddr.uAddr.IPv4, &pszNext);
                if (RT_SUCCESS(rc))
                {
                    rc = rcp_address_trailer(&pszNext, &NetAddr, RTNETADDRTYPE_IPV4);
                    if (RT_FAILURE(rc))
                    {
                        LogRel(("NAT: resolv.conf: garbage at the end of IPv4 address %s\n", tok));
                        continue;
                    }

                    LogRel(("NAT: resolv.conf: nameserver %RTnaddr\n", &NetAddr));
                }
            } /* IPv4 */

            if (NetAddr.enmType == RTNETADDRTYPE_INVALID)
            {
                rc = RTNetStrToIPv6AddrEx(tok, &NetAddr.uAddr.IPv6, &pszNext);
                if (RT_SUCCESS(rc))
                {
                    if (*pszNext == '%') /* XXX: TODO: IPv6 zones */
                    {
                        size_t zlen = RTStrOffCharOrTerm(pszNext, '.');
                        LogRel(("NAT: resolv.conf: FIXME: ignoring IPv6 zone %*.*s\n",
                                zlen, zlen, pszNext));
                        pszNext += zlen;
                    }

                    rc = rcp_address_trailer(&pszNext, &NetAddr, RTNETADDRTYPE_IPV6);
                    if (RT_FAILURE(rc))
                    {
                        LogRel(("NAT: resolv.conf: garbage at the end of IPv6 address %s\n", tok));
                        continue;
                    }

                    LogRel(("NAT: resolv.conf: nameserver %RTnaddr\n", &NetAddr));
                }
            } /* IPv6 */

            if (NetAddr.enmType == RTNETADDRTYPE_INVALID)
            {
                LogRel(("NAT: resolv.conf: bad nameserver address %s\n", tok));
                continue;
            }


            tok = getToken(NULL, &s);
            if (tok != NULL)
                LogRel(("NAT: resolv.conf: ignoring unexpected trailer on the nameserver line\n"));

            if ((flags & RCPSF_IGNORE_IPV6) && NetAddr.enmType == RTNETADDRTYPE_IPV6)
            {
                Log2(("NAT: resolv.conf: IPv6 address ignored\n"));
                continue;
            }

            /* seems ok, save it */
            {
                i = state->rcps_num_nameserver;

                state->rcps_nameserver[i] = NetAddr;

                /* XXX: TODO: don't save strings unless asked to */
                Log2(("NAT: resolv.conf: saving address @%td,+%zu\n",
                      pszAddrBuf - state->rcps_nameserver_str_buffer, cbAddrBuf));
                state->rcps_str_nameserver[i] = pszAddrBuf;
                rc = RTStrCopyP(&pszAddrBuf, &cbAddrBuf, pszAddr);
                if (RT_SUCCESS(rc))
                {
                    ++pszAddrBuf; /* skip '\0' */
                    if (cbAddrBuf > 0) /* on overflow we get 1 (for the '\0'), but be defensive */
                        --cbAddrBuf;
                    ++state->rcps_num_nameserver;
                }
                else
                {
                    Log2(("NAT: resolv.conf: ... truncated\n"));
                }
            }

            continue;
        }


#ifdef RCP_ACCEPT_PORT /* OS X extention */
        /*
         * PORT
         */
        if (RTStrCmp(tok, "port") == 0)
        {
            uint16_t port;

            if (default_port != RTNETADDR_PORT_NA)
            {
                LogRel(("NAT: resolv.conf: ignoring multiple port lines\n"));
                continue;
            }

            tok = getToken(NULL, &s);
            if (tok == NULL)
            {
                LogRel(("NAT: resolv.conf: port line without value\n"));
                continue;
            }

            rc = RTStrToUInt16Full(tok, 10, &port);
            if (RT_SUCCESS(rc))
            {
                if (port != 0)
                    default_port = port;
                else
                    LogRel(("NAT: resolv.conf: port 0 is invalid\n"));
            }

            continue;
        }
#endif


        /*
         * DOMAIN
         */
        if (RTStrCmp(tok, "domain") == 0)
        {
            if (state->rcps_domain != NULL)
            {
                LogRel(("NAT: resolv.conf: ignoring multiple domain lines\n"));
                continue;
            }

            tok = getToken(NULL, &s);
            if (tok == NULL)
            {
                LogRel(("NAT: resolv.conf: domain line without value\n"));
                continue;
            }

            rc = RTStrCopy(state->rcps_domain_buffer, sizeof(state->rcps_domain_buffer), tok);
            if (RT_SUCCESS(rc))
            {
                state->rcps_domain = state->rcps_domain_buffer;
            }
            else
            {
                LogRel(("NAT: resolv.conf: domain name too long\n"));
                RT_ZERO(state->rcps_domain_buffer);
            }

            continue;
        }


        /*
         * SEARCH
         */
        if (RTStrCmp(tok, "search") == 0)
        {
            while ((tok = getToken(NULL, &s)) && tok != NULL)
            {
                i = state->rcps_num_searchlist;
                if (RT_UNLIKELY(i >= RCPS_MAX_SEARCHLIST))
                {
                    LogRel(("NAT: resolv.conf: too many search domains, ignoring %s\n", tok));
                    continue;
                }

                Log2(("NAT: resolv.conf: saving search %s @%td,+%zu\n",
                      tok, pszSearchBuf - state->rcps_searchlist_buffer, cbSearchBuf));
                state->rcps_searchlist[i] = pszSearchBuf;
                rc = RTStrCopyP(&pszSearchBuf, &cbSearchBuf, tok);
                if (RT_SUCCESS(rc))
                {
                    ++pszSearchBuf; /* skip '\0' */
                    if (cbSearchBuf > 0) /* on overflow we get 1 (for the '\0'), but be defensive */
                        --cbSearchBuf;
                    ++state->rcps_num_searchlist;
                }
                else
                {
                    LogRel(("NAT: resolv.conf: no buffer space, ignoring search domain %s\n", tok));
                    pszSearchBuf = state->rcps_searchlist[i];
                    cbSearchBuf = sizeof(state->rcps_searchlist_buffer)
                        - (pszSearchBuf - state->rcps_searchlist_buffer);
                    Log2(("NAT: resolv.conf: backtracking to @%td,+%zu\n",
                          pszSearchBuf - state->rcps_searchlist_buffer, cbSearchBuf));
                }
            }

            continue;
        }


        LogRel(("NAT: resolv.conf: ignoring \"%s %s\"\n", tok, s));
    }

    if (filename != NULL)
        RTStrmClose(stream);

    if (RT_FAILURE(rc))
        return rc;


    /* XXX: I don't like that OS X would return a different result here */
#ifdef RCP_ACCEPT_PORT /* OS X extention */
    if (default_port == RTNETADDR_PORT_NA)
        default_port = 53;

    for (i = 0; i < state->rcps_num_nameserver; ++i)
    {
        RTNETADDR *addr = &state->rcps_nameserver[i];
        if (addr->uPort == RTNETADDR_PORT_NA || addr->uPort == 0)
            addr->uPort = (uint16_t)default_port;
    }
#endif

    if (   state->rcps_domain == NULL
        && state->rcps_num_searchlist > 0)
    {
        state->rcps_domain = state->rcps_searchlist[0];
    }

    return VINF_SUCCESS;
}


static int
rcp_address_trailer(char **ppszNext, PRTNETADDR pNetAddr, RTNETADDRTYPE enmType)
{
    char *pszNext = *ppszNext;
    int rc = VINF_SUCCESS;

    if (*pszNext == '\0')
    {
        pNetAddr->enmType = enmType;
        rc = VINF_SUCCESS;
    }
#ifdef RCP_ACCEPT_PORT /* OS X extention */
    else if (*pszNext == '.')
    {
        uint16_t port;

        rc = RTStrToUInt16Ex(++pszNext, NULL, 10, &port);
        if (RT_SUCCESS(rc))
        {
            pNetAddr->enmType = enmType;
            pNetAddr->uPort = port;
        }
    }
#endif
    else
    {
        rc = VERR_TRAILING_CHARS;
    }

    return rc;
}


static char *getToken(char *psz, char **ppszSavePtr)
{
    char *pszToken;

    AssertPtrReturn(ppszSavePtr, NULL);

    if (psz == NULL)
    {
        psz = *ppszSavePtr;
        if (psz == NULL)
            return NULL;
    }

    while (*psz == ' ' || *psz == '\t')
        ++psz;

    if (*psz == '\0')
    {
        *ppszSavePtr = NULL;
        return NULL;
    }

    pszToken = psz;
    while (*psz && *psz != ' ' && *psz != '\t')
        ++psz;

    if (*psz == '\0')
        psz = NULL;
    else
        *psz++ = '\0';

    *ppszSavePtr = psz;
    return pszToken;
}
