/* $Id: openssl-md5.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - MD5 message digest functions, implemented using OpenSSL.
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
#include <openssl/md5.h>
#include "internal/openssl-post.h"

#define RT_MD5_OPENSSL_PRIVATE_CONTEXT
#include <iprt/md5.h>

#include <iprt/assert.h>

AssertCompile(RT_SIZEOFMEMB(RTMD5CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTMD5CONTEXT, OsslPrivate));


RTDECL(void) RTMd5(const void *pvBuf, size_t cbBuf, uint8_t pabDigest[RTMD5_HASH_SIZE])
{
    RTMD5CONTEXT Ctx;
    RTMd5Init(&Ctx);
    RTMd5Update(&Ctx, pvBuf, cbBuf);
    RTMd5Final(pabDigest, &Ctx);
}
RT_EXPORT_SYMBOL(RTMd5);


RTDECL(void) RTMd5Init(PRTMD5CONTEXT pCtx)
{
    MD5_Init(&pCtx->OsslPrivate);
}
RT_EXPORT_SYMBOL(RTMd5Init);


RTDECL(void) RTMd5Update(PRTMD5CONTEXT pCtx, const void *pvBuf, size_t cbBuf)
{
    MD5_Update(&pCtx->OsslPrivate, pvBuf, cbBuf);
}
RT_EXPORT_SYMBOL(RTMd5Update);


RTDECL(void) RTMd5Final(uint8_t pabDigest[RTMD5_HASH_SIZE], PRTMD5CONTEXT pCtx)
{
    MD5_Final((unsigned char *)&pabDigest[0], &pCtx->OsslPrivate);
}
RT_EXPORT_SYMBOL(RTMd5Final);

