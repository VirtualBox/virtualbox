/* $Id: alt-md5.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - MD5 message digest functions, alternative implementation.
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

/* The code is virtually unchanged from the original version (see copyright
 * notice below). Most changes are related to the function names and data
 * types - in order to fit the code in the IPRT naming style. */

/*
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * RTMD5CONTEXT structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/md5.h>
#include "internal/iprt.h"

#include <iprt/string.h>                 /* for memcpy() */
#if defined(RT_BIG_ENDIAN)
# include <iprt/asm.h>                   /* RT_LE2H_U32 uses ASMByteSwapU32. */
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* The four core functions - F1 is optimized somewhat */
#if 1
/* #define F1(x, y, z) (x & y | ~x & z) */
# define F1(x, y, z) (z ^ (x & (y ^ z)))
# define F2(x, y, z) F1(z, x, y)
# define F3(x, y, z) (x ^ y ^ z)
# define F4(x, y, z) (y ^ (x | ~z))
#else  /* gcc 4.0.1 (x86) benefits from the explicitness of F1() here. */
DECL_FORCE_INLINE(uint32_t) F1(uint32_t x, uint32_t y, uint32_t z)
{
    register uint32_t r = y ^ z;
    r &= x;
    r ^= z;
    return r;
}
# define F2(x, y, z) F1(z, x, y)
DECL_FORCE_INLINE(uint32_t) F3(uint32_t x, uint32_t y, uint32_t z)
{
    register uint32_t r = x ^ y;
    r ^= z;
    return r;
}
DECL_FORCE_INLINE(uint32_t) F4(uint32_t x, uint32_t y, uint32_t z)
{
    register uint32_t r = ~z;
    r |= x;
    r ^= y;
    return r;
}
#endif

/* This is the central step in the MD5 algorithm. */
#define MD5STEP(f, w, x, y, z, data, s) \
    ( w += f(x, y, z) + (data),  w = w<<s | w>>(32-s),  w += x )


/**
 * The core of the MD5 algorithm, this alters an existing MD5 hash to reflect
 * the addition of 16 longwords of new data.  RTMd5Update blocks the data and
 * converts bytes into longwords for this routine.
 */
static void rtMd5Transform(uint32_t buf[4], uint32_t const in[16])
{
    uint32_t a, b, c, d;

    a = buf[0];
    b = buf[1];
    c = buf[2];
    d = buf[3];

    /*      fn, w, x, y, z, data,                 s) */
    MD5STEP(F1, a, b, c, d, in[ 0] + 0xd76aa478,  7);
    MD5STEP(F1, d, a, b, c, in[ 1] + 0xe8c7b756, 12);
    MD5STEP(F1, c, d, a, b, in[ 2] + 0x242070db, 17);
    MD5STEP(F1, b, c, d, a, in[ 3] + 0xc1bdceee, 22);
    MD5STEP(F1, a, b, c, d, in[ 4] + 0xf57c0faf,  7);
    MD5STEP(F1, d, a, b, c, in[ 5] + 0x4787c62a, 12);
    MD5STEP(F1, c, d, a, b, in[ 6] + 0xa8304613, 17);
    MD5STEP(F1, b, c, d, a, in[ 7] + 0xfd469501, 22);
    MD5STEP(F1, a, b, c, d, in[ 8] + 0x698098d8,  7);
    MD5STEP(F1, d, a, b, c, in[ 9] + 0x8b44f7af, 12);
    MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
    MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
    MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122,  7);
    MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
    MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
    MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

    MD5STEP(F2, a, b, c, d, in[ 1] + 0xf61e2562,  5);
    MD5STEP(F2, d, a, b, c, in[ 6] + 0xc040b340,  9);
    MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
    MD5STEP(F2, b, c, d, a, in[ 0] + 0xe9b6c7aa, 20);
    MD5STEP(F2, a, b, c, d, in[ 5] + 0xd62f105d,  5);
    MD5STEP(F2, d, a, b, c, in[10] + 0x02441453,  9);
    MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
    MD5STEP(F2, b, c, d, a, in[ 4] + 0xe7d3fbc8, 20);
    MD5STEP(F2, a, b, c, d, in[ 9] + 0x21e1cde6,  5);
    MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6,  9);
    MD5STEP(F2, c, d, a, b, in[ 3] + 0xf4d50d87, 14);
    MD5STEP(F2, b, c, d, a, in[ 8] + 0x455a14ed, 20);
    MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905,  5);
    MD5STEP(F2, d, a, b, c, in[ 2] + 0xfcefa3f8,  9);
    MD5STEP(F2, c, d, a, b, in[ 7] + 0x676f02d9, 14);
    MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

    MD5STEP(F3, a, b, c, d, in[ 5] + 0xfffa3942,  4);
    MD5STEP(F3, d, a, b, c, in[ 8] + 0x8771f681, 11);
    MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
    MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
    MD5STEP(F3, a, b, c, d, in[ 1] + 0xa4beea44,  4);
    MD5STEP(F3, d, a, b, c, in[ 4] + 0x4bdecfa9, 11);
    MD5STEP(F3, c, d, a, b, in[ 7] + 0xf6bb4b60, 16);
    MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
    MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6,  4);
    MD5STEP(F3, d, a, b, c, in[ 0] + 0xeaa127fa, 11);
    MD5STEP(F3, c, d, a, b, in[ 3] + 0xd4ef3085, 16);
    MD5STEP(F3, b, c, d, a, in[ 6] + 0x04881d05, 23);
    MD5STEP(F3, a, b, c, d, in[ 9] + 0xd9d4d039,  4);
    MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
    MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
    MD5STEP(F3, b, c, d, a, in[ 2] + 0xc4ac5665, 23);

    MD5STEP(F4, a, b, c, d, in[ 0] + 0xf4292244,  6);
    MD5STEP(F4, d, a, b, c, in[ 7] + 0x432aff97, 10);
    MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
    MD5STEP(F4, b, c, d, a, in[ 5] + 0xfc93a039, 21);
    MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3,  6);
    MD5STEP(F4, d, a, b, c, in[ 3] + 0x8f0ccc92, 10);
    MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
    MD5STEP(F4, b, c, d, a, in[ 1] + 0x85845dd1, 21);
    MD5STEP(F4, a, b, c, d, in[ 8] + 0x6fa87e4f,  6);
    MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
    MD5STEP(F4, c, d, a, b, in[ 6] + 0xa3014314, 15);
    MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
    MD5STEP(F4, a, b, c, d, in[ 4] + 0xf7537e82,  6);
    MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
    MD5STEP(F4, c, d, a, b, in[ 2] + 0x2ad7d2bb, 15);
    MD5STEP(F4, b, c, d, a, in[ 9] + 0xeb86d391, 21);

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}


#ifdef RT_BIG_ENDIAN
/*
 * Note: this code is harmless on little-endian machines.
 */
static void rtMd5ByteReverse(uint32_t *buf, unsigned int longs)
{
    uint32_t t;
    do
    {
        t = *buf;
        t = RT_LE2H_U32(t);
        *buf = t;
        buf++;
    } while (--longs);
}
#else   /* little endian - do nothing */
# define rtMd5ByteReverse(buf, len) do { /* Nothing */ } while (0)
#endif



/*
 * Start MD5 accumulation.  Set bit count to 0 and buffer to mysterious
 * initialization constants.
 */
RTDECL(void) RTMd5Init(PRTMD5CONTEXT pCtx)
{
    pCtx->AltPrivate.buf[0] = 0x67452301;
    pCtx->AltPrivate.buf[1] = 0xefcdab89;
    pCtx->AltPrivate.buf[2] = 0x98badcfe;
    pCtx->AltPrivate.buf[3] = 0x10325476;

    pCtx->AltPrivate.bits[0] = 0;
    pCtx->AltPrivate.bits[1] = 0;
}
RT_EXPORT_SYMBOL(RTMd5Init);


/*
 * Update context to reflect the concatenation of another buffer full
 * of bytes.
 */
RTDECL(void) RTMd5Update(PRTMD5CONTEXT pCtx, const void *pvBuf, size_t len)
{
    const uint8_t  *buf = (const uint8_t *)pvBuf;
    uint32_t        t;

    /* Update bitcount */
    t = pCtx->AltPrivate.bits[0];
    if ((pCtx->AltPrivate.bits[0] = t + ((uint32_t) len << 3)) < t)
    pCtx->AltPrivate.bits[1]++; /* Carry from low to high */
    pCtx->AltPrivate.bits[1] += (uint32_t)(len >> 29);

    t = (t >> 3) & 0x3f;        /* Bytes already in shsInfo->data */

    /* Handle any leading odd-sized chunks */
    if (t)
    {
        uint8_t *p = (uint8_t *) pCtx->AltPrivate.in + t;

        t = 64 - t;
        if (len < t)
        {
            memcpy(p, buf, len);
            return;
        }
        memcpy(p, buf, t);
        rtMd5ByteReverse(pCtx->AltPrivate.in, 16);
        rtMd5Transform(pCtx->AltPrivate.buf, pCtx->AltPrivate.in);
        buf += t;
        len -= t;
    }

    /* Process data in 64-byte chunks */
#ifndef RT_BIG_ENDIAN
    if (!((uintptr_t)buf & 0x3))
    {
        while (len >= 64) {
            rtMd5Transform(pCtx->AltPrivate.buf, (uint32_t const *)buf);
            buf += 64;
            len -= 64;
        }
    }
    else
#endif
    {
        while (len >= 64) {
            memcpy(pCtx->AltPrivate.in, buf, 64);
            rtMd5ByteReverse(pCtx->AltPrivate.in, 16);
            rtMd5Transform(pCtx->AltPrivate.buf, pCtx->AltPrivate.in);
            buf += 64;
            len -= 64;
        }
    }

    /* Handle any remaining bytes of data */
    memcpy(pCtx->AltPrivate.in, buf, len);
}
RT_EXPORT_SYMBOL(RTMd5Update);


/*
 * Final wrapup - pad to 64-byte boundary with the bit pattern
 * 1 0* (64-bit count of bits processed, MSB-first)
 */
RTDECL(void) RTMd5Final(uint8_t digest[16], PRTMD5CONTEXT pCtx)
{
    unsigned int count;
    uint8_t *p;

    /* Compute number of bytes mod 64 */
    count = (pCtx->AltPrivate.bits[0] >> 3) & 0x3F;

    /* Set the first char of padding to 0x80.  This is safe since there is
       always at least one byte free */
    p = (uint8_t *)pCtx->AltPrivate.in + count;
    *p++ = 0x80;

    /* Bytes of padding needed to make 64 bytes */
    count = 64 - 1 - count;

    /* Pad out to 56 mod 64 */
    if (count < 8)
    {
        /* Two lots of padding:  Pad the first block to 64 bytes */
        memset(p, 0, count);
        rtMd5ByteReverse(pCtx->AltPrivate.in, 16);
        rtMd5Transform(pCtx->AltPrivate.buf, pCtx->AltPrivate.in);

        /* Now fill the next block with 56 bytes */
        memset(pCtx->AltPrivate.in, 0, 56);
    }
    else
    {
        /* Pad block to 56 bytes */
        memset(p, 0, count - 8);
    }
    rtMd5ByteReverse(pCtx->AltPrivate.in, 14);

    /* Append length in bits and transform */
    pCtx->AltPrivate.in[14] = pCtx->AltPrivate.bits[0];
    pCtx->AltPrivate.in[15] = pCtx->AltPrivate.bits[1];

    rtMd5Transform(pCtx->AltPrivate.buf, pCtx->AltPrivate.in);
    rtMd5ByteReverse(pCtx->AltPrivate.buf, 4);
    memcpy(digest, pCtx->AltPrivate.buf, 16);
    memset(pCtx, 0, sizeof(*pCtx));        /* In case it's sensitive */
}
RT_EXPORT_SYMBOL(RTMd5Final);


RTDECL(void) RTMd5(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTMD5HASHSIZE])
{
#if 0
    RTMD5CONTEXT        Ctx[2];
    PRTMD5CONTEXT const pCtx = RT_ALIGN_PT(&Ctx[0], 64, PRTMD5CONTEXT);
#else
    RTMD5CONTEXT        Ctx;
    PRTMD5CONTEXT const pCtx = &Ctx;
#endif

    RTMd5Init(pCtx);
    for (;;)
    {
        uint32_t cb = (uint32_t)RT_MIN(cbBuf, _2M);
        RTMd5Update(pCtx, pvBuf, cb);
        if (cb == cbBuf)
            break;
        cbBuf -= cb;
        pvBuf  = (uint8_t const *)pvBuf + cb;
    }
    RTMd5Final(pabDigest, pCtx);
}
RT_EXPORT_SYMBOL(RTMd5);

