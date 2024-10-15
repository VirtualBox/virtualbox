/* $Id: pkix-signature-ossl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Public Key Signature Schema Algorithm, ECDSA Providers.
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
#ifdef IPRT_WITH_OPENSSL /* whole file */
# define LOG_GROUP RTLOGGROUP_CRYPTO
# include "internal/iprt.h"
# include <iprt/crypto/rsa.h>

# include <iprt/bignum.h>
# include <iprt/err.h>
# include <iprt/log.h>
# include <iprt/mem.h>
# include <iprt/string.h>
# include <iprt/crypto/digest.h>
# include <iprt/crypto/pkix.h>

# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include "internal/openssl-post.h"
# ifndef OPENSSL_VERSION_NUMBER
#  error "Missing OPENSSL_VERSION_NUMBER!"
# endif

# include "pkix-signature-builtin.h"
# include "rsa-internal.h"
# include "key-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * OpenSSL EVP signature provider instance.
 */
typedef struct RTCRPKIXSIGNATUREOSSLEVP
{
    /** Set if we're signing, clear if verifying.  */
    bool                    fSigning;
} RTCRPKIXSIGNATUREOSSLEVP;
/** Pointer to an OpenSSL EVP signature provider instance. */
typedef RTCRPKIXSIGNATUREOSSLEVP *PRTCRPKIXSIGNATUREOSSLEVP;



/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnInit}  */
static DECLCALLBACK(int) rtCrPkixSignatureOsslEvp_Init(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, void *pvOpaque,
                                                       bool fSigning, RTCRKEY hKey, PCRTASN1DYNTYPE pParams)
{
    RT_NOREF_PV(pvOpaque);

    RTCRKEYTYPE enmKeyType = RTCrKeyGetType(hKey);
    if (strcmp(pDesc->pszObjId, RTCR_X962_ECDSA_OID) == 0)
    {
        if (fSigning)
            AssertReturn(enmKeyType == RTCRKEYTYPE_ECDSA_PRIVATE, VERR_CR_PKIX_NOT_ECDSA_PRIVATE_KEY);
        else
            AssertReturn(enmKeyType == RTCRKEYTYPE_ECDSA_PUBLIC, VERR_CR_PKIX_NOT_ECDSA_PUBLIC_KEY);
    }
    else if (strcmp(pDesc->pszObjId, RTCR_PKCS1_RSA_OID) == 0)
    {
        if (fSigning)
            AssertReturn(enmKeyType == RTCRKEYTYPE_RSA_PRIVATE, VERR_CR_PKIX_NOT_RSA_PRIVATE_KEY);
        else
            AssertReturn(enmKeyType == RTCRKEYTYPE_RSA_PUBLIC, VERR_CR_PKIX_NOT_RSA_PUBLIC_KEY);
    }
    else
        AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    int rc = RTCrKeyVerifyParameterCompatibility(hKey, pParams, true /*fForSignature*/, NULL /*pAlgorithm*/, NULL /*pErrInfo*/);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Locate the EVP_MD for the algorithm.
     */
    rtCrOpenSslInit();
    int iAlgoNid = OBJ_txt2nid(pDesc->pszObjId);
    if (iAlgoNid != NID_undef)
    {
        PRTCRPKIXSIGNATUREOSSLEVP pThis = (PRTCRPKIXSIGNATUREOSSLEVP)pvState;
        pThis->fSigning = fSigning;
        return VINF_SUCCESS;
    }
    return VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP;
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnReset}  */
static DECLCALLBACK(int) rtCrPkixSignatureOsslEvp_Reset(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, bool fSigning)
{
    PRTCRPKIXSIGNATUREOSSLEVP pThis = (PRTCRPKIXSIGNATUREOSSLEVP)pvState;
    RT_NOREF_PV(fSigning); RT_NOREF_PV(pDesc);
    Assert(pThis->fSigning == fSigning); NOREF(pThis);
    return VINF_SUCCESS;
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnDelete}  */
static DECLCALLBACK(void) rtCrPkixSignatureOsslEvp_Delete(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, bool fSigning)
{
    PRTCRPKIXSIGNATUREOSSLEVP pThis = (PRTCRPKIXSIGNATUREOSSLEVP)pvState;
    RT_NOREF_PV(fSigning); RT_NOREF_PV(pDesc);
    Assert(pThis->fSigning == fSigning);
    NOREF(pThis);
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnVerify}  */
static DECLCALLBACK(int) rtCrPkixSignatureOsslEvp_Verify(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, RTCRKEY hKey,
                                                         RTCRDIGEST hDigest, void const *pvSignature, size_t cbSignature)
{
    PRTCRPKIXSIGNATUREOSSLEVP pThis = (PRTCRPKIXSIGNATUREOSSLEVP)pvState;
    Assert(!pThis->fSigning);
    RT_NOREF_PV(pThis);

#if OPENSSL_VERSION_NUMBER >= 0x10000000
    PRTERRINFO const pErrInfo = NULL;

    /*
     * Get the hash before we do anything that needs cleaning up.
     */
    int rc = RTCrDigestFinal(hDigest, NULL, 0);
    AssertRCReturn(rc, rc);
    uint8_t const * const pbDigest = RTCrDigestGetHash(hDigest);
    AssertReturn(pbDigest, VERR_INTERNAL_ERROR_3);

    uint32_t const      cbDigest = RTCrDigestGetHashSize(hDigest);
    AssertReturn(cbDigest > 0 && cbDigest < _16K, VERR_INTERNAL_ERROR_4);

    /*
     * Combine the encryption and digest algorithms.
     */
    const char * const  pszDigestOid          = RTCrDigestGetAlgorithmOid(hDigest);
    const char * const  pszEncryptedDigestOid
        = RTCrX509AlgorithmIdentifier_CombineEncryptionOidAndDigestOid(pDesc->pszObjId, pszDigestOid);

    /*
     * Create an EVP public key from hKey and pszEncryptedDigestOid.
     */
    int const           iAlgoNid = OBJ_txt2nid(pszEncryptedDigestOid);
    if (iAlgoNid == NID_undef)
        return VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP;

    /* Create an EVP public key. */
    EVP_PKEY     *pEvpPublicKey = NULL;
    const EVP_MD *pEvpMdType    = NULL;
    rc = rtCrKeyToOpenSslKeyEx(hKey, true /*fNeedPublic*/, pszEncryptedDigestOid,
                               (void **)&pEvpPublicKey, (const void **)&pEvpMdType, pErrInfo);
    if (RT_SUCCESS(rc))
    {
# if OPENSSL_VERSION_NUMBER >= 0x30000000 && !defined(LIBRESSL_VERSION_NUMBER)
        EVP_PKEY_CTX * const pEvpPublickKeyCtx = EVP_PKEY_CTX_new_from_pkey(NULL, pEvpPublicKey, NULL);
# else
        EVP_PKEY_CTX * const pEvpPublickKeyCtx = EVP_PKEY_CTX_new(pEvpPublicKey, NULL);
# endif
        if (pEvpPublickKeyCtx)
        {
            rc = EVP_PKEY_verify_init(pEvpPublickKeyCtx);
            if (rc > 0)
            {
                rc = EVP_PKEY_CTX_set_signature_md(pEvpPublickKeyCtx, pEvpMdType);
                if (rc > 0)
                {
                    rc = EVP_PKEY_verify(pEvpPublickKeyCtx, (const unsigned char *)pvSignature, cbSignature, pbDigest, cbDigest);
                    if (rc > 0)
                        rc = VINF_SUCCESS;
                    else
                        rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_VERIFY_FINAL_FAILED,
                                                 "EVP_PKEY_verify failed (%d)", rc);
                }
                else
                    rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALOG_INIT_FAILED,
                                             "EVP_PKEY_CTX_set_signature_md failed (%d)", rc);
            }
            else
                rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALOG_INIT_FAILED,
                                         "EVP_PKEY_verify_init failed (%d)", rc);
            EVP_PKEY_CTX_free(pEvpPublickKeyCtx);
        }
        else
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALOG_INIT_FAILED,
                                     "EVP_PKEY_CTX_new_from_pkey failed (%d)", rc);
        EVP_PKEY_free(pEvpPublicKey);
    }
    return rc;

#else
    RT_NOREF(pDesc, pvState, hKey, hDigest, pvSignature, cbSignature);
    return VERR_CR_OPENSSL_VERSION_TOO_OLD;
#endif
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnSign}  */
static DECLCALLBACK(int) rtCrPkixSignatureOsslEvp_Sign(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, RTCRKEY hKey,
                                                       RTCRDIGEST hDigest, void *pvSignature, size_t *pcbSignature)
{
    PRTCRPKIXSIGNATUREOSSLEVP pThis = (PRTCRPKIXSIGNATUREOSSLEVP)pvState;
    Assert(pThis->fSigning);
    RT_NOREF(pThis, pDesc);

#if 0
    /*
     * Get the key bits we need.
     */
    Assert(RTCrKeyGetType(hKey) == RTCRKEYTYPE_RSA_PRIVATE);
    PRTBIGNUM pModulus  = &hKey->u.RsaPrivate.Modulus;
    PRTBIGNUM pExponent = &hKey->u.RsaPrivate.PrivateExponent;

    return rc;
#else
    RT_NOREF(pDesc, pvState, hKey, hDigest, pvSignature, pcbSignature);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/** ECDSA alias ODIs. */
static const char * const g_apszHashWithEcdsaAliases[] =
{
    RTCR_X962_ECDSA_WITH_SHA1_OID,
    RTCR_X962_ECDSA_WITH_SHA2_OID,
    RTCR_X962_ECDSA_WITH_SHA224_OID,
    RTCR_X962_ECDSA_WITH_SHA256_OID,
    RTCR_X962_ECDSA_WITH_SHA384_OID,
    RTCR_X962_ECDSA_WITH_SHA512_OID,
    RTCR_NIST_SHA3_224_WITH_ECDSA_OID,
    RTCR_NIST_SHA3_256_WITH_ECDSA_OID,
    RTCR_NIST_SHA3_384_WITH_ECDSA_OID,
    RTCR_NIST_SHA3_512_WITH_ECDSA_OID,
    NULL
};


/** ECDSA descriptor. */
DECL_HIDDEN_CONST(RTCRPKIXSIGNATUREDESC const) g_rtCrPkixSigningHashWithEcdsaDesc =
{
    "ECDSA",
    RTCR_X962_ECDSA_OID,
    g_apszHashWithEcdsaAliases,
    sizeof(RTCRPKIXSIGNATUREOSSLEVP),
    0,
    0,
    rtCrPkixSignatureOsslEvp_Init,
    rtCrPkixSignatureOsslEvp_Reset,
    rtCrPkixSignatureOsslEvp_Delete,
    rtCrPkixSignatureOsslEvp_Verify,
    rtCrPkixSignatureOsslEvp_Sign,
};
#endif /* IPRT_WITH_OPENSSL */

