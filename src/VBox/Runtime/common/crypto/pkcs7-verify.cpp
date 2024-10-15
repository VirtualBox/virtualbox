/* $Id: pkcs7-verify.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS \#7, Verification
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
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/key.h>
#include <iprt/crypto/pkix.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/x509.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/pkcs7.h>
# include <openssl/x509.h>
# include <openssl/err.h>
# include "internal/openssl-post.h"
#endif



#ifdef IPRT_WITH_OPENSSL
static int rtCrPkcs7VerifySignedDataUsingOpenSsl(PCRTCRPKCS7CONTENTINFO pContentInfo, uint32_t fFlags,
                                                 RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                                 void const *pvContent, size_t cbContent, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(fFlags);

    /*
     * Verify using OpenSSL.          ERR_PUT_error
     */
    unsigned char const *pbRawContent;
    uint32_t             cbRawContent;
    void                *pvFree;
    int rcOssl = RTAsn1EncodeQueryRawBits(RTCrPkcs7ContentInfo_GetAsn1Core(pContentInfo),
                                          (const uint8_t **)&pbRawContent, &cbRawContent, &pvFree, pErrInfo);
    AssertRCReturn(rcOssl, rcOssl);

    PKCS7 *pOsslPkcs7    = NULL;
    PKCS7 *pOsslPkcs7Ret = d2i_PKCS7(&pOsslPkcs7, &pbRawContent, cbRawContent);

    RTMemTmpFree(pvFree);

    if (pOsslPkcs7 != NULL && pOsslPkcs7Ret == pOsslPkcs7)
    {
        STACK_OF(X509) *pAddCerts = NULL;
        if (hAdditionalCerts != NIL_RTCRSTORE)
            rcOssl = RTCrStoreConvertToOpenSslCertStack(hAdditionalCerts, 0, (void **)&pAddCerts, pErrInfo);
        else
        {
            pAddCerts = sk_X509_new_null();
            rcOssl = RT_LIKELY(pAddCerts != NULL) ? VINF_SUCCESS : VERR_NO_MEMORY;
        }
        if (RT_SUCCESS(rcOssl))
        {
            PCRTCRPKCS7SETOFCERTS pCerts = &pContentInfo->u.pSignedData->Certificates;
            for (uint32_t i = 0; i < pCerts->cItems; i++)
                if (pCerts->papItems[i]->enmChoice == RTCRPKCS7CERTCHOICE_X509)
                    rtCrOpenSslAddX509CertToStack(pAddCerts, pCerts->papItems[i]->u.pX509Cert, NULL);

            X509_STORE *pTrustedCerts = NULL;
            if (hTrustedCerts != NIL_RTCRSTORE)
                rcOssl = RTCrStoreConvertToOpenSslCertStore(hTrustedCerts, 0, (void **)&pTrustedCerts, pErrInfo);
            if (RT_SUCCESS(rcOssl))
            {
                rtCrOpenSslInit();

                BIO *pBioContent = BIO_new_mem_buf((void *)pvContent, (int)cbContent);
                if (pBioContent)
                {
                    uint32_t fOsslFlags = PKCS7_NOCHAIN;
                    fOsslFlags |= PKCS7_NOVERIFY; // temporary hack.
                    if (PKCS7_verify(pOsslPkcs7, pAddCerts, pTrustedCerts, pBioContent, NULL /*out*/, fOsslFlags))
                        rcOssl = VINF_SUCCESS;
                    else
                    {
                        rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_OSSL_VERIFY_FAILED, "PKCS7_verify failed: ");
                        if (pErrInfo)
                            ERR_print_errors_cb(rtCrOpenSslErrInfoCallback, pErrInfo);
                    }
                    BIO_free(pBioContent);
                }
                if (pTrustedCerts)
                    X509_STORE_free(pTrustedCerts);
            }
            else
                rcOssl = RTErrInfoSet(pErrInfo, rcOssl, "RTCrStoreConvertToOpenSslCertStore failed");
#include "internal/openssl-pre.h" /* Need to disable C5039 warning here. */
            if (pAddCerts)
                sk_X509_pop_free(pAddCerts, X509_free);
#include "internal/openssl-post.h"
        }
        else
            rcOssl = RTErrInfoSet(pErrInfo, rcOssl, "RTCrStoreConvertToOpenSslCertStack failed");
        PKCS7_free(pOsslPkcs7);
    }
    else
    {
        rcOssl = RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_OSSL_D2I_FAILED, "d2i_PKCS7 failed");
        if (pErrInfo)
            ERR_print_errors_cb(rtCrOpenSslErrInfoCallback, pErrInfo);
    }

    return rcOssl;
}
#endif /* IPRT_WITH_OPENSSL */



static int rtCrPkcs7VerifyCertUsageTimstamping(PCRTCRX509CERTIFICATE pCert, PRTERRINFO pErrInfo)
{
    if (!(pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_EXT_KEY_USAGE))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "No extended key usage certificate attribute.");
    if (!(pCert->TbsCertificate.T3.fExtKeyUsage & (RTCRX509CERT_EKU_F_TIMESTAMPING | RTCRX509CERT_EKU_F_MS_TIMESTAMP_SIGNING)))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "fExtKeyUsage=%#x, missing %#x (time stamping)",
                             pCert->TbsCertificate.T3.fExtKeyUsage,
                             RTCRX509CERT_EKU_F_TIMESTAMPING | RTCRX509CERT_EKU_F_MS_TIMESTAMP_SIGNING);
    return VINF_SUCCESS;
}


static int rtCrPkcs7VerifyCertUsageDigitalSignature(PCRTCRX509CERTIFICATE pCert, PRTERRINFO pErrInfo)
{
    if (   (pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_KEY_USAGE)
        && !(pCert->TbsCertificate.T3.fKeyUsage & RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "fKeyUsage=%#x, missing %#x",
                             pCert->TbsCertificate.T3.fKeyUsage, RTCRX509CERT_KEY_USAGE_F_DIGITAL_SIGNATURE);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{RTCRPKCS7VERIFYCERTCALLBACK,
 *  Default implementation that checks for the DigitalSignature KeyUsage bit.}
 */
RTDECL(int) RTCrPkcs7VerifyCertCallbackDefault(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths, uint32_t fFlags,
                                               void *pvUser, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(hCertPaths); RT_NOREF_PV(pvUser);
    int rc = VINF_SUCCESS;

    if (fFlags & RTCRPKCS7VCC_F_SIGNED_DATA)
        rc = rtCrPkcs7VerifyCertUsageDigitalSignature(pCert, pErrInfo);

    if (   (fFlags & RTCRPKCS7VCC_F_TIMESTAMP)
        && RT_SUCCESS(rc))
        rc = rtCrPkcs7VerifyCertUsageTimstamping(pCert, pErrInfo);

    return rc;
}


/**
 * @callback_method_impl{RTCRPKCS7VERIFYCERTCALLBACK,
 * Standard code signing.  Use this for Microsoft SPC.}
 */
RTDECL(int) RTCrPkcs7VerifyCertCallbackCodeSigning(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths, uint32_t fFlags,
                                                   void *pvUser, PRTERRINFO pErrInfo)
{
    RT_NOREF_PV(hCertPaths); RT_NOREF_PV(pvUser);
    int rc = VINF_SUCCESS;
    if (fFlags & RTCRPKCS7VCC_F_SIGNED_DATA)
    {
        /*
         * If KeyUsage is present it must include digital signature.
         */
        rc = rtCrPkcs7VerifyCertUsageDigitalSignature(pCert, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            /*
             * The extended usage 'code signing' must be present.
             */
            if (!(pCert->TbsCertificate.T3.fFlags & RTCRX509TBSCERTIFICATE_F_PRESENT_EXT_KEY_USAGE))
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "No extended key usage certificate attribute.");
            if (!(pCert->TbsCertificate.T3.fExtKeyUsage & RTCRX509CERT_EKU_F_CODE_SIGNING))
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_KEY_USAGE_MISMATCH, "fExtKeyUsage=%#RX64, missing CODE_SIGNING (%#RX64)",
                                     pCert->TbsCertificate.T3.fExtKeyUsage, RTCRX509CERT_EKU_F_CODE_SIGNING);
        }
    }

    /*
     * Timestamping too?
     */
    if (   (fFlags & RTCRPKCS7VCC_F_TIMESTAMP)
        && RT_SUCCESS(rc))
        rc = rtCrPkcs7VerifyCertUsageTimstamping(pCert, pErrInfo);

    return rc;
}


/**
 * Deals with authenticated attributes.
 *
 * When authenticated attributes are present (checked by caller) we must:
 *  - fish out the content type and check it against the content inof,
 *  - fish out the message digest among and check it against *phDigest,
 *  - compute the message digest of the authenticated attributes and
 *    replace *phDigest with this for the signature verification.
 *
 * @returns IPRT status code.
 * @param   pSignerInfo         The signer info being verified.
 * @param   pSignedData         The signed data.
 * @param   phDigest            On input this is the digest of the content. On
 *                              output it will (on success) be a reference to
 *                              the message digest of the authenticated
 *                              attributes.  The input reference is consumed.
 *                              The caller shall release the output reference.
 * @param   fFlags              Flags.
 * @param   pErrInfo            Extended error info, optional.
 */
static int rtCrPkcs7VerifySignerInfoAuthAttribs(PCRTCRPKCS7SIGNERINFO pSignerInfo, PCRTCRPKCS7SIGNEDDATA pSignedData,
                                                PRTCRDIGEST phDigest, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /*
     * Scan the attributes and validate the two required attributes
     * (RFC-2315, chapter 9.2, fourth bullet).  Checking that we've got exactly
     * one of each of them is checked by the santiy checker function, so we'll
     * just assert that it did it's job here.
     */
    uint32_t    cContentTypes   = 0;
    uint32_t    cMessageDigests = 0;
    uint32_t    i               = pSignerInfo->AuthenticatedAttributes.cItems;
    while (i-- > 0)
    {
        PCRTCRPKCS7ATTRIBUTE pAttrib = pSignerInfo->AuthenticatedAttributes.papItems[i];

        if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_CONTENT_TYPE_OID) == 0)
        {
            AssertReturn(!cContentTypes, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->uValues.pObjIds->cItems == 1, VERR_CR_PKCS7_INTERNAL_ERROR);

            if (   !(fFlags & RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE) /* See note about microsoft below. */
                && RTAsn1ObjId_Compare(pAttrib->uValues.pObjIds->papItems[0], &pSignedData->ContentInfo.ContentType) != 0)
                   return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_CONTENT_TYPE_ATTRIB_MISMATCH,
                                        "Expected content-type %s, found %s", pAttrib->uValues.pObjIds->papItems[0]->szObjId,
                                        pSignedData->ContentInfo.ContentType.szObjId);
            cContentTypes++;
        }
        else if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_MESSAGE_DIGEST_OID) == 0)
        {
            AssertReturn(!cMessageDigests, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OCTET_STRINGS, VERR_CR_PKCS7_INTERNAL_ERROR);
            AssertReturn(pAttrib->uValues.pOctetStrings->cItems == 1, VERR_CR_PKCS7_INTERNAL_ERROR);

            if (!RTCrDigestMatch(*phDigest,
                                 pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.uData.pv,
                                 pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.cb))
            {
                size_t cbHash = RTCrDigestGetHashSize(*phDigest);
                if (cbHash != pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.cb)
                    return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MESSAGE_DIGEST_ATTRIB_MISMATCH,
                                         "Authenticated message-digest attribute mismatch: cbHash=%#zx cbValue=%#x",
                                         cbHash, pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.cb);
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MESSAGE_DIGEST_ATTRIB_MISMATCH,
                                     "Authenticated message-digest attribute mismatch (cbHash=%#zx):\n"
                                     "signed: %.*Rhxs\n"
                                     "our:    %.*Rhxs\n",
                                     cbHash,
                                     cbHash, pAttrib->uValues.pOctetStrings->papItems[0]->Asn1Core.uData.pv,
                                     cbHash, RTCrDigestGetHash(*phDigest));
            }
            cMessageDigests++;
        }
    }

    /*
     * Full error reporting here as we don't currently extensively santiy check
     * counter signatures.
     * Note! Microsoft includes content info in their timestamp counter signatures,
     *       at least for vista, despite the RFC-3852 stating counter signatures
     *       "MUST NOT contain a content-type".
     */
    if (RT_UNLIKELY(   cContentTypes != 1
                    && !(fFlags & RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE)))
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_MISSING_CONTENT_TYPE_ATTRIB,
                            "Missing authenticated content-type attribute.");
    if (RT_UNLIKELY(cMessageDigests != 1))
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_MISSING_MESSAGE_DIGEST_ATTRIB,
                            "Missing authenticated message-digest attribute.");

    /*
     * Calculate the digest of the authenticated attributes for use in the
     * signature validation.
     */
    if (   pSignerInfo->DigestAlgorithm.Parameters.enmType != RTASN1TYPE_NULL
        && pSignerInfo->DigestAlgorithm.Parameters.enmType != RTASN1TYPE_NOT_PRESENT)
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_DIGEST_PARAMS_NOT_IMPL, "Digest algorithm has unsupported parameters");

    RTCRDIGEST hDigest;
    int rc = RTCrDigestCreateByObjId(&hDigest, &pSignerInfo->DigestAlgorithm.Algorithm);
    if (RT_SUCCESS(rc))
    {
        RTCrDigestRelease(*phDigest);
        *phDigest = hDigest;

        /** @todo The encoding step modifies the data, contradicting the const-ness
         *        of the parameter. */
        rc = RTCrPkcs7Attributes_HashAttributes((PRTCRPKCS7ATTRIBUTES)&pSignerInfo->AuthenticatedAttributes, hDigest, pErrInfo);
    }
    return rc;
}


/**
 * Find the handle to the digest given by the specified SignerInfo.
 *
 * @returns IPRT status code
 * @param   phDigest            Where to return a referenced digest handle on
 *                              success.
 * @param   pSignedData         The signed data structure.
 * @param   pSignerInfo         The signer info.
 * @param   pahDigests          Array of content digests that runs parallel to
 *                              pSignedData->DigestAlgorithms.
 * @param   pErrInfo            Where to store additional error details,
 *                              optional.
 */
static int rtCrPkcs7VerifyFindDigest(PRTCRDIGEST phDigest, PCRTCRPKCS7SIGNEDDATA pSignedData,
                                     PCRTCRPKCS7SIGNERINFO pSignerInfo, PRTCRDIGEST pahDigests, PRTERRINFO pErrInfo)
{
    uint32_t iDigest = pSignedData->DigestAlgorithms.cItems;
    while (iDigest-- > 0)
        if (RTCrX509AlgorithmIdentifier_Compare(pSignedData->DigestAlgorithms.papItems[iDigest],
                                                &pSignerInfo->DigestAlgorithm) == 0)
        {
            RTCRDIGEST hDigest = pahDigests[iDigest];
            uint32_t cRefs = RTCrDigestRetain(hDigest);
            AssertReturn(cRefs != UINT32_MAX, VERR_CR_PKCS7_INTERNAL_ERROR);
            *phDigest = hDigest;
            return VINF_SUCCESS;
        }
    *phDigest = NIL_RTCRDIGEST; /* Make gcc happy. */
    return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_ALGO_NOT_FOUND_IN_LIST,
                         "SignerInfo.DigestAlgorithm %s not found.",
                         pSignerInfo->DigestAlgorithm.Algorithm.szObjId);
}


/**
 * Verifies one signature on a PKCS \#7 SignedData.
 *
 * @returns IPRT status code.
 * @param   pSignerInfo         The signature.
 * @param   pSignedData         The SignedData.
 * @param   hDigests            The digest corresponding to
 *                              pSignerInfo->DigestAlgorithm.
 * @param   fFlags              Verification flags.
 * @param   hAdditionalCerts    Store containing optional certificates,
 *                              optional.
 * @param   hTrustedCerts       Store containing trusted certificates, required.
 * @param   pValidationTime     The time we're supposed to validate the
 *                              certificates chains at.
 * @param   pfnVerifyCert       Signing certificate verification callback.
 * @param   fVccFlags           Signing certificate verification callback flags.
 * @param   pvUser              Callback parameter.
 * @param   pErrInfo            Where to store additional error details,
 *                              optional.
 */
static int rtCrPkcs7VerifySignerInfo(PCRTCRPKCS7SIGNERINFO pSignerInfo, PCRTCRPKCS7SIGNEDDATA pSignedData,
                                     RTCRDIGEST hDigest, uint32_t fFlags, RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                     PCRTTIMESPEC pValidationTime, PFNRTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert,
                                     uint32_t fVccFlags, void *pvUser, PRTERRINFO pErrInfo)
{
    /*
     * Locate the certificate used for signing.
     */
    PCRTCRCERTCTX           pSignerCertCtx = NULL;
    PCRTCRX509CERTIFICATE   pSignerCert = NULL;
    if (hTrustedCerts != NIL_RTCRSTORE)
        pSignerCertCtx = RTCrStoreCertByIssuerAndSerialNo(hTrustedCerts, &pSignerInfo->IssuerAndSerialNumber.Name,
                                                          &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
    if (!pSignerCertCtx && hAdditionalCerts != NIL_RTCRSTORE)
        pSignerCertCtx = RTCrStoreCertByIssuerAndSerialNo(hAdditionalCerts, &pSignerInfo->IssuerAndSerialNumber.Name,
                                                          &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
    if (pSignerCertCtx)
        pSignerCert = pSignerCertCtx->pCert;
    else
    {
        pSignerCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                          &pSignerInfo->IssuerAndSerialNumber.Name,
                                                                          &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
        if (!pSignerCert)
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNED_DATA_CERT_NOT_FOUND,
                                 "Certificate not found: serial=%.*Rhxs",
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.cb,
                                 pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.uData.pv);
    }

    /*
     * Unless caller requesed all certificates to be trusted fully, we always
     * pass it on to the certificate path builder so it can do the requested
     * checks on trust anchors.   (We didn't used to do this as the path
     * builder could handle trusted targets.  A benefit here is that
     * pfnVerifyCert can assume a hCertPaths now, and get the validation time
     * from it if it wants it.)
     *
     * If no valid paths are found, this step will fail.
     */
    int rc;
    if (!(fFlags & RTCRPKCS7VERIFY_SD_F_TRUST_ALL_CERTS))
    {
        RTCRX509CERTPATHS hCertPaths;
        rc = RTCrX509CertPathsCreate(&hCertPaths, pSignerCert);
        if (RT_SUCCESS(rc))
        {
            rc = RTCrX509CertPathsSetValidTimeSpec(hCertPaths, pValidationTime);
            if (hTrustedCerts != NIL_RTCRSTORE && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetTrustedStore(hCertPaths, hTrustedCerts);
            if (hAdditionalCerts != NIL_RTCRSTORE && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetUntrustedStore(hCertPaths, hAdditionalCerts);
            if (pSignedData->Certificates.cItems > 0 && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetUntrustedSet(hCertPaths, &pSignedData->Certificates);
            if ((fFlags & RTCRPKCS7VERIFY_SD_F_CHECK_TRUST_ANCHORS) && RT_SUCCESS(rc))
                rc = RTCrX509CertPathsSetTrustAnchorChecks(hCertPaths, true /*fEnable*/);
            if (RT_SUCCESS(rc))
            {
                rc = RTCrX509CertPathsBuild(hCertPaths, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = RTCrX509CertPathsValidateAll(hCertPaths, NULL, pErrInfo);

                /*
                 * Check that the certificate purpose and whatnot matches what
                 * is being signed.
                 */
                if (RT_SUCCESS(rc))
                    rc = pfnVerifyCert(pSignerCert, hCertPaths, fVccFlags, pvUser, pErrInfo);
            }
            else
                RTErrInfoSetF(pErrInfo, rc, "Error configuring path builder: %Rrc", rc);
            RTCrX509CertPathsRelease(hCertPaths);
        }
    }
    /*
     * Check that the certificate purpose matches what is signed.
     */
    else
        rc = pfnVerifyCert(pSignerCert, NIL_RTCRX509CERTPATHS, fVccFlags, pvUser, pErrInfo);

    /*
     * Reference the digest so we can safely replace with one on the
     * authenticated attributes below.
     */
    if (   RT_SUCCESS(rc)
        && RTCrDigestRetain(hDigest) != UINT32_MAX)
    {
        /*
         * If there are authenticated attributes, we've got more work before we
         * can verify the signature.
         */
        if (   RT_SUCCESS(rc)
            && RTCrPkcs7Attributes_IsPresent(&pSignerInfo->AuthenticatedAttributes))
            rc = rtCrPkcs7VerifySignerInfoAuthAttribs(pSignerInfo, pSignedData, &hDigest, fFlags, pErrInfo);

        /*
         * Verify the signature.
         */
        if (RT_SUCCESS(rc))
        {
            RTCRKEY hKey;
            rc = RTCrKeyCreateFromSubjectPublicKeyInfo(&hKey, &pSignerCert->TbsCertificate.SubjectPublicKeyInfo,
                                                       pErrInfo, "pkcs7");
            if (RT_SUCCESS(rc))
            {
                RTCRPKIXSIGNATURE hSignature;
                rc = RTCrPkixSignatureCreateByObjId(&hSignature, &pSignerInfo->DigestEncryptionAlgorithm.Algorithm,
                                                    hKey, &pSignerInfo->DigestEncryptionAlgorithm.Parameters, false /*fSigning*/);
                RTCrKeyRelease(hKey);
                if (RT_SUCCESS(rc))
                {
                    /** @todo Check that DigestEncryptionAlgorithm is compatible with hSignature
                     *        (this is not vital). */
                    rc = RTCrPkixSignatureVerifyOctetString(hSignature, hDigest, &pSignerInfo->EncryptedDigest);
                    if (RT_FAILURE(rc))
                        rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNATURE_VERIFICATION_FAILED,
                                           "Signature verification failed: %Rrc", rc);
                    RTCrPkixSignatureRelease(hSignature);
                }
                else
                    rc = RTErrInfoSetF(pErrInfo, rc, "Failure to instantiate public key algorithm [IPRT]: %s (%s)",
                                       pSignerCert->TbsCertificate.SubjectPublicKeyInfo.Algorithm.Algorithm.szObjId,
                                       pSignerInfo->DigestEncryptionAlgorithm.Algorithm.szObjId);
            }
        }

        RTCrDigestRelease(hDigest);
    }
    else if (RT_SUCCESS(rc))
        rc = VERR_CR_PKCS7_INTERNAL_ERROR;
    RTCrCertCtxRelease(pSignerCertCtx);
    return rc;
}


/**
 * Verifies a counter signature.
 *
 * @returns IPRT status code.
 * @param   pCounterSignerInfo  The counter signature.
 * @param   pPrimarySignerInfo  The primary signature (can be a counter
 *                              signature too if nested).
 * @param   pSignedData         The SignedData.
 * @param   fFlags              Verification flags.
 * @param   hAdditionalCerts    Store containing optional certificates,
 *                              optional.
 * @param   hTrustedCerts       Store containing trusted certificates, required.
 * @param   pValidationTime     The time we're supposed to validate the
 *                              certificates chains at.
 * @param   pfnVerifyCert       Signing certificate verification callback.
 * @param   fVccFlags           Signing certificate verification callback flags.
 * @param   pvUser              Callback parameter.
 * @param   pErrInfo            Where to store additional error details,
 *                              optional.
 */
static int rtCrPkcs7VerifyCounterSignerInfo(PCRTCRPKCS7SIGNERINFO pCounterSignerInfo, PCRTCRPKCS7SIGNERINFO pPrimarySignerInfo,
                                            PCRTCRPKCS7SIGNEDDATA pSignedData, uint32_t fFlags,
                                            RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts, PCRTTIMESPEC pValidationTime,
                                            PFNRTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert, uint32_t fVccFlags,
                                            void *pvUser, PRTERRINFO pErrInfo)
{
    /*
     * Calculate the digest we need to verify.
     */
    RTCRDIGEST hDigest;
    int rc = RTCrDigestCreateByObjId(&hDigest, &pCounterSignerInfo->DigestAlgorithm.Algorithm);
    if (RT_SUCCESS(rc))
    {
        rc = RTCrDigestUpdate(hDigest,
                              pPrimarySignerInfo->EncryptedDigest.Asn1Core.uData.pv,
                              pPrimarySignerInfo->EncryptedDigest.Asn1Core.cb);
        if (RT_SUCCESS(rc))
            rc = RTCrDigestFinal(hDigest, NULL, 0);
        if (RT_SUCCESS(rc))
        {
            /*
             * Pass it on to the common SignerInfo verifier function.
             */
            rc = rtCrPkcs7VerifySignerInfo(pCounterSignerInfo, pSignedData, hDigest,
                                           fFlags | RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE,
                                           hAdditionalCerts, hTrustedCerts, pValidationTime,
                                           pfnVerifyCert, fVccFlags, pvUser, pErrInfo);

        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_CALC_ERROR,
                               "Hashing for counter signature failed unexpectedly: %Rrc", rc);
        RTCrDigestRelease(hDigest);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_CREATE_ERROR, "Error creating digest for '%s': %Rrc",
                           pCounterSignerInfo->DigestAlgorithm.Algorithm.szObjId, rc);

    return rc;
}


/**
 * Worker.
 */
static int rtCrPkcs7VerifySignedDataEx(PCRTCRPKCS7CONTENTINFO pContentInfo, uint32_t fFlags,
                                       RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                       PCRTTIMESPEC pValidationTime,
                                       PFNRTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert, void *pvUser,
                                       void const *pvContent, size_t cbContent, PRTERRINFO pErrInfo)
{
    /*
     * Check and adjust the input.
     */
    if (pfnVerifyCert)
        AssertPtrReturn(pfnVerifyCert, VERR_INVALID_POINTER);
    else
        pfnVerifyCert = RTCrPkcs7VerifyCertCallbackDefault;

    if (!RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_NOT_SIGNED_DATA, "Not PKCS #7 SignedData.");
    PCRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;
    int rc = RTCrPkcs7SignedData_CheckSanity(pSignedData, 0, pErrInfo, "");
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Hash the content info.
     */
    /* Check that there aren't too many or too few hash algorithms for our
       implementation and purposes. */
    RTCRDIGEST     ahDigests[2];
    uint32_t const cDigests = pSignedData->DigestAlgorithms.cItems;
    if (!cDigests) /** @todo we might have to support this... */
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_NO_DIGEST_ALGORITHMS, "No digest algorithms");

    if (cDigests > RT_ELEMENTS(ahDigests))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_TOO_MANY_DIGEST_ALGORITHMS,
                             "Too many digest algorithm: cAlgorithms=%u", cDigests);

    /* Create the message digest calculators. */
    rc = VERR_CR_PKCS7_NO_DIGEST_ALGORITHMS;
    uint32_t i;
    for (i = 0; i < cDigests; i++)
    {
        rc = RTCrDigestCreateByObjId(&ahDigests[i], &pSignedData->DigestAlgorithms.papItems[i]->Algorithm);
        if (RT_FAILURE(rc))
        {
            rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_CREATE_ERROR, "Error creating digest for '%s': %Rrc",
                               pSignedData->DigestAlgorithms.papItems[i]->Algorithm.szObjId, rc);
            break;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /* Hash the content. */
        for (i = 0; i < cDigests && RT_SUCCESS(rc); i++)
        {
            rc = RTCrDigestUpdate(ahDigests[i], pvContent, cbContent);
            if (RT_SUCCESS(rc))
                rc = RTCrDigestFinal(ahDigests[i], NULL, 0);
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Validate the signed infos.  The flags may select one particular entry.
             */
            RTTIMESPEC const GivenValidationTime = *pValidationTime;
            uint32_t fPrimaryVccFlags = !(fFlags & RTCRPKCS7VERIFY_SD_F_USAGE_TIMESTAMPING)
                                      ? RTCRPKCS7VCC_F_SIGNED_DATA : RTCRPKCS7VCC_F_TIMESTAMP;
            uint32_t cItems           = pSignedData->SignerInfos.cItems;
            i                         = 0;
            if (fFlags & RTCRPKCS7VERIFY_SD_F_HAS_SIGNER_INDEX)
            {
                i      = (fFlags & RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX_MASK) >> RTCRPKCS7VERIFY_SD_F_SIGNER_INDEX_SHIFT;
                cItems = RT_MIN(cItems, i + 1);
            }
            rc = VERR_CR_PKCS7_NO_SIGNER_INFOS;
            for (; i < cItems; i++)
            {
                PCRTCRPKCS7SIGNERINFO   pSignerInfo = pSignedData->SignerInfos.papItems[i];
                RTCRDIGEST              hThisDigest = NIL_RTCRDIGEST; /* (gcc maybe incredible stupid.) */
                rc = rtCrPkcs7VerifyFindDigest(&hThisDigest, pSignedData, pSignerInfo, ahDigests, pErrInfo);
                if (RT_FAILURE(rc))
                    break;

                /*
                 * See if we can find a trusted signing time.
                 * (Note that while it would make sense splitting up this function,
                 * we need to carry a lot of arguments around, so better not.)
                 */
                bool                    fDone              = false;
                PCRTCRPKCS7SIGNERINFO   pSigningTimeSigner = NULL;
                PCRTASN1TIME            pSignedTime;
                while (   !fDone
                       && (pSignedTime = RTCrPkcs7SignerInfo_GetSigningTime(pSignerInfo, &pSigningTimeSigner)) != NULL)
                {
                    RTTIMESPEC ThisValidationTime;
                    if (RT_LIKELY(RTTimeImplode(&ThisValidationTime, &pSignedTime->Time)))
                    {
                        if (pSigningTimeSigner == pSignerInfo)
                        {
                            if (fFlags & RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY)
                                continue;
                            rc = rtCrPkcs7VerifySignerInfo(pSignerInfo, pSignedData, hThisDigest, fFlags,
                                                           hAdditionalCerts, hTrustedCerts, &ThisValidationTime,
                                                           pfnVerifyCert, fPrimaryVccFlags | RTCRPKCS7VCC_F_TIMESTAMP,
                                                           pvUser, pErrInfo);
                        }
                        else
                        {
                            rc = VINF_SUCCESS;
                            if (!(fFlags & RTCRPKCS7VERIFY_SD_F_USE_SIGNING_TIME_UNVERIFIED))
                                rc = rtCrPkcs7VerifyCounterSignerInfo(pSigningTimeSigner, pSignerInfo, pSignedData,
                                                                      fFlags & ~RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME,
                                                                      hAdditionalCerts, hTrustedCerts, &ThisValidationTime,
                                                                      pfnVerifyCert, RTCRPKCS7VCC_F_TIMESTAMP, pvUser, pErrInfo);
                            if (RT_SUCCESS(rc))
                                rc = rtCrPkcs7VerifySignerInfo(pSignerInfo, pSignedData, hThisDigest, fFlags, hAdditionalCerts,
                                                               hTrustedCerts, &ThisValidationTime,
                                                               pfnVerifyCert, fPrimaryVccFlags, pvUser, pErrInfo);
                        }
                        fDone = RT_SUCCESS(rc)
                             || (fFlags & RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT);
                        if ((fFlags & RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME) && fDone)
                            *(PRTTIMESPEC)pValidationTime = ThisValidationTime;
                    }
                    else
                    {
                        rc = RTErrInfoSet(pErrInfo, VERR_INTERNAL_ERROR_3, "RTTimeImplode failed");
                        fDone = true;
                    }
                }

                /*
                 * If not luck, check for microsoft timestamp counter signatures.
                 */
                if (!fDone && !(fFlags & RTCRPKCS7VERIFY_SD_F_IGNORE_MS_TIMESTAMP))
                {
                    PCRTCRPKCS7CONTENTINFO pSignedTimestamp = NULL;
                    pSignedTime = RTCrPkcs7SignerInfo_GetMsTimestamp(pSignerInfo, &pSignedTimestamp);
                    if (pSignedTime)
                    {
                        RTTIMESPEC ThisValidationTime;
                        if (RT_LIKELY(RTTimeImplode(&ThisValidationTime, &pSignedTime->Time)))
                        {
                            rc = VINF_SUCCESS;
                            if (!(fFlags & RTCRPKCS7VERIFY_SD_F_USE_MS_TIMESTAMP_UNVERIFIED))
                                rc = RTCrPkcs7VerifySignedData(pSignedTimestamp,
                                                               fFlags | RTCRPKCS7VERIFY_SD_F_IGNORE_MS_TIMESTAMP
                                                               | RTCRPKCS7VERIFY_SD_F_USAGE_TIMESTAMPING,
                                                               hAdditionalCerts, hTrustedCerts, &ThisValidationTime,
                                                               pfnVerifyCert, pvUser, pErrInfo);

                            if (RT_SUCCESS(rc))
                                rc = rtCrPkcs7VerifySignerInfo(pSignerInfo, pSignedData, hThisDigest, fFlags, hAdditionalCerts,
                                                               hTrustedCerts, &ThisValidationTime,
                                                               pfnVerifyCert, fPrimaryVccFlags, pvUser, pErrInfo);
                            fDone = RT_SUCCESS(rc)
                                 || (fFlags & RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT);
                            if ((fFlags & RTCRPKCS7VERIFY_SD_F_UPDATE_VALIDATION_TIME) && fDone)
                                *(PRTTIMESPEC)pValidationTime = ThisValidationTime;
                        }
                        else
                        {
                            rc = RTErrInfoSet(pErrInfo, VERR_INTERNAL_ERROR_3, "RTTimeImplode failed");
                            fDone = true;
                        }

                    }
                }

                /*
                 * No valid signing time found, use the one specified instead.
                 */
                if (!fDone)
                    rc = rtCrPkcs7VerifySignerInfo(pSignerInfo, pSignedData, hThisDigest, fFlags, hAdditionalCerts, hTrustedCerts,
                                                   &GivenValidationTime, pfnVerifyCert, fPrimaryVccFlags, pvUser, pErrInfo);
                RTCrDigestRelease(hThisDigest);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_CALC_ERROR,
                               "Hashing content failed unexpectedly (i=%u): %Rrc", i, rc);

        /* Clean up digests. */
        i = cDigests;
    }
    while (i-- > 0)
    {
        int rc2 = RTCrDigestRelease(ahDigests[i]);
        AssertRC(rc2);
    }


#ifdef IPRT_WITH_OPENSSL
    /*
     * Verify using OpenSSL and combine the results (should be identical).
     */
    /** @todo figure out how to verify MS timstamp signatures using OpenSSL. */
    if (fFlags & RTCRPKCS7VERIFY_SD_F_USAGE_TIMESTAMPING)
        return rc;
    /** @todo figure out if we can verify just one signer info item using OpenSSL. */
    if (!(fFlags & RTCRPKCS7VERIFY_SD_F_HAS_SIGNER_INDEX) && pSignedData->SignerInfos.cItems > 1)
        return rc;

    int rcOssl = rtCrPkcs7VerifySignedDataUsingOpenSsl(pContentInfo, fFlags, hAdditionalCerts, hTrustedCerts,
                                                       pvContent, cbContent, RT_SUCCESS(rc) ? pErrInfo : NULL);
    if (RT_SUCCESS(rcOssl) && RT_SUCCESS(rc))
        return rc;
//    AssertMsg(RT_FAILURE_NP(rcOssl) && RT_FAILURE_NP(rc), ("%Rrc, %Rrc\n", rcOssl, rc));
    if (RT_FAILURE(rc))
        return rc;
    return rcOssl;
#else
    return rc;
#endif
}


RTDECL(int) RTCrPkcs7VerifySignedData(PCRTCRPKCS7CONTENTINFO pContentInfo, uint32_t fFlags,
                                      RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                      PCRTTIMESPEC pValidationTime, PFNRTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert, void *pvUser,
                                      PRTERRINFO pErrInfo)
{
    /*
     * Find the content and pass it on to common worker.
     */
    if (!RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_NOT_SIGNED_DATA, "Not PKCS #7 SignedData.");

    /* Exactly what the content is, is for some stupid reason unnecessarily complicated. */
    PCRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;
    void const *pvContent = pSignedData->ContentInfo.Content.Asn1Core.uData.pv;
    uint32_t    cbContent = pSignedData->ContentInfo.Content.Asn1Core.cb;
    if (pSignedData->ContentInfo.Content.pEncapsulated)
    {
        pvContent = pSignedData->ContentInfo.Content.pEncapsulated->uData.pv;
        cbContent = pSignedData->ContentInfo.Content.pEncapsulated->cb;
    }

    return rtCrPkcs7VerifySignedDataEx(pContentInfo, fFlags, hAdditionalCerts, hTrustedCerts, pValidationTime,
                                       pfnVerifyCert, pvUser, pvContent, cbContent, pErrInfo);
}


RTDECL(int) RTCrPkcs7VerifySignedDataWithExternalData(PCRTCRPKCS7CONTENTINFO pContentInfo, uint32_t fFlags,
                                                      RTCRSTORE hAdditionalCerts, RTCRSTORE hTrustedCerts,
                                                      PCRTTIMESPEC pValidationTime,
                                                      PFNRTCRPKCS7VERIFYCERTCALLBACK pfnVerifyCert, void *pvUser,
                                                      void const *pvData, size_t cbData, PRTERRINFO pErrInfo)
{
    /*
     * Require 'data' as inner content type.
     */
    if (!RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_NOT_SIGNED_DATA, "Not PKCS #7 SignedData.");
    PCRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;

    if (RTAsn1ObjId_CompareWithString(&pSignedData->ContentInfo.ContentType, RTCR_PKCS7_DATA_OID) != 0)
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_NOT_DATA,
                             "The signedData content type is %s, expected 'data' (%s)",
                             pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID);

    return rtCrPkcs7VerifySignedDataEx(pContentInfo, fFlags, hAdditionalCerts, hTrustedCerts, pValidationTime,
                                       pfnVerifyCert, pvUser, pvData, cbData, pErrInfo);
}

