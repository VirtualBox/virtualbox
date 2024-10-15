/* $Id: shacrypt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - SHA-crypt.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#include <iprt/crypto/shacrypt.h>

#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/sha.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * The SHACRYPT_MINIMAL option reduces the .text + .rdata size in a Windows release
 * build from 6956 to 4592 bytes by not unrolling the not-base64 encoding and instead
 * using common code w/ a mapping table.
 */
#define SHACRYPT_MINIMAL /* don't unroll the digest -> characters conversion */

/**
 * Encode a byte triplet into four characters (or less), base64-style.
 *
 * @note This differs from base64 in that it is LSB oriented,
 *       so where base64 will use bits[7:2] from the first byte for the first
 *       char, this will use bits [5:0].
 *
 *       The character set is also different.
 */
#define NOT_BASE64_ENCODE(a_psz, a_off, a_bVal2, a_bVal1, a_bVal0, a_cOutputChars) \
    do { \
        uint32_t uWord = RT_MAKE_U32_FROM_MSB_U8(0, a_bVal2, a_bVal1, a_bVal0); \
        a_psz[(a_off)++] = g_achCryptBase64[uWord & 0x3f]; \
        if ((a_cOutputChars) > 1) \
        { \
            uWord >>= 6; \
            a_psz[(a_off)++] = g_achCryptBase64[uWord & 0x3f]; \
        } \
        if ((a_cOutputChars) > 2) \
        { \
            uWord >>= 6; \
            a_psz[(a_off)++] = g_achCryptBase64[uWord & 0x3f]; \
        } \
        if ((a_cOutputChars) > 3) \
        { \
            uWord >>= 6; \
            a_psz[(a_off)++] = g_achCryptBase64[uWord & 0x3f]; \
        } \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** This is the non-standard base64 encoding characters used by SHA-crypt and friends. */
static const char g_achCryptBase64[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
AssertCompile(sizeof(g_achCryptBase64) == 64 + 1);


#ifdef SHACRYPT_MINIMAL
/**
 * Common "base64" encoding function used by RTCrShaCrypt256ToString and
 * RTCrShaCrypt512ToString in minimal mode.
 */
static size_t rtCrShaCryptDigestToChars(char *pszString, size_t off, uint8_t const *pabHash, size_t cbHash,
                                        uint8_t const *pabMapping)
{
    /* full triplets first: */
    uintptr_t idx = 0;
    while (idx + 3 <= cbHash)
    {
        NOT_BASE64_ENCODE(pszString, off,
                          pabHash[pabMapping[idx + 2]],
                          pabHash[pabMapping[idx + 1]],
                          pabHash[pabMapping[idx + 0]], 4);
        idx += 3;
    }

    /* Anything remaining: Either 1 or 2, never zero. */
    switch (cbHash - idx)
    {
        case 1:
            NOT_BASE64_ENCODE(pszString, off,
                              0,
                              0,
                              pabHash[pabMapping[idx + 0]], 2);
            break;
        case 2:
            NOT_BASE64_ENCODE(pszString, off,
                              0,
                              pabHash[pabMapping[idx + 1]],
                              pabHash[pabMapping[idx + 0]], 3);
            break;
        default: AssertFailedBreak();
    }

    return off;
}
#endif /* SHACRYPT_MINIMAL */


/**
 * Extracts the salt from a given string.
 *
 * @returns Pointer to the salt string, or NULL if not found / invalid.
 * @param   pszSalt     The string containing the salt.
 * @param   pcchSalt    Where to return the extracted salt length (in
 *                      characters) on success.
 * @param   pcRounds    Where to return the round count on success.
 */
static const char *rtCrShaCryptExtractSaltAndRounds(const char *pszSalt, size_t *pcchSalt, uint32_t *pcRounds)
{
    /*
     * Skip either of the two SHA-2 prefixes.
     */
    if (   pszSalt[0] == '$'
        && (pszSalt[1] == '5' || pszSalt[1] == '6')
        && pszSalt[2] == '$')
        pszSalt += 3;

    /* Look for 'rounds=xxxxx$'. */
    if (strncmp(pszSalt, RT_STR_TUPLE("rounds=")) == 0)
    {
        const char * const pszValue  = pszSalt ? &pszSalt[sizeof("rounds=") - 1] : NULL; /* For ASAN build (false positive). */
        const char * const pszDollar = strchr(pszValue, '$');
        if (pszDollar)
        {
            char *pszNext = NULL;
            int rc = RTStrToUInt32Ex(pszValue, &pszNext, 10, pcRounds);
            if (rc == VWRN_TRAILING_CHARS && pszNext == pszDollar)
            { /* likely */ }
            else if (rc == VWRN_NUMBER_TOO_BIG)
                *pcRounds = UINT32_MAX;
            else
                return NULL;
            pszSalt = pszDollar + 1;
        }
    }

    /* Find the length of the salt - it sends with '$' or '\0'. */
    const char * const pszDollar = strchr(pszSalt, '$');
    if (!pszDollar)
        *pcchSalt = strlen(pszSalt);
    else
        *pcchSalt = (size_t)(pszDollar - pszSalt);
    return pszSalt;
}


/*
 * The algorithm for the SHA-256 and SHA-512 encryption is identical, except for
 * how the bytes are distributed in the final step.  So we use a pre-processor
 * code template for the implementation.
 */

/* SHA-256*/
#define TMPL_HASH_BITS              256
#define TMPL_HASH_SIZE              RTSHA256_HASH_SIZE
#define TMPL_HASH_CONTEXT_T         RTSHA256CONTEXT
#define TmplHashInit                RTSha256Init
#define TmplHashUpdate              RTSha256Update
#define TmplHashFinal               RTSha256Final
#define TMPL_SHACRYPT_ID_STR        RT_SHACRYPT_ID_STR_256
#define RTCrShaCryptTmpl            RTCrShaCrypt256
#define RTCrShaCryptTmplEx          RTCrShaCrypt256Ex
#define RTCrShaCryptTmplToString    RTCrShaCrypt256ToString
#include "shacrypt-tmpl.cpp.h"

/* SHA-512*/
#define TMPL_HASH_BITS              512
#define TMPL_HASH_SIZE              RTSHA512_HASH_SIZE
#define TMPL_HASH_CONTEXT_T         RTSHA512CONTEXT
#define TmplHashInit                RTSha512Init
#define TmplHashUpdate              RTSha512Update
#define TmplHashFinal               RTSha512Final
#define TMPL_SHACRYPT_ID_STR        RT_SHACRYPT_ID_STR_512
#define RTCrShaCryptTmpl            RTCrShaCrypt512
#define RTCrShaCryptTmplEx          RTCrShaCrypt512Ex
#define RTCrShaCryptTmplToString    RTCrShaCrypt512ToString
#include "shacrypt-tmpl.cpp.h"


RTDECL(int) RTCrShaCryptGenerateSalt(char *pszSalt, size_t cchSalt)
{
    AssertMsgReturn(cchSalt >= RT_SHACRYPT_SALT_MIN_LEN && cchSalt <= RT_SHACRYPT_SALT_MAX_LEN, ("len=%zu\n", cchSalt),
                    VERR_OUT_OF_RANGE);

    for (size_t i = 0; i < cchSalt; i++)
        pszSalt[i] = g_achCryptBase64[RTRandU32Ex(0, sizeof(g_achCryptBase64) - 2)];

    pszSalt[cchSalt] = '\0';
    return VINF_SUCCESS;
}

