/* $Id: pkix-signature-rsa.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Public Key Signature Schema Algorithm, RSA Providers.
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
#include <iprt/crypto/rsa.h>

#include <iprt/bignum.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/pkix.h>

#include "rsa-internal.h"
#include "pkix-signature-builtin.h"
#include "key-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * RSA signature provider instance.
 */
typedef struct RTCRPKIXSIGNATURERSA
{
    /** Set if we're signing, clear if verifying.  */
    bool                    fSigning;

    /** Temporary big number for use when signing or verifiying. */
    RTBIGNUM                TmpBigNum1;
    /** Temporary big number for use when signing or verifiying. */
    RTBIGNUM                TmpBigNum2;

    /** Scratch space for decoding the key. */
    union
    {
        /** Public key. */
        RTCRRSAPUBLICKEY    PublicKey;
        /** Private key. */
        RTCRRSAPRIVATEKEY   PrivateKey;
        /** Scratch area where we assemble the signature. */
        uint8_t             abSignature[RTCRRSA_MAX_MODULUS_BITS / 8 * 2];
    } Scratch;
} RTCRPKIXSIGNATURERSA;
/** Pointer to an RSA signature provider instance. */
typedef RTCRPKIXSIGNATURERSA *PRTCRPKIXSIGNATURERSA;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name Pre-encoded DigestInfo DER sequences.
 * @{ */
static const uint8_t g_abMd2[] =
{/* {          {          1.2.840.113549.2.2 (MD2),                                 NULL },     hash octet-string } */
    0x30,0x20, 0x30,0x0c, 0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x02,0x02,        0x05,0x00,  0x04,0x10
};
static const uint8_t g_abMd4[] =
{/* {          {          1.2.840.113549.2.4 (MD4),                                 NULL },     hash octet-string } */
    0x30,0x20, 0x30,0x0c, 0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x02,0x04,        0x05,0x00,  0x04,0x10
};
static const uint8_t g_abMd5[] =
{/* {          {          1.2.840.113549.2.5 (MD5),                                 NULL },     hash octet-string } */
    0x30,0x20, 0x30,0x0c, 0x06,0x08,0x2a,0x86,0x48,0x86,0xf7,0x0d,0x02,0x05,        0x05,0x00,  0x04,0x10
};
static const uint8_t g_abSha1[] =
{/* {          {          1.3.14.3.2.26 (SHA-1),                                    NULL },     hash octet-string } */
    0x30,0x21, 0x30,0x09, 0x06,0x05,0x2b,0x0e,0x03,0x02,0x1a,                       0x05,0x00,  0x04,0x14
};
static const uint8_t g_abSha256[] =
{/* {          {          2.16.840.1.101.3.4.2.1 (SHA-256),                         NULL },     hash octet-string } */
    0x30,0x31, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,   0x05,0x00,  0x04,0x20
};
static const uint8_t g_abSha384[] =
{/* {          {          2.16.840.1.101.3.4.2.2 (SHA-384),                         NULL },     hash octet-string } */
    0x30,0x41, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,   0x05,0x00,  0x04,0x30
};
static const uint8_t g_abSha512[] =
{/* {          {          2.16.840.1.101.3.4.2.3 (SHA-512),                         NULL },     hash octet-string } */
    0x30,0x51, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,   0x05,0x00,  0x04,0x40
};
static const uint8_t g_abSha224[] =
{/* {          {          2.16.840.1.101.3.4.2.4 (SHA-224),                         NULL },     hash octet-string } */
    0x30,0x2d, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x04,   0x05,0x00,  0x04,0x1c
};
static const uint8_t g_abSha512t224[] =
{/* {          {          2.16.840.1.101.3.4.2.5 (SHA-512T224),                     NULL },     hash octet-string } */
    0x30,0x2d, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x05,   0x05,0x00,  0x04,0x1c
};
static const uint8_t g_abSha512t256[] =
{/* {          {          2.16.840.1.101.3.4.2.6 (SHA-512T256),                     NULL },     hash octet-string } */
    0x30,0x31, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x06,   0x05,0x00,  0x04,0x20
};
static const uint8_t g_abSha3t224[] =
{/* {          {          2.16.840.1.101.3.4.2.7 (SHA3-224),                        NULL },     hash octet-string } */
    0x30,0x2d, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x07,   0x05,0x00,  0x04,0x1c
};
static const uint8_t g_abSha3t256[] =
{/* {          {          2.16.840.1.101.3.4.2.8 (SHA3-256),                        NULL },     hash octet-string } */
    0x30,0x31, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x08,   0x05,0x00,  0x04,0x20
};
static const uint8_t g_abSha3t384[] =
{/* {          {          2.16.840.1.101.3.4.2.9 (SHA3-384),                        NULL },     hash octet-string } */
    0x30,0x41, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x09,   0x05,0x00,  0x04,0x30
};
static const uint8_t g_abSha3t512[] =
{/* {          {          2.16.840.1.101.3.4.2.10 (SHA3-512),                       NULL },     hash octet-string } */
    0x30,0x51, 0x30,0x0d, 0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x0a,   0x05,0x00,  0x04,0x40
};
/** @} */

/** Lookup array for the pre-encoded DigestInfo DER sequences. */
static struct
{
    RTDIGESTTYPE    enmDigest;
    const uint8_t  *pb;
    size_t          cb;
} const g_aDigestInfos[] =
{
    { RTDIGESTTYPE_SHA1,        g_abSha1,       sizeof(g_abSha1) },
    { RTDIGESTTYPE_SHA256,      g_abSha256,     sizeof(g_abSha256) },
    { RTDIGESTTYPE_SHA512,      g_abSha512,     sizeof(g_abSha512) },
    { RTDIGESTTYPE_MD2,         g_abMd2,        sizeof(g_abMd2) },
    { RTDIGESTTYPE_MD4,         g_abMd4,        sizeof(g_abMd4) },
    { RTDIGESTTYPE_MD5,         g_abMd5,        sizeof(g_abMd5) },
    { RTDIGESTTYPE_SHA384,      g_abSha384,     sizeof(g_abSha384) },
    { RTDIGESTTYPE_SHA224,      g_abSha224,     sizeof(g_abSha224) },
    { RTDIGESTTYPE_SHA512T224,  g_abSha512t224, sizeof(g_abSha512t224)},
    { RTDIGESTTYPE_SHA512T256,  g_abSha512t256, sizeof(g_abSha512t256)},
    { RTDIGESTTYPE_SHA3_224,    g_abSha3t224,   sizeof(g_abSha3t224) },
    { RTDIGESTTYPE_SHA3_256,    g_abSha3t256,   sizeof(g_abSha3t256) },
    { RTDIGESTTYPE_SHA3_384,    g_abSha3t384,   sizeof(g_abSha3t384) },
    { RTDIGESTTYPE_SHA3_512,    g_abSha3t512,   sizeof(g_abSha3t512) },
};


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnInit}  */
static DECLCALLBACK(int) rtCrPkixSignatureRsa_Init(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, void *pvOpaque,
                                                   bool fSigning, RTCRKEY hKey, PCRTASN1DYNTYPE pParams)
{
    RT_NOREF_PV(pDesc); RT_NOREF_PV(pvState); RT_NOREF_PV(pvOpaque);

    if (   !pParams
        || pParams->enmType == RTASN1TYPE_NULL
        || pParams->enmType == RTASN1TYPE_NOT_PRESENT)
    { /* likely */ }
    else
        return VERR_CR_PKIX_SIGNATURE_TAKES_NO_PARAMETERS;

    RTCRKEYTYPE enmKeyType = RTCrKeyGetType(hKey);
    if (fSigning)
        AssertReturn(enmKeyType == RTCRKEYTYPE_RSA_PRIVATE, VERR_CR_PKIX_NOT_RSA_PRIVATE_KEY);
    else
        AssertReturn(enmKeyType == RTCRKEYTYPE_RSA_PUBLIC, VERR_CR_PKIX_NOT_RSA_PUBLIC_KEY);

    PRTCRPKIXSIGNATURERSA pThis = (PRTCRPKIXSIGNATURERSA)pvState;
    pThis->fSigning = fSigning;

    return VINF_SUCCESS;
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnReset}  */
static DECLCALLBACK(int) rtCrPkixSignatureRsa_Reset(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, bool fSigning)
{
    PRTCRPKIXSIGNATURERSA pThis = (PRTCRPKIXSIGNATURERSA)pvState;
    RT_NOREF_PV(fSigning); RT_NOREF_PV(pDesc);
    Assert(pThis->fSigning == fSigning); NOREF(pThis);
    return VINF_SUCCESS;
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnDelete}  */
static DECLCALLBACK(void) rtCrPkixSignatureRsa_Delete(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, bool fSigning)
{
    PRTCRPKIXSIGNATURERSA pThis = (PRTCRPKIXSIGNATURERSA)pvState;
    RT_NOREF_PV(fSigning); RT_NOREF_PV(pDesc);
    Assert(pThis->fSigning == fSigning);
    NOREF(pThis);
}


/**
 * Common worker for rtCrPkixSignatureRsa_Verify and
 * rtCrPkixSignatureRsa_Sign that encodes an EMSA-PKCS1-V1_5 signature in
 * the scratch area.
 *
 * This function is referred to as EMSA-PKCS1-v1_5-ENCODE(M,k) in RFC-3447 and
 * is described in section 9.2
 *
 * @returns IPRT status code.
 * @param   pThis           The RSA signature provider instance.
 * @param   hDigest         The digest which hash to turn into a signature.
 * @param   cbEncodedMsg    The desired encoded message length.
 * @param   fNoDigestInfo   If true, skip the DigestInfo and encode the digest
 *                          without any prefix like described in v1.5 (RFC-2313)
 *                          and observed with RSA+MD5 signed timestamps.  If
 *                          false, include the prefix like v2.0 (RFC-2437)
 *                          describes in step in section 9.2.1
 *                          (EMSA-PKCS1-v1_5)
 *
 * @remarks Must preserve informational status codes!
 */
static int rtCrPkixSignatureRsa_EmsaPkcs1V15Encode(PRTCRPKIXSIGNATURERSA pThis, RTCRDIGEST hDigest, size_t cbEncodedMsg,
                                                   bool fNoDigestInfo)
{
    AssertReturn(cbEncodedMsg * 2 <= sizeof(pThis->Scratch), VERR_CR_PKIX_INTERNAL_ERROR);

    /*
     * Figure out which hash and select the associate prebaked DigestInfo.
     */
    RTDIGESTTYPE const  enmDigest = RTCrDigestGetType(hDigest);
    AssertReturn(enmDigest != RTDIGESTTYPE_INVALID && enmDigest != RTDIGESTTYPE_UNKNOWN, VERR_CR_PKIX_UNKNOWN_DIGEST_TYPE);
    uint8_t const      *pbDigestInfoStart = NULL;
    size_t              cbDigestInfoStart = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aDigestInfos); i++)
        if (g_aDigestInfos[i].enmDigest == enmDigest)
        {
            pbDigestInfoStart = g_aDigestInfos[i].pb;
            cbDigestInfoStart = g_aDigestInfos[i].cb;
            break;
        }
    if (!pbDigestInfoStart)
        return VERR_CR_PKIX_UNKNOWN_DIGEST_TYPE;

    /*
     * Get the hash size and verify that it matches what we've got in the
     * precooked DigestInfo. ASSUMES less that 256 bytes of hash.
     */
    uint32_t const cbHash = RTCrDigestGetHashSize(hDigest);
    AssertReturn(cbHash > 0 && cbHash < _16K, VERR_OUT_OF_RANGE);
    AssertReturn(cbHash == pbDigestInfoStart[cbDigestInfoStart - 1], VERR_CR_PKIX_INTERNAL_ERROR);

    if (fNoDigestInfo)
        cbDigestInfoStart = 0;

    if (cbDigestInfoStart + cbHash + 11 > cbEncodedMsg)
        return VERR_CR_PKIX_HASH_TOO_LONG_FOR_KEY;

    /*
     * Encode the message the first part of the scratch area.
     */
    uint8_t *pbDst = &pThis->Scratch.abSignature[0];
    pbDst[0] = 0x00;
    pbDst[1] = 0x01;    /* BT - block type, see RFC-2313. */
    size_t cbFFs = cbEncodedMsg - cbHash - cbDigestInfoStart - 3;
    memset(&pbDst[2], 0xff, cbFFs);
    pbDst += cbFFs + 2;
    *pbDst++ = 0x00;
    memcpy(pbDst, pbDigestInfoStart, cbDigestInfoStart);
    pbDst += cbDigestInfoStart;
    /* Note! Must preserve informational status codes from this call . */
    int rc = RTCrDigestFinal(hDigest, pbDst, cbHash);
    if (RT_SUCCESS(rc))
    {
        pbDst += cbHash;
        Assert((size_t)(pbDst - &pThis->Scratch.abSignature[0]) == cbEncodedMsg);
    }
    return rc;
}



/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnVerify}  */
static DECLCALLBACK(int) rtCrPkixSignatureRsa_Verify(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, RTCRKEY hKey,
                                                     RTCRDIGEST hDigest, void const *pvSignature, size_t cbSignature)
{
    PRTCRPKIXSIGNATURERSA pThis = (PRTCRPKIXSIGNATURERSA)pvState;
    RT_NOREF_PV(pDesc);
    Assert(!pThis->fSigning);
    if (cbSignature > sizeof(pThis->Scratch) / 2)
        return VERR_CR_PKIX_SIGNATURE_TOO_LONG;

    /*
     * Get the key bits we need.
     */
    Assert(RTCrKeyGetType(hKey) == RTCRKEYTYPE_RSA_PUBLIC);
    PRTBIGNUM pModulus  = &hKey->u.RsaPublic.Modulus;
    PRTBIGNUM pExponent = &hKey->u.RsaPublic.Exponent;

    /*
     * 8.2.2.1 - Length check.  (RFC-3447)
     */
    if (cbSignature != RTBigNumByteWidth(pModulus))
        return VERR_CR_PKIX_INVALID_SIGNATURE_LENGTH;

    /*
     * 8.2.2.2 - RSA verification / Decrypt the signature.
     */
    /* a) s = OS2IP(S) -- Convert signature to integer. */
    int rc = RTBigNumInit(&pThis->TmpBigNum1, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_UNSIGNED,
                          pvSignature, cbSignature);
    if (RT_FAILURE(rc))
        return rc;
    /* b) RSAVP1 - 5.2.2.2: Range check (0 <= s < n). */
    if (RTBigNumCompare(&pThis->TmpBigNum1, pModulus) < 0)
    {
        if (RTBigNumCompareWithU64(&pThis->TmpBigNum1, 0) >= 0)
        {
            /* b) RSAVP1 - 5.2.2.3: s^e mod n */
            rc = RTBigNumInitZero(&pThis->TmpBigNum2, 0);
            if (RT_SUCCESS(rc))
            {
                rc = RTBigNumModExp(&pThis->TmpBigNum2, &pThis->TmpBigNum1, pExponent, pModulus);
                if (RT_SUCCESS(rc))
                {
                    /* c) EM' = I2OSP(m, k) -- Convert the result to bytes. */
                    uint32_t cbDecrypted = RTBigNumByteWidth(&pThis->TmpBigNum2) + 1; /* 1 = leading zero byte */
                    if (cbDecrypted <= sizeof(pThis->Scratch) / 2)
                    {
                        uint8_t *pbDecrypted = &pThis->Scratch.abSignature[sizeof(pThis->Scratch) / 2];
                        rc = RTBigNumToBytesBigEndian(&pThis->TmpBigNum2, pbDecrypted, cbDecrypted);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * 8.2.2.3 - Build a hopefully identical signature using hDigest.
                             */
                            rc = rtCrPkixSignatureRsa_EmsaPkcs1V15Encode(pThis, hDigest, cbDecrypted, false /* fNoDigestInfo */);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * 8.2.2.4 - Compare the two.
                                 */
                                if (memcmp(&pThis->Scratch.abSignature[0], pbDecrypted, cbDecrypted) == 0)
                                { /* No rc = VINF_SUCCESS here,  mustpreserve informational status codes from digest. */ }
                                else
                                {
                                    /*
                                     * Try again without digestinfo.  This style signing has been
                                     * observed in Vista timestamp counter signatures (Thawte).
                                     */
                                    rc = rtCrPkixSignatureRsa_EmsaPkcs1V15Encode(pThis, hDigest, cbDecrypted,
                                                                                 true /* fNoDigestInfo */);
                                    if (RT_SUCCESS(rc))
                                    {
                                        if (memcmp(&pThis->Scratch.abSignature[0], pbDecrypted, cbDecrypted) == 0)
                                        { /* No rc = VINF_SUCCESS here,  mustpreserve informational status codes from digest. */ }
                                        else
                                            rc = VERR_CR_PKIX_SIGNATURE_MISMATCH;
                                    }
                                }
                            }
                        }
                    }
                    else
                        rc = VERR_CR_PKIX_SIGNATURE_TOO_LONG;
                }
                RTBigNumDestroy(&pThis->TmpBigNum2);
            }
        }
        else
            rc = VERR_CR_PKIX_SIGNATURE_NEGATIVE;
    }
    else
        rc = VERR_CR_PKIX_SIGNATURE_GE_KEY;
    RTBigNumDestroy(&pThis->TmpBigNum1);
    return rc;
}


/** @impl_interface_method{RTCRPKIXSIGNATUREDESC,pfnSign}  */
static DECLCALLBACK(int) rtCrPkixSignatureRsa_Sign(PCRTCRPKIXSIGNATUREDESC pDesc, void *pvState, RTCRKEY hKey,
                                                   RTCRDIGEST hDigest, void *pvSignature, size_t *pcbSignature)
{
    PRTCRPKIXSIGNATURERSA pThis = (PRTCRPKIXSIGNATURERSA)pvState;
    RT_NOREF_PV(pDesc);
    Assert(pThis->fSigning);

    /*
     * Get the key bits we need.
     */
    Assert(RTCrKeyGetType(hKey) == RTCRKEYTYPE_RSA_PRIVATE);
    PRTBIGNUM pModulus  = &hKey->u.RsaPrivate.Modulus;
    PRTBIGNUM pExponent = &hKey->u.RsaPrivate.PrivateExponent;

    /*
     * Calc signature length and return if destination buffer isn't big enough.
     */
    size_t const cbDst        = *pcbSignature;
    size_t const cbEncodedMsg = RTBigNumByteWidth(pModulus);
    *pcbSignature = cbEncodedMsg;
    if (cbEncodedMsg > sizeof(pThis->Scratch) / 2)
        return VERR_CR_PKIX_SIGNATURE_TOO_LONG;
    if (!pvSignature || cbDst < cbEncodedMsg)
        return VERR_BUFFER_OVERFLOW;

    /*
     * 8.1.1.1 - EMSA-PSS encoding.  (RFC-3447)
     */
    int rcRetSuccess;
    int rc = rcRetSuccess = rtCrPkixSignatureRsa_EmsaPkcs1V15Encode(pThis, hDigest, cbEncodedMsg, false /* fNoDigestInfo */);
    if (RT_SUCCESS(rc))
    {
        /*
         * 8.1.1.2 - RSA signature.
         */
        /* a) m = OS2IP(EM) -- Convert the encoded message (EM) to integer. */
        rc = RTBigNumInit(&pThis->TmpBigNum1, RTBIGNUMINIT_F_ENDIAN_BIG | RTBIGNUMINIT_F_UNSIGNED,
                          pThis->Scratch.abSignature, cbEncodedMsg);
        if (RT_SUCCESS(rc))
        {
            /* b) s = RSASP1(K, m = EM) - 5.2.1.1: Range check (0 <= m < n). */
            if (RTBigNumCompare(&pThis->TmpBigNum1, pModulus) < 0)
            {
                /* b) s = RSAVP1(K, m = EM) - 5.2.1.2.a: s = m^d mod n */
                rc = RTBigNumInitZero(&pThis->TmpBigNum2, 0);
                if (RT_SUCCESS(rc))
                {
                    rc = RTBigNumModExp(&pThis->TmpBigNum2, &pThis->TmpBigNum1, pExponent, pModulus);
                    if (RT_SUCCESS(rc))
                    {
                        /* c) S = I2OSP(s, k) -- Convert the result to bytes. */
                        rc = RTBigNumToBytesBigEndian(&pThis->TmpBigNum2, pvSignature, cbEncodedMsg);
                        AssertStmt(RT_SUCCESS(rc) || rc != VERR_BUFFER_OVERFLOW, rc = VERR_CR_PKIX_INTERNAL_ERROR);

                        /* Make sure we return the informational status code from the digest on success. */
                        if (rc == VINF_SUCCESS && rcRetSuccess != VINF_SUCCESS)
                            rc = rcRetSuccess;
                    }
                    RTBigNumDestroy(&pThis->TmpBigNum2);
                }
            }
            else
                rc = VERR_CR_PKIX_SIGNATURE_GE_KEY;
            RTBigNumDestroy(&pThis->TmpBigNum1);
        }
    }
    return rc;
}




/** RSA alias ODIs. */
static const char * const g_apszHashWithRsaAliases[] =
{
    RTCR_PKCS1_MD2_WITH_RSA_OID,
    RTCR_PKCS1_MD4_WITH_RSA_OID,
    RTCR_PKCS1_MD5_WITH_RSA_OID,
    RTCR_PKCS1_SHA1_WITH_RSA_OID,
    RTCR_PKCS1_SHA256_WITH_RSA_OID,
    RTCR_PKCS1_SHA384_WITH_RSA_OID,
    RTCR_PKCS1_SHA512_WITH_RSA_OID,
    RTCR_PKCS1_SHA224_WITH_RSA_OID,
    RTCR_PKCS1_SHA512T224_WITH_RSA_OID,
    RTCR_PKCS1_SHA512T256_WITH_RSA_OID,
    RTCR_NIST_SHA3_224_WITH_RSA_OID,
    RTCR_NIST_SHA3_256_WITH_RSA_OID,
    RTCR_NIST_SHA3_384_WITH_RSA_OID,
    RTCR_NIST_SHA3_512_WITH_RSA_OID,
    /* Note: Note quite sure about these OIW oddballs. */
    "1.3.14.3.2.11" /* OIW rsaSignature */,
    "1.3.14.3.2.14" /* OIW mdc2WithRSASignature */,
    "1.3.14.3.2.15" /* OIW shaWithRSASignature */,
    "1.3.14.3.2.24" /* OIW md2WithRSASignature */,
    "1.3.14.3.2.25" /* OIW md5WithRSASignature */,
    "1.3.14.3.2.29" /* OIW sha1WithRSASignature */,
    NULL
};


/** RSA descriptor. */
DECL_HIDDEN_CONST(RTCRPKIXSIGNATUREDESC const) g_rtCrPkixSigningHashWithRsaDesc =
{
    "RSASSA-PKCS1-v1_5",
    RTCR_PKCS1_RSA_OID,
    g_apszHashWithRsaAliases,
    sizeof(RTCRPKIXSIGNATURERSA),
    0,
    0,
    rtCrPkixSignatureRsa_Init,
    rtCrPkixSignatureRsa_Reset,
    rtCrPkixSignatureRsa_Delete,
    rtCrPkixSignatureRsa_Verify,
    rtCrPkixSignatureRsa_Sign,
};


/**
 * Worker for RTCrRsaPublicKey_CanHandleDigestType and
 * RTCrRsaPrivateKey_CanHandleDigestType.
 *
 * We implement these two functions here because we've already got the
 * DigestInfo sizes nicely lined up here.
 */
static bool rtCrRsa_CanHandleDigestType(int32_t cModulusBits, RTDIGESTTYPE enmDigestType, PRTERRINFO pErrInfo)
{
    /*
     * ASSUME EMSA-PKCS1-v1_5 padding scheme (RFC-8017 section 9.2):
     *     - 11 byte padding prefix (00, 01, 8 times ff)
     *     - digest info der sequence for rsaWithXxxxEncryption
     *     - the hash value.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aDigestInfos); i++)
        if (g_aDigestInfos[i].enmDigest == enmDigestType)
        {
            size_t const cbHash = RTCrDigestTypeToHashSize(enmDigestType);
            AssertBreak(cbHash > 0);

            size_t cbMsg = 11 + g_aDigestInfos[i].cb + cbHash;
            if ((ssize_t)cbMsg <= cModulusBits / 8)
                return true;
            RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_INVALID_SIGNATURE_LENGTH, "cModulusBits=%d cbMsg=%u", cModulusBits, cbMsg);
            return false;
        }
    RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_UNKNOWN_DIGEST_TYPE, "%s", RTCrDigestTypeToName(enmDigestType));
    return false;
}


RTDECL(bool) RTCrRsaPublicKey_CanHandleDigestType(PCRTCRRSAPUBLICKEY pRsaPublicKey, RTDIGESTTYPE enmDigestType,
                                                  PRTERRINFO pErrInfo)
{
    if (RTCrRsaPublicKey_IsPresent(pRsaPublicKey))
        return rtCrRsa_CanHandleDigestType(RTAsn1Integer_UnsignedLastBit(&pRsaPublicKey->Modulus) + 1, enmDigestType, pErrInfo);
    return false;
}


RTDECL(bool) RTCrRsaPrivateKey_CanHandleDigestType(PCRTCRRSAPRIVATEKEY pRsaPrivateKey, RTDIGESTTYPE enmDigestType,
                                                   PRTERRINFO pErrInfo)
{
    if (RTCrRsaPrivateKey_IsPresent(pRsaPrivateKey))
        return rtCrRsa_CanHandleDigestType(RTAsn1Integer_UnsignedLastBit(&pRsaPrivateKey->Modulus) + 1, enmDigestType, pErrInfo);
    return false;
}

