/* $Id: utf-16.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - UTF-16.
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
#include <iprt/utf16.h>
#include "internal/iprt.h"

#include <iprt/uni.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include "internal/string.h"


/**
 * Get get length in code points of an UTF-16 encoded string, validating the
 * string while doing so.
 *
 * @returns IPRT status code.
 * @param   pwsz            Pointer to the UTF-16 string.
 * @param   cwc             The max length of the string in UTF-16 units.  Use
 *                          RTSTR_MAX if all of the string is to be examined.
 * @param   pcuc            Where to store the length in unicode code points.
 * @param   pcwcActual      Where to store the actual size of the UTF-16 string
 *                          on success. Optional.
 */
static int rtUtf16Length(PCRTUTF16 pwsz, size_t cwc, size_t *pcuc, size_t *pcwcActual)
{
    PCRTUTF16 pwszStart   = pwsz;
    size_t    cCodePoints = 0;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz;
        if (!wc)
            break;
        if (wc < 0xd800 || wc > 0xdfff)
        {
            cCodePoints++;
            pwsz++;
            cwc--;
        }
        /* Surrogate pair: */
        else if (wc >= 0xdc00)
        {
            RTStrAssertMsgFailed(("Lone UTF-16 trail surrogate: %#x (%.*Rhxs)\n", wc, RT_MIN(cwc * 2, 10), pwsz));
            return VERR_INVALID_UTF16_ENCODING;
        }
        else if (cwc < 2)
        {
            RTStrAssertMsgFailed(("Lone UTF-16 lead surrogate: %#x\n", wc));
            return VERR_INVALID_UTF16_ENCODING;
        }
        else
        {
            RTUTF16 wcTrail = pwsz[1];
            if (wcTrail < 0xdc00 || wcTrail > 0xdfff)
            {
                RTStrAssertMsgFailed(("Invalid UTF-16 trail surrogate: %#x (lead %#x)\n", wcTrail, wc));
                return VERR_INVALID_UTF16_ENCODING;
            }

            cCodePoints++;
            pwsz += 2;
            cwc -= 2;
        }
    }

    /* done */
    *pcuc = cCodePoints;
    if (pcwcActual)
        *pcwcActual = pwsz - pwszStart;
    return VINF_SUCCESS;
}


RTDECL(PRTUTF16) RTUtf16AllocTag(size_t cb, const char *pszTag)
{
    if (cb > sizeof(RTUTF16))
        cb = RT_ALIGN_Z(cb, sizeof(RTUTF16));
    else
        cb = sizeof(RTUTF16);
    PRTUTF16 pwsz = (PRTUTF16)RTMemAllocTag(cb, pszTag);
    if (pwsz)
        *pwsz = '\0';
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16AllocTag);


RTDECL(int) RTUtf16ReallocTag(PRTUTF16 *ppwsz, size_t cbNew, const char *pszTag)
{
    PRTUTF16 pwszOld = *ppwsz;
    cbNew = RT_ALIGN_Z(cbNew, sizeof(RTUTF16));
    if (!cbNew)
    {
        RTMemFree(pwszOld);
        *ppwsz = NULL;
    }
    else if (pwszOld)
    {
        PRTUTF16 pwszNew = (PRTUTF16)RTMemReallocTag(pwszOld, cbNew, pszTag);
        if (!pwszNew)
            return VERR_NO_STR_MEMORY;
        pwszNew[cbNew / sizeof(RTUTF16) - 1] = '\0';
        *ppwsz = pwszNew;
    }
    else
    {
        PRTUTF16 pwszNew = (PRTUTF16)RTMemAllocTag(cbNew, pszTag);
        if (!pwszNew)
            return VERR_NO_UTF16_MEMORY;
        pwszNew[0] = '\0';
        pwszNew[cbNew / sizeof(RTUTF16) - 1] = '\0';
        *ppwsz = pwszNew;
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTUtf16ReallocTag);


RTDECL(void)  RTUtf16Free(PRTUTF16 pwszString)
{
    if (pwszString)
        RTMemTmpFree(pwszString);
}
RT_EXPORT_SYMBOL(RTUtf16Free);


RTDECL(PRTUTF16) RTUtf16DupTag(PCRTUTF16 pwszString, const char *pszTag)
{
    Assert(pwszString);
    size_t cb = (RTUtf16Len(pwszString) + 1) * sizeof(RTUTF16);
    PRTUTF16 pwsz = (PRTUTF16)RTMemAllocTag(cb, pszTag);
    if (pwsz)
        memcpy(pwsz, pwszString, cb);
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16DupTag);


RTDECL(int) RTUtf16DupExTag(PRTUTF16 *ppwszString, PCRTUTF16 pwszString, size_t cwcExtra, const char *pszTag)
{
    Assert(pwszString);
    size_t cb = (RTUtf16Len(pwszString) + 1) * sizeof(RTUTF16);
    PRTUTF16 pwsz = (PRTUTF16)RTMemAllocTag(cb + cwcExtra * sizeof(RTUTF16), pszTag);
    if (pwsz)
    {
        memcpy(pwsz, pwszString, cb);
        *ppwszString = pwsz;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}
RT_EXPORT_SYMBOL(RTUtf16DupExTag);


RTDECL(size_t) RTUtf16Len(PCRTUTF16 pwszString)
{
    if (!pwszString)
        return 0;

    PCRTUTF16 pwsz = pwszString;
    while (*pwsz)
        pwsz++;
    return pwsz - pwszString;
}
RT_EXPORT_SYMBOL(RTUtf16Len);


RTDECL(int) RTUtf16Cmp(PCRTUTF16 pwsz1, PCRTUTF16 pwsz2)
{
    if (pwsz1 == pwsz2)
        return 0;
    if (!pwsz1)
        return -1;
    if (!pwsz2)
        return 1;

    for (;;)
    {
        RTUTF16 wcs = *pwsz1;
        int     iDiff = wcs - *pwsz2;
        if (iDiff || !wcs)
            return iDiff;
        pwsz1++;
        pwsz2++;
    }
}
RT_EXPORT_SYMBOL(RTUtf16Cmp);


RTDECL(int) RTUtf16CmpUtf8(PCRTUTF16 pwsz1, const char *psz2)
{
    /*
     * NULL and empty strings are all the same.
     */
    if (!pwsz1)
        return !psz2 || !*psz2 ? 0 : -1;
    if (!psz2)
        return !*pwsz1         ? 0 :  1;

    /*
     * Compare with a UTF-8 string by enumerating them char by char.
     */
    for (;;)
    {
        RTUNICP uc1;
        int rc = RTUtf16GetCpEx(&pwsz1, &uc1);
        AssertRCReturn(rc, 1);

        RTUNICP uc2;
        rc = RTStrGetCpEx(&psz2, &uc2);
        AssertRCReturn(rc, -1);
        if (uc1 == uc2)
        {
            if (uc1)
                continue;
            return 0;
        }
        return uc1 < uc2 ? -1 : 1;
    }
}
RT_EXPORT_SYMBOL(RTUtf16CmpUtf8);


RTDECL(int) RTUtf16ValidateEncoding(PCRTUTF16 pwsz)
{
    return RTUtf16ValidateEncodingEx(pwsz, RTSTR_MAX, 0);
}
RT_EXPORT_SYMBOL(RTUtf16ValidateEncoding);


RTDECL(int) RTUtf16ValidateEncodingEx(PCRTUTF16 pwsz, size_t cwc, uint32_t fFlags)
{
    AssertReturn(!(fFlags & ~(RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH)),
                 VERR_INVALID_PARAMETER);
    AssertPtr(pwsz);

    /*
     * Use rtUtf16Length for the job.
     */
    size_t cwcActual = 0; /* Shut up cc1plus. */
    size_t cCpsIgnored;
    int rc = rtUtf16Length(pwsz, cwc, &cCpsIgnored, &cwcActual);
    if (RT_SUCCESS(rc))
    {
        if (fFlags & RTSTR_VALIDATE_ENCODING_EXACT_LENGTH)
        {
            if (fFlags & RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)
                cwcActual++;
            if (cwcActual == cwc)
                rc = VINF_SUCCESS;
            else if (cwcActual < cwc)
                rc = VERR_BUFFER_UNDERFLOW;
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
        else if (    (fFlags & RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)
                 &&  cwcActual >= cwc)
            rc = VERR_BUFFER_OVERFLOW;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16ValidateEncodingEx);


RTDECL(bool) RTUtf16IsValidEncoding(PCRTUTF16 pwsz)
{
    int rc = RTUtf16ValidateEncodingEx(pwsz, RTSTR_MAX, 0);
    return RT_SUCCESS(rc);
}
RT_EXPORT_SYMBOL(RTUtf16IsValidEncoding);


/**
 * Helper for RTUtf16PurgeComplementSet.
 *
 * @returns true if @a Cp is valid, false if not.
 * @param   Cp              The code point to validate.
 * @param   puszValidPairs  Pair of valid code point sets.
 * @param   cValidPairs     Number of pairs.
 */
DECLINLINE(bool) rtUtf16PurgeIsInSet(RTUNICP Cp, PCRTUNICP puszValidPairs, uint32_t cValidPairs)
{
    while (cValidPairs-- > 0)
    {
        if (   Cp >= puszValidPairs[0]
            && Cp <= puszValidPairs[1])
            return true;
        puszValidPairs += 2;
    }
    return false;
}


RTDECL(ssize_t) RTUtf16PurgeComplementSet(PRTUTF16 pwsz, PCRTUNICP puszValidPairs, char chReplacement)
{
    AssertReturn(chReplacement && (unsigned)chReplacement < 128, -1);

    /*
     * Calc valid pairs and check that we've got an even number.
     */
    uint32_t cValidPairs = 0;
    while (puszValidPairs[cValidPairs * 2])
    {
        AssertReturn(puszValidPairs[cValidPairs * 2 + 1], -1);
        AssertMsg(puszValidPairs[cValidPairs * 2] <= puszValidPairs[cValidPairs * 2 + 1],
                  ("%#x vs %#x\n", puszValidPairs[cValidPairs * 2], puszValidPairs[cValidPairs * 2 + 1]));
        cValidPairs++;
    }

    /*
     * Do the replacing.
     */
    ssize_t cReplacements = 0;
    for (;;)
    {
        PRTUTF16 pwszCur = pwsz;
        RTUNICP Cp;
        int rc = RTUtf16GetCpEx((PCRTUTF16 *)&pwsz, &Cp);
        if (RT_SUCCESS(rc))
        {
            if (Cp)
            {
                if (!rtUtf16PurgeIsInSet(Cp, puszValidPairs, cValidPairs))
                {
                    for (; pwszCur != pwsz; ++pwszCur)
                        *pwszCur = chReplacement;
                    ++cReplacements;
                }
            }
            else
                break;
        }
        else
            return -1;
    }
    return cReplacements;
}
RT_EXPORT_SYMBOL(RTUtf16PurgeComplementSet);


/**
 * Validate the UTF-16BE encoding and calculates the length of an UTF-8
 * encoding.
 *
 * @returns iprt status code.
 * @param   pwsz        The UTF-16BE string.
 * @param   cwc         The max length of the UTF-16BE string to consider.
 * @param   pcch        Where to store the length (excluding '\\0') of the UTF-8 string. (cch == cb, btw)
 *
 * @note    rtUtf16LittleCalcUtf8Length | s/RT_LE2H_U16/RT_BE2H_U16/g
 */
static int rtUtf16BigCalcUtf8Length(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    int     rc = VINF_SUCCESS;
    size_t  cch = 0;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz++; cwc--;
        if (!wc)
            break;
        wc = RT_BE2H_U16(wc);
        if (wc < 0xd800 || wc > 0xdfff)
        {
            if (wc < 0x80)
                cch++;
            else if (wc < 0x800)
                cch += 2;
            else if (wc < 0xfffe)
                cch += 3;
            else
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
            wc = RT_BE2H_U16(wc);
            if (wc < 0xdc00 || wc > 0xdfff)
            {
                RTStrAssertMsgFailed(("Wrong 2nd char in surrogate! wc=%#x\n", wc));
                rc = VERR_INVALID_UTF16_ENCODING;
                break;
            }
            cch += 4;
        }
    }


    /* done */
    *pcch = cch;
    return rc;
}


/**
 * Validate the UTF-16LE encoding and calculates the length of an UTF-8
 * encoding.
 *
 * @returns iprt status code.
 * @param   pwsz        The UTF-16LE string.
 * @param   cwc         The max length of the UTF-16LE string to consider.
 * @param   pcch        Where to store the length (excluding '\\0') of the UTF-8 string. (cch == cb, btw)
 *
 * @note    rtUtf16BigCalcUtf8Length | s/RT_BE2H_U16/RT_LE2H_U16/g
 */
static int rtUtf16LittleCalcUtf8Length(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    int     rc = VINF_SUCCESS;
    size_t  cch = 0;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz++; cwc--;
        if (!wc)
            break;
        wc = RT_LE2H_U16(wc);
        if (wc < 0xd800 || wc > 0xdfff)
        {
            if (wc < 0x80)
                cch++;
            else if (wc < 0x800)
                cch += 2;
            else if (wc < 0xfffe)
                cch += 3;
            else
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
            wc = RT_LE2H_U16(wc);
            if (wc < 0xdc00 || wc > 0xdfff)
            {
                RTStrAssertMsgFailed(("Wrong 2nd char in surrogate! wc=%#x\n", wc));
                rc = VERR_INVALID_UTF16_ENCODING;
                break;
            }
            cch += 4;
        }
    }


    /* done */
    *pcch = cch;
    return rc;
}


/**
 * Recodes an valid UTF-16BE string as UTF-8.
 *
 * @returns iprt status code.
 * @param   pwsz        The UTF-16BE string.
 * @param   cwc         The number of RTUTF16 characters to process from pwsz. The recoding
 *                      will stop when cwc or '\\0' is reached.
 * @param   psz         Where to store the UTF-8 string.
 * @param   cch         The size of the UTF-8 buffer, excluding the terminator.
 * @param   pcch        Where to store the number of octets actually encoded.
 *
 * @note    rtUtf16LittleRecodeAsUtf8 == s/RT_BE2H_U16/RT_LE2H_U16/g
 */
static int rtUtf16BigRecodeAsUtf8(PCRTUTF16 pwsz, size_t cwc, char *psz, size_t cch, size_t *pcch)
{
    unsigned char  *pwch = (unsigned char *)psz;
    int             rc = VINF_SUCCESS;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz++; cwc--;
        if (!wc)
            break;
        wc = RT_BE2H_U16(wc);
        if (wc < 0xd800 || wc > 0xdfff)
        {
            if (wc < 0x80)
            {
                if (RT_UNLIKELY(cch < 1))
                {
                    RTStrAssertMsgFailed(("Buffer overflow! 1\n"));
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                cch--;
                *pwch++ = (unsigned char)wc;
            }
            else if (wc < 0x800)
            {
                if (RT_UNLIKELY(cch < 2))
                {
                    RTStrAssertMsgFailed(("Buffer overflow! 2\n"));
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                cch -= 2;
                *pwch++ = 0xc0 | (wc >> 6);
                *pwch++ = 0x80 | (wc & 0x3f);
            }
            else if (wc < 0xfffe)
            {
                if (RT_UNLIKELY(cch < 3))
                {
                    RTStrAssertMsgFailed(("Buffer overflow! 3\n"));
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                cch -= 3;
                *pwch++ = 0xe0 | (wc >> 12);
                *pwch++ = 0x80 | ((wc >> 6) & 0x3f);
                *pwch++ = 0x80 | (wc & 0x3f);
            }
            else
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
            wc2 = RT_BE2H_U16(wc2);
            if (wc2 < 0xdc00 || wc2 > 0xdfff)
            {
                RTStrAssertMsgFailed(("Wrong 2nd char in surrogate! wc=%#x\n", wc));
                rc = VERR_INVALID_UTF16_ENCODING;
                break;
            }
            uint32_t CodePoint = 0x10000
                               + (  ((wc & 0x3ff) << 10)
                                  | (wc2 & 0x3ff));
            if (RT_UNLIKELY(cch < 4))
            {
                RTStrAssertMsgFailed(("Buffer overflow! 4\n"));
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            cch -= 4;
            *pwch++ = 0xf0 | (CodePoint >> 18);
            *pwch++ = 0x80 | ((CodePoint >> 12) & 0x3f);
            *pwch++ = 0x80 | ((CodePoint >>  6) & 0x3f);
            *pwch++ = 0x80 | (CodePoint & 0x3f);
        }
    }

    /* done */
    *pwch = '\0';
    *pcch = (char *)pwch - psz;
    return rc;
}


/**
 * Recodes an valid UTF-16LE string as UTF-8.
 *
 * @returns iprt status code.
 * @param   pwsz        The UTF-16LE string.
 * @param   cwc         The number of RTUTF16 characters to process from pwsz. The recoding
 *                      will stop when cwc or '\\0' is reached.
 * @param   psz         Where to store the UTF-8 string.
 * @param   cch         The size of the UTF-8 buffer, excluding the terminator.
 * @param   pcch        Where to store the number of octets actually encoded.
 *
 * @note    rtUtf16LittleRecodeAsUtf8 == s/RT_LE2H_U16/RT_GE2H_U16/g
 */
static int rtUtf16LittleRecodeAsUtf8(PCRTUTF16 pwsz, size_t cwc, char *psz, size_t cch, size_t *pcch)
{
    unsigned char  *pwch = (unsigned char *)psz;
    int             rc = VINF_SUCCESS;
    while (cwc > 0)
    {
        RTUTF16 wc = *pwsz++; cwc--;
        if (!wc)
            break;
        wc = RT_LE2H_U16(wc);
        if (wc < 0xd800 || wc > 0xdfff)
        {
            if (wc < 0x80)
            {
                if (RT_UNLIKELY(cch < 1))
                {
                    RTStrAssertMsgFailed(("Buffer overflow! 1\n"));
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                cch--;
                *pwch++ = (unsigned char)wc;
            }
            else if (wc < 0x800)
            {
                if (RT_UNLIKELY(cch < 2))
                {
                    RTStrAssertMsgFailed(("Buffer overflow! 2\n"));
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                cch -= 2;
                *pwch++ = 0xc0 | (wc >> 6);
                *pwch++ = 0x80 | (wc & 0x3f);
            }
            else if (wc < 0xfffe)
            {
                if (RT_UNLIKELY(cch < 3))
                {
                    RTStrAssertMsgFailed(("Buffer overflow! 3\n"));
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                cch -= 3;
                *pwch++ = 0xe0 | (wc >> 12);
                *pwch++ = 0x80 | ((wc >> 6) & 0x3f);
                *pwch++ = 0x80 | (wc & 0x3f);
            }
            else
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
            wc2 = RT_LE2H_U16(wc2);
            if (wc2 < 0xdc00 || wc2 > 0xdfff)
            {
                RTStrAssertMsgFailed(("Wrong 2nd char in surrogate! wc=%#x\n", wc));
                rc = VERR_INVALID_UTF16_ENCODING;
                break;
            }
            uint32_t CodePoint = 0x10000
                               + (  ((wc & 0x3ff) << 10)
                                  | (wc2 & 0x3ff));
            if (RT_UNLIKELY(cch < 4))
            {
                RTStrAssertMsgFailed(("Buffer overflow! 4\n"));
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            cch -= 4;
            *pwch++ = 0xf0 | (CodePoint >> 18);
            *pwch++ = 0x80 | ((CodePoint >> 12) & 0x3f);
            *pwch++ = 0x80 | ((CodePoint >>  6) & 0x3f);
            *pwch++ = 0x80 | (CodePoint & 0x3f);
        }
    }

    /* done */
    *pwch = '\0';
    *pcch = (char *)pwch - psz;
    return rc;
}



RTDECL(int)  RTUtf16ToUtf8Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag)
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
#ifdef RT_BIG_ENDIAN
    int rc = rtUtf16BigCalcUtf8Length(pwszString, RTSTR_MAX, &cch);
#else
    int rc = rtUtf16LittleCalcUtf8Length(pwszString, RTSTR_MAX, &cch);
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer and recode it.
         */
        char *pszResult = (char *)RTMemAllocTag(cch + 1, pszTag);
        if (pszResult)
        {
#ifdef RT_BIG_ENDIAN
            rc = rtUtf16BigRecodeAsUtf8(pwszString, RTSTR_MAX, pszResult, cch, &cch);
#else
            rc = rtUtf16LittleRecodeAsUtf8(pwszString, RTSTR_MAX, pszResult, cch, &cch);
#endif
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
RT_EXPORT_SYMBOL(RTUtf16ToUtf8Tag);


RTDECL(int)  RTUtf16BigToUtf8Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag)
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
    int rc = rtUtf16BigCalcUtf8Length(pwszString, RTSTR_MAX, &cch);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer and recode it.
         */
        char *pszResult = (char *)RTMemAllocTag(cch + 1, pszTag);
        if (pszResult)
        {
            rc = rtUtf16BigRecodeAsUtf8(pwszString, RTSTR_MAX, pszResult, cch, &cch);
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
RT_EXPORT_SYMBOL(RTUtf16BigToUtf8Tag);


RTDECL(int)  RTUtf16LittleToUtf8Tag(PCRTUTF16 pwszString, char **ppszString, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(ppszString);
    AssertPtr(pwszString);
    *ppszString = NULL;

    /*
     * Validate the UTF-16LE string and calculate the length of the UTF-8 encoding of it.
     */
    size_t cch;
    int rc = rtUtf16LittleCalcUtf8Length(pwszString, RTSTR_MAX, &cch);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate buffer and recode it.
         */
        char *pszResult = (char *)RTMemAllocTag(cch + 1, pszTag);
        if (pszResult)
        {
            rc = rtUtf16LittleRecodeAsUtf8(pwszString, RTSTR_MAX, pszResult, cch, &cch);
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
RT_EXPORT_SYMBOL(RTUtf16LittleToUtf8Tag);


RTDECL(int)  RTUtf16ToUtf8ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(pwszString);
    AssertPtr(ppsz);
    AssertPtrNull(pcch);

    /*
     * Validate the UTF-16 string and calculate the length of the UTF-8 encoding of it.
     */
    size_t cchResult;
#ifdef RT_BIG_ENDIAN
    int rc = rtUtf16BigCalcUtf8Length(pwszString, cwcString, &cchResult);
#else
    int rc = rtUtf16LittleCalcUtf8Length(pwszString, cwcString, &cchResult);
#endif
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
            if (RT_UNLIKELY(cch <= cchResult))
                return VERR_BUFFER_OVERFLOW;
            pszResult = *ppsz;
        }
        else
        {
            *ppsz = NULL;
            fShouldFree = true;
            cch = RT_MAX(cch, cchResult + 1);
            pszResult = (char *)RTStrAllocTag(cch, pszTag);
        }
        if (pszResult)
        {
#ifdef RT_BIG_ENDIAN
            rc = rtUtf16BigRecodeAsUtf8(pwszString, cwcString, pszResult, cch - 1, &cch);
#else
            rc = rtUtf16LittleRecodeAsUtf8(pwszString, cwcString, pszResult, cch - 1, &cch);
#endif
            if (RT_SUCCESS(rc))
            {
                *ppsz = pszResult;
                return rc;
            }

            if (fShouldFree)
                RTStrFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16ToUtf8ExTag);


RTDECL(int)  RTUtf16BigToUtf8ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch, const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(pwszString);
    AssertPtr(ppsz);
    AssertPtrNull(pcch);

    /*
     * Validate the UTF-16BE string and calculate the length of the UTF-8 encoding of it.
     */
    size_t cchResult;
    int rc = rtUtf16BigCalcUtf8Length(pwszString, cwcString, &cchResult);
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
            if (RT_UNLIKELY(cch <= cchResult))
                return VERR_BUFFER_OVERFLOW;
            pszResult = *ppsz;
        }
        else
        {
            *ppsz = NULL;
            fShouldFree = true;
            cch = RT_MAX(cch, cchResult + 1);
            pszResult = (char *)RTStrAllocTag(cch, pszTag);
        }
        if (pszResult)
        {
            rc = rtUtf16BigRecodeAsUtf8(pwszString, cwcString, pszResult, cch - 1, &cch);
            if (RT_SUCCESS(rc))
            {
                *ppsz = pszResult;
                return rc;
            }

            if (fShouldFree)
                RTStrFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16BigToUtf8ExTag);


RTDECL(int)  RTUtf16LittleToUtf8ExTag(PCRTUTF16 pwszString, size_t cwcString, char **ppsz, size_t cch, size_t *pcch,
                                      const char *pszTag)
{
    /*
     * Validate input.
     */
    AssertPtr(pwszString);
    AssertPtr(ppsz);
    AssertPtrNull(pcch);

    /*
     * Validate the UTF-16LE string and calculate the length of the UTF-8 encoding of it.
     */
    size_t cchResult;
    int rc = rtUtf16LittleCalcUtf8Length(pwszString, cwcString, &cchResult);
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
            if (RT_UNLIKELY(cch <= cchResult))
                return VERR_BUFFER_OVERFLOW;
            pszResult = *ppsz;
        }
        else
        {
            *ppsz = NULL;
            fShouldFree = true;
            cch = RT_MAX(cch, cchResult + 1);
            pszResult = (char *)RTStrAllocTag(cch, pszTag);
        }
        if (pszResult)
        {
            rc = rtUtf16LittleRecodeAsUtf8(pwszString, cwcString, pszResult, cch - 1, &cch);
            if (RT_SUCCESS(rc))
            {
                *ppsz = pszResult;
                return rc;
            }

            if (fShouldFree)
                RTStrFree(pszResult);
        }
        else
            rc = VERR_NO_STR_MEMORY;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16BigToUtf8ExTag);


RTDECL(size_t) RTUtf16CalcUtf8Len(PCRTUTF16 pwsz)
{
    size_t cch;
#ifdef RT_BIG_ENDIAN
    int rc = rtUtf16BigCalcUtf8Length(pwsz, RTSTR_MAX, &cch);
#else
    int rc = rtUtf16LittleCalcUtf8Length(pwsz, RTSTR_MAX, &cch);
#endif
    return RT_SUCCESS(rc) ? cch : 0;
}
RT_EXPORT_SYMBOL(RTUtf16CalcUtf8Len);


RTDECL(size_t) RTUtf16BigCalcUtf8Len(PCRTUTF16 pwsz)
{
    size_t cch;
    int rc = rtUtf16BigCalcUtf8Length(pwsz, RTSTR_MAX, &cch);
    return RT_SUCCESS(rc) ? cch : 0;
}
RT_EXPORT_SYMBOL(RTUtf16BigCalcUtf8Len);


RTDECL(size_t) RTUtf16LittleCalcUtf8Len(PCRTUTF16 pwsz)
{
    size_t cch;
    int rc = rtUtf16LittleCalcUtf8Length(pwsz, RTSTR_MAX, &cch);
    return RT_SUCCESS(rc) ? cch : 0;
}
RT_EXPORT_SYMBOL(RTUtf16LittleCalcUtf8Len);


RTDECL(int) RTUtf16CalcUtf8LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    size_t cch;
#ifdef RT_BIG_ENDIAN
    int rc = rtUtf16BigCalcUtf8Length(pwsz, cwc, &cch);
#else
    int rc = rtUtf16LittleCalcUtf8Length(pwsz, cwc, &cch);
#endif
    if (pcch)
        *pcch = RT_SUCCESS(rc) ? cch : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16CalcUtf8LenEx);


RTDECL(int) RTUtf16BigCalcUtf8LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    size_t cch;
    int rc = rtUtf16BigCalcUtf8Length(pwsz, cwc, &cch);
    if (pcch)
        *pcch = RT_SUCCESS(rc) ? cch : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16BigCalcUtf8LenEx);


RTDECL(int) RTUtf16LittleCalcUtf8LenEx(PCRTUTF16 pwsz, size_t cwc, size_t *pcch)
{
    size_t cch;
    int rc = rtUtf16LittleCalcUtf8Length(pwsz, cwc, &cch);
    if (pcch)
        *pcch = RT_SUCCESS(rc) ? cch : ~(size_t)0;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16LittleCalcUtf8LenEx);


RTDECL(RTUNICP) RTUtf16GetCpInternal(PCRTUTF16 pwsz)
{
    const RTUTF16 wc = *pwsz;

    /* simple */
    if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
        return wc;
    if (wc < 0xfffe)
    {
        /* surrogate pair */
        if (wc < 0xdc00)
        {
            const RTUTF16 wc2 = pwsz[1];
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                return uc;
            }

            RTStrAssertMsgFailed(("wc=%#08x wc2=%#08x - invalid 2nd char in surrogate pair\n", wc, wc2));
        }
        else
            RTStrAssertMsgFailed(("wc=%#08x - invalid surrogate pair order\n", wc));
    }
    else
        RTStrAssertMsgFailed(("wc=%#08x - endian indicator\n", wc));
    return RTUNICP_INVALID;
}
RT_EXPORT_SYMBOL(RTUtf16GetCpInternal);


RTDECL(int) RTUtf16GetCpExInternal(PCRTUTF16 *ppwsz, PRTUNICP pCp)
{
    const RTUTF16 wc = **ppwsz;

    /* simple */
    if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
    {
        (*ppwsz)++;
        *pCp = wc;
        return VINF_SUCCESS;
    }

    int rc;
    if (wc < 0xfffe)
    {
        /* surrogate pair */
        if (wc < 0xdc00)
        {
            const RTUTF16 wc2 = (*ppwsz)[1];
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                *pCp = uc;
                (*ppwsz) += 2;
                return VINF_SUCCESS;
            }

            RTStrAssertMsgFailed(("wc=%#08x wc2=%#08x - invalid 2nd char in surrogate pair\n", wc, wc2));
        }
        else
            RTStrAssertMsgFailed(("wc=%#08x - invalid surrogate pair order\n", wc));
        rc = VERR_INVALID_UTF16_ENCODING;
    }
    else
    {
        RTStrAssertMsgFailed(("wc=%#08x - endian indicator\n", wc));
        rc = VERR_CODE_POINT_ENDIAN_INDICATOR;
    }
    *pCp = RTUNICP_INVALID;
    (*ppwsz)++;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16GetCpExInternal);


RTDECL(int) RTUtf16GetCpNExInternal(PCRTUTF16 *ppwsz, size_t *pcwc, PRTUNICP pCp)
{
    int          rc;
    const size_t cwc = *pcwc;
    if (cwc > 0)
    {
        PCRTUTF16     pwsz = *ppwsz;
        const RTUTF16 wc   = **ppwsz;

        /* simple */
        if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
        {
            *pCp   = wc;
            *pcwc  = cwc  - 1;
            *ppwsz = pwsz + 1;
            return VINF_SUCCESS;
        }

        if (wc < 0xfffe)
        {
            /* surrogate pair */
            if (wc < 0xdc00)
            {
                if (cwc >= 2)
                {
                    const RTUTF16 wc2 = pwsz[1];
                    if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
                    {
                        *pCp   = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                        *pcwc  = cwc  - 2;
                        *ppwsz = pwsz + 2;
                        return VINF_SUCCESS;
                    }

                    RTStrAssertMsgFailed(("wc=%#08x wc2=%#08x - invalid 2nd char in surrogate pair\n", wc, wc2));
                }
                else
                    RTStrAssertMsgFailed(("wc=%#08x - incomplete surrogate pair\n", wc));
            }
            else
                RTStrAssertMsgFailed(("wc=%#08x - invalid surrogate pair order\n", wc));
            rc = VERR_INVALID_UTF16_ENCODING;
        }
        else
        {
            RTStrAssertMsgFailed(("wc=%#08x - endian indicator\n", wc));
            rc = VERR_CODE_POINT_ENDIAN_INDICATOR;
        }
        *pcwc  = cwc  - 1;
        *ppwsz = pwsz + 1;
    }
    else
        rc = VERR_END_OF_STRING;
    *pCp = RTUNICP_INVALID;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16GetCpNExInternal);


RTDECL(int) RTUtf16BigGetCpExInternal(PCRTUTF16 *ppwsz, PRTUNICP pCp)
{
    const RTUTF16 wc = RT_BE2H_U16(**ppwsz);

    /* simple */
    if (wc < 0xd800 || (wc > 0xdfff && wc < 0xfffe))
    {
        (*ppwsz)++;
        *pCp = wc;
        return VINF_SUCCESS;
    }

    int rc;
    if (wc < 0xfffe)
    {
        /* surrogate pair */
        if (wc < 0xdc00)
        {
            const RTUTF16 wc2 = RT_BE2H_U16((*ppwsz)[1]);
            if (wc2 >= 0xdc00 && wc2 <= 0xdfff)
            {
                RTUNICP uc = 0x10000 + (((wc & 0x3ff) << 10) | (wc2 & 0x3ff));
                *pCp = uc;
                (*ppwsz) += 2;
                return VINF_SUCCESS;
            }

            RTStrAssertMsgFailed(("wc=%#08x wc2=%#08x - invalid 2nd char in surrogate pair\n", wc, wc2));
        }
        else
            RTStrAssertMsgFailed(("wc=%#08x - invalid surrogate pair order\n", wc));
        rc = VERR_INVALID_UTF16_ENCODING;
    }
    else
    {
        RTStrAssertMsgFailed(("wc=%#08x - endian indicator\n", wc));
        rc = VERR_CODE_POINT_ENDIAN_INDICATOR;
    }
    *pCp = RTUNICP_INVALID;
    (*ppwsz)++;
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16GetCpExInternal);


RTDECL(PRTUTF16) RTUtf16PutCpInternal(PRTUTF16 pwsz, RTUNICP CodePoint)
{
    /* simple */
    if (    CodePoint < 0xd800
        ||  (   CodePoint > 0xdfff
             && CodePoint < 0xfffe))
    {
        *pwsz++ = (RTUTF16)CodePoint;
        return pwsz;
    }

    /* surrogate pair */
    if (CodePoint >= 0x10000 && CodePoint <= 0x0010ffff)
    {
        CodePoint -= 0x10000;
        *pwsz++ = 0xd800 | (CodePoint >> 10);
        *pwsz++ = 0xdc00 | (CodePoint & 0x3ff);
        return pwsz;
    }

    /* invalid code point. */
    RTStrAssertMsgFailed(("Invalid codepoint %#x\n", CodePoint));
    *pwsz++ = 0x7f;
    return pwsz;
}
RT_EXPORT_SYMBOL(RTUtf16PutCpInternal);

