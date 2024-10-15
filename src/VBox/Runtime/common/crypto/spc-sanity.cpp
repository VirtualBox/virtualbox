/* $Id: spc-sanity.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Microsoft SPC / Authenticode, Sanity Checkers.
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
#include <iprt/crypto/spc.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/uuid.h>

#include "spc-internal.h"


RTDECL(int) RTCrSpcIndirectDataContent_CheckSanityEx(PCRTCRSPCINDIRECTDATACONTENT pIndData,
                                                     PCRTCRPKCS7SIGNEDDATA pSignedData,
                                                     uint32_t fFlags,
                                                     PRTERRINFO pErrInfo)
{
    /*
     * Match up the digest algorithms (page 8, v1.0).
     */
    if (pSignedData->SignerInfos.cItems != 1)
        return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_NOT_EXACTLY_ONE_SIGNER_INFOS,
                             "SpcIndirectDataContent expects SignedData to have exactly one SignerInfos entries, found: %u",
                             pSignedData->SignerInfos.cItems);
    if (pSignedData->DigestAlgorithms.cItems != 1)
        return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_NOT_EXACTLY_ONE_DIGEST_ALGO,
                             "SpcIndirectDataContent expects SignedData to have exactly one DigestAlgorithms entries, found: %u",
                             pSignedData->DigestAlgorithms.cItems);

    if (RTCrX509AlgorithmIdentifier_Compare(&pIndData->DigestInfo.DigestAlgorithm, /** @todo not entirely sure about this check... */
                                            &pSignedData->SignerInfos.papItems[0]->DigestAlgorithm) != 0)
        return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_SIGNED_IND_DATA_DIGEST_ALGO_MISMATCH,
                             "SpcIndirectDataContent DigestInfo and SignerInfos algorithms mismatch: %s vs %s",
                             pIndData->DigestInfo.DigestAlgorithm.Algorithm.szObjId,
                             pSignedData->SignerInfos.papItems[0]->DigestAlgorithm.Algorithm.szObjId);

    if (RTCrX509AlgorithmIdentifier_Compare(&pIndData->DigestInfo.DigestAlgorithm,
                                            pSignedData->DigestAlgorithms.papItems[0]) != 0)
        return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_IND_DATA_DIGEST_ALGO_NOT_IN_DIGEST_ALGOS,
                             "SpcIndirectDataContent DigestInfo and SignedData.DigestAlgorithms[0] mismatch: %s vs %s",
                             pIndData->DigestInfo.DigestAlgorithm.Algorithm.szObjId,
                             pSignedData->DigestAlgorithms.papItems[0]->Algorithm.szObjId);

    if (fFlags & RTCRSPCINDIRECTDATACONTENT_SANITY_F_ONLY_KNOWN_HASH)
    {
        if (   RTCrX509AlgorithmIdentifier_GetDigestType(&pIndData->DigestInfo.DigestAlgorithm, true /*fPureDigestsOnly*/)
            == RTDIGESTTYPE_INVALID)
            return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_UNKNOWN_DIGEST_ALGO,
                                 "SpcIndirectDataContent DigestAlgortihm is not known: %s",
                                 pIndData->DigestInfo.DigestAlgorithm.Algorithm.szObjId);
    }

    uint32_t cbDigest = RTCrX509AlgorithmIdentifier_GetDigestSize(&pIndData->DigestInfo.DigestAlgorithm,
                                                                  true /*fPureDigestsOnly*/);
    if (   pIndData->DigestInfo.Digest.Asn1Core.cb != cbDigest
        && (cbDigest != UINT32_MAX || (fFlags & RTCRSPCINDIRECTDATACONTENT_SANITY_F_ONLY_KNOWN_HASH)))
        return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_IND_DATA_DIGEST_SIZE_MISMATCH,
                             "SpcIndirectDataContent Digest size mismatch with algorithm: %u, expected %u (%s)",
                             pIndData->DigestInfo.Digest.Asn1Core.cb, cbDigest,
                             pIndData->DigestInfo.DigestAlgorithm.Algorithm.szObjId);

    /*
     * Data.
     */
    if (fFlags & RTCRSPCINDIRECTDATACONTENT_SANITY_F_PE_IMAGE)
    {
        if (   pIndData->Data.enmType != RTCRSPCAAOVTYPE_PE_IMAGE_DATA
            || RTAsn1ObjId_CompareWithString(&pIndData->Data.Type, RTCRSPCPEIMAGEDATA_OID) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_EXPECTED_PE_IMAGE_DATA,
                                 "SpcIndirectDataContent.Data.Type is %s, expected %s (SpcPeImageData) [enmType=%d]",
                                 pIndData->Data.Type.szObjId, RTCRSPCPEIMAGEDATA_OID, pIndData->Data.enmType);
        if (   pIndData->Data.uValue.pPeImage
            || !RTCrSpcPeImageData_IsPresent(pIndData->Data.uValue.pPeImage) )
            return RTErrInfoSet(pErrInfo, VERR_CR_SPC_PEIMAGE_DATA_NOT_PRESENT,
                                "SpcIndirectDataContent.Data.uValue/PEImage is missing");

        if (   pIndData->Data.uValue.pPeImage->T0.File.enmChoice == RTCRSPCLINKCHOICE_MONIKER
            && RTCrSpcSerializedObject_IsPresent(pIndData->Data.uValue.pPeImage->T0.File.u.pMoniker) )
        {
            PCRTCRSPCSERIALIZEDOBJECT pObj = pIndData->Data.uValue.pPeImage->T0.File.u.pMoniker;

            if (pObj->Uuid.Asn1Core.cb != sizeof(RTUUID))
                return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_BAD_MONIKER_UUID,
                                     "SpcIndirectDataContent...MonikerT1.Uuid incorrect size: %u, expected %u.",
                                     pObj->Uuid.Asn1Core.cb, sizeof(RTUUID));
            if (RTUuidCompareStr(pObj->Uuid.Asn1Core.uData.pUuid, RTCRSPCSERIALIZEDOBJECT_UUID_STR) != 0)
                return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_UNKNOWN_MONIKER_UUID,
                                     "SpcIndirectDataContent...MonikerT1.Uuid mismatch: %RTuuid, expected %s.",
                                     pObj->Uuid.Asn1Core.uData.pUuid, RTCRSPCSERIALIZEDOBJECT_UUID_STR);

            if (pObj->enmType != RTCRSPCSERIALIZEDOBJECTTYPE_ATTRIBUTES)
                return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_BAD_MONIKER_CHOICE,
                                     "SpcIndirectDataContent...pMoniker->enmType=%d, expected %d.",
                                     pObj->enmType, RTCRSPCSERIALIZEDOBJECTTYPE_ATTRIBUTES);
            if (!pObj->u.pData)
                return RTErrInfoSet(pErrInfo, VERR_CR_SPC_MONIKER_BAD_DATA,
                                    "SpcIndirectDataContent...pMoniker->pData is NULL.");

            uint32_t cPageHashTabs = 0;
            for (uint32_t i = 0; i < pObj->u.pData->cItems; i++)
            {
                PCRTCRSPCSERIALIZEDOBJECTATTRIBUTE pAttr = pObj->u.pData->papItems[i];
                if (   RTAsn1ObjId_CompareWithString(&pAttr->Type, RTCRSPC_PE_IMAGE_HASHES_V1_OID) == 0
                    || RTAsn1ObjId_CompareWithString(&pAttr->Type, RTCRSPC_PE_IMAGE_HASHES_V2_OID) == 0 )
                {
                    cPageHashTabs++;
                    AssertPtr(pAttr->u.pPageHashes->pData);
                }
                else
                    return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_PEIMAGE_UNKNOWN_ATTRIBUTE,
                                         "SpcIndirectDataContent...MonikerT1 unknown attribute %u: %s.",
                                         i, pAttr->Type.szObjId);
            }
            if (cPageHashTabs > 0)
                return RTErrInfoSetF(pErrInfo, VERR_CR_SPC_PEIMAGE_MULTIPLE_HASH_TABS,
                                     "SpcIndirectDataContent...MonikerT1 multiple page hash attributes (%u).", cPageHashTabs);

        }
        else if (   pIndData->Data.uValue.pPeImage->T0.File.enmChoice == RTCRSPCLINKCHOICE_FILE
                 && RTCrSpcString_IsPresent(&pIndData->Data.uValue.pPeImage->T0.File.u.pT2->File) )
        {
            /* Could check for "<<<Obsolete>>>" here, but it's really irrelevant. */
        }
        else if (   pIndData->Data.uValue.pPeImage->T0.File.enmChoice == RTCRSPCLINKCHOICE_URL
                 && RTAsn1String_IsPresent(pIndData->Data.uValue.pPeImage->T0.File.u.pUrl) )
            return RTErrInfoSet(pErrInfo, VERR_CR_SPC_PEIMAGE_URL_UNEXPECTED,
                                "SpcIndirectDataContent.Data.uValue.pPeImage->File is an URL, expected object Moniker or File.");
        else
            return RTErrInfoSet(pErrInfo, VERR_CR_SPC_PEIMAGE_NO_CONTENT,
                                "SpcIndirectDataContent.Data.uValue.pPeImage->File has no content");
    }

    return VINF_SUCCESS;
}


/*
 * Generate the standard core code.
 */
#include <iprt/asn1-generator-sanity.h>

