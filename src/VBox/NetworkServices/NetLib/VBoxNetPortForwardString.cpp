/* $Id: VBoxNetPortForwardString.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetPortForwardString - Routines for managing port-forward strings.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifndef RT_OS_WINDOWS
# include <netinet/in.h>
#else
# include <iprt/win/winsock2.h>
# include <Ws2ipdef.h>
#endif

#include <iprt/cdefs.h>
#include <iprt/cidr.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/net.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include <VBox/log.h>

#include "VBoxPortForwardString.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define PF_FIELD_SEPARATOR ':'
#define PF_ADDRESS_FIELD_STARTS '['
#define PF_ADDRESS_FIELD_ENDS ']'

#define PF_STR_FIELD_SEPARATOR ":"
#define PF_STR_ADDRESS_FIELD_STARTS "["
#define PF_STR_ADDRESS_FIELD_ENDS "]"


static int netPfStrAddressParse(char *pszRaw, size_t cchRaw,
                                char *pszAddress, int cbAddress,
                                bool fEmptyAcceptable)
{
    size_t cchField = 0;

    AssertPtrReturn(pszRaw, -1);
    AssertPtrReturn(pszAddress, -1);
    AssertReturn(pszRaw[0] == PF_ADDRESS_FIELD_STARTS, -1);

    /* shift pszRaw to next symbol */
    pszRaw++;
    cchRaw--;

    /* we shouldn't face with ending here */
    AssertReturn(cchRaw > 0, VERR_INVALID_PARAMETER);

    char *pszEndOfAddress = RTStrStr(pszRaw, PF_STR_ADDRESS_FIELD_ENDS);

    /* no pair closing sign */
    AssertPtrReturn(pszEndOfAddress, VERR_INVALID_PARAMETER);

    cchField = pszEndOfAddress - pszRaw;

    /* field should be less then the rest of the string */
    AssertReturn(cchField < cchRaw, VERR_INVALID_PARAMETER);

    if (cchField != 0)
        RTStrCopy(pszAddress, RT_MIN(cchField + 1, (size_t)cbAddress), pszRaw);
    else if (!fEmptyAcceptable)
        return -1;

    AssertReturn(pszRaw[cchField] == PF_ADDRESS_FIELD_ENDS, -1);
    return (int)cchField + 2; /* length of the field and closing braces */
}


/**
 * Parses a port something.
 *
 * @returns Offset relative to @a pszRaw of the end of the port field.
 *          -1 on failure.
 * @param   pszRaw          The zero terminated string to parse.  Points a field
 *                          separator.
 * @param   pu16Port        Where to store the port number on success.
 */
static int netPfStrPortParse(char *pszRaw, uint16_t *pu16Port)
{
#if 1
    AssertPtrReturn(pszRaw, -1);
    AssertPtrReturn(pu16Port, -1);
    AssertReturn(pszRaw[0] == PF_FIELD_SEPARATOR, -1);

    char *pszNext = NULL;
    int rc = RTStrToUInt16Ex(&pszRaw[1], &pszNext, 0, pu16Port);
    if (rc == VWRN_TRAILING_CHARS)
        AssertReturn(*pszNext == PF_FIELD_SEPARATOR, -1);
    else if (rc == VINF_SUCCESS)
        Assert(*pszNext == '\0');
    else
        AssertMsgFailedReturn(("rc=%Rrc\n", rc), -1);
    if (*pu16Port == 0)
        return -1;
    return (int)(pszNext - pszRaw);

#else /* The same code, just a little more verbose: */
    char *pszEndOfPort = NULL;
    uint16_t u16Port = 0;
    int idxRaw = 1; /* we increment pszRaw after checks. */
    int cbRest = 0;
    size_t cbPort = 0;

    AssertPtrReturn(pszRaw, -1);
    AssertPtrReturn(pu16Port, -1);
    AssertReturn(pszRaw[0] == PF_FIELD_SEPARATOR, -1);

    pszRaw++; /* skip field separator */
    cchRaw --;

    char *pszEndOfPort = RTStrStr(pszRaw, ":");
    if (!pszEndOfPort)
    {
        cbRest = strlen(pszRaw);

        Assert(cchRaw == cbRest);

        /* XXX: Assumption that if string is too big, it will be reported by
         * RTStrToUint16.
         */
        if (cbRest > 0)
        {
            pszEndOfPort = pszRaw + cbRest;
            cbPort = cbRest;
        }
        else
            return -1;
    }
    else
        cbPort = pszEndOfPort - pszRaw;


    idxRaw += cbPort;

    Assert(cbRest || pszRaw[idxRaw - 1] == PF_FIELD_SEPARATOR); /* we are 1 char ahead */

    char szPort[10];
    RT_ZERO(szPort);

    Assert(idxRaw > 0);
    RTStrCopy(szPort, RT_MIN(sizeof(szPort), (size_t)(cbPort) + 1), pszRaw);

    if (!(u16Port = RTStrToUInt16(szPort)))
        return -1;

    *pu16Port = u16Port;

     return idxRaw;
#endif
}


static int netPfStrAddressPortPairParse(char *pszRaw, size_t cchRaw,
                                        char *pszAddress, int cbAddress,
                                        bool fEmptyAddressAcceptable,
                                        uint16_t *pu16Port)
{
    int idxRaw = 0;
    int idxRawTotal = 0;

    AssertPtrReturn(pszRaw, -1);
    AssertPtrReturn(pszAddress, -1);
    AssertPtrReturn(pu16Port, -2);

    /* XXX: Here we should check 0 - ':' and 1 - '[' */
    Assert(   pszRaw[0] == PF_FIELD_SEPARATOR
              && pszRaw[1] == PF_ADDRESS_FIELD_STARTS);

    pszRaw++; /* field separator skip */
    cchRaw--;
    AssertReturn(cchRaw > 0, VERR_INVALID_PARAMETER);

    idxRaw = 0;

    if (pszRaw[0] == PF_ADDRESS_FIELD_STARTS)
    {
        idxRaw += netPfStrAddressParse(pszRaw,
                                       cchRaw - idxRaw,
                                       pszAddress,
                                       cbAddress,
                                       fEmptyAddressAcceptable);
        if (idxRaw == -1)
            return -1;

        Assert(pszRaw[idxRaw] == PF_FIELD_SEPARATOR);
    }
    else return -1;

    pszRaw += idxRaw;
    idxRawTotal += idxRaw;
    cchRaw -= idxRaw;

    AssertReturn(cchRaw > 0, VERR_INVALID_PARAMETER);

    idxRaw = 0;

    Assert(pszRaw[0] == PF_FIELD_SEPARATOR);

    if (pszRaw[0] == PF_FIELD_SEPARATOR)
    {
        idxRaw = netPfStrPortParse(pszRaw, pu16Port);

        Assert(strlen(&pszRaw[idxRaw]) == 0 || pszRaw[idxRaw] == PF_FIELD_SEPARATOR);

        if (idxRaw == -1)
            return -2;

        idxRawTotal += idxRaw;

        return idxRawTotal + 1;
    }
    else return -1; /* trailing garbage in the address */
}

/* XXX: Having fIPv6 we might emprove adress verification comparing address length
 * with INET[6]_ADDRLEN
 *
 */
int netPfStrToPf(const char *pcszStrPortForward, bool fIPv6, PPORTFORWARDRULE pPfr)
{
/** r=bird: Redo from scratch?  This is very hard to read.  And it's going about
 *  things in a very complicated, potentially leaky (pszRaw) fashion. */

    int  proto;
    uint16_t u16HostPort;
    uint16_t u16GuestPort;
    bool fTcpProto = false;

    int idxRaw = 0;
    int cbToken = 0;

    AssertPtrReturn(pcszStrPortForward, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPfr, VERR_INVALID_PARAMETER);

    RT_ZERO(*pPfr);

    char *pszHostAddr = &pPfr->szPfrHostAddr[0];
    char *pszGuestAddr = &pPfr->szPfrGuestAddr[0];
    char *pszName = &pPfr->szPfrName[0];

    size_t cchRaw = strlen(pcszStrPortForward);

    /* Minimal rule ":tcp:[]:0:[]:0" has got lenght 14 */
    AssertReturn(cchRaw > 14, VERR_INVALID_PARAMETER);

    char *pszRaw = RTStrDup(pcszStrPortForward);
    AssertReturn(pszRaw, VERR_NO_MEMORY);

    char *pszRawBegin = pszRaw;

    /* name */
    if (pszRaw[idxRaw] != PF_FIELD_SEPARATOR)
    {
        char *pszEndOfName = RTStrStr(pszRaw + 1, PF_STR_FIELD_SEPARATOR);
        if (!pszEndOfName)
            goto invalid_parameter;

        cbToken = pszEndOfName - pszRaw; /* don't take : into account */
        /* XXX it's unacceptable to have only name entry in PF */
        AssertReturn(cbToken < (ssize_t)cchRaw, VERR_INVALID_PARAMETER);

        if (   cbToken < 0
            || (size_t)cbToken >= PF_NAMELEN)
            goto invalid_parameter;

        RTStrCopy(pszName,
                  RT_MIN((size_t)cbToken + 1, PF_NAMELEN),
                  pszRaw);
        pszRaw += cbToken; /* move to separator */
        cchRaw -= cbToken;
    }

    AssertReturn(pszRaw[0] == PF_FIELD_SEPARATOR, VERR_INVALID_PARAMETER);
    /* protocol */

    pszRaw++; /* skip separator */
    cchRaw--;
    idxRaw = 0;

    if (  (  (fTcpProto = (RTStrNICmp(pszRaw, "tcp", 3) == 0))
           ||              RTStrNICmp(pszRaw, "udp", 3) == 0)
        && pszRaw[3] == PF_FIELD_SEPARATOR)
    {
        proto = (fTcpProto ? IPPROTO_TCP : IPPROTO_UDP);
        idxRaw = 3;
    }
    else
        goto invalid_parameter;

    pszRaw += idxRaw;
    cchRaw -= idxRaw;

    idxRaw = netPfStrAddressPortPairParse(pszRaw, cchRaw,
                                          pszHostAddr, INET6_ADDRSTRLEN,
                                          true, &u16HostPort);
    if (idxRaw < 0)
        return VERR_INVALID_PARAMETER;

    pszRaw += idxRaw;
    cchRaw -= idxRaw;

    Assert(pszRaw[0] == PF_FIELD_SEPARATOR);

    idxRaw = netPfStrAddressPortPairParse(pszRaw, cchRaw,
                                          pszGuestAddr, INET6_ADDRSTRLEN,
                                          false, &u16GuestPort);

    if (idxRaw < 0)
        goto invalid_parameter;

    /* XXX: fill the rule */
    pPfr->fPfrIPv6 = fIPv6;
    pPfr->iPfrProto = proto;

    pPfr->u16PfrHostPort = u16HostPort;

    if (*pszGuestAddr == '\0')
        goto invalid_parameter; /* guest address should be defined */

    pPfr->u16PfrGuestPort = u16GuestPort;

    Log(("name: %s\n"
         "proto: %d\n"
         "host address: %s\n"
         "host port: %d\n"
         "guest address: %s\n"
         "guest port:%d\n",
         pszName, proto,
         pszHostAddr, u16HostPort,
         pszGuestAddr, u16GuestPort));

    RTStrFree(pszRawBegin);
    return VINF_SUCCESS;

invalid_parameter:
    RTStrFree(pszRawBegin);
    RT_ZERO(*pPfr);
    return VERR_INVALID_PARAMETER;
}
