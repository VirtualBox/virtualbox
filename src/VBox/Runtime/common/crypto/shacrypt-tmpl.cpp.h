/* $Id: shacrypt-tmpl.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - SHA-crypt, code template the core code.
 *
 * This is included a couple of times from shacrypt.cpp with different set of
 * defines for each variation.
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


RTDECL(int) RTCrShaCryptTmpl(const char *pszPhrase, const char *pszSalt, uint32_t cRounds, char *pszString, size_t cbString)
{
    uint8_t abHash[TMPL_HASH_SIZE];
    int rc = RTCrShaCryptTmplEx(pszPhrase, pszSalt, cRounds, abHash);
    if (RT_SUCCESS(rc))
        rc = RTCrShaCryptTmplToString(abHash, pszSalt, cRounds, pszString, cbString);
    return rc;
}



RTR3DECL(int) RTCrShaCryptTmplEx(const char *pszPhrase, const char *pszSalt, uint32_t cRounds, uint8_t pabHash[TMPL_HASH_SIZE])
{
    /*
     * Validate and adjust input.
     */
    AssertPtrReturn(pszPhrase, VERR_INVALID_POINTER);
    size_t const cchPhrase = strlen(pszPhrase);
    AssertReturn(cchPhrase, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszSalt, VERR_INVALID_POINTER);
    size_t cchSalt;
    pszSalt = rtCrShaCryptExtractSaltAndRounds(pszSalt, &cchSalt, &cRounds);
    AssertReturn(pszSalt != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(cchSalt >= RT_SHACRYPT_SALT_MIN_LEN, VERR_BUFFER_UNDERFLOW);
    AssertReturn(cchSalt <= RT_SHACRYPT_SALT_MAX_LEN, VERR_TOO_MUCH_DATA);
    AssertReturn(cRounds >= RT_SHACRYPT_ROUNDS_MIN && cRounds <= RT_SHACRYPT_ROUNDS_MAX, VERR_OUT_OF_RANGE);

    /*
     * Get started...
     */
    TMPL_HASH_CONTEXT_T CtxA;
    TmplHashInit(&CtxA);                                                        /* Step 1. */
    TmplHashUpdate(&CtxA, pszPhrase, cchPhrase);                                /* Step 2. */
    TmplHashUpdate(&CtxA, pszSalt, cchSalt);                                    /* Step 3. */

    TMPL_HASH_CONTEXT_T CtxB;
    TmplHashInit(&CtxB);                                                        /* Step 4. */
    TmplHashUpdate(&CtxB, pszPhrase, cchPhrase);                                /* Step 5. */
    TmplHashUpdate(&CtxB, pszSalt, cchSalt);                                    /* Step 6. */
    TmplHashUpdate(&CtxB, pszPhrase, cchPhrase);                                /* Step 7. */
    uint8_t abDigest[TMPL_HASH_SIZE];
    TmplHashFinal(&CtxB, abDigest);                                             /* Step 8. */

    size_t cbLeft = cchPhrase;
    while (cbLeft > TMPL_HASH_SIZE)                                             /* Step 9. */
    {
        TmplHashUpdate(&CtxA, abDigest, sizeof(abDigest));
        cbLeft -= TMPL_HASH_SIZE;
    }
    TmplHashUpdate(&CtxA, abDigest, cbLeft);                                    /* Step 10. */

    size_t iPhraseBit = cchPhrase;
    while (iPhraseBit)                                                          /* Step 11. */
    {
        if ((iPhraseBit & 1) != 0)
            TmplHashUpdate(&CtxA, abDigest, sizeof(abDigest));                  /* a) */
        else
            TmplHashUpdate(&CtxA, pszPhrase, cchPhrase);                        /* b) */
        iPhraseBit >>= 1;
    }

    TmplHashFinal(&CtxA, abDigest);                                             /* Step 12. */

    TmplHashInit(&CtxB);                                                        /* Step 13. */
    for (size_t i = 0; i < cchPhrase; i++)                                      /* Step 14. */
        TmplHashUpdate(&CtxB, pszPhrase, cchPhrase);

    uint8_t abDigestTemp[TMPL_HASH_SIZE];
    TmplHashFinal(&CtxB, abDigestTemp);                                         /* Step 15. */

    /*
     * Byte sequence P (= password).
     */
    /* Step 16. */
    size_t const cbSeqP  = cchPhrase;
    uint8_t     *pabSeqP = (uint8_t *)RTMemTmpAllocZ(cbSeqP + 1);               /* +1 because the password may be empty */
    uint8_t     *pb       = pabSeqP;
    AssertPtrReturn(pabSeqP, VERR_NO_MEMORY);
    cbLeft = cbSeqP;
    while (cbLeft > TMPL_HASH_SIZE)
    {
        memcpy(pb, abDigestTemp, sizeof(abDigestTemp));                         /* a) */
        pb     += TMPL_HASH_SIZE;
        cbLeft -= TMPL_HASH_SIZE;
    }
    memcpy(pb, abDigestTemp, cbLeft);                                           /* b) */

    TmplHashInit(&CtxB);                                                        /* Step 17. */

    for (size_t i = 0; i < 16 + (unsigned)abDigest[0]; i++)                     /* Step 18. */
        TmplHashUpdate(&CtxB, pszSalt, cchSalt);

    TmplHashFinal(&CtxB, abDigestTemp);                                         /* Step 19. */

    /*
     * Byte sequence S (= salt).
     */
    /* Step 20. */
    size_t   const  cbSeqS  = cchSalt;
#if 0 /* Given that the salt has a fixed range (8 thru 16 bytes), and SHA-512/256
       * producing 64 bytes, we can safely skip the loop part here (a) and go
       * straight for step (b). Further, we can drop the whole memory allocation,
       * let alone duplication (it's all overwritten!), and use an uninitalized
       * stack buffer. */
    uint8_t * const pabSeqS = (uint8_t *)RTMemDup(pszSalt, cbSeqS + 1);
    AssertPtrReturn(pabSeqS, VERR_NO_MEMORY);

    pb     = pabSeqS;
    cbLeft = cbSeqS;
    while (cbLeft > TMPL_HASH_SIZE)
    {
        memcpy(pb, (void *)abDigestTemp, sizeof(abDigestTemp));                 /* a) */
        pb     += TMPL_HASH_SIZE;
        cbLeft -= TMPL_HASH_SIZE
    }
    memcpy(pb, abDigestTemp, cbLeft);                                           /* b) */
#else
    AssertCompile(RT_SHACRYPT_SALT_MAX_LEN < TMPL_HASH_SIZE);
    uint8_t         abSeqS[RT_SHACRYPT_SALT_MAX_LEN + 2];
    uint8_t * const pabSeqS = abSeqS;
    memcpy(abSeqS, abDigestTemp, cbSeqS);                                       /* b) */
#endif

    /* Step 21. */
    for (uint32_t iRound = 0; iRound < cRounds; iRound++)
    {
        TMPL_HASH_CONTEXT_T CtxC;
        TmplHashInit(&CtxC);                                                    /* a) */

        if ((iRound & 1) != 0)
            TmplHashUpdate(&CtxC, pabSeqP, cbSeqP);                             /* b) */
        else
            TmplHashUpdate(&CtxC, abDigest, sizeof(abDigest));                  /* c) */

        if (iRound % 3 != 0)                                                    /* d) */
            TmplHashUpdate(&CtxC, pabSeqS, cbSeqS);

        if (iRound % 7 != 0)
            TmplHashUpdate(&CtxC, pabSeqP, cbSeqP);                             /* e) */

        if ((iRound & 1) != 0)
            TmplHashUpdate(&CtxC, abDigest, sizeof(abDigest));                  /* f) */
        else
            TmplHashUpdate(&CtxC, pabSeqP, cbSeqP);                             /* g) */

        TmplHashFinal(&CtxC, abDigest);                                         /* h) */
    }

    /*
     * Done.
     */
    memcpy(pabHash, abDigest, TMPL_HASH_SIZE);

    /*
     * Cleanup.
     */
    RTMemWipeThoroughly(abDigestTemp, TMPL_HASH_SIZE, 3);
    RTMemWipeThoroughly(pabSeqP, cbSeqP, 3);
    RTMemTmpFree(pabSeqP);
#if 0
    RTMemWipeThoroughly(pabSeqS, cbSeqS, 3);
    RTMemFree(pabSeqS);
#else
    RTMemWipeThoroughly(abSeqS, sizeof(abSeqS), 3);
#endif

    return VINF_SUCCESS;
}


RTR3DECL(int) RTCrShaCryptTmplToString(uint8_t const pabHash[TMPL_HASH_SIZE], const char *pszSalt, uint32_t cRounds,
                                       char *pszString, size_t cbString)
{
    /*
     * Validate and adjust input.
     */
    AssertPtrReturn(pszSalt, VERR_INVALID_POINTER);
    size_t cchSalt;
    pszSalt = rtCrShaCryptExtractSaltAndRounds(pszSalt, &cchSalt, &cRounds);
    AssertReturn(pszSalt != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(cchSalt >= RT_SHACRYPT_SALT_MIN_LEN, VERR_BUFFER_UNDERFLOW);
    AssertReturn(cchSalt <= RT_SHACRYPT_SALT_MAX_LEN, VERR_TOO_MUCH_DATA);
    AssertReturn(cRounds >= RT_SHACRYPT_ROUNDS_MIN && cRounds <= RT_SHACRYPT_ROUNDS_MAX, VERR_OUT_OF_RANGE);

    AssertPtrReturn(pszString, VERR_INVALID_POINTER);

    /*
     * Calc the necessary buffer space and check that the caller supplied enough.
     */
    char    szRounds[64];
    ssize_t cchRounds = 0;
    if (cRounds != RT_SHACRYPT_ROUNDS_DEFAULT)
    {
        cchRounds = RTStrFormatU32(szRounds, sizeof(szRounds), cRounds, 10, 0, 0, 0);
        Assert(cchRounds > 0 && cchRounds <= 9);
    }

    size_t const cchNeeded = sizeof(TMPL_SHACRYPT_ID_STR) - 1
                           + (cRounds != RT_SHACRYPT_ROUNDS_DEFAULT ? cchRounds + sizeof("rounds=$") - 1 : 0)
                           + cchSalt + 1
                           + TMPL_HASH_SIZE * 4 / 3
                           + 1;
    AssertReturn(cbString > cchNeeded, VERR_BUFFER_OVERFLOW);

    /*
     * Do the formatting.
     */
    memcpy(pszString, RT_STR_TUPLE(TMPL_SHACRYPT_ID_STR));
    size_t off = sizeof(TMPL_SHACRYPT_ID_STR) - 1;

    if (cRounds != RT_SHACRYPT_ROUNDS_DEFAULT)
    {
        memcpy(&pszString[off], RT_STR_TUPLE("rounds="));
        off += sizeof("rounds=") - 1;

        memcpy(&pszString[off], szRounds, cchRounds);
        off += cchRounds;
        pszString[off++] = '$';
    }

    memcpy(&pszString[off], pszSalt, cchSalt);
    off += cchSalt;
    pszString[off++] = '$';

#ifdef SHACRYPT_MINIMAL
    /*
     * Use a table for the shuffling of the digest bytes and work it in a loop.
     */
    static uint8_t const s_abMapping[] =
    {
# if TMPL_HASH_BITS == 512
        42, 21,  0,
         1, 43, 22,
        23,  2, 44,
        45, 24,  3,
         4, 46, 25,
        26,  5, 47,
        48, 27,  6,
         7, 49, 28,
        29,  8, 50,
        51, 30,  9,
        10, 52, 31,
        32, 11, 53,
        54, 33, 12,
        13, 55, 34,
        35, 14, 56,
        57, 36, 15,
        16, 58, 37,
        38, 17, 59,
        60, 39, 18,
        19, 61, 40,
        41, 20, 62,
        63
# elif TMPL_HASH_BITS == 256
        20, 10,  0,
        11,  1, 21,
         2, 22, 12,
        23, 13,  3,
        14,  4, 24,
         5, 25, 15,
        26, 16,  6,
        17,  7, 27,
         8, 28, 18,
        29, 19,  9,
        30, 31,
# else
#  error "TMPL_HASH_BITS"
# endif
    };
    AssertCompile(sizeof(s_abMapping) == TMPL_HASH_SIZE);
    off = rtCrShaCryptDigestToChars(pszString, off, pabHash, TMPL_HASH_SIZE, s_abMapping);

#else /* !SHACRYPT_MINIMAL */
    /*
     * Unroll the digest shuffling and conversion to characters.
     * This takes a lot of code space.
     */
# if TMPL_HASH_BITS == 512
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 0], pabHash[21], pabHash[42], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[22], pabHash[43], pabHash[ 1], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[44], pabHash[ 2], pabHash[23], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 3], pabHash[24], pabHash[45], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[25], pabHash[46], pabHash[ 4], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[47], pabHash[ 5], pabHash[26], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 6], pabHash[27], pabHash[48], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[28], pabHash[49], pabHash[ 7], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[50], pabHash[ 8], pabHash[29], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 9], pabHash[30], pabHash[51], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[31], pabHash[52], pabHash[10], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[53], pabHash[11], pabHash[32], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[12], pabHash[33], pabHash[54], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[34], pabHash[55], pabHash[13], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[56], pabHash[14], pabHash[35], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[15], pabHash[36], pabHash[57], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[37], pabHash[58], pabHash[16], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[59], pabHash[17], pabHash[38], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[18], pabHash[39], pabHash[60], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[40], pabHash[61], pabHash[19], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[62], pabHash[20], pabHash[41], 4);
    NOT_BASE64_ENCODE(pszString, off,           0,           0, pabHash[63], 2);

# elif TMPL_HASH_BITS == 256
    NOT_BASE64_ENCODE(pszString, off, pabHash[00], pabHash[10], pabHash[20], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[21], pabHash[ 1], pabHash[11], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[12], pabHash[22], pabHash[ 2], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 3], pabHash[13], pabHash[23], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[24], pabHash[ 4], pabHash[14], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[15], pabHash[25], pabHash[ 5], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 6], pabHash[16], pabHash[26], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[27], pabHash[ 7], pabHash[17], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[18], pabHash[28], pabHash[ 8], 4);
    NOT_BASE64_ENCODE(pszString, off, pabHash[ 9], pabHash[19], pabHash[29], 4);
    NOT_BASE64_ENCODE(pszString, off, 0,           pabHash[31], pabHash[30], 3);

# else
#  error "TMPL_HASH_BITS"
# endif
#endif  /* !SHACRYPT_MINIMAL */

    pszString[off] = '\0';
    Assert(off < cbString);

    return VINF_SUCCESS;
}


#undef TMPL_HASH_BITS
#undef TMPL_HASH_SIZE
#undef TMPL_HASH_CONTEXT_T
#undef TmplHashInit
#undef TmplHashUpdate
#undef TmplHashFinal
#undef TMPL_SHACRYPT_ID_STR
#undef RTCrShaCryptTmpl
#undef RTCrShaCryptTmplEx
#undef RTCrShaCryptTmplToString

