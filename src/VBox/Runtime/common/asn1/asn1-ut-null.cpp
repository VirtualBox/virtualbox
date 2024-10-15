/* $Id: asn1-ut-null.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, NULL type.
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
 * ASN.1 NULL - Special Methods.
 */


/*
 * ASN.1 NULL - Standard Methods.
 */


RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1Null_Vtable =
{
    "RTAsn1Null",
    sizeof(RTASN1NULL),
    ASN1_TAG_NULL,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1Null_Delete,
    (PFNRTASN1COREVTENUM)RTAsn1Null_Enum,
    (PFNRTASN1COREVTCLONE)RTAsn1Null_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1Null_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1Null_CheckSanity,
    NULL,
    NULL
};


RTDECL(int) RTAsn1Null_Init(PRTASN1NULL pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    return RTAsn1Core_InitEx(&pThis->Asn1Core, ASN1_TAG_NULL, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                             &g_RTAsn1Null_Vtable, RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
}


RTDECL(int) RTAsn1Null_Clone(PRTASN1NULL pThis, PCRTASN1NULL pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator); RT_NOREF_PV(pAllocator);
    RT_ZERO(*pThis);
    if (RTAsn1Null_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1Null_Vtable, VERR_INTERNAL_ERROR_3);
        AssertReturn(pSrc->Asn1Core.cb == 0, VERR_INTERNAL_ERROR_4);

        int rc = RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1Null_Delete(PRTASN1NULL pThis)
{
    if (   pThis
        && RTAsn1Null_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1Null_Vtable);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1Null_Enum(PRTASN1NULL pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    RT_NOREF_PV(pThis); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(uDepth); RT_NOREF_PV(pvUser);
    Assert(pThis && (!RTAsn1Null_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1Null_Vtable));

    /* No children to enumerate. */
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Null_Compare(PCRTASN1NULL pLeft, PCRTASN1NULL pRight)
{
    Assert(pLeft  && (!RTAsn1Null_IsPresent(pLeft)  || pLeft->Asn1Core.pOps  == &g_RTAsn1Null_Vtable));
    Assert(pRight && (!RTAsn1Null_IsPresent(pRight) || pRight->Asn1Core.pOps == &g_RTAsn1Null_Vtable));

    return (int)RTAsn1Null_IsPresent(pLeft) - (int)RTAsn1Null_IsPresent(pRight);
}


RTDECL(int) RTAsn1Null_CheckSanity(PCRTASN1NULL pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags);
    if (RT_UNLIKELY(!RTAsn1Null_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (NULL).", pszErrorTag);
    return VINF_SUCCESS;
}

/* No NULL object collections. */

