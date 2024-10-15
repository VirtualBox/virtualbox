/* $Id: openssl-sha512.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-512 hash functions.
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

#define RT_SHA512_PRIVATE_CONTEXT
#include <iprt/sha.h>

#include <iprt/assert.h>
#include <iprt/string.h>


AssertCompile(RT_SIZEOFMEMB(RTSHA512CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTSHA512CONTEXT, Private));


RTDECL(void) RTSha512(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA512_HASH_SIZE])
{
    RTSHA512CONTEXT Ctx;
    RTSha512Init(&Ctx);
    RTSha512Update(&Ctx, pvBuf, cbBuf);
    RTSha512Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha512);


RTDECL(bool) RTSha512Check(const void *pvBuf, size_t cbBuf, uint8_t const pabDigest[RTSHA512_HASH_SIZE])
{
    RTSHA512CONTEXT Ctx;
    RTSha512Init(&Ctx);
    RTSha512Update(&Ctx, pvBuf, cbBuf);
    uint8_t abActualDigest[RTSHA512_HASH_SIZE];
    RTSha512Final(&Ctx, abActualDigest);
    bool fRet = memcmp(pabDigest, abActualDigest, RTSHA512_HASH_SIZE) == 0;
    RT_ZERO(abActualDigest);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha512Check);


RTDECL(void) RTSha512Init(PRTSHA512CONTEXT pCtx)
{
    SHA512_Init(&pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha512Init);


RTDECL(void) RTSha512Update(PRTSHA512CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    SHA512_Update(&pCtx->Private, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha512Update);


RTDECL(void) RTSha512Final(PRTSHA512CONTEXT pCtx, uint8_t pabDigest[RTSHA512_HASH_SIZE])
{
    SHA512_Final((unsigned char *)&pabDigest[0], &pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha512Final);


/*
 * We have to expose the same API as alt-sha512.cpp, so the SHA-384,
 * SHA-512/224 and SHA-512/256 implementations also live here. (They are all
 * just truncted SHA-512 with different initial values.)
 */

RTDECL(void) RTSha384(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTSHA384_HASH_SIZE])
{
    RTSHA384CONTEXT Ctx;
    RTSha384Init(&Ctx);
    RTSha384Update(&Ctx, pvBuf, cbBuf);
    RTSha384Final(&Ctx, pabDigest);
}
RT_EXPORT_SYMBOL(RTSha384);


RTDECL(bool) RTSha384Check(const void *pvBuf, size_t cbBuf, uint8_t const pabDigest[RTSHA384_HASH_SIZE])
{
    RTSHA384CONTEXT Ctx;
    RTSha384Init(&Ctx);
    RTSha384Update(&Ctx, pvBuf, cbBuf);
    uint8_t abActualDigest[RTSHA384_HASH_SIZE];
    RTSha384Final(&Ctx, abActualDigest);
    bool fRet = memcmp(pabDigest, abActualDigest, RTSHA384_HASH_SIZE) == 0;
    RT_ZERO(abActualDigest);
    return fRet;
}
RT_EXPORT_SYMBOL(RTSha384Check);


RTDECL(void) RTSha384Init(PRTSHA384CONTEXT pCtx)
{
    SHA384_Init(&pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha384Init);


RTDECL(void) RTSha384Update(PRTSHA384CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    SHA384_Update(&pCtx->Private, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTSha384Update);


RTDECL(void) RTSha384Final(PRTSHA384CONTEXT pCtx, uint8_t pabDigest[RTSHA384_HASH_SIZE])
{
    SHA384_Final((unsigned char *)&pabDigest[0], &pCtx->Private);
}
RT_EXPORT_SYMBOL(RTSha384Final);

