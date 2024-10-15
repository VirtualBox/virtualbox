/* $Id: alt-sha512.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-512 and SHA-384 hash functions, Alternative Implementation.
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
/** The SHA-512 block size (in bytes). */
#define RTSHA512_BLOCK_SIZE   128U

/** Enables the unrolled code. */
#define RTSHA512_UNROLLED 1


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>


/** Our private context structure. */
typedef struct RTSHA512ALTPRIVATECTX
{
    /** The W array.
     * Buffering happens in the first 16 words, converted from big endian to host
     * endian immediately before processing.  The amount of buffered data is kept
     * in the 6 least significant bits of cbMessage. */
    uint64_t    auW[80];
    /** The message length (in bytes). */
    RTUINT128U  cbMessage;
    /** The 8 hash values. */
    uint64_t    auH[8];
} RTSHA512ALTPRIVATECTX;

#define RT_SHA512_PRIVATE_ALT_CONTEXT
#include <iprt/sha.h>


AssertCompile(RT_SIZEOFMEMB(RTSHA512CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTSHA512CONTEXT, AltPrivate));
AssertCompileMemberSize(RTSHA512ALTPRIVATECTX, auH, RTSHA512_HASH_SIZE);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef RTSHA512_UNROLLED
/** The K constants. */
static uint64_t const g_auKs[] =
{
    UINT64_C(0x428a2f98d728ae22), UINT64_C(0x7137449123ef65cd), UINT64_C(0xb5c0fbcfec4d3b2f), UINT64_C(0xe9b5dba58189dbbc),
    UINT64_C(0x3956c25bf348b538), UINT64_C(0x59f111f1b605d019), UINT64_C(0x923f82a4af194f9b), UINT64_C(0xab1c5ed5da6d8118),
    UINT64_C(0xd807aa98a3030242), UINT64_C(0x12835b0145706fbe), UINT64_C(0x243185be4ee4b28c), UINT64_C(0x550c7dc3d5ffb4e2),
    UINT64_C(0x72be5d74f27b896f), UINT64_C(0x80deb1fe3b1696b1), UINT64_C(0x9bdc06a725c71235), UINT64_C(0xc19bf174cf692694),
    UINT64_C(0xe49b69c19ef14ad2), UINT64_C(0xefbe4786384f25e3), UINT64_C(0x0fc19dc68b8cd5b5), UINT64_C(0x240ca1cc77ac9c65),
    UINT64_C(0x2de92c6f592b0275), UINT64_C(0x4a7484aa6ea6e483), UINT64_C(0x5cb0a9dcbd41fbd4), UINT64_C(0x76f988da831153b5),
    UINT64_C(0x983e5152ee66dfab), UINT64_C(0xa831c66d2db43210), UINT64_C(0xb00327c898fb213f), UINT64_C(0xbf597fc7beef0ee4),
    UINT64_C(0xc6e00bf33da88fc2), UINT64_C(0xd5a79147930aa725), UINT64_C(0x06ca6351e003826f), UINT64_C(0x142929670a0e6e70),
    UINT64_C(0x27b70a8546d22ffc), UINT64_C(0x2e1b21385c26c926), UINT64_C(0x4d2c6dfc5ac42aed), UINT64_C(0x53380d139d95b3df),
    UINT64_C(0x650a73548baf63de), UINT64_C(0x766a0abb3c77b2a8), UINT64_C(0x81c2c92e47edaee6), UINT64_C(0x92722c851482353b),
    UINT64_C(0xa2bfe8a14cf10364), UINT64_C(0xa81a664bbc423001), UINT64_C(0xc24b8b70d0f89791), UINT64_C(0xc76c51a30654be30),
    UINT64_C(0xd192e819d6ef5218), UINT64_C(0xd69906245565a910), UINT64_C(0xf40e35855771202a), UINT64_C(0x106aa07032bbd1b8),
    UINT64_C(0x19a4c116b8d2d0c8), UINT64_C(0x1e376c085141ab53), UINT64_C(0x2748774cdf8eeb99), UINT64_C(0x34b0bcb5e19b48a8),
    UINT64_C(0x391c0cb3c5c95a63), UINT64_C(0x4ed8aa4ae3418acb), UINT64_C(0x5b9cca4f7763e373), UINT64_C(0x682e6ff3d6b2b8a3),
    UINT64_C(0x748f82ee5defb2fc), UINT64_C(0x78a5636f43172f60), UINT64_C(0x84c87814a1f0ab72), UINT64_C(0x8cc702081a6439ec),
    UINT64_C(0x90befffa23631e28), UINT64_C(0xa4506cebde82bde9), UINT64_C(0xbef9a3f7b2c67915), UINT64_C(0xc67178f2e372532b),
    UINT64_C(0xca273eceea26619c), UINT64_C(0xd186b8c721c0c207), UINT64_C(0xeada7dd6cde0eb1e), UINT64_C(0xf57d4f7fee6ed178),
    UINT64_C(0x06f067aa72176fba), UINT64_C(0x0a637dc5a2c898a6), UINT64_C(0x113f9804bef90dae), UINT64_C(0x1b710b35131c471b),
    UINT64_C(0x28db77f523047d84), UINT64_C(0x32caab7b40c72493), UINT64_C(0x3c9ebe0a15c9bebc), UINT64_C(0x431d67c49c100d4c),
    UINT64_C(0x4cc5d4becb3e42b6), UINT64_C(0x597f299cfc657e2a), UINT64_C(0x5fcb6fab3ad6faec), UINT64_C(0x6c44198c4a475817),
};
#endif /* !RTSHA512_UNROLLED */



RTDECL(void) RTSha512Init(PRTSHA512CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage.s.Lo = 0;
    pCtx->AltPrivate.cbMessage.s.Hi = 0;
    pCtx->AltPrivate.auH[0] = UINT64_C(0x6a09e667f3bcc908);
    pCtx->AltPrivate.auH[1] = UINT64_C(0xbb67ae8584caa73b);
    pCtx->AltPrivate.auH[2] = UINT64_C(0x3c6ef372fe94f82b);
    pCtx->AltPrivate.auH[3] = UINT64_C(0xa54ff53a5f1d36f1);
    pCtx->AltPrivate.auH[4] = UINT64_C(0x510e527fade682d1);
    pCtx->AltPrivate.auH[5] = UINT64_C(0x9b05688c2b3e6c1f);
    pCtx->AltPrivate.auH[6] = UINT64_C(0x1f83d9abfb41bd6b);
    pCtx->AltPrivate.auH[7] = UINT64_C(0x5be0cd19137e2179);
}
RT_EXPORT_SYMBOL(RTSha512Init);


/** Function 4.8. */
DECL_FORCE_INLINE(uint64_t) rtSha512Ch(uint64_t uX, uint64_t uY, uint64_t uZ)
{
#if 1
    /* Optimization that saves one operation and probably a temporary variable. */
    uint64_t uResult = uY;
    uResult ^= uZ;
    uResult &= uX;
    uResult ^= uZ;
    return uResult;
#else
    /* The original. */
    uint64_t uResult = uX & uY;
    uResult ^= ~uX & uZ;
    return uResult;
#endif
}


/** Function 4.9. */
DECL_FORCE_INLINE(uint64_t) rtSha512Maj(uint64_t uX, uint64_t uY, uint64_t uZ)
{
#if 1
    /* Optimization that save one operation and probably a temporary variable. */
    uint64_t uResult = uY;
    uResult ^= uZ;
    uResult &= uX;
    uResult ^= uY & uZ;
    return uResult;
#else
    /* The original. */
    uint64_t uResult = uX & uY;
    uResult ^= uX & uZ;
    uResult ^= uY & uZ;
    return uResult;
#endif
}


/** Function 4.10. */
DECL_FORCE_INLINE(uint64_t) rtSha512CapitalSigma0(uint64_t uX)
{
    uint64_t uResult = uX = ASMRotateRightU64(uX, 28);
    uX = ASMRotateRightU64(uX, 34 - 28);
    uResult ^= uX;
    uX = ASMRotateRightU64(uX, 39 - 34);
    uResult ^= uX;
    return uResult;
}


/** Function 4.11. */
DECL_FORCE_INLINE(uint64_t) rtSha512CapitalSigma1(uint64_t uX)
{
    uint64_t uResult = uX = ASMRotateRightU64(uX, 14);
    uX = ASMRotateRightU64(uX, 18 - 14);
    uResult ^= uX;
    uX = ASMRotateRightU64(uX, 41 - 18);
    uResult ^= uX;
    return uResult;
}


/** Function 4.12. */
DECL_FORCE_INLINE(uint64_t) rtSha512SmallSigma0(uint64_t uX)
{
    uint64_t uResult = uX >> 7;
    uX = ASMRotateRightU64(uX, 1);
    uResult ^= uX;
    uX = ASMRotateRightU64(uX, 8 - 1);
    uResult ^= uX;
    return uResult;
}


/** Function 4.13. */
DECL_FORCE_INLINE(uint64_t) rtSha512SmallSigma1(uint64_t uX)
{
    uint64_t uResult = uX >> 6;
    uX = ASMRotateRightU64(uX, 19);
    uResult ^= uX;
    uX = ASMRotateRightU64(uX, 61 - 19);
    uResult ^= uX;
    return uResult;
}


/**
 * Initializes the auW array from the specfied input block.
 *
 * @param   pCtx                The SHA-512 context.
 * @param   pbBlock             The block.  Must be 64-bit aligned.
 */
DECLINLINE(void) rtSha512BlockInit(PRTSHA512CONTEXT pCtx, uint8_t const *pbBlock)
{
#ifdef RTSHA512_UNROLLED
    uint64_t const *puSrc = (uint64_t const *)pbBlock;
    uint64_t       *puW   = &pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puSrc & 7));
    Assert(!((uintptr_t)puW & 7));

    /* Copy and byte-swap the block. Initializing the rest of the Ws are done
       in the processing loop. */
# ifdef RT_LITTLE_ENDIAN
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);

    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);

    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);

    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
    *puW++ = ASMByteSwapU64(*puSrc++);
# else
    memcpy(puW, puSrc, RTSHA512_BLOCK_SIZE);
# endif

#else  /* !RTSHA512_UNROLLED */

    uint64_t const *pu32Block = (uint64_t const *)pbBlock;
    Assert(!((uintptr_t)pu32Block & 3));

    unsigned iWord;
    for (iWord = 0; iWord < 16; iWord++)
        pCtx->AltPrivate.auW[iWord] = RT_BE2H_U64(pu32Block[iWord]);

    for (; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint64_t u64 = rtSha512SmallSigma1(pCtx->AltPrivate.auW[iWord - 2]);
        u64         += rtSha512SmallSigma0(pCtx->AltPrivate.auW[iWord - 15]);
        u64         += pCtx->AltPrivate.auW[iWord - 7];
        u64         += pCtx->AltPrivate.auW[iWord - 16];
        pCtx->AltPrivate.auW[iWord] = u64;
    }
#endif /* !RTSHA512_UNROLLED */
}


/**
 * Initializes the auW array from data buffered in the first part of the array.
 *
 * @param   pCtx                The SHA-512 context.
 */
DECLINLINE(void) rtSha512BlockInitBuffered(PRTSHA512CONTEXT pCtx)
{
#ifdef RTSHA512_UNROLLED
    uint64_t *puW = &pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puW & 7));

    /* Do the byte swap if necessary. Initializing the rest of the Ws are done
       in the processing loop. */
# ifdef RT_LITTLE_ENDIAN
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;

    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;

    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;

    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
    *puW = ASMByteSwapU64(*puW); puW++;
# endif

#else  /* !RTSHA512_UNROLLED */

    unsigned iWord;
    for (iWord = 0; iWord < 16; iWord++)
        pCtx->AltPrivate.auW[iWord] = RT_BE2H_U64(pCtx->AltPrivate.auW[iWord]);

    for (; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint64_t u64 = rtSha512SmallSigma1(pCtx->AltPrivate.auW[iWord - 2]);
        u64         += rtSha512SmallSigma0(pCtx->AltPrivate.auW[iWord - 15]);
        u64         += pCtx->AltPrivate.auW[iWord - 7];
        u64         += pCtx->AltPrivate.auW[iWord - 16];
        pCtx->AltPrivate.auW[iWord] = u64;
    }
#endif /* !RTSHA512_UNROLLED */
}


/**
 * Process the current block.
 *
 * Requires one of the rtSha512BlockInit functions to be called first.
 *
 * @param   pCtx                The SHA-512 context.
 */
static void rtSha512BlockProcess(PRTSHA512CONTEXT pCtx)
{
    uint64_t uA = pCtx->AltPrivate.auH[0];
    uint64_t uB = pCtx->AltPrivate.auH[1];
    uint64_t uC = pCtx->AltPrivate.auH[2];
    uint64_t uD = pCtx->AltPrivate.auH[3];
    uint64_t uE = pCtx->AltPrivate.auH[4];
    uint64_t uF = pCtx->AltPrivate.auH[5];
    uint64_t uG = pCtx->AltPrivate.auH[6];
    uint64_t uH = pCtx->AltPrivate.auH[7];

#ifdef RTSHA512_UNROLLED
    uint64_t *puW = &pCtx->AltPrivate.auW[0];
# define RTSHA512_BODY(a_iWord, a_uK, a_uA, a_uB, a_uC, a_uD, a_uE, a_uF, a_uG, a_uH) \
        do { \
            if ((a_iWord) < 16) \
                a_uH += *puW++; \
            else \
            { \
                uint64_t u64 = puW[-16]; \
                u64 += rtSha512SmallSigma0(puW[-15]); \
                u64 += puW[-7]; \
                u64 += rtSha512SmallSigma1(puW[-2]); \
                if (a_iWord < 80-2) *puW++ = u64; else puW++; \
                a_uH += u64; \
            } \
            \
            a_uH += rtSha512CapitalSigma1(a_uE); \
            a_uH += a_uK; \
            a_uH += rtSha512Ch(a_uE, a_uF, a_uG); \
            a_uD += a_uH; \
            \
            a_uH += rtSha512CapitalSigma0(a_uA); \
            a_uH += rtSha512Maj(a_uA, a_uB, a_uC); \
        } while (0)
# define RTSHA512_EIGHT(a_uK0, a_uK1, a_uK2, a_uK3, a_uK4, a_uK5, a_uK6, a_uK7, a_iFirst) \
        do { \
            RTSHA512_BODY(a_iFirst + 0, a_uK0, uA, uB, uC, uD, uE, uF, uG, uH); \
            RTSHA512_BODY(a_iFirst + 1, a_uK1, uH, uA, uB, uC, uD, uE, uF, uG); \
            RTSHA512_BODY(a_iFirst + 2, a_uK2, uG, uH, uA, uB, uC, uD, uE, uF); \
            RTSHA512_BODY(a_iFirst + 3, a_uK3, uF, uG, uH, uA, uB, uC, uD, uE); \
            RTSHA512_BODY(a_iFirst + 4, a_uK4, uE, uF, uG, uH, uA, uB, uC, uD); \
            RTSHA512_BODY(a_iFirst + 5, a_uK5, uD, uE, uF, uG, uH, uA, uB, uC); \
            RTSHA512_BODY(a_iFirst + 6, a_uK6, uC, uD, uE, uF, uG, uH, uA, uB); \
            RTSHA512_BODY(a_iFirst + 7, a_uK7, uB, uC, uD, uE, uF, uG, uH, uA); \
        } while (0)
    RTSHA512_EIGHT(UINT64_C(0x428a2f98d728ae22), UINT64_C(0x7137449123ef65cd), UINT64_C(0xb5c0fbcfec4d3b2f), UINT64_C(0xe9b5dba58189dbbc),
                   UINT64_C(0x3956c25bf348b538), UINT64_C(0x59f111f1b605d019), UINT64_C(0x923f82a4af194f9b), UINT64_C(0xab1c5ed5da6d8118),
                   0);
    RTSHA512_EIGHT(UINT64_C(0xd807aa98a3030242), UINT64_C(0x12835b0145706fbe), UINT64_C(0x243185be4ee4b28c), UINT64_C(0x550c7dc3d5ffb4e2),
                   UINT64_C(0x72be5d74f27b896f), UINT64_C(0x80deb1fe3b1696b1), UINT64_C(0x9bdc06a725c71235), UINT64_C(0xc19bf174cf692694),
                   8);
    RTSHA512_EIGHT(UINT64_C(0xe49b69c19ef14ad2), UINT64_C(0xefbe4786384f25e3), UINT64_C(0x0fc19dc68b8cd5b5), UINT64_C(0x240ca1cc77ac9c65),
                   UINT64_C(0x2de92c6f592b0275), UINT64_C(0x4a7484aa6ea6e483), UINT64_C(0x5cb0a9dcbd41fbd4), UINT64_C(0x76f988da831153b5),
                   16);
    RTSHA512_EIGHT(UINT64_C(0x983e5152ee66dfab), UINT64_C(0xa831c66d2db43210), UINT64_C(0xb00327c898fb213f), UINT64_C(0xbf597fc7beef0ee4),
                   UINT64_C(0xc6e00bf33da88fc2), UINT64_C(0xd5a79147930aa725), UINT64_C(0x06ca6351e003826f), UINT64_C(0x142929670a0e6e70),
                   24);
    RTSHA512_EIGHT(UINT64_C(0x27b70a8546d22ffc), UINT64_C(0x2e1b21385c26c926), UINT64_C(0x4d2c6dfc5ac42aed), UINT64_C(0x53380d139d95b3df),
                   UINT64_C(0x650a73548baf63de), UINT64_C(0x766a0abb3c77b2a8), UINT64_C(0x81c2c92e47edaee6), UINT64_C(0x92722c851482353b),
                   32);
    RTSHA512_EIGHT(UINT64_C(0xa2bfe8a14cf10364), UINT64_C(0xa81a664bbc423001), UINT64_C(0xc24b8b70d0f89791), UINT64_C(0xc76c51a30654be30),
                   UINT64_C(0xd192e819d6ef5218), UINT64_C(0xd69906245565a910), UINT64_C(0xf40e35855771202a), UINT64_C(0x106aa07032bbd1b8),
                   40);
    RTSHA512_EIGHT(UINT64_C(0x19a4c116b8d2d0c8), UINT64_C(0x1e376c085141ab53), UINT64_C(0x2748774cdf8eeb99), UINT64_C(0x34b0bcb5e19b48a8),
                   UINT64_C(0x391c0cb3c5c95a63), UINT64_C(0x4ed8aa4ae3418acb), UINT64_C(0x5b9cca4f7763e373), UINT64_C(0x682e6ff3d6b2b8a3),
                   48);
    RTSHA512_EIGHT(UINT64_C(0x748f82ee5defb2fc), UINT64_C(0x78a5636f43172f60), UINT64_C(0x84c87814a1f0ab72), UINT64_C(0x8cc702081a6439ec),
                   UINT64_C(0x90befffa23631e28), UINT64_C(0xa4506cebde82bde9), UINT64_C(0xbef9a3f7b2c67915), UINT64_C(0xc67178f2e372532b),
                   56);
    RTSHA512_EIGHT(UINT64_C(0xca273eceea26619c), UINT64_C(0xd186b8c721c0c207), UINT64_C(0xeada7dd6cde0eb1e), UINT64_C(0xf57d4f7fee6ed178),
                   UINT64_C(0x06f067aa72176fba), UINT64_C(0x0a637dc5a2c898a6), UINT64_C(0x113f9804bef90dae), UINT64_C(0x1b710b35131c471b),
                   64);
    RTSHA512_EIGHT(UINT64_C(0x28db77f523047d84), UINT64_C(0x32caab7b40c72493), UINT64_C(0x3c9ebe0a15c9bebc), UINT64_C(0x431d67c49c100d4c),
                   UINT64_C(0x4cc5d4becb3e42b6), UINT64_C(0x597f299cfc657e2a), UINT64_C(0x5fcb6fab3ad6faec), UINT64_C(0x6c44198c4a475817),
                   72);
#else
    for (unsigned iWord = 0; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint64_t uT1 = uH;
        uT1 += rtSha512CapitalSigma1(uE);
        uT1 += rtSha512Ch(uE, uF, uG);
        uT1 += g_auKs[iWord];
        uT1 += pCtx->AltPrivate.auW[iWord];

        uint64_t uT2 = rtSha512CapitalSigma0(uA);
        uT2 += rtSha512Maj(uA, uB, uC);

        uH = uG;
        uG = uF;
        uF = uE;
        uE = uD + uT1;
        uD = uC;
        uC = uB;
        uB = uA;
        uA = uT1 + uT2;
    }
#endif

    pCtx->AltPrivate.auH[0] += uA;
    pCtx->AltPrivate.auH[1] += uB;
    pCtx->AltPrivate.auH[2] += uC;
    pCtx->AltPrivate.auH[3] += uD;
    pCtx->AltPrivate.auH[4] += uE;
    pCtx->AltPrivate.auH[5] += uF;
    pCtx->AltPrivate.auH[6] += uG;
    pCtx->AltPrivate.auH[7] += uH;
}


RTDECL(void) RTSha512Update(PRTSHA512CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    Assert(pCtx->AltPrivate.cbMessage.s.Hi < UINT64_MAX / 8);
    uint8_t const *pbBuf = (uint8_t const *)pvBuf;

    /*
     * Deal with buffered bytes first.
     */
    size_t cbBuffered = (size_t)pCtx->AltPrivate.cbMessage.s.Lo & (RTSHA512_BLOCK_SIZE - 1U);
    if (cbBuffered)
    {
        size_t cbMissing = RTSHA512_BLOCK_SIZE - cbBuffered;
        if (cbBuf >= cbMissing)
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, pbBuf, cbMissing);
            pCtx->AltPrivate.cbMessage.s.Lo += cbMissing;
            if (!pCtx->AltPrivate.cbMessage.s.Lo)
                pCtx->AltPrivate.cbMessage.s.Hi++;
            pbBuf += cbMissing;
            cbBuf -= cbMissing;

            rtSha512BlockInitBuffered(pCtx);
            rtSha512BlockProcess(pCtx);
        }
        else
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, pbBuf, cbBuf);
            pCtx->AltPrivate.cbMessage.s.Lo += cbBuf;
            return;
        }
    }

    if (!((uintptr_t)pbBuf & 7))
    {
        /*
         * Process full blocks directly from the input buffer.
         */
        while (cbBuf >= RTSHA512_BLOCK_SIZE)
        {
            rtSha512BlockInit(pCtx, pbBuf);
            rtSha512BlockProcess(pCtx);

            pCtx->AltPrivate.cbMessage.s.Lo += RTSHA512_BLOCK_SIZE;
            if (!pCtx->AltPrivate.cbMessage.s.Lo)
                pCtx->AltPrivate.cbMessage.s.Hi++;
            pbBuf += RTSHA512_BLOCK_SIZE;
            cbBuf -= RTSHA512_BLOCK_SIZE;
        }
    }
    else
    {
        /*
         * Unaligned input, so buffer it.
         */
        while (cbBuf >= RTSHA512_BLOCK_SIZE)
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0], pbBuf, RTSHA512_BLOCK_SIZE);
            rtSha512BlockInitBuffered(pCtx);
            rtSha512BlockProcess(pCtx);

            pCtx->AltPrivate.cbMessage.s.Lo += RTSHA512_BLOCK_SIZE;
            if (!pCtx->AltPrivate.cbMessage.s.Lo)
                pCtx->AltPrivate.cbMessage.s.Hi++;
            pbBuf += RTSHA512_BLOCK_SIZE;
            cbBuf -= RTSHA512_BLOCK_SIZE;
        }
    }

    /*
     * Stash any remaining bytes into the context buffer.
     */
    if (cbBuf > 0)
    {
        memcpy((uint8_t *)&pCtx->AltPrivate.auW[0], pbBuf, cbBuf);
        pCtx->AltPrivate.cbMessage.s.Lo += cbBuf;
        if (!pCtx->AltPrivate.cbMessage.s.Lo)
            pCtx->AltPrivate.cbMessage.s.Hi++;
    }
}
RT_EXPORT_SYMBOL(RTSha512Update);


/**
 * Internal worker for RTSha512Final and RTSha384Final that finalizes the
 * computation but does not copy out the hash value.
 *
 * @param   pCtx                The SHA-512 context.
 */
static void rtSha512FinalInternal(PRTSHA512CONTEXT pCtx)
{
    Assert(pCtx->AltPrivate.cbMessage.s.Hi < UINT64_MAX / 8);

    /*
     * Complete the message by adding a single bit (0x80), padding till
     * the next 448-bit boundrary, the add the message length.
     */
    RTUINT128U cMessageBits = pCtx->AltPrivate.cbMessage;
    cMessageBits.s.Hi <<= 3;
    cMessageBits.s.Hi |= cMessageBits.s.Lo >> 61;
    cMessageBits.s.Lo <<= 3;

    unsigned cbMissing = RTSHA512_BLOCK_SIZE - ((unsigned)pCtx->AltPrivate.cbMessage.s.Lo & (RTSHA512_BLOCK_SIZE - 1U));
    static uint8_t const s_abSingleBitAndSomePadding[20] =
    {   0x80, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,};
    if (cbMissing < 1U + 16U)
        /* Less than 64+16 bits left in the current block, force a new block. */
        RTSha512Update(pCtx, &s_abSingleBitAndSomePadding, sizeof(s_abSingleBitAndSomePadding));
    else
        RTSha512Update(pCtx, &s_abSingleBitAndSomePadding, 1);

    unsigned cbBuffered = (unsigned)pCtx->AltPrivate.cbMessage.s.Lo & (RTSHA512_BLOCK_SIZE - 1U);
    cbMissing = RTSHA512_BLOCK_SIZE - cbBuffered;
    Assert(cbMissing >= 16);
    memset((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, 0, cbMissing - 16);

    pCtx->AltPrivate.auW[14] = RT_H2BE_U64(cMessageBits.s.Hi);
    pCtx->AltPrivate.auW[15] = RT_H2BE_U64(cMessageBits.s.Lo);

    /*
     * Process the last buffered block constructed/completed above.
     */
    rtSha512BlockInitBuffered(pCtx);
    rtSha512BlockProcess(pCtx);

    /*
     * Convert the byte order of the hash words and we're done.
     */
    pCtx->AltPrivate.auH[0] = RT_H2BE_U64(pCtx->AltPrivate.auH[0]);
    pCtx->AltPrivate.auH[1] = RT_H2BE_U64(pCtx->AltPrivate.auH[1]);
    pCtx->AltPrivate.auH[2] = RT_H2BE_U64(pCtx->AltPrivate.auH[2]);
    pCtx->AltPrivate.auH[3] = RT_H2BE_U64(pCtx->AltPrivate.auH[3]);
    pCtx->AltPrivate.auH[4] = RT_H2BE_U64(pCtx->AltPrivate.auH[4]);
    pCtx->AltPrivate.auH[5] = RT_H2BE_U64(pCtx->AltPrivate.auH[5]);
    pCtx->AltPrivate.auH[6] = RT_H2BE_U64(pCtx->AltPrivate.auH[6]);
    pCtx->AltPrivate.auH[7] = RT_H2BE_U64(pCtx->AltPrivate.auH[7]);

    RT_ZERO(pCtx->AltPrivate.auW);
    pCtx->AltPrivate.cbMessage.s.Lo = UINT64_MAX;
    pCtx->AltPrivate.cbMessage.s.Hi = UINT64_MAX;
}
RT_EXPORT_SYMBOL(RTSha512Final);


RTDECL(void) RTSha512Final(PRTSHA512CONTEXT pCtx, uint8_t pabDigest[RTSHA512_HASH_SIZE])
{
    rtSha512FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA512_HASH_SIZE);
    RT_ZERO(pCtx->AltPrivate.auH);
}
RT_EXPORT_SYMBOL(RTSha512Final);


RTDECL(void) RTSha512(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA512_HASH_SIZE])
{
    RTSHA512CONTEXT Ctx;
    RTSha512Init(&Ctx);
    RTSha512Update(&Ctx, pvBuf, cbBuf);
    RTSha512Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha512);


RTDECL(bool) RTSha512Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA512_HASH_SIZE])
{
    RTSHA512CONTEXT Ctx;
    RTSha512Init(&Ctx);
    RTSha512Update(&Ctx, pvBuf, cbBuf);
    rtSha512FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA512_HASH_SIZE) == 0;

    RT_ZERO(Ctx.AltPrivate.auH);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha512Check);



/*
 * SHA-384 is just SHA-512 with different initial values an a truncated result.
 */

RTDECL(void) RTSha384Init(PRTSHA384CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage.s.Lo = 0;
    pCtx->AltPrivate.cbMessage.s.Hi = 0;
    pCtx->AltPrivate.auH[0] = UINT64_C(0xcbbb9d5dc1059ed8);
    pCtx->AltPrivate.auH[1] = UINT64_C(0x629a292a367cd507);
    pCtx->AltPrivate.auH[2] = UINT64_C(0x9159015a3070dd17);
    pCtx->AltPrivate.auH[3] = UINT64_C(0x152fecd8f70e5939);
    pCtx->AltPrivate.auH[4] = UINT64_C(0x67332667ffc00b31);
    pCtx->AltPrivate.auH[5] = UINT64_C(0x8eb44a8768581511);
    pCtx->AltPrivate.auH[6] = UINT64_C(0xdb0c2e0d64f98fa7);
    pCtx->AltPrivate.auH[7] = UINT64_C(0x47b5481dbefa4fa4);
}
RT_EXPORT_SYMBOL(RTSha384Init);


RTDECL(void) RTSha384Update(PRTSHA384CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    RTSha512Update(pCtx, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha384Update);


RTDECL(void) RTSha384Final(PRTSHA384CONTEXT pCtx, uint8_t pabDigest[RTSHA384_HASH_SIZE])
{
    rtSha512FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA384_HASH_SIZE);
    RT_ZERO(pCtx->AltPrivate.auH);
}
RT_EXPORT_SYMBOL(RTSha384Final);


RTDECL(void) RTSha384(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA384_HASH_SIZE])
{
    RTSHA384CONTEXT Ctx;
    RTSha384Init(&Ctx);
    RTSha384Update(&Ctx, pvBuf, cbBuf);
    RTSha384Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha384);


RTDECL(bool) RTSha384Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA384_HASH_SIZE])
{
    RTSHA384CONTEXT Ctx;
    RTSha384Init(&Ctx);
    RTSha384Update(&Ctx, pvBuf, cbBuf);
    rtSha512FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA384_HASH_SIZE) == 0;

    RT_ZERO(Ctx.AltPrivate.auH);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha384Check);


/*
 * SHA-512/224 is just SHA-512 with different initial values an a truncated result.
 */

RTDECL(void) RTSha512t224Init(PRTSHA512T224CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage.s.Lo = 0;
    pCtx->AltPrivate.cbMessage.s.Hi = 0;
    pCtx->AltPrivate.auH[0] = UINT64_C(0x8c3d37c819544da2);
    pCtx->AltPrivate.auH[1] = UINT64_C(0x73e1996689dcd4d6);
    pCtx->AltPrivate.auH[2] = UINT64_C(0x1dfab7ae32ff9c82);
    pCtx->AltPrivate.auH[3] = UINT64_C(0x679dd514582f9fcf);
    pCtx->AltPrivate.auH[4] = UINT64_C(0x0f6d2b697bd44da8);
    pCtx->AltPrivate.auH[5] = UINT64_C(0x77e36f7304c48942);
    pCtx->AltPrivate.auH[6] = UINT64_C(0x3f9d85a86a1d36c8);
    pCtx->AltPrivate.auH[7] = UINT64_C(0x1112e6ad91d692a1);
}
RT_EXPORT_SYMBOL(RTSha512t224Init);


RTDECL(void) RTSha512t224Update(PRTSHA512T224CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    RTSha512Update(pCtx, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha512t224Update);


RTDECL(void) RTSha512t224Final(PRTSHA512T224CONTEXT pCtx, uint8_t pabDigest[RTSHA512T224_HASH_SIZE])
{
    rtSha512FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA512T224_HASH_SIZE);
    RT_ZERO(pCtx->AltPrivate.auH);
}
RT_EXPORT_SYMBOL(RTSha512t224Final);


RTDECL(void) RTSha512t224(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA512T224_HASH_SIZE])
{
    RTSHA512T224CONTEXT Ctx;
    RTSha512t224Init(&Ctx);
    RTSha512t224Update(&Ctx, pvBuf, cbBuf);
    RTSha512t224Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha512t224);


RTDECL(bool) RTSha512t224Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA512T224_HASH_SIZE])
{
    RTSHA512T224CONTEXT Ctx;
    RTSha512t224Init(&Ctx);
    RTSha512t224Update(&Ctx, pvBuf, cbBuf);
    rtSha512FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA512T224_HASH_SIZE) == 0;

    RT_ZERO(Ctx.AltPrivate.auH);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha512t224Check);


/*
 * SHA-512/256 is just SHA-512 with different initial values an a truncated result.
 */

RTDECL(void) RTSha512t256Init(PRTSHA512T256CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage.s.Lo = 0;
    pCtx->AltPrivate.cbMessage.s.Hi = 0;
    pCtx->AltPrivate.auH[0] = UINT64_C(0x22312194fc2bf72c);
    pCtx->AltPrivate.auH[1] = UINT64_C(0x9f555fa3c84c64c2);
    pCtx->AltPrivate.auH[2] = UINT64_C(0x2393b86b6f53b151);
    pCtx->AltPrivate.auH[3] = UINT64_C(0x963877195940eabd);
    pCtx->AltPrivate.auH[4] = UINT64_C(0x96283ee2a88effe3);
    pCtx->AltPrivate.auH[5] = UINT64_C(0xbe5e1e2553863992);
    pCtx->AltPrivate.auH[6] = UINT64_C(0x2b0199fc2c85b8aa);
    pCtx->AltPrivate.auH[7] = UINT64_C(0x0eb72ddc81c52ca2);
}
RT_EXPORT_SYMBOL(RTSha512t256Init);


RTDECL(void) RTSha512t256Update(PRTSHA512T256CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    RTSha512Update(pCtx, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha512t256Update);


RTDECL(void) RTSha512t256Final(PRTSHA512T256CONTEXT pCtx, uint8_t pabDigest[RTSHA512T256_HASH_SIZE])
{
    rtSha512FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA512T256_HASH_SIZE);
    RT_ZERO(pCtx->AltPrivate.auH);
}
RT_EXPORT_SYMBOL(RTSha512t256Final);


RTDECL(void) RTSha512t256(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA512T256_HASH_SIZE])
{
    RTSHA512T256CONTEXT Ctx;
    RTSha512t256Init(&Ctx);
    RTSha512t256Update(&Ctx, pvBuf, cbBuf);
    RTSha512t256Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha512t256);


RTDECL(bool) RTSha512t256Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA512T256_HASH_SIZE])
{
    RTSHA512T256CONTEXT Ctx;
    RTSha512t256Init(&Ctx);
    RTSha512t256Update(&Ctx, pvBuf, cbBuf);
    rtSha512FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA512T256_HASH_SIZE) == 0;

    RT_ZERO(Ctx.AltPrivate.auH);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha512t256Check);

