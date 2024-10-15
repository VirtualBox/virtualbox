/* $Id: pkcs7-asn1-decoder.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS \#7, Decoder for ASN.1.
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
#include <iprt/crypto/spc.h>
#include <iprt/crypto/tsp.h>

#include "pkcs7-internal.h"


/*
 * PKCS #7 ContentInfo
 */
typedef enum RTCRPKCS7CONTENTINFOCHOICE
{
    RTCRPKCS7CONTENTINFOCHOICE_INVALID = 0,
    RTCRPKCS7CONTENTINFOCHOICE_UNKNOWN,
    RTCRPKCS7CONTENTINFOCHOICE_SIGNED_DATA,
    RTCRPKCS7CONTENTINFOCHOICE_SPC_INDIRECT_DATA_CONTENT,
    RTCRPKCS7CONTENTINFOCHOICE_TSP_TST_INFO,
    RTCRPKCS7CONTENTINFOCHOICE_END,
    RTCRPKCS7CONTENTINFOCHOICE_32BIT_HACK = 0x7fffffff
} RTCRPKCS7CONTENTINFOCHOICE;

static int rtCrPkcs7ContentInfo_DecodeExtra(PRTASN1CURSOR pCursor, uint32_t fFlags, PRTCRPKCS7CONTENTINFO pThis,
                                            const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags); RT_NOREF_PV(pszErrorTag);
    pThis->u.pCore = NULL;

    /*
     * Figure the type.
     */
    RTCRPKCS7CONTENTINFOCHOICE  enmChoice;
    size_t                      cbContent = 0;
    if (RTAsn1ObjId_CompareWithString(&pThis->ContentType, RTCRPKCS7SIGNEDDATA_OID) == 0)
    {
        enmChoice = RTCRPKCS7CONTENTINFOCHOICE_SIGNED_DATA;
        cbContent = sizeof(*pThis->u.pSignedData);
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ContentType, RTCRSPCINDIRECTDATACONTENT_OID) == 0)
    {
        enmChoice = RTCRPKCS7CONTENTINFOCHOICE_SPC_INDIRECT_DATA_CONTENT;
        cbContent = sizeof(*pThis->u.pIndirectDataContent);
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ContentType, RTCRTSPTSTINFO_OID) == 0)
    {
        enmChoice = RTCRPKCS7CONTENTINFOCHOICE_TSP_TST_INFO;
        cbContent = sizeof(*pThis->u.pTstInfo);
    }
    else
    {
        enmChoice = RTCRPKCS7CONTENTINFOCHOICE_UNKNOWN;
        cbContent = 0;
    }

    int rc = VINF_SUCCESS;
    if (enmChoice != RTCRPKCS7CONTENTINFOCHOICE_UNKNOWN)
    {
        /*
         * Detect CMS octet string format and open the content cursor.
         *
         * Current we don't have any octent string content which, they're all
         * sequences, which make detection so much simpler.
         */
        PRTASN1OCTETSTRING  pOctetString = &pThis->Content;
        RTASN1CURSOR        ContentCursor;
        rc = RTAsn1CursorInitSubFromCore(pCursor, &pThis->Content.Asn1Core, &ContentCursor, "Content");
        if (   RT_SUCCESS(rc)
            && RTAsn1CursorIsNextEx(&ContentCursor, ASN1_TAG_OCTET_STRING, ASN1_TAGFLAG_PRIMITIVE | ASN1_TAGCLASS_UNIVERSAL))
        {
            rc = RTAsn1MemAllocZ(&pThis->Content.EncapsulatedAllocation, (void **)&pThis->Content.pEncapsulated,
                                 sizeof(*pOctetString));
            if (RT_SUCCESS(rc))
            {
                pThis->pCmsContent = pOctetString = (PRTASN1OCTETSTRING)pThis->Content.pEncapsulated;
                rc = RTAsn1OctetString_DecodeAsn1(&ContentCursor, 0, pOctetString, "CmsContent");
                if (RT_SUCCESS(rc))
                    rc = RTAsn1CursorCheckEnd(&ContentCursor);
                if (RT_SUCCESS(rc))
                    rc = RTAsn1CursorInitSubFromCore(pCursor, &pOctetString->Asn1Core, &ContentCursor, "CmsContent");
            }
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Allocate memory for the decoded content.
             */
            rc = RTAsn1MemAllocZ(&pOctetString->EncapsulatedAllocation, (void **)&pOctetString->pEncapsulated, cbContent);
            if (RT_SUCCESS(rc))
            {
                pThis->u.pCore = pOctetString->pEncapsulated;

                /*
                 * Decode it.
                 */
                switch (enmChoice)
                {
                    case RTCRPKCS7CONTENTINFOCHOICE_SIGNED_DATA:
                        rc = RTCrPkcs7SignedData_DecodeAsn1(&ContentCursor, 0, pThis->u.pSignedData, "SignedData");
                        break;
                    case RTCRPKCS7CONTENTINFOCHOICE_SPC_INDIRECT_DATA_CONTENT:
                        rc = RTCrSpcIndirectDataContent_DecodeAsn1(&ContentCursor, 0, pThis->u.pIndirectDataContent,
                                                                   "IndirectDataContent");
                        break;
                    case RTCRPKCS7CONTENTINFOCHOICE_TSP_TST_INFO:
                        rc = RTCrTspTstInfo_DecodeAsn1(&ContentCursor, 0, pThis->u.pTstInfo, "TstInfo");
                        break;
                    default:
                        AssertFailed();
                        rc = VERR_IPE_NOT_REACHED_DEFAULT_CASE;
                        break;
                }
                if (RT_SUCCESS(rc))
                    rc = RTAsn1CursorCheckOctStrEnd(&ContentCursor, &pThis->Content);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;

                RTAsn1MemFree(&pOctetString->EncapsulatedAllocation, pOctetString->pEncapsulated);
                pOctetString->pEncapsulated = NULL;
                pThis->u.pCore = NULL;
            }
        }
    }
    return rc;
}


/*
 * Generate the code.
 */
#include <iprt/asn1-generator-asn1-decoder.h>

