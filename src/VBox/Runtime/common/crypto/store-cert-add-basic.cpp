/* $Id: store-cert-add-basic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Cryptographic (Certificate) Store, RTCrStoreCertAddFromDir.
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
#include <iprt/crypto/store.h>

#include <iprt/assert.h>
#include <iprt/crypto/pem.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/sha.h>
#include <iprt/string.h>

#include "x509-internal.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** BEGIN CERTIFICATE / END CERTIFICATE. */
static RTCRPEMMARKERWORD const g_aWords_Certificate[] =
{
    { RT_STR_TUPLE("CERTIFICATE") }
};

/** BEGIN TRUSTED CERTIFICATE / END TRUSTED CERTIFICATE. */
static RTCRPEMMARKERWORD const g_aWords_TrustedCertificate[] =
{
    { RT_STR_TUPLE("TRUSTED") },
    { RT_STR_TUPLE("CERTIFICATE") }
};

/** BEGIN X509 CERTIFICATE / END X509 CERTIFICATE. (old) */
static RTCRPEMMARKERWORD const g_aWords_X509Certificate[] =
{
    { RT_STR_TUPLE("X509") },
    { RT_STR_TUPLE("CERTIFICATE") }
};

/**
 * X509 Certificate markers.
 *
 * @remark See crypto/pem/pem.h in OpenSSL for a matching list.
 */
static RTCRPEMMARKER const g_aX509CertificateMarkers[3] =
{
    { g_aWords_Certificate,         RT_ELEMENTS(g_aWords_Certificate) },
    { g_aWords_TrustedCertificate,  RT_ELEMENTS(g_aWords_TrustedCertificate) },
    { g_aWords_X509Certificate,     RT_ELEMENTS(g_aWords_X509Certificate) }
};



#ifdef RT_STRICT
/**
 * Checks if we've found all the certificates already.
 *
 * @returns true if all found, false if not.
 * @param   afFound             Indicator array.
 * @param   cWanted             Number of wanted certificates.
 */
DECLINLINE(bool) rtCrStoreAllDone(bool const *afFound, size_t cWanted)
{
    while (cWanted-- > 0)
        if (!afFound[cWanted])
            return false;
    return true;
}
#endif /* RT_STRICT */


/**
 * Checks if the given certificate specs matches the given wanted poster.
 *
 * @returns true if match, false if not.
 * @param   pWanted     The certificate wanted poster.
 * @param   cbEncoded   The candidate certificate encoded size.
 * @param   paSha1      The candidate certificate SHA-1 fingerprint.
 * @param   paSha512    The candidate certificate SHA-512 fingerprint.
 * @param   pCert       The decoded candidate certificate, optional.  If not
 *                      given the result will be uncertain.
 */
DECLINLINE(bool) rtCrStoreIsCertEqualToWanted(PCRTCRCERTWANTED pWanted,
                                              size_t cbEncoded,
                                              uint8_t const pabSha1[RTSHA1_HASH_SIZE],
                                              uint8_t const pabSha512[RTSHA512_HASH_SIZE],
                                              PCRTCRX509CERTIFICATE pCert)
{
    if (   pWanted->cbEncoded != cbEncoded
        && pWanted->cbEncoded != 0)
        return false;

    if (   pWanted->fSha1Fingerprint
        && memcmp(pWanted->abSha1, pabSha1, RTSHA1_HASH_SIZE) != 0)
        return false;

    if (   pWanted->fSha512Fingerprint
        && memcmp(pWanted->abSha512, pabSha512, RTSHA512_HASH_SIZE) != 0)
        return false;

    if (   pWanted->pszSubject
        && pCert
        && !RTCrX509Name_MatchWithString(&pCert->TbsCertificate.Subject, pWanted->pszSubject))
        return false;

    return true;
}


/**
 * Checks if a certificate is wanted.
 *
 * @returns true if match, false if not.
 * @param   paWanted    The certificate wanted posters.
 * @param   cWanted     The number of wanted posters.
 * @param   apfFound    Found initicators running paralell to @a paWanted.
 * @param   cbEncoded   The candidate certificate encoded size.
 * @param   paSha1      The candidate certificate SHA-1 fingerprint.
 * @param   paSha512    The candidate certificate SHA-512 fingerprint.
 * @param   pCert       The decoded candidate certificate, optional.  If not
 *                      given the result will be uncertain.
 */
DECLINLINE(bool) rtCrStoreIsCertWanted(PCRTCRCERTWANTED paWanted, size_t cWanted, bool const *pafFound, size_t cbEncoded,
                                       uint8_t const pabSha1[RTSHA1_HASH_SIZE], uint8_t const pabSha512[RTSHA512_HASH_SIZE],
                                       PCRTCRX509CERTIFICATE pCert)
{
    for (size_t iCert = 0; iCert < cWanted; iCert++)
        if (!pafFound[iCert])
            if (rtCrStoreIsCertEqualToWanted(&paWanted[iCert], cbEncoded, pabSha1, pabSha512, pCert))
                return true;
    return false;
}


/**
 * Marks a certificate as found after it has been added to the store.
 *
 * May actually mark several certificates as found if there are duplicates or
 * ambiguities in the wanted list.
 *
 * @returns true if all have been found, false if more to search for.
 *
 * @param   apfFound    Found initicators running paralell to @a paWanted.
 *                      This is what this function updates.
 * @param   paWanted    The certificate wanted posters.
 * @param   cWanted     The number of wanted posters.
 * @param   cbEncoded   The candidate certificate encoded size.
 * @param   paSha1      The candidate certificate SHA-1 fingerprint.
 * @param   paSha512    The candidate certificate SHA-512 fingerprint.
 * @param   pCert       The decoded candidate certificate, optional.  If not
 *                      given the result will be uncertain.
 */
static bool rtCrStoreMarkCertFound(bool *pafFound, PCRTCRCERTWANTED paWanted, size_t cWanted, size_t cbEncoded,
                                   uint8_t const pabSha1[RTSHA1_HASH_SIZE], uint8_t const pabSha512[RTSHA512_HASH_SIZE],
                                   PCRTCRX509CERTIFICATE pCert)
{
    size_t cFound = 0;
    for (size_t iCert = 0; iCert < cWanted; iCert++)
        if (pafFound[iCert])
            cFound++;
        else if (rtCrStoreIsCertEqualToWanted(&paWanted[iCert], cbEncoded, pabSha1, pabSha512, pCert))
        {
            pafFound[iCert] = true;
            cFound++;
        }
    return cFound == cWanted;
}


RTDECL(int) RTCrStoreCertAddFromStore(RTCRSTORE hStore, uint32_t fFlags, RTCRSTORE hStoreSrc)
{
    /*
     * Validate input.
     */
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);

    /*
     * Enumerate all the certificates in the source store, adding them to the destination.
     */
    RTCRSTORECERTSEARCH Search;
    int rc = RTCrStoreCertFindAll(hStoreSrc, &Search);
    if (RT_SUCCESS(rc))
    {
        PCRTCRCERTCTX pCertCtx;
        while ((pCertCtx = RTCrStoreCertSearchNext(hStoreSrc, &Search)) != NULL)
        {
            int rc2 = RTCrStoreCertAddEncoded(hStore, pCertCtx->fFlags | (fFlags & RTCRCERTCTX_F_ADD_IF_NOT_FOUND),
                                              pCertCtx->pabEncoded, pCertCtx->cbEncoded, NULL);
            if (RT_FAILURE(rc2))
            {
                rc = rc2;
                if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                    break;
            }
            RTCrCertCtxRelease(pCertCtx);
        }

        int rc2 = RTCrStoreCertSearchDestroy(hStoreSrc, &Search); AssertRC(rc2);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddFromStore);


RTDECL(int) RTCrStoreCertAddWantedFromStore(RTCRSTORE hStore, uint32_t fFlags, RTCRSTORE hSrcStore,
                                            PCRTCRCERTWANTED paWanted, size_t cWanted, bool *pafFound)
{
    /*
     * Validate input a little.
     */
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);
    fFlags |= RTCRCERTCTX_F_ADD_IF_NOT_FOUND; /* forced */

    AssertReturn(cWanted, VERR_NOT_FOUND);
    for (uint32_t i = 0; i < cWanted; i++)
    {
        AssertReturn(!paWanted[i].pszSubject || *paWanted[i].pszSubject, VERR_INVALID_PARAMETER);
        AssertReturn(   paWanted[i].pszSubject
                     || paWanted[i].fSha1Fingerprint
                     || paWanted[i].fSha512Fingerprint,
                     VERR_INVALID_PARAMETER);
    }

    /*
     * Make sure we've got a result array.
     */
    bool *pafFoundFree = NULL;
    if (!pafFound)
    {
        pafFound = pafFoundFree = (bool *)RTMemTmpAllocZ(sizeof(bool) * cWanted);
        AssertReturn(pafFound, VERR_NO_TMP_MEMORY);
    }

    /*
     * Enumerate the store entries.
     */
    RTCRSTORECERTSEARCH Search;
    int rc = RTCrStoreCertFindAll(hSrcStore, &Search);
    if (RT_SUCCESS(rc))
    {
        rc = VWRN_NOT_FOUND;
        PCRTCRCERTCTX pCertCtx;
        while ((pCertCtx = RTCrStoreCertSearchNext(hSrcStore, &Search)) != NULL)
        {
            if (   (pCertCtx->fFlags & RTCRCERTCTX_F_ENC_MASK) == RTCRCERTCTX_F_ENC_X509_DER
                && pCertCtx->cbEncoded > 0
                && pCertCtx->pCert)
            {
                /*
                 * If the certificate is wanted, try add it to the store.
                 */
                uint8_t abSha1[RTSHA1_HASH_SIZE];
                RTSha1(pCertCtx->pabEncoded, pCertCtx->cbEncoded, abSha1);
                uint8_t abSha512[RTSHA512_HASH_SIZE];
                RTSha512(pCertCtx->pabEncoded, pCertCtx->cbEncoded, abSha512);
                if (rtCrStoreIsCertWanted(paWanted, cWanted, pafFound, pCertCtx->cbEncoded, abSha1, abSha512, pCertCtx->pCert))
                {
                    int rc2 = RTCrStoreCertAddEncoded(hStore,
                                                      RTCRCERTCTX_F_ENC_X509_DER | (fFlags & RTCRCERTCTX_F_ADD_IF_NOT_FOUND),
                                                      pCertCtx->pabEncoded, pCertCtx->cbEncoded, NULL /*pErrInfo*/);
                    if (RT_SUCCESS(rc2))
                    {
                        /*
                         * Mark it as found, stop if we've found all.
                         */
                        if (rtCrStoreMarkCertFound(pafFound, paWanted, cWanted,
                                                   pCertCtx->cbEncoded, abSha1, abSha512, pCertCtx->pCert))
                        {
                            if (RT_SUCCESS(rc))
                                rc = VINF_SUCCESS;
                            RTCrCertCtxRelease(pCertCtx);
                            break;
                        }
                    }
                    else
                    {
                        /*
                         * Some error adding the certificate.  Since it cannot be anything with
                         * the encoding, it must be something with the store or resources, so
                         * always return the error status.
                         */
                        rc = rc2;
                        if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                        {
                            RTCrCertCtxRelease(pCertCtx);
                            break;
                        }
                    }
                }
            }
            RTCrCertCtxRelease(pCertCtx);
        }
        int rc2 = RTCrStoreCertSearchDestroy(hSrcStore, &Search);
        AssertRC(rc2);
    }

    if (pafFoundFree)
        RTMemTmpFree(pafFoundFree);
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddWantedFromStore);


RTDECL(int) RTCrStoreCertCheckWanted(RTCRSTORE hStore, PCRTCRCERTWANTED paWanted, size_t cWanted, bool *pafFound)
{
    /*
     * Validate input a little.
     */
    AssertReturn(cWanted, VERR_NOT_FOUND);
    for (uint32_t i = 0; i < cWanted; i++)
    {
        AssertReturn(!paWanted[i].pszSubject || *paWanted[i].pszSubject, VERR_INVALID_PARAMETER);
        AssertReturn(   paWanted[i].pszSubject
                     || paWanted[i].fSha1Fingerprint
                     || paWanted[i].fSha512Fingerprint,
                     VERR_INVALID_PARAMETER);
    }
    AssertPtrReturn(pafFound, VERR_INVALID_POINTER);

    /*
     * Clear the found array.
     */
    for (uint32_t iCert = 0; iCert < cWanted; iCert++)
        pafFound[iCert] = false;

    /*
     * Enumerate the store entries.
     */
    RTCRSTORECERTSEARCH Search;
    int rc = RTCrStoreCertFindAll(hStore, &Search);
    if (RT_SUCCESS(rc))
    {
        rc = VWRN_NOT_FOUND;
        PCRTCRCERTCTX pCertCtx;
        while ((pCertCtx = RTCrStoreCertSearchNext(hStore, &Search)) != NULL)
        {
            if (   (pCertCtx->fFlags & RTCRCERTCTX_F_ENC_MASK) == RTCRCERTCTX_F_ENC_X509_DER
                && pCertCtx->cbEncoded > 0
                && pCertCtx->pCert)
            {
                /*
                 * Hash it and check if it's wanted.  Stop when we've found all.
                 */
                uint8_t abSha1[RTSHA1_HASH_SIZE];
                RTSha1(pCertCtx->pabEncoded, pCertCtx->cbEncoded, abSha1);
                uint8_t abSha512[RTSHA512_HASH_SIZE];
                RTSha512(pCertCtx->pabEncoded, pCertCtx->cbEncoded, abSha512);
                if (rtCrStoreMarkCertFound(pafFound, paWanted, cWanted, pCertCtx->cbEncoded, abSha1, abSha512, pCertCtx->pCert))
                {
                    rc = VINF_SUCCESS;
                    RTCrCertCtxRelease(pCertCtx);
                    break;
                }
            }
            RTCrCertCtxRelease(pCertCtx);
        }
        int rc2 = RTCrStoreCertSearchDestroy(hStore, &Search);
        AssertRC(rc2);
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddWantedFromStore);


RTDECL(int) RTCrStoreCertAddFromFile(RTCRSTORE hStore, uint32_t fFlags, const char *pszFilename, PRTERRINFO pErrInfo)
{
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);

    size_t      cbContent;
    void        *pvContent;
    int rc = RTFileReadAllEx(pszFilename, 0, 64U*_1M, RTFILE_RDALL_O_DENY_WRITE, &pvContent, &cbContent);
    if (RT_SUCCESS(rc))
    {
        /*
         * Is it a java key store file?
         */
        if (   cbContent > 32
            && ((uint32_t const *)pvContent)[0] == RT_H2BE_U32_C(UINT32_C(0xfeedfeed)) /* magic */
            && ((uint32_t const *)pvContent)[1] == RT_H2BE_U32_C(UINT32_C(0x00000002)) /* version */ )
            rc = RTCrStoreCertAddFromJavaKeyStoreInMem(hStore, fFlags, pvContent, cbContent, pszFilename, pErrInfo);
        /*
         * No assume PEM or DER encoded binary certificate.
         */
        else if (cbContent)
        {
            PCRTCRPEMSECTION pSectionHead;
            rc = RTCrPemParseContent(pvContent, cbContent,
                                     (fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)
                                     ? RTCRPEMREADFILE_F_CONTINUE_ON_ENCODING_ERROR : 0,
                                     g_aX509CertificateMarkers, RT_ELEMENTS(g_aX509CertificateMarkers),
                                     &pSectionHead, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                PCRTCRPEMSECTION pCurSec = pSectionHead;
                while (pCurSec)
                {
                    int rc2 = RTCrStoreCertAddEncoded(hStore,
                                                      RTCRCERTCTX_F_ENC_X509_DER | (fFlags & RTCRCERTCTX_F_ADD_IF_NOT_FOUND),
                                                      pCurSec->pbData, pCurSec->cbData,
                                                      !RTErrInfoIsSet(pErrInfo) ? pErrInfo : NULL);
                    if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                    {
                        rc = rc2;
                        if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                            break;
                    }
                    pCurSec = pCurSec->pNext;
                }

                RTCrPemFreeSections(pSectionHead);
            }
        }
        else /* Will happen if proxy not set / no connection available. */
            rc = RTErrInfoSetF(pErrInfo, VERR_EOF, "Certificate '%s' is empty", pszFilename);
        RTFileReadAllFree(pvContent, cbContent);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, rc, "RTFileReadAllEx failed with %Rrc on '%s'", rc, pszFilename);
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddFromFile);


RTDECL(int) RTCrStoreCertAddWantedFromFile(RTCRSTORE hStore, uint32_t fFlags, const char *pszFilename,
                                           PCRTCRCERTWANTED paWanted, size_t cWanted, bool *pafFound, PRTERRINFO pErrInfo)
{
    /*
     * Validate input a little.
     */
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);
    fFlags |= RTCRCERTCTX_F_ADD_IF_NOT_FOUND; /* forced */

    AssertReturn(cWanted, VERR_NOT_FOUND);
    for (uint32_t i = 0; i < cWanted; i++)
    {
        AssertReturn(!paWanted[i].pszSubject || *paWanted[i].pszSubject, VERR_INVALID_PARAMETER);
        AssertReturn(   paWanted[i].pszSubject
                     || paWanted[i].fSha1Fingerprint
                     || paWanted[i].fSha512Fingerprint,
                     VERR_INVALID_PARAMETER);
    }

    /*
     * Make sure we've got a result array.
     */
    bool *pafFoundFree = NULL;
    if (!pafFound)
    {
        pafFound = pafFoundFree = (bool *)RTMemTmpAllocZ(sizeof(bool) * cWanted);
        AssertReturn(pafFound, VERR_NO_TMP_MEMORY);
    }

    size_t cbContent;
    void  *pvContent;
    int rc = RTFileReadAllEx(pszFilename, 0, 64U*_1M, RTFILE_RDALL_O_DENY_WRITE, &pvContent, &cbContent);
    if (RT_SUCCESS(rc))
    {
        /*
         * Is it a java key store file?   If so, load it into a tmp store
         * which we can search.  Don't want to duplicate the JKS reader code.
         */
        if (   cbContent > 32
            && ((uint32_t const *)pvContent)[0] == RT_H2BE_U32_C(UINT32_C(0xfeedfeed)) /* magic */
            && ((uint32_t const *)pvContent)[1] == RT_H2BE_U32_C(UINT32_C(0x00000002)) /* version */ )
        {
            RTCRSTORE hTmpStore;
            rc = RTCrStoreCreateInMem(&hTmpStore, 64);
            if (RT_SUCCESS(rc))
            {
                rc = RTCrStoreCertAddFromJavaKeyStoreInMem(hStore, fFlags, pvContent, cbContent, pszFilename, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = RTCrStoreCertAddWantedFromStore(hStore, fFlags, hTmpStore, paWanted, cWanted, pafFound);
                RTCrStoreRelease(hTmpStore);
            }
            else
                rc = RTErrInfoSet(pErrInfo, rc, "Error creating temporary crypto store");
        }
        /*
         * No assume PEM or DER encoded binary certificate.  Inspect them one by one.
         */
        else if (cbContent)
        {
            PCRTCRPEMSECTION pSectionHead;
            rc = RTCrPemParseContent(pvContent, cbContent,
                                     (fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)
                                     ? RTCRPEMREADFILE_F_CONTINUE_ON_ENCODING_ERROR : 0,
                                     g_aX509CertificateMarkers, RT_ELEMENTS(g_aX509CertificateMarkers),
                                     &pSectionHead, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                rc = VWRN_NOT_FOUND;
                for (PCRTCRPEMSECTION pCurSec = pSectionHead; pCurSec; pCurSec = pCurSec->pNext)
                {
                    if (!pCurSec->cbData)
                        continue;

                    /*
                     * See if this is a binary blob we might be interested in.
                     */
                    uint8_t abSha1[RTSHA1_HASH_SIZE];
                    RTSha1(pCurSec->pbData, pCurSec->cbData, abSha1);
                    uint8_t abSha512[RTSHA512_HASH_SIZE];
                    RTSha512(pCurSec->pbData, pCurSec->cbData, abSha512);
                    if (!rtCrStoreIsCertWanted(paWanted, cWanted, pafFound, pCurSec->cbData, abSha1, abSha512, NULL))
                        continue;

                    /*
                     * Decode the certificate so we can match the subject string.
                     */
                    RTASN1CURSORPRIMARY Cursor;
                    RTAsn1CursorInitPrimary(&Cursor, pCurSec->pbData, (uint32_t)pCurSec->cbData,
                                            !RTErrInfoIsSet(pErrInfo) ? pErrInfo : NULL,
                                            &g_RTAsn1DefaultAllocator, RTASN1CURSOR_FLAGS_DER, "InMem");
                    RTCRX509CERTIFICATE X509Cert;
                    int rc2 = RTCrX509Certificate_DecodeAsn1(&Cursor.Cursor, 0, &X509Cert, "Cert");
                    if (RT_SUCCESS(rc2))
                    {
                        rc2 = RTCrX509Certificate_CheckSanity(&X509Cert, 0, !RTErrInfoIsSet(pErrInfo) ? pErrInfo : NULL, "Cert");
                        if (RT_SUCCESS(rc2))
                        {
                            if (rtCrStoreIsCertWanted(paWanted, cWanted, pafFound, pCurSec->cbData, abSha1, abSha512, &X509Cert))
                            {
                                /*
                                 * The certificate is wanted, now add it to the store.
                                 */
                                rc2 = RTCrStoreCertAddEncoded(hStore,
                                                              RTCRCERTCTX_F_ENC_X509_DER
                                                              | (fFlags & RTCRCERTCTX_F_ADD_IF_NOT_FOUND),
                                                              pCurSec->pbData, pCurSec->cbData,
                                                              !RTErrInfoIsSet(pErrInfo) ? pErrInfo : NULL);
                                if (RT_SUCCESS(rc2))
                                {
                                    /*
                                     * Mark it as found, stop if we've found all.
                                     */
                                    if (rtCrStoreMarkCertFound(pafFound, paWanted, cWanted,
                                                               pCurSec->cbData, abSha1, abSha512, &X509Cert))
                                    {
                                        RTAsn1VtDelete(&X509Cert.SeqCore.Asn1Core);
                                        rc = VINF_SUCCESS;
                                        break;
                                    }
                                }
                            }
                        }
                        else
                            Assert(!pErrInfo || RTErrInfoIsSet(pErrInfo));
                        RTAsn1VtDelete(&X509Cert.SeqCore.Asn1Core);
                    }
                    else if (!RTErrInfoIsSet(pErrInfo))
                        RTErrInfoSetF(pErrInfo, rc2, "RTCrX509Certificate_DecodeAsn1 failed");

                    /*
                     * Stop on error, if requested.  Otherwise, let pErrInfo keep it.
                     */
                    if (RT_FAILURE(rc2) && !(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                    {
                        rc = rc2;
                        break;
                    }
                } /* For each PEM section. */

                RTCrPemFreeSections(pSectionHead);
            }
        }
        else /* Will happen if proxy not set / no connection available. */
            rc = RTErrInfoSetF(pErrInfo, VERR_EOF, "Certificate '%s' is empty", pszFilename);
        RTFileReadAllFree(pvContent, cbContent);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, rc, "RTFileReadAllEx failed with %Rrc on '%s'", rc, pszFilename);

    if (pafFoundFree)
        RTMemTmpFree(pafFoundFree);
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddWantedFromFile);


/**
 * Checks if the directory entry matches the specified suffixes.
 *
 * @returns true on match, false on mismatch.
 * @param   pDirEntry           The directory to check.
 * @param   paSuffixes          The array of suffixes to match against.
 * @param   cSuffixes           The number of suffixes in the array.
 */
DECLINLINE(bool) rtCrStoreIsSuffixMatch(PCRTDIRENTRY pDirEntry, PCRTSTRTUPLE paSuffixes, size_t cSuffixes)
{
    if (cSuffixes == 0)
        return true;

    size_t const cchName = pDirEntry->cbName;
    size_t i = cSuffixes;
    while (i-- > 0)
        if (   cchName > paSuffixes[i].cch
            && memcmp(&pDirEntry->szName[cchName - paSuffixes[i].cch], paSuffixes[i].psz, paSuffixes[i].cch) == 0)
            return true;

    return false;
}


RTDECL(int) RTCrStoreCertAddFromDir(RTCRSTORE hStore, uint32_t fFlags, const char *pszDir,
                                    PCRTSTRTUPLE paSuffixes, size_t cSuffixes, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);
    size_t i = cSuffixes;
    while (i-- > 0)
    {
        Assert(paSuffixes[i].cch > 0);
        Assert(strlen(paSuffixes[i].psz) == paSuffixes[i].cch);
    }

    /*
     * Prepare for constructing path to the files in the directory, so that we
     * can open them.
     */
    char szPath[RTPATH_MAX];
    int rc = RTStrCopy(szPath, sizeof(szPath), pszDir);
    if (RT_SUCCESS(rc))
    {
        size_t cchPath = RTPathEnsureTrailingSeparator(szPath, sizeof(szPath));
        if (cchPath > 0)
        {
            size_t const cbMaxFilename = sizeof(szPath) - cchPath;

            /*
             * Enumerate the directory.
             */
            RTDIR hDir;
            rc = RTDirOpen(&hDir, pszDir);
            if (RT_SUCCESS(rc))
            {
                for (;;)
                {
                    /* Read the next entry. */
                    union
                    {
                        RTDIRENTRY  DirEntry;
                        uint8_t     abPadding[RTPATH_MAX + sizeof(RTDIRENTRY)];
                    } u;
                    size_t cbBuf = sizeof(u);
                    int rc2 = RTDirRead(hDir, &u.DirEntry, &cbBuf);
                    if (RT_SUCCESS(rc2))
                    {
                        if (   (   u.DirEntry.enmType == RTDIRENTRYTYPE_FILE
                                || u.DirEntry.enmType == RTDIRENTRYTYPE_SYMLINK
                                || (   u.DirEntry.enmType == RTDIRENTRYTYPE_UNKNOWN
                                    && !RTDirEntryIsStdDotLink(&u.DirEntry)) )
                            && rtCrStoreIsSuffixMatch(&u.DirEntry, paSuffixes, cSuffixes) )
                        {
                            if (u.DirEntry.cbName < cbMaxFilename)
                            {
                                memcpy(&szPath[cchPath], u.DirEntry.szName, u.DirEntry.cbName + 1);
                                rc2 = RTDirQueryUnknownType(szPath, true /*fFollowSymlinks*/, &u.DirEntry.enmType);
                                if (   RT_SUCCESS(rc2)
                                    && u.DirEntry.enmType == RTDIRENTRYTYPE_FILE)
                                {
                                    /*
                                     * Add it.
                                     */
                                    rc2 = RTCrStoreCertAddFromFile(hStore, fFlags, szPath, pErrInfo);
                                    if (RT_FAILURE(rc2))
                                    {
                                        rc = rc2;
                                        if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                                            break;
                                    }
                                }
                            }
                            else
                            {
                                rc = RTErrInfoAddF(pErrInfo, VERR_FILENAME_TOO_LONG,
                                                   "  Too long filename (%u bytes)", u.DirEntry.cbName);
                                if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                                    break;
                            }
                        }
                    }
                    else
                    {
                        if (rc2 != VERR_NO_MORE_FILES)
                            rc = RTErrInfoAddF(pErrInfo, rc2, "  RTDirRead failed: %Rrc", rc2);
                        break;
                    }
                }

                RTDirClose(hDir);
            }
            else
                rc = RTErrInfoAddF(pErrInfo, rc, "  RTDirOpen('%s'): %Rrc", pszDir, rc);
        }
        else
            rc = VERR_FILENAME_TOO_LONG;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddFromDir);


RTDECL(int) RTCrStoreCertAddWantedFromDir(RTCRSTORE hStore, uint32_t fFlags,
                                          const char *pszDir, PCRTSTRTUPLE paSuffixes, size_t cSuffixes,
                                          PCRTCRCERTWANTED paWanted, size_t cWanted, bool *pafFound, PRTERRINFO pErrInfo)
{
    /*
     * Validate input a little.
     */
    AssertReturn(*pszDir, VERR_PATH_ZERO_LENGTH);
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);
    fFlags |= RTCRCERTCTX_F_ADD_IF_NOT_FOUND; /* forced */

    AssertReturn(cWanted, VERR_NOT_FOUND);
    for (uint32_t i = 0; i < cWanted; i++)
    {
        AssertReturn(!paWanted[i].pszSubject || *paWanted[i].pszSubject, VERR_INVALID_PARAMETER);
        AssertReturn(   paWanted[i].pszSubject
                     || paWanted[i].fSha1Fingerprint
                     || paWanted[i].fSha512Fingerprint,
                     VERR_INVALID_PARAMETER);
    }

    /*
     * Prepare for constructing path to the files in the directory, so that we
     * can open them.
     */
    char szPath[RTPATH_MAX];
    int rc = RTStrCopy(szPath, sizeof(szPath), pszDir);
    if (RT_SUCCESS(rc))
    {
        size_t cchPath = RTPathEnsureTrailingSeparator(szPath, sizeof(szPath));
        if (cchPath > 0)
        {
            size_t const cbMaxFilename = sizeof(szPath) - cchPath;

            /*
             * Enumerate the directory.
             */
            RTDIR hDir;
            rc = RTDirOpen(&hDir, pszDir);
            if (RT_SUCCESS(rc))
            {
                rc = VWRN_NOT_FOUND;
                for (;;)
                {
                    /* Read the next entry. */
                    union
                    {
                        RTDIRENTRY  DirEntry;
                        uint8_t     abPadding[RTPATH_MAX + sizeof(RTDIRENTRY)];
                    } u;
                    size_t cbEntry = sizeof(u);
                    int rc2 = RTDirRead(hDir, &u.DirEntry, &cbEntry);
                    if (RT_SUCCESS(rc2))
                    {
                        if (   (   u.DirEntry.enmType == RTDIRENTRYTYPE_FILE
                                || u.DirEntry.enmType == RTDIRENTRYTYPE_SYMLINK
                                || (   u.DirEntry.enmType == RTDIRENTRYTYPE_UNKNOWN
                                    && !RTDirEntryIsStdDotLink(&u.DirEntry)) )
                            && rtCrStoreIsSuffixMatch(&u.DirEntry, paSuffixes, cSuffixes) )
                        {
                            if (u.DirEntry.cbName < cbMaxFilename)
                            {
                                memcpy(&szPath[cchPath], u.DirEntry.szName, u.DirEntry.cbName);
                                szPath[cchPath + u.DirEntry.cbName] = '\0';
                                if (u.DirEntry.enmType != RTDIRENTRYTYPE_FILE)
                                    RTDirQueryUnknownType(szPath, true /*fFollowSymlinks*/, &u.DirEntry.enmType);
                                if (u.DirEntry.enmType == RTDIRENTRYTYPE_FILE)
                                {
                                    rc2 = RTCrStoreCertAddWantedFromFile(hStore, fFlags, szPath,
                                                                         paWanted, cWanted, pafFound, pErrInfo);
                                    if (rc2 == VINF_SUCCESS)
                                    {
                                        Assert(rtCrStoreAllDone(pafFound, cWanted));
                                        if (RT_SUCCESS(rc))
                                            rc = VINF_SUCCESS;
                                        break;
                                    }
                                    if (RT_FAILURE(rc2) && !(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                                    {
                                        rc = rc2;
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                /*
                                 * pErrInfo keeps the status code unless it's fatal.
                                 */
                                RTErrInfoAddF(pErrInfo, VERR_FILENAME_TOO_LONG,
                                              "  Too long filename (%u bytes)", u.DirEntry.cbName);
                                if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                                {
                                    rc = VERR_FILENAME_TOO_LONG;
                                    break;
                                }
                            }
                        }
                    }
                    else
                    {
                        if (rc2 != VERR_NO_MORE_FILES)
                        {
                            RTErrInfoAddF(pErrInfo, rc2, "RTDirRead failed: %Rrc", rc2);
                            if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                                rc = rc2;
                        }
                        break;
                    }
                }
                RTDirClose(hDir);
            }
        }
        else
            rc = VERR_FILENAME_TOO_LONG;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddWantedFromDir);

