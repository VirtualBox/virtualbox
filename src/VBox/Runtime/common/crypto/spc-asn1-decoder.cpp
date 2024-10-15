/* $Id: spc-asn1-decoder.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Microsoft SPC / Authenticode, Decoder for ASN.1.
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


/*
 * One SPC Serialized Object Attribute.
 */

/** Decode the content of the octet string value if known. */
static int rtCrSpcSerializedObject_DecodeMore(PRTASN1CURSOR pCursor, uint32_t fFlags,
                                              PRTCRSPCSERIALIZEDOBJECT pThis, const char *pszErrorTag)

{
    RT_NOREF_PV(fFlags); RT_NOREF_PV(pszErrorTag);

    int rc;
    RTASN1CURSOR SubCursor;
    if (RTUuidCompareStr(pThis->Uuid.Asn1Core.uData.pUuid, RTCRSPCSERIALIZEDOBJECT_UUID_STR) == 0)
    {
        rc = RTAsn1MemAllocZ(&pThis->SerializedData.EncapsulatedAllocation, (void **)&pThis->u.pData, sizeof(*pThis->u.pData));
        if (RT_SUCCESS(rc))
        {
            pThis->SerializedData.pEncapsulated = (PRTASN1CORE)pThis->u.pData;
            pThis->enmType = RTCRSPCSERIALIZEDOBJECTTYPE_ATTRIBUTES;

            rc = RTAsn1CursorInitSubFromCore(pCursor, &pThis->SerializedData.Asn1Core, &SubCursor, "SerializedData");
            if (RT_SUCCESS(rc))
                rc = RTCrSpcSerializedObjectAttributes_DecodeAsn1(&SubCursor, 0, pThis->u.pData, "SD");
        }
    }
    else
        return VINF_SUCCESS;
    if (RT_SUCCESS(rc))
        rc = RTAsn1CursorCheckEnd(&SubCursor);
    return rc;
}

/*
 * Generate the code.
 */
#include <iprt/asn1-generator-asn1-decoder.h>

