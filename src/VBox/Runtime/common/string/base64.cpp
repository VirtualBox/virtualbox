/* $Id: base64.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Base64, MIME content transfer encoding.
 *
 * @note The base64-utf16.cpp file must be diffable with this one.
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
#include <iprt/ctype.h>
#include <iprt/string.h>
#ifdef RT_STRICT
# include <iprt/asm.h>
#endif

#include "base64.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Base64 character to value. (RFC 2045)
 * ASSUMES ASCII / UTF-8. */
DECL_HIDDEN_CONST(const uint8_t)    g_au8rtBase64CharToVal[256] =
{
    0xfe, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xc0, 0xc0, 0xc0,   0xc0, 0xc0, 0xff, 0xff, /* 0x00..0x0f */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0x10..0x1f */
    0xc0, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff,   62,   0xff, 0xff, 0xff,   63, /* 0x20..0x2f */
      52,   53,   54,   55,     56,   57,   58,   59,     60,   61, 0xff, 0xff,   0xff, 0xe0, 0xff, 0xff, /* 0x30..0x3f */
    0xff,    0,    1,    2,      3,    4,    5,    6,      7,    8,    9,   10,     11,   12,   13,   14, /* 0x40..0x4f */
      15,   16,   17,   18,     19,   20,   21,   22,     23,   24,   25, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0x50..0x5f */
    0xff,   26,   27,   28,     29,   30,   31,   32,     33,   34,   35,   36,     37,   38,   39,   40, /* 0x60..0x6f */
      41,   42,   43,   44,     45,   46,   47,   48,     49,   50,   51, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0x70..0x7f */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0x80..0x8f */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0x90..0x9f */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0xa0..0xaf */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0xb0..0xbf */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0xc0..0xcf */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0xd0..0xdf */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff, /* 0xe0..0xef */
    0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff,   0xff, 0xff, 0xff, 0xff  /* 0xf0..0xff */
};

/** Value to Base64 character. (RFC 2045) */
DECL_HIDDEN_CONST(const char)   g_szrtBase64ValToChar[64+1] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/** The end-of-line lengths (indexed by style flag value). */
DECL_HIDDEN_CONST(const size_t) g_acchrtBase64EolStyles[RTBASE64_FLAGS_EOL_STYLE_MASK + 1] =
{
    /*[RTBASE64_FLAGS_EOL_NATIVE    ]:*/ RTBASE64_EOL_SIZE,
    /*[RTBASE64_FLAGS_NO_LINE_BREAKS]:*/ 0,
    /*[RTBASE64_FLAGS_EOL_LF        ]:*/ 1,
    /*[RTBASE64_FLAGS_EOL_CRLF      ]:*/ 2
};

/** The end-of-line characters (zero, one or two). */
DECL_HIDDEN_CONST(const char)   g_aachrtBase64EolStyles[RTBASE64_FLAGS_EOL_STYLE_MASK + 1][2] =
{
    /*[RTBASE64_FLAGS_EOL_NATIVE    ]:*/ { RTBASE64_EOL_SIZE == 1 ? '\n' : '\r', RTBASE64_EOL_SIZE == 1 ? '\0' : '\n', },
    /*[RTBASE64_FLAGS_NO_LINE_BREAKS]:*/ { '\0', '\0' },
    /*[RTBASE64_FLAGS_EOL_LF        ]:*/ { '\n', '\0' },
    /*[RTBASE64_FLAGS_EOL_CRLF      ]:*/ { '\r', '\n' },
};



#ifdef RT_STRICT
/**
 * Perform table sanity checks on the first call.
 */
DECLHIDDEN(void) rtBase64Sanity(void)
{
    static bool s_fSane = false;
    if (RT_UNLIKELY(!s_fSane))
    {
        for (unsigned i = 0; i < 64; i++)
        {
            unsigned ch = g_szrtBase64ValToChar[i];
            Assert(ch);
            Assert(g_au8rtBase64CharToVal[ch] == i);
        }

        for (unsigned i = 0; i < 256; i++)
        {
            uint8_t u8 = g_au8rtBase64CharToVal[i];
            Assert(   (     u8 == BASE64_INVALID
                       &&   !RT_C_IS_ALNUM(i)
                       &&   !RT_C_IS_SPACE(i))
                   || (     u8 == BASE64_PAD
                       &&   i  == '=')
                   || (     u8 == BASE64_SPACE
                       &&   RT_C_IS_SPACE(i))
                   || (     u8 < 64
                       &&   (unsigned)g_szrtBase64ValToChar[u8] == i)
                   || (     u8 == BASE64_NULL
                       &&   i  == 0) );
        }
        ASMAtomicWriteBool(&s_fSane, true);
    }
}
#endif /* RT_STRICT */



/** Fetched the next character in the string and translates it. */
DECL_FORCE_INLINE(uint8_t) rtBase64TranslateNext(const char *pszString, size_t cchStringMax)
{
    AssertCompile(sizeof(unsigned char) == sizeof(uint8_t));
    if (cchStringMax > 0)
        return g_au8rtBase64CharToVal[(unsigned char)*pszString];
    return BASE64_NULL;
}


/*
 * Mostly the same as RTBase64DecodedUtf16SizeEx, except for the simpler
 * character type.  Fixes must be applied to both copies of the code.
 */
RTDECL(ssize_t) RTBase64DecodedSizeEx(const char *pszString, size_t cchStringMax, char **ppszEnd)
{
#ifdef RT_STRICT
    rtBase64Sanity();
#endif

    /*
     * Walk the string until a non-encoded or non-space character is encountered.
     */
    uint32_t    c6Bits = 0;
    uint8_t     u8;

    while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) != BASE64_NULL)
    {
        if (u8 < 64)
            c6Bits++;
        else if (RT_UNLIKELY(u8 != BASE64_SPACE))
            break;

        /* advance */
        pszString++;
        cchStringMax--;
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
        pszString++;
        cchStringMax--;
        while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) != BASE64_NULL)
        {
            if (u8 != BASE64_SPACE)
            {
                if (u8 != BASE64_PAD)
                    break;
                c6Bits++;
                cbPad++;
            }
            pszString++;
            cchStringMax--;
        }
        if (cbPad >= 3)
            return -1;
    }

    /*
     * Invalid char and no where to indicate where the
     * Base64 text ends? Return failure.
     */
    if (   u8 == BASE64_INVALID
        && !ppszEnd)
        return -1;

    /*
     * Recalc 6-bit to 8-bit and adjust for padding.
     */
    if (ppszEnd)
        *ppszEnd = (char *)pszString;
    return rtBase64DecodedSizeRecalc(c6Bits, cbPad);
}
RT_EXPORT_SYMBOL(RTBase64DecodedSizeEx);


RTDECL(ssize_t) RTBase64DecodedSize(const char *pszString, char **ppszEnd)
{
    return RTBase64DecodedSizeEx(pszString, RTSTR_MAX, ppszEnd);
}
RT_EXPORT_SYMBOL(RTBase64DecodedSize);


RTDECL(int) RTBase64DecodeEx(const char *pszString, size_t cchStringMax, void *pvData, size_t cbData,
                             size_t *pcbActual, char **ppszEnd)
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
        while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) == BASE64_SPACE)
            pszString++, cchStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 0;
            break;
        }
        u8Trio[0] = u8 << 2;
        pszString++;
        cchStringMax--;

        /* The second 6-bit group. */
        while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) == BASE64_SPACE)
            pszString++, cchStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 1;
            break;
        }
        u8Trio[0] |= u8 >> 4;
        u8Trio[1]  = u8 << 4;
        pszString++;
        cchStringMax--;

        /* The third 6-bit group. */
        u8 = BASE64_INVALID;
        while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) == BASE64_SPACE)
            pszString++, cchStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 2;
            break;
        }
        u8Trio[1] |= u8 >> 2;
        u8Trio[2]  = u8 << 6;
        pszString++;
        cchStringMax--;

        /* The fourth 6-bit group. */
        u8 = BASE64_INVALID;
        while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) == BASE64_SPACE)
            pszString++, cchStringMax--;
        if (u8 >= 64)
        {
            c6Bits = 3;
            break;
        }
        u8Trio[2] |= u8;
        pszString++;
        cchStringMax--;

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
        pszString++;
        cchStringMax--;
        while ((u8 = rtBase64TranslateNext(pszString, cchStringMax)) != BASE64_NULL)
        {
            if (u8 != BASE64_SPACE)
            {
                if (u8 != BASE64_PAD)
                    break;
                cbPad++;
            }
            pszString++;
            cchStringMax--;
        }
        if (cbPad >= 3)
            return VERR_INVALID_BASE64_ENCODING;
    }

    /*
     * Invalid char and no where to indicate where the
     * Base64 text ends? Return failure.
     */
    if (   u8 == BASE64_INVALID
        && !ppszEnd)
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
    if (ppszEnd)
        *ppszEnd = (char *)pszString;
    if (pcbActual)
        *pcbActual = pbData - (uint8_t *)pvData;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTBase64DecodeEx);


RTDECL(int) RTBase64Decode(const char *pszString, void *pvData, size_t cbData, size_t *pcbActual, char **ppszEnd)
{
    return RTBase64DecodeEx(pszString, RTSTR_MAX, pvData, cbData, pcbActual, ppszEnd);
}
RT_EXPORT_SYMBOL(RTBase64Decode);


RTDECL(size_t) RTBase64EncodedLength(size_t cbData)
{
    return RTBase64EncodedLengthEx(cbData, 0);
}
RT_EXPORT_SYMBOL(RTBase64EncodedLength);


RTDECL(size_t) RTBase64EncodedLengthEx(size_t cbData, uint32_t fFlags)
{
    size_t const cchEol = g_acchrtBase64EolStyles[fFlags & RTBASE64_FLAGS_EOL_STYLE_MASK];

    if (cbData * 8 / 8 != cbData)
    {
        AssertReturn(sizeof(size_t) == sizeof(uint64_t), ~(size_t)0);
        uint64_t cch = cbData * (uint64_t)8;
        while (cch % 24)
            cch += 8;
        cch /= 6;
        cch += ((cch - 1) / RTBASE64_LINE_LEN) * cchEol;
        return cch;
    }

    size_t cch = cbData * 8;
    while (cch % 24)
        cch += 8;
    cch /= 6;
    cch += ((cch - 1) / RTBASE64_LINE_LEN) * cchEol;
    return cch;
}
RT_EXPORT_SYMBOL(RTBase64EncodedLengthEx);


RTDECL(int) RTBase64Encode(const void *pvData, size_t cbData, char *pszBuf, size_t cbBuf, size_t *pcchActual)
{
    return RTBase64EncodeEx(pvData, cbData, 0, pszBuf, cbBuf, pcchActual);
}
RT_EXPORT_SYMBOL(RTBase64Encode);


/*
 * Please note that RTBase64EncodeUtf16Ex contains an almost exact copy of
 * this code, just using different output character type and variable prefixes.
 * So, all fixes must be applied to both versions of the code.
 */
RTDECL(int) RTBase64EncodeEx(const void *pvData, size_t cbData, uint32_t fFlags,
                             char *pszBuf, size_t cbBuf, size_t *pcchActual)
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
    size_t          cbLineFeed = cchEol ? cbBuf - RTBASE64_LINE_LEN : ~(size_t)0;
    const uint8_t  *pbSrc      = (const uint8_t *)pvData;
    char           *pchDst     = pszBuf;
    while (cbData >= 3)
    {
        if (cbBuf < 4 + 1)
            return VERR_BUFFER_OVERFLOW;

        /* encode */
        u8A = pbSrc[0];
        pchDst[0] = g_szrtBase64ValToChar[u8A >> 2];
        u8B = pbSrc[1];
        pchDst[1] = g_szrtBase64ValToChar[((u8A << 4) & 0x3f) | (u8B >> 4)];
        u8C = pbSrc[2];
        pchDst[2] = g_szrtBase64ValToChar[((u8B << 2) & 0x3f) | (u8C >> 6)];
        pchDst[3] = g_szrtBase64ValToChar[u8C & 0x3f];

        /* advance */
        cbBuf  -= 4;
        pchDst += 4;
        cbData -= 3;
        pbSrc  += 3;

        /* deal out end-of-line */
        if (cbBuf == cbLineFeed && cbData && cchEol)
        {
            if (cbBuf < cchEol + 1)
                return VERR_BUFFER_OVERFLOW;
            cbBuf -= cchEol;
            *pchDst++ = chEol0;
            if (chEol1)
                *pchDst++ = chEol1;
            cbLineFeed = cbBuf - RTBASE64_LINE_LEN;
        }
    }

    /*
     * Deal with the odd bytes and string termination.
     */
    if (cbData)
    {
        if (cbBuf < 4 + 1)
            return VERR_BUFFER_OVERFLOW;
        switch (cbData)
        {
            case 1:
                u8A = pbSrc[0];
                pchDst[0] = g_szrtBase64ValToChar[u8A >> 2];
                pchDst[1] = g_szrtBase64ValToChar[(u8A << 4) & 0x3f];
                pchDst[2] = '=';
                pchDst[3] = '=';
                break;
            case 2:
                u8A = pbSrc[0];
                pchDst[0] = g_szrtBase64ValToChar[u8A >> 2];
                u8B = pbSrc[1];
                pchDst[1] = g_szrtBase64ValToChar[((u8A << 4) & 0x3f) | (u8B >> 4)];
                pchDst[2] = g_szrtBase64ValToChar[(u8B << 2) & 0x3f];
                pchDst[3] = '=';
                break;
        }
        pchDst += 4;
    }

    *pchDst = '\0';

    if (pcchActual)
        *pcchActual = pchDst - pszBuf;
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTBase64EncodeEx);

