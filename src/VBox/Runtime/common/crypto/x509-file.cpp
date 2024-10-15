/* $Id: x509-file.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - X.509, File related APIs.
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
#include <iprt/crypto/x509.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/crypto/pem.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTCRPEMMARKERWORD const g_aWords_Certificate[]  = { { RT_STR_TUPLE("CERTIFICATE") } };
/** X509 Certificate markers. */
RT_DECL_DATA_CONST(RTCRPEMMARKER const) g_aRTCrX509CertificateMarkers[] =
{
    { g_aWords_Certificate, RT_ELEMENTS(g_aWords_Certificate) }
};
/** Number of entries in g_aRTCrX509CertificateMarkers. */
RT_DECL_DATA_CONST(uint32_t const) g_cRTCrX509CertificateMarkers = RT_ELEMENTS(g_aRTCrX509CertificateMarkers);


RTDECL(int) RTCrX509Certificate_ReadFromFile(PRTCRX509CERTIFICATE pCertificate, const char *pszFilename, uint32_t fFlags,
                                             PCRTASN1ALLOCATORVTABLE pAllocator, PRTERRINFO pErrInfo)
{
    AssertReturn(!(fFlags & ~RTCRX509CERT_READ_F_PEM_ONLY), VERR_INVALID_FLAGS);
    PCRTCRPEMSECTION pSectionHead;
    int rc = RTCrPemReadFile(pszFilename,
                             fFlags & RTCRX509CERT_READ_F_PEM_ONLY ? RTCRPEMREADFILE_F_ONLY_PEM : 0,
                             g_aRTCrX509CertificateMarkers, g_cRTCrX509CertificateMarkers,
                             &pSectionHead, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (pSectionHead)
        {
            RTCRX509CERTIFICATE TmpCert;
            RTASN1CURSORPRIMARY PrimaryCursor;
            RTAsn1CursorInitPrimary(&PrimaryCursor, pSectionHead->pbData, (uint32_t)RT_MIN(pSectionHead->cbData, UINT32_MAX),
                                    pErrInfo, pAllocator, RTASN1CURSOR_FLAGS_DER, RTPathFilename(pszFilename));
            rc = RTCrX509Certificate_DecodeAsn1(&PrimaryCursor.Cursor, 0, &TmpCert, "Cert");
            if (RT_SUCCESS(rc))
            {
                rc = RTCrX509Certificate_CheckSanity(&TmpCert, 0, pErrInfo, "Cert");
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrX509Certificate_Clone(pCertificate, &TmpCert, pAllocator);
                    if (RT_SUCCESS(rc))
                    {
                        if (pSectionHead->pNext || PrimaryCursor.Cursor.cbLeft)
                            rc = VINF_ASN1_MORE_DATA;
                    }
                }
                RTCrX509Certificate_Delete(&TmpCert);
            }
            RTCrPemFreeSections(pSectionHead);
        }
        else
            rc = rc != VINF_SUCCESS ? -rc : VERR_INTERNAL_ERROR_2;

    }
    return rc;
}


RTDECL(int) RTCrX509Certificate_ReadFromBuffer(PRTCRX509CERTIFICATE pCertificate, const void *pvBuf, size_t cbBuf,
                                               uint32_t fFlags, PCRTASN1ALLOCATORVTABLE pAllocator,
                                               PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    AssertReturn(!(fFlags & ~RTCRX509CERT_READ_F_PEM_ONLY), VERR_INVALID_FLAGS);
    PCRTCRPEMSECTION pSectionHead;
    int rc = RTCrPemParseContent(pvBuf, cbBuf,
                                 fFlags & RTCRX509CERT_READ_F_PEM_ONLY ? RTCRPEMREADFILE_F_ONLY_PEM : 0,
                                 g_aRTCrX509CertificateMarkers, g_cRTCrX509CertificateMarkers,
                                 &pSectionHead, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (pSectionHead)
        {
            RTCRX509CERTIFICATE TmpCert;
            RTASN1CURSORPRIMARY PrimaryCursor;
            RTAsn1CursorInitPrimary(&PrimaryCursor, pSectionHead->pbData, (uint32_t)RT_MIN(pSectionHead->cbData, UINT32_MAX),
                                    pErrInfo, pAllocator, RTASN1CURSOR_FLAGS_DER, pszErrorTag);
            rc = RTCrX509Certificate_DecodeAsn1(&PrimaryCursor.Cursor, 0, &TmpCert, "Cert");
            if (RT_SUCCESS(rc))
            {
                rc = RTCrX509Certificate_CheckSanity(&TmpCert, 0, pErrInfo, "Cert");
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrX509Certificate_Clone(pCertificate, &TmpCert, pAllocator);
                    if (RT_SUCCESS(rc))
                    {
                        if (pSectionHead->pNext || PrimaryCursor.Cursor.cbLeft)
                            rc = VINF_ASN1_MORE_DATA;
                    }
                }
                RTCrX509Certificate_Delete(&TmpCert);
            }
            RTCrPemFreeSections(pSectionHead);
        }
        else
            rc = rc != VINF_SUCCESS ? -rc : VERR_INTERNAL_ERROR_2;
    }
    return rc;
}



#if 0
RTDECL(int) RTCrX509Certificates_ReadFromFile(const char *pszFilename, uint32_t fFlags,
                                              PRTCRX509CERTIFICATES pCertificates, PRTERRINFO pErrInfo)
{
    AssertReturn(!(fFlags & ~RTCRX509CERT_READ_F_PEM_ONLY), VERR_INVALID_FLAGS);
    PCRTCRPEMSECTION pSectionHead;
    int rc = RTCrPemReadFile(pszFilename,
                             fFlags & RTCRX509CERT_READ_F_PEM_ONLY ? RTCRPEMREADFILE_F_ONLY_PEM : 0,
                             g_aRTCrX509CertificateMarkers, g_cRTCrX509CertificateMarkers,
                             &pSectionHead, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        pCertificates->Allocation

        PCRTCRPEMSECTION pCurSec = pSectionHead;
        while (pCurSec)
        {

            pCurSec = pCurSec->pNext;
        }

        RTCrPemFreeSections(pSectionHead);
    }
    return rc;
}
#endif

