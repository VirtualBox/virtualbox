/* $Id: asn1-ut-boolean.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, BOOLEAN Type.
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

#include <iprt/bignum.h>
#include <iprt/err.h>
#include <iprt/string.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The false value (DER & CER). */
static const uint8_t  g_bFalse = 0;
/** The true value (DER & CER). */
static const uint8_t  g_bTrue  = 0xff;


/*
 * ASN.1 BOOLEAN - Special Methods.
 */

RTDECL(int) RTAsn1Boolean_InitDefault(PRTASN1BOOLEAN pThis, bool fValue, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    RTAsn1Core_InitEx(&pThis->Asn1Core, ASN1_TAG_BOOLEAN, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1Boolean_Vtable, RTASN1CORE_F_DEFAULT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    pThis->fValue = fValue;
    pThis->Asn1Core.uData.pv = (void *)(fValue ? &g_bTrue : &g_bFalse);
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Boolean_Set(PRTASN1BOOLEAN pThis, bool fValue)
{
    /* Since we don't need an allocator, let's automatically initialize the struct. */
    if (!RTAsn1Boolean_IsPresent(pThis))
        RTAsn1Boolean_Init(pThis, NULL);
    else
        RTAsn1ContentFree(&pThis->Asn1Core);
    pThis->fValue            = fValue;
    pThis->Asn1Core.uData.pv = (void *)(fValue ? &g_bTrue : &g_bFalse);
    pThis->Asn1Core.cb       = 1;
    pThis->Asn1Core.fFlags  &= ~RTASN1CORE_F_DEFAULT;
    pThis->Asn1Core.fFlags  |= RTASN1CORE_F_PRESENT;
    return VINF_SUCCESS;
}



/*
 * ASN.1 BOOLEAN - Standard Methods.
 */

RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1Boolean_Vtable =
{
    "RTAsn1Boolean",
    sizeof(RTASN1BOOLEAN),
    ASN1_TAG_BOOLEAN,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1Boolean_Delete,
    NULL,
    (PFNRTASN1COREVTCLONE)RTAsn1Boolean_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1Boolean_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1Boolean_CheckSanity,
    NULL,
    NULL
};


RTDECL(int) RTAsn1Boolean_Init(PRTASN1BOOLEAN pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    RTAsn1Core_InitEx(&pThis->Asn1Core, ASN1_TAG_BOOLEAN, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1Boolean_Vtable, RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    pThis->fValue = true;
    pThis->Asn1Core.cb = 1;
    pThis->Asn1Core.uData.pv = (void *)&g_bTrue;
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Boolean_Clone(PRTASN1BOOLEAN pThis, PCRTASN1BOOLEAN pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);
    RT_ZERO(*pThis);
    if (RTAsn1Boolean_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1Boolean_Vtable, VERR_INTERNAL_ERROR_3);
        AssertReturn(pSrc->Asn1Core.cb <= 1, VERR_INTERNAL_ERROR_4);

        int rc;
        if (   pSrc->Asn1Core.cb == 1
            && pSrc->Asn1Core.uData.pu8[0] != 0x00
            && pSrc->Asn1Core.uData.pu8[0] != 0xff)
        {
            /* DER/CER incompatible value must be copied as-is. */
            rc = RTAsn1Core_CloneContent(&pThis->Asn1Core, &pSrc->Asn1Core, pAllocator);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
        {
            /* No value or one of the standard values. */
            rc = RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
            if (RT_FAILURE(rc))
                return rc;
            pThis->Asn1Core.uData.pv = (void *)(pSrc->fValue ? &g_bTrue : &g_bFalse);
        }
        pThis->fValue = pSrc->fValue;
    }
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1Boolean_Delete(PRTASN1BOOLEAN pThis)
{
    if (   pThis
        && RTAsn1Boolean_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1Boolean_Vtable);
        Assert(pThis->Asn1Core.cb <= 1);

        RTAsn1ContentFree(&pThis->Asn1Core);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1Boolean_Enum(PRTASN1BOOLEAN pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    Assert(pThis && (!RTAsn1Boolean_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1Boolean_Vtable));
    RT_NOREF_PV(pThis); RT_NOREF_PV(pfnCallback); RT_NOREF_PV(uDepth); RT_NOREF_PV(pvUser);

    /* No children to enumerate. */
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Boolean_Compare(PCRTASN1BOOLEAN pLeft, PCRTASN1BOOLEAN pRight)
{
    Assert(pLeft  && (!RTAsn1Boolean_IsPresent(pLeft)  || pLeft->Asn1Core.pOps  == &g_RTAsn1Boolean_Vtable));
    Assert(pRight && (!RTAsn1Boolean_IsPresent(pRight) || pRight->Asn1Core.pOps == &g_RTAsn1Boolean_Vtable));

    int iDiff;
    if (RTAsn1Boolean_IsPresent(pLeft))
    {
        if (RTAsn1Boolean_IsPresent(pRight))
            iDiff = (int)pLeft->fValue - (int)pRight->fValue;
        else
            iDiff = -1;
    }
    else
        iDiff = 0 - (int)RTAsn1Boolean_IsPresent(pRight);
    return iDiff;
}


RTDECL(int) RTAsn1Boolean_CheckSanity(PCRTASN1BOOLEAN pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    if (RT_UNLIKELY(!RTAsn1Boolean_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (BOOLEAN).", pszErrorTag);
    RT_NOREF_PV(fFlags);
    return VINF_SUCCESS;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-boolean-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

