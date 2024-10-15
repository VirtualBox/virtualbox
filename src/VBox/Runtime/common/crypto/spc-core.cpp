/* $Id: spc-core.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Microsoft SPC / Authenticode, Core APIs.
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

#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#include "spc-internal.h"


RTDECL(int) RTCrSpcSerializedPageHashes_UpdateDerivedData(PRTCRSPCSERIALIZEDPAGEHASHES pThis)
{
    pThis->pData = (PCRTCRSPCPEIMAGEPAGEHASHES)pThis->RawData.Asn1Core.uData.pv;
    return VINF_SUCCESS;
}


/*
 * SPC Indirect Data Content.
 */

RTDECL(PCRTCRSPCSERIALIZEDOBJECTATTRIBUTE)
RTCrSpcIndirectDataContent_GetPeImageObjAttrib(PCRTCRSPCINDIRECTDATACONTENT pThis,
                                               RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE enmType)
{
    if (pThis->Data.enmType == RTCRSPCAAOVTYPE_PE_IMAGE_DATA)
    {
        Assert(RTAsn1ObjId_CompareWithString(&pThis->Data.Type, RTCRSPCPEIMAGEDATA_OID) == 0);

        if (   pThis->Data.uValue.pPeImage
            && pThis->Data.uValue.pPeImage->T0.File.enmChoice == RTCRSPCLINKCHOICE_MONIKER
            && RTCrSpcSerializedObject_IsPresent(pThis->Data.uValue.pPeImage->T0.File.u.pMoniker) )
        {
            if (pThis->Data.uValue.pPeImage->T0.File.u.pMoniker->enmType == RTCRSPCSERIALIZEDOBJECTTYPE_ATTRIBUTES)
            {
                Assert(RTUuidCompareStr(pThis->Data.uValue.pPeImage->T0.File.u.pMoniker->Uuid.Asn1Core.uData.pUuid,
                                        RTCRSPCSERIALIZEDOBJECT_UUID_STR) == 0);
                PCRTCRSPCSERIALIZEDOBJECTATTRIBUTES pData = pThis->Data.uValue.pPeImage->T0.File.u.pMoniker->u.pData;
                if (pData)
                    for (uint32_t i = 0; i < pData->cItems; i++)
                        if (pData->papItems[i]->enmType == enmType)
                            return pData->papItems[i];
            }
        }
    }
    return NULL;
}


/*
 * Generate the standard core code.
 */
#include <iprt/asn1-generator-core.h>

