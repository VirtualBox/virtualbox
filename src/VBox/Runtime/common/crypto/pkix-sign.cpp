/* $Id: pkix-sign.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Public Key Infrastructure API, Verification.
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
#include "internal/iprt.h"
#include <iprt/crypto/pkix.h>

#include <iprt/alloca.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/key.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include <openssl/rsa.h>
# include "internal/openssl-post.h"
# ifndef OPENSSL_VERSION_NUMBER
#  error "Missing OPENSSL_VERSION_NUMBER!"
# endif
#endif


#if 0
RTDECL(int) RTCrPkixPubKeySignData(PCRTASN1OBJID pAlgorithm, PCRTASN1DYNTYPE pParameters, PCRTASN1BITSTRING pPrivateKey,
                                   PCRTASN1BITSTRING pSignatureValue, const void *pvData, size_t cbData, PRTERRINFO pErrInfo)
{
    /*
     * Validate the digest related inputs.
     */
    AssertPtrReturn(pAlgorithm, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1ObjId_IsPresent(pAlgorithm), VERR_INVALID_POINTER);

    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData > 0, VERR_INVALID_PARAMETER);

    /*
     * Digest the data and call the other API.
     */
    RTCRDIGEST hDigest;
    int rc = RTCrDigestCreateByObjId(&hDigest, pAlgorithm);
    if (RT_SUCCESS(rcIprt))
    {
        rc = RTCrDigestUpdate(hDigest, pvData, cbData);
        if (RT_SUCCESS(rcIprt))
            rc = RTCrPkixPubKeySignDigest(pAlgorithm, pParameters, pPrivateKey, pvSignedDigest, cbSignedDigest, hDigest, pErrInfo);
        else
            RTErrInfoSet(pErrInfo, rcIprt, "RTCrDigestUpdate failed");
        RTCrDigestRelease(hDigest);
    }
    else
        RTErrInfoSetF(pErrInfo, rcIprt, "Unknown digest algorithm [IPRT]: %s", pAlgorithm->szObjId);
    return rc;
}
#endif


RTDECL(int) RTCrPkixPubKeySignDigest(PCRTASN1OBJID pAlgorithm, RTCRKEY hPrivateKey, PCRTASN1DYNTYPE pParameters,
                                     RTCRDIGEST hDigest, uint32_t fFlags,
                                     void *pvSignature, size_t *pcbSignature, PRTERRINFO pErrInfo)
{
    /*
     * Valid input.
     */
    AssertPtrReturn(pAlgorithm, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1ObjId_IsPresent(pAlgorithm), VERR_INVALID_POINTER);

    if (pParameters)
    {
        AssertPtrReturn(pParameters, VERR_INVALID_POINTER);
        if (pParameters->enmType == RTASN1TYPE_NULL)
            pParameters = NULL;
    }

    AssertPtrReturn(hPrivateKey, VERR_INVALID_POINTER);
    Assert(RTCrKeyHasPrivatePart(hPrivateKey));

    AssertPtrReturn(pcbSignature, VERR_INVALID_PARAMETER);
    size_t cbSignature = *pcbSignature;
    if (cbSignature)
        AssertPtrReturn(pvSignature, VERR_INVALID_POINTER);
    else
        pvSignature = NULL;

    AssertPtrReturn(hDigest, VERR_INVALID_HANDLE);

    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);

    /*
     * Parameters are not currently supported (openssl code path).
     */
    if (pParameters)
        return RTErrInfoSet(pErrInfo, VERR_CR_PKIX_CIPHER_ALGO_PARAMS_NOT_IMPL,
                            "Cipher algorithm parameters are not yet supported.");

    /*
     * Sign using IPRT.
     */
    RTCRPKIXSIGNATURE hSignature;
    int rcIprt = RTCrPkixSignatureCreateByObjId(&hSignature, pAlgorithm, hPrivateKey, pParameters, true /*fSigning*/);
    if (RT_FAILURE(rcIprt))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_CIPHER_ALGO_NOT_KNOWN,
                             "Unknown private key algorithm [IPRT %Rrc]: %s", rcIprt, pAlgorithm->szObjId);

    rcIprt = RTCrPkixSignatureSign(hSignature, hDigest, pvSignature, pcbSignature);
    if (RT_FAILURE(rcIprt))
        RTErrInfoSet(pErrInfo, rcIprt, "RTCrPkixSignatureSign failed");

    RTCrPkixSignatureRelease(hSignature);

    /*
     * Sign using OpenSSL EVP if we can.
     */
#if defined(IPRT_WITH_OPENSSL) \
  && (OPENSSL_VERSION_NUMBER > 0x10000000L) /* 0.9.8 doesn't seem to have EVP_PKEY_CTX_set_signature_md. */

    /* Make sure the algorithm includes the digest and isn't just RSA, ECDSA or similar.  */
    const char *pszAlgObjId = RTCrX509AlgorithmIdentifier_CombineEncryptionOidAndDigestOid(pAlgorithm->szObjId,
                                                                                           RTCrDigestGetAlgorithmOid(hDigest));
    AssertMsgStmt(pszAlgObjId, ("enc=%s hash=%s\n", pAlgorithm->szObjId, RTCrDigestGetAlgorithmOid(hDigest)),
                  pszAlgObjId = RTCrDigestGetAlgorithmOid(hDigest));

    /* Create an EVP private key. */
    EVP_PKEY     *pEvpPrivateKey = NULL;
    const EVP_MD *pEvpMdType = NULL;
    int rcOssl = rtCrKeyToOpenSslKeyEx(hPrivateKey, false /*fNeedPublic*/, pszAlgObjId,
                                       (void **)&pEvpPrivateKey, (const void **)&pEvpMdType, pErrInfo);
    if (RT_SUCCESS(rcOssl))
    {
        /* Create an EVP Private key context we can use to validate the digest. */
        EVP_PKEY_CTX *pEvpPKeyCtx = EVP_PKEY_CTX_new(pEvpPrivateKey, NULL);
        if (pEvpPKeyCtx)
        {
            rcOssl = EVP_PKEY_sign_init(pEvpPKeyCtx);
            if (rcOssl > 0)
            {
                rcOssl = EVP_PKEY_CTX_set_rsa_padding(pEvpPKeyCtx, RSA_PKCS1_PADDING);
                if (rcOssl > 0)
                {
                    rcOssl = EVP_PKEY_CTX_set_signature_md(pEvpPKeyCtx, pEvpMdType);
                    if (rcOssl > 0)
                    {
                        /* Allocate a signature buffer. */
                        unsigned char *pbOsslSignature     = NULL;
                        void          *pvOsslSignatureFree = NULL;
                        size_t         cbOsslSignature     = cbSignature;
                        if (cbOsslSignature > 0)
                        {
                            if (cbOsslSignature < _1K)
                                pbOsslSignature = (unsigned char *)alloca(cbOsslSignature);
                            else
                            {
                                pbOsslSignature = (unsigned char *)RTMemTmpAlloc(cbOsslSignature);
                                pvOsslSignatureFree = pbOsslSignature;
                            }
                        }
                        if (cbOsslSignature == 0 || pbOsslSignature != NULL)
                        {
                            /* Get the digest from hDigest and sign it. */
                            rcOssl = EVP_PKEY_sign(pEvpPKeyCtx,
                                                   pbOsslSignature,
                                                   &cbOsslSignature,
                                                   (const unsigned char *)RTCrDigestGetHash(hDigest),
                                                   RTCrDigestGetHashSize(hDigest));
                            if (rcOssl > 0)
                            {
                                /* Compare the result.  The memcmp assums no random padding bits. */
                                rcOssl = VINF_SUCCESS;
                                AssertMsgStmt(cbOsslSignature == *pcbSignature,
                                              ("cbOsslSignature=%#x, iprt %#x\n", cbOsslSignature, *pcbSignature),
                                              rcOssl = VERR_CR_PKIX_OSSL_VS_IPRT_SIGNATURE_SIZE);
                                AssertMsgStmt(   pbOsslSignature == NULL
                                              || rcOssl != VINF_SUCCESS
                                              || memcmp(pbOsslSignature, pvSignature, cbOsslSignature) == 0,
                                              ("OpenSSL: %.*Rhxs\n"
                                               "IPRT:    %.*Rhxs\n",
                                               cbOsslSignature, pbOsslSignature, *pcbSignature, pvSignature),
                                              rcOssl = VERR_CR_PKIX_OSSL_VS_IPRT_SIGNATURE);
                                if (!pbOsslSignature && rcOssl == VINF_SUCCESS)
                                    rcOssl = VERR_BUFFER_OVERFLOW;
                            }
                            else
                                rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_SIGN_FINAL_FAILED,
                                                       "EVP_PKEY_sign failed (%d)", rcOssl);
                            if (pvOsslSignatureFree)
                                RTMemTmpFree(pvOsslSignatureFree);
                        }
                        else
                            rcOssl = VERR_NO_TMP_MEMORY;
                    }
                    else
                        rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR,
                                               "EVP_PKEY_CTX_set_signature_md failed (%d)", rcOssl);
                }
                else
                    rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_RSA_PAD_ERROR,
                                           "EVP_PKEY_CTX_set_rsa_padding failed (%d)", rcOssl);
            }
            else
                rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR,
                                       "EVP_PKEY_verify_init failed (%d)", rcOssl);
            EVP_PKEY_CTX_free(pEvpPKeyCtx);
        }
        else
            rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR, "EVP_PKEY_CTX_new failed");
        EVP_PKEY_free(pEvpPrivateKey);
    }

    /*
     * Check the result.
     */
    if (   (RT_SUCCESS(rcIprt) && RT_SUCCESS(rcOssl))
        || (RT_FAILURE_NP(rcIprt) && RT_FAILURE_NP(rcOssl))
        || (RT_SUCCESS(rcIprt) && rcOssl == VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP) )
        return rcIprt;
    AssertMsgFailed(("rcIprt=%Rrc rcOssl=%Rrc\n", rcIprt, rcOssl));
    if (RT_FAILURE_NP(rcOssl))
        return rcOssl;
#endif /* IPRT_WITH_OPENSSL */

    return rcIprt;
}

