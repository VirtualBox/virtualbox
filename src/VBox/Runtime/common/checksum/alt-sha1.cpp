/* $Id: alt-sha1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-1 hash functions, Alternative Implementation.
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
/** The SHA-1 block size (in bytes). */
#define RTSHA1_BLOCK_SIZE   64U

/** Enables the unrolled code. */
#define RTSHA1_UNROLLED 1


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/types.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>


/** Our private context structure. */
typedef struct RTSHA1ALTPRIVATECTX
{
    /** The W array.
     * Buffering happens in the first 16 words, converted from big endian to host
     * endian immediately before processing.  The amount of buffered data is kept
     * in the 6 least significant bits of cbMessage. */
    uint32_t    auW[80];
    /** The message length (in bytes). */
    uint64_t    cbMessage;

    /** The 5 hash values. */
    uint32_t    auH[5];
} RTSHA1ALTPRIVATECTX;

#define RT_SHA1_PRIVATE_ALT_CONTEXT
#include <iprt/sha.h>


AssertCompile(RT_SIZEOFMEMB(RTSHA1CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTSHA1CONTEXT, AltPrivate));
AssertCompileMemberSize(RTSHA1ALTPRIVATECTX, auH, RTSHA1_HASH_SIZE);




RTDECL(void) RTSha1Init(PRTSHA1CONTEXT pCtx)
{
    pCtx->AltPrivate.cbMessage = 0;
    pCtx->AltPrivate.auH[0] = UINT32_C(0x67452301);
    pCtx->AltPrivate.auH[1] = UINT32_C(0xefcdab89);
    pCtx->AltPrivate.auH[2] = UINT32_C(0x98badcfe);
    pCtx->AltPrivate.auH[3] = UINT32_C(0x10325476);
    pCtx->AltPrivate.auH[4] = UINT32_C(0xc3d2e1f0);
}
RT_EXPORT_SYMBOL(RTSha1Init);


/**
 * Initializes the auW array from the specfied input block.
 *
 * @param   pCtx                The SHA1 context.
 * @param   pbBlock             The block.  Must be 32-bit aligned.
 */
DECLINLINE(void) rtSha1BlockInit(PRTSHA1CONTEXT pCtx, uint8_t const *pbBlock)
{
#ifdef RTSHA1_UNROLLED
    uint32_t const *puSrc = (uint32_t const *)pbBlock;
    uint32_t       *puW   = &pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puSrc & 3));
    Assert(!((uintptr_t)puW & 3));

    /* Copy and byte-swap the block. Initializing the rest of the Ws are done
       in the processing loop. */
# ifdef RT_LITTLE_ENDIAN
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
# else
    memcpy(puW, puSrc, RTSHA1_BLOCK_SIZE);
# endif

#else  /* !RTSHA1_UNROLLED */
    uint32_t const *pu32Block = (uint32_t const *)pbBlock;
    Assert(!((uintptr_t)pu32Block & 3));

    unsigned iWord;
    for (iWord = 0; iWord < 16; iWord++)
        pCtx->AltPrivate.auW[iWord] = RT_BE2H_U32(pu32Block[iWord]);

    for (; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint32_t u32 = pCtx->AltPrivate.auW[iWord - 16];
        u32         ^= pCtx->AltPrivate.auW[iWord - 14];
        u32         ^= pCtx->AltPrivate.auW[iWord -  8];
        u32         ^= pCtx->AltPrivate.auW[iWord -  3];
        pCtx->AltPrivate.auW[iWord] = ASMRotateLeftU32(u32, 1);
    }
#endif /* !RTSHA1_UNROLLED */
}


/**
 * Initializes the auW array from data buffered in the first part of the array.
 *
 * @param   pCtx                The SHA1 context.
 */
DECLINLINE(void) rtSha1BlockInitBuffered(PRTSHA1CONTEXT pCtx)
{
#ifdef RTSHA1_UNROLLED
    uint32_t       *puW   = &pCtx->AltPrivate.auW[0];
    Assert(!((uintptr_t)puW & 3));

    /* Do the byte swap if necessary. Initializing the rest of the Ws are done
       in the processing loop. */
# ifdef RT_LITTLE_ENDIAN
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
# endif

#else  /* !RTSHA1_UNROLLED_INIT */
    unsigned iWord;
    for (iWord = 0; iWord < 16; iWord++)
        pCtx->AltPrivate.auW[iWord] = RT_BE2H_U32(pCtx->AltPrivate.auW[iWord]);

    for (; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint32_t u32 = pCtx->AltPrivate.auW[iWord - 16];
        u32         ^= pCtx->AltPrivate.auW[iWord - 14];
        u32         ^= pCtx->AltPrivate.auW[iWord -  8];
        u32         ^= pCtx->AltPrivate.auW[iWord -  3];
        pCtx->AltPrivate.auW[iWord] = ASMRotateLeftU32(u32, 1);
    }
#endif /* !RTSHA1_UNROLLED_INIT */
}


/** Function 4.1, Ch(x,y,z). */
DECL_FORCE_INLINE(uint32_t) rtSha1Ch(uint32_t uX, uint32_t uY, uint32_t uZ)
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


/** Function 4.1, Parity(x,y,z). */
DECL_FORCE_INLINE(uint32_t) rtSha1Parity(uint32_t uX, uint32_t uY, uint32_t uZ)
{
    uint32_t uResult = uX;
    uResult ^= uY;
    uResult ^= uZ;
    return uResult;
}


/** Function 4.1, Maj(x,y,z). */
DECL_FORCE_INLINE(uint32_t) rtSha1Maj(uint32_t uX, uint32_t uY, uint32_t uZ)
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
    uint32_t uResult = (uX & uY);
    uResult |= (uX & uZ);
    uResult |= (uY & uZ);
    return uResult;
#endif
}


/**
 * Process the current block.
 *
 * Requires one of the rtSha1BlockInit functions to be called first.
 *
 * @param   pCtx                The SHA1 context.
 */
static void rtSha1BlockProcess(PRTSHA1CONTEXT pCtx)
{
    uint32_t uA = pCtx->AltPrivate.auH[0];
    uint32_t uB = pCtx->AltPrivate.auH[1];
    uint32_t uC = pCtx->AltPrivate.auH[2];
    uint32_t uD = pCtx->AltPrivate.auH[3];
    uint32_t uE = pCtx->AltPrivate.auH[4];

#ifdef RTSHA1_UNROLLED
    /* This fully unrolled version will avoid the variable rotation by
       embedding it into the loop unrolling. */
    uint32_t *puW = &pCtx->AltPrivate.auW[0];
# define SHA1_BODY(a_iWord, a_uK, a_fnFt, a_uA, a_uB, a_uC, a_uD, a_uE) \
        do { \
            if (a_iWord < 16) \
                a_uE += *puW++; \
            else \
            { \
                uint32_t u32 = puW[-16]; \
                u32         ^= puW[-14]; \
                u32         ^= puW[-8]; \
                u32         ^= puW[-3]; \
                u32 = ASMRotateLeftU32(u32, 1); \
                *puW++ = u32; \
                a_uE += u32; \
            } \
            a_uE += (a_uK); \
            a_uE += ASMRotateLeftU32(a_uA, 5); \
            a_uE += a_fnFt(a_uB, a_uC, a_uD); \
            a_uB = ASMRotateLeftU32(a_uB, 30); \
        } while (0)
# define FIVE_ITERATIONS(a_iFirst, a_uK, a_fnFt) \
    do { \
        SHA1_BODY(a_iFirst + 0, a_uK, a_fnFt, uA, uB, uC, uD, uE); \
        SHA1_BODY(a_iFirst + 1, a_uK, a_fnFt, uE, uA, uB, uC, uD); \
        SHA1_BODY(a_iFirst + 2, a_uK, a_fnFt, uD, uE, uA, uB, uC); \
        SHA1_BODY(a_iFirst + 3, a_uK, a_fnFt, uC, uD, uE, uA, uB); \
        SHA1_BODY(a_iFirst + 4, a_uK, a_fnFt, uB, uC, uD, uE, uA); \
    } while (0)
# define TWENTY_ITERATIONS(a_iStart, a_uK, a_fnFt) \
    do { \
        FIVE_ITERATIONS(a_iStart +  0, a_uK, a_fnFt); \
        FIVE_ITERATIONS(a_iStart +  5, a_uK, a_fnFt); \
        FIVE_ITERATIONS(a_iStart + 10, a_uK, a_fnFt); \
        FIVE_ITERATIONS(a_iStart + 15, a_uK, a_fnFt); \
    } while (0)

    TWENTY_ITERATIONS( 0, UINT32_C(0x5a827999), rtSha1Ch);
    TWENTY_ITERATIONS(20, UINT32_C(0x6ed9eba1), rtSha1Parity);
    TWENTY_ITERATIONS(40, UINT32_C(0x8f1bbcdc), rtSha1Maj);
    TWENTY_ITERATIONS(60, UINT32_C(0xca62c1d6), rtSha1Parity);

#elif 1 /* Version avoiding the constant selection. */
    unsigned iWord = 0;
# define TWENTY_ITERATIONS(a_iWordStop, a_uK, a_uExprBCD) \
        for (; iWord < a_iWordStop; iWord++) \
        { \
            uint32_t uTemp = ASMRotateLeftU32(uA, 5); \
            uTemp += (a_uExprBCD); \
            uTemp += uE; \
            uTemp += pCtx->AltPrivate.auW[iWord]; \
            uTemp += (a_uK); \
            \
            uE = uD; \
            uD = uC; \
            uC = ASMRotateLeftU32(uB, 30); \
            uB = uA; \
            uA = uTemp; \
        } do { } while (0)
    TWENTY_ITERATIONS(20, UINT32_C(0x5a827999), rtSha1Ch(uB, uC, uD));
    TWENTY_ITERATIONS(40, UINT32_C(0x6ed9eba1), rtSha1Parity(uB, uC, uD));
    TWENTY_ITERATIONS(60, UINT32_C(0x8f1bbcdc), rtSha1Maj(uB, uC, uD));
    TWENTY_ITERATIONS(80, UINT32_C(0xca62c1d6), rtSha1Parity(uB, uC, uD));

#else /* Dead simple implementation. */
    for (unsigned iWord = 0; iWord < RT_ELEMENTS(pCtx->AltPrivate.auW); iWord++)
    {
        uint32_t uTemp = ASMRotateLeftU32(uA, 5);
        uTemp += uE;
        uTemp += pCtx->AltPrivate.auW[iWord];
        if (iWord <= 19)
        {
            uTemp += (uB & uC) | (~uB & uD);
            uTemp += UINT32_C(0x5a827999);
        }
        else if (iWord <= 39)
        {
            uTemp += uB ^ uC ^ uD;
            uTemp += UINT32_C(0x6ed9eba1);
        }
        else if (iWord <= 59)
        {
            uTemp += (uB & uC) | (uB & uD) | (uC & uD);
            uTemp += UINT32_C(0x8f1bbcdc);
        }
        else
        {
            uTemp += uB ^ uC ^ uD;
            uTemp += UINT32_C(0xca62c1d6);
        }

        uE = uD;
        uD = uC;
        uC = ASMRotateLeftU32(uB, 30);
        uB = uA;
        uA = uTemp;
    }
#endif

    pCtx->AltPrivate.auH[0] += uA;
    pCtx->AltPrivate.auH[1] += uB;
    pCtx->AltPrivate.auH[2] += uC;
    pCtx->AltPrivate.auH[3] += uD;
    pCtx->AltPrivate.auH[4] += uE;
}


RTDECL(void) RTSha1Update(PRTSHA1CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    Assert(pCtx->AltPrivate.cbMessage < UINT64_MAX / 2);
    uint8_t const *pbBuf = (uint8_t const *)pvBuf;

    /*
     * Deal with buffered bytes first.
     */
    size_t cbBuffered = (size_t)pCtx->AltPrivate.cbMessage & (RTSHA1_BLOCK_SIZE - 1U);
    if (cbBuffered)
    {
        size_t cbMissing = RTSHA1_BLOCK_SIZE - cbBuffered;
        if (cbBuf >= cbMissing)
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, pbBuf, cbMissing);
            pCtx->AltPrivate.cbMessage += cbMissing;
            pbBuf += cbMissing;
            cbBuf -= cbMissing;

            rtSha1BlockInitBuffered(pCtx);
            rtSha1BlockProcess(pCtx);
        }
        else
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, pbBuf, cbBuf);
            pCtx->AltPrivate.cbMessage += cbBuf;
            return;
        }
    }

    if (!((uintptr_t)pbBuf & 3))
    {
        /*
         * Process full blocks directly from the input buffer.
         */
        while (cbBuf >= RTSHA1_BLOCK_SIZE)
        {
            rtSha1BlockInit(pCtx, pbBuf);
            rtSha1BlockProcess(pCtx);

            pCtx->AltPrivate.cbMessage += RTSHA1_BLOCK_SIZE;
            pbBuf += RTSHA1_BLOCK_SIZE;
            cbBuf -= RTSHA1_BLOCK_SIZE;
        }
    }
    else
    {
        /*
         * Unaligned input, so buffer it.
         */
        while (cbBuf >= RTSHA1_BLOCK_SIZE)
        {
            memcpy((uint8_t *)&pCtx->AltPrivate.auW[0], pbBuf, RTSHA1_BLOCK_SIZE);
            rtSha1BlockInitBuffered(pCtx);
            rtSha1BlockProcess(pCtx);

            pCtx->AltPrivate.cbMessage += RTSHA1_BLOCK_SIZE;
            pbBuf += RTSHA1_BLOCK_SIZE;
            cbBuf -= RTSHA1_BLOCK_SIZE;
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
RT_EXPORT_SYMBOL(RTSha1Update);


static void rtSha1FinalInternal(PRTSHA1CONTEXT pCtx)
{
    Assert(pCtx->AltPrivate.cbMessage < UINT64_MAX / 2);

    /*
     * Complete the message by adding a single bit (0x80), padding till
     * the next 448-bit boundrary, the add the message length.
     */
    uint64_t const cMessageBits = pCtx->AltPrivate.cbMessage * 8;

    unsigned cbMissing = RTSHA1_BLOCK_SIZE - ((unsigned)pCtx->AltPrivate.cbMessage & (RTSHA1_BLOCK_SIZE - 1U));
    static uint8_t const s_abSingleBitAndSomePadding[12] =  { 0x80, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0, };
    if (cbMissing < 1U + 8U)
        /* Less than 64+8 bits left in the current block, force a new block. */
        RTSha1Update(pCtx, &s_abSingleBitAndSomePadding, sizeof(s_abSingleBitAndSomePadding));
    else
        RTSha1Update(pCtx, &s_abSingleBitAndSomePadding, 1);

    unsigned cbBuffered = (unsigned)pCtx->AltPrivate.cbMessage & (RTSHA1_BLOCK_SIZE - 1U);
    cbMissing = RTSHA1_BLOCK_SIZE - cbBuffered;
    Assert(cbMissing >= 8);
    memset((uint8_t *)&pCtx->AltPrivate.auW[0] + cbBuffered, 0, cbMissing - 8);

    *(uint64_t *)&pCtx->AltPrivate.auW[14] = RT_H2BE_U64(cMessageBits);

    /*
     * Process the last buffered block constructed/completed above.
     */
    rtSha1BlockInitBuffered(pCtx);
    rtSha1BlockProcess(pCtx);

    /*
     * Convert the byte order of the hash words and we're done.
     */
    pCtx->AltPrivate.auH[0] = RT_H2BE_U32(pCtx->AltPrivate.auH[0]);
    pCtx->AltPrivate.auH[1] = RT_H2BE_U32(pCtx->AltPrivate.auH[1]);
    pCtx->AltPrivate.auH[2] = RT_H2BE_U32(pCtx->AltPrivate.auH[2]);
    pCtx->AltPrivate.auH[3] = RT_H2BE_U32(pCtx->AltPrivate.auH[3]);
    pCtx->AltPrivate.auH[4] = RT_H2BE_U32(pCtx->AltPrivate.auH[4]);
}


DECLINLINE(void) rtSha1WipeCtx(PRTSHA1CONTEXT pCtx)
{
    RT_ZERO(pCtx->AltPrivate);
    pCtx->AltPrivate.cbMessage = UINT64_MAX;
}


RTDECL(void) RTSha1Final(PRTSHA1CONTEXT pCtx, uint8_t pabDigest[RTSHA1_HASH_SIZE])
{
    rtSha1FinalInternal(pCtx);
    memcpy(pabDigest, &pCtx->AltPrivate.auH[0], RTSHA1_HASH_SIZE);
    rtSha1WipeCtx(pCtx);
}
RT_EXPORT_SYMBOL(RTSha1Final);


RTDECL(void) RTSha1(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA1_HASH_SIZE])
{
    RTSHA1CONTEXT Ctx;
    RTSha1Init(&Ctx);
    RTSha1Update(&Ctx, pvBuf, cbBuf);
    RTSha1Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha1);


RTDECL(bool) RTSha1Check(const void *pvBuf, size_t cbBuf, uint8_t const pabHash[RTSHA1_HASH_SIZE])
{
    RTSHA1CONTEXT Ctx;
    RTSha1Init(&Ctx);
    RTSha1Update(&Ctx, pvBuf, cbBuf);
    rtSha1FinalInternal(&Ctx);

    bool fRet = memcmp(pabHash, &Ctx.AltPrivate.auH[0], RTSHA1_HASH_SIZE) == 0;

    rtSha1WipeCtx(&Ctx);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha1Check);

