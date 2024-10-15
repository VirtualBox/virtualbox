/* $Id: pkcs7-sanity.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS \#7, Sanity Checkers.
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

//#include <iprt/stream.h>

#include "pkcs7-internal.h"


static int rtCrPkcs7SignedData_CheckSanityExtra(PCRTCRPKCS7SIGNEDDATA pSignedData, uint32_t fFlags,
                                                PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    bool const fAuthenticode = RT_BOOL(fFlags & RTCRPKCS7SIGNEDDATA_SANITY_F_AUTHENTICODE);
    RT_NOREF_PV(fFlags);

    //RTAsn1Dump(&pSignedData->SeqCore.Asn1Core, 0, 0, RTAsn1DumpStrmPrintfV, g_pStdOut);

    if (   RTAsn1Integer_UnsignedCompareWithU32(&pSignedData->Version, RTCRPKCS7SIGNEDDATA_V1) != 0
        && RTAsn1Integer_UnsignedCompareWithU32(&pSignedData->Version, RTCRPKCS7SIGNEDDATA_V3) != 0
        && RTAsn1Integer_UnsignedCompareWithU32(&pSignedData->Version, RTCRPKCS7SIGNEDDATA_V4) != 0
        && RTAsn1Integer_UnsignedCompareWithU32(&pSignedData->Version, RTCRPKCS7SIGNEDDATA_V5) != 0)
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNED_DATA_VERSION, "SignedData version is %llu, expected %u",
                             pSignedData->Version.uValue.u, RTCRPKCS7SIGNEDDATA_V1);

    /*
     * DigestAlgorithms.
     */
    if (pSignedData->DigestAlgorithms.cItems == 0) /** @todo this might be too strict */
        return RTErrInfoSet(pErrInfo, VERR_CR_PKCS7_SIGNED_DATA_NO_DIGEST_ALGOS, "SignedData.DigestAlgorithms is empty");
    if (pSignedData->DigestAlgorithms.cItems != 1 && fAuthenticode)
        return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_NOT_EXACTLY_ONE_DIGEST_ALGO,
                             "%s: SignedData.DigestAlgorithms has more than one algorithm (%u)",
                             pszErrorTag, pSignedData->DigestAlgorithms.cItems);

    if (fFlags & RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH)
        for (uint32_t i = 0; i < pSignedData->DigestAlgorithms.cItems; i++)
        {
            if (   RTCrX509AlgorithmIdentifier_GetDigestType(pSignedData->DigestAlgorithms.papItems[i],
                                                             true /*fPureDigestsOnly*/)
                == RTDIGESTTYPE_INVALID)
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_UNKNOWN_DIGEST_ALGORITHM,
                                     "%s: SignedData.DigestAlgorithms[%i] is not known: %s",
                                     pszErrorTag, i, pSignedData->DigestAlgorithms.papItems[i]->Algorithm.szObjId);
            if (   pSignedData->DigestAlgorithms.papItems[i]->Parameters.enmType != RTASN1TYPE_NULL
                && pSignedData->DigestAlgorithms.papItems[i]->Parameters.enmType != RTASN1TYPE_NOT_PRESENT)
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_PARAMS_NOT_IMPL,
                                     "%s: SignedData.DigestAlgorithms[%i] has parameters: tag=%u",
                                     pszErrorTag, i, pSignedData->DigestAlgorithms.papItems[i]->Parameters.u.Core.uTag);
        }

    /*
     * Certificates.
     */
    if (   (fFlags & RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT)
        && pSignedData->Certificates.cItems == 0)
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_NO_CERTIFICATES,
                             "%s: SignedData.Certifcates is empty, expected at least one certificate", pszErrorTag);

    /*
     * Crls.
     */
    if (fAuthenticode && RTAsn1Core_IsPresent(&pSignedData->Crls))
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_EXPECTED_NO_CRLS,
                             "%s: SignedData.Crls is not empty as expected for authenticode.", pszErrorTag);
    /** @todo check Crls when they become important. */

    /*
     * SignerInfos.
     */
    if (pSignedData->SignerInfos.cItems == 0)
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_NO_SIGNER_INFOS, "%s: SignedData.SignerInfos is empty?", pszErrorTag);
    if (fAuthenticode && pSignedData->SignerInfos.cItems != 1)
        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_EXPECTED_ONE_SIGNER_INFO,
                             "%s: SignedData.SignerInfos should have one entry for authenticode: %u",
                             pszErrorTag, pSignedData->SignerInfos.cItems);

    for (uint32_t i = 0; i < pSignedData->SignerInfos.cItems; i++)
    {
        PCRTCRPKCS7SIGNERINFO pSignerInfo = pSignedData->SignerInfos.papItems[i];

        if (RTAsn1Integer_UnsignedCompareWithU32(&pSignerInfo->Version, RTCRPKCS7SIGNERINFO_V1) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNER_INFO_VERSION,
                                 "%s: SignedData.SignerInfos[%u] version is %llu, expected %u",
                                 pszErrorTag, i, pSignerInfo->Version.uValue.u, RTCRPKCS7SIGNERINFO_V1);

        /* IssuerAndSerialNumber. */
        int rc = RTCrX509Name_CheckSanity(&pSignerInfo->IssuerAndSerialNumber.Name, 0, pErrInfo,
                                          "SignedData.SignerInfos[#].IssuerAndSerialNumber.Name");
        if (RT_FAILURE(rc))
            return rc;

        if (pSignerInfo->IssuerAndSerialNumber.SerialNumber.Asn1Core.cb == 0)
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNER_INFO_NO_ISSUER_SERIAL_NO,
                                 "%s: SignedData.SignerInfos[%u].IssuerAndSerialNumber.SerialNumber is missing (zero length)",
                                 pszErrorTag, i);

        PCRTCRX509CERTIFICATE pCert;
        pCert = RTCrPkcs7SetOfCerts_FindX509ByIssuerAndSerialNumber(&pSignedData->Certificates,
                                                                    &pSignerInfo->IssuerAndSerialNumber.Name,
                                                                    &pSignerInfo->IssuerAndSerialNumber.SerialNumber);
        if (!pCert && (fFlags & RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT))
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNER_CERT_NOT_SHIPPED,
                                 "%s: SignedData.SignerInfos[%u].IssuerAndSerialNumber not found in T0.Certificates",
                                 pszErrorTag, i);

        /* DigestAlgorithm */
        uint32_t j = 0;
        while (   j < pSignedData->DigestAlgorithms.cItems
               && RTCrX509AlgorithmIdentifier_Compare(pSignedData->DigestAlgorithms.papItems[j],
                                                      &pSignerInfo->DigestAlgorithm) != 0)
            j++;
        if (j >= pSignedData->DigestAlgorithms.cItems)
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_DIGEST_ALGO_NOT_FOUND_IN_LIST,
                                 "%s: SignedData.SignerInfos[%u].DigestAlgorithm (%s) not found in SignedData.DigestAlgorithms",
                                 pszErrorTag, i, pSignerInfo->DigestAlgorithm.Algorithm.szObjId);

        /* Digest encryption algorithm. */
#if 0  /** @todo Unimportant: Seen timestamp signatures specifying pkcs1-Sha256WithRsaEncryption in SignerInfo and just RSA in the certificate.  Figure out how to compare the two. */
        if (   pCert
            && RTCrX509AlgorithmIdentifier_Compare(&pSignerInfo->DigestEncryptionAlgorithm,
                                                   &pCert->TbsCertificate.SubjectPublicKeyInfo.Algorithm) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_SIGNER_INFO_DIGEST_ENCRYPT_MISMATCH,
                                 "SignedData.SignerInfos[%u].DigestEncryptionAlgorithm (%s) mismatch with certificate (%s)",
                                 i, pSignerInfo->DigestEncryptionAlgorithm.Algorithm.szObjId,
                                 pCert->TbsCertificate.SubjectPublicKeyInfo.Algorithm.Algorithm.szObjId);
#endif

        /* Authenticated attributes we know. */
        if (RTCrPkcs7Attributes_IsPresent(&pSignerInfo->AuthenticatedAttributes))
        {
            bool fFoundContentInfo   = false;
            bool fFoundMessageDigest = false;
            for (j = 0; j < pSignerInfo->AuthenticatedAttributes.cItems; j++)
            {
                PCRTCRPKCS7ATTRIBUTE pAttrib = pSignerInfo->AuthenticatedAttributes.papItems[j];
                if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_CONTENT_TYPE_OID) == 0)
                {
                    if (fFoundContentInfo)
                        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MISSING_CONTENT_TYPE_ATTRIB,
                                             "%s: Multiple authenticated content-type attributes.", pszErrorTag);
                    fFoundContentInfo = true;
                    AssertReturn(pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS, VERR_INTERNAL_ERROR_3);
                    if (pAttrib->uValues.pObjIds->cItems != 1)
                        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_BAD_CONTENT_TYPE_ATTRIB,
                                             "%s: Expected exactly one value for content-type attrib, found: %u",
                                             pszErrorTag, pAttrib->uValues.pObjIds->cItems);
                }
                else if (RTAsn1ObjId_CompareWithString(&pAttrib->Type, RTCR_PKCS9_ID_MESSAGE_DIGEST_OID) == 0)
                {
                    if (fFoundMessageDigest)
                        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MISSING_MESSAGE_DIGEST_ATTRIB,
                                             "%s: Multiple authenticated message-digest attributes.", pszErrorTag);
                    fFoundMessageDigest = true;
                    AssertReturn(pAttrib->enmType == RTCRPKCS7ATTRIBUTETYPE_OCTET_STRINGS, VERR_INTERNAL_ERROR_3);
                    if (pAttrib->uValues.pOctetStrings->cItems != 1)
                        return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_BAD_CONTENT_TYPE_ATTRIB,
                                             "%s: Expected exactly one value for message-digest attrib, found: %u",
                                             pszErrorTag, pAttrib->uValues.pOctetStrings->cItems);
                }
            }

            if (!fFoundContentInfo)
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MISSING_CONTENT_TYPE_ATTRIB,
                                     "%s: Missing authenticated content-type attribute.", pszErrorTag);
            if (!fFoundMessageDigest)
                return RTErrInfoSetF(pErrInfo, VERR_CR_PKCS7_MISSING_MESSAGE_DIGEST_ATTRIB,
                                     "%s: Missing authenticated message-digest attribute.", pszErrorTag);
        }
    }

    return VINF_SUCCESS;
}


/*
 * Generate the code.
 */
#include <iprt/asn1-generator-sanity.h>

