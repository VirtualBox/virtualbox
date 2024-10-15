/* $Id: alt-sha256.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-256 and SHA-224 hash functions, Alternative Implementation.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The SHA-256 block size (in bytes). */
#define RTSHA256_BLOCK_SIZE   64U

/** Enables the unrolled code. */
#define RTSHA256_UNROLLED 1


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>


/** Our private context structure. */
typedef struct RTSHA256ALTPRIVATECTX
{
    /** The W array.
     * Buffering happens in the first 16 words, converted from big endian to host
     * endian immediately before processing.  The amount of buffered data is kept
     * in the 6 least significant bits of cbMessage. */
    uint32_t    auW[64];
    /** The message length (in bytes). */
    uint64_t    cbMessage;
    /** The 8 hash values. */
    uint32_t    auH[8];
} RTSHA256ALTPRIVATECTX;

#define RT_SHA256_PRIVATE_ALT_CONTEXT
#include <iprt/sha.h>


AssertCompile(RT_SIZEOFMEMB(RTSHA256CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTSHA256CONTEXT, AltPrivate));
AssertCompileMemberSize(RTSHA256ALTPRIVATECTX, auH, RTSHA256_HASH_SIZE);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef RTSHA256_UNROLLED
/** The K constants */
static uint32_t const g_auKs[] =
{
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
};
#endif /* !RTSHA256_UNROLLED */



RTDECL(void) RTSha256Init(PRTSHA256CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage = 0;
    pCtx->AltPrivate.auH[0] = UINT32_C(0x6a09e667);
    pCtx->AltPrivate.auH[1] = UINT32_C(0xbb67ae85);
    pCtx->AltPrivate.auH[2] = UINT32_C(0x3c6ef372);
    pCtx->AltPrivate.auH[3] = UINT32_C(0xa54ff53a);
    pCtx->AltPrivate.auH[4] = UINT32_C(0x510e527f);
    pCtx->AltPrivate.auH[5] = UINT32_C(0x9b05688c);
    pCtx->AltPrivate.auH[6] = UINT32_C(0x1f83d9ab);
    pCtx->AltPrivate.auH[7] = UINT32_C(0x5be0cd19);
}
RT_EXPORT_SYMBOL(RTSha256Init);


/** Function 4.2. */
DECL_FORCE_INLINE(uint32_t) rtSha256Ch(uint32_t uX, uint32_t uY, uint32_t uZ)
{
#if 1
    /* Optimization that saves one operation and probably a temporary variable. */
    uint32_t uResult = uY;
    uResult ^= uZ;
    uResult &= uX;
    uResult ^= uZ;
    return uResult;
#else
    /* The original. */
    uint32_t uResult = uX & uY;
    uResult ^= ~uX & uZ;
    return uResult;
#endif
}


/** Function 4.3. */
DECL_FORCE_INLINE(uint32_t) rtSha256Maj(uint32_t uX, uint32_t uY, uint32_t uZ)
{
#if 1
    /* Optimization that save one operation and probably a temporary variable. */
    uint32_t uResult = uY;
    uResult ^= uZ;
    uResult &= uX;
    uResult ^= uY & uZ;
    return uResult;
#else
    /* The original. */
    uint32_t uResult = uX & uY;
    uResult ^= uX & uZ;
    uResult ^= uY & uZ;
    return uResult;
#endif
}


/** Function 4.4. */
DECL_FORCE_INLINE(uint32_t) rtSha256CapitalSigma0(uint32_t uX)
{
    uint32_t uResult = uX = ASMRotateRightU32(uX, 2);
    uX = ASMRotateRightU32(uX, 13 - 2);
    uResult ^= uX;
    uX = ASMRotateRightU32(uX, 22 - 13);
    uResult ^= uX;
    return uResult;
}


/** Function 4.5. */
DECL_FORCE_INLINE(uint32_t) rtSha256CapitalSigma1(uint32_t uX)
{
    uint32_t uResult = uX = ASMRotateRightU32(uX, 6);
    uX = ASMRotateRightU32(uX, 11 - 6);
    uResult ^= uX;
    uX = ASMRotateRightU32(uX, 25 - 11);
    uResult ^= uX;
    return uResult;
}


/** Function 4.6. */
DECL_FORCE_INLINE(uint32_t) rtSha256SmallSigma0(uint32_t uX)
{
    uint32_t uResult = uX >> 3;
    uX = ASMRotateRightU32(uX, 7);
    uResult ^= uX;
    uX = ASMRotateRightU32(uX, 18 - 7);
    uResult ^= uX;
    return uResult;
}


/** Function 4.7. */
DECL_FORCE_INLINE(uint32_t) rtSha256SmallSigma1(uint32_t uX)
{
    uint32_t uResult = uX >> 10;
    uX = ASMRotateRightU32(uX, 17);
    uResult ^= uX;
    uX = ASMRotateRightU32(uX, 19 - 17);
    uResult ^= uX;
    return uResult;
}


/**
 * Initializes the auW array from the specfied input block.
 *
 * @param   pCtx                The SHA-256 context.
 * @param   pbBlock             The block.  Must be arch-bit-width aligned.
 */
DECLINLINE(void) rtSha256BlockInit(PRTSHA256CONTEXT pCtx, uint8_t const *pbBlock)
{
#ifdef RTSHA256_UNROLLED
    /* Copy and byte-swap the block. Initializing the rest of the Ws are done
       in the processing loop. */
# ifdef RT_LITTLE_ENDIAN
#  if 0 /* Just an idea... very little gain as this isn't the expensive code. */
    __m128i const  uBSwapConst = { 3, 2, 1, 0,  7, 6, 5, 4,  11, 10, 9, 8,  15, 14, 13, 12 };
    __m128i const *puSrc = (__m128i const *)pbBlock;
    __m128i       *puDst = (__m128i *)&pCtx->AltPrivate.auW[0];

    _mm_storeu_si128(puDst, _mm_shuffle_epi8(_mm_loadu_si128(puSrc), uBSwapConst)); puDst++; puSrc++;
    _mm_storeu_si128(puDst, _mm_shuffle_epi8(_mm_loadu_si128(puSrc), uBSwapConst)); puDst++; puSrc++;
    _mm_storeu_si128(puDst, _mm_shuffle_epi8(_mm_loadu_si128(puSrc), uBSwapConst)); puDst++; puSrc++;
    _mm_storeu_si128(puDst, _mm_shuffle_epi8(_mm_loadu_si128(puSrc), uBSwapConst)); puDst++; puSrc++;

#  elif ARCH_BITS == 64
    uint64_t const *puSrc = (uint64_t const *)pbBlock;
    uint64_t       *puW   = (uint64_t *)&pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puSrc & 7));
    Assert(!((uintptr_t)puW & 7));

    /* b0 b1 b2 b3  b4 b5 b6 b7 --bwap--> b7 b6 b5 b4 b3 b2 b1 b0 --ror--> b3 b2 b1 b0  b7 b6 b5 b4; */
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);

    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);
    *puW++ = ASMRotateRightU64(ASMByteSwapU64(*puSrc++), 32);

#  else
    uint32_t const *puSrc = (uint32_t const *)pbBlock;
    uint32_t       *puW   = &pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puSrc & 3));
    Assert(!((uintptr_t)puW & 3));

    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);

    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);

    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);

    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
    *puW++ = ASMByteSwapU32(*puSrc++);
#  endif
# else  /* RT_BIG_ENDIAN */
    memcpy(&pCtx->AltPrivate.auW[0], pbBlock, RTSHA256_BLOCK_SIZE);
# endif /* RT_BIG_ENDIAN */

#else  /* !RTSHA256_UNROLLED */
    uint32_t const *pu32Block = (uint32_t const *)pbBlock;
    Assert(!((uintptr_t)pu32Block & 3));

    unsigned iWord;
    for (iWord = 0; iWord < 16; iWord++)
        pCtx->AltPrivate.auW[iWord] = RT_BE2H_U32(pu32Block[iWord]);

    for (; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint32_t u32 = rtSha256SmallSigma1(pCtx->AltPrivate.auW[iWord - 2]);
        u32         += rtSha256SmallSigma0(pCtx->AltPrivate.auW[iWord - 15]);
        u32         += pCtx->AltPrivate.auW[iWord - 7];
        u32         += pCtx->AltPrivate.auW[iWord - 16];
        pCtx->AltPrivate.auW[iWord] = u32;
    }
#endif /* !RTSHA256_UNROLLED */
}


/**
 * Initializes the auW array from data buffered in the first part of the array.
 *
 * @param   pCtx                The SHA-256 context.
 */
DECLINLINE(void) rtSha256BlockInitBuffered(PRTSHA256CONTEXT pCtx)
{
#ifdef RTSHA256_UNROLLED
    /* Do the byte swap if necessary. Initializing the rest of the Ws are done
       in the processing loop. */
# ifdef RT_LITTLE_ENDIAN
#  if ARCH_BITS == 64
    uint64_t *puW = (uint64_t *)&pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puW & 7));
    /* b0 b1 b2 b3  b4 b5 b6 b7 --bwap--> b7 b6 b5 b4 b3 b2 b1 b0 --ror--> b3 b2 b1 b0  b7 b6 b5 b4; */
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;

    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;
    *puW = ASMRotateRightU64(ASMByteSwapU64(*puW), 32); puW++;

#  else
    uint32_t *puW = &pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puW & 3));

    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;

    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;

    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;

    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
    *puW = ASMByteSwapU32(*puW); puW++;
#  endif
# endif

#else  /* !RTSHA256_UNROLLED */
    unsigned iWord;
    for (iWord = 0; iWord < 16; iWord++)
        pCtx->AltPrivate.auW[iWord] = RT_BE2H_U32(pCtx->AltPrivate.auW[iWord]);

    for (; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint32_t u32 = rtSha256SmallSigma1(pCtx->AltPrivate.auW[iWord - 2]);
        u32         += rtSha256SmallSigma0(pCtx->AltPrivate.auW[iWord - 15]);
        u32         += pCtx->AltPrivate.auW[iWord - 7];
        u32         += pCtx->AltPrivate.auW[iWord - 16];
        pCtx->AltPrivate.auW[iWord] = u32;
    }
#endif /* !RTSHA256_UNROLLED */
}


/**
 * Process the current block.
 *
 * Requires one of the rtSha256BlockInit functions to be called first.
 *
 * @param   pCtx                The SHA-256 context.
 */
static void rtSha256BlockProcess(PRTSHA256CONTEXT pCtx)
{
    uint32_t uA = pCtx->AltPrivate.auH[0];
    uint32_t uB = pCtx->AltPrivate.auH[1];
    uint32_t uC = pCtx->AltPrivate.auH[2];
    uint32_t uD = pCtx->AltPrivate.auH[3];
    uint32_t uE = pCtx->AltPrivate.auH[4];
    uint32_t uF = pCtx->AltPrivate.auH[5];
    uint32_t uG = pCtx->AltPrivate.auH[6];
    uint32_t uH = pCtx->AltPrivate.auH[7];

#ifdef RTSHA256_UNROLLED
    uint32_t *puW = &pCtx->AltPrivate.auW[0];
# define RTSHA256_BODY(a_iWord, a_uK, a_uA, a_uB, a_uC, a_uD, a_uE, a_uF, a_uG, a_uH) \
        do { \
            if ((a_iWord) < 16) \
                a_uH += *puW++; \
            else \
            { \
                uint32_t u32 = puW[-16]; \
                u32 += rtSha256SmallSigma0(puW[-15]); \
                u32 += puW[-7]; \
                u32 += rtSha256SmallSigma1(puW[-2]); \
                if (a_iWord < 64-2) *puW++ = u32; else puW++; \
                a_uH += u32; \
            } \
            \
            a_uH += rtSha256CapitalSigma1(a_uE); \
            a_uH += a_uK; \
            a_uH += rtSha256Ch(a_uE, a_uF, a_uG); \
            a_uD += a_uH; \
            \
            a_uH += rtSha256CapitalSigma0(a_uA); \
            a_uH += rtSha256Maj(a_uA, a_uB, a_uC); \
        } while (0)
# define RTSHA256_EIGHT(a_uK0, a_uK1, a_uK2, a_uK3, a_uK4, a_uK5, a_uK6, a_uK7, a_iFirst) \
        do { \
            RTSHA256_BODY(a_iFirst + 0, a_uK0, uA, uB, uC, uD, uE, uF, uG, uH); \
            RTSHA256_BODY(a_iFirst + 1, a_uK1, uH, uA, uB, uC, uD, uE, uF, uG); \
            RTSHA256_BODY(a_iFirst + 2, a_uK2, uG, uH, uA, uB, uC, uD, uE, uF); \
            RTSHA256_BODY(a_iFirst + 3, a_uK3, uF, uG, uH, uA, uB, uC, uD, uE); \
            RTSHA256_BODY(a_iFirst + 4, a_uK4, uE, uF, uG, uH, uA, uB, uC, uD); \
            RTSHA256_BODY(a_iFirst + 5, a_uK5, uD, uE, uF, uG, uH, uA, uB, uC); \
            RTSHA256_BODY(a_iFirst + 6, a_uK6, uC, uD, uE, uF, uG, uH, uA, uB); \
            RTSHA256_BODY(a_iFirst + 7, a_uK7, uB, uC, uD, uE, uF, uG, uH, uA); \
        } while (0)
    RTSHA256_EIGHT(UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
                   UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), 0);
    RTSHA256_EIGHT(UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
                   UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174), 8);
    RTSHA256_EIGHT(UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
                   UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da), 16);
    RTSHA256_EIGHT(UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
                   UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967), 24);
    RTSHA256_EIGHT(UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
                   UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85), 32);
    RTSHA256_EIGHT(UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
                   UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070), 40);
    RTSHA256_EIGHT(UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
                   UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), 48);
    RTSHA256_EIGHT(UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
                   UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2), 56);

#else  /* !RTSHA256_UNROLLED */
    for (unsigned iWord = 0; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint32_t uT1 = uH;
        uT1 += rtSha256CapitalSigma1(uE);
        uT1 += rtSha256Ch(uE, uF, uG);
        uT1 += g_auKs[iWord];
        uT1 += pCtx->AltPrivate.auW[iWord];

        uint32_t uT2 = rtSha256CapitalSigma0(uA);
        uT2 += rtSha256Maj(uA, uB, uC);

        uH = uG;
        uG = uF;
        uF = uE;
        uE = uD + uT1;
        uD = uC;
        uC = uB;
        uB = uA;
        uA = uT1 + uT2;
    }
#endif /* !RTSHA256_UNROLLED */

    pCtx->AltPrivate.auH[0] += uA;
    pCtx->AltPrivate.auH[1] += uB;
    pCtx->AltPrivate.auH[2] += uC;
    pCtx->AltPrivate.auH[3] += uD;
    pCtx->AltPrivate.auH[4] += uE;
    pCtx->AltPrivate.auH[5] += uF;
    pCtx->AltPrivate.auH[6] += uG;
    pCtx->AltPrivate.auH[7] += uH;
}


RTDECL(void) RTSha256Update(PRTSHA256CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    Assert(pCtx->AltPrivate.cbMessage < UINT64_MAX / 8);
    uint8_t const *pbBuf = (uint8_t const *)pvBuf;

    /*
     * Deal with buffered bytes first.
     */
    size_t cbBuffered = (size_t)pCtx->AltPrivate.cbMessage & (RTSHA256_BLOCK_SIZE - 1U);
    if (cbBuffered)
    {
        size_t cbMissing = RTSHA256_BLOCK_SIZE - cbBuffered;
        if (cbBuf >= cbMissing)
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, pbBuf, cbMissing);
            pCtx->AltPrivate.cbMessage += cbMissing;
            pbBuf += cbMissing;
            cbBuf -= cbMissing;

            rtSha256BlockInitBuffered(pCtx);
            rtSha256BlockProcess(pCtx);
        }
        else
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, pbBuf, cbBuf);
            pCtx->AltPrivate.cbMessage += cbBuf;
            return;
        }
    }

    if (!((uintptr_t)pbBuf & (sizeof(void *) - 1)))
    {
        /*
         * Process full blocks directly from the input buffer.
         */
        while (cbBuf >= RTSHA256_BLOCK_SIZE)
        {
            rtSha256BlockInit(pCtx, pbBuf);
            rtSha256BlockProcess(pCtx);

            pCtx->AltPrivate.cbMessage += RTSHA256_BLOCK_SIZE;
            pbBuf += RTSHA256_BLOCK_SIZE;
            cbBuf -= RTSHA256_BLOCK_SIZE;
        }
    }
    else
    {
        /*
         * Unaligned input, so buffer it.
         */
        while (cbBuf >= RTSHA256_BLOCK_SIZE)
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0], pbBuf, RTSHA256_BLOCK_SIZE);
            rtSha256BlockInitBuffered(pCtx);
            rtSha256BlockProcess(pCtx);

            pCtx->AltPrivate.cbMessage += RTSHA256_BLOCK_SIZE;
            pbBuf += RTSHA256_BLOCK_SIZE;
            cbBuf -= RTSHA256_BLOCK_SIZE;
        }
    }

    /*
     * Stash any remaining bytes into the context buffer.
     */
    if (cbBuf > 0)
    {
        memcpy((uint8_t *)&pCtx->AltPrivate.auW[0], pbBuf, cbBuf);
        pCtx->AltPrivate.cbMessage += cbBuf;
    }
}
RT_EXPORT_SYMBOL(RTSha256Update);


/**
 * Internal worker for RTSha256Final and RTSha224Final that finalizes the
 * computation but does not copy out the hash value.
 *
 * @param   pCtx                The SHA-256 context.
 */
static void rtSha256FinalInternal(PRTSHA256CONTEXT pCtx)
{
    Assert(pCtx->AltPrivate.cbMessage < UINT64_MAX / 8);

    /*
     * Complete the message by adding a single bit (0x80), padding till
     * the next 448-bit boundrary, the add the message length.
     */
    uint64_t const cMessageBits = pCtx->AltPrivate.cbMessage * 8;

    unsigned cbMissing = RTSHA256_BLOCK_SIZE - ((unsigned)pCtx->AltPrivate.cbMessage & (RTSHA256_BLOCK_SIZE - 1U));
    static uint8_t const s_abSingleBitAndSomePadding[12] =  { 0x80, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, };
    if (cbMissing < 1U + 8U)
        /* Less than 64+8 bits left in the current block, force a new block. */
        RTSha256Update(pCtx, &s_abSingleBitAndSomePadding, sizeof(s_abSingleBitAndSomePadding));
    else
        RTSha256Update(pCtx, &s_abSingleBitAndSomePadding, 1);

    unsigned cbBuffered = (unsigned)pCtx->AltPrivate.cbMessage & (RTSHA256_BLOCK_SIZE - 1U);
    cbMissing = RTSHA256_BLOCK_SIZE - cbBuffered;
    Assert(cbMissing >= 8);
    memset((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, 0, cbMissing - 8);

    *(uint64_t *)&pCtx->AltPrivate.auW[14] = RT_H2BE_U64(cMessageBits);

    /*
     * Process the last buffered block constructed/completed above.
     */
    rtSha256BlockInitBuffered(pCtx);
    rtSha256BlockProcess(pCtx);

    /*
     * Convert the byte order of the hash words and we're done.
     */
    pCtx->AltPrivate.auH[0] = RT_H2BE_U32(pCtx->AltPrivate.auH[0]);
    pCtx->AltPrivate.auH[1] = RT_H2BE_U32(pCtx->AltPrivate.auH[1]);
    pCtx->AltPrivate.auH[2] = RT_H2BE_U32(pCtx->AltPrivate.auH[2]);
    pCtx->AltPrivate.auH[3] = RT_H2BE_U32(pCtx->AltPrivate.auH[3]);
    pCtx->AltPrivate.auH[4] = RT_H2BE_U32(pCtx->AltPrivate.auH[4]);
    pCtx->AltPrivate.auH[5] = RT_H2BE_U32(pCtx->AltPrivate.auH[5]);
    pCtx->AltPrivate.auH[6] = RT_H2BE_U32(pCtx->AltPrivate.auH[6]);
    pCtx->AltPrivate.auH[7] = RT_H2BE_U32(pCtx->AltPrivate.auH[7]);

    RT_ZERO(pCtx->AltPrivate.auW);
    pCtx->AltPrivate.cbMessage = UINT64_MAX;
}
RT_EXPORT_SYMBOL(RTSha256Final);


RTDECL(void) RTSha256Final(PRTSHA256CONTEXT pCtx, uint8_t pabDigest[RTSHA256_HASH_SIZE])
{
    rtSha256FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA256_HASH_SIZE);
    RT_ZERO(pCtx->AltPrivate.auH);
}
RT_EXPORT_SYMBOL(RTSha256Final);


RTDECL(void) RTSha256(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA256_HASH_SIZE])
{
    RTSHA256CONTEXT Ctx;
    RTSha256Init(&Ctx);
    RTSha256Update(&Ctx, pvBuf, cbBuf);
    RTSha256Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha256);


RTDECL(bool) RTSha256Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA256_HASH_SIZE])
{
    RTSHA256CONTEXT Ctx;
    RTSha256Init(&Ctx);
    RTSha256Update(&Ctx, pvBuf, cbBuf);
    rtSha256FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA256_HASH_SIZE) == 0;

    RT_ZERO(Ctx.AltPrivate.auH);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha256Check);



/*
 * SHA-224 is just SHA-256 with different initial values an a truncated result.
 */

RTDECL(void) RTSha224Init(PRTSHA224CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage = 0;
    pCtx->AltPrivate.auH[0] = UINT32_C(0xc1059ed8);
    pCtx->AltPrivate.auH[1] = UINT32_C(0x367cd507);
    pCtx->AltPrivate.auH[2] = UINT32_C(0x3070dd17);
    pCtx->AltPrivate.auH[3] = UINT32_C(0xf70e5939);
    pCtx->AltPrivate.auH[4] = UINT32_C(0xffc00b31);
    pCtx->AltPrivate.auH[5] = UINT32_C(0x68581511);
    pCtx->AltPrivate.auH[6] = UINT32_C(0x64f98fa7);
    pCtx->AltPrivate.auH[7] = UINT32_C(0xbefa4fa4);
}
RT_EXPORT_SYMBOL(RTSha224Init);


RTDECL(void) RTSha224Update(PRTSHA224CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    RTSha256Update(pCtx, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha224Update);


RTDECL(void) RTSha224Final(PRTSHA224CONTEXT pCtx, uint8_t pabDigest[RTSHA224_HASH_SIZE])
{
    rtSha256FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA224_HASH_SIZE);
    RT_ZERO(pCtx->AltPrivate.auH);
}
RT_EXPORT_SYMBOL(RTSha224Final);


RTDECL(void) RTSha224(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA224_HASH_SIZE])
{
    RTSHA224CONTEXT Ctx;
    RTSha224Init(&Ctx);
    RTSha224Update(&Ctx, pvBuf, cbBuf);
    RTSha224Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha224);


RTDECL(bool) RTSha224Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA224_HASH_SIZE])
{
    RTSHA224CONTEXT Ctx;
    RTSha224Init(&Ctx);
    RTSha224Update(&Ctx, pvBuf, cbBuf);
    rtSha256FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA224_HASH_SIZE) == 0;

    RT_ZERO(Ctx.AltPrivate.auH);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha224Check);

