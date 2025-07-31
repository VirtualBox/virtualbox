/* $Id: uri.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Uniform Resource Identifier handling.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/uri.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Internal magic value we use to check if a RTURIPARSED structure has made it thru RTUriParse. */
#define RTURIPARSED_MAGIC   UINT32_C(0x439e0745)


/* General URI format:

    foo://example.com:8042/over/there?name=ferret#nose
    \_/   \______________/\_________/ \_________/ \__/
     |           |             |           |        |
  scheme     authority       path        query   fragment
     |   _____________________|__
    / \ /                        \
    urn:example:animal:ferret:nose
*/


/**
 * The following defines characters which have to be % escaped:
 *  control = 00-1F
 *  space   = ' '
 *  delims  = '<' , '>' , '#' , '%' , '"'
 *  unwise  = '{' , '}' , '|' , '\' , '^' , '[' , ']' , '`'
 *
 * @note ARM defines char as unsigned by default in the AAPCS(64) so the first check would trigger
 *       a compiler warning/error. Apple decided to ignore that and declares char a signed like on
 *       the other platforms.
 */
#if    defined(RT_OS_LINUX) \
    && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
# define URI_EXCLUDED(a) \
  (   ((a) <= 0x20) \
   || ((a) >= 0x5B && (a) <= 0x5E) \
   || ((a) >= 0x7B && (a) <= 0x7D) \
   || (a) == '<' || (a) == '>' || (a) == '#' \
   || (a) == '%' || (a) == '"' || (a) == '`' )
#else
# define URI_EXCLUDED(a) \
  (   ((a) >= 0x0  && (a) <= 0x20) \
   || ((a) >= 0x5B && (a) <= 0x5E) \
   || ((a) >= 0x7B && (a) <= 0x7D) \
   || (a) == '<' || (a) == '>' || (a) == '#' \
   || (a) == '%' || (a) == '"' || (a) == '`' )
#endif

static char *rtUriPercentEncodeN(const char *pszString, size_t cchMax)
{
    if (!pszString)
        return NULL;

    int rc = VINF_SUCCESS;

    size_t const cchStr = strlen(pszString);
    size_t cbLen = RT_MIN(cchStr, cchMax);
    /* The new string can be max 3 times in size of the original string. */
    char *pszNew = RTStrAlloc(cbLen * 3 + 1);
    if (!pszNew)
        return NULL;

    char *pszRes = NULL;
    size_t iIn = 0;
    size_t iOut = 0;
    while (iIn < cbLen)
    {
        if (URI_EXCLUDED(pszString[iIn]))
        {
            char szNum[3] = { 0, 0, 0 };
            RTStrFormatU8(&szNum[0], 3, pszString[iIn++], 16, 2, 2, RTSTR_F_CAPITAL | RTSTR_F_ZEROPAD);
            pszNew[iOut++] = '%';
            pszNew[iOut++] = szNum[0];
            pszNew[iOut++] = szNum[1];
        }
        else
            pszNew[iOut++] = pszString[iIn++];
    }
    if (RT_SUCCESS(rc))
    {
        pszNew[iOut] = '\0';
        if (iOut != iIn)
        {
            /* If the source and target strings have different size, recreate
             * the target string with the correct size. */
            pszRes = RTStrDupN(pszNew, iOut);
            RTStrFree(pszNew);
        }
        else
            pszRes = pszNew;
    }
    else
        RTStrFree(pszNew);

    return pszRes;
}


/**
 * Calculates the encoded string length.
 *
 * @returns Number of chars (excluding the terminator).
 * @param   pszString       The string to encode.
 * @param   cchMax          The maximum string length (e.g. RTSTR_MAX).
 * @param   fEncodeDosSlash Whether to encode DOS slashes or not.
 */
static size_t rtUriCalcEncodedLength(const char *pszString, size_t cchMax, bool fEncodeDosSlash)
{
    size_t cchEncoded = 0;
    if (pszString)
    {
        size_t cchSrcLeft = RTStrNLen(pszString, cchMax);
        while (cchSrcLeft-- > 0)
        {
            char const ch = *pszString++;
            if (!URI_EXCLUDED(ch) || (ch == '\\' && !fEncodeDosSlash))
                cchEncoded += 1;
            else
                cchEncoded += 3;
        }
    }
    return cchEncoded;
}


/**
 * Encodes an URI into a caller allocated buffer.
 *
 * @returns IPRT status code.
 * @param   pszString       The string to encode.
 * @param   cchMax          The maximum string length (e.g. RTSTR_MAX).
 * @param   fEncodeDosSlash Whether to encode DOS slashes or not.
 * @param   pszDst          The destination buffer.
 * @param   cbDst           The size of the destination buffer.
 */
static int rtUriEncodeIntoBuffer(const char *pszString, size_t cchMax, bool fEncodeDosSlash, char *pszDst, size_t cbDst)
{
    AssertReturn(pszString, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);

    /*
     * We do buffer size checking up front and every time we encode a special
     * character.  That's faster than checking for each char.
     */
    size_t cchSrcLeft = RTStrNLen(pszString, cchMax);
    AssertMsgReturn(cbDst > cchSrcLeft, ("cbDst=%zu cchSrcLeft=%zu\n", cbDst, cchSrcLeft), VERR_BUFFER_OVERFLOW);
    cbDst -= cchSrcLeft;

    while (cchSrcLeft-- > 0)
    {
        char const ch = *pszString++;
        if (!URI_EXCLUDED(ch) || (ch == '\\' && !fEncodeDosSlash))
            *pszDst++ = ch;
        else
        {
            AssertReturn(cbDst >= 3, VERR_BUFFER_OVERFLOW); /* 2 extra bytes + zero terminator. */
            cbDst -= 2;

            *pszDst++ = '%';
            ssize_t cchTmp = RTStrFormatU8(pszDst, 3, (unsigned char)ch, 16, 2, 2, RTSTR_F_CAPITAL | RTSTR_F_ZEROPAD);
            Assert(cchTmp == 2); NOREF(cchTmp);
            pszDst += 2;
        }
    }

    *pszDst = '\0';
    return VINF_SUCCESS;
}


static char *rtUriPercentDecodeN(const char *pszString, size_t cchString)
{
    AssertPtrReturn(pszString, NULL);
    AssertReturn(memchr(pszString, '\0', cchString) == NULL, NULL);

    /*
     * The new string can only get smaller, so use the input length as a
     * staring buffer size.
     */
    char *pszDecoded = RTStrAlloc(cchString + 1);
    if (pszDecoded)
    {
        /*
         * Knowing that the pszString itself is valid UTF-8, we only have to
         * validate the escape sequences.
         */
        size_t      cchLeft = cchString;
        char const *pchSrc  = pszString;
        char       *pchDst  = pszDecoded;
        while (cchLeft > 0)
        {
            const char *pchPct = (const char *)memchr(pchSrc, '%', cchLeft);
            if (pchPct)
            {
                size_t cchBefore = pchPct - pchSrc;
                if (cchBefore)
                {
                    memcpy(pchDst, pchSrc, cchBefore);
                    pchDst  += cchBefore;
                    pchSrc  += cchBefore;
                    cchLeft -= cchBefore;
                }

                char chHigh, chLow;
                if (   cchLeft >= 3
                    && RT_C_IS_XDIGIT(chHigh = pchSrc[1])
                    && RT_C_IS_XDIGIT(chLow  = pchSrc[2]))
                {
                    uint8_t b = RT_C_IS_DIGIT(chHigh) ? chHigh - '0' : (chHigh & ~0x20) - 'A' + 10;
                    b <<= 4;
                    b |= RT_C_IS_DIGIT(chLow) ? chLow - '0' : (chLow & ~0x20) - 'A' + 10;
                    *pchDst++ = (char)b;
                    pchSrc  += 3;
                    cchLeft -= 3;
                }
                else
                {
                    AssertFailed();
                    *pchDst++ = *pchSrc++;
                    cchLeft--;
                }
            }
            else
            {
                memcpy(pchDst, pchSrc, cchLeft);
                pchDst += cchLeft;
                pchSrc += cchLeft;
                cchLeft = 0;
                break;
            }
        }

        *pchDst = '\0';

        /*
         * If we've got lof space room in the result string, reallocate it.
         */
        size_t cchDecoded = pchDst - pszDecoded;
        Assert(cchDecoded <= cchString);
        if (cchString - cchDecoded > 64)
            RTStrRealloc(&pszDecoded, cchDecoded + 1);
    }
    return pszDecoded;
}


/**
 * Calculates the decoded string length.
 *
 * @returns Number of chars (excluding the terminator).
 * @param   pszString       The string to decode.
 * @param   cchMax          The maximum string length (e.g. RTSTR_MAX).
 */
static size_t rtUriCalcDecodedLength(const char *pszString, size_t cchMax)
{
    size_t cchDecoded;
    if (pszString)
    {
        size_t cchSrcLeft = cchDecoded = RTStrNLen(pszString, cchMax);
        while (cchSrcLeft-- > 0)
        {
            char const ch = *pszString++;
            if (ch != '%')
            { /* typical */}
            else if (   cchSrcLeft >= 2
                     && RT_C_IS_XDIGIT(pszString[0])
                     && RT_C_IS_XDIGIT(pszString[1]))
            {
                cchDecoded -= 2;
                pszString  += 2;
                cchSrcLeft -= 2;
            }
        }
    }
    else
        cchDecoded = 0;
    return cchDecoded;
}


/**
 * Decodes a string into a buffer.
 *
 * @returns IPRT status code.
 * @param   pchSrc      The source string.
 * @param   cchSrc      The max number of bytes to decode in the source string.
 * @param   pszDst      The destination buffer.
 * @param   cbDst       The size of the buffer (including terminator).
 */
static int rtUriDecodeIntoBuffer(const char *pchSrc, size_t cchSrc, char *pszDst, size_t cbDst)
{
    AssertPtrReturn(pchSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);

    /*
     * Knowing that the pszString itself is valid UTF-8, we only have to
     * validate the escape sequences.
     */
    cchSrc = RTStrNLen(pchSrc, cchSrc);
    while (cchSrc > 0)
    {
        const char *pchPct = (const char *)memchr(pchSrc, '%', cchSrc);
        if (pchPct)
        {
            size_t cchBefore = pchPct - pchSrc;
            AssertReturn(cchBefore + 1 < cbDst, VERR_BUFFER_OVERFLOW);
            if (cchBefore)
            {
                memcpy(pszDst, pchSrc, cchBefore);
                pszDst += cchBefore;
                cbDst  -= cchBefore;
                pchSrc += cchBefore;
                cchSrc -= cchBefore;
            }

            char chHigh, chLow;
            if (   cchSrc >= 3
                && RT_C_IS_XDIGIT(chHigh = pchSrc[1])
                && RT_C_IS_XDIGIT(chLow  = pchSrc[2]))
            {
                uint8_t b = RT_C_IS_DIGIT(chHigh) ? chHigh - '0' : (chHigh & ~0x20) - 'A' + 10;
                b <<= 4;
                b |= RT_C_IS_DIGIT(chLow) ? chLow - '0' : (chLow & ~0x20) - 'A' + 10;
                *pszDst++ = (char)b;
                pchSrc += 3;
                cchSrc -= 3;
            }
            else
            {
                AssertFailed();
                *pszDst++ = *pchSrc++;
                cchSrc--;
            }
            cbDst -= 1;
        }
        else
        {
            AssertReturn(cchSrc < cbDst, VERR_BUFFER_OVERFLOW);
            memcpy(pszDst, pchSrc, cchSrc);
            pszDst += cchSrc;
            cbDst  -= cchSrc;
            pchSrc += cchSrc;
            cchSrc  = 0;
            break;
        }
    }

    AssertReturn(cbDst > 0, VERR_BUFFER_OVERFLOW);
    *pszDst = '\0';
    return VINF_SUCCESS;
}



static int rtUriParse(const char *pszUri, PRTURIPARSED pParsed)
{
    /*
     * Validate the input and clear the output.
     */
    AssertPtrReturn(pParsed, VERR_INVALID_POINTER);
    RT_ZERO(*pParsed);
    pParsed->uAuthorityPort = UINT32_MAX;

    AssertPtrReturn(pszUri, VERR_INVALID_POINTER);

    size_t const cchUri = strlen(pszUri);
    if (RT_LIKELY(cchUri >= 3)) { /* likely */ }
    else return cchUri ? VERR_URI_TOO_SHORT : VERR_URI_EMPTY;

    /*
     * Validating escaped text sequences is much simpler if we know that
     * that the base URI string is valid.  Also, we don't necessarily trust
     * the developer calling us to remember to do this.
     */
    int rc = RTStrValidateEncoding(pszUri);
    AssertRCReturn(rc, rc);

    /*
     * RFC-3986, section 3.1:
     *      scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
     *
     * The scheme ends with a ':', which we also skip here.
     */
    size_t off = 0;
    char ch = pszUri[off++];
    if (RT_LIKELY(RT_C_IS_ALPHA(ch))) { /* likely */ }
    else return VERR_URI_INVALID_SCHEME;
    for (;;)
    {
        ch = pszUri[off];
        if (ch == ':')
            break;
        if (RT_LIKELY(RT_C_IS_ALNUM(ch) || ch == '.' || ch == '-' || ch == '+')) { /* likely */ }
        else return VERR_URI_INVALID_SCHEME;
        off++;
    }
    pParsed->cchScheme = off;

    /* Require the scheme length to be at least two chars so we won't confuse
       it with a path starting with a DOS drive letter specification. */
    if (RT_LIKELY(off >= 2)) { /* likely */ }
    else return VERR_URI_INVALID_SCHEME;

    off++;                              /* (skip colon) */

    /*
     * Find the end of the path, we'll need this several times.
     * Also, while we're potentially scanning the whole thing, check for '%'.
     */
    size_t const offHash         = RTStrOffCharOrTerm(&pszUri[off], '#') + off;
    size_t const offQuestionMark = RTStrOffCharOrTerm(&pszUri[off], '?') + off;

    if (memchr(pszUri, '%', cchUri) != NULL)
        pParsed->fFlags |= RTURIPARSED_F_CONTAINS_ESCAPED_CHARS;

    /*
     * RFC-3986, section 3.2:
     *      The authority component is preceded by a double slash ("//")...
     */
    if (   pszUri[off] == '/'
        && pszUri[off + 1] == '/')
    {
        off += 2;
        pParsed->offAuthority = pParsed->offAuthorityUsername = pParsed->offAuthorityPassword = pParsed->offAuthorityHost = off;
        pParsed->fFlags |= RTURIPARSED_F_HAS_AUTHORITY;

        /*
         * RFC-3986, section 3.2:
         *      ...and is terminated by the next slash ("/"), question mark ("?"),
         *       or number sign ("#") character, or by the end of the URI.
         */
        const char *pszAuthority = &pszUri[off];
        size_t      cchAuthority = RTStrOffCharOrTerm(pszAuthority, '/');
        cchAuthority = RT_MIN(cchAuthority, offHash - off);
        cchAuthority = RT_MIN(cchAuthority, offQuestionMark - off);
        pParsed->cchAuthority     = cchAuthority;

        /* The Authority can be empty, like for: file:///usr/bin/grep  */
        if (cchAuthority > 0)
        {
            pParsed->cchAuthorityHost = cchAuthority;

            /*
             * If there is a userinfo part, it is ended by a '@'.
             */
            const char *pszAt = (const char *)memchr(pszAuthority, '@', cchAuthority);
            if (pszAt)
            {
                size_t cchTmp = pszAt - pszAuthority;
                pParsed->offAuthorityHost += cchTmp + 1;
                pParsed->cchAuthorityHost -= cchTmp + 1;

                /* If there is a password part, it's separated from the username with a colon. */
                const char *pszColon = (const char *)memchr(pszAuthority, ':', cchTmp);
                if (pszColon)
                {
                    pParsed->cchAuthorityUsername = pszColon - pszAuthority;
                    pParsed->offAuthorityPassword = &pszColon[1] - pszUri;
                    pParsed->cchAuthorityPassword = pszAt - &pszColon[1];
                }
                else
                {
                    pParsed->cchAuthorityUsername = cchTmp;
                    pParsed->offAuthorityPassword = off + cchTmp;
                }
            }

            /*
             * If there is a port part, its after the last colon in the host part.
             */
            const char *pszColon = (const char *)memrchr(&pszUri[pParsed->offAuthorityHost], ':', pParsed->cchAuthorityHost);
            if (pszColon)
            {
                size_t cchTmp = &pszUri[pParsed->offAuthorityHost + pParsed->cchAuthorityHost] - &pszColon[1];
                pParsed->cchAuthorityHost -= cchTmp + 1;
                pParsed->fFlags |= RTURIPARSED_F_HAS_PORT;
                if (cchTmp > 0)
                {
                    pParsed->uAuthorityPort = 0;
                    while (cchTmp-- > 0)
                    {
                        ch = *++pszColon;
                        if (   RT_C_IS_DIGIT(ch)
                            && pParsed->uAuthorityPort < UINT32_MAX / UINT32_C(10))
                        {
                            pParsed->uAuthorityPort *= 10;
                            pParsed->uAuthorityPort += ch - '0';
                        }
                        else
                            return VERR_URI_INVALID_PORT_NUMBER;
                    }
                }
            }
        }

        /* Skip past the authority. */
        off += cchAuthority;
    }
    else
        pParsed->offAuthority = pParsed->offAuthorityUsername = pParsed->offAuthorityPassword = pParsed->offAuthorityHost = off;

    /*
     * RFC-3986, section 3.3: Path
     *      The path is terminated by the first question mark ("?")
     *      or number sign ("#") character, or by the end of the URI.
     */
    pParsed->offPath = off;
    pParsed->cchPath = RT_MIN(offHash, offQuestionMark) - off;
    off += pParsed->cchPath;

    /*
     * RFC-3986, section 3.4: Query
     *      The query component is indicated by the first question mark ("?")
     *      character and terminated by a number sign ("#") character or by the
     *      end of the URI.
     */
    if (   off == offQuestionMark
        && off < cchUri)
    {
        Assert(pszUri[offQuestionMark] == '?');
        pParsed->offQuery = ++off;
        pParsed->cchQuery = offHash - off;
        off = offHash;
    }
    else
    {
        Assert(!pszUri[offQuestionMark]);
        pParsed->offQuery = off;
    }

    /*
     * RFC-3986, section 3.5: Fragment
     *      A fragment identifier component is indicated by the presence of a
     *      number sign ("#") character and terminated by the end of the URI.
     */
    if (   off == offHash
        && off < cchUri)
    {
        pParsed->offFragment = ++off;
        pParsed->cchFragment = cchUri - off;
    }
    else
    {
        Assert(!pszUri[offHash]);
        pParsed->offFragment = off;
    }

    /*
     * If there are any escape sequences, validate them.
     *
     * This is reasonably simple as we already know that the string is valid UTF-8
     * before they get decoded.  Thus we only have to validate the escaped sequences.
     */
    if (pParsed->fFlags & RTURIPARSED_F_CONTAINS_ESCAPED_CHARS)
    {
        const char *pchSrc  = (const char *)memchr(pszUri, '%', cchUri);
        AssertReturn(pchSrc, VERR_INTERNAL_ERROR);
        do
        {
            char        szUtf8Seq[8];
            unsigned    cchUtf8Seq = 0;
            unsigned    cchNeeded  = 0;
            size_t      cchLeft    = &pszUri[cchUri] - pchSrc;
            do
            {
                if (cchLeft >= 3)
                {
                    char chHigh = pchSrc[1];
                    char chLow  = pchSrc[2];
                    if (   RT_C_IS_XDIGIT(chHigh)
                        && RT_C_IS_XDIGIT(chLow))
                    {
                        uint8_t b = RT_C_IS_DIGIT(chHigh) ? chHigh - '0' : (chHigh & ~0x20) - 'A' + 10;
                        b <<= 4;
                        b |= RT_C_IS_DIGIT(chLow) ? chLow - '0' : (chLow & ~0x20) - 'A' + 10;

                        if (!(b & 0x80))
                        {
                            /* We don't want the string to be terminated prematurely. */
                            if (RT_LIKELY(b != 0)) { /* likely */ }
                            else return VERR_URI_ESCAPED_ZERO;

                            /* Check that we're not expecting more UTF-8 bytes. */
                            if (RT_LIKELY(cchNeeded == 0)) { /* likely */ }
                            else return VERR_URI_MISSING_UTF8_CONTINUATION_BYTE;
                        }
                        /* Are we waiting UTF-8 bytes? */
                        else if (cchNeeded > 0)
                        {
                            if (RT_LIKELY(!(b & 0x40))) { /* likely */ }
                            else return VERR_URI_INVALID_ESCAPED_UTF8_CONTINUATION_BYTE;

                            szUtf8Seq[cchUtf8Seq++] = (char)b;
                            if (--cchNeeded == 0)
                            {
                                szUtf8Seq[cchUtf8Seq] = '\0';
                                rc = RTStrValidateEncoding(szUtf8Seq);
                                if (RT_FAILURE(rc))
                                    return VERR_URI_ESCAPED_CHARS_NOT_VALID_UTF8;
                                cchUtf8Seq = 0;
                            }
                        }
                        /* Start a new UTF-8 sequence. */
                        else
                        {
                            if ((b & 0xf8) == 0xf0)
                                cchNeeded = 3;
                            else if ((b & 0xf0) == 0xe0)
                                cchNeeded = 2;
                            else if ((b & 0xe0) == 0xc0)
                                cchNeeded = 1;
                            else
                                return VERR_URI_INVALID_ESCAPED_UTF8_LEAD_BYTE;
                            szUtf8Seq[0] = (char)b;
                            cchUtf8Seq = 1;
                        }
                        pchSrc  += 3;
                        cchLeft -= 3;
                    }
                    else
                        return VERR_URI_INVALID_ESCAPE_SEQ;
                }
                else
                    return VERR_URI_INVALID_ESCAPE_SEQ;
            } while (cchLeft > 0 && pchSrc[0] == '%');

            /* Check that we're not expecting more UTF-8 bytes. */
            if (RT_LIKELY(cchNeeded == 0)) { /* likely */ }
            else return VERR_URI_MISSING_UTF8_CONTINUATION_BYTE;

            /* next */
            pchSrc = (const char *)memchr(pchSrc, '%', cchLeft);
        } while (pchSrc);
    }

    pParsed->u32Magic = RTURIPARSED_MAGIC;
    return VINF_SUCCESS;
}


RTDECL(int) RTUriParse(const char *pszUri, PRTURIPARSED pParsed)
{
    return rtUriParse(pszUri, pParsed);
}


RTDECL(char *) RTUriParsedScheme(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    return RTStrDupN(pszUri, pParsed->cchScheme);
}


RTDECL(char *) RTUriParsedAuthority(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchAuthority || (pParsed->fFlags & RTURIPARSED_F_HAS_AUTHORITY))
        return rtUriPercentDecodeN(&pszUri[pParsed->offAuthority], pParsed->cchAuthority);
    return NULL;
}


RTDECL(char *) RTUriParsedAuthorityUsername(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchAuthorityUsername)
        return rtUriPercentDecodeN(&pszUri[pParsed->offAuthorityUsername], pParsed->cchAuthorityUsername);
    return NULL;
}


RTDECL(char *) RTUriParsedAuthorityPassword(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchAuthorityPassword)
        return rtUriPercentDecodeN(&pszUri[pParsed->offAuthorityPassword], pParsed->cchAuthorityPassword);
    return NULL;
}


RTDECL(char *) RTUriParsedAuthorityHost(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchAuthorityHost)
        return rtUriPercentDecodeN(&pszUri[pParsed->offAuthorityHost], pParsed->cchAuthorityHost);
    return NULL;
}


RTDECL(uint32_t) RTUriParsedAuthorityPort(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, UINT32_MAX);
    AssertPtrReturn(pParsed, UINT32_MAX);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, UINT32_MAX);
    return pParsed->uAuthorityPort;
}


RTDECL(char *) RTUriParsedPath(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchPath)
        return rtUriPercentDecodeN(&pszUri[pParsed->offPath], pParsed->cchPath);
    return NULL;
}


RTDECL(char *) RTUriParsedQuery(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchQuery)
        return rtUriPercentDecodeN(&pszUri[pParsed->offQuery], pParsed->cchQuery);
    return NULL;
}


RTDECL(char *) RTUriParsedFragment(const char *pszUri, PCRTURIPARSED pParsed)
{
    AssertPtrReturn(pszUri, NULL);
    AssertPtrReturn(pParsed, NULL);
    AssertReturn(pParsed->u32Magic == RTURIPARSED_MAGIC, NULL);
    if (pParsed->cchFragment)
        return rtUriPercentDecodeN(&pszUri[pParsed->offFragment], pParsed->cchFragment);
    return NULL;
}


RTDECL(char *) RTUriCreate(const char *pszScheme, const char *pszAuthority, const char *pszPath, const char *pszQuery,
                           const char *pszFragment)
{
    if (!pszScheme) /* Scheme is minimum requirement */
        return NULL;

    char *pszResult = 0;
    char *pszAuthority1 = 0;
    char *pszPath1 = 0;
    char *pszQuery1 = 0;
    char *pszFragment1 = 0;

    do
    {
        /* Create the percent encoded strings and calculate the necessary uri
         * length. */
        size_t cbSize = strlen(pszScheme) + 1 + 1; /* plus zero byte */
        if (pszAuthority)
        {
            pszAuthority1 = rtUriPercentEncodeN(pszAuthority, RTSTR_MAX);
            if (!pszAuthority1)
                break;
            cbSize += strlen(pszAuthority1) + 2;
        }
        if (pszPath)
        {
            pszPath1 = rtUriPercentEncodeN(pszPath, RTSTR_MAX);
            if (!pszPath1)
                break;
            cbSize += strlen(pszPath1);
        }
        if (pszQuery)
        {
            pszQuery1 = rtUriPercentEncodeN(pszQuery, RTSTR_MAX);
            if (!pszQuery1)
                break;
            cbSize += strlen(pszQuery1) + 1;
        }
        if (pszFragment)
        {
            pszFragment1 = rtUriPercentEncodeN(pszFragment, RTSTR_MAX);
            if (!pszFragment1)
                break;
            cbSize += strlen(pszFragment1) + 1;
        }

        char *pszTmp = pszResult = (char *)RTStrAlloc(cbSize);
        if (!pszResult)
            break;
        RT_BZERO(pszTmp, cbSize);

        /* Compose the target uri string. */
        RTStrCatP(&pszTmp, &cbSize, pszScheme);
        RTStrCatP(&pszTmp, &cbSize, ":");
        if (pszAuthority1)
        {
            RTStrCatP(&pszTmp, &cbSize, "//");
            RTStrCatP(&pszTmp, &cbSize, pszAuthority1);
        }
        if (pszPath1)
        {
            RTStrCatP(&pszTmp, &cbSize, pszPath1);
        }
        if (pszQuery1)
        {
            RTStrCatP(&pszTmp, &cbSize, "?");
            RTStrCatP(&pszTmp, &cbSize, pszQuery1);
        }
        if (pszFragment1)
        {
            RTStrCatP(&pszTmp, &cbSize, "#");
            RTStrCatP(&pszTmp, &cbSize, pszFragment1);
        }
    } while (0);

    /* Cleanup */
    if (pszAuthority1)
        RTStrFree(pszAuthority1);
    if (pszPath1)
        RTStrFree(pszPath1);
    if (pszQuery1)
        RTStrFree(pszQuery1);
    if (pszFragment1)
        RTStrFree(pszFragment1);

    return pszResult;
}


RTDECL(bool)   RTUriIsSchemeMatch(const char *pszUri, const char *pszScheme)
{
    AssertPtrReturn(pszUri, false);
    size_t const cchScheme = strlen(pszScheme);
    return RTStrNICmp(pszUri, pszScheme, cchScheme) == 0
        && pszUri[cchScheme] == ':';
}


RTDECL(int) RTUriFileCreateEx(const char *pszPath, uint32_t fPathStyle, char **ppszUri, size_t cbUri, size_t *pcchUri)
{
    /*
     * Validate and adjust input. (RTPathParse check pszPath out for us)
     */
    if (pcchUri)
    {
        AssertPtrReturn(pcchUri, VERR_INVALID_POINTER);
        *pcchUri = ~(size_t)0;
    }
    AssertPtrReturn(ppszUri, VERR_INVALID_POINTER);
    AssertReturn(!(fPathStyle & ~RTPATH_STR_F_STYLE_MASK) && fPathStyle != RTPATH_STR_F_STYLE_RESERVED, VERR_INVALID_FLAGS);
    if (fPathStyle == RTPATH_STR_F_STYLE_HOST)
        fPathStyle = RTPATH_STYLE;

    /*
     * Let the RTPath code parse the stuff (no reason to duplicate path parsing
     * and get it slightly wrong here).
     */
    union
    {
        RTPATHPARSED ParsedPath;
        uint8_t      abPadding[sizeof(RTPATHPARSED)];
    } u;
    int rc = RTPathParse(pszPath, &u.ParsedPath, sizeof(u.ParsedPath), fPathStyle);
    if (RT_SUCCESS(rc) || rc == VERR_BUFFER_OVERFLOW)
    {
        /* Skip leading slashes. */
        if (u.ParsedPath.fProps & RTPATH_PROP_ROOT_SLASH)
        {
            if (fPathStyle == RTPATH_STR_F_STYLE_DOS)
                while (pszPath[0] == '/' || pszPath[0] == '\\')
                    pszPath++;
            else
                while (pszPath[0] == '/')
                    pszPath++;
        }
        const size_t cchPath = strlen(pszPath);

        /*
         * Calculate the encoded length and figure destination buffering.
         */
        static const char s_szPrefix[] = "file:///";
        size_t const      cchPrefix    = sizeof(s_szPrefix) - (u.ParsedPath.fProps & RTPATH_PROP_UNC ? 2 : 1);
        size_t cchEncoded = rtUriCalcEncodedLength(pszPath, cchPath, fPathStyle != RTPATH_STR_F_STYLE_DOS);

        if (pcchUri)
            *pcchUri = cchEncoded;

        char  *pszDst;
        char  *pszFreeMe = NULL;
        if (!cbUri || *ppszUri == NULL)
        {
            cbUri = RT_MAX(cbUri, cchPrefix + cchEncoded + 1);
            *ppszUri = pszFreeMe = pszDst = RTStrAlloc(cbUri);
            AssertReturn(pszDst, VERR_NO_STR_MEMORY);
        }
        else if (cchEncoded < cbUri)
            pszDst = *ppszUri;
        else
            return VERR_BUFFER_OVERFLOW;

        /*
         * Construct the URI.
         */
        memcpy(pszDst, s_szPrefix, cchPrefix);
        pszDst[cchPrefix] = '\0';
        rc = rtUriEncodeIntoBuffer(pszPath, cchPath, fPathStyle != RTPATH_STR_F_STYLE_DOS, &pszDst[cchPrefix], cbUri - cchPrefix);
        if (RT_SUCCESS(rc))
        {
            Assert(strlen(pszDst) == cbUri - 1);
            if (fPathStyle == RTPATH_STR_F_STYLE_DOS)
                RTPathChangeToUnixSlashes(pszDst, true /*fForce*/);
            return VINF_SUCCESS;
        }

        AssertRC(rc); /* Impossible! rtUriCalcEncodedLength or something above is busted! */
        if (pszFreeMe)
            RTStrFree(pszFreeMe);
    }
    return rc;
}


RTDECL(char *) RTUriFileCreate(const char *pszPath)
{
    char *pszUri = NULL;
    int rc = RTUriFileCreateEx(pszPath, RTPATH_STR_F_STYLE_HOST, &pszUri, 0 /*cbUri*/, NULL /*pcchUri*/);
    if (RT_SUCCESS(rc))
        return pszUri;
    return NULL;
}


RTDECL(int) RTUriFilePathEx(const char *pszUri, uint32_t fPathStyle, char **ppszPath, size_t cbPath, size_t *pcchPath)
{
    /*
     * Validate and adjust input.
     */
    if (pcchPath)
    {
        AssertPtrReturn(pcchPath, VERR_INVALID_POINTER);
        *pcchPath = ~(size_t)0;
    }
    AssertPtrReturn(ppszPath, VERR_INVALID_POINTER);
    AssertReturn(!(fPathStyle & ~RTPATH_STR_F_STYLE_MASK) && fPathStyle != RTPATH_STR_F_STYLE_RESERVED, VERR_INVALID_FLAGS);
    if (fPathStyle == RTPATH_STR_F_STYLE_HOST)
        fPathStyle = RTPATH_STYLE;
    AssertPtrReturn(pszUri, VERR_INVALID_POINTER);

    /*
     * Check that this is a file URI.
     */
    if (RTStrNICmp(pszUri, RT_STR_TUPLE("file:")) == 0)
    { /* likely */ }
    else
        return VERR_URI_NOT_FILE_SCHEME;

    /*
     * We may have a number of variations here, mostly thanks to
     * various windows software.  First the canonical variations:
     *     - file:///C:/Windows/System32/kernel32.dll
     *     - file:///C|/Windows/System32/kernel32.dll
     *     - file:///C:%5CWindows%5CSystem32%5Ckernel32.dll
     *     - file://localhost/C:%5CWindows%5CSystem32%5Ckernel32.dll
     *     - file://cifsserver.dev/systemshare%5CWindows%5CSystem32%5Ckernel32.dll
     *     - file://cifsserver.dev:139/systemshare%5CWindows%5CSystem32%5Ckernel32.dll  (not quite sure here, but whatever)
     *
     * Legacy variant without any slashes after the schema:
     *     - file:C:/Windows/System32/kernel32.dll
     *     - file:C|/Windows/System32%5Ckernel32.dll
     *     - file:~/.bashrc
     *            \--path-/
     *
     * Legacy variant with exactly one slashes after the schema:
     *     - file:/C:/Windows/System32%5Ckernel32.dll
     *     - file:/C|/Windows/System32/kernel32.dll
     *     - file:/usr/bin/env
     *            \---path---/
     *
     * Legacy variant with two slashes after the schema and an unescaped DOS path:
     *     - file://C:/Windows/System32\kernel32.dll (**)
     *     - file://C|/Windows/System32\kernel32.dll
     *                \---path---------------------/
     *              -- authority, with ':' as non-working port separator
     *
     * Legacy variant with exactly four slashes after the schema and an unescaped DOS path.
     *     - file:////C:/Windows\System32\user32.dll
     *
     * Legacy variant with four or more slashes after the schema and an unescaped UNC path:
     *     - file:////cifsserver.dev/systemshare/System32%\kernel32.dll
     *     - file://///cifsserver.dev/systemshare/System32\kernel32.dll
     *              \---path--------------------------------------------/
     *
     * The two unescaped variants shouldn't be handed to rtUriParse, which
     * is good as we cannot actually handle the one marked by (**).  So, handle
     * those two special when parsing.
     */
    RTURIPARSED Parsed;
    int         rc;
    size_t      cSlashes = 0;
    while (pszUri[5 + cSlashes] == '/')
        cSlashes++;
    if (   (cSlashes == 2 || cSlashes == 4)
        && RT_C_IS_ALPHA(pszUri[5 + cSlashes])
        && (pszUri[5 + cSlashes + 1] == ':' || pszUri[5 + cSlashes + 1] == '|'))
    {
        RT_ZERO(Parsed); /* RTURIPARSED_F_CONTAINS_ESCAPED_CHARS is now clear. */
        Parsed.offPath = 5 + cSlashes;
        Parsed.cchPath = strlen(&pszUri[Parsed.offPath]);
        rc = RTStrValidateEncoding(&pszUri[Parsed.offPath]);
    }
    else if (cSlashes >= 4)
    {
        RT_ZERO(Parsed);
        Parsed.fFlags  = cSlashes > 4 ? RTURIPARSED_F_CONTAINS_ESCAPED_CHARS : 0;
        Parsed.offPath = 5 + cSlashes - 2;
        Parsed.cchPath = strlen(&pszUri[Parsed.offPath]);
        rc = RTStrValidateEncoding(&pszUri[Parsed.offPath]);
    }
    else
        rc = rtUriParse(pszUri, &Parsed);
    if (RT_SUCCESS(rc))
    {
        /*
         * Ignore localhost as hostname (it's implicit).
         */
        static char const s_szLocalhost[] = "localhost";
        if (    Parsed.cchAuthorityHost == sizeof(s_szLocalhost) - 1U
            &&  RTStrNICmp(&pszUri[Parsed.offAuthorityHost], RT_STR_TUPLE(s_szLocalhost)) == 0)
        {
            Parsed.cchAuthorityHost = 0;
            Parsed.cchAuthority     = 0;
        }

        /*
         * Ignore leading path slash/separator if we detect a DOS drive letter
         * and we don't have a host name.
         */
        if (   Parsed.cchPath >= 3
            && Parsed.cchAuthorityHost    == 0
            && pszUri[Parsed.offPath]     == '/'           /* Leading path slash/separator. */
            && (   pszUri[Parsed.offPath + 2] == ':'       /* Colon after drive letter. */
                || pszUri[Parsed.offPath + 2] == '|')      /* Colon alternative. */
            && RT_C_IS_ALPHA(pszUri[Parsed.offPath + 1]) ) /* Drive letter. */
        {
            Parsed.offPath++;
            Parsed.cchPath--;
        }

        /*
         * Calculate the size of the encoded result.
         *
         * Since we're happily returning "C:/Windows/System32/kernel.dll"
         * style paths when the caller requested UNIX style paths, we will
         * return straight UNC paths too ("//cifsserver/share/dir/file").
         */
        size_t cchDecodedHost = 0;
        size_t cbResult;
        if (Parsed.fFlags & RTURIPARSED_F_CONTAINS_ESCAPED_CHARS)
        {
            cchDecodedHost = rtUriCalcDecodedLength(&pszUri[Parsed.offAuthorityHost], Parsed.cchAuthorityHost);
            cbResult = cchDecodedHost + rtUriCalcDecodedLength(&pszUri[Parsed.offPath], Parsed.cchPath) + 1;
        }
        else
        {
            cchDecodedHost = 0;
            cbResult = Parsed.cchAuthorityHost + Parsed.cchPath + 1;
        }
        if (pcchPath)
            *pcchPath = cbResult - 1;
        if (cbResult > 1)
        {
            /*
             * Prepare the necessary buffer space for the result.
             */
            char  *pszDst;
            char  *pszFreeMe = NULL;
            if (!cbPath || *ppszPath == NULL)
            {
                cbPath = RT_MAX(cbPath, cbResult);
                *ppszPath = pszFreeMe = pszDst = RTStrAlloc(cbPath);
                AssertReturn(pszDst, VERR_NO_STR_MEMORY);
            }
            else if (cbResult <= cbPath)
                pszDst = *ppszPath;
            else
                return VERR_BUFFER_OVERFLOW;

            /*
             * Compose the result.
             */
            if (Parsed.fFlags & RTURIPARSED_F_CONTAINS_ESCAPED_CHARS)
            {
                rc = rtUriDecodeIntoBuffer(&pszUri[Parsed.offAuthorityHost],Parsed.cchAuthorityHost,
                                           pszDst, cchDecodedHost + 1);
                Assert(RT_SUCCESS(rc) && strlen(pszDst) == cchDecodedHost);
                if (RT_SUCCESS(rc))
                    rc = rtUriDecodeIntoBuffer(&pszUri[Parsed.offPath], Parsed.cchPath,
                                               &pszDst[cchDecodedHost], cbResult - cchDecodedHost);
                Assert(RT_SUCCESS(rc) && strlen(pszDst) == cbResult - 1);
            }
            else
            {
                memcpy(pszDst, &pszUri[Parsed.offAuthorityHost], Parsed.cchAuthorityHost);
                memcpy(&pszDst[Parsed.cchAuthorityHost], &pszUri[Parsed.offPath], Parsed.cchPath);
                pszDst[cbResult - 1] = '\0';
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Convert colon DOS driver letter colon alternative.
                 * We do this regardless of the desired path style.
                 */
                if (   RT_C_IS_ALPHA(pszDst[0])
                    && pszDst[1] == '|')
                    pszDst[1] = ':';

                /*
                 * Fix slashes.
                 */
                if (fPathStyle == RTPATH_STR_F_STYLE_DOS)
                    RTPathChangeToDosSlashes(pszDst, true);
                else if (fPathStyle == RTPATH_STR_F_STYLE_UNIX)
                    RTPathChangeToUnixSlashes(pszDst, true); /** @todo not quite sure how this actually makes sense... */
                else
                    AssertFailed();
                return rc;
            }

            /* bail out */
            RTStrFree(pszFreeMe);
        }
        else
            rc = VERR_PATH_ZERO_LENGTH;
    }
    return rc;
}


RTDECL(char *) RTUriFilePath(const char *pszUri)
{
    char *pszPath = NULL;
    int rc = RTUriFilePathEx(pszUri, RTPATH_STR_F_STYLE_HOST, &pszPath, 0 /*cbPath*/, NULL /*pcchPath*/);
    if (RT_SUCCESS(rc))
        return pszPath;
    return NULL;
}

