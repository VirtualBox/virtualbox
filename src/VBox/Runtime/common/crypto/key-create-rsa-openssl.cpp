/* $Id: key-create-rsa-openssl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - RSA Key Creation using OpenSSL.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/crypto/key.h>

#ifdef IPRT_WITH_OPENSSL    /* Whole file. */
# include <iprt/err.h>
# include <iprt/string.h>

# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/rsa.h>
# include <openssl/err.h>
# include "internal/openssl-post.h"

# include "key-internal.h"



RTDECL(int) RTCrKeyCreateNewRsa(PRTCRKEY phKey, uint32_t cBits, uint32_t uPubExp, uint32_t fFlags)
{
    AssertPtrReturn(phKey, VERR_INVALID_POINTER);
    AssertMsgReturn(cBits >= 128 && cBits <= _64K, ("cBits=%u\n", cBits), VERR_OUT_OF_RANGE);
    AssertReturn(uPubExp > 0, VERR_OUT_OF_RANGE);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    rtCrOpenSslInit();

    /*
     * Do the key generation first.
     */
    int rc = VERR_NO_MEMORY;
    RSA *pRsa = RSA_new();
    if (pRsa)
    {
        BIGNUM *pPubExp = BN_new();
        if (pPubExp)
        {
            if (BN_set_word(pPubExp, uPubExp) != 0)
            {
                if (RSA_generate_key_ex(pRsa, cBits, pPubExp, NULL))
                {
                    /*
                     * Create a IPRT key for it by encoding it as a private key.
                     */
                    unsigned char *pbRsaPrivateKey = NULL;
                    int cbRsaPrivateKey = i2d_RSAPrivateKey(pRsa, &pbRsaPrivateKey);
                    if (cbRsaPrivateKey > 0)
                    {
                        rc = rtCrKeyCreateRsaPrivate(phKey, pbRsaPrivateKey, cbRsaPrivateKey, NULL, NULL);
                        OPENSSL_free(pbRsaPrivateKey);
                    }
                    /* else: VERR_NO_MEMORY */
                }
                else
                    rc = VERR_CR_KEY_GEN_FAILED_RSA;
            }
            BN_free(pPubExp);
        }
        RSA_free(pRsa);
    }
    return rc;
}

#endif /* IPRT_WITH_OPENSSL */

