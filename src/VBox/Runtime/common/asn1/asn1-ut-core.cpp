/* $Id: asn1-ut-core.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Generic Core Type.
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

#include <iprt/alloca.h>
#include <iprt/bignum.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/uni.h>

#include <iprt/formats/asn1.h>


/*
 * ASN.1 Core - Special methods (for all applications of RTASN1CORE).
 */

RTDECL(int) RTAsn1Core_SetTagAndFlags(PRTASN1CORE pAsn1Core, uint32_t uTag, uint8_t fClass)
{
    if (!(pAsn1Core->fFlags & RTASN1CORE_F_TAG_IMPLICIT))
    {
        pAsn1Core->fRealClass = pAsn1Core->fClass;
        pAsn1Core->uRealTag   = pAsn1Core->uTag;
        Assert(pAsn1Core->uRealTag == pAsn1Core->uTag);
        pAsn1Core->fFlags |= RTASN1CORE_F_TAG_IMPLICIT;
    }
    pAsn1Core->uTag   = uTag;
    pAsn1Core->fClass = fClass;
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Core_ChangeTag(PRTASN1CORE pAsn1Core, uint32_t uTag)
{
    if (!(pAsn1Core->fFlags & RTASN1CORE_F_TAG_IMPLICIT))
        pAsn1Core->uTag = uTag;
    pAsn1Core->uRealTag = uTag;
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1Core_ResetImplict(PRTASN1CORE pThis)
{
    AssertPtr(pThis);
    if (pThis->fFlags & RTASN1CORE_F_TAG_IMPLICIT)
    {
        pThis->fFlags &= ~RTASN1CORE_F_TAG_IMPLICIT;
        pThis->uTag    = pThis->uRealTag;
        pThis->fClass  = pThis->fRealClass;
    }
}


RTDECL(int) RTAsn1Core_InitEx(PRTASN1CORE pAsn1Core, uint32_t uTag, uint8_t fClass, PCRTASN1COREVTABLE pOps, uint32_t fFlags)
{
    pAsn1Core->uTag         = uTag;
    pAsn1Core->fClass       = fClass;
    pAsn1Core->uRealTag     = uTag;
    pAsn1Core->fRealClass   = fClass;
    pAsn1Core->cbHdr        = 0;
    pAsn1Core->cb           = 0;
    pAsn1Core->fFlags       = fFlags;
    pAsn1Core->uData.pv     = NULL;
    pAsn1Core->pOps         = pOps;
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Core_InitDefault(PRTASN1CORE pAsn1Core, uint32_t uTag, uint8_t fClass)
{
    return RTAsn1Core_InitEx(pAsn1Core, uTag, fClass, NULL, RTASN1CORE_F_DEFAULT);
}


static int rtAsn1Core_CloneEx(PRTASN1CORE pThis, PCRTASN1CORE pSrc, PCRTASN1ALLOCATORVTABLE pAllocator, bool fCopyContent)
{
    Assert(RTASN1CORE_IS_PRESENT(pSrc));
    pThis->uTag         = pSrc->uTag;
    pThis->fClass       = pSrc->fClass;
    pThis->uRealTag     = pSrc->uRealTag;
    pThis->fRealClass   = pSrc->fRealClass;
    pThis->cbHdr        = pSrc->cbHdr;
    pThis->fFlags       = pSrc->fFlags & ~(RTASN1CORE_F_ALLOCATED_CONTENT | RTASN1CORE_F_DECODED_CONTENT);
    pThis->pOps         = pSrc->pOps;
    pThis->cb           = 0;
    pThis->uData.pv     = NULL;
    if (pSrc->cb)
    {
        if (!fCopyContent)
            pThis->cb = pSrc->cb;
        else
        {
            int rc = RTAsn1ContentDup(pThis, pSrc->uData.pv, pSrc->cb, pAllocator);
            if (RT_FAILURE(rc))
            {
                RT_ZERO(*pThis);
                return rc;
            }
            Assert(pThis->cb == pSrc->cb);
            AssertPtr(pThis->uData.pv);
        }
    }
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Core_CloneContent(PRTASN1CORE pThis, PCRTASN1CORE pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    return rtAsn1Core_CloneEx(pThis, pSrc, pAllocator, true /*fCopyContent*/);
}


RTDECL(int) RTAsn1Core_CloneNoContent(PRTASN1CORE pThis, PCRTASN1CORE pSrc)
{
    return rtAsn1Core_CloneEx(pThis, pSrc, NULL, false /*fCopyContent*/);
}


RTDECL(int) RTAsn1Core_CompareEx(PCRTASN1CORE pLeft, PCRTASN1CORE pRight, bool fIgnoreTagAndClass)
{
    int iDiff;
    if (RTASN1CORE_IS_PRESENT(pLeft))
    {
        if (RTASN1CORE_IS_PRESENT(pRight))
        {
            iDiff = memcmp(pLeft->uData.pv, pRight->uData.pv, RT_MIN(pLeft->cb, pRight->cb));
            if (!iDiff)
            {
                if (pLeft->cb != pRight->cb)
                    iDiff = pLeft->cb < pRight->cb ? -1 : 1;
                else if (!fIgnoreTagAndClass)
                {
                    if (pLeft->uTag != pRight->uTag)
                        iDiff = pLeft->uTag < pRight->uTag ? -1 : 1;
                    else if (pLeft->fClass != pRight->fClass)
                        iDiff = pLeft->fClass < pRight->fClass ? -1 : 1;
                }
            }
            else
                iDiff = iDiff < 0 ? -1 : 1;
        }
        else
            iDiff = -1;
    }
    else
        iDiff = 0 - (int)RTASN1CORE_IS_PRESENT(pRight);
    return iDiff;
}


/**
 * @interface_method_impl{RTASN1COREVTABLE,pfnEncodePrep,
 *      This is for not dropping the unparsed content of a 'core' structure when
 *      re-encoding it. }
 */
static DECLCALLBACK(int) rtAsn1Core_EncodePrep(PRTASN1CORE pThisCore, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    /* We don't update anything here. */
    RT_NOREF(pThisCore, fFlags, pErrInfo);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTASN1COREVTABLE,pfnEncodeWrite,
 *      This is for not dropping the unparsed content of a 'core' structure when
 *      re-encoding it. }
 */
static DECLCALLBACK(int) rtAsn1Core_EncodeWrite(PRTASN1CORE pThisCore, uint32_t fFlags, PFNRTASN1ENCODEWRITER pfnWriter,
                                                void *pvUser, PRTERRINFO pErrInfo)
{
    int rc = RTAsn1EncodeWriteHeader(pThisCore, fFlags, pfnWriter, pvUser, pErrInfo);
    if (   RT_SUCCESS(rc)
        && rc != VINF_ASN1_NOT_ENCODED)
    {
        Assert(!RTASN1CORE_IS_DUMMY(pThisCore));
        if (pThisCore->cb)
        {
            AssertPtrReturn(pThisCore->uData.pv,
                            RTErrInfoSetF(pErrInfo, VERR_ASN1_INVALID_DATA_POINTER,
                                          "Invalid uData pointer %p for lone ASN.1 core with %#x bytes of content",
                                          pThisCore->uData.pv, pThisCore->cb));
            rc = pfnWriter(pThisCore->uData.pv, pThisCore->cb, pvUser, pErrInfo);
        }
    }
    return rc;
}



/*
 * ASN.1 Core - Standard Methods.
 *
 * @note Children of the ASN.1 Core doesn't normally call these, they are for
 *       when RTASN1CORE is used as a member type.
 */

RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1Core_Vtable =
{
    "RTAsn1Core",
    sizeof(RTASN1CORE),
    UINT8_MAX,
    UINT8_MAX,
    0,
    RTAsn1Core_Delete,
    RTAsn1Core_Enum,
    (PFNRTASN1COREVTCLONE)RTAsn1Core_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1Core_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1Core_CheckSanity,
    rtAsn1Core_EncodePrep,
    rtAsn1Core_EncodeWrite
};


RTDECL(int) RTAsn1Core_Init(PRTASN1CORE pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_NOREF_PV(pAllocator);
    return RTAsn1Core_InitEx(pThis, 0, ASN1_TAGCLASS_CONTEXT | ASN1_TAGFLAG_PRIMITIVE,
                             &g_RTAsn1Core_Vtable, RTASN1CORE_F_PRESENT);
}


RTDECL(int) RTAsn1Core_Clone(PRTASN1CORE pThis, PCRTASN1CORE pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    int rc;
    RT_ZERO(*pThis);
    if (RTASN1CORE_IS_PRESENT(pSrc))
    {
        Assert(pSrc->pOps == &g_RTAsn1Core_Vtable);

        rc = RTAsn1Core_CloneContent(pThis, pSrc, pAllocator);
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


RTDECL(void) RTAsn1Core_Delete(PRTASN1CORE pThis)
{
    if (pThis && RTASN1CORE_IS_PRESENT(pThis))
    {
        Assert(pThis->pOps == &g_RTAsn1Core_Vtable);

        RTAsn1ContentFree(pThis);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1Core_Enum(PRTASN1CORE pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    /* We have no children to enumerate. */
    Assert(pThis && (!RTASN1CORE_IS_PRESENT(pThis) || pThis->pOps == &g_RTAsn1Core_Vtable));
    NOREF(pThis);
    NOREF(pfnCallback);
    NOREF(uDepth);
    NOREF(pvUser);
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1Core_Compare(PCRTASN1CORE pLeft, PCRTASN1CORE pRight)
{
    Assert(pLeft  && (!RTASN1CORE_IS_PRESENT(pLeft)  || pLeft->pOps  == &g_RTAsn1Core_Vtable));
    Assert(pRight && (!RTASN1CORE_IS_PRESENT(pRight) || pRight->pOps == &g_RTAsn1Core_Vtable));

    return RTAsn1Core_CompareEx(pLeft, pRight, false /*fIgnoreTagAndClass*/);
}


RTDECL(int) RTAsn1Core_CheckSanity(PCRTASN1CORE pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags);

    /* We can only check that it's present. */
    if (!RTAsn1Core_IsPresent(pThis))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (RTASN1CORE).", pszErrorTag);
    return VINF_SUCCESS;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-core-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

