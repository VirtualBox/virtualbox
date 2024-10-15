/* $Id: pkcs7-sign.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS \#7, Signing
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
#include <iprt/crypto/pkcs7.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/key.h>
#include <iprt/crypto/pkix.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/x509.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/asn1t.h>
# include <openssl/pkcs7.h>
# include <openssl/cms.h>
# include <openssl/x509.h>
# include <openssl/err.h>
# include "internal/openssl-post.h"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * PKCS\#7 / CMS signing operation instance.
 */
typedef struct RTCRPKCS7SIGNINGJOBINT
{
    /** Magic value (RTCRPKCS7SIGNINGJOBINT).  */
    uint32_t                u32Magic;
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** RTCRPKCS7SIGN_F_XXX. */
    uint64_t                fFlags;
    /** Set if finalized. */
    bool                    fFinallized;

    //....
} RTCRPKCS7SIGNINGJOBINT;

/** Magic value for RTCRPKCS7SIGNINGJOBINT (Jonathan Lethem). */
#define RTCRPKCS7SIGNINGJOBINT_MAGIC    UINT32_C(0x19640219)

/** Handle to PKCS\#7/CMS signing operation. */
typedef struct RTCRPKCS7SIGNINGJOBINT *RTCRPKCS7SIGNINGJOB;
/** Pointer to a PKCS\#7/CMS signing operation handle. */
typedef RTCRPKCS7SIGNINGJOB *PRTCRPKCS7SIGNINGJOB;

//// CMS_sign
//RTDECL(int) RTCrPkcs7Sign(PRTCRPKCS7SIGNINGJOB *phJob, uint64_t fFlags, PCRTCRX509CERTIFICATE pSigner, RTCRKEY hPrivateKey,
//                          RTCRSTORE hAdditionalCerts,
//

#ifdef IPRT_WITH_OPENSSL

static int rtCrPkcs7SimpleSignSignedDataDoV1TweakContent(PKCS7 *pOsslPkcs7, const char *pszContentId,
                                                         const void *pvData, size_t cbData,
                                                         PRTERRINFO pErrInfo)
{
    AssertReturn(pszContentId, RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_MISSING_CONTENT_TYPE_ATTRIB,
                                            "RTCRPKCS7SIGN_SD_F_NO_DATA_ENCAP requires content type in additional attribs"));

    /*
     * Create a new inner PKCS#7 content container, forcing it to the 'other' type.
     */
    PKCS7 *pOsslInnerContent = PKCS7_new();
    if (!pOsslInnerContent)
        return RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "PKCS7_new failed");

    /* Set the type. */
    int rc;
    pOsslInnerContent->type = OBJ_txt2obj(pszContentId, 1);
    if (pOsslInnerContent->type)
    {
        /* Create a dynamic ASN1 type which we set to a sequence.  */
        ASN1_TYPE *pOsslOther = pOsslInnerContent->d.other = ASN1_TYPE_new();
        if (pOsslOther)
        {
            pOsslOther->type = V_ASN1_SEQUENCE;

            /* Create a string and put the data in it. */
            ASN1_STRING *pOsslStr = pOsslOther->value.sequence = ASN1_STRING_new();
            if (pOsslStr)
            {
                rc = ASN1_STRING_set(pOsslStr, pvData, (int)cbData); /* copies the buffer content */
                if (rc > 0)
                {
                    /*
                     * Set the content in the PKCS#7 signed data we're constructing.
                     * This consumes pOsslInnerContent on success.
                     */
                    rc = PKCS7_set_content(pOsslPkcs7, pOsslInnerContent);
                    if (rc > 0)
                        return VINF_SUCCESS;

                    /* failed */
                    rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "PKCS7_set_content");
                }
                else
                    rc = RTErrInfoSetF(pErrInfo, VERR_NO_MEMORY, "ASN1_STRING_set(,,%#x)", cbData);
            }
            else
                rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "ASN1_STRING_new");
        }
        else
            rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "ASN1_TYPE_new");
    }
    else
        rc = RTErrInfoSetF(pErrInfo, VERR_NO_MEMORY, "OBJ_txt2obj(%s, 1) failed", pszContentId);
    PKCS7_free(pOsslInnerContent);
    return rc;
}


static int rtCrPkcs7SimpleSignSignedDataDoV1TweakedFinal(PKCS7 *pOsslPkcs7, const char *pszContentId,
                                                         const void *pvData, size_t cbData, PRTERRINFO pErrInfo)
{
    AssertReturn(pszContentId, RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_MISSING_CONTENT_TYPE_ATTRIB,
                                            "RTCRPKCS7SIGN_SD_F_NO_DATA_ENCAP requires content type in additional attribs"));

    /*
     * Prepare a BIO of what should be hashed with all the hashing filters attached.
     */
    BIO *pOsslBio = PKCS7_dataInit(pOsslPkcs7, NULL);
    if (!pOsslBio)
        return RTErrInfoSet(pErrInfo, VERR_CR_CIPHER_OSSL_ENCRYPT_FINAL_FAILED, "PKCS7_dataInit failed");

    /*
     * Now write the data.
     *
     * We must skip the outer wrapper here (see RTCrPkcs7VerifySignedData).  This
     * is probably a bit presumptive about what we're working on, so add an extra
     * flag for this later.
     */
    uint8_t const *pbToWrite = (uint8_t const *)pvData;
    size_t         cbToWrite = cbData;

    /** @todo add extra flag for this? */
    RTASN1CURSORPRIMARY SkipCursor;
    RTAsn1CursorInitPrimary(&SkipCursor, pvData, (uint32_t)cbData,
                            pErrInfo,&g_RTAsn1DefaultAllocator, RTASN1CURSOR_FLAGS_DER, "skip");
    RTASN1CORE SkipAsn1Core = { 0 };
    int rc = RTAsn1CursorReadHdr(&SkipCursor.Cursor, &SkipAsn1Core, "skip-core");
    if (RT_SUCCESS(rc))
    {
        pbToWrite += SkipAsn1Core.cbHdr;
        cbToWrite -= SkipAsn1Core.cbHdr;

        rc = BIO_write(pOsslBio, pbToWrite, (int)cbToWrite);
        if (rc == (ssize_t)cbToWrite)
        {
            BIO_flush(pOsslBio); /** @todo error check this */
            if (true)
            {
                /*
                 * Finalize the job - produce the signer info signatures and stuff.
                 */
                rc = PKCS7_dataFinal(pOsslPkcs7, pOsslBio);
                if (rc > 0)
                {
                    /*
                     * Now tweak the content so we get the desired content type and
                     * no extra wrappers and stuff.
                     */
                    rc = rtCrPkcs7SimpleSignSignedDataDoV1TweakContent(pOsslPkcs7, pszContentId, pvData, cbData, pErrInfo);
                }
                else
                    rc = RTErrInfoSetF(pErrInfo, VERR_CR_CIPHER_OSSL_ENCRYPT_FINAL_FAILED, "PKCS7_dataFinal failed: %d", rc);
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_CR_CIPHER_OSSL_ENCRYPT_FINAL_FAILED,
                               "%zu byte data write failed: %d", cbToWrite, rc);
    }
    BIO_free_all(pOsslBio);
    return rc;
}


static int rtCrPkcs7SimpleSignSignedDataDoV1AttribConversion(PKCS7_SIGNER_INFO *pSignerInfo,
                                                             PCRTCRPKCS7ATTRIBUTES pAdditionalAuthenticatedAttribs,
                                                             const char **ppszContentId, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    *ppszContentId = NULL;

    if (pAdditionalAuthenticatedAttribs)
    {

        /*
         * Convert each attribute.
         */
        STACK_OF(X509_ATTRIBUTE) *pOsslAttributes = sk_X509_ATTRIBUTE_new_null();
        for (uint32_t i = 0; i < pAdditionalAuthenticatedAttribs->cItems && RT_SUCCESS(rc); i++)
        {
            PCRTCRPKCS7ATTRIBUTE pAttrib = pAdditionalAuthenticatedAttribs->papItems[i];

            /* Look out for content type, as we will probably need that for
               RTCRPKCS7SIGN_SD_F_NO_DATA_ENCAP later. */
            if (   pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS
                && RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_CONTENT_TYPE_OID) == 0)
            {
                AssertBreakStmt(!*ppszContentId, rc = VERR_CR_PKCS7_BAD_CONTENT_TYPE_ATTRIB);
                AssertBreakStmt(pAttrib->uValues.pObjIds && pAttrib->uValues.pObjIds->cItems == 1,
                                rc = VERR_CR_PKCS7_BAD_CONTENT_TYPE_ATTRIB);
                *ppszContentId = pAttrib->uValues.pObjIds->papItems[0]->szObjId;
            }

            /* The conversion (IPRT encode, OpenSSL decode). */
            X509_ATTRIBUTE *pOsslAttrib;
            rc = rtCrOpenSslConvertPkcs7Attribute((void **)&pOsslAttrib, pAttrib, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (!sk_X509_ATTRIBUTE_push(pOsslAttributes, pOsslAttrib))
                    rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "sk_X509_ATTRIBUTE_push failed");
            }
        }

        /*
         * If we've successfully converted all the attributes, make a deep copy
         * (waste of resource, but whatever) into the signer info we're working on.
         */
        if (RT_SUCCESS(rc))
        {
            rc = PKCS7_set_signed_attributes(pSignerInfo, pOsslAttributes); /* deep copy */
            if (rc <= 0)
                rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "PKCS7_set_signed_attributes failed");
        }

        /*
         * Free the attributes (they were copied).  Cannot use X509_ATTRIBUTE_pop_free as
         * the callback causes Visual C++ to complain about exceptions on the callback.
         */
        for (int i = sk_X509_ATTRIBUTE_num(pOsslAttributes) - 1; i >= 0; i--)
            X509_ATTRIBUTE_free(sk_X509_ATTRIBUTE_value(pOsslAttributes, i));
        sk_X509_ATTRIBUTE_free(pOsslAttributes);
    }
    return rc;
}

static int rtCrPkcs7SimpleSignSignedDataDoV1(uint32_t fFlags, X509 *pOsslSigner, EVP_PKEY *pEvpPrivateKey,
                                             BIO *pOsslData, const EVP_MD *pEvpMd, STACK_OF(X509) *pOsslAdditionalCerts,
                                             PCRTCRPKCS7ATTRIBUTES pAdditionalAuthenticatedAttribs,
                                             const void *pvData, size_t cbData,
                                             BIO **ppOsslResult, PRTERRINFO pErrInfo)
{
    /*
     * Use PKCS7_sign with PKCS7_PARTIAL to start a extended the signing process.
     */
    /* Create a ContentInfo we can modify using CMS_sign w/ CMS_PARTIAL. */
    unsigned int fOsslSign = PKCS7_BINARY | PKCS7_PARTIAL;
    if (fFlags & RTCRPKCS7SIGN_SD_F_DEATCHED)
        fOsslSign |= PKCS7_DETACHED;
    if (fFlags & RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP)
        fOsslSign |= PKCS7_NOSMIMECAP;
    int rc = VINF_SUCCESS;
    PKCS7 *pCms = PKCS7_sign(NULL, NULL, pOsslAdditionalCerts, NULL, fOsslSign);
    if (pCms != NULL)
    {
        /*
         * Add a signer.
         */
        PKCS7_SIGNER_INFO *pSignerInfo = PKCS7_sign_add_signer(pCms, pOsslSigner, pEvpPrivateKey, pEvpMd, fOsslSign);
        if (pSignerInfo)
        {
            /*
             * Add additional attributes to the signer.
             */
            const char *pszContentId = NULL;
            rc = rtCrPkcs7SimpleSignSignedDataDoV1AttribConversion(pSignerInfo, pAdditionalAuthenticatedAttribs,
                                                                   &pszContentId, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Finalized and actually sign the data.
                 */
                bool const fTweaked = (fFlags & (RTCRPKCS7SIGN_SD_F_DEATCHED | RTCRPKCS7SIGN_SD_F_NO_DATA_ENCAP))
                                   == RTCRPKCS7SIGN_SD_F_NO_DATA_ENCAP;
                if (fTweaked)
                    rc = rtCrPkcs7SimpleSignSignedDataDoV1TweakedFinal(pCms, pszContentId, pvData, cbData, pErrInfo);
                else
                {
                    rc = PKCS7_final(pCms, pOsslData, fOsslSign);
                    if (rc > 0)
                        rc = VINF_SUCCESS;
                    else
                        rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "PKCS7_final");
                    /** @todo maybe we want to use rtCrPkcs7SimpleSignSignedDataDoV1TweakContent
                     * for when the content type isn't 'data'...  */
                }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Get the output and copy it into the result buffer.
                     */
                    BIO *pOsslResult = BIO_new(BIO_s_mem());
                    if (pOsslResult)
                    {
                        rc = i2d_PKCS7_bio(pOsslResult, pCms);
                        if (rc > 0)
                        {
                            *ppOsslResult = pOsslResult;
                            rc = VINF_SUCCESS;
                        }
                        else
                        {
                            rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "i2d_PKCS7_bio");
                            BIO_free(pOsslResult);
                        }
                    }
                    else
                        rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "BIO_new/BIO_s_mem");
                }
            }
            else
                rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "PKCS7_sign_add_signer");
        }
        PKCS7_free(pCms);
    }
    else
        rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "PKCS7_sign");
    return rc;
}


static int  rtCrPkcs7SimpleSignSignedDataDoDefault(uint32_t fFlags, X509 *pOsslSigner, EVP_PKEY *pEvpPrivateKey,
                                                   BIO *pOsslData, const EVP_MD *pEvpMd, STACK_OF(X509) *pOsslAdditionalCerts,
                                                   PCRTCRPKCS7ATTRIBUTES pAdditionalAuthenticatedAttribs,
                                                   BIO **ppOsslResult, PRTERRINFO pErrInfo)

{
    /*
     * Use CMS_sign with CMS_PARTIAL to start a extended the signing process.
     */
    /* Create a ContentInfo we can modify using CMS_sign w/ CMS_PARTIAL. */
    unsigned int fOsslSign = CMS_BINARY | CMS_PARTIAL;
    if (fFlags & RTCRPKCS7SIGN_SD_F_DEATCHED)
        fOsslSign |= CMS_DETACHED;
    if (fFlags & RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP)
        fOsslSign |= CMS_NOSMIMECAP;
    int rc = VINF_SUCCESS;
    CMS_ContentInfo *pCms = CMS_sign(NULL, NULL, pOsslAdditionalCerts, NULL, fOsslSign);
    if (pCms != NULL)
    {
        /*
         * Set encapsulated content type if present in the auth attribs.
         */
        uint32_t iAuthAttrSkip = UINT32_MAX;
        for (uint32_t i = 0; i < pAdditionalAuthenticatedAttribs->cItems && RT_SUCCESS(rc); i++)
        {
            PCRTCRPKCS7ATTRIBUTE pAttrib = pAdditionalAuthenticatedAttribs->papItems[i];
            if (   pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS
                && RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_CONTENT_TYPE_OID) == 0)
            {
                AssertBreakStmt(pAttrib->uValues.pObjIds && pAttrib->uValues.pObjIds->cItems == 1,
                                rc = VERR_INTERNAL_ERROR_3);
                PCRTASN1OBJID pObjId     = pAttrib->uValues.pObjIds->papItems[0];
                ASN1_OBJECT  *pOsslObjId = OBJ_txt2obj(pObjId->szObjId, 0 /*no_name*/);
                if (pOsslObjId)
                {
                    rc = CMS_set1_eContentType(pCms, pOsslObjId);
                    ASN1_OBJECT_free(pOsslObjId);
                    if (rc < 0)
                        rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_GENERIC_ERROR,
                                           "CMS_set1_eContentType(%s)", pObjId->szObjId);
                }
                else
                    rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "OBJ_txt2obj");

                iAuthAttrSkip = i;
                break;
            }
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Add a signer.
             */
            CMS_SignerInfo *pSignerInfo = CMS_add1_signer(pCms, pOsslSigner, pEvpPrivateKey, pEvpMd, fOsslSign);
            if (pSignerInfo)
            {
                /*
                 * Add additional attributes, skipping the content type if found above.
                 */
                if (pAdditionalAuthenticatedAttribs)
                    for (uint32_t i = 0; i < pAdditionalAuthenticatedAttribs->cItems && RT_SUCCESS(rc); i++)
                        if (i != iAuthAttrSkip)
                        {
                            PCRTCRPKCS7ATTRIBUTE pAttrib = pAdditionalAuthenticatedAttribs->papItems[i];
                            X509_ATTRIBUTE *pOsslAttrib;
                            rc = rtCrOpenSslConvertPkcs7Attribute((void **)&pOsslAttrib, pAttrib, pErrInfo);
                            if (RT_SUCCESS(rc))
                            {
                                rc = CMS_signed_add1_attr(pSignerInfo, pOsslAttrib);
                                rtCrOpenSslFreeConvertedPkcs7Attribute((void **)pOsslAttrib);
                                if (rc <= 0)
                                    rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "CMS_signed_add1_attr");
                            }
                        }
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Finalized and actually sign the data.
                     */
                    rc = CMS_final(pCms, pOsslData, NULL /*dcont*/, fOsslSign);
                    if (rc > 0)
                    {
                        /*
                         * Get the output and copy it into the result buffer.
                         */
                        BIO *pOsslResult = BIO_new(BIO_s_mem());
                        if (pOsslResult)
                        {
                            rc = i2d_CMS_bio(pOsslResult, pCms);
                            if (rc > 0)
                            {
                                *ppOsslResult = pOsslResult;
                                rc = VINF_SUCCESS;
                            }
                            else
                            {
                                rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "i2d_CMS_bio");
                                BIO_free(pOsslResult);
                            }
                        }
                        else
                            rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "BIO_new/BIO_s_mem");
                    }
                    else
                        rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "CMS_final");
                }
            }
            else
                rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "CMS_add1_signer");
        }
        CMS_ContentInfo_free(pCms);
    }
    else
        rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "CMS_sign");
    return rc;
}

#endif /* IPRT_WITH_OPENSSL */



RTDECL(int) RTCrPkcs7SimpleSignSignedData(uint32_t fFlags, PCRTCRX509CERTIFICATE pSigner, RTCRKEY hPrivateKey,
                                          void const *pvData, size_t cbData, RTDIGESTTYPE enmDigestType,
                                          RTCRSTORE hAdditionalCerts, PCRTCRPKCS7ATTRIBUTES pAdditionalAuthenticatedAttribs,
                                          void *pvResult, size_t *pcbResult, PRTERRINFO pErrInfo)
{
    size_t const cbResultBuf = *pcbResult;
    *pcbResult = 0;
    AssertReturn(!(fFlags & ~RTCRPKCS7SIGN_SD_F_VALID_MASK), VERR_INVALID_FLAGS);
#ifdef IPRT_WITH_OPENSSL
    AssertReturn((int)cbData >= 0 && (unsigned)cbData == cbData, VERR_TOO_MUCH_DATA);

    /*
     * Resolve the digest type.
     */
    const EVP_MD *pEvpMd = NULL;
    if (enmDigestType != RTDIGESTTYPE_UNKNOWN)
    {
        pEvpMd = (const EVP_MD *)rtCrOpenSslConvertDigestType(enmDigestType, pErrInfo);
        AssertReturn(pEvpMd, pErrInfo ? pErrInfo->rc : VERR_INVALID_PARAMETER);
    }

    /*
     * Convert the private key.
     */
    EVP_PKEY *pEvpPrivateKey = NULL;
    int rc = rtCrKeyToOpenSslKey(hPrivateKey, false /*fNeedPublic*/, (void **)&pEvpPrivateKey, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /*
         * Convert the signing certificate.
         */
        X509 *pOsslSigner = NULL;
        rc = rtCrOpenSslConvertX509Cert((void **)&pOsslSigner, pSigner, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            /*
             * Convert any additional certificates.
             */
            STACK_OF(X509) *pOsslAdditionalCerts = NULL;
            if (hAdditionalCerts != NIL_RTCRSTORE)
                rc = RTCrStoreConvertToOpenSslCertStack(hAdditionalCerts, 0 /*fFlags*/, (void **)&pOsslAdditionalCerts, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Create a BIO for the data buffer.
                 */
                BIO *pOsslData = BIO_new_mem_buf((void *)pvData, (int)cbData);
                if (pOsslData)
                {
                    /*
                     * Do the work.
                     */
                    BIO *pOsslResult = NULL;
                    if (!(fFlags & RTCRPKCS7SIGN_SD_F_USE_V1))
                        rc = rtCrPkcs7SimpleSignSignedDataDoDefault(fFlags, pOsslSigner, pEvpPrivateKey, pOsslData, pEvpMd,
                                                                    pOsslAdditionalCerts, pAdditionalAuthenticatedAttribs,
                                                                    &pOsslResult, pErrInfo);
                    else
                        rc = rtCrPkcs7SimpleSignSignedDataDoV1(fFlags, pOsslSigner, pEvpPrivateKey, pOsslData, pEvpMd,
                                                               pOsslAdditionalCerts, pAdditionalAuthenticatedAttribs,
                                                               pvData, cbData,
                                                               &pOsslResult, pErrInfo);
                    BIO_free(pOsslData);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Copy out the result.
                         */
                        BUF_MEM *pBuf = NULL;
                        rc = (int)BIO_get_mem_ptr(pOsslResult, &pBuf);
                        if (rc > 0)
                        {
                            AssertPtr(pBuf);
                            size_t const cbResult = pBuf->length;
                            if (   cbResultBuf >= cbResult
                                && pvResult != NULL)
                            {
                                memcpy(pvResult, pBuf->data, cbResult);
                                rc = VINF_SUCCESS;
                            }
                            else
                                rc = VERR_BUFFER_OVERFLOW;
                            *pcbResult = cbResult;
                        }
                        else
                            rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "BIO_get_mem_ptr");
                        BIO_free(pOsslResult);
                    }
                }
            }
            rtCrOpenSslFreeConvertedX509Cert(pOsslSigner);
        }
        EVP_PKEY_free(pEvpPrivateKey);
    }
    return rc;
#else
    RT_NOREF(fFlags, pSigner, hPrivateKey, pvData, cbData, enmDigestType, hAdditionalCerts, pAdditionalAuthenticatedAttribs,
             pvResult, pErrInfo, cbResultBuf);
    *pcbResult = 0;
    return VERR_NOT_IMPLEMENTED;
#endif
}

