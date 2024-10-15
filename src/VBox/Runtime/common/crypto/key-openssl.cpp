/* $Id: key-openssl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Cryptographic Keys, OpenSSL glue.
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
#define LOG_GROUP RTLOGGROUP_CRYPTO
#include "internal/iprt.h"
#include <iprt/crypto/key.h>

#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/magics.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include "internal/openssl-post.h"
# ifndef OPENSSL_VERSION_NUMBER
#  error "Missing OPENSSL_VERSION_NUMBER!"
# endif
# if OPENSSL_VERSION_NUMBER < 0x30000000 || defined(LIBRESSL_VERSION_NUMBER)
#  include "openssl/x509.h"
#  include <iprt/crypto/x509.h>
# endif

# include "key-internal.h"


/**
 * Helper that loads key parameters and the actual key bits if present.
 */
static int rtCrKeyToOpenSslKeyLoad(RTCRKEY hKey, int idKeyType, EVP_PKEY **ppEvpNewKey, bool fNeedPublic, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    if (   hKey->enmType == RTCRKEYTYPE_ECDSA_PUBLIC
        || hKey->enmType == RTCRKEYTYPE_ECDSA_PRIVATE)
    {
# if OPENSSL_VERSION_NUMBER >= 0x30000000 && !defined(LIBRESSL_VERSION_NUMBER)
        void          *pvFree = NULL;
        const uint8_t *pbRaw  = NULL;
        uint32_t       cbRaw  = 0;
        if (hKey->enmType == RTCRKEYTYPE_ECDSA_PUBLIC)
            rc = RTAsn1EncodeQueryRawBits(&hKey->u.EcdsaPublic.NamedCurve.Asn1Core, &pbRaw, &cbRaw, &pvFree, pErrInfo);
        else
            AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
        if (RT_SUCCESS(rc))
        {
            const unsigned char *puchParams = pbRaw;
            EVP_PKEY *pRet = d2i_KeyParams(idKeyType, ppEvpNewKey, &puchParams, cbRaw);
            if (pRet != NULL && pRet == *ppEvpNewKey)
                rc = VINF_SUCCESS;
            else
                rc = RTERRINFO_LOG_SET(pErrInfo, VERR_CR_PKIX_OSSL_D2I_KEY_PARAMS_FAILED, "d2i_KeyParams failed");

            RTMemTmpFree(pvFree);
        }
#else
        /*
         * Cannot find any real suitable alternative to d2i_KeyParams in pre-3.0.x
         * OpenSSL, so decided to use d2i_PUBKEY instead.  This means we need to
         * encode the stuff a X.509 SubjectPublicKeyInfo ASN.1 sequence first.
         */
        if (hKey->enmType == RTCRKEYTYPE_ECDSA_PUBLIC)
        {
            RTCRX509SUBJECTPUBLICKEYINFO PubKeyInfo;
            rc = RTCrX509SubjectPublicKeyInfo_Init(&PubKeyInfo, &g_RTAsn1DefaultAllocator);
            AssertRCReturn(rc, rc);

            rc = RTAsn1ObjId_SetFromString(&PubKeyInfo.Algorithm.Algorithm, RTCRX509ALGORITHMIDENTIFIERID_ECDSA,
                                           &g_RTAsn1DefaultAllocator);
            if (RT_SUCCESS(rc))
                rc = RTAsn1DynType_SetToObjId(&PubKeyInfo.Algorithm.Parameters, &hKey->u.EcdsaPublic.NamedCurve,
                                              &g_RTAsn1DefaultAllocator);
            if (RT_SUCCESS(rc))
            {
                RTAsn1BitString_Delete(&PubKeyInfo.SubjectPublicKey);
                rc = RTAsn1BitString_InitWithData(&PubKeyInfo.SubjectPublicKey, hKey->pbEncoded, hKey->cbEncoded * 8,
                                                  &g_RTAsn1DefaultAllocator);
                if (RT_SUCCESS(rc))
                {
                    /* Encode the whole shebang. */
                    void          *pvFree = NULL;
                    const uint8_t *pbRaw  = NULL;
                    uint32_t       cbRaw  = 0;
                    rc = RTAsn1EncodeQueryRawBits(&PubKeyInfo.SeqCore.Asn1Core, &pbRaw, &cbRaw, &pvFree, pErrInfo);
                    if (RT_SUCCESS(rc))
                    {

                        const unsigned char *puchPubKey = pbRaw;
                        EVP_PKEY *pRet = d2i_PUBKEY(ppEvpNewKey, &puchPubKey, cbRaw);
                        if (pRet != NULL && pRet == *ppEvpNewKey)
                            rc = VINF_SUCCESS;
                        else
                            rc = RTERRINFO_LOG_SET(pErrInfo, VERR_CR_PKIX_OSSL_D2I_KEY_PARAMS_FAILED, "d2i_KeyParams failed");
                        RTMemTmpFree(pvFree);
                    }
                }
            }
            AssertRC(rc);
            RTCrX509SubjectPublicKeyInfo_Delete(&PubKeyInfo);
            return rc;
        }
        rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_OPENSSL_VERSION_TOO_OLD,
                                 "OpenSSL version %#x is too old for IPRTs ECDSA code", OPENSSL_VERSION_NUMBER);
        RT_NOREF(idKeyType, ppEvpNewKey);
#endif
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Load the key into the structure.
         */
        const unsigned char *puchPublicKey = hKey->pbEncoded;
        EVP_PKEY *pRet;
        if (fNeedPublic)
            pRet = d2i_PublicKey(idKeyType, ppEvpNewKey, &puchPublicKey, hKey->cbEncoded);
        else
            pRet = d2i_PrivateKey(idKeyType, ppEvpNewKey, &puchPublicKey, hKey->cbEncoded);
        if (pRet != NULL && pRet == *ppEvpNewKey)
            return VINF_SUCCESS;

        /* Bail out: */
        if (fNeedPublic)
            rc = RTERRINFO_LOG_SET(pErrInfo, VERR_CR_PKIX_OSSL_D2I_PUBLIC_KEY_FAILED, "d2i_PublicKey failed");
        else
            rc = RTERRINFO_LOG_SET(pErrInfo, VERR_CR_PKIX_OSSL_D2I_PRIVATE_KEY_FAILED, "d2i_PrivateKey failed");
    }
    return rc;
}


/**
 * Creates an OpenSSL key for the given IPRT one, returning the message digest
 * algorithm if desired.
 *
 * @returns IRPT status code.
 * @param   hKey            The key to convert to an OpenSSL key.
 * @param   fNeedPublic     Set if we need the public side of the key.
 * @param   pszAlgoObjId    Alogrithm stuff we currently need.
 * @param   ppEvpKey        Where to return the pointer to the key structure.
 * @param   ppEvpMdType     Where to optionally return the message digest type.
 * @param   pErrInfo        Where to optionally return more error details.
 */
DECLHIDDEN(int) rtCrKeyToOpenSslKey(RTCRKEY hKey, bool fNeedPublic, void /*EVP_PKEY*/ **ppEvpKey, PRTERRINFO pErrInfo)
{
    *ppEvpKey = NULL;
    AssertReturn(hKey->u32Magic == RTCRKEYINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fNeedPublic == !(hKey->fFlags & RTCRKEYINT_F_PRIVATE), VERR_WRONG_TYPE);
    AssertReturn(hKey->fFlags & RTCRKEYINT_F_INCLUDE_ENCODED, VERR_WRONG_TYPE); /* build misconfig */

    rtCrOpenSslInit();

    /*
     * Translate the key type from IPRT to EVP speak.
     */
    int         idKeyType;
    switch (hKey->enmType)
    {
        case RTCRKEYTYPE_RSA_PRIVATE:
        case RTCRKEYTYPE_RSA_PUBLIC:
            idKeyType = EVP_PKEY_RSA;
            break;

        case RTCRKEYTYPE_ECDSA_PUBLIC:
        case RTCRKEYTYPE_ECDSA_PRIVATE:
            idKeyType = EVP_PKEY_EC;
            break;

        default:
            return RTErrInfoSetF(pErrInfo, VERR_NOT_SUPPORTED, "Unsupported key type: %d", hKey->enmType);
    }

    /*
     * Allocate a new key structure and set its type.
     */
    EVP_PKEY *pEvpNewKey = EVP_PKEY_new();
    if (!pEvpNewKey)
        return RTErrInfoSetF(pErrInfo, VERR_NO_MEMORY, "EVP_PKEY_new/%d failed", idKeyType);

    /*
     * Load key parameters and the key into the EVP structure.
     */
    int rc = rtCrKeyToOpenSslKeyLoad(hKey, idKeyType, &pEvpNewKey, fNeedPublic, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        *ppEvpKey = pEvpNewKey;
        return rc;
    }
    EVP_PKEY_free(pEvpNewKey);
    return rc;
}


/**
 * Creates an OpenSSL key for the given IPRT one, returning the message digest
 * algorithm if desired.
 *
 * @returns IRPT status code.
 * @param   hKey            The key to convert to an OpenSSL key.
 * @param   fNeedPublic     Set if we need the public side of the key.
 * @param   pszAlgoObjId    Alogrithm stuff we currently need.
 * @param   ppEvpKey        Where to return the pointer to the key structure.
 * @param   ppEvpMdType     Where to optionally return the message digest type.
 * @param   pErrInfo        Where to optionally return more error details.
 */
DECLHIDDEN(int) rtCrKeyToOpenSslKeyEx(RTCRKEY hKey, bool fNeedPublic, const char *pszAlgoObjId,
                                      void /*EVP_PKEY*/ **ppEvpKey, const void /*EVP_MD*/ **ppEvpMdType, PRTERRINFO pErrInfo)
{
    *ppEvpKey = NULL;
    if (ppEvpMdType)
        *ppEvpMdType = NULL;
    AssertReturn(hKey->u32Magic == RTCRKEYINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(fNeedPublic == !(hKey->fFlags & RTCRKEYINT_F_PRIVATE), VERR_WRONG_TYPE);
    AssertReturn(hKey->fFlags & RTCRKEYINT_F_INCLUDE_ENCODED, VERR_WRONG_TYPE); /* build misconfig */

    rtCrOpenSslInit();

    /*
     * Translate algorithm object ID into stuff that OpenSSL wants.
     */
    int iAlgoNid = OBJ_txt2nid(pszAlgoObjId);
    if (iAlgoNid == NID_undef)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN,
                                   "Unknown public key algorithm [OpenSSL]: %s", pszAlgoObjId);
    const char *pszAlgoSn = OBJ_nid2sn(iAlgoNid);

# if OPENSSL_VERSION_NUMBER >= 0x10001000 && !defined(LIBRESSL_VERSION_NUMBER)
    int idAlgoPkey = 0;
    int idAlgoMd = 0;
    if (!OBJ_find_sigid_algs(iAlgoNid, &idAlgoMd, &idAlgoPkey))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP,
                                   "OBJ_find_sigid_algs failed on %u (%s, %s)", iAlgoNid, pszAlgoSn, pszAlgoObjId);
    if (ppEvpMdType)
    {
        const EVP_MD *pEvpMdType = EVP_get_digestbynid(idAlgoMd);
        if (!pEvpMdType)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP,
                                       "EVP_get_digestbynid failed on %d (%s, %s)", idAlgoMd, pszAlgoSn, pszAlgoObjId);
        *ppEvpMdType = pEvpMdType;
    }
# else
    const EVP_MD *pEvpMdType = EVP_get_digestbyname(pszAlgoSn);
    if (!pEvpMdType)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_CIPHER_ALGO_NOT_KNOWN_EVP,
                                   "EVP_get_digestbyname failed on %s (%s)", pszAlgoSn, pszAlgoObjId);
    if (ppEvpMdType)
        *ppEvpMdType = pEvpMdType;
# endif

    /*
     * Allocate a new key structure and set its type.
     */
    EVP_PKEY *pEvpNewKey = EVP_PKEY_new();
    if (!pEvpNewKey)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_NO_MEMORY, "EVP_PKEY_new(%d) failed", iAlgoNid);

    int rc;
# if OPENSSL_VERSION_NUMBER >= 0x10001000 && !defined(LIBRESSL_VERSION_NUMBER)
    if (EVP_PKEY_set_type(pEvpNewKey, idAlgoPkey))
    {
        int idKeyType = EVP_PKEY_base_id(pEvpNewKey);
# else
        int idKeyType = pEvpNewKey->type = EVP_PKEY_type(pEvpMdType->required_pkey_type[0]);
# endif
        if (idKeyType != NID_undef)

        {
            /*
             * Load key parameters and the key into the EVP structure.
             */
            rc = rtCrKeyToOpenSslKeyLoad(hKey, idKeyType, &pEvpNewKey, fNeedPublic, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                *ppEvpKey = pEvpNewKey;
                return rc;
            }
        }
        else
# if OPENSSL_VERSION_NUMBER < 0x10001000 || defined(LIBRESSL_VERSION_NUMBER)
            rc = RTERRINFO_LOG_SET(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR, "EVP_PKEY_type() failed");
# else
            rc = RTERRINFO_LOG_SET(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR, "EVP_PKEY_base_id() failed");
    }
    else
        rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_PKIX_OSSL_EVP_PKEY_TYPE_ERROR,
                                 "EVP_PKEY_set_type(%u) failed (sig algo %s)", idAlgoPkey, pszAlgoSn);
# endif

    EVP_PKEY_free(pEvpNewKey);
    *ppEvpKey = NULL;
    return rc;
}

#endif /* IPRT_WITH_OPENSSL */

