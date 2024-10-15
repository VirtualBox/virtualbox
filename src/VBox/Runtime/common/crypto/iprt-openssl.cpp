/* $Id: iprt-openssl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - OpenSSL Helpers.
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

#ifdef IPRT_WITH_OPENSSL    /* Whole file. */
# include <iprt/err.h>
# include <iprt/file.h>
# include <iprt/string.h>
# include <iprt/mem.h>
# include <iprt/asn1.h>
# include <iprt/crypto/digest.h>
# include <iprt/crypto/pkcs7.h>
# include <iprt/crypto/spc.h>

# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/x509.h>
# include <openssl/err.h>
# include "internal/openssl-post.h"


DECLHIDDEN(void) rtCrOpenSslInit(void)
{
    static bool s_fOssInitalized;
    if (!s_fOssInitalized)
    {
        OpenSSL_add_all_algorithms();
        ERR_load_ERR_strings();
        ERR_load_crypto_strings();

        /* Add some OIDs we might possibly want to use. */
        static struct { const char *pszOid, *pszDesc; } const s_aOids[] =
        {
            { RTCRSPC_PE_IMAGE_HASHES_V1_OID,               "Ms-SpcPeImagePageHashesV1" },
            { RTCRSPC_PE_IMAGE_HASHES_V2_OID,               "Ms-SpcPeImagePageHashesV2" },
            { RTCRSPC_STMT_TYPE_INDIVIDUAL_CODE_SIGNING,    "Ms-SpcIndividualCodeSigning" },
            { RTCRSPCPEIMAGEDATA_OID,                       "Ms-SpcPeImageData" },
            { RTCRSPCINDIRECTDATACONTENT_OID,               "Ms-SpcIndirectDataContext" },
            { RTCR_PKCS9_ID_MS_TIMESTAMP,                   "Ms-CounterSign" },
            { RTCR_PKCS9_ID_MS_NESTED_SIGNATURE,            "Ms-SpcNestedSignature" },
            { RTCR_PKCS9_ID_MS_STATEMENT_TYPE,              "Ms-SpcStatementType" },
            { RTCR_PKCS9_ID_MS_SP_OPUS_INFO,                "Ms-SpcOpusInfo" },
            { "1.3.6.1.4.1.311.3.2.1",                      "Ms-SpcTimeStampRequest" },     /** @todo define */
            { "1.3.6.1.4.1.311.10.1",                       "Ms-CertTrustList" },           /** @todo define */
        };
        for (unsigned i = 0; i < RT_ELEMENTS(s_aOids); i++)
            OBJ_create(s_aOids[i].pszOid, s_aOids[i].pszDesc, s_aOids[i].pszDesc);

        s_fOssInitalized = true;
    }
}


DECLHIDDEN(int) rtCrOpenSslErrInfoCallback(const char *pach, size_t cch, void *pvUser)
{
    PRTERRINFO pErrInfo = (PRTERRINFO)pvUser;
    size_t cchAlready = pErrInfo->fFlags & RTERRINFO_FLAGS_SET ? strlen(pErrInfo->pszMsg) : 0;
    if (cchAlready + 1 < pErrInfo->cbMsg)
        RTStrCopyEx(pErrInfo->pszMsg + cchAlready, pErrInfo->cbMsg - cchAlready, pach, cch);
    return -1;
}


DECLHIDDEN(int) rtCrOpenSslConvertX509Cert(void **ppvOsslCert, PCRTCRX509CERTIFICATE pCert, PRTERRINFO pErrInfo)
{
    const unsigned char *pabEncoded;
    uint32_t             cbEncoded;
    void                *pvFree;
    int rc = RTAsn1EncodeQueryRawBits(RTCrX509Certificate_GetAsn1Core(pCert),
                                      (const uint8_t **)&pabEncoded, &cbEncoded, &pvFree, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        X509 *pOsslCert = NULL;
        X509 *pOsslCertRet = d2i_X509(&pOsslCert, &pabEncoded, cbEncoded);
        RTMemTmpFree(pvFree);
        if (pOsslCert != NULL && pOsslCertRet == pOsslCert)
        {
            *ppvOsslCert = pOsslCert;
            return VINF_SUCCESS;
        }
        rc = RTErrInfoSet(pErrInfo, VERR_CR_X509_OSSL_D2I_FAILED, "d2i_X509");

    }
    *ppvOsslCert = NULL;
    return rc;
}


DECLHIDDEN(void) rtCrOpenSslFreeConvertedX509Cert(void *pvOsslCert)
{
    X509_free((X509 *)pvOsslCert);
}


DECLHIDDEN(int) rtCrOpenSslAddX509CertToStack(void *pvOsslStack, PCRTCRX509CERTIFICATE pCert, PRTERRINFO pErrInfo)
{
    X509 *pOsslCert = NULL;
    int rc = rtCrOpenSslConvertX509Cert((void **)&pOsslCert, pCert, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (sk_X509_push((STACK_OF(X509) *)pvOsslStack, pOsslCert))
            rc = VINF_SUCCESS;
        else
        {
            rtCrOpenSslFreeConvertedX509Cert(pOsslCert);
            rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "sk_X509_push");
        }
    }
    return rc;
}


DECLHIDDEN(const void /*EVP_MD*/ *) rtCrOpenSslConvertDigestType(RTDIGESTTYPE enmDigestType, PRTERRINFO pErrInfo)
{
    const char *pszAlgoObjId = RTCrDigestTypeToAlgorithmOid(enmDigestType);
    AssertReturnStmt(pszAlgoObjId, RTErrInfoSetF(pErrInfo, VERR_INVALID_PARAMETER, "Invalid type: %d", enmDigestType), NULL);

    int iAlgoNid = OBJ_txt2nid(pszAlgoObjId);
    AssertReturnStmt(iAlgoNid != NID_undef,
                     RTErrInfoSetF(pErrInfo, VERR_CR_DIGEST_OSSL_DIGEST_INIT_ERROR,
                                   "OpenSSL does not know: %s (%s)", pszAlgoObjId, RTCrDigestTypeToName(enmDigestType)),
                     NULL);

    const char   *pszAlgoSn  = OBJ_nid2sn(iAlgoNid);
    const EVP_MD *pEvpMdType = EVP_get_digestbyname(pszAlgoSn);
    AssertReturnStmt(pEvpMdType,
                     RTErrInfoSetF(pErrInfo, VERR_CR_DIGEST_OSSL_DIGEST_INIT_ERROR, "OpenSSL/EVP does not know: %d (%s; %s; %s)",
                                   iAlgoNid, pszAlgoSn, pszAlgoSn, RTCrDigestTypeToName(enmDigestType)),
                     NULL);

    return pEvpMdType;
}


DECLHIDDEN(int) rtCrOpenSslConvertPkcs7Attribute(void **ppvOsslAttrib, PCRTCRPKCS7ATTRIBUTE pAttrib, PRTERRINFO pErrInfo)
{
    const unsigned char *pabEncoded = NULL;
    uint32_t             cbEncoded  = 0;
    void                *pvFree     = NULL;
    int rc = RTAsn1EncodeQueryRawBits(RTCrPkcs7Attribute_GetAsn1Core(pAttrib),
                                      (const uint8_t **)&pabEncoded, &cbEncoded, &pvFree, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        X509_ATTRIBUTE *pOsslAttrib = NULL;
        X509_ATTRIBUTE *pOsslAttribRet = d2i_X509_ATTRIBUTE(&pOsslAttrib, &pabEncoded, cbEncoded);
        RTMemTmpFree(pvFree);
        if (pOsslAttrib != NULL && pOsslAttribRet == pOsslAttrib)
        {
            *ppvOsslAttrib = pOsslAttrib;
            return VINF_SUCCESS;
        }
        rc = RTErrInfoSet(pErrInfo, VERR_CR_X509_OSSL_D2I_FAILED, "d2i_X509_ATTRIBUTE");
    }
    *ppvOsslAttrib = NULL;
    return rc;
}


DECLHIDDEN(void) rtCrOpenSslFreeConvertedPkcs7Attribute(void *pvOsslAttrib)
{
    X509_ATTRIBUTE_free((X509_ATTRIBUTE *)pvOsslAttrib);
}


/**
 * Writes the content of the @a pvMemBio to the new file @a pszFilename.
 *
 * @returns IPRT status code.
 * @param   pvMemBio    The memory BIO to write out.
 * @param   pszFilename The destination file.  This will be created.
 *                      The function will fail if this already exists.
 * @param   pErrInfo    Where to provide additional error details. Optional.
 */
DECLHIDDEN(int) rtCrOpenSslWriteMemBioToNewFile(void *pvMemBio, const char *pszFilename, PRTERRINFO pErrInfo)
{
    int rc;

    /* Get the BIO buffer pointer first. */
    BUF_MEM *pBioBuf = NULL;
    long rcOssl = BIO_get_mem_ptr((BIO *)pvMemBio, &pBioBuf);
    if (rcOssl > 0)
    {
        AssertPtr(pBioBuf);
        RTFILE hFile = NIL_RTFILE;
        rc = RTFileOpen(&hFile, pszFilename,
                        RTFILE_O_WRITE | RTFILE_O_DENY_ALL | RTFILE_O_CREATE | (0600 << RTFILE_O_CREATE_MODE_SHIFT));
        if (RT_SUCCESS(rc))
        {
            rc = RTFileWrite(hFile, pBioBuf->data, pBioBuf->length, NULL);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileClose(hFile);
                AssertRCStmt(rc, rc = RTErrInfoSetF(pErrInfo, rc, "RTFileClose failed on '%s'", pszFilename));
            }
            else
            {
                rc = RTErrInfoSetF(pErrInfo, rc, "RTFileWrite(,,%#zx,) failed on '%s'", pBioBuf->length, pszFilename);
                RTFileClose(hFile);
            }
            if (RT_FAILURE(rc))
                RTFileDelete(pszFilename);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "RTFileOpen failed on '%s'", pszFilename);
    }
    else
        rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "BIO_get_mem_ptr");
    return rc;
}


#endif /* IPRT_WITH_OPENSSL */

