/* $Id: netaddrstr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Network Address String Handling.
 *
 * @remarks Don't add new functionality to this file, it goes into netaddrstr2.cpp
 *          or some other suitable file (legal reasons + code not up to oracle
 *          quality standards and requires rewrite from scratch).
 */

/*
 * Contributed by Oliver Loch.
 *
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include "internal/string.h"


/** @page pg_rtnetipv6_addr IPv6 Address Format
 *
 * IPv6 Addresses, their representation in text and other problems.
 *
 * The following is based on:
 *
 * - http://tools.ietf.org/html/rfc4291
 * - http://tools.ietf.org/html/rfc5952
 * - http://tools.ietf.org/html/rfc6052
 *
 *
 * Before you start using those functions, you should have an idea of
 * what you're dealing with, before you come and blame the functions...
 *
 * First of all, the address itself:
 *
 * An address is written like this: (READ THIS FINE MANUAL!)
 *
 * - 2001:db8:abc:def::1
 *
 * The characters between two colons are called a "hextet".
 * Each hextet consists of four characters and each IPv6 address
 * consists of a maximum of eight hextets. So a full blown address
 * would look like this:
 *
 * - 1111:2222:3333:4444:5555:6666:7777:8888
 *
 * The allowed characters are "0123456789abcdef". They have to be
 * lower case. Upper case is not allowed.
 *
 * *** Gaps and adress shortening
 *
 * If an address contains hextets that contain only "0"s, they
 * can be shortened, like this:
 *
 * - 1111:2222:0000:0000:0000:0000:7777:8888 -> 1111:2222::7777:8888
 *
 * The double colon represents the hextets that have been shortened "::".
 * The "::" will be called "gap" from now on.
 *
 * When shortening an address, there are some special rules that need to be applied:
 *
 * - Shorten always the longest group of hextets.
 *
 *   Let's say, you have this address: 2001:db8:0:0:0:1:0:0 then it has to be
 *   shortened to "2001:db8::1:0:0". Shortening to "2001:db8:0:0:0:1::" would
 *   return an error.
 *
 * - Two or more gaps the same size.
 *
 *   Let's say you have this address: 2001:db8:0:0:1:0:0:1. As you can see, there
 *   are two gaps, both the size of two hextets. If you shorten the last two hextets,
 *   you end up in pain, as the RFC forbids this, so the correct address is:
 *   "2001:db8::1:0:0:1"
 *
 * It's important to note that an address can only be shortened ONE TIME!
 * This is invalid: "2001:db8::1::1"
 *
 * *** The scope.
 *
 * Each address has a so called "scope" it is added to the end of the address,
 * separated by a percent sign "%". If there is no scope at the end, it defaults
 * to "0".
 *
 * So "2001:db8::1" is the same as "2001:db8::1%0".
 *
 * As in IPv6 all network interfaces can/should have the same address, the scope
 * gives you the ability to choose on which interface the system should listen.
 *
 * AFAIK, the scope can be used with unicast as well as link local addresses, but
 * it is mandatory with link local addresses (starting with fe80::).
 *
 * On Linux the default scope is the interface's name. On Windows it's just the index
 * of the interface. Run "route print -6" in the shell, to see the interface's index
 * on Winodows.
 *
 * All functions can deal with the scope, and DO NOT warn if you put garbage there.
 *
 * *** Port added to the IPv6 address
 *
 * There is only one way to add a port to an IPv6 address is to embed it in brackets:
 *
 * [2001:db8::1]:12345
 *
 * This gives you the address "2001:db8::1" and the port "12345".
 *
 * What also works, but is not recommended by rfc is to separate the port
 * by a dot:
 *
 * 2001:db8::1.12345
 *
 * It even works with embedded IPv4 addresses.
 *
 * *** Special addresses and how they are written
 *
 * The following are notations to represent "special addresses".
 *
 * "::" IN6ADDR_ANY
 * ":::123" IN6ADDR_ANY with port "123"
 * "[::]:123" IN6ADDR_ANY with port "123"
 * "[:::123]" -> NO. Not allowed and makes no sense
 * "::1" -> address of the loopback device (127.0.0.1 in v4)
 *
 * On systems with dual sockets, one can use so called embedded IPv4 addresses:
 *
 * "::ffff:192.168.1.1" results in the IPv6 address "::ffff:c0a8:0101" as two octets
 * of the IPv4 address will be converted to one hextet in the IPv6 address.
 *
 * The prefix of such addresses MUST BE "::ffff:", 10 bytes as zero and two bytes as 255.
 *
 * The so called IPv4-compatible IPv6 addresses are deprecated and no longer in use.
 *
 * *** Valid addresses and string
 *
 * If you use any of the IPv6 address functions, keep in mind, that those addresses
 * are all returning "valid" even if the underlying system (e.g. VNC) doesn't like
 * such strings.
 *
 * [2001:db8::1]
 * [2001:db8::1]:12345
 *
 * and so on. So to make sure you only pass the underlying software a pure IPv6 address
 * without any garbage, you should use the "outAddress" parameters to get a RFC compliant
 * address returned.
 *
 * So after reading the above, you'll start using the functions and see a bool called
 * "followRfc" which is true by default. This is what this bool does:
 *
 * The following addresses all represent the exact same address:
 *
 * 1 - 2001:db8::1
 * 2 - 2001:db8:0::1
 * 3 - 2001:0db8:0000:0000:0000:0000:0000:0001
 * 4 - 2001:DB8::1
 * 5 - [2001:db8::1]
 * 6 - [2001:db8:0::1]
 *
 * According to RFC 5952, number two, three, four and six are invalid.
 *
 * #2 - because there is a single hextet that hasn't been shortened
 *
 * #3 - because there has nothing been shortened (hextets 3 to 7) and
 *      there are leading zeros in at least one hextet ("0db8")
 *
 * #4 - all characters in an IPv6 address have to be lower case
 *
 * #6 - same as two but included in brackets
 *
 * If you follow RFC, the above addresses are not converted and an
 * error is returned. If you turn RFC off, you will get the expected
 * representation of the address.
 *
 * It's a nice way to convert "weird" addresses to rfc compliant addresses
 *
 */


/**
 * Parses any string and tests if it is an IPv6 Address
 *
 * This function should NOT be used directly. If you do, note
 * that no security checks are done at the moment. This can change.
 *
 * @returns iprt sstatus code, yeah, right... This function most certainly DOES
 *          NOT RETURN ANY IPRT STATUS CODES.  It's also a unreadable mess.
 * @param pszAddress       The strin that holds the IPv6 address
 * @param addressLength    The length of pszAddress
 * @param pszAddressOut    Returns a plain, full blown IPv6 address
 *                         as a char array
 * @param addressOutSize   The size of pszAddressOut (length)
 * @param pPortOut         32 bit unsigned integer, holding the port
 *                         If pszAddress doesn't contain a port, it's 0
 * @param pszScopeOut      Returns the scope of the address, if none it's 0
 * @param scopeOutSize     sizeof(pszScopeOut)
 * @param pBrackets        returns true if the address was enclosed in brackets
 * @param pEmbeddedV4      returns true if the address is an embedded IPv4 address
 * @param followRfc        if set to true, the function follows RFC (default)
 */
static int rtStrParseAddrStr6(const char *pszAddress, size_t addressLength, char *pszAddressOut, size_t addressOutSize, uint32_t *pPortOut, char *pszIfIdOut, size_t ifIdOutSize, bool *pBrackets, bool *pEmbeddedV4, bool followRfc)
{
    /************************\
     *  Pointer Hell Ahead  *
    \************************/

    const char szIpV6AddressChars[] = "ABCDEF01234567890abcdef.:[]%"; // order IMPORTANT
    const char szIpV4AddressChars[] = "01234567890.:[]"; // order IMPORTANT
    const char szLinkLocalPrefix[] = "FfEe8800"; //
    const char *pszIpV6AddressChars = NULL, *pszIpV4AddressChars = NULL, *pszLinkLocalPrefix = NULL;

    char *pszSourceAddress = NULL, *pszSourceAddressStart = NULL;
    char *pszResultAddress = NULL, *pszResultAddressStart = NULL;
    char *pszResultAddress4 = NULL, *pszResultAddress4Start = NULL;
    char *pszResultPort = NULL, *pszResultPortStart = NULL;
    char *pszInternalAddress = NULL, *pszInternalAddressStart = NULL;
    char *pszInternalPort = NULL, *pszInternalPortStart = NULL;

    char *pStart = NULL, *pNow = NULL, *pNext = NULL, *pNowChar = NULL, *pIfId = NULL, *pIfIdEnd = NULL;
    char *pNowDigit = NULL, *pFrom = NULL, *pTo = NULL, *pLast = NULL;
    char *pGap = NULL, *pMisc = NULL, *pDotStart = NULL, *pFieldStart = NULL, *pFieldEnd = NULL;
    char *pFieldStartLongest = NULL, *pBracketOpen = NULL, *pBracketClose = NULL;
    char *pszRc = NULL;

    bool isLinkLocal = false;
    char szDummy[4];

    uint8_t *pByte = NULL;
    uint32_t byteOut = 0;
    uint16_t returnValue = 0;
    uint32_t colons = 0;
    uint32_t colonsOverAll = 0;
    uint32_t fieldLength = 0;
    uint32_t dots = 0;
    size_t gapSize = 0;
    uint32_t intPortOut = 0;

    pszIpV4AddressChars = &szIpV4AddressChars[0];
    pszIpV6AddressChars = &szIpV6AddressChars[6];
    pszLinkLocalPrefix = &szLinkLocalPrefix[6];

    if (!followRfc)
        pszIpV6AddressChars = &szIpV6AddressChars[0];

    if (addressLength<2)
        returnValue = 711;

    pszResultAddressStart = (char *)RTMemTmpAlloc(34);
    pszInternalAddressStart = (char *)RTMemTmpAlloc(34);
    pszInternalPortStart = (char * )RTMemTmpAlloc(10);

    if (! (pszResultAddressStart && pszInternalAddressStart && pszInternalPortStart))
    {
        if (pszResultAddressStart)
            RTMemTmpFree(pszResultAddressStart);

        if (pszInternalAddressStart)
            RTMemTmpFree(pszInternalAddressStart);

        if (pszInternalPortStart)
            RTMemTmpFree(pszInternalPortStart);

        return -701;
    }

    memset(szDummy, '\0', 4);

    pszResultAddress = pszResultAddressStart;
    memset(pszResultAddressStart, '\0', 34);

    pszInternalAddress = pszInternalAddressStart;
    memset(pszInternalAddressStart, '\0' , 34);

    pszInternalPort = pszInternalPortStart;
    memset(pszInternalPortStart, '\0', 10);

    pszSourceAddress = pszSourceAddressStart = (char *)pszAddress;

    pFrom = pTo = pStart = pLast = pszSourceAddressStart;

    while (*pszSourceAddress != '\0' && !returnValue)
    {
        pNow = NULL;
        pNext = NULL;
        pNowChar = NULL;
        pNowDigit = NULL;

        pNow = pszSourceAddress;
        pNext = pszSourceAddress + 1;

        if (!pFrom)
            pFrom = pTo = pNow;

        pNowChar = (char *)memchr(pszIpV6AddressChars, *pNow, strlen(pszIpV6AddressChars));
        pNowDigit = (char *)memchr(pszIpV6AddressChars, *pNow, strlen(pszIpV6AddressChars) - 5);

        if (pszResultPort)
        {
            if (pLast && (pszResultPort == pszSourceAddressStart))
            {
                if (*pLast == '\0')
                    returnValue = 721;

                pszResultPortStart = (char *)RTMemTmpAlloc(10);

                if (!pszResultPortStart)
                    returnValue = 702;

                memset(pszResultPortStart, '\0', 10);
                pszResultPort = pszResultPortStart;
                pszSourceAddress = pLast;
                pMisc = pLast;
                pLast = NULL;
                continue;
            }

            pNowDigit = NULL;
            pNowDigit = (char *)memchr(pszIpV4AddressChars, *pNow, strlen(pszIpV4AddressChars) - 4);

            if (strlen(pszResultPortStart) == 5)
                returnValue = 11;

            if (*pNow == '0' && pszResultPort == pszResultPortStart && *pNext != '\0' && (pNow - pMisc) < 5 )
            {
                pszSourceAddress++;
                continue;
            }

            if (pNowDigit)
            {
                *pszResultPort = *pNowDigit;
                pszResultPort++;
                pszSourceAddress++;
                continue;
            }
            else
                returnValue = 12;
        }

        if (pszResultAddress4)
        {
            if (pszResultAddress4 == pszSourceAddressStart && pLast)
            {
                dots = 0;
                pszResultAddress4 = NULL;
                pszResultAddress4Start = NULL;
                pszResultAddress4Start = (char *)RTMemTmpAlloc(20);

                if (!pszResultAddress4Start)
                {
                    returnValue = 401;
                    break;
                }

                memset(pszResultAddress4Start, '\0', 20);
                pszResultAddress4 = pszResultAddress4Start;
                pszSourceAddress = pLast;
                pFrom = pLast;
                pTo = pLast;
                pLast = NULL;
                continue;
            }

            pTo = pNow;
            pNowDigit = NULL;
            pNowDigit = (char *)memchr(pszIpV4AddressChars, *pNow, strlen(pszIpV4AddressChars) - 4);

            if (!pNowDigit && *pNow != '.' && *pNow != ']' && *pNow != ':' && *pNow != '%')
                returnValue = 412;

            if ((pNow - pFrom) > 3)
            {
                returnValue = 402;
                break;
            }

            if (pNowDigit && *pNext != '\0')
            {
                pszSourceAddress++;
                continue;
            }

            if (!pNowDigit  && !pBracketOpen && (*pNext == '.' || *pNext == ']' || *pNext == ':'))
                returnValue = 411;

            memset(pszResultAddress4, '0', 3);
            pMisc = pszResultAddress4 + 2;
            pszResultAddress4 = pszResultAddress4 + 3;

            if (*pNow != '.' && !pNowDigit && strlen(pszResultAddress4Start) < 9)
                returnValue = 403;

            if ((pTo - pFrom) > 0)
                pTo--;

            dots++;

            while (pTo >= pFrom)
            {
                *pMisc = *pTo;
                pMisc--;
                pTo--;
            }

            if (dots == 4 && *pNow == '.')
            {
                if (!pBracketOpen)
                {
                    pszResultPort = pszSourceAddressStart;
                    pLast = pNext;
                }
                else
                {
                    returnValue = 409;
                }
            }

            dots = 0;

            pFrom = pNext;
            pTo = pNext;

            if (strlen(pszResultAddress4Start) > 11)
                pszResultAddress4 = NULL;

            if ((*pNow == ':' || *pNow == '.') && strlen(pszResultAddress4Start) == 12)
            {
                pLast = pNext;
                pszResultPort = pszSourceAddressStart;
            }

            if (*pNow == '%')
            {
                pIfId =  pNow;
                pLast = pNow;
                continue;
            }
            pszSourceAddress = pNext;

            if (*pNow != ']')
                continue;

            pFrom = pNow;
            pTo = pNow;
        }

        if (pIfId && (!pIfIdEnd))
        {
            if (*pIfId == '%' && pIfId == pLast && *pNext != '\0')
            {
                pFrom = pNext;
                pIfId = pNext;
                pLast = NULL;

                pszSourceAddress++;
                continue;
            }

            if (*pNow == '%' && pIfId <= pNow)
            {
                returnValue = 442;
                break;
            }

            if (*pNow != ']' && *pNext != '\0')
            {
                pTo = pNow;
                pszSourceAddress++;
                continue;
            }

            if (*pNow == ']')
            {
                pIfIdEnd = pNow - 1;
                pFrom = pNow;
                pTo = pNow;
                continue;
            }
            else
            {
                pIfIdEnd = pNow;
                pFrom = NULL;
                pTo = NULL;
                pszSourceAddress++;
                continue;
            }
        }

        if (!pNowChar)
        {
            returnValue = 254;

            if (followRfc)
            {
                pMisc = (char *)memchr(&szIpV6AddressChars[0], *pNow, strlen(&szIpV6AddressChars[0]));

                if (pMisc)
                    returnValue = 253;
            }
        }

        if (strlen(pszResultAddressStart) > 32 && !pszResultAddress4Start)
            returnValue = 255;

        if (pNowDigit && *pNext != '\0' && colons == 0)
        {
            pTo = pNow;
            pszSourceAddress++;
            continue;
        }

        if (*pNow == ':' && *pNext != '\0')
        {
            colonsOverAll++;
            colons++;
            pszSourceAddress++;
            continue;
        }

        if (*pNow == ':' )
        {
            colons++;
            colonsOverAll++;
        }

        if (*pNow == '.')
        {
            pMisc = pNow;

            while (*pMisc != '\0' && *pMisc != ']')
            {
                if (*pMisc == '.')
                    dots++;

                pMisc++;
            }
        }

        if (*pNow == ']')
        {
            if (pBracketClose)
                returnValue = 77;

            if (!pBracketOpen)
                returnValue = 22;

            if (*pNext == ':' || *pNext == '.')
            {
                pszResultPort = pszSourceAddressStart;
                pLast = pNext + 1;
            }

            if (pFrom == pNow)
                pFrom = NULL;

            pBracketClose = pNow;
        }

        if (*pNow == '[')
        {
            if (pBracketOpen)
                returnValue = 23;

            if (pStart != pNow)
                returnValue = 24;

            pBracketOpen = pNow;
            pStart++;
            pFrom++;
            pszSourceAddress++;
            continue;
        }

        if (*pNow == '%')
        {
            if (pIfId)
                returnValue = 441;

            pLast = pNext;
            pIfId = pNext;
        }

        if (colons > 0)
        {
            if (colons == 1)
            {
                if (pStart + 1 == pNow )
                    returnValue = 31;

                if (*pNext == '\0' && !pNowDigit)
                    returnValue = 32;

                pLast = pNow;
            }

            if (colons == 2)
            {
                if (pGap)
                    returnValue = 33;

                pGap = pszResultAddress + 4;

                if (pStart + 1 == pNow || pStart + 2 == pNow)
                {
                    pGap = pszResultAddressStart;
                    pFrom = pNow;
                }

                if (*pNext == '\0' && !pNowDigit)
                    pszSourceAddress++;

                if (*pNext != ':' && *pNext != '.')
                    pLast = pNow;
            }

            if (colons == 3)
            {
                pFrom = pLast;
                pLast = pNow;

                if (*pNext == '\0' && !pNowDigit)
                    returnValue = 34;

                if (pBracketOpen)
                    returnValue = 35;

                if (pGap && followRfc)
                    returnValue = 36;

                if (!pGap)
                    pGap = pszResultAddress + 4;

                if (pStart + 3 == pNow)
                {
                    pszResultPort = pszSourceAddressStart;
                    pGap = pszResultAddress;
                    pFrom = NULL;
                }

                if (pNowDigit)
                {
                    pszResultPort = pszSourceAddressStart;
                }
            }
        }
        if (*pNext == '\0' && colons == 0 && !pIfIdEnd)
        {
            pFrom = pLast;

            if (pNowDigit)
                pTo = pNow;

            pLast = NULL;
        }

        if (dots > 0)
        {
            if (dots == 1)
            {
                pszResultPort = pszSourceAddressStart;
                pLast = pNext;
            }

            if (dots == 4 && pBracketOpen)
                returnValue = 601;

            if (dots == 3 || dots == 4)
            {
                pszResultAddress4 = pszSourceAddressStart;
                pLast = pFrom;
                pFrom = NULL;
            }

            if (dots > 4)
                returnValue = 603;

            dots = 0;
        }

        if (pFrom && pTo)
        {
            if (pTo - pFrom > 3)
            {
                returnValue = 51;
                break;
            }

            if (followRfc)
            {
                if ((pTo - pFrom > 0) && *pFrom == '0')
                    returnValue = 101;

                if ((pTo - pFrom) == 0 && *pFrom == '0' && colons == 2)
                    returnValue = 102;

                if ((pTo - pFrom) == 0 && *pFrom == '0' && pszResultAddress == pGap)
                    returnValue = 103;

                if ((pTo - pFrom) == 0 && *pFrom == '0')
                {
                    if (!pFieldStart)
                    {
                        pFieldStart = pszResultAddress;
                        pFieldEnd = pszResultAddress + 4;
                    }
                    else
                    {
                        pFieldEnd = pFieldEnd + 4;
                    }
                }
                else
                {
                    if ((size_t)(pFieldEnd - pFieldStart) > fieldLength)
                    {
                        fieldLength = pFieldEnd - pFieldStart;
                        pFieldStartLongest = pFieldStart;
                    }

                    pFieldStart = NULL;
                    pFieldEnd = NULL;
                }
            }
            if (!(pGap == pszResultAddressStart && (size_t)(pNow - pStart) == colons))
            {
                memset(pszResultAddress, '0', 4);
                pMisc = pszResultAddress + 3;
                pszResultAddress = pszResultAddress + 4;

                if (pFrom == pStart && (pTo - pFrom) == 3)
                {
                    isLinkLocal = true;

                    while (pTo >= pFrom)
                    {
                        *pMisc = *pTo;

                        if (*pTo != *pszLinkLocalPrefix && *pTo != *(pszLinkLocalPrefix + 1))
                            isLinkLocal = false;

                        pTo--;
                        pMisc--;
                        pszLinkLocalPrefix = pszLinkLocalPrefix - 2;
                    }
                }
                else
                {
                    while (pTo >= pFrom)
                    {
                        *pMisc = *pTo;
                        pMisc--;
                        pTo--;
                    }
                }
            }

            pFrom = pNow;
            pTo = pNow;

        }
        if (*pNext == '\0' && colons == 0)
            pszSourceAddress++;

        if (*pNext == '\0' && !pBracketClose && !pszResultPort)
            pTo = pNext;

        colons = 0;
    } // end of loop

    if (!returnValue && colonsOverAll < 2)
        returnValue = 252;

    if (!returnValue && (pBracketOpen && !pBracketClose))
        returnValue = 25;

    if (!returnValue && pGap)
    {
        gapSize = 32 - strlen(pszResultAddressStart);

        if (followRfc)
        {
            if (gapSize < 5)
                returnValue = 104;

            if (fieldLength > gapSize)
                returnValue = 105;

            if (fieldLength == gapSize && pFieldStartLongest < pGap)
                returnValue = 106;
        }

        pszResultAddress = pszResultAddressStart;
        pszInternalAddress = pszInternalAddressStart;

        if (!returnValue && pszResultAddress4Start)
        {
            if (strlen(pszResultAddressStart) > 4)
                returnValue = 405;

            pszResultAddress = pszResultAddressStart;

            if (pGap != pszResultAddressStart)
                returnValue = 407;

            memset(pszInternalAddressStart, '0', 20);
            pszInternalAddress = pszInternalAddressStart + 20;

            for (int i = 0; i < 4; i++)
            {
                if (*pszResultAddress != 'f' && *pszResultAddress != 'F')
                {
                    returnValue = 406;
                    break;
                }

                *pszInternalAddress = *pszResultAddress;
                pszResultAddress++;
                pszInternalAddress++;
            }
            pszResultAddress4 = pszResultAddress4Start;

            for (int i = 0; i<4; i++)
            {
                memcpy(szDummy, pszResultAddress4, 3);

                int rc = RTStrToUInt32Ex((const char *)&szDummy[0], NULL, 16, &byteOut);

                if (rc == 0 && byteOut < 256)
                {
                    RTStrPrintf(szDummy, 3, "%02x", byteOut);
                    memcpy(pszInternalAddress, szDummy, 2);
                    pszInternalAddress = pszInternalAddress + 2;
                    pszResultAddress4 = pszResultAddress4 + 3;
                    memset(szDummy, '\0', 4);
                }
                else
                {
                    returnValue = 499;
                }
            }
        }
        else
        {
            while (!returnValue && pszResultAddress != pGap)
            {
                *pszInternalAddress = *pszResultAddress;
                pszResultAddress++;
                pszInternalAddress++;
            }

            memset(pszInternalAddress, '0', gapSize);
            pszInternalAddress = pszInternalAddress + gapSize;

            while (!returnValue && *pszResultAddress != '\0')
            {
                *pszInternalAddress = *pszResultAddress;
                pszResultAddress++;
                pszInternalAddress++;
            }
        }
    }
    else
    {
        if (!returnValue)
        {
            if (strlen(pszResultAddressStart) != 32)
                returnValue = 111;

            if (followRfc)
            {
                if (fieldLength > 4)
                    returnValue = 112;
            }

            memcpy(pszInternalAddressStart, pszResultAddressStart, strlen(pszResultAddressStart));
        }
    }

    if (pszResultPortStart)
    {
        if (strlen(pszResultPortStart) > 0 && strlen(pszResultPortStart) < 6)
        {
            memcpy(pszInternalPortStart, pszResultPortStart, strlen(pszResultPortStart));

            intPortOut = 0;
            int rc = RTStrToUInt32Ex(pszInternalPortStart, NULL, 10, &intPortOut);

            if (rc == 0)
            {
                if (!(intPortOut > 0 && intPortOut < 65536))
                    intPortOut = 0;
            }
            else
            {
                returnValue = 888;
            }
        }
        else
        {
            returnValue = 889;
        }
    }

    /*
       full blown address 32 bytes, no colons -> pszInternalAddressStart
       port as string -> pszResultPortStart
       port as binary integer -> intPortOut
       interface id in pIfId and pIfIdEnd

       Now fill the out parameters.

     */

    if (!returnValue && pszAddressOut)
    {
        if (strlen(pszInternalAddressStart) < addressOutSize)
        {
            pszRc = NULL;
            pszRc = (char *)memset(pszAddressOut, '\0', addressOutSize);

            if (!pszRc)
                returnValue = 910;

            pszRc = NULL;

            pszRc = (char *)memcpy(pszAddressOut, pszInternalAddressStart, strlen(pszInternalAddressStart));

            if (!pszRc)
                returnValue = 911;
        }
        else
        {
            returnValue = 912;
        }
    }

    if (!returnValue && pPortOut)
    {
        *pPortOut = intPortOut;
    }

    if (!returnValue && pszIfIdOut)
    {
        if (pIfIdEnd && pIfId)
        {
            if ((size_t)(pIfIdEnd - pIfId) + 1 < ifIdOutSize)
            {
                pszRc = NULL;
                pszRc = (char *)memset(pszIfIdOut, '\0', ifIdOutSize);

                if (!pszRc)
                    returnValue = 913;

                pszRc = NULL;
                pszRc = (char *)memcpy(pszIfIdOut, pIfId, (pIfIdEnd - pIfId) + 1);

                if (!pszRc)
                    returnValue = 914;
            }
            else
            {
                returnValue = 915;
            }
        }
        else
        {
            pszRc = NULL;
            pszRc = (char *)memset(pszIfIdOut, '\0', ifIdOutSize);

            if (!pszRc)
                returnValue = 916;
        }
        // temporary hack
        if (isLinkLocal && (strlen(pszIfIdOut) < 1))
        {
            memset(pszIfIdOut, '\0', ifIdOutSize);
            *pszIfIdOut = '%';
            pszIfIdOut++;
            *pszIfIdOut = '0';
            pszIfIdOut++;
        }
    }

    if (pBracketOpen && pBracketClose && pBrackets)
        *pBrackets = true;

    if (pEmbeddedV4 && pszResultAddress4Start)
        *pEmbeddedV4 = true;

    if (pszResultAddressStart)
        RTMemTmpFree(pszResultAddressStart);

    if (pszResultPortStart)
        RTMemTmpFree(pszResultPortStart);

    if (pszResultAddress4Start)
        RTMemTmpFree(pszResultAddress4Start);

    if (pszInternalAddressStart)
        RTMemTmpFree(pszInternalAddressStart);

    if (pszInternalPortStart)
        RTMemTmpFree(pszInternalPortStart);

    return (uint32_t)(returnValue - (returnValue * 2)); // make it negative...
}

/**
 * Takes a string and returns a RFC compliant string of the address
 * This function SHOULD NOT be used directly. It expects a 33 byte
 * char array with a full blown IPv6 address without separators.
 *
 * @returns iprt status code.
 * @param psz              The string to convert
 * @param pszAddrOut       The char[] that will hold the result
 * @param addOutSize       The size of the char[] from above.
 * @param pszPortOut       char[] for the text representation of the port
 * @param portOutSize      sizeof(pszPortOut);
 */
DECLHIDDEN(int) rtStrToIpAddr6Str(const char *psz, char *pszAddrOut, size_t addrOutSize, char *pszPortOut, size_t portOutSize, bool followRfc)
{
    char *pStart = NULL;
    char *pGapStart = NULL;
    char *pGapEnd = NULL;
    char *pGapTStart = NULL;
    char *pGapTEnd = NULL;
    char *pCurrent = NULL;
    char *pOut = NULL;

    if (!psz || !pszAddrOut)
        return VERR_NOT_SUPPORTED;

    if (addrOutSize < 40)
        return VERR_NOT_SUPPORTED;

    pStart = (char *)psz;
    pCurrent = (char *)psz;
    pGapStart = (char *)psz;
    pGapEnd =  (char *)psz;

    while (*pCurrent != '\0')
    {
        if (*pCurrent != '0')
            pGapTStart = NULL;

        if ((pCurrent - pStart) % 4 == 0) // ok, start of a hextet
        {
            if (*pCurrent == '0' &&  !pGapTStart)
                pGapTStart = pCurrent;
        }

        if ((pCurrent - pStart) % 4 == 3)
        {
            if (*pCurrent == '0' && pGapTStart)
                pGapTEnd = pCurrent;

            if (pGapTStart && pGapTEnd)
            {
                pGapTEnd = pCurrent;

                if ((pGapTEnd - pGapTStart) > (pGapEnd - pGapStart))
                {
                    pGapEnd = pGapTEnd;
                    pGapStart = pGapTStart;
                }
            }
        }

        pCurrent++;
    }

    pCurrent = (char *)psz;
    pStart = (char *)psz;
    pOut = (char *)pszAddrOut;

    while (*pCurrent != '\0')
    {
        if (*pCurrent != '0')
            pGapTStart = NULL;

        if (!pGapTStart)
        {
            *pOut = *pCurrent;
            pOut++;
        }

        if ((pCurrent - pStart) % 4 == 3)
        {
            if (pGapTStart && *pCurrent == '0')
            {
                *pOut = *pCurrent;
                pOut++;
            }

            if (*(pCurrent + 1) != '\0')
            {
                *pOut = ':';
                pOut++;
            }

            pGapTStart = pCurrent + 1;
        }

        if ((pCurrent + 1) == pGapStart && (pGapEnd - pGapStart) > 3)
        {
            *pOut = ':';
            pOut++;
            pCurrent = pGapEnd;
        }

        pCurrent++;
    }

    return VINF_SUCCESS;
}


/**
 * Tests if the given string is a valid IPv6 address.
 *
 * @returns 0 if valid, some random number if not.  THIS IS NOT AN IPRT STATUS!
 * @param psz                  The string to test
 * @param pszResultAddress     plain address, optional read "valid addresses
 *                             and strings" above.
 * @param resultAddressSize    size of pszResultAddress
 * @param addressOnly          return only the plain address (no scope)
 *                             Ignored, and will always return the if id
 */
static int rtNetIpv6CheckAddrStr(const char *psz, char *pszResultAddress, size_t resultAddressSize, bool addressOnly, bool followRfc)
{
    int rc;
    int rc2;
    int returnValue = VERR_NOT_SUPPORTED; /* gcc want's this initialized, I understand its confusion. */

    char *p = NULL, *pl = NULL;

    size_t memAllocMaxSize = RT_MAX(strlen(psz), resultAddressSize) + 40;

    char *pszAddressOutLocal = (char *)RTMemTmpAlloc(memAllocMaxSize);
    char *pszIfIdOutLocal = (char *)RTMemTmpAlloc(memAllocMaxSize);
    char *pszAddressRfcOutLocal = (char *)RTMemTmpAlloc(memAllocMaxSize);

    if (!pszAddressOutLocal || !pszIfIdOutLocal || !pszAddressRfcOutLocal)
        return VERR_NO_TMP_MEMORY;

    memset(pszAddressOutLocal, '\0', memAllocMaxSize);
    memset(pszIfIdOutLocal, '\0', memAllocMaxSize);
    memset(pszAddressRfcOutLocal, '\0', memAllocMaxSize);

    rc = rtStrParseAddrStr6(psz, strlen(psz), pszAddressOutLocal, memAllocMaxSize, NULL, pszIfIdOutLocal, memAllocMaxSize, NULL, NULL, followRfc);

    if (rc == 0)
        returnValue = VINF_SUCCESS;

    if (rc == 0 && pszResultAddress)
    {
        // convert the 32 characters to a valid, shortened ipv6 address

        rc2 = rtStrToIpAddr6Str((const char *)pszAddressOutLocal, pszAddressRfcOutLocal, memAllocMaxSize, NULL, 0, followRfc);

        if (rc2 != 0)
            returnValue = 951;

        // this is a temporary solution
        if (!returnValue && strlen(pszIfIdOutLocal) > 0) // the if identifier is copied over _ALWAYS_ && !addressOnly)
        {
            p = pszAddressRfcOutLocal + strlen(pszAddressRfcOutLocal);

            *p = '%';

            p++;

            pl = (char *)memcpy(p, pszIfIdOutLocal, strlen(pszIfIdOutLocal));

            if (!pl)
                returnValue = VERR_NOT_SUPPORTED;
        }

        pl = NULL;

        pl = (char *)memcpy(pszResultAddress, pszAddressRfcOutLocal, strlen(pszAddressRfcOutLocal));

        if (!pl)
            returnValue = VERR_NOT_SUPPORTED;
    }

    if (rc != 0)
        returnValue = VERR_NOT_SUPPORTED;

    if (pszAddressOutLocal)
        RTMemTmpFree(pszAddressOutLocal);

    if (pszAddressRfcOutLocal)
        RTMemTmpFree(pszAddressRfcOutLocal);

    if (pszIfIdOutLocal)
        RTMemTmpFree(pszIfIdOutLocal);

    return returnValue;

}
