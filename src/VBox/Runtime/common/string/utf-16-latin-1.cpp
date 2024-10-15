/* $Id: utf-16-latin-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Latin-1 and UTF-16.
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
#include <iprt/latin1.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/uni.h>
#include "internal/string.h"


/**
 * Validate the UTF-16 encoding and calculates the length of a Latin1 encoding.
 *
 * @returns iprt status code.
 * @param   pwsz        The UTF-16 string.
 * @param   cwc         The max length of the UTF-16 string to consider.
 * @param   pcch        Where to store the length (excluding '\\0') of the Latin1 string. (cch == cb, btw)
 */
static int rtUtf16CalcLatin1Length(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    int     rc = VINF_SUCCESS;
    size_t  cch = 0;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz++; cwc--;
        if (!wc)
            break;
        else if (RT_LIKELY(wc < 0x100))
            ++cch;
        else
        {
            if (wc < 0xd800 || wc > 0xdfff)
            {
                if (wc >= 0xfffe)
                {
                    RTStrAssertMsgFailed(("endian indicator! wc=%#x\n", wc));
                    rc = VERR_CODE_POINT_ENDIAN_INDICATOR;
                    break;
                }
            }
            else
            {
                if (wc >= 0xdc00)
                {
                    RTStrAssertMsgFailed(("Wrong 1st char in surrogate! wc=%#x\n", wc));
                    rc = VERR_INVALID_UTF16_ENCODING;
                    break;
                }
                if (cwc <= 0)
                {
                    RTStrAssertMsgFailed(("Invalid length! wc=%#x\n", wc));
                    rc = VERR_INVALID_UTF16_ENCODING;
                    break;
                }
                wc = *pwsz++; cwc--;
                if (wc < 0xdc00 || wc > 0xdfff)
                {
                    RTStrAssertMsgFailed(("Wrong 2nd char in surrogate! wc=%#x\n", wc));
                    rc = VERR_INVALID_UTF16_ENCODING;
                    break;
                }
            }

            rc = VERR_NO_TRANSLATION;
            break;
        }
    }

    /* done */
    *pcch = cch;
    return rc;
}


/**
 * Recodes an valid UTF-16 string as Latin1.
 *
 * @returns iprt status code.
 * @param   pwsz        The UTF-16 string.
 * @param   cwc         The number of RTUTF16 characters to process from pwsz. The recoding
 *                      will stop when cwc or '\\0' is reached.
 * @param   psz         Where to store the Latin1 string.
 * @param   cch         The size of the Latin1 buffer, excluding the terminator.
 */
static int rtUtf16RecodeAsLatin1(PCRTUTF16 pwsz, size_t cwc, char *psz, size_t cch)
{
    unsigned char  *pch = (unsigned char *)psz;
    int             rc  = VINF_SUCCESS;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz++; cwc--;
        if (!wc)
            break;
        if (RT_LIKELY(wc < 0x100))
        {
            if (RT_UNLIKELY(cch < 1))
            {
                RTStrAssertMsgFailed(("Buffer overflow! 1\n"));
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            cch--;
            *pch++ = (unsigned char)wc;
        }
        else
        {
            if (wc < 0xd800 || wc > 0xdfff)
            {
                if (wc >= 0xfffe)
                {
                    RTStrAssertMsgFailed(("endian indicator! wc=%#x\n", wc));
                    rc = VERR_CODE_POINT_ENDIAN_INDICATOR;
                    break;
                }
            }
            else
            {
                if (wc >= 0xdc00)
                {
                    RTStrAssertMsgFailed(("Wrong 1st char in surrogate! wc=%#x\n", wc));
                    rc = VERR_INVALID_UTF16_ENCODING;
                    break;
                }
                if (cwc <= 0)
                {
                    RTStrAssertMsgFailed(("Invalid length! wc=%#x\n", wc));
                    rc = VERR_INVALID_UTF16_ENCODING;
                    break;
                }
                RTUTF16 wc2 = *pwsz++; cwc--;
                if (wc2 < 0xdc00 || wc2 > 0xdfff)
                {
                    RTStrAssertMsgFailed(("Wrong 2nd char in surrogate! wc=%#x\n", wc));
                    rc = VERR_INVALID_UTF16_ENCODING;
                    break;
                }
            }

            rc = VERR_NO_TRANSLATION;
            break;
        }
    }

    /* done */
    *pch = '\0';
    return rc;
}


RTDECL(int)  RTUtf16ToLatin1Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(ppszString);
    AssertPtr(pwszString);
    *ppszString = NULL;

    /*
     * Validate the UTF-16 string and calculate the length of the UTF-8 encoding of it.
     */
    size_t cch;
    int rc = rtUtf16CalcLatin1Length(pwszString, RTSTR_MAX, &cch);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer and recode it.
         */
        char *pszResult = (char *)RTMemAllocTag(cch + 1, pszTag);
        if (pszResult)
        {
            rc = rtUtf16RecodeAsLatin1(pwszString, RTSTR_MAX, pszResult, cch);
            if (RT_SUCCESS(rc))
            {
                *ppszString = pszResult;
                return rc;
            }

            RTMemFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16ToLatin1Tag);


RTDECL(int)  RTUtf16ToLatin1ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(pwszString);
    AssertPtr(ppsz);
    AssertPtrNull(pcch);

    /*
     * Validate the UTF-16 string and calculate the length of the Latin1 encoding of it.
     */
    size_t cchResult;
    int rc = rtUtf16CalcLatin1Length(pwszString, cwcString, &cchResult);
    if (RT_SUCCESS(rc))
    {
        if (pcch)
            *pcch = cchResult;

        /*
         * Check buffer size / Allocate buffer and recode it.
         */
        bool fShouldFree;
        char *pszResult;
        if (cch > 0 && *ppsz)
        {
            fShouldFree = false;
            if (cch <= cchResult)
                return VERR_BUFFER_OVERFLOW;
            pszResult = *ppsz;
        }
        else
        {
            *ppsz = NULL;
            fShouldFree = true;
            cch = RT_MAX(cch, cchResult + 1);
            pszResult = (char *)RTMemAllocTag(cch, pszTag);
        }
        if (pszResult)
        {
            rc = rtUtf16RecodeAsLatin1(pwszString, cwcString, pszResult, cch - 1);
            if (RT_SUCCESS(rc))
            {
                *ppsz = pszResult;
                return rc;
            }

            if (fShouldFree)
                RTMemFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16ToLatin1ExTag);


RTDECL(size_t) RTUtf16CalcLatin1Len(PCRTUTF16 pwsz)
{
    size_t cch;
    int rc = rtUtf16CalcLatin1Length(pwsz, RTSTR_MAX, &cch);
    return RT_SUCCESS(rc) ? cch : 0;
}
RT_EXPORT_SYMBOL(RTUtf16CalcLatin1Len);


RTDECL(int) RTUtf16CalcLatin1LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    size_t cch;
    int rc = rtUtf16CalcLatin1Length(pwsz, cwc, &cch);
    if (pcch)
        *pcch = RT_SUCCESS(rc) ? cch : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16CalcLatin1LenEx);


/**
 * Calculates the UTF-16 length of a Latin1 string.  In fact this is just the
 * original length, but the function saves us nasty comments to that effect
 * all over the place.
 *
 * @returns IPRT status code.
 * @param   psz     Pointer to the Latin1 string.
 * @param   cch     The max length of the string. (btw cch = cb)
 *                  Use RTSTR_MAX if all of the string is to be examined.s
 * @param   pcwc    Where to store the length of the UTF-16 string as a number of RTUTF16 characters.
 */
static int rtLatin1CalcUtf16Length(const char *psz, size_t cch, size_t *pcwc)
{
    *pcwc = RTStrNLen(psz, cch);
    return VINF_SUCCESS;
}


/**
 * Recodes a Latin1 string as UTF-16.  This is just a case of expanding it to
 * sixteen bits, as Unicode is a superset of Latin1.
 *
 * Since we know the input is valid, we do *not* perform length checks.
 *
 * @returns iprt status code.
 * @param   psz     The Latin1 string to recode.
 * @param   cch     The number of chars (the type char, so bytes if you like) to process of the Latin1 string.
 *                  The recoding will stop when cch or '\\0' is reached. Pass RTSTR_MAX to process up to '\\0'.
 * @param   pwsz    Where to store the UTF-16 string.
 * @param   cwc     The number of RTUTF16 items the pwsz buffer can hold, excluding the terminator ('\\0').
 */
static int rtLatin1RecodeAsUtf16(const char *psz, size_t cch, PRTUTF16 pwsz, size_t cwc)
{
    int                     rc   = VINF_SUCCESS;
    const unsigned char    *puch = (const unsigned char *)psz;
    PRTUTF16                pwc  = pwsz;
    while (cch-- > 0)
    {
        /* read the next char and check for terminator. */
        const unsigned char uch = *puch;
        if (!uch)
            break;

        /* check for output overflow */
        if (RT_UNLIKELY(cwc < 1))
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }

        /* expand the code point */
        *pwc++ = uch;
        cwc--;
        puch++;
    }

    /* done */
    *pwc = '\0';
    return rc;
}


RTDECL(int) RTLatin1ToUtf16Tag(const char *pszString, PRTUTF16 *ppwszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(ppwszString);
    AssertPtr(pszString);
    *ppwszString = NULL;

    /*
     * Validate the input and calculate the length of the UTF-16 string.
     */
    size_t cwc;
    int rc = rtLatin1CalcUtf16Length(pszString, RTSTR_MAX, &cwc);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer.
         */
        PRTUTF16 pwsz = (PRTUTF16)RTMemAllocTag((cwc + 1) * sizeof(RTUTF16), pszTag);
        if (pwsz)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtLatin1RecodeAsUtf16(pszString, RTSTR_MAX, pwsz, cwc);
            if (RT_SUCCESS(rc))
            {
                *ppwszString = pwsz;
                return rc;
            }
            RTMemFree(pwsz);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLatin1ToUtf16Tag);


RTDECL(int)  RTLatin1ToUtf16ExTag(const char *pszString, size_t cchString,
                                  PRTUTF16 *ppwsz, size_t cwc, size_t *pcwc, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(pszString);
    AssertPtr(ppwsz);
    AssertPtrNull(pcwc);

    /*
     * Validate the input and calculate the length of the UTF-16 string.
     */
    size_t cwcResult;
    int rc = rtLatin1CalcUtf16Length(pszString, cchString, &cwcResult);
    if (RT_SUCCESS(rc))
    {
        if (pcwc)
            *pcwc = cwcResult;

        /*
         * Check buffer size / Allocate buffer.
         */
        bool fShouldFree;
        PRTUTF16 pwszResult;
        if (cwc > 0 && *ppwsz)
        {
            fShouldFree = false;
            if (cwc <= cwcResult)
                return VERR_BUFFER_OVERFLOW;
            pwszResult = *ppwsz;
        }
        else
        {
            *ppwsz = NULL;
            fShouldFree = true;
            cwc = RT_MAX(cwcResult + 1, cwc);
            pwszResult = (PRTUTF16)RTMemAllocTag(cwc * sizeof(RTUTF16), pszTag);
        }
        if (pwszResult)
        {
            /*
             * Encode the UTF-16 string.
             */
            rc = rtLatin1RecodeAsUtf16(pszString, cchString, pwszResult, cwc - 1);
            if (RT_SUCCESS(rc))
            {
                *ppwsz = pwszResult;
                return rc;
            }
            if (fShouldFree)
                RTMemFree(pwszResult);
        }
        else
            rc = VERR_NO_UTF16_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLatin1ToUtf16ExTag);


RTDECL(size_t) RTLatin1CalcUtf16Len(const char *psz)
{
    size_t cwc;
    int rc = rtLatin1CalcUtf16Length(psz, RTSTR_MAX, &cwc);
    return RT_SUCCESS(rc) ? cwc : 0;
}
RT_EXPORT_SYMBOL(RTLatin1CalcUtf16Len);


RTDECL(int) RTLatin1CalcUtf16LenEx(const char *psz, size_t cch, size_t *pcwc)
{
    size_t cwc;
    int rc = rtLatin1CalcUtf16Length(psz, cch, &cwc);
    if (pcwc)
        *pcwc = RT_SUCCESS(rc) ? cwc : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTLatin1CalcUtf16LenEx);

