/* $Id: pkix-verify.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/key.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include "internal/openssl-post.h"
# ifndef OPENSSL_VERSION_NUMBER
#  error "Missing OPENSSL_VERSION_NUMBER!"
# endif
#endif



RTDECL(int) RTCrPkixPubKeyVerifySignature(PCRTASN1OBJID pAlgorithm, RTCRKEY hPublicKey, PCRTASN1DYNTYPE pParameters,
                                          PCRTASN1BITSTRING pSignatureValue, const void *pvData, size_t cbData,
                                          PRTERRINFO pErrInfo)
{
    /*
     * Valid input.
     */
    AssertPtrReturn(pAlgorithm, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1ObjId_IsPresent(pAlgorithm), VERR_INVALID_POINTER);

    AssertPtrReturn(hPublicKey, VERR_INVALID_POINTER);
    Assert(RTCrKeyHasPublicPart(hPublicKey));
    RTCRKEYTYPE const enmKeyType = RTCrKeyGetType(hPublicKey);
    AssertReturn(enmKeyType != RTCRKEYTYPE_INVALID, VERR_INVALID_HANDLE);

    AssertPtrReturn(pSignatureValue, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1BitString_IsPresent(pSignatureValue), VERR_INVALID_POINTER);

    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData > 0, VERR_INVALID_PARAMETER);

    /*
     * Verify that the parameters are compatible with the key.  We ASSUME the
     * parameters are for a hash+cryption combination, like those found in
     * RTCRX509TBSCERTIFICATE::Signature.  At present, these should NULL (or
     * absent) for the two key types we support RSA & ECDSA, which is an
     * ASSUMPTION by the OpenSSL code below.
     */
    int rcIprt = RTCrKeyVerifyParameterCompatibility(hPublicKey, pParameters, true /*fForSignature*/, pAlgorithm, pErrInfo);
    AssertRCReturn(rcIprt, rcIprt);

    /*
     * Validate using IPRT.
     */
    RTCRPKIXSIGNATURE hSignature;
    rcIprt = RTCrPkixSignatureCreateByObjId(&hSignature, pAlgorithm, hPublicKey, pParameters, false /*fSigning*/);
    if (RT_FAILURE(rcIprt))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_CIPHER_ALGO_NOT_KNOWN,
                             "Unknown public key algorithm [IPRT %Rrc]: %s", rcIprt, pAlgorithm->szObjId);

    RTCRDIGEST hDigest;
    rcIprt = RTCrDigestCreateByObjId(&hDigest, pAlgorithm);
    if (RT_SUCCESS(rcIprt))
    {
        /* Calculate the digest. */
        rcIprt = RTCrDigestUpdate(hDigest, pvData, cbData);
        if (RT_SUCCESS(rcIprt))
        {
            rcIprt = RTCrPkixSignatureVerifyBitString(hSignature, hDigest, pSignatureValue);
            if (RT_FAILURE(rcIprt))
                RTErrInfoSet(pErrInfo, rcIprt, "RTCrPkixSignatureVerifyBitString failed");
        }
        else
            RTErrInfoSet(pErrInfo, rcIprt, "RTCrDigestUpdate failed");
        RTCrDigestRelease(hDigest);
    }
    else
        RTErrInfoSetF(pErrInfo, rcIprt, "Unknown digest algorithm [IPRT]: %s", pAlgorithm->szObjId);
    RTCrPkixSignatureRelease(hSignature);

#ifdef IPRT_WITH_OPENSSL
    /* We don't implement digest+cipher parameters in OpenSSL (or at all),
       RTCrKeyVerifyParameterCompatibility should ensure we don't get here
       (ASSUMING only RSA and ECDSA keys). But, just in case, bail out if we do. */
    AssertReturn(   !pParameters
                 || pParameters->enmType == RTASN1TYPE_NULL
                 || pParameters->enmType == RTASN1TYPE_NOT_PRESENT,
                 VERR_CR_PKIX_CIPHER_ALGO_PARAMS_NOT_IMPL);

    /*
     * Validate using OpenSSL EVP.
     */
    /* Create an EVP public key. */
    EVP_PKEY     *pEvpPublicKey = NULL;
    const EVP_MD *pEvpMdType = NULL;
    int rcOssl = rtCrKeyToOpenSslKeyEx(hPublicKey, true /*fNeedPublic*/, pAlgorithm->szObjId,
                                       (void **)&pEvpPublicKey, (const void **)&pEvpMdType, pErrInfo);
    if (RT_SUCCESS(rcOssl))
    {
        EVP_MD_CTX *pEvpMdCtx = EVP_MD_CTX_create();
        if (pEvpMdCtx)
        {
            if (EVP_VerifyInit_ex(pEvpMdCtx, pEvpMdType, NULL /*engine*/))
            {
                /* Digest the data. */
                EVP_VerifyUpdate(pEvpMdCtx, pvData, cbData);

                /* Verify the signature. */
                if (EVP_VerifyFinal(pEvpMdCtx,
                                    RTASN1BITSTRING_GET_BIT0_PTR(pSignatureValue),
                                    RTASN1BITSTRING_GET_BYTE_SIZE(pSignatureValue),
                                    pEvpPublicKey) > 0)
                    rcOssl = VINF_SUCCESS;
                else
                    rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKIX_OSSL_VERIFY_FINAL_FAILED, "EVP_VerifyFinal failed");

                /* Cleanup and return: */
            }
            else
                rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALOG_INIT_FAILED,
                                       "EVP_VerifyInit_ex failed (algorithm type is %s)", pAlgorithm->szObjId);
            EVP_MD_CTX_destroy(pEvpMdCtx);
        }
        else
            rcOssl = RTErrInfoSetF(pErrInfo, VERR_NO_MEMORY, "EVP_MD_CTX_create failed");
        EVP_PKEY_free(pEvpPublicKey);
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


RTDECL(int) RTCrPkixPubKeyVerifySignedDigest(PCRTASN1OBJID pAlgorithm, RTCRKEY hPublicKey, PCRTASN1DYNTYPE pParameters,
                                             void const *pvSignedDigest, size_t cbSignedDigest, RTCRDIGEST hDigest,
                                             PRTERRINFO pErrInfo)
{
    /*
     * Valid input.
     */
    AssertPtrReturn(pAlgorithm, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1ObjId_IsPresent(pAlgorithm), VERR_INVALID_POINTER);

    AssertPtrReturn(hPublicKey, VERR_INVALID_POINTER);
    Assert(RTCrKeyHasPublicPart(hPublicKey));
    RTCRKEYTYPE const enmKeyType = RTCrKeyGetType(hPublicKey);
    AssertReturn(enmKeyType != RTCRKEYTYPE_INVALID, VERR_INVALID_HANDLE);

    AssertPtrReturn(pvSignedDigest, VERR_INVALID_POINTER);
    AssertReturn(cbSignedDigest, VERR_INVALID_PARAMETER);

    AssertPtrReturn(hDigest, VERR_INVALID_HANDLE);

    /*
     * Verify that the parameters are compatible with the key.  We ASSUME the
     * parameters are for a hash+cryption combination, like those found in
     * RTCRX509TBSCERTIFICATE::Signature.  At present, these should NULL (or
     * absent) for the two key types we support RSA & ECDSA, which is an
     * ASSUMPTION by the OpenSSL code below.
     */
    int rcIprt = RTCrKeyVerifyParameterCompatibility(hPublicKey, pParameters, true /*fForSignature*/, pAlgorithm, pErrInfo);
    AssertRCReturn(rcIprt, rcIprt);

    /*
     * Validate using IPRT.
     */
    RTCRPKIXSIGNATURE hSignature;
    rcIprt = RTCrPkixSignatureCreateByObjId(&hSignature, pAlgorithm, hPublicKey, pParameters, false /*fSigning*/);
    if (RT_FAILURE(rcIprt))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_CIPHER_ALGO_NOT_KNOWN,
                             "Unknown public key algorithm [IPRT %Rrc]: %s", rcIprt, pAlgorithm->szObjId);

    rcIprt = RTCrPkixSignatureVerify(hSignature, hDigest, pvSignedDigest, cbSignedDigest);
    if (RT_FAILURE(rcIprt))
        RTErrInfoSet(pErrInfo, rcIprt, "RTCrPkixSignatureVerifyBitString failed");

    RTCrPkixSignatureRelease(hSignature);

#if defined(IPRT_WITH_OPENSSL) \
  && (OPENSSL_VERSION_NUMBER > 0x10000000L) /* 0.9.8 doesn't seem to have EVP_PKEY_CTX_set_signature_md. */
    /*
     * Validate using OpenSSL EVP.
     */
    /* Make sure the algorithm includes the digest and isn't just RSA, ECDSA or similar.  */
    const char *pszAlgObjId = RTCrX509AlgorithmIdentifier_CombineEncryptionOidAndDigestOid(pAlgorithm->szObjId,
                                                                                           RTCrDigestGetAlgorithmOid(hDigest));
    AssertMsgStmt(pszAlgObjId, ("enc=%s hash=%s\n", pAlgorithm->szObjId, RTCrDigestGetAlgorithmOid(hDigest)),
                  pszAlgObjId = RTCrDigestGetAlgorithmOid(hDigest));

    /* We don't implement digest+cipher parameters in OpenSSL (or at all),
       RTCrKeyVerifyParameterCompatibility should ensure we don't get here
       (ASSUMING only RSA and ECDSA keys). But, just in case, bail out if we do. */
    AssertReturn(   !pParameters
                 || pParameters->enmType == RTASN1TYPE_NULL
                 || pParameters->enmType == RTASN1TYPE_NOT_PRESENT,
                 VERR_CR_PKIX_CIPHER_ALGO_PARAMS_NOT_IMPL);

    /* Create an EVP public key. */
    EVP_PKEY     *pEvpPublicKey = NULL;
    const EVP_MD *pEvpMdType = NULL;
    int rcOssl = rtCrKeyToOpenSslKeyEx(hPublicKey, true /*fNeedPublic*/, pszAlgObjId,
                                       (void **)&pEvpPublicKey, (const void **)&pEvpMdType, pErrInfo);
    if (RT_SUCCESS(rcOssl))
    {
        /* Create an EVP public key context we can use to validate the digest. */
        EVP_PKEY_CTX *pEvpPKeyCtx = EVP_PKEY_CTX_new(pEvpPublicKey, NULL);
        if (pEvpPKeyCtx)
        {
            rcOssl = EVP_PKEY_verify_init(pEvpPKeyCtx);
            if (rcOssl > 0)
            {
                rcOssl = EVP_PKEY_CTX_set_signature_md(pEvpPKeyCtx, pEvpMdType);
                if (rcOssl > 0)
                {
                    /* Get the digest from hDigest and verify it. */
                    rcOssl = EVP_PKEY_verify(pEvpPKeyCtx,
                                             (uint8_t const *)pvSignedDigest,
                                             cbSignedDigest,
                                             RTCrDigestGetHash(hDigest),
                                             RTCrDigestGetHashSize(hDigest));
                    if (rcOssl > 0)
                        rcOssl = VINF_SUCCESS;
                    else
                        rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_VERIFY_FINAL_FAILED,
                                               "EVP_PKEY_verify failed (%d)", rcOssl);
                    /* Cleanup and return: */
                }
                else
                    rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR,
                                           "EVP_PKEY_CTX_set_signature_md failed (%d)", rcOssl);
            }
            else
                rcOssl = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR,
                                       "EVP_PKEY_verify_init failed (%d)", rcOssl);
            EVP_PKEY_CTX_free(pEvpPKeyCtx);
        }
        else
            rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR, "EVP_PKEY_CTX_new failed");
        EVP_PKEY_free(pEvpPublicKey);
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


RTDECL(int) RTCrPkixPubKeyVerifySignedDigestByCertPubKeyInfo(PCRTCRX509SUBJECTPUBLICKEYINFO pCertPubKeyInfo,
                                                             void const *pvSignedDigest, size_t cbSignedDigest,
                                                             RTCRDIGEST hDigest, PRTERRINFO pErrInfo)
{
    RTCRKEY hPublicKey;
    int rc = RTCrKeyCreateFromPublicAlgorithmAndBits(&hPublicKey, &pCertPubKeyInfo->Algorithm.Algorithm,
                                                     &pCertPubKeyInfo->Algorithm.Parameters,
                                                     &pCertPubKeyInfo->SubjectPublicKey, pErrInfo, NULL);
    if (RT_SUCCESS(rc))
    {
        /** @todo r=bird (2023-07-06): This ASSUMES no digest+cipher parameters, which
         *        is the case for RSA and ECDSA. */
        rc = RTCrPkixPubKeyVerifySignedDigest(&pCertPubKeyInfo->Algorithm.Algorithm, hPublicKey, NULL,
                                              pvSignedDigest, cbSignedDigest, hDigest, pErrInfo);

        uint32_t cRefs = RTCrKeyRelease(hPublicKey);
        Assert(cRefs == 0); RT_NOREF(cRefs);
    }
    return rc;
}

