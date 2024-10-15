/* $Id: rc4-openssl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Alleged RC4 via OpenSSL.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt.h"
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/rc4.h>
# include "internal/openssl-post.h"
# include <iprt/crypto/rc4.h>

# include <iprt/assert.h>
# include <iprt/assertcompile.h>



RTDECL(void) RTCrRc4SetKey(PRTCRRC4KEY pKey, size_t cbData, void const *pvData)
{

    AssertCompile(sizeof(RC4_INT) == 4 ? sizeof(pKey->au64Padding) == sizeof(pKey->Ossl) : sizeof(pKey->au64Padding) >= sizeof(pKey->Ossl));
    Assert((int)cbData == (ssize_t)cbData);
    AssertPtr(pKey);

    RC4_set_key(&pKey->Ossl, (int)cbData, (const unsigned char *)pvData);
}


RTDECL(void) RTCrRc4(PRTCRRC4KEY pKey, size_t cbData, void const *pvDataIn, void *pvDataOut)
{
    AssertCompile(sizeof(RC4_INT) == 4 ? sizeof(pKey->au64Padding) == sizeof(pKey->Ossl) : sizeof(pKey->au64Padding) >= sizeof(pKey->Ossl));
    Assert((int)cbData == (ssize_t)cbData);
    AssertPtr(pKey);

    RC4(&pKey->Ossl, (int)cbData, (const unsigned char *)pvDataIn, (unsigned char *)pvDataOut);
}

#endif /* IPRT_WITH_OPENSSL */

