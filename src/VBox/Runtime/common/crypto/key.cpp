/* $Id: key.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Cryptographic Keys.
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

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/memsafer.h>
#include <iprt/string.h>
#include <iprt/crypto/rsa.h>
#include <iprt/crypto/pkix.h>

#include "internal/magics.h"
#include "key-internal.h"


/**
 * Internal crypto key instance creator.
 *
 * This does most of the common work, caller does the 'u' and cBits jobs.
 *
 * @returns IPRT status code.
 * @param   ppThis              Where to return the key instance.
 * @param   enmType             The key type.
 * @param   fFlags              The key flags.
 * @param   pvEncoded           The encoded key bits.
 * @param   cbEncoded           The size of the encoded key bits (in bytes).
 */
DECLHIDDEN(int) rtCrKeyCreateWorker(PRTCRKEYINT *ppThis, RTCRKEYTYPE enmType, uint32_t fFlags,
                                    void const *pvEncoded, uint32_t cbEncoded)
{
    PRTCRKEYINT pThis = (PRTCRKEYINT)RTMemAllocZ(sizeof(*pThis) + (fFlags & RTCRKEYINT_F_SENSITIVE ? 0 : cbEncoded));
    if (pThis)
    {
        pThis->enmType      = enmType;
        pThis->fFlags       = fFlags;
#if defined(IPRT_WITH_OPENSSL)
        pThis->fFlags      |= RTCRKEYINT_F_INCLUDE_ENCODED;
        pThis->cbEncoded    = cbEncoded;
        if (!(fFlags & RTCRKEYINT_F_SENSITIVE))
            pThis->pbEncoded = (uint8_t *)(pThis + 1);
        else
        {
            pThis->pbEncoded = (uint8_t *)RTMemSaferAllocZ(cbEncoded);
            if (!pThis->pbEncoded)
            {
                RTMemFree(pThis);
                return VERR_NO_MEMORY;
            }
        }
        memcpy(pThis->pbEncoded, pvEncoded, cbEncoded);
#else
        RT_NOREF(pvEncoded, cbEncoded);
#endif
        pThis->cRefs    = 1;
        pThis->u32Magic = RTCRKEYINT_MAGIC;
        *ppThis = pThis;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Creates an RSA public key from a DER encoded RTCRRSAPUBLICKEY blob.
 *
 * @returns IPRT status code.
 * @param   phKey       Where to return the key handle.
 * @param   pvKeyBits   The DER encoded RTCRRSAPUBLICKEY blob.
 * @param   cbKeyBits   The size of the blob.
 * @param   pErrInfo    Where to supply addition error details.  Optional.
 * @param   pszErrorTag Error tag. Optional.
 */
DECLHIDDEN(int) rtCrKeyCreateRsaPublic(PRTCRKEY phKey, const void *pvKeyBits, uint32_t cbKeyBits,
                                       PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    /*
     * Decode the key data first since that's what's most likely to fail here.
     */
    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pvKeyBits, cbKeyBits, pErrInfo, &g_RTAsn1DefaultAllocator,
                            RTASN1CURSOR_FLAGS_DER, pszErrorTag ? pszErrorTag : "rsa");
    RTCRRSAPUBLICKEY PublicKey;
    RT_ZERO(PublicKey);
    int rc = RTCrRsaPublicKey_DecodeAsn1(&PrimaryCursor.Cursor, 0, &PublicKey, pszErrorTag ? pszErrorTag : "PublicKey");
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a key instance for it.
         */
        PRTCRKEYINT pThis;
        rc = rtCrKeyCreateWorker(&pThis, RTCRKEYTYPE_RSA_PUBLIC, RTCRKEYINT_F_PUBLIC, pvKeyBits, cbKeyBits);
        if (RT_SUCCESS(rc))
        {
            rc = RTAsn1Integer_ToBigNum(&PublicKey.Modulus, &pThis->u.RsaPublic.Modulus, 0);
            if (RT_SUCCESS(rc))
            {
                pThis->cBits = RTBigNumBitWidth(&pThis->u.RsaPublic.Modulus);
                rc = RTAsn1Integer_ToBigNum(&PublicKey.PublicExponent, &pThis->u.RsaPublic.Exponent, 0);
                if (RT_SUCCESS(rc))
                {

                    /* Done. */
                    RTAsn1VtDelete(&PublicKey.SeqCore.Asn1Core);
                    *phKey = pThis;
                    return VINF_SUCCESS;
                }
            }
            RTCrKeyRelease(pThis);
        }
        RTAsn1VtDelete(&PublicKey.SeqCore.Asn1Core);
    }
    *phKey = NIL_RTCRKEY;
    return rc;
}


/**
 * Creates an EC (ECDSA) public key from a DER encoded RTCRX50-PUBLICKEY blob.
 *
 * @returns IPRT status code.
 * @param   phKey       Where to return the key handle.
 * @param   pParameters The algorithm parameters, typically namedCurve OID.
 * @param   pvKeyBits   The DER encoded RTCRRSAPUBLICKEY blob.
 * @param   cbKeyBits   The size of the blob.
 * @param   pErrInfo    Where to supply addition error details.  Optional.
 * @param   pszErrorTag Error tag. Optional.
 */
DECLHIDDEN(int) rtCrKeyCreateEcdsaPublic(PRTCRKEY phKey, PCRTASN1DYNTYPE pParameters, const void *pvKeyBits, uint32_t cbKeyBits,
                                         PRTERRINFO pErrInfo, const char *pszErrorTag)
{
#if 0
    /*
     * Decode the key data first since that's what's most likely to fail here.
     */
    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pvKeyBits, cbKeyBits, pErrInfo, &g_RTAsn1DefaultAllocator,
                            RTASN1CURSOR_FLAGS_DER, pszErrorTag ? pszErrorTag : "rsa");
    RTCRRSAPUBLICKEY PublicKey;
    RT_ZERO(PublicKey);
    int rc = RTCrRsaPublicKey_DecodeAsn1(&PrimaryCursor.Cursor, 0, &PublicKey, pszErrorTag ? pszErrorTag : "PublicKey");
#else
    RT_NOREF(pszErrorTag);
    int rc = VINF_SUCCESS;
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Check the parameter (see RTC-5480, section 2.1.1).
         */
        if (   pParameters
            && pParameters->enmType == RTASN1TYPE_OBJID
            && RTAsn1ObjId_IsPresent(&pParameters->u.ObjId) /* paranoia */)
        {
            /*
             * Create a key instance for it.
             */
            PRTCRKEYINT pThis;
            rc = rtCrKeyCreateWorker(&pThis, RTCRKEYTYPE_ECDSA_PUBLIC, RTCRKEYINT_F_PUBLIC, pvKeyBits, cbKeyBits);
            if (RT_SUCCESS(rc))
            {
                rc = RTAsn1ObjId_Clone(&pThis->u.EcdsaPublic.NamedCurve, &pParameters->u.ObjId, &g_RTAsn1DefaultAllocator);
                if (RT_SUCCESS(rc))
                {
                    /* Done. */
#if 0
                    RTAsn1VtDelete(&PublicKey.SeqCore.Asn1Core);
#endif
                    *phKey = pThis;
                    return VINF_SUCCESS;
                }
                RTCrKeyRelease(pThis);
            }
        }
        else if (!pParameters || pParameters->enmType == RTASN1TYPE_NOT_PRESENT)
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_MISSING,
                                     "%s: ECDSA public key expected a namedCurve parameter", pszErrorTag);
        else if (pParameters->enmType == RTASN1TYPE_NULL)
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_UNKNOWN,
                                     "%s: ECDSA public key expected a namedCurve parameter, found implicitCurve (NULL) instead",
                                     pszErrorTag);
        else
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_UNKNOWN,
                                     "%s: ECDSA public key expected namedCurve parameter, found %d",
                                     pszErrorTag, pParameters->enmType);
#if 0
        RTAsn1VtDelete(&PublicKey.SeqCore.Asn1Core);
#endif
    }
    *phKey = NIL_RTCRKEY;
    return rc;
}


RTDECL(int) RTCrKeyCreateFromPublicAlgorithmAndBits(PRTCRKEY phKey, PCRTASN1OBJID pAlgorithm, PCRTASN1DYNTYPE pParameters,
                                                    PCRTASN1BITSTRING pPublicKey, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phKey, VERR_INVALID_POINTER);
    *phKey = NIL_RTCRKEY;

    AssertPtrReturn(pAlgorithm, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1ObjId_IsPresent(pAlgorithm), VERR_INVALID_PARAMETER);

    AssertPtrReturn(pPublicKey, VERR_INVALID_POINTER);
    AssertReturn(RTAsn1BitString_IsPresent(pPublicKey), VERR_INVALID_PARAMETER);

    /*
     * Identify the key type from the algorithm ID
     */
    const char * const pszEncryptionOid = RTCrX509AlgorithmIdentifier_GetEncryptionOidFromOid(pAlgorithm->szObjId,
                                                                                              false /*fMustIncludeHash*/);
    if (pszEncryptionOid)
    {
        if (strcmp(pszEncryptionOid, RTCRX509ALGORITHMIDENTIFIERID_RSA) == 0)
            return rtCrKeyCreateRsaPublic(phKey,
                                          RTASN1BITSTRING_GET_BIT0_PTR(pPublicKey),
                                          RTASN1BITSTRING_GET_BYTE_SIZE(pPublicKey),
                                          pErrInfo, pszErrorTag);
        if (strcmp(pszEncryptionOid, RTCRX509ALGORITHMIDENTIFIERID_ECDSA) == 0)
            return rtCrKeyCreateEcdsaPublic(phKey,
                                            pParameters,
                                            RTASN1BITSTRING_GET_BIT0_PTR(pPublicKey),
                                            RTASN1BITSTRING_GET_BYTE_SIZE(pPublicKey),
                                            pErrInfo, pszErrorTag);
    }
    Assert(pszEncryptionOid == NULL);
    return RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_CIPHER_ALGO_NOT_KNOWN, "oid=%s", pAlgorithm->szObjId);
}


RTDECL(int) RTCrKeyCreateFromSubjectPublicKeyInfo(PRTCRKEY phKey, struct RTCRX509SUBJECTPUBLICKEYINFO const *pSrc,
                                                  PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    AssertPtrReturn(pSrc, VERR_INVALID_POINTER);
    AssertReturn(RTCrX509SubjectPublicKeyInfo_IsPresent(pSrc), VERR_INVALID_PARAMETER);
    return RTCrKeyCreateFromPublicAlgorithmAndBits(phKey, &pSrc->Algorithm.Algorithm, &pSrc->Algorithm.Parameters,
                                                   &pSrc->SubjectPublicKey, pErrInfo, pszErrorTag);
}


/**
 * Creates an RSA private key from a DER encoded RTCRRSAPRIVATEKEY blob.
 *
 * @returns IPRT status code.
 * @param   phKey       Where to return the key handle.
 * @param   pvKeyBits   The DER encoded RTCRRSAPRIVATEKEY blob.
 * @param   cbKeyBits   The size of the blob.
 * @param   pErrInfo    Where to supply addition error details.  Optional.
 * @param   pszErrorTag Error tag. Optional.
 */
DECLHIDDEN(int) rtCrKeyCreateRsaPrivate(PRTCRKEY phKey, const void *pvKeyBits, uint32_t cbKeyBits,
                                        PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    /*
     * Decode the key data first since that's what's most likely to fail here.
     */
    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pvKeyBits, cbKeyBits, pErrInfo, &g_RTAsn1SaferAllocator,
                            RTASN1CURSOR_FLAGS_DER, pszErrorTag ? pszErrorTag : "rsa");
    RTCRRSAPRIVATEKEY PrivateKey;
    RT_ZERO(PrivateKey);
    int rc = RTCrRsaPrivateKey_DecodeAsn1(&PrimaryCursor.Cursor, 0, &PrivateKey, pszErrorTag ? pszErrorTag : "PrivateKey");
    if (RT_SUCCESS(rc))
    {
       /*
        * Create a key instance for it.
        */
        PRTCRKEYINT pThis;
        rc = rtCrKeyCreateWorker(&pThis, RTCRKEYTYPE_RSA_PRIVATE, RTCRKEYINT_F_PRIVATE | RTCRKEYINT_F_SENSITIVE,
                                 pvKeyBits, cbKeyBits);
        if (RT_SUCCESS(rc))
        {
            rc = RTAsn1Integer_ToBigNum(&PrivateKey.Modulus, &pThis->u.RsaPrivate.Modulus, 0);
            if (RT_SUCCESS(rc))
            {
                pThis->cBits = RTBigNumBitWidth(&pThis->u.RsaPrivate.Modulus);
                rc = RTAsn1Integer_ToBigNum(&PrivateKey.PrivateExponent, &pThis->u.RsaPrivate.PrivateExponent, 0);
                if (RT_SUCCESS(rc))
                {
                    rc = RTAsn1Integer_ToBigNum(&PrivateKey.PublicExponent, &pThis->u.RsaPrivate.PublicExponent, 0);
                    if (RT_SUCCESS(rc))
                    {
                        /* Done. */
                        RTAsn1VtDelete(&PrivateKey.SeqCore.Asn1Core);
                        RTMemWipeThoroughly(&PrivateKey, sizeof(PrivateKey), 3);
                        *phKey = pThis;
                        return VINF_SUCCESS;
                    }
                }
            }
            RTCrKeyRelease(pThis);
        }
        RTAsn1VtDelete(&PrivateKey.SeqCore.Asn1Core);
        RTMemWipeThoroughly(&PrivateKey, sizeof(PrivateKey), 3);
    }
    *phKey = NIL_RTCRKEY;
    return rc;
}


RTDECL(uint32_t) RTCrKeyRetain(RTCRKEY hKey)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < 1024, ("%#x\n", cRefs));
    return cRefs;
}


/**
 * Destructor.
 *
 * @returns 0
 * @param   pThis           The key to destroy.
 */
static int rtCrKeyDestroy(PRTCRKEYINT pThis)
{
    /* Invalidate the object. */
    pThis->u32Magic = ~RTCRKEYINT_MAGIC;

    /* Type specific cleanup. */
    switch (pThis->enmType)
    {
        case RTCRKEYTYPE_RSA_PUBLIC:
            RTBigNumDestroy(&pThis->u.RsaPublic.Modulus);
            RTBigNumDestroy(&pThis->u.RsaPublic.Exponent);
            break;

        case RTCRKEYTYPE_RSA_PRIVATE:
            RTBigNumDestroy(&pThis->u.RsaPrivate.Modulus);
            RTBigNumDestroy(&pThis->u.RsaPrivate.PrivateExponent);
            RTBigNumDestroy(&pThis->u.RsaPrivate.PublicExponent);
            break;

        case RTCRKEYTYPE_ECDSA_PUBLIC:
            RTAsn1ObjId_Delete(&pThis->u.EcdsaPublic.NamedCurve);
            break;

        case RTCRKEYTYPE_ECDSA_PRIVATE: /* not yet implemented */
        case RTCRKEYTYPE_INVALID:
        case RTCRKEYTYPE_END:
        case RTCRKEYTYPE_32BIT_HACK:
            AssertFailed();
    }
    pThis->enmType = RTCRKEYTYPE_INVALID;

#if defined(IPRT_WITH_OPENSSL)
    /* Free the encoded form if sensitive (otherwise it follows pThis). */
    if (pThis->pbEncoded)
    {
        if (pThis->fFlags & RTCRKEYINT_F_SENSITIVE)
            RTMemSaferFree((uint8_t *)pThis->pbEncoded, pThis->cbEncoded);
        else
            Assert(pThis->pbEncoded == (uint8_t *)(pThis + 1));
        pThis->pbEncoded = NULL;
    }
#endif

    /* Finally, free the key object itself. */
    RTMemFree(pThis);
    return 0;
}


RTDECL(uint32_t) RTCrKeyRelease(RTCRKEY hKey)
{
    if (hKey == NIL_RTCRKEY)
        return 0;
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < 1024, ("%#x\n", cRefs));
    if (cRefs != 0)
        return cRefs;
    return rtCrKeyDestroy(pThis);
}


RTDECL(RTCRKEYTYPE) RTCrKeyGetType(RTCRKEY hKey)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, RTCRKEYTYPE_INVALID);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, RTCRKEYTYPE_INVALID);
    return pThis->enmType;
}


RTDECL(bool) RTCrKeyHasPrivatePart(RTCRKEY hKey)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, false);
    return RT_BOOL(pThis->fFlags & RTCRKEYINT_F_PRIVATE);
}


RTDECL(bool) RTCrKeyHasPublicPart(RTCRKEY hKey)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, false);
    return RT_BOOL(pThis->fFlags & RTCRKEYINT_F_PUBLIC);
}


RTDECL(uint32_t) RTCrKeyGetBitCount(RTCRKEY hKey)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, 0);
    return pThis->cBits;
}


RTDECL(int) RTCrKeyQueryRsaModulus(RTCRKEY hKey, PRTBIGNUM pModulus)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->enmType == RTCRKEYTYPE_RSA_PRIVATE || pThis->enmType == RTCRKEYTYPE_RSA_PUBLIC, VERR_WRONG_TYPE);
    AssertPtrReturn(pModulus, VERR_INVALID_POINTER);

    if (pThis->enmType == RTCRKEYTYPE_RSA_PRIVATE)
        return RTBigNumAssign(pModulus, &pThis->u.RsaPrivate.Modulus);
    return RTBigNumAssign(pModulus, &pThis->u.RsaPublic.Modulus);
}


RTDECL(int) RTCrKeyQueryRsaPrivateExponent(RTCRKEY hKey, PRTBIGNUM pPrivateExponent)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->enmType == RTCRKEYTYPE_RSA_PRIVATE, VERR_WRONG_TYPE);
    AssertPtrReturn(pPrivateExponent, VERR_INVALID_POINTER);

    return RTBigNumAssign(pPrivateExponent, &pThis->u.RsaPrivate.PrivateExponent);
}


RTDECL(int) RTCrKeyVerifyParameterCompatibility(RTCRKEY hKey, PCRTASN1DYNTYPE pParameters, bool fForSignature,
                                                PCRTASN1OBJID pAlgorithm, PRTERRINFO pErrInfo)
{
    PRTCRKEYINT pThis = hKey;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRKEYINT_MAGIC, VERR_INVALID_HANDLE);
    RT_NOREF(pAlgorithm);

    switch (pThis->enmType)
    {
        /*
         * No parameters. Ignore NULL.
         */
        case RTCRKEYTYPE_RSA_PRIVATE:
        case RTCRKEYTYPE_RSA_PUBLIC:
            if (   !pParameters
                || pParameters->enmType == RTASN1TYPE_NOT_PRESENT
                || pParameters->enmType == RTASN1TYPE_NULL)
                return VINF_SUCCESS;
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_UNEXPECTED,
                                       "RSA keys does not generally take parameters (enmType=%d)", pParameters->enmType);

        /*
         * ECDSA requires a parameter.  Currently only the named curve choice is supported.
         * RFC-3279 section 2.2.3 states that for ecdsa-with-SHA1 the parameter MUST be
         * omitted.  ASSUMING the same applies to the other ecdsa-with-xxxx variants.
         */
        case RTCRKEYTYPE_ECDSA_PUBLIC:
            if (!fForSignature)
            {
                /* Key rules: Parameters required. */
                if (pParameters)
                {
                    if (pParameters->enmType == RTASN1TYPE_OBJID)
                    {
                        if (RTAsn1ObjId_Compare(&pParameters->u.ObjId, &pThis->u.EcdsaPublic.NamedCurve) == 0)
                            return VINF_SUCCESS;
                        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_MISMATCH,
                                                   "ECDSA NamedCurve difference: %s, key uses %s",
                                                   pParameters->u.ObjId.szObjId, pThis->u.EcdsaPublic.NamedCurve.szObjId);
                    }
                    /* We don't implement the other variants. */
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_UNKNOWN,
                                               "Unexpected ECDSA parameter: enmType=%d", pParameters->enmType);
                }
                return RTERRINFO_LOG_SET(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_MISSING, "ECDSA keys requires parameter(s)");
            }

            /* Hash+ecdsa parameter rules: No parameters */
            if (   !pParameters
                || pParameters->enmType == RTASN1TYPE_NOT_PRESENT
                || pParameters->enmType == RTASN1TYPE_NULL)
                return VINF_SUCCESS;
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_CR_KEY_ALGO_PARAMS_UNEXPECTED,
                                       "ECDSA signature should have no parameters (enmType=%d)", pParameters->enmType);

        case RTCRKEYTYPE_ECDSA_PRIVATE:
            AssertFailedReturn(VERR_NOT_IMPLEMENTED);

        case RTCRKEYTYPE_INVALID:
        case RTCRKEYTYPE_END:
        case RTCRKEYTYPE_32BIT_HACK:
            break;
    }
    AssertFailedReturn(VERR_INTERNAL_ERROR_5);
}

