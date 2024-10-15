/* $Id: pkcs7-file.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS\#7/CMS, File related APIs.
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

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/crypto/pem.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTCRPEMMARKERWORD const g_aWords_Cms[]   = { { RT_STR_TUPLE("CMS") } };
static RTCRPEMMARKERWORD const g_aWords_Pkcs7[] = { { RT_STR_TUPLE("PKCS7") } };
/** X509 Certificate markers. */
RT_DECL_DATA_CONST(RTCRPEMMARKER const) g_aRTCrPkcs7Markers[] =
{
    { g_aWords_Cms,   RT_ELEMENTS(g_aWords_Cms) },
    { g_aWords_Pkcs7, RT_ELEMENTS(g_aWords_Pkcs7) }
};
/** Number of entries in g_aRTCrPkcs7Markers. */
RT_DECL_DATA_CONST(uint32_t const) g_cRTCrPkcs7Markers = RT_ELEMENTS(g_aRTCrPkcs7Markers);


/** @name Flags for RTCrPkcs7ContentInfo_ReadFromBuffer
 * @{ */
/** Only allow PEM certificates, not binary ones.
 * @sa RTCRPEMREADFILE_F_ONLY_PEM  */
#define RTCRPKCS7_READ_F_PEM_ONLY        RT_BIT(1)
/** @} */


RTDECL(int) RTCrPkcs7_ReadFromBuffer(PRTCRPKCS7CONTENTINFO pContentInfo, const void *pvBuf, size_t cbBuf,
                                     uint32_t fFlags, PCRTASN1ALLOCATORVTABLE pAllocator,
                                     bool *pfCmsLabeled, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    if (pfCmsLabeled)
        *pfCmsLabeled = false;
    AssertReturn(!(fFlags & ~RTCRX509CERT_READ_F_PEM_ONLY), VERR_INVALID_FLAGS);

    PCRTCRPEMSECTION pSectionHead;
    int rc = RTCrPemParseContent(pvBuf, cbBuf,
                                 fFlags & RTCRX509CERT_READ_F_PEM_ONLY ? RTCRPEMREADFILE_F_ONLY_PEM : 0,
                                 g_aRTCrPkcs7Markers, g_cRTCrPkcs7Markers,
                                 &pSectionHead, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (pSectionHead)
        {
            if (pfCmsLabeled)
                *pfCmsLabeled = pSectionHead->pMarker == &g_aRTCrPkcs7Markers[0];

            RTASN1CURSORPRIMARY PrimaryCursor;
            RTAsn1CursorInitPrimary(&PrimaryCursor, pSectionHead->pbData, (uint32_t)RT_MIN(pSectionHead->cbData, UINT32_MAX),
                                    pErrInfo, pAllocator, RTASN1CURSOR_FLAGS_DER, pszErrorTag);

            RTCRPKCS7CONTENTINFO TmpContentInfo;
            rc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, &TmpContentInfo, "CI");
            if (RT_SUCCESS(rc))
            {
                rc = RTCrPkcs7ContentInfo_CheckSanity(&TmpContentInfo, 0, pErrInfo, "CI");
                if (RT_SUCCESS(rc))
                {
                    rc = RTCrPkcs7ContentInfo_Clone(pContentInfo, &TmpContentInfo, pAllocator);
                    if (RT_SUCCESS(rc))
                    {
                        if (pSectionHead->pNext || PrimaryCursor.Cursor.cbLeft)
                            rc = VINF_ASN1_MORE_DATA;
                    }
                }
                RTCrPkcs7ContentInfo_Delete(&TmpContentInfo);
            }
            RTCrPemFreeSections(pSectionHead);
        }
        else
            rc = rc != VINF_SUCCESS ? -rc : VERR_INTERNAL_ERROR_2;
    }
    return rc;
}

