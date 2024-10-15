/* $Id: base64-utf16.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Base64, MIME content transfer encoding.
 *
 * @note The base64.cpp file must be diffable with this one.
 *       Fixed typically applies to both files.
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
#include <iprt/base64.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/uni.h>
#ifdef RT_STRICT
# include <iprt/asm.h>
#endif

#include "base64.h"


/** Translates the given character. */
DECL_FORCE_INLINE(uint8_t) rtBase64TranslateUtf16(RTUTF16 wc)
{
    if (wc < RT_ELEMENTS(g_au8rtBase64CharToVal))
        return g_au8rtBase64CharToVal[wc];
    if (RTUniCpIsSpace(wc))
        return BASE64_SPACE;
    return BASE64_INVALID;
}


/** Fetched the next character in the string and translates it. */
DECL_FORCE_INLINE(uint8_t) rtBase64TranslateNextUtf16(PCRTUTF16 pwszString, size_t cwcStringMax)
{
    if (cwcStringMax > 0)
        return rtBase64TranslateUtf16(*pwszString);
    return BASE64_NULL;
}


/*
 * Mostly the same as RTBase64DecodedSizeEx, except for the wider character
 * type and therefore more careful handling of g_szrtBase64ValToChar and additional
 * space characters.  Fixes must be applied to both copies of the code.
 */
RTDECL(ssize_t) RTBase64DecodedUtf16SizeEx(PCRTUTF16 pwszString, size_t cwcStringMax, PRTUTF16 *ppwszEnd)
{
#ifdef RT_STRICT
    rtBase64Sanity();
#endif

    /*
     * Walk the string until a non-encoded or non-space character is encountered.
     */
    uint32_t    c6Bits = 0;
    uint8_t     u8;

    while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) != BASE64_NULL)
    {
        if (u8 < 64)
            c6Bits++;
        else if (RT_UNLIKELY(u8 != BASE64_SPACE))
            break;

        /* advance */
        pwszString++;
        cwcStringMax--;
    }

    /*
     * Padding can only be found at the end and there is
     * only 1 or 2 padding chars. Deal with it first.
     */
    unsigned    cbPad = 0;
    if (u8 == BASE64_PAD)
    {
        cbPad = 1;
        c6Bits++;
        pwszString++;
        cwcStringMax--;
        while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) != BASE64_NULL)
        {
            if (u8 != BASE64_SPACE)
            {
                if (u8 != BASE64_PAD)
                    break;
                c6Bits++;
                cbPad++;
            }
            pwszString++;
            cwcStringMax--;
        }
        if (cbPad >= 3)
            return -1;
    }

    /*
     * Invalid char and no where to indicate where the
     * Base64 text ends? Return failure.
     */
    if (   u8 == BASE64_INVALID
        && !ppwszEnd)
        return -1;

    /*
     * Recalc 6-bit to 8-bit and adjust for padding.
     */
    if (ppwszEnd)
        *ppwszEnd = (PRTUTF16)pwszString;
    return rtBase64DecodedSizeRecalc(c6Bits, cbPad);
}
RT_EXPORT_SYMBOL(RTBase64DecodedUtf16SizeEx);


RTDECL(ssize_t) RTBase64DecodedUtf16Size(PCRTUTF16 pwszString, PRTUTF16 *ppwszEnd)
{
    return RTBase64DecodedUtf16SizeEx(pwszString, RTSTR_MAX, ppwszEnd);
}
RT_EXPORT_SYMBOL(RTBase64DecodedUtf16Size);


RTDECL(int) RTBase64DecodeUtf16Ex(PCRTUTF16 pwszString, size_t cwcStringMax, void *pvData, size_t cbData,
                                  size_t *pcbActual, PRTUTF16 *ppwszEnd)
{
#ifdef RT_STRICT
    rtBase64Sanity();
#endif

    /*
     * Process input in groups of 4 input / 3 output chars.
     */
    uint8_t     u8Trio[3] = { 0, 0, 0 }; /* shuts up gcc */
    uint8_t    *pbData    = (uint8_t *)pvData;
    uint8_t     u8;
    unsigned    c6Bits    = 0;

    for (;;)
    {
        /* The first 6-bit group. */
        while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) == BASE64_SPACE)
            pwszString++, cwcStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 0;
            break;
        }
        u8Trio[0] = u8 << 2;
        pwszString++;
        cwcStringMax--;

        /* The second 6-bit group. */
        while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) == BASE64_SPACE)
            pwszString++, cwcStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 1;
            break;
        }
        u8Trio[0] |= u8 >> 4;
        u8Trio[1]  = u8 << 4;
        pwszString++;
        cwcStringMax--;

        /* The third 6-bit group. */
        u8 = BASE64_INVALID;
        while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) == BASE64_SPACE)
            pwszString++, cwcStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 2;
            break;
        }
        u8Trio[1] |= u8 >> 2;
        u8Trio[2]  = u8 << 6;
        pwszString++;
        cwcStringMax--;

        /* The fourth 6-bit group. */
        u8 = BASE64_INVALID;
        while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) == BASE64_SPACE)
            pwszString++, cwcStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 3;
            break;
        }
        u8Trio[2] |= u8;
        pwszString++;
        cwcStringMax--;

        /* flush the trio */
        if (cbData < 3)
            return VERR_BUFFER_OVERFLOW;
        cbData -= 3;
        pbData[0] = u8Trio[0];
        pbData[1] = u8Trio[1];
        pbData[2] = u8Trio[2];
        pbData += 3;
    }

    /*
     * Padding can only be found at the end and there is
     * only 1 or 2 padding chars. Deal with it first.
     */
    unsigned cbPad = 0;
    if (u8 == BASE64_PAD)
    {
        cbPad = 1;
        pwszString++;
        cwcStringMax--;
        while ((u8 = rtBase64TranslateNextUtf16(pwszString, cwcStringMax)) != BASE64_NULL)
        {
            if (u8 != BASE64_SPACE)
            {
                if (u8 != BASE64_PAD)
                    break;
                cbPad++;
            }
            pwszString++;
            cwcStringMax--;
        }
        if (cbPad >= 3)
            return VERR_INVALID_BASE64_ENCODING;
    }

    /*
     * Invalid char and no where to indicate where the
     * Base64 text ends? Return failure.
     */
    if (   u8 == BASE64_INVALID
        && !ppwszEnd)
        return VERR_INVALID_BASE64_ENCODING;

    /*
     * Check padding vs. pending sextets, if anything left to do finish it off.
     */
    if (c6Bits || cbPad)
    {
        if (c6Bits + cbPad != 4)
            return VERR_INVALID_BASE64_ENCODING;

        switch (c6Bits)
        {
            case 1:
                u8Trio[1] = u8Trio[2] = 0;
                break;
            case 2:
                u8Trio[2] = 0;
                break;
            case 3:
            default:
                break;
        }
        switch (3 - cbPad)
        {
            case 1:
                if (cbData < 1)
                    return VERR_BUFFER_OVERFLOW;
                cbData--;
                pbData[0] = u8Trio[0];
                pbData++;
                break;

            case 2:
                if (cbData < 2)
                    return VERR_BUFFER_OVERFLOW;
                cbData -= 2;
                pbData[0] = u8Trio[0];
                pbData[1] = u8Trio[1];
                pbData += 2;
                break;

            default:
                break;
        }
    }

    /*
     * Set optional return values and return successfully.
     */
    if (ppwszEnd)
        *ppwszEnd = (PRTUTF16)pwszString;
    if (pcbActual)
        *pcbActual = pbData - (uint8_t *)pvData;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTBase64DecodeUtf16Ex);


RTDECL(int) RTBase64DecodeUtf16(PCRTUTF16 pwszString, void *pvData, size_t cbData, size_t *pcbActual, PRTUTF16 *ppwszEnd)
{
    return RTBase64DecodeUtf16Ex(pwszString, RTSTR_MAX, pvData, cbData, pcbActual, ppwszEnd);
}
RT_EXPORT_SYMBOL(RTBase64DecodeUtf16);


RTDECL(size_t) RTBase64EncodedUtf16Length(size_t cbData)
{
    return RTBase64EncodedLengthEx(cbData, 0);
}
RT_EXPORT_SYMBOL(RTBase64EncodedUtf16Length);


RTDECL(size_t) RTBase64EncodedUtf16LengthEx(size_t cbData, uint32_t fFlags)
{
    return RTBase64EncodedLengthEx(cbData, fFlags);
}
RT_EXPORT_SYMBOL(RTBase64EncodedUtf16LengthEx);


RTDECL(int) RTBase64EncodeUtf16(const void *pvData, size_t cbData, PRTUTF16 pwszBuf, size_t cwcBuf, size_t *pcwcActual)
{
    return RTBase64EncodeUtf16Ex(pvData, cbData, 0, pwszBuf, cwcBuf, pcwcActual);
}
RT_EXPORT_SYMBOL(RTBase64EncodeUtf16);


/*
 * Please note that RTBase64EncodeEx contains an almost exact copy of
 * this code, just using different output character type and variable prefixes.
 * So, all fixes must be applied to both versions of the code.
 */
RTDECL(int) RTBase64EncodeUtf16Ex(const void *pvData, size_t cbData, uint32_t fFlags,
                                  PRTUTF16 pwszBuf, size_t cwcBuf, size_t *pcwcActual)
{
    /* Expand the EOL style flags: */
    size_t const    cchEol = g_acchrtBase64EolStyles[fFlags & RTBASE64_FLAGS_EOL_STYLE_MASK];
    char const      chEol0 = g_aachrtBase64EolStyles[fFlags & RTBASE64_FLAGS_EOL_STYLE_MASK][0];
    char const      chEol1 = g_aachrtBase64EolStyles[fFlags & RTBASE64_FLAGS_EOL_STYLE_MASK][1];
    Assert(cchEol == (chEol0 != '\0' ? 1U : 0U) + (chEol1 != '\0' ? 1U : 0U));

    /*
     * Process whole "trios" of input data.
     */
    uint8_t         u8A;
    uint8_t         u8B;
    uint8_t         u8C;
    size_t          cwcLineFeed = cchEol ? cwcBuf - RTBASE64_LINE_LEN : ~(size_t)0;
    const uint8_t  *pbSrc       = (const uint8_t *)pvData;
    PRTUTF16        pwcDst      = pwszBuf;
    while (cbData >= 3)
    {
        if (cwcBuf < 4 + 1)
            return VERR_BUFFER_OVERFLOW;

        /* encode */
        u8A = pbSrc[0];
        pwcDst[0] = g_szrtBase64ValToChar[u8A >> 2];
        u8B = pbSrc[1];
        pwcDst[1] = g_szrtBase64ValToChar[((u8A << 4) & 0x3f) | (u8B >> 4)];
        u8C = pbSrc[2];
        pwcDst[2] = g_szrtBase64ValToChar[((u8B << 2) & 0x3f) | (u8C >> 6)];
        pwcDst[3] = g_szrtBase64ValToChar[u8C & 0x3f];

        /* advance */
        cwcBuf -= 4;
        pwcDst += 4;
        cbData -= 3;
        pbSrc  += 3;

        /* deal out end-of-line */
        if (cwcBuf == cwcLineFeed && cbData && cchEol)
        {
            if (cwcBuf < cchEol + 1)
                return VERR_BUFFER_OVERFLOW;
            cwcBuf -= cchEol;
            *pwcDst++ = chEol0;
            if (chEol1)
                *pwcDst++ = chEol1;
            cwcLineFeed = cwcBuf - RTBASE64_LINE_LEN;
        }
    }

    /*
     * Deal with the odd bytes and string termination.
     */
    if (cbData)
    {
        if (cwcBuf < 4 + 1)
            return VERR_BUFFER_OVERFLOW;
        switch (cbData)
        {
            case 1:
                u8A = pbSrc[0];
                pwcDst[0] = g_szrtBase64ValToChar[u8A >> 2];
                pwcDst[1] = g_szrtBase64ValToChar[(u8A << 4) & 0x3f];
                pwcDst[2] = '=';
                pwcDst[3] = '=';
                break;
            case 2:
                u8A = pbSrc[0];
                pwcDst[0] = g_szrtBase64ValToChar[u8A >> 2];
                u8B = pbSrc[1];
                pwcDst[1] = g_szrtBase64ValToChar[((u8A << 4) & 0x3f) | (u8B >> 4)];
                pwcDst[2] = g_szrtBase64ValToChar[(u8B << 2) & 0x3f];
                pwcDst[3] = '=';
                break;
        }
        pwcDst += 4;
    }

    *pwcDst = '\0';

    if (pcwcActual)
        *pcwcActual = pwcDst - pwszBuf;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTBase64EncodeUtf16Ex);

