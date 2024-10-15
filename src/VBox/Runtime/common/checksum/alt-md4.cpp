/* $Id: alt-md4.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Message-Digest Algorithm 4, Alternative Implementation.
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
/** The MD4 block size in bytes. */
#define RTMD4_BLOCK_SIZE            64
/** The MD4 block size in bits. */
#define RTMD4_BLOCK_SIZE_IN_BITS    (RTMD4_BLOCK_SIZE * 8)


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/types.h>


/** Our private context structure. */
typedef struct RTMD4ALTPRIVATECTX
{
    uint32_t    uA;
    uint32_t    uB;
    uint32_t    uC;
    uint32_t    uD;
    /** Message length in bits. */
    uint64_t    cTotalBits;
    /** Input buffer. cTotalBits indicates how much is present. */
    union
    {
        uint8_t     abBuffer[RTMD4_BLOCK_SIZE];
        uint32_t    aX[RTMD4_BLOCK_SIZE / 4];
    } u;
} RTMD4ALTPRIVATECTX;


#define RT_MD4_PRIVATE_ALT_CONTEXT
#include <iprt/md4.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>

AssertCompile(RT_SIZEOFMEMB(RTMD4CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTMD4CONTEXT, AltPrivate));


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** MD4 padding. */
static const uint8_t g_abMd4Padding[RTMD4_BLOCK_SIZE] =
{
    0x80, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
    0x00, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
    0x00, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
    0x00, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
};




RTDECL(void) RTMd4Init(PRTMD4CONTEXT pCtx)
{
    pCtx->AltPrivate.uA         = UINT32_C(0x67452301);
    pCtx->AltPrivate.uB         = UINT32_C(0xefcdab89);
    pCtx->AltPrivate.uC         = UINT32_C(0x98badcfe);
    pCtx->AltPrivate.uD         = UINT32_C(0x10325476);
    pCtx->AltPrivate.cTotalBits = 0;
    RT_ZERO(pCtx->AltPrivate.u);
}
RT_EXPORT_SYMBOL(RTMd4Init);


DECLINLINE(uint32_t) rtMd4FuncF(uint32_t uX, uint32_t uY, uint32_t uZ)
{
    return (uX & uY) | (~uX & uZ);
}

DECLINLINE(uint32_t) rtMd4FuncG(uint32_t uX, uint32_t uY, uint32_t uZ)
{
    return (uX & (uY | uZ)) | (uY & uZ);
}

DECLINLINE(uint32_t) rtMd4FuncH(uint32_t uX, uint32_t uY, uint32_t uZ)
{
    return uX ^ uY ^ uZ;
}


/**
 * Process the current block.
 *
 * Requires one of the rtMD4BlockInit functions to be called first.
 *
 * @param   pCtx                The MD4 context.
 */
DECLINLINE(void) rtMD4BlockProcess(PRTMD4CONTEXT pCtx)
{
#ifdef RT_BIG_ENDIAN
    /* Convert the X array to little endian. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pCtx->AltPrivate.u.aX); i++)
        pCtx->AltPrivate.u.aX[i] = RT_BSWAP_U32(pCtx->AltPrivate.u.aX[i]);
#endif

    /* Instead of saving A, B, C, D we copy them into variables and work on those. */
    uint32_t A = pCtx->AltPrivate.uA;
    uint32_t B = pCtx->AltPrivate.uB;
    uint32_t C = pCtx->AltPrivate.uC;
    uint32_t D = pCtx->AltPrivate.uD;

    /* Round #1: */
#define ABCD_K_S(a,b,c,d,  k, s) ASMRotateLeftU32(a + rtMd4FuncF(b, c, d) + pCtx->AltPrivate.u.aX[k], s)
    A = ABCD_K_S(A,B,C,D,  0, 3); D = ABCD_K_S(D,A,B,C,  1, 7); C = ABCD_K_S(C,D,A,B,  2, 11); B = ABCD_K_S(B,C,D,A,  3, 19);
    A = ABCD_K_S(A,B,C,D,  4, 3); D = ABCD_K_S(D,A,B,C,  5, 7); C = ABCD_K_S(C,D,A,B,  6, 11); B = ABCD_K_S(B,C,D,A,  7, 19);
    A = ABCD_K_S(A,B,C,D,  8, 3); D = ABCD_K_S(D,A,B,C,  9, 7); C = ABCD_K_S(C,D,A,B, 10, 11); B = ABCD_K_S(B,C,D,A, 11, 19);
    A = ABCD_K_S(A,B,C,D, 12, 3); D = ABCD_K_S(D,A,B,C, 13, 7); C = ABCD_K_S(C,D,A,B, 14, 11); B = ABCD_K_S(B,C,D,A, 15, 19);
#undef ABCD_K_S

    /* Round #2: */
#define ABCD_K_S(a,b,c,d,  k, s) ASMRotateLeftU32(a + rtMd4FuncG(b, c, d) + pCtx->AltPrivate.u.aX[k] + UINT32_C(0x5a827999), s)
    A = ABCD_K_S(A,B,C,D,  0, 3); D = ABCD_K_S(D,A,B,C,  4, 5); C = ABCD_K_S(C,D,A,B,  8,  9); B = ABCD_K_S(B,C,D,A, 12, 13);
    A = ABCD_K_S(A,B,C,D,  1, 3); D = ABCD_K_S(D,A,B,C,  5, 5); C = ABCD_K_S(C,D,A,B,  9,  9); B = ABCD_K_S(B,C,D,A, 13, 13);
    A = ABCD_K_S(A,B,C,D,  2, 3); D = ABCD_K_S(D,A,B,C,  6, 5); C = ABCD_K_S(C,D,A,B, 10,  9); B = ABCD_K_S(B,C,D,A, 14, 13);
    A = ABCD_K_S(A,B,C,D,  3, 3); D = ABCD_K_S(D,A,B,C,  7, 5); C = ABCD_K_S(C,D,A,B, 11,  9); B = ABCD_K_S(B,C,D,A, 15, 13);
#undef ABCD_K_S

    /* Round #3: */
#define ABCD_K_S(a,b,c,d,  k, s) ASMRotateLeftU32(a + rtMd4FuncH(b, c, d) + pCtx->AltPrivate.u.aX[k] + UINT32_C(0x6ed9eba1), s)
    A = ABCD_K_S(A,B,C,D,  0, 3); D = ABCD_K_S(D,A,B,C,  8, 9); C = ABCD_K_S(C,D,A,B, 4, 11); B = ABCD_K_S(B,C,D,A, 12, 15);
    A = ABCD_K_S(A,B,C,D,  2, 3); D = ABCD_K_S(D,A,B,C, 10, 9); C = ABCD_K_S(C,D,A,B, 6, 11); B = ABCD_K_S(B,C,D,A, 14, 15);
    A = ABCD_K_S(A,B,C,D,  1, 3); D = ABCD_K_S(D,A,B,C,  9, 9); C = ABCD_K_S(C,D,A,B, 5, 11); B = ABCD_K_S(B,C,D,A, 13, 15);
    A = ABCD_K_S(A,B,C,D,  3, 3); D = ABCD_K_S(D,A,B,C, 11, 9); C = ABCD_K_S(C,D,A,B, 7, 11); B = ABCD_K_S(B,C,D,A, 15, 15);
#undef ABCD_K_S

    /* Perform the additions. */
    pCtx->AltPrivate.uA += A;
    pCtx->AltPrivate.uB += B;
    pCtx->AltPrivate.uC += C;
    pCtx->AltPrivate.uD += D;
}


RTDECL(void) RTMd4Update(PRTMD4CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    uint8_t const *pbBuf = (uint8_t const *)pvBuf;

    /*
     * Deal with buffered bytes first.
     */
    if (pCtx->AltPrivate.cTotalBits & (RTMD4_BLOCK_SIZE_IN_BITS - 1))
    {
        uint8_t cbBuffered = (pCtx->AltPrivate.cTotalBits >> 3) & (RTMD4_BLOCK_SIZE - 1);
        uint8_t cbMissing  = RTMD4_BLOCK_SIZE - cbBuffered;
        if (cbBuf >= cbMissing)
        {
            memcpy(&pCtx->AltPrivate.u.abBuffer[cbBuffered], pbBuf, cbMissing);
            pCtx->AltPrivate.cTotalBits += cbMissing << 3;
            pbBuf += cbMissing;
            cbBuf -= cbMissing;

            rtMD4BlockProcess(pCtx);
        }
        else
        {
            memcpy(&pCtx->AltPrivate.u.abBuffer[cbBuffered], pbBuf, cbBuf);
            pCtx->AltPrivate.cTotalBits += cbBuf << 3;
            return;
        }
    }

    /*
     * Process full blocks directly from the input buffer.
     */
    while (cbBuf >= RTMD4_BLOCK_SIZE)
    {
        memcpy(&pCtx->AltPrivate.u.abBuffer[0], pbBuf, RTMD4_BLOCK_SIZE);
        rtMD4BlockProcess(pCtx);

        pbBuf += RTMD4_BLOCK_SIZE;
        cbBuf -= RTMD4_BLOCK_SIZE;
        pCtx->AltPrivate.cTotalBits += RTMD4_BLOCK_SIZE_IN_BITS;
    }

    /*
     * Stash any remaining bytes into the context buffer.
     */
    if (cbBuf > 0)
    {
        memcpy(&pCtx->AltPrivate.u.abBuffer[0], pbBuf, cbBuf);
        pCtx->AltPrivate.cTotalBits += cbBuf << 3;
    }
}
RT_EXPORT_SYMBOL(RTMd4Update);


RTDECL(void) RTMd4Final(PRTMD4CONTEXT pCtx, uint8_t pabDigest[RTMD4_HASH_SIZE])
{
    uint64_t const cTotalBits = pCtx->AltPrivate.cTotalBits;

    /*
     * Pad input to block size minus sizeof(cTotalBits).
     */
    uint8_t cbMissing  = RTMD4_BLOCK_SIZE - ((cTotalBits >> 3) & (RTMD4_BLOCK_SIZE - 1));
    uint8_t cbPadding  = cbMissing + (cbMissing > 8 ? 0 : RTMD4_BLOCK_SIZE) - 8;
    Assert(cbPadding > 0 && cbPadding <= sizeof(g_abMd4Padding));
    RTMd4Update(pCtx, g_abMd4Padding, cbPadding);
    Assert(((pCtx->AltPrivate.cTotalBits >> 3) & (RTMD4_BLOCK_SIZE - 1)) == RTMD4_BLOCK_SIZE - 8);

    /*
     * Encode the total bitcount at the end of the buffer and do the final round.
     */
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 8] = (uint8_t)(cTotalBits      );
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 7] = (uint8_t)(cTotalBits >>  8);
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 6] = (uint8_t)(cTotalBits >> 16);
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 5] = (uint8_t)(cTotalBits >> 24);
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 4] = (uint8_t)(cTotalBits >> 32);
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 3] = (uint8_t)(cTotalBits >> 40);
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 2] = (uint8_t)(cTotalBits >> 48);
    pCtx->AltPrivate.u.abBuffer[RTMD4_BLOCK_SIZE - 1] = (uint8_t)(cTotalBits >> 56);
    rtMD4BlockProcess(pCtx);

    /*
     * Done. Just encode the digest.
     */
    pabDigest[ 0] = (uint8_t)(pCtx->AltPrivate.uA      );
    pabDigest[ 1] = (uint8_t)(pCtx->AltPrivate.uA >>  8);
    pabDigest[ 2] = (uint8_t)(pCtx->AltPrivate.uA >> 16);
    pabDigest[ 3] = (uint8_t)(pCtx->AltPrivate.uA >> 24);
    pabDigest[ 4] = (uint8_t)(pCtx->AltPrivate.uB      );
    pabDigest[ 5] = (uint8_t)(pCtx->AltPrivate.uB >>  8);
    pabDigest[ 6] = (uint8_t)(pCtx->AltPrivate.uB >> 16);
    pabDigest[ 7] = (uint8_t)(pCtx->AltPrivate.uB >> 24);
    pabDigest[ 8] = (uint8_t)(pCtx->AltPrivate.uC      );
    pabDigest[ 9] = (uint8_t)(pCtx->AltPrivate.uC >>  8);
    pabDigest[10] = (uint8_t)(pCtx->AltPrivate.uC >> 16);
    pabDigest[11] = (uint8_t)(pCtx->AltPrivate.uC >> 24);
    pabDigest[12] = (uint8_t)(pCtx->AltPrivate.uD      );
    pabDigest[13] = (uint8_t)(pCtx->AltPrivate.uD >>  8);
    pabDigest[14] = (uint8_t)(pCtx->AltPrivate.uD >> 16);
    pabDigest[15] = (uint8_t)(pCtx->AltPrivate.uD >> 24);

    /*
     * Nuke the state.
     */
    RT_ZERO(pCtx->AltPrivate);
}
RT_EXPORT_SYMBOL(RTMd4Final);


RTDECL(void) RTMd4(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTMD4_HASH_SIZE])
{
    RTMD4CONTEXT Ctx;
    RTMd4Init(&Ctx);
    RTMd4Update(&Ctx, pvBuf, cbBuf);
    RTMd4Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTMd4);

