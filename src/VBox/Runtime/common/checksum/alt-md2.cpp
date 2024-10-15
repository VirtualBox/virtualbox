/* $Id: alt-md2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Message-Digest Algorithm 2, Alternative Implementation.
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
/** The MD2 block size. */
#define RTMD2_BLOCK_SIZE    16
/** The offset of the buffer into RTMD2ALTPRIVATECTX::abStateX. */
#define RTMD2_BUF_OFF       RTMD2_BLOCK_SIZE


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/types.h>


/** Our private context structure. */
typedef struct RTMD2ALTPRIVATECTX
{
    /** The state (X).
     * The staging buffer starts byte 16. */
    uint8_t     abStateX[RTMD2_BLOCK_SIZE * 3];
    /** The checksum. */
    uint8_t     abChecksum[RTMD2_BLOCK_SIZE];
    /** The number of buffered bytes. */
    uint8_t     cbBuffer;
} RTMD2ALTPRIVATECTX;

#define RT_MD2_PRIVATE_ALT_CONTEXT
#include <iprt/md2.h>

#include <iprt/assert.h>
#include <iprt/string.h>

AssertCompile(RT_SIZEOFMEMB(RTMD2CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTMD2CONTEXT, AltPrivate));


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** PI substitation used by MD2. */
static uint8_t const g_PiSubst[256] =
{
     41,  46,  67, 201, 162, 216, 124,   1,  61,  54,  84, 161, 236, 240,   6,  19,
     98, 167,   5, 243, 192, 199, 115, 140, 152, 147,  43, 217, 188,  76, 130, 202,
     30, 155,  87,  60, 253, 212, 224,  22, 103,  66, 111,  24, 138,  23, 229,  18,
    190,  78, 196, 214, 218, 158, 222,  73, 160, 251, 245, 142, 187,  47, 238, 122,
    169, 104, 121, 145,  21, 178,   7,  63, 148, 194,  16, 137,  11,  34,  95,  33,
    128, 127,  93, 154,  90, 144, 50,   39,  53,  62, 204, 231, 191, 247, 151,   3,
    255,  25,  48, 179,  72, 165, 181, 209, 215,  94, 146,  42, 172,  86, 170, 198,
     79, 184,  56, 210, 150, 164, 125, 182, 118, 252, 107, 226, 156, 116,   4, 241,
     69, 157, 112,  89, 100, 113, 135,  32, 134,  91, 207, 101, 230,  45, 168,   2,
     27,  96,  37, 173, 174, 176, 185, 246,  28,  70,  97, 105,  52,  64, 126,  15,
     85,  71, 163,  35, 221,  81, 175,  58, 195,  92, 249, 206, 186, 197, 234,  38,
     44,  83,  13, 110, 133,  40, 132,   9, 211, 223, 205, 244,  65, 129,  77,  82,
    106, 220,  55, 200, 108, 193, 171, 250,  36, 225, 123,   8,  12, 189, 177,  74,
    120, 136, 149, 139, 227,  99, 232, 109, 233, 203, 213, 254,  59,   0,  29,  57,
    242, 239, 183,  14, 102,  88, 208, 228, 166, 119, 114, 248, 235, 117,  75,  10,
     49,  68,  80, 180, 143, 237,  31,  26, 219, 153, 141,  51, 159,  17, 131,  20,
};


RTDECL(void) RTMd2Init(PRTMD2CONTEXT pCtx)
{
    pCtx->AltPrivate.cbBuffer = 0;
    RT_ZERO(pCtx->AltPrivate.abStateX);
    RT_ZERO(pCtx->AltPrivate.abChecksum);
}
RT_EXPORT_SYMBOL(RTMd2Init);



/**
 * Initializes the processing of a whole block directly from the input buffer.
 *
 * This will update the checksum as well as initializing abStateX.
 *
 * @param   pCtx                The MD2 context.
 * @param   pbBlock             The block.
 */
DECLINLINE(void) rtMd2BlockInit(PRTMD2CONTEXT pCtx, const uint8_t *pbBlock)
{
    uint8_t bL = pCtx->AltPrivate.abChecksum[15];
    for (unsigned j = 0; j < RTMD2_BLOCK_SIZE; j++)
    {
        uint8_t bIn = pbBlock[j];
        pCtx->AltPrivate.abStateX[j + RTMD2_BLOCK_SIZE] = bIn;
        pCtx->AltPrivate.abStateX[j + RTMD2_BLOCK_SIZE * 2] = bIn ^ pCtx->AltPrivate.abStateX[j];
        bL = pCtx->AltPrivate.abChecksum[j] ^= g_PiSubst[bIn ^ bL];
    }
}


/**
 * Special version of rtMd2BlockInit that does not update the checksum.
 *
 * This is used in the final round when adding the checksum to the calculation.
 *
 * @param   pCtx                The MD2 context.
 * @param   pbBlock             The block (i.e. the checksum).
 */
DECLINLINE(void) rtMd2BlockInitNoChecksum(PRTMD2CONTEXT pCtx, const uint8_t *pbBlock)
{
    for (unsigned j = 0; j < RTMD2_BLOCK_SIZE; j++)
    {
        uint8_t bIn = pbBlock[j];
        pCtx->AltPrivate.abStateX[j + RTMD2_BLOCK_SIZE] = bIn;
        pCtx->AltPrivate.abStateX[j + RTMD2_BLOCK_SIZE * 2] = bIn ^ pCtx->AltPrivate.abStateX[j];
    }
}


/**
 * Initalizes the abStateX from a full buffer and update the checksum.
 *
 * The buffer is part of the abStateX structure (bytes 16 thru 31), so this
 * is a somewhat reduced version of rtMd2BlockInit.
 *
 * @param   pCtx                The MD2 context.
 */
DECLINLINE(void) rtMd2BlockInitBuffered(PRTMD2CONTEXT pCtx)
{
    uint8_t bL = pCtx->AltPrivate.abChecksum[15];
    for (unsigned j = 0; j < RTMD2_BLOCK_SIZE; j++)
    {
        uint8_t bIn = pCtx->AltPrivate.abStateX[j + RTMD2_BLOCK_SIZE];
        pCtx->AltPrivate.abStateX[j + RTMD2_BLOCK_SIZE * 2] = bIn ^ pCtx->AltPrivate.abStateX[j];
        bL = pCtx->AltPrivate.abChecksum[j] ^= g_PiSubst[bIn ^ bL];
    }
}


/**
 * Process the current block.
 *
 * Requires one of the rtMd2BlockInit functions to be called first.
 *
 * @param   pCtx                The MD2 context.
 */
DECLINLINE(void) rtMd2BlockProcess(PRTMD2CONTEXT pCtx)
{
    uint8_t bT = 0;
    for (unsigned j = 0; j < 18; j++) /* 18 rounds */
    {
        for (unsigned k = 0; k < RTMD2_BLOCK_SIZE * 3; k++)
            pCtx->AltPrivate.abStateX[k] = bT = pCtx->AltPrivate.abStateX[k] ^ g_PiSubst[bT];
        bT += (uint8_t)j;
    }
}


RTDECL(void) RTMd2Update(PRTMD2CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    Assert(pCtx->AltPrivate.cbBuffer < RTMD2_BLOCK_SIZE);
    uint8_t const *pbBuf = (uint8_t const *)pvBuf;

    /*
     * Deal with buffered bytes first.
     */
    if (pCtx->AltPrivate.cbBuffer)
    {
        uint8_t cbMissing = RTMD2_BLOCK_SIZE - pCtx->AltPrivate.cbBuffer;
        if (cbBuf >= cbMissing)
        {
            memcpy(&pCtx->AltPrivate.abStateX[RTMD2_BUF_OFF + pCtx->AltPrivate.cbBuffer], pbBuf, cbMissing);
            pbBuf += cbMissing;
            cbBuf -= cbMissing;

            rtMd2BlockInitBuffered(pCtx);
            rtMd2BlockProcess(pCtx);

            pCtx->AltPrivate.cbBuffer = 0;
        }
        else
        {
            memcpy(&pCtx->AltPrivate.abStateX[RTMD2_BUF_OFF + pCtx->AltPrivate.cbBuffer], pbBuf, cbBuf);
            pCtx->AltPrivate.cbBuffer += (uint8_t)cbBuf;
            return;
        }
    }

    /*
     * Process full blocks directly from the input buffer.
     */
    while (cbBuf >= RTMD2_BLOCK_SIZE)
    {
        rtMd2BlockInit(pCtx, pbBuf);
        rtMd2BlockProcess(pCtx);

        pbBuf += RTMD2_BLOCK_SIZE;
        cbBuf -= RTMD2_BLOCK_SIZE;
    }

    /*
     * Stash any remaining bytes into the context buffer.
     */
    if (cbBuf > 0)
    {
        memcpy(&pCtx->AltPrivate.abStateX[RTMD2_BUF_OFF], pbBuf, cbBuf);
        pCtx->AltPrivate.cbBuffer = (uint8_t)cbBuf;
    }
}
RT_EXPORT_SYMBOL(RTMd2Update);


RTDECL(void) RTMd2Final(PRTMD2CONTEXT pCtx, uint8_t pabDigest[RTMD2_HASH_SIZE])
{
    Assert(pCtx->AltPrivate.cbBuffer < RTMD2_BLOCK_SIZE);

    /*
     * Pad the message to a multiple of 16 bytes. This is done even if the
     * message already is a multiple of 16.
     */
    unsigned cbPad = RTMD2_BLOCK_SIZE - pCtx->AltPrivate.cbBuffer;
    memset(&pCtx->AltPrivate.abStateX[RTMD2_BUF_OFF + pCtx->AltPrivate.cbBuffer], cbPad, cbPad);
    rtMd2BlockInitBuffered(pCtx);
    rtMd2BlockProcess(pCtx);
    pCtx->AltPrivate.cbBuffer = 0;

    /*
     * Add the checksum.
     */
    rtMd2BlockInitNoChecksum(pCtx, pCtx->AltPrivate.abChecksum);
    rtMd2BlockProcess(pCtx);

    /*
     * Done. Just copy out the digest.
     */
    memcpy(pabDigest, pCtx->AltPrivate.abStateX, RTMD2_HASH_SIZE);

    RT_ZERO(pCtx->AltPrivate);
    pCtx->AltPrivate.cbBuffer = UINT8_MAX;
}
RT_EXPORT_SYMBOL(RTMd2Final);


RTDECL(void) RTMd2(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTMD2_HASH_SIZE])
{
    RTMD2CONTEXT Ctx;
    RTMd2Init(&Ctx);
    RTMd2Update(&Ctx, pvBuf, cbBuf);
    RTMd2Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTMd2);

