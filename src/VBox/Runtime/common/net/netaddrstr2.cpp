/* $Id: netaddrstr2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Network Address String Handling.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/net.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include "internal/string.h"


static int rtNetStrToIPv4AddrEx(const char *pcszAddr, PRTNETADDRIPV4 pAddr,
                                char **ppszNext)
{
    char *pszNext;
    int rc;

    AssertPtrReturn(pcszAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pAddr, VERR_INVALID_PARAMETER);

    rc = RTStrToUInt8Ex(pcszAddr, &pszNext, 10, &pAddr->au8[0]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != '.')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[1]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != '.')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[2]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;
    if (*pszNext++ != '.')
        return VERR_INVALID_PARAMETER;

    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &pAddr->au8[3]);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;

    if (ppszNext != NULL)
        *ppszNext = pszNext;
    return rc;
}


RTDECL(int) RTNetStrToIPv4AddrEx(const char *pcszAddr, PRTNETADDRIPV4 pAddr,
                                 char **ppszNext)
{
    return rtNetStrToIPv4AddrEx(pcszAddr, pAddr, ppszNext);
}
RT_EXPORT_SYMBOL(RTNetStrToIPv4AddrEx);


RTDECL(int) RTNetStrToIPv4Addr(const char *pcszAddr, PRTNETADDRIPV4 pAddr)
{
    char *pszNext;
    int rc;

    AssertPtrReturn(pcszAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pAddr, VERR_INVALID_PARAMETER);

    pcszAddr = RTStrStripL(pcszAddr);
    rc = rtNetStrToIPv4AddrEx(pcszAddr, pAddr, &pszNext);
    if (RT_FAILURE(rc) || rc == VWRN_TRAILING_CHARS)
        return VERR_INVALID_PARAMETER;

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetStrToIPv4Addr);


RTDECL(bool) RTNetIsIPv4AddrStr(const char *pcszAddr)
{
    RTNETADDRIPV4 addrIPv4;
    char *pszNext;
    int rc;

    if (pcszAddr == NULL)
        return false;

    rc = rtNetStrToIPv4AddrEx(pcszAddr, &addrIPv4, &pszNext);
    if (rc != VINF_SUCCESS)
        return false;

    if (*pszNext != '\0')
        return false;

    return true;
}
RT_EXPORT_SYMBOL(RTNetIsIPv4AddrStr);


RTDECL(bool) RTNetStrIsIPv4AddrAny(const char *pcszAddr)
{
    RTNETADDRIPV4 addrIPv4;
    char *pszNext;
    int rc;

    if (pcszAddr == NULL)
        return false;

    pcszAddr = RTStrStripL(pcszAddr);
    rc = rtNetStrToIPv4AddrEx(pcszAddr, &addrIPv4, &pszNext);
    if (RT_FAILURE(rc) || rc == VWRN_TRAILING_CHARS)
        return false;

    if (addrIPv4.u != 0u)       /* INADDR_ANY? */
        return false;

    return true;
}
RT_EXPORT_SYMBOL(RTNetStrIsIPv4AddrAny);


RTDECL(int) RTNetMaskToPrefixIPv4(PCRTNETADDRIPV4 pMask, int *piPrefix)
{
    AssertReturn(pMask != NULL, VERR_INVALID_PARAMETER);

    if (pMask->u == 0)
    {
        if (piPrefix != NULL)
            *piPrefix = 0;
        return VINF_SUCCESS;
    }

    const uint32_t uMask = RT_N2H_U32(pMask->u);

    uint32_t uPrefixMask = UINT32_C(0xffffffff);
    int iPrefixLen = 32;

    while (iPrefixLen > 0)
    {
        if (uMask == uPrefixMask)
        {
            if (piPrefix != NULL)
                *piPrefix = iPrefixLen;
            return VINF_SUCCESS;
        }

        --iPrefixLen;
        uPrefixMask <<= 1;
    }

    return VERR_INVALID_PARAMETER;
}
RT_EXPORT_SYMBOL(RTNetMaskToPrefixIPv4);


RTDECL(int) RTNetPrefixToMaskIPv4(int iPrefix, PRTNETADDRIPV4 pMask)
{
    AssertReturn(pMask != NULL, VERR_INVALID_PARAMETER);

    if (RT_UNLIKELY(iPrefix < 0 || 32 < iPrefix))
        return VERR_INVALID_PARAMETER;

    if (RT_LIKELY(iPrefix != 0))
        pMask->u = RT_H2N_U32(UINT32_C(0xffffffff) << (32 - iPrefix));
    else /* avoid UB in the shift */
        pMask->u = 0;

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetPrefixToMaskIPv4);


RTDECL(int) RTNetStrToIPv4Cidr(const char *pcszAddr, PRTNETADDRIPV4 pAddr, int *piPrefix)
{
    RTNETADDRIPV4 Addr, Mask;
    uint8_t u8Prefix;
    char *pszNext;
    int rc;

    AssertPtrReturn(pcszAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(piPrefix, VERR_INVALID_PARAMETER);

    pcszAddr = RTStrStripL(pcszAddr);
    rc = rtNetStrToIPv4AddrEx(pcszAddr, &Addr, &pszNext);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * If the prefix is missing, treat is as exact (/32) address
     * specification.
     */
    if (*pszNext == '\0' || rc == VWRN_TRAILING_SPACES)
    {
        *pAddr = Addr;
        *piPrefix = 32;
        return VINF_SUCCESS;
    }

    /*
     * Be flexible about the way the prefix is specified after the
     * slash: accept both the prefix length and the netmask, and for
     * the latter accept both dotted-decimal and hex.  The inputs we
     * convert here are likely coming from a user and people have
     * different preferences.  Sometimes they just remember specific
     * different networks in specific formats!
     */
    if (*pszNext == '/')
        ++pszNext;
    else
        return VERR_INVALID_PARAMETER;

    /* .../0x... is a hex mask */
    if (pszNext[0] == '0' && (pszNext[1] == 'x' || pszNext[1] == 'X'))
    {
        rc = RTStrToUInt32Ex(pszNext, &pszNext, 16, &Mask.u);
        if (rc == VINF_SUCCESS || rc == VWRN_TRAILING_SPACES)
            Mask.u = RT_H2N_U32(Mask.u);
        else
            return VERR_INVALID_PARAMETER;

        int iPrefix;
        rc = RTNetMaskToPrefixIPv4(&Mask, &iPrefix);
        if (RT_SUCCESS(rc))
            u8Prefix = (uint8_t)iPrefix;
        else
            return VERR_INVALID_PARAMETER;
    }
    else
    {
        char *pszLookAhead;
        uint32_t u32;
        rc = RTStrToUInt32Ex(pszNext, &pszLookAhead, 10, &u32);

        /* single number after the slash is prefix length */
        if (rc == VINF_SUCCESS || rc == VWRN_TRAILING_SPACES)
        {
            if (u32 <= 32)
                u8Prefix = (uint8_t)u32;
            else
                return VERR_INVALID_PARAMETER;
        }
        /* a number followed by more stuff, may be a dotted-decimal */
        else if (rc == VWRN_TRAILING_CHARS)
        {
            if (*pszLookAhead != '.') /* don't even bother checking */
                return VERR_INVALID_PARAMETER;

            rc = rtNetStrToIPv4AddrEx(pszNext, &Mask, &pszNext);
            if (rc == VINF_SUCCESS || rc == VWRN_TRAILING_SPACES)
            {
                int iPrefix;
                rc = RTNetMaskToPrefixIPv4(&Mask, &iPrefix);
                if (RT_SUCCESS(rc))
                    u8Prefix = (uint8_t)iPrefix;
                else
                    return VERR_INVALID_PARAMETER;
            }
            else
                return VERR_INVALID_PARAMETER;
        }
        /* failed to convert to number */
        else
            return VERR_INVALID_PARAMETER;
    }

    if (u8Prefix > 32)
        return VERR_INVALID_PARAMETER;

    *pAddr = Addr;
    *piPrefix = u8Prefix;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetStrToIPv4Cidr);


static int rtNetStrToHexGroup(const char *pcszValue, char **ppszNext,
                              uint16_t *pu16)
{
    char *pszNext;
    int rc;

    rc = RTStrToUInt16Ex(pcszValue, &pszNext, 16, pu16);
    if (RT_FAILURE(rc))
        return rc;

    if (   rc != VINF_SUCCESS
        && rc != VWRN_TRAILING_CHARS
        && rc != VWRN_TRAILING_SPACES)
    {
        return -rc;             /* convert warning to error */
    }

    /* parser always accepts 0x prefix */
    if (pcszValue[0] == '0' && (pcszValue[1] == 'x' || pcszValue[1] == 'X'))
    {
        if (pu16)
            *pu16 = 0;
        if (ppszNext)
            *ppszNext = (/* UNCONST */ char *)pcszValue + 1; /* to 'x' */
        return VWRN_TRAILING_CHARS;
    }

    /* parser accepts leading zeroes "000000f" */
    if (pszNext - pcszValue > 4)
        return VERR_PARSE_ERROR;

    if (ppszNext)
        *ppszNext = pszNext;
    return rc;
}


/*
 * This function deals only with the hex-group IPv6 address syntax
 * proper (with possible embedded IPv4).
 */
static int rtNetStrToIPv6AddrBase(const char *pcszAddr, PRTNETADDRIPV6 pAddrResult,
                                  char **ppszNext)
{
    RTNETADDRIPV6 ipv6;
    RTNETADDRIPV4 ipv4;
    const char *pcszPos;
    char *pszNext;
    int iGroup;
    uint16_t u16;
    int rc;

    RT_ZERO(ipv6);

    pcszPos = pcszAddr;

    if (pcszPos[0] == ':') /* compressed zero run at the beginning? */
    {
        if (pcszPos[1] != ':')
            return VERR_PARSE_ERROR;

        pcszPos += 2;           /* skip over "::" */
        pszNext = (/* UNCONST */ char *)pcszPos;
        iGroup = 1;
    }
    else
    {
        /*
         * Scan forward until we either get complete address or find
         * "::" compressed zero run.
         */
        pszNext = NULL; /* (MSC incorrectly thinks it may be used unitialized) */
        for (iGroup = 0; iGroup < 8; ++iGroup)
        {
            /* check for embedded IPv4 at the end */
            if (iGroup == 6)
            {
                rc = rtNetStrToIPv4AddrEx(pcszPos, &ipv4, &pszNext);
                if (rc == VINF_SUCCESS)
                {
                    ipv6.au32[3] = ipv4.au32[0];
                    iGroup = 8; /* filled 6 and 7 */
                    break;      /* we are done */
                }
            }

            rc = rtNetStrToHexGroup(pcszPos, &pszNext, &u16);
            if (RT_FAILURE(rc))
                return VERR_PARSE_ERROR;

            ipv6.au16[iGroup] = RT_H2N_U16(u16);

            if (iGroup == 7)
                pcszPos = pszNext;
            else
            {
                /* skip the colon that delimits this group */
                if (*pszNext != ':')
                    return VERR_PARSE_ERROR;
                pcszPos = pszNext + 1;

                /* compressed zero run? */
                if (*pcszPos == ':')
                {
                    ++pcszPos;    /* skip over :: */
                    pszNext += 2; /* skip over :: (in case we are done) */
                    iGroup += 2;  /* current field and the zero in the next */
                    break;
                }
            }
        }
    }

    if (iGroup != 8)
    {
        /*
         * iGroup is the first group that can be filled by the part of
         * the address after "::".
         */
        RTNETADDRIPV6 ipv6Tail;
        const int iMaybeStart = iGroup;
        int j;

        RT_ZERO(ipv6Tail);

        /*
         * We try to accept longest match; we'll shift if necessary.
         * Unlike the first loop, a failure to parse a group doesn't
         * mean invalid address.
         */
        for (; iGroup < 8; ++iGroup)
        {
            /* check for embedded IPv4 at the end */
            if (iGroup <= 6)
            {
                rc = rtNetStrToIPv4AddrEx(pcszPos, &ipv4, &pszNext);
                if (rc == VINF_SUCCESS)
                {
                    ipv6Tail.au16[iGroup]     = ipv4.au16[0];
                    ipv6Tail.au16[iGroup + 1] = ipv4.au16[1];
                    iGroup = iGroup + 2; /* these two are done */
                    break;               /* the rest is trailer */
                }
            }

            rc = rtNetStrToHexGroup(pcszPos, &pszNext, &u16);
            if (RT_FAILURE(rc))
                break;

            ipv6Tail.au16[iGroup] = RT_H2N_U16(u16);

            if (iGroup == 7)
                pcszPos = pszNext;
            else
            {
                if (*pszNext != ':')
                {
                    ++iGroup;   /* this one is done */
                    break;      /* the rest is trailer */
                }

                pcszPos = pszNext + 1;
            }
        }

        for (j = 7, --iGroup; iGroup >= iMaybeStart; --j, --iGroup)
            ipv6.au16[j] = ipv6Tail.au16[iGroup];
    }

    if (pAddrResult != NULL)
        memcpy(pAddrResult, &ipv6, sizeof(ipv6));
    if (ppszNext != NULL)
        *ppszNext = pszNext;
    return VINF_SUCCESS;
}


static int rtNetStrToIPv6AddrEx(const char *pcszAddr, PRTNETADDRIPV6 pAddr,
                                char **ppszZone, char **ppszNext)
{
    char *pszNext, *pszZone;
    int rc;

    rc = rtNetStrToIPv6AddrBase(pcszAddr, pAddr, &pszNext);
    if (RT_FAILURE(rc))
        return rc;

    if (*pszNext != '%')      /* is there a zone id? */
    {
        pszZone = NULL;
    }
    else
    {
        pszZone = pszNext + 1; /* skip '%' zone id delimiter */
        if (*pszZone == '\0')
            return VERR_PARSE_ERROR; /* empty zone id */

        /*
         * XXX: this is speculative as zone id syntax is
         * implementation dependent, so we kinda guess here (accepting
         * unreserved characters from URI syntax).
         */
        for (pszNext = pszZone; *pszNext != '\0'; ++pszNext)
        {
            const char c = *pszNext;
            if (   !('0' <= c && c <= '9')
                   && !('a' <= c && c <= 'z')
                   && !('A' <= c && c <= 'Z')
                   && c != '_'
                   && c != '.'
                   && c != '-'
                   && c != '~')
            {
                break;
            }
        }
    }

    if (ppszZone != NULL)
        *ppszZone = pszZone;
    if (ppszNext != NULL)
        *ppszNext = pszNext;

    if (*pszNext == '\0')       /* all input string consumed */
        return VINF_SUCCESS;
    else
    {
        while (*pszNext == ' ' || *pszNext == '\t')
            ++pszNext;
        if (*pszNext == '\0')
            return VWRN_TRAILING_SPACES;
        else
            return VWRN_TRAILING_CHARS;
    }
}


RTDECL(int) RTNetStrToIPv6AddrEx(const char *pcszAddr, PRTNETADDRIPV6 pAddr,
                                 char **ppszNext)
{
    AssertPtrReturn(pcszAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pAddr, VERR_INVALID_PARAMETER);

    return rtNetStrToIPv6AddrBase(pcszAddr, pAddr, ppszNext);
}
RT_EXPORT_SYMBOL(RTNetStrToIPv6AddrEx);


RTDECL(int) RTNetStrToIPv6Addr(const char *pcszAddr, PRTNETADDRIPV6 pAddr,
                               char **ppszZone)
{
    int rc;

    AssertPtrReturn(pcszAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszZone, VERR_INVALID_PARAMETER);

    pcszAddr = RTStrStripL(pcszAddr);
    rc = rtNetStrToIPv6AddrEx(pcszAddr, pAddr, ppszZone, NULL);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
        return VERR_INVALID_PARAMETER;

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetStrToIPv6Addr);


RTDECL(bool) RTNetIsIPv6AddrStr(const char *pcszAddr)
{
    RTNETADDRIPV6 addrIPv6;
    int rc;

    if (pcszAddr == NULL)
        return false;

    rc = rtNetStrToIPv6AddrEx(pcszAddr, &addrIPv6, NULL, NULL);
    if (rc != VINF_SUCCESS)
        return false;

    return true;
}
RT_EXPORT_SYMBOL(RTNetIsIPv6AddrStr);


RTDECL(bool) RTNetStrIsIPv6AddrAny(const char *pcszAddr)
{
    RTNETADDRIPV6 addrIPv6;
    char *pszZone, *pszNext;
    int rc;

    if (pcszAddr == NULL)
        return false;

    pcszAddr = RTStrStripL(pcszAddr);
    rc = rtNetStrToIPv6AddrEx(pcszAddr, &addrIPv6, &pszZone, &pszNext);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
        return false;

    if (pszZone != NULL)
        return false;

    if (addrIPv6.s.Lo != 0 || addrIPv6.s.Hi != 0) /* in6addr_any? */
        return false;

    return true;
}
RT_EXPORT_SYMBOL(RTNetStrIsIPv6AddrAny);


RTDECL(int) RTNetMaskToPrefixIPv6(PCRTNETADDRIPV6 pMask, int *piPrefix)
{
    AssertReturn(pMask != NULL, VERR_INVALID_PARAMETER);

    int iPrefix = 0;
    unsigned int i;

    for (i = 0; i < RT_ELEMENTS(pMask->au8); ++i)
    {
        int iBits;
        switch (pMask->au8[i])
        {
            case 0x00: iBits = 0; break;
            case 0x80: iBits = 1; break;
            case 0xc0: iBits = 2; break;
            case 0xe0: iBits = 3; break;
            case 0xf0: iBits = 4; break;
            case 0xf8: iBits = 5; break;
            case 0xfc: iBits = 6; break;
            case 0xfe: iBits = 7; break;
            case 0xff: iBits = 8; break;
            default:                /* non-contiguous mask */
                return VERR_INVALID_PARAMETER;
        }

        iPrefix += iBits;
        if (iBits != 8)
            break;
    }

    for (++i; i < RT_ELEMENTS(pMask->au8); ++i)
        if (pMask->au8[i] != 0)
            return VERR_INVALID_PARAMETER;

    if (piPrefix != NULL)
        *piPrefix = iPrefix;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetMaskToPrefixIPv6);


RTDECL(int) RTNetPrefixToMaskIPv6(int iPrefix, PRTNETADDRIPV6 pMask)
{
    AssertReturn(pMask != NULL, VERR_INVALID_PARAMETER);

    if (RT_UNLIKELY(iPrefix < 0 || 128 < iPrefix))
        return VERR_INVALID_PARAMETER;

    for (unsigned int i = 0; i < RT_ELEMENTS(pMask->au32); ++i)
    {
        if (iPrefix == 0)
        {
            pMask->au32[i] = 0;
        }
        else if (iPrefix >= 32)
        {
            pMask->au32[i] = UINT32_C(0xffffffff);
            iPrefix -= 32;
        }
        else
        {
            pMask->au32[i] = RT_H2N_U32(UINT32_C(0xffffffff) << (32 - iPrefix));
            iPrefix = 0;
        }
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetPrefixToMaskIPv6);


RTDECL(int) RTNetStrToIPv6Cidr(const char *pcszAddr, PRTNETADDRIPV6 pAddr, int *piPrefix)
{
    RTNETADDRIPV6 Addr;
    uint8_t u8Prefix;
    char *pszZone, *pszNext;
    int rc;

    AssertPtrReturn(pcszAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pAddr, VERR_INVALID_PARAMETER);
    AssertPtrReturn(piPrefix, VERR_INVALID_PARAMETER);

    pcszAddr = RTStrStripL(pcszAddr);
    rc = rtNetStrToIPv6AddrEx(pcszAddr, &Addr, &pszZone, &pszNext);
    if (RT_FAILURE(rc))
        return rc;

    RT_NOREF(pszZone);

    /*
     * If the prefix is missing, treat is as exact (/128) address
     * specification.
     */
    if (*pszNext == '\0' || rc == VWRN_TRAILING_SPACES)
    {
        *pAddr = Addr;
        *piPrefix = 128;
        return VINF_SUCCESS;
    }

    if (*pszNext != '/')
        return VERR_INVALID_PARAMETER;

    ++pszNext;
    rc = RTStrToUInt8Ex(pszNext, &pszNext, 10, &u8Prefix);
    if (rc != VINF_SUCCESS && rc != VWRN_TRAILING_SPACES)
        return VERR_INVALID_PARAMETER;

    if (u8Prefix > 128)
        return VERR_INVALID_PARAMETER;

    *pAddr = Addr;
    *piPrefix = u8Prefix;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTNetStrToIPv6Cidr);
