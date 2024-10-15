/* $Id: asn1-ut-dyntype.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Basic Operations.
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
#include <iprt/asn1.h>

#include <iprt/err.h>
#include <iprt/string.h>

#include <iprt/formats/asn1.h>


/*
 * ASN.1 dynamic type union - Standard Methods.
 */


RTDECL(int) RTAsn1DynType_Init(PRTASN1DYNTYPE pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    RT_ZERO(*pThis);
    pThis->enmType = RTASN1TYPE_NOT_PRESENT;
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1DynType_Clone(PRTASN1DYNTYPE pThis, PCRTASN1DYNTYPE pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);
    RT_ZERO(*pThis);
    if (RTAsn1DynType_IsPresent(pSrc))
    {
        int rc;
        switch (pSrc->enmType)
        {
            case RTASN1TYPE_CORE:           rc = RTAsn1Core_Clone(&pThis->u.Core, &pSrc->u.Core, pAllocator); break;
            case RTASN1TYPE_NULL:           rc = RTAsn1Null_Clone(&pThis->u.Asn1Null, &pSrc->u.Asn1Null, pAllocator); break;
            case RTASN1TYPE_INTEGER:        rc = RTAsn1Integer_Clone(&pThis->u.Integer, &pSrc->u.Integer, pAllocator); break;
            case RTASN1TYPE_BOOLEAN:        rc = RTAsn1Boolean_Clone(&pThis->u.Boolean, &pSrc->u.Boolean, pAllocator); break;
            case RTASN1TYPE_STRING:         rc = RTAsn1String_Clone(&pThis->u.String, &pSrc->u.String, pAllocator); break;
            case RTASN1TYPE_OCTET_STRING:   rc = RTAsn1OctetString_Clone(&pThis->u.OctetString, &pSrc->u.OctetString, pAllocator); break;
            case RTASN1TYPE_BIT_STRING:     rc = RTAsn1BitString_Clone(&pThis->u.BitString, &pSrc->u.BitString, pAllocator); break;
            case RTASN1TYPE_TIME:           rc = RTAsn1Time_Clone(&pThis->u.Time, &pSrc->u.Time, pAllocator); break;
#if 0
            case RTASN1TYPE_SEQUENCE_CORE:  rc = VERR_NOT_SUPPORTED; //rc = RTAsn1SequenceCore_Clone(&pThis->u.SeqCore, &pSrc->u.SeqCore, pAllocator); break;
            case RTASN1TYPE_SET_CORE:       rc = VERR_NOT_SUPPORTED; //rc = RTAsn1SetCore_Clone(&pThis->u.SetCore, &pSrc->u.SetCore, pAllocator); break;
#endif
            case RTASN1TYPE_OBJID:          rc = RTAsn1ObjId_Clone(&pThis->u.ObjId, &pSrc->u.ObjId, pAllocator); break;
            default:
                AssertFailedReturn(VERR_ASN1_INTERNAL_ERROR_2);
        }
        if (RT_FAILURE(rc))
        {
            RT_ZERO(*pThis);
            return rc;
        }
        pThis->enmType = pSrc->enmType;
    }
    else
        pThis->enmType = RTASN1TYPE_NOT_PRESENT;
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1DynType_Delete(PRTASN1DYNTYPE pThis)
{
    if (   pThis
        && RTAsn1DynType_IsPresent(pThis))
    {
        if (   pThis->u.Core.pOps
            && pThis->u.Core.pOps->pfnDtor)
            pThis->u.Core.pOps->pfnDtor(&pThis->u.Core);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1DynType_SetToNull(PRTASN1DYNTYPE pThis)
{
    RTAsn1DynType_Delete(pThis);
    pThis->enmType = RTASN1TYPE_NULL;
    return RTAsn1Null_Init(&pThis->u.Asn1Null, NULL /*pAllocator*/);
}


RTDECL(int) RTAsn1DynType_SetToObjId(PRTASN1DYNTYPE pThis, PCRTASN1OBJID pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RTAsn1DynType_Delete(pThis);
    pThis->enmType = RTASN1TYPE_OBJID;
    return RTAsn1ObjId_Clone(&pThis->u.ObjId, pSrc, pAllocator);
}


RTDECL(int) RTAsn1DynType_Enum(PRTASN1DYNTYPE pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    if (   pThis
        && RTAsn1DynType_IsPresent(pThis))
    {
        if (   pThis->u.Core.pOps
            && pThis->u.Core.pOps->pfnEnum)
            return pThis->u.Core.pOps->pfnEnum(&pThis->u.Core, pfnCallback, uDepth, pvUser);
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1DynType_Compare(PCRTASN1DYNTYPE pLeft, PCRTASN1DYNTYPE pRight)
{
    if (RTAsn1DynType_IsPresent(pLeft) && RTAsn1DynType_IsPresent(pRight))
    {
        if (pLeft->enmType != pRight->enmType)
            return pLeft->enmType < pRight->enmType ? -1 : 1;

        switch (pLeft->enmType)
        {
            case RTASN1TYPE_CORE:           return RTAsn1Core_Compare(&pLeft->u.Core, &pRight->u.Core); break;
            case RTASN1TYPE_NULL:           return RTAsn1Null_Compare(&pLeft->u.Asn1Null, &pRight->u.Asn1Null);
            case RTASN1TYPE_INTEGER:        return RTAsn1Integer_Compare(&pLeft->u.Integer, &pRight->u.Integer);
            case RTASN1TYPE_BOOLEAN:        return RTAsn1Boolean_Compare(&pLeft->u.Boolean, &pRight->u.Boolean);
            case RTASN1TYPE_STRING:         return RTAsn1String_Compare(&pLeft->u.String, &pRight->u.String);
            case RTASN1TYPE_OCTET_STRING:   return RTAsn1OctetString_Compare(&pLeft->u.OctetString, &pRight->u.OctetString);
            case RTASN1TYPE_BIT_STRING:     return RTAsn1BitString_Compare(&pLeft->u.BitString, &pRight->u.BitString);
            case RTASN1TYPE_TIME:           return RTAsn1Time_Compare(&pLeft->u.Time, &pRight->u.Time);
            case RTASN1TYPE_OBJID:          return RTAsn1ObjId_Compare(&pLeft->u.ObjId, &pRight->u.ObjId);
            default:  AssertFailedReturn(-1);
        }
    }
    else
        return (int)RTAsn1DynType_IsPresent(pLeft) - (int)RTAsn1DynType_IsPresent(pRight);
}


RTDECL(int) RTAsn1DynType_CheckSanity(PCRTASN1DYNTYPE pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    if (RT_UNLIKELY(!RTAsn1DynType_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (DYNTYPE).", pszErrorTag);

    int rc;
    switch (pThis->enmType)
    {
        case RTASN1TYPE_CORE:           rc = RTAsn1Core_CheckSanity(&pThis->u.Core, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_NULL:           rc = RTAsn1Null_CheckSanity(&pThis->u.Asn1Null, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_INTEGER:        rc = RTAsn1Integer_CheckSanity(&pThis->u.Integer, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_BOOLEAN:        rc = RTAsn1Boolean_CheckSanity(&pThis->u.Boolean, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_STRING:         rc = RTAsn1String_CheckSanity(&pThis->u.String, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_OCTET_STRING:   rc = RTAsn1OctetString_CheckSanity(&pThis->u.OctetString, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_BIT_STRING:     rc = RTAsn1BitString_CheckSanity(&pThis->u.BitString, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_TIME:           rc = RTAsn1Time_CheckSanity(&pThis->u.Time, fFlags, pErrInfo, pszErrorTag); break;
#if 0
        case RTASN1TYPE_SEQUENCE_CORE:  rc = VINF_SUCCESS; //rc = RTAsn1SequenceCore_CheckSanity(&pThis->u.SeqCore, fFlags, pErrInfo, pszErrorTag); break;
        case RTASN1TYPE_SET_CORE:       rc = VINF_SUCCESS; //rc = RTAsn1SetCore_CheckSanity(&pThis->u.SetCore, fFlags, pErrInfo, pszErrorTag); break;
#endif
        case RTASN1TYPE_OBJID:          rc = RTAsn1ObjId_CheckSanity(&pThis->u.ObjId, fFlags, pErrInfo, pszErrorTag); break;
        default:
            AssertFailedReturn(VERR_ASN1_INTERNAL_ERROR_2);
    }

    return rc;
}

