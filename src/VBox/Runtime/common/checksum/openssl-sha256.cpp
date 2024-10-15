/* $Id: openssl-sha256.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-256 hash functions.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"

#include "internal/openssl-pre.h"
#include <openssl/sha.h>
#include "internal/openssl-post.h"

#define RT_SHA256_PRIVATE_CONTEXT
#include <iprt/sha.h>

#include <iprt/assert.h>
#include <iprt/string.h>


AssertCompile(RT_SIZEOFMEMB(RTSHA256CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTSHA256CONTEXT, Private));


RTDECL(void) RTSha256(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA256_HASH_SIZE])
{
    RTSHA256CONTEXT Ctx;
    RTSha256Init(&Ctx);
    RTSha256Update(&Ctx, pvBuf, cbBuf);
    RTSha256Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha256);


RTDECL(bool) RTSha256Check(const void *pvBuf, size_t cbBuf, uint8_t const pabDigest[RTSHA256_HASH_SIZE])
{
    RTSHA256CONTEXT Ctx;
    RTSha256Init(&Ctx);
    RTSha256Update(&Ctx, pvBuf, cbBuf);
    uint8_t abActualDigest[RTSHA256_HASH_SIZE];
    RTSha256Final(&Ctx, abActualDigest);
    bool fRet = memcmp(pabDigest, abActualDigest, RTSHA256_HASH_SIZE) == 0;
    RT_ZERO(abActualDigest);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha256Check);


RTDECL(void) RTSha256Init(PRTSHA256CONTEXT pCtx)
{
    SHA256_Init(&pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha256Init);


RTDECL(void) RTSha256Update(PRTSHA256CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    SHA256_Update(&pCtx->Private, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha256Update);


RTDECL(void) RTSha256Final(PRTSHA256CONTEXT pCtx, uint8_t pabDigest[RTSHA256_HASH_SIZE])
{
    SHA256_Final((unsigned char *)&pabDigest[0], &pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha256Final);


/*
 * We have to expose the same API as alt-sha256.cpp, so the SHA-224
 * implementation also lives here. (SHA-224 is just a truncated SHA-256 with
 * different initial values.)
 */
RTDECL(void) RTSha224(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA224_HASH_SIZE])
{
    RTSHA224CONTEXT Ctx;
    RTSha224Init(&Ctx);
    RTSha224Update(&Ctx, pvBuf, cbBuf);
    RTSha224Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha224);


RTDECL(bool) RTSha224Check(const void *pvBuf, size_t cbBuf, uint8_t const pabDigest[RTSHA224_HASH_SIZE])
{
    RTSHA224CONTEXT Ctx;
    RTSha224Init(&Ctx);
    RTSha224Update(&Ctx, pvBuf, cbBuf);
    uint8_t abActualDigest[RTSHA224_HASH_SIZE];
    RTSha224Final(&Ctx, abActualDigest);
    bool fRet = memcmp(pabDigest, abActualDigest, RTSHA224_HASH_SIZE) == 0;
    RT_ZERO(abActualDigest);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha224Check);


RTDECL(void) RTSha224Init(PRTSHA224CONTEXT pCtx)
{
    SHA224_Init(&pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha224Init);


RTDECL(void) RTSha224Update(PRTSHA224CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    SHA224_Update(&pCtx->Private, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha224Update);


RTDECL(void) RTSha224Final(PRTSHA224CONTEXT pCtx, uint8_t pabDigest[RTSHA224_HASH_SIZE])
{
    SHA224_Final((unsigned char *)&pabDigest[0], &pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha224Final);

