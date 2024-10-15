/* $Id: x509-create-sign.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - X.509, Certificate Creation.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/crypto/x509.h>

#ifdef IPRT_WITH_OPENSSL
# include <iprt/err.h>
# include <iprt/file.h>
# include <iprt/rand.h>

# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include <openssl/pem.h>
# include <openssl/x509.h>
# include <openssl/bio.h>
# include "internal/openssl-post.h"

# if OPENSSL_VERSION_NUMBER < 0x1010000f
#  define BIO_s_secmem      BIO_s_mem
# endif



RTDECL(int) RTCrX509Certificate_GenerateSelfSignedRsa(RTDIGESTTYPE enmDigestType, uint32_t cBits, uint32_t cSecsValidFor,
                                                      uint32_t fKeyUsage, uint64_t fExtKeyUsage, const char *pvSubject,
                                                      const char *pszCertFile, const char *pszPrivateKeyFile, PRTERRINFO pErrInfo)
{
    AssertReturn(cSecsValidFor <= (uint32_t)INT32_MAX, VERR_OUT_OF_RANGE); /* larger values are not portable (win) */
    AssertReturn(!fKeyUsage, VERR_NOT_IMPLEMENTED);
    AssertReturn(!fExtKeyUsage, VERR_NOT_IMPLEMENTED);

    /*
     * Translate enmDigestType.
     */
    const EVP_MD * const pEvpDigest = (const EVP_MD *)rtCrOpenSslConvertDigestType(enmDigestType, pErrInfo);
    AssertReturn(pEvpDigest, pErrInfo ? pErrInfo->rc : VERR_CR_DIGEST_NOT_SUPPORTED);

    /*
     * Create a new RSA private key.
     */
# if OPENSSL_VERSION_NUMBER >= 0x30000000 /* RSA_generate_key is depreated in v3 */
    EVP_PKEY * const pPrivateKey = EVP_RSA_gen(cBits);
    if (!pPrivateKey)
        return RTErrInfoSetF(pErrInfo, VERR_CR_KEY_GEN_FAILED_RSA, "EVP_RSA_gen(%u) failed", cBits);
# else
    RSA * const pRsaKey = RSA_generate_key(
        cBits,  /* Number of bits for the key */
        RSA_F4, /* Exponent - RSA_F4 is defined as 0x10001L */
        NULL,   /* Callback */
        NULL    /* Callback argument */
    );
    if (!pRsaKey)
        return RTErrInfoSetF(pErrInfo, VERR_CR_KEY_GEN_FAILED_RSA, "RSA_generate_key(%u,RSA_F4,,) failed", cBits);

    EVP_PKEY * const pPrivateKey = EVP_PKEY_new();
    if (pPrivateKey)
        EVP_PKEY_assign_RSA(pPrivateKey, pRsaKey); /* Takes ownership of pRsaKey. */
    else
    {
        RSA_free(pRsaKey);
        return RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "EVP_PKEY_new failed");
    }
# endif

    /*
     * Construct the certificate.
     */
    int   rc = VINF_SUCCESS;
    X509 *pNewCert = X509_new();
    if (pNewCert)
    {
        int rcOssl;

        /* Set to X509 version 1: */
# if 0
        if (fKeyUsage || fExtKeyUsage)
            rcOssl = X509_set_version(pNewCert, RTCRX509TBSCERTIFICATE_V3);
        else
# endif
            rcOssl = X509_set_version(pNewCert, RTCRX509TBSCERTIFICATE_V1);
        AssertStmt(rcOssl > 0, rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_set_version failed"));

        /* Set the serial number to a random number in the 1 - 1G range: */
        rcOssl = ASN1_INTEGER_set(X509_get_serialNumber(pNewCert), RTRandU32Ex(1, UINT32_MAX / 4));
        AssertStmt(rcOssl > 0, rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_set_version failed"));

        /* The certificate is valid from now and the specifice number of seconds forwards: */
# if OPENSSL_VERSION_NUMBER >= 0x1010000f
        AssertStmt(X509_gmtime_adj(X509_getm_notBefore(pNewCert), 0),
                   rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_gmtime_adj/before failed"));
        AssertStmt(X509_gmtime_adj(X509_getm_notAfter(pNewCert), cSecsValidFor),
                   rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_gmtime_adj/after failed"));
# else
        AssertStmt(X509_gmtime_adj(X509_get_notBefore(pNewCert), 0),
                   rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_gmtime_adj/before failed"));
        AssertStmt(X509_gmtime_adj(X509_get_notAfter(pNewCert), cSecsValidFor),
                   rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_gmtime_adj/after failed"));
# endif

        /* Set the public key (part of the private): */
        rcOssl = X509_set_pubkey(pNewCert, pPrivateKey);
        AssertStmt(rcOssl > 0, rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_set_pubkey failed"));

# if 0
        /* Set key usage. */
        if (fKeyUsage)
        {
        }
        /* Set extended key usage. */
        if (fExtKeyUsage)
        {
        }
# endif
        /** @todo set other certificate attributes? */


        /** @todo check what the subject name is...  Offer way to specify it? */

        /* Make it self signed: */
        X509_NAME *pX509Name = X509_get_subject_name(pNewCert);
        rcOssl = X509_NAME_add_entry_by_txt(pX509Name, "CN", MBSTRING_ASC, (unsigned char *) pvSubject, -1, -1, 0);
        AssertStmt(rcOssl > 0, rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_NAME_add_entry_by_txt failed"));
        rcOssl = X509_set_issuer_name(pNewCert, pX509Name);
        AssertStmt(rcOssl > 0, rc = RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "X509_set_issuer_name failed"));

        if (RT_SUCCESS(rc))
        {
            /*
             * Sign the certificate.
             */
            rcOssl = X509_sign(pNewCert, pPrivateKey, pEvpDigest);
            if (rcOssl > 0)
            {
                /*
                 * Write out the result to the two files.
                 */
                /* The certificate (not security sensitive). */
                BIO * const pCertBio = BIO_new(BIO_s_mem());
                if (pCertBio)
                {
                    rcOssl = PEM_write_bio_X509(pCertBio, pNewCert);
                    if (rcOssl > 0)
                        rc = rtCrOpenSslWriteMemBioToNewFile(pCertBio, pszCertFile, pErrInfo);
                    else
                        rc = RTErrInfoSet(pErrInfo, VERR_CR_KEY_GEN_FAILED_RSA, "PEM_write_bio_X509 failed");
                    BIO_free(pCertBio);
                }
                else
                    rc = VERR_NO_MEMORY;

                if (RT_SUCCESS(rc))
                {
                    /* The private key as plain text (security sensitive, thus last). */
                    BIO * const pPkBio = BIO_new(BIO_s_secmem());
                    if (pPkBio)
                    {
                        rcOssl = PEM_write_bio_PrivateKey(pPkBio, pPrivateKey,
                                                          NULL /*enc*/, NULL /*kstr*/, 0 /*klen*/, NULL /*cb*/, NULL /*u*/);
                        if (rcOssl > 0)
                            rc = rtCrOpenSslWriteMemBioToNewFile(pPkBio, pszPrivateKeyFile, pErrInfo);
                        else
                            rc = RTErrInfoSet(pErrInfo, VERR_CR_KEY_GEN_FAILED_RSA, "PEM_write_bio_PrivateKey failed");
                        BIO_free(pPkBio);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                    if (RT_FAILURE(rc))
                        RTFileDelete(pszCertFile);
                }
            }
            else
                rc = RTErrInfoSet(pErrInfo, VERR_CR_KEY_GEN_FAILED_RSA, "X509_sign failed");
        }

        X509_free(pNewCert);
    }
    else
        rc = RTErrInfoSet(pErrInfo, VERR_NO_MEMORY, "X509_new failed");

    EVP_PKEY_free(pPrivateKey);
    return rc;
}

#endif /* IPRT_WITH_OPENSSL */
