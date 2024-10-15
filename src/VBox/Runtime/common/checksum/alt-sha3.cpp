/* $Id: alt-sha3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-3 hash functions, Alternative Implementation.
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
/** Number of rounds [3.4]. */
#define RTSHA3_ROUNDS   24

/** @def RTSHA3_FULL_UNROLL
 * Do full loop unrolling.
 *
 * With gcc 10.2.1 on a recent Intel system (10890XE), this results SHA3-512
 * throughput (tstRTDigest-2) increasing from 83532 KiB/s to 194942 KiB/s
 * against a text size jump from 5913 to 6929 bytes, i.e. +1016 bytes.
 *
 * With VS2019 on a half decent AMD system (3990X), this results in SHA3-512
 * speedup from 147676 KiB/s to about 192770 KiB/s.  The text cost is +612 bytes
 * (4496 to 5108).  When disabling the unrolling of Rho+Pi we get a little
 * increase 196591 KiB/s (+3821) for some reason, saving 22 bytes of code.
 *
 * For comparison, openssl 1.1.1g assembly code (AMD64) achives 264915 KiB/s,
 * which is only 36% more.  Performance is more or less exactly the same as
 * KECCAK_2X without ROL optimizations (they improve it to 203493 KiB/s).
 */
#if !defined(IN_SUP_HARDENED_R3) || defined(DOXYGEN_RUNNING)
# define RTSHA3_FULL_UNROLL
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/assert.h>
#include <iprt/assertcompile.h>
#include <iprt/asm.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTSHA3ALTPRIVATECTX
{
    /** The KECCAK state (W=1600). */
    union
    {
        uint64_t au64[/*1600/64 =*/ 25];
        uint8_t  ab[/*1600/8 =*/   200];
    };

    /** Current input position. */
    uint8_t     offInput;
    /** The number of bytes to xor into the state before doing KECCAK. */
    uint8_t     cbInput;
    /** The digest size in bytes. */
    uint8_t     cbDigest;
    /** Padding the size up to 208 bytes. */
    uint8_t     abPadding[4];
    /** Set if we've finalized the digest. */
    bool        fFinal;
} RTSHA3ALTPRIVATECTX;

#define RT_SHA3_PRIVATE_ALT_CONTEXT
#include <iprt/sha.h>



static void rtSha3Keccak(RTSHA3ALTPRIVATECTX *pState)
{
#ifdef RT_BIG_ENDIAN
    /* This sucks a performance wise on big endian systems, sorry.  We just
       needed something simple that works on AMD64 and x86. */
    for (size_t i = 0; i < RT_ELEMENTS(pState->au64); i++)
        pState->au64[i] = RT_LE2H_U64(pState->au64[i]);
#endif

    /*
     * Rounds: Rnd(A,idxRound) = Iota(Chi(Pi(Rho(Theta(A)))), idxRount) [3.3]
     */
    for (uint32_t idxRound = 0; idxRound < RTSHA3_ROUNDS; idxRound++)
    {
        /*
         * 3.2.1 Theta
         */
        {
            /* Step 1: */
            const uint64_t au64C[5] =
            {
                pState->au64[0] ^ pState->au64[5] ^ pState->au64[10] ^ pState->au64[15] ^ pState->au64[20],
                pState->au64[1] ^ pState->au64[6] ^ pState->au64[11] ^ pState->au64[16] ^ pState->au64[21],
                pState->au64[2] ^ pState->au64[7] ^ pState->au64[12] ^ pState->au64[17] ^ pState->au64[22],
                pState->au64[3] ^ pState->au64[8] ^ pState->au64[13] ^ pState->au64[18] ^ pState->au64[23],
                pState->au64[4] ^ pState->au64[9] ^ pState->au64[14] ^ pState->au64[19] ^ pState->au64[24],
            };

            /* Step 2 & 3: */
#ifndef RTSHA3_FULL_UNROLL
            for (size_t i = 0; i < RT_ELEMENTS(au64C); i++)
            {
                uint64_t const u64D = au64C[(i + 4) % RT_ELEMENTS(au64C)]
                                    ^ ASMRotateLeftU64(au64C[(i + 1) % RT_ELEMENTS(au64C)], 1);
                pState->au64[ 0 + i] ^= u64D;
                pState->au64[ 5 + i] ^= u64D;
                pState->au64[10 + i] ^= u64D;
                pState->au64[15 + i] ^= u64D;
                pState->au64[20 + i] ^= u64D;
            }
#else  /* RTSHA3_FULL_UNROLL */
# define THETA_STEP_2_3(a_i, a_idxCLeft, a_idxCRight) do { \
                uint64_t const u64D = au64C[a_idxCLeft] ^ ASMRotateLeftU64(au64C[a_idxCRight], 1); \
                pState->au64[ 0 + a_i] ^= u64D; \
                pState->au64[ 5 + a_i] ^= u64D; \
                pState->au64[10 + a_i] ^= u64D; \
                pState->au64[15 + a_i] ^= u64D; \
                pState->au64[20 + a_i] ^= u64D; \
            } while (0)
            THETA_STEP_2_3(0, 4, 1);
            THETA_STEP_2_3(1, 0, 2);
            THETA_STEP_2_3(2, 1, 3);
            THETA_STEP_2_3(3, 2, 4);
            THETA_STEP_2_3(4, 3, 0);
#endif /* RTSHA3_FULL_UNROLL */
        }

        /*
         * 3.2.2 Rho + 3.2.3 Pi
         */
        {
#if !defined(RTSHA3_FULL_UNROLL) || defined(_MSC_VER) /* VS2019 is slightly slow with this section unrolled. go figure */
            static uint8_t const s_aidxState[] = {10,7,11,17,18,  3, 5,16, 8,21, 24, 4,15,23,19, 13,12, 2,20,14, 22, 9, 6, 1};
            static uint8_t const s_acRotate[]  = { 1,3, 6,10,15, 21,28,36,45,55,  2,14,27,41,56,  8,25,43,62,18, 39,61,20,44};
            AssertCompile(RT_ELEMENTS(s_aidxState) == 24); AssertCompile(RT_ELEMENTS(s_acRotate) == 24);
            uint64_t u64 = pState->au64[1 /*s_aidxState[RT_ELEMENTS(s_aidxState) - 1]*/];
# if !defined(_MSC_VER) /* This is slower with VS2019 but slightly faster with g++ (10.2.1). */
            for (size_t i = 0; i <= 23 - 1; i++) /*i=t*/
            {
                uint64_t const u64Result = ASMRotateLeftU64(u64, s_acRotate[i]);
                size_t const   idxState  = s_aidxState[i];
                u64 = pState->au64[idxState];
                pState->au64[idxState] = u64Result;
            }
            pState->au64[1 /*s_aidxState[23]*/] = ASMRotateLeftU64(u64, 44 /*s_acRotate[23]*/);
# else
            for (size_t i = 0; i <= 23; i++) /*i=t*/
            {
                uint64_t const u64Result = ASMRotateLeftU64(u64, s_acRotate[i]);
                size_t const   idxState  = s_aidxState[i];
                u64 = pState->au64[idxState];
                pState->au64[idxState] = u64Result;
            }
# endif
#else  /* RTSHA3_FULL_UNROLL */
# define RHO_AND_PI(a_idxState, a_cRotate) do { \
                uint64_t const u64Result = ASMRotateLeftU64(u64, a_cRotate); \
                u64 = pState->au64[a_idxState]; \
                pState->au64[a_idxState] = u64Result; \
            } while (0)

            uint64_t u64 = pState->au64[1 /*s_aidxState[RT_ELEMENTS(s_aidxState) - 1]*/];
            RHO_AND_PI(10,  1);
            RHO_AND_PI( 7,  3);
            RHO_AND_PI(11,  6);
            RHO_AND_PI(17, 10);
            RHO_AND_PI(18, 15);
            RHO_AND_PI( 3, 21);
            RHO_AND_PI( 5, 28);
            RHO_AND_PI(16, 36);
            RHO_AND_PI( 8, 45);
            RHO_AND_PI(21, 55);
            RHO_AND_PI(24,  2);
            RHO_AND_PI( 4, 14);
            RHO_AND_PI(15, 27);
            RHO_AND_PI(23, 41);
            RHO_AND_PI(19, 56);
            RHO_AND_PI(13,  8);
            RHO_AND_PI(12, 25);
            RHO_AND_PI( 2, 43);
            RHO_AND_PI(20, 62);
            RHO_AND_PI(14, 18);
            RHO_AND_PI(22, 39);
            RHO_AND_PI( 9, 61);
            RHO_AND_PI( 6, 20);
            pState->au64[1 /*s_aidxState[23]*/] = ASMRotateLeftU64(u64, 44 /*s_acRotate[23]*/);

#endif /* RTSHA3_FULL_UNROLL */
        }

        /*
         * 3.2.4 Chi & 3.2.5 Iota.
         */
        /* Iota values xor constants (indexed by round). */
        static uint64_t const s_au64RC[] =
        {
            UINT64_C(0x0000000000000001), UINT64_C(0x0000000000008082), UINT64_C(0x800000000000808a), UINT64_C(0x8000000080008000),
            UINT64_C(0x000000000000808b), UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008009),
            UINT64_C(0x000000000000008a), UINT64_C(0x0000000000000088), UINT64_C(0x0000000080008009), UINT64_C(0x000000008000000a),
            UINT64_C(0x000000008000808b), UINT64_C(0x800000000000008b), UINT64_C(0x8000000000008089), UINT64_C(0x8000000000008003),
            UINT64_C(0x8000000000008002), UINT64_C(0x8000000000000080), UINT64_C(0x000000000000800a), UINT64_C(0x800000008000000a),
            UINT64_C(0x8000000080008081), UINT64_C(0x8000000000008080), UINT64_C(0x0000000080000001), UINT64_C(0x8000000080008008),
        };
        AssertCompile(RT_ELEMENTS(s_au64RC) == RTSHA3_ROUNDS);
#ifndef RTSHA3_FULL_UNROLL
        /* Chi */
        for (size_t i = 0; i < 25; i += 5)
        {
# ifndef _MSC_VER /* This is typically slower with VS2019 - go figure.  Makes not difference with g++. */
            uint64_t const u0 = pState->au64[i + 0];
            uint64_t const u1 = pState->au64[i + 1];
            uint64_t const u2 = pState->au64[i + 2];
            pState->au64[i + 0] = u0 ^ (~u1 & u2);
            uint64_t const u3 = pState->au64[i + 3];
            pState->au64[i + 1] = u1 ^ (~u2 & u3);
            uint64_t const u4 = pState->au64[i + 4];
            pState->au64[i + 2] = u2 ^ (~u3 & u4);
            pState->au64[i + 3] = u3 ^ (~u4 & u0);
            pState->au64[i + 4] = u4 ^ (~u0 & u1);
# else
            uint64_t const au64Tmp[] = { pState->au64[i + 0], pState->au64[i + 1], pState->au64[i + 2],
                                         pState->au64[i + 3], pState->au64[i + 4] };
            pState->au64[i + 0] ^= ~au64Tmp[1] & au64Tmp[2];
            pState->au64[i + 1] ^= ~au64Tmp[2] & au64Tmp[3];
            pState->au64[i + 2] ^= ~au64Tmp[3] & au64Tmp[4];
            pState->au64[i + 3] ^= ~au64Tmp[4] & au64Tmp[0];
            pState->au64[i + 4] ^= ~au64Tmp[0] & au64Tmp[1];
# endif
        }

        /* Iota. */
        pState->au64[0] ^= s_au64RC[idxRound];

#else  /* RTSHA3_FULL_UNROLL */
# define CHI_AND_IOTA(a_i, a_IotaExpr) do { \
            uint64_t const u0 = pState->au64[a_i + 0]; \
            uint64_t const u1 = pState->au64[a_i + 1]; \
            uint64_t const u2 = pState->au64[a_i + 2]; \
            pState->au64[a_i + 0] = u0 ^ (~u1 & u2) a_IotaExpr; \
            uint64_t const u3 = pState->au64[a_i + 3]; \
            pState->au64[a_i + 1] = u1 ^ (~u2 & u3); \
            uint64_t const u4 = pState->au64[a_i + 4]; \
            pState->au64[a_i + 2] = u2 ^ (~u3 & u4); \
            pState->au64[a_i + 3] = u3 ^ (~u4 & u0); \
            pState->au64[a_i + 4] = u4 ^ (~u0 & u1); \
        } while (0)
        CHI_AND_IOTA( 0, ^ s_au64RC[idxRound]);
        CHI_AND_IOTA( 5, RT_NOTHING);
        CHI_AND_IOTA(10, RT_NOTHING);
        CHI_AND_IOTA(15, RT_NOTHING);
        CHI_AND_IOTA(20, RT_NOTHING);
#endif /* RTSHA3_FULL_UNROLL */
    }

#ifdef RT_BIG_ENDIAN
    for (size_t i = 0; i < RT_ELEMENTS(pState->au64); i++)
        pState->au64[i] = RT_H2LE_U64(pState->au64[i]);
#endif
}


static int rtSha3Init(RTSHA3ALTPRIVATECTX *pCtx, unsigned cBitsDigest)
{
    RT_ZERO(pCtx->au64);
    pCtx->offInput  = 0;
    pCtx->cbInput   = (uint8_t)(sizeof(pCtx->ab) - (2 * cBitsDigest / 8));
    pCtx->cbDigest  = cBitsDigest / 8;
    pCtx->fFinal    = false;
    return VINF_SUCCESS;
}


static int rtSha3Update(RTSHA3ALTPRIVATECTX *pCtx, uint8_t const *pbData, size_t cbData)
{
    Assert(!pCtx->fFinal);
    size_t const    cbInput  = pCtx->cbInput;
    size_t          offState = pCtx->offInput;
    Assert(!(cbInput & 7));
#if 1
    if (   ((uintptr_t)pbData & 7) == 0
        && (offState & 7)          == 0
        && (cbData & 7)            == 0)
    {
        uint64_t const  cQwordsInput = cbInput / sizeof(uint64_t);
        uint64_t const *pu64Data     = (uint64_t const *)pbData;
        size_t          cQwordsData  = cbData / sizeof(uint64_t);
        size_t          offData      = 0;
        offState /= sizeof(uint64_t);

        /*
         * Any catching up to do?
         */
        if (offState == 0 || cQwordsData >= cQwordsInput - offState)
        {
            if (offState > 0)
            {
                while (offState < cQwordsInput)
                    pCtx->au64[offState++] ^= pu64Data[offData++];
                rtSha3Keccak(pCtx);
                offState = 0;
            }
            if (offData < cQwordsData)
            {
                /*
                 * Do full chunks.
                 */
# if 1
                switch (cQwordsInput)
                {
                    case 18:   /* ( 200 - (2 * 224/8) = 0x90 (144) ) / 8 = 0x12 (18) */
                    {
                        size_t cFullChunks = (cQwordsData - offData) / 18;
                        while (cFullChunks-- > 0)
                        {
                            pCtx->au64[ 0] ^= pu64Data[offData +  0];
                            pCtx->au64[ 1] ^= pu64Data[offData +  1];
                            pCtx->au64[ 2] ^= pu64Data[offData +  2];
                            pCtx->au64[ 3] ^= pu64Data[offData +  3];
                            pCtx->au64[ 4] ^= pu64Data[offData +  4];
                            pCtx->au64[ 5] ^= pu64Data[offData +  5];
                            pCtx->au64[ 6] ^= pu64Data[offData +  6];
                            pCtx->au64[ 7] ^= pu64Data[offData +  7];
                            pCtx->au64[ 8] ^= pu64Data[offData +  8];
                            pCtx->au64[ 9] ^= pu64Data[offData +  9];
                            pCtx->au64[10] ^= pu64Data[offData + 10];
                            pCtx->au64[11] ^= pu64Data[offData + 11];
                            pCtx->au64[12] ^= pu64Data[offData + 12];
                            pCtx->au64[13] ^= pu64Data[offData + 13];
                            pCtx->au64[14] ^= pu64Data[offData + 14];
                            pCtx->au64[15] ^= pu64Data[offData + 15];
                            pCtx->au64[16] ^= pu64Data[offData + 16];
                            pCtx->au64[17] ^= pu64Data[offData + 17];
                            offData += 18;
                            rtSha3Keccak(pCtx);
                        }
                        break;
                    }

                    case 17:   /* ( 200 - (2 * 256/8) = 0x88 (136) ) / 8 = 0x11 (17) */
                    {
                        size_t cFullChunks = (cQwordsData - offData) / 17;
                        while (cFullChunks-- > 0)
                        {
                            pCtx->au64[ 0] ^= pu64Data[offData +  0];
                            pCtx->au64[ 1] ^= pu64Data[offData +  1];
                            pCtx->au64[ 2] ^= pu64Data[offData +  2];
                            pCtx->au64[ 3] ^= pu64Data[offData +  3];
                            pCtx->au64[ 4] ^= pu64Data[offData +  4];
                            pCtx->au64[ 5] ^= pu64Data[offData +  5];
                            pCtx->au64[ 6] ^= pu64Data[offData +  6];
                            pCtx->au64[ 7] ^= pu64Data[offData +  7];
                            pCtx->au64[ 8] ^= pu64Data[offData +  8];
                            pCtx->au64[ 9] ^= pu64Data[offData +  9];
                            pCtx->au64[10] ^= pu64Data[offData + 10];
                            pCtx->au64[11] ^= pu64Data[offData + 11];
                            pCtx->au64[12] ^= pu64Data[offData + 12];
                            pCtx->au64[13] ^= pu64Data[offData + 13];
                            pCtx->au64[14] ^= pu64Data[offData + 14];
                            pCtx->au64[15] ^= pu64Data[offData + 15];
                            pCtx->au64[16] ^= pu64Data[offData + 16];
                            offData += 17;
                            rtSha3Keccak(pCtx);
                        }
                        break;
                    }

                    case 13:   /* ( 200 - (2 * 384/8) = 0x68 (104) ) / 8 = 0x0d (13) */
                    {
                        size_t cFullChunks = (cQwordsData - offData) / 13;
                        while (cFullChunks-- > 0)
                        {
                            pCtx->au64[ 0] ^= pu64Data[offData +  0];
                            pCtx->au64[ 1] ^= pu64Data[offData +  1];
                            pCtx->au64[ 2] ^= pu64Data[offData +  2];
                            pCtx->au64[ 3] ^= pu64Data[offData +  3];
                            pCtx->au64[ 4] ^= pu64Data[offData +  4];
                            pCtx->au64[ 5] ^= pu64Data[offData +  5];
                            pCtx->au64[ 6] ^= pu64Data[offData +  6];
                            pCtx->au64[ 7] ^= pu64Data[offData +  7];
                            pCtx->au64[ 8] ^= pu64Data[offData +  8];
                            pCtx->au64[ 9] ^= pu64Data[offData +  9];
                            pCtx->au64[10] ^= pu64Data[offData + 10];
                            pCtx->au64[11] ^= pu64Data[offData + 11];
                            pCtx->au64[12] ^= pu64Data[offData + 12];
                            offData += 13;
                            rtSha3Keccak(pCtx);
                        }
                        break;
                    }

                    case  9:   /* ( 200 - (2 * 512/8) = 0x48 (72)  ) / 8 = 0x09 (9) */
                    {
                        size_t cFullChunks = (cQwordsData - offData) / 9;
                        while (cFullChunks-- > 0)
                        {
                            pCtx->au64[ 0] ^= pu64Data[offData +  0];
                            pCtx->au64[ 1] ^= pu64Data[offData +  1];
                            pCtx->au64[ 2] ^= pu64Data[offData +  2];
                            pCtx->au64[ 3] ^= pu64Data[offData +  3];
                            pCtx->au64[ 4] ^= pu64Data[offData +  4];
                            pCtx->au64[ 5] ^= pu64Data[offData +  5];
                            pCtx->au64[ 6] ^= pu64Data[offData +  6];
                            pCtx->au64[ 7] ^= pu64Data[offData +  7];
                            pCtx->au64[ 8] ^= pu64Data[offData +  8];
                            offData += 9;
                            rtSha3Keccak(pCtx);
                        }
                        break;
                    }

                    default:
                    {
                        AssertFailed();
# endif
                        size_t cFullChunks = (cQwordsData - offData) / cQwordsInput;
                        while (cFullChunks-- > 0)
                        {
                            offState = cQwordsInput;
                            while (offState-- > 0)
                                pCtx->au64[offState] ^= pu64Data[offData + offState];
                            offData += cQwordsInput;
                            rtSha3Keccak(pCtx);
                        }
# if 1
                        break;
                    }
                }
# endif
                offState = 0;

                /*
                 * Partial last chunk?
                 */
                if (offData < cQwordsData)
                {
                    Assert(cQwordsData - offData < cQwordsInput);
                    while (offData < cQwordsData)
                        pCtx->au64[offState++] ^= pu64Data[offData++];
                    offState *= sizeof(uint64_t);
                }
            }
        }
        else
        {
            while (offData < cQwordsData)
                pCtx->au64[offState++] ^= pu64Data[offData++];
            offState *= sizeof(uint64_t);
        }
        Assert(offData == cQwordsData);
    }
    else
#endif
    {
        /*
         * Misaligned input/state, so just do simpe byte by byte processing.
         */
        for (size_t offData = 0; offData < cbData; offData++)
        {
            pCtx->ab[offState] ^= pbData[offData];
            offState++;
            if (offState < cbInput)
            { /* likely */ }
            else
            {
                rtSha3Keccak(pCtx);
                offState = 0;
            }
        }
    }
    pCtx->offInput = (uint8_t)offState;
    return VINF_SUCCESS;
}


static void rtSha3FinalInternal(RTSHA3ALTPRIVATECTX *pCtx)
{
    Assert(!pCtx->fFinal);

    pCtx->ab[pCtx->offInput]    ^= 0x06;
    pCtx->ab[pCtx->cbInput - 1] ^= 0x80;
    rtSha3Keccak(pCtx);
}


static int rtSha3Final(RTSHA3ALTPRIVATECTX *pCtx, uint8_t *pbDigest)
{
    Assert(!pCtx->fFinal);

    rtSha3FinalInternal(pCtx);

    memcpy(pbDigest, pCtx->ab, pCtx->cbDigest);

    /* Wipe non-hash state. */
    RT_BZERO(&pCtx->ab[pCtx->cbDigest], sizeof(pCtx->ab) - pCtx->cbDigest);
    pCtx->fFinal = true;
    return VINF_SUCCESS;
}


static int rtSha3(const void *pvData, size_t cbData, unsigned cBitsDigest, uint8_t *pabHash)
{
    RTSHA3ALTPRIVATECTX Ctx;
    rtSha3Init(&Ctx, cBitsDigest);
    rtSha3Update(&Ctx, (uint8_t const *)pvData, cbData);
    rtSha3Final(&Ctx, pabHash);
    return VINF_SUCCESS;
}


static bool rtSha3Check(const void *pvData, size_t cbData, unsigned cBitsDigest, const uint8_t *pabHash)
{
    RTSHA3ALTPRIVATECTX Ctx;
    rtSha3Init(&Ctx, cBitsDigest);
    rtSha3Update(&Ctx, (uint8_t const *)pvData, cbData);
    rtSha3FinalInternal(&Ctx);
    bool fRet = memcmp(pabHash, &Ctx.ab, cBitsDigest / 8) == 0;
    RT_ZERO(Ctx);
    return fRet;
}


/** Macro for declaring the interface for a SHA3 variation.
 * @internal */
#define RTSHA3_DEFINE_VARIANT(a_cBits) \
AssertCompile((a_cBits / 8) == RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)); \
AssertCompile(sizeof(RT_CONCAT3(RTSHA3T,a_cBits,CONTEXT)) >= sizeof(RTSHA3ALTPRIVATECTX)); \
\
RTDECL(int) RT_CONCAT(RTSha3t,a_cBits)(const void *pvBuf, size_t cbBuf, uint8_t pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    return rtSha3(pvBuf, cbBuf, a_cBits, pabHash); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT(RTSha3t,a_cBits)); \
\
\
RTDECL(bool) RT_CONCAT3(RTSha3t,a_cBits,Check)(const void *pvBuf, size_t cbBuf, \
                                               uint8_t const pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    return rtSha3Check(pvBuf, cbBuf, a_cBits, pabHash); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Check)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Init)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx) \
{ \
    AssertCompile(sizeof(pCtx->Sha3.a64Padding) >= sizeof(pCtx->Sha3.AltPrivate)); \
    AssertCompile(sizeof(pCtx->Sha3.a64Padding) == sizeof(pCtx->Sha3.abPadding)); \
    return rtSha3Init(&pCtx->Sha3.AltPrivate, a_cBits); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Init)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Update)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx, const void *pvBuf, size_t cbBuf) \
{ \
    Assert(pCtx->Sha3.AltPrivate.cbDigest == (a_cBits) / 8); \
    return rtSha3Update(&pCtx->Sha3.AltPrivate, (uint8_t const *)pvBuf, cbBuf); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Update)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Final)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx, \
                                              uint8_t pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    Assert(pCtx->Sha3.AltPrivate.cbDigest == (a_cBits) / 8); \
    return rtSha3Final(&pCtx->Sha3.AltPrivate, pabHash); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Final)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Cleanup)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx) \
{ \
    if (pCtx) \
    { \
        Assert(pCtx->Sha3.AltPrivate.cbDigest == (a_cBits) / 8); \
        RT_ZERO(*pCtx); \
    } \
    return VINF_SUCCESS; \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Cleanup)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Clone)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx, \
                                              RT_CONCAT3(RTSHA3T,a_cBits,CONTEXT) const *pCtxSrc) \
{ \
    memcpy(pCtx, pCtxSrc, sizeof(*pCtx)); \
    return VINF_SUCCESS; \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Clone)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,ToString)(uint8_t const pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)], \
                                                 char *pszDigest, size_t cchDigest) \
{ \
    return RTStrPrintHexBytes(pszDigest, cchDigest, pabHash, (a_cBits) / 8, 0 /*fFlags*/); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,ToString)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,FromString)(char const *pszDigest, uint8_t pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    return RTStrConvertHexBytes(RTStrStripL(pszDigest), &pabHash[0], (a_cBits) / 8, 0 /*fFlags*/); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,FromString))


RTSHA3_DEFINE_VARIANT(224);
RTSHA3_DEFINE_VARIANT(256);
RTSHA3_DEFINE_VARIANT(384);
RTSHA3_DEFINE_VARIANT(512);

