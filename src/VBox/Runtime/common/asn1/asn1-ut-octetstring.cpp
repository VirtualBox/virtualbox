/* $Id: asn1-ut-octetstring.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Octet String.
 *
 * @remarks This file should remain very similar to asn1-ut-bitstring.cpp.
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


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTASN1OCTETSTRINGWRITERCTX
{
    /** Pointer to the output buffer. */
    uint8_t        *pbBuf;
    /** The current buffer offset. */
    uint32_t        offBuf;
    /** The size of the buffer. */
    uint32_t        cbBuf;
} RTASN1OCTETSTRINGWRITERCTX;


/** @callback_method_impl{FNRTASN1ENCODEWRITER,
 *  Used to refresh the content of octet and bit strings. } */
static DECLCALLBACK(int) rtAsn1OctetStringEncodeWriter(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RTASN1OCTETSTRINGWRITERCTX *pCtx = (RTASN1OCTETSTRINGWRITERCTX *)pvUser;
    AssertReturn(cbToWrite <= pCtx->cbBuf - pCtx->offBuf,
                 RTErrInfoSetF(pErrInfo, VERR_BUFFER_OVERFLOW,
                               "cbToWrite=%#x offBuf=%#x cbBuf=%#x", cbToWrite, pCtx->cbBuf, pCtx->offBuf));
    memcpy(&pCtx->pbBuf[pCtx->offBuf], pvBuf, cbToWrite);
    pCtx->offBuf += (uint32_t)cbToWrite;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNRTASN1ENCODEWRITER,
 *  Used to compare the encoded raw content of an octet or bit string with the
 *  encapsulated object. } */
static DECLCALLBACK(int) rtAsn1OctetStringEncodeCompare(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RTASN1OCTETSTRINGWRITERCTX *pCtx = (RTASN1OCTETSTRINGWRITERCTX *)pvUser;
    RT_NOREF_PV(pErrInfo);
    AssertReturn(cbToWrite <= pCtx->cbBuf - pCtx->offBuf, VERR_BUFFER_OVERFLOW);
    if (memcmp(&pCtx->pbBuf[pCtx->offBuf], pvBuf, cbToWrite) != 0)
        return VERR_NOT_EQUAL;
    pCtx->offBuf += (uint32_t)cbToWrite;
    return VINF_SUCCESS;
}


/*
 * ASN.1 OCTET STRING - Specific Methods
 */

RTDECL(int) RTAsn1OctetString_RefreshContent(PRTASN1OCTETSTRING pThis, uint32_t fFlags,
                                             PCRTASN1ALLOCATORVTABLE pAllocator, PRTERRINFO pErrInfo)
{
    AssertReturn(pThis->pEncapsulated, VERR_INVALID_STATE);

    uint32_t cbEncoded;
    int rc = RTAsn1EncodePrepare(pThis->pEncapsulated, fFlags, &cbEncoded, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        pThis->Asn1Core.cb = cbEncoded;

        rc = RTAsn1ContentReallocZ(&pThis->Asn1Core, cbEncoded, pAllocator);
        if (RT_SUCCESS(rc))
        {
            /* Initialize the writer context. */
            RTASN1OCTETSTRINGWRITERCTX Ctx;
            Ctx.pbBuf  = (uint8_t *)pThis->Asn1Core.uData.pu8;
            Ctx.cbBuf  = cbEncoded;
            Ctx.offBuf = 0;

            rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, rtAsn1OctetStringEncodeWriter, &Ctx, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (Ctx.offBuf == cbEncoded)
                    return VINF_SUCCESS;

                rc = RTErrInfoSetF(pErrInfo, rc, "Expected %#x bytes, got %#x", cbEncoded, Ctx.offBuf);
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "Error allocating %#x bytes for storing content\n", cbEncoded);
    }
    return rc;
}


RTDECL(int) RTAsn1OctetString_AllocContent(PRTASN1OCTETSTRING pThis, void const *pvSrc, size_t cb,
                                           PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertReturn(!pThis->pEncapsulated, VERR_INVALID_STATE);
    int rc;
    if (pvSrc)
        rc = RTAsn1ContentDup(&pThis->Asn1Core, pvSrc, cb, pAllocator);
    else
        rc = RTAsn1ContentAllocZ(&pThis->Asn1Core, cb, pAllocator);
    return rc;
}


RTDECL(int) RTAsn1OctetString_SetContent(PRTASN1OCTETSTRING pThis, void const *pvSrc, size_t cbSrc,
                                         PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtrReturn(pvSrc, VERR_INVALID_POINTER);
    return RTAsn1OctetString_AllocContent(pThis, pvSrc, cbSrc, pAllocator);
}


RTDECL(bool) RTAsn1OctetString_AreContentBytesValid(PCRTASN1OCTETSTRING pThis, uint32_t fFlags)
{
    if (pThis->pEncapsulated)
    {
        /* Check the encoded length of the octets. */
        uint32_t cbEncoded;
        int rc = RTAsn1EncodePrepare(pThis->pEncapsulated, fFlags, &cbEncoded, NULL);
        if (RT_FAILURE(rc))
            return false;
        if (pThis->Asn1Core.cb != cbEncoded)
            return false;

        /* Check the encoded bytes, if there are any. */
        if (cbEncoded)
        {
            if (!pThis->Asn1Core.uData.pv)
                return false;

            /* Check the other bytes. */
            RTASN1OCTETSTRINGWRITERCTX Ctx;
            Ctx.pbBuf  = (uint8_t *)pThis->Asn1Core.uData.pu8;
            Ctx.cbBuf  = cbEncoded;
            Ctx.offBuf = 0;
            rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, rtAsn1OctetStringEncodeCompare, &Ctx, NULL);
            if (RT_FAILURE(rc))
                return false;
        }
    }
    return true;
}


/*
 * ASN.1 OCTET STRING - Standard Methods.
 */

/** @interface_method_impl{FNRTASN1COREVTENCODEPREP} */
static DECLCALLBACK(int) RTAsn1OctetString_EncodePrep(PRTASN1CORE pThisCore, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    PRTASN1OCTETSTRING pThis = (PRTASN1OCTETSTRING)pThisCore;
    if (!pThis->pEncapsulated)
    {
        Assert(pThis->Asn1Core.cb == 0 || pThis->Asn1Core.uData.pv);
        return VINF_SUCCESS;
    }

    /* Figure out the size of the encapsulated content. */
    uint32_t cbEncoded;
    int rc = RTAsn1EncodePrepare(pThis->pEncapsulated, fFlags, &cbEncoded, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        /* Free the bytes if they don't match up.  */
        if (pThis->Asn1Core.uData.pv)
        {
            bool fMustFree = pThis->Asn1Core.cb != cbEncoded;
            if (!fMustFree)
            {
                RTASN1OCTETSTRINGWRITERCTX Ctx;
                Ctx.pbBuf  = (uint8_t *)pThis->Asn1Core.uData.pu8;
                Ctx.cbBuf  = cbEncoded;
                Ctx.offBuf = 0;
                rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, rtAsn1OctetStringEncodeCompare, &Ctx, NULL);
                fMustFree = RT_FAILURE_NP(rc);
            }
            if (fMustFree)
                RTAsn1ContentFree(&pThis->Asn1Core);
        }

        pThis->Asn1Core.cb = cbEncoded;
        rc = RTAsn1EncodeRecalcHdrSize(&pThis->Asn1Core, fFlags, pErrInfo);
    }
    return rc;
}


/** @interface_method_impl{FNRTASN1COREVTENCODEWRITE} */
static DECLCALLBACK(int) RTAsn1OctetString_EncodeWrite(PRTASN1CORE pThisCore, uint32_t fFlags, PFNRTASN1ENCODEWRITER pfnWriter,
                                                       void *pvUser, PRTERRINFO pErrInfo)
{
    PRTASN1OCTETSTRING pThis = (PRTASN1OCTETSTRING)pThisCore;

    /*
     * First the header.
     */
    int rc = RTAsn1EncodeWriteHeader(&pThis->Asn1Core, fFlags, pfnWriter, pvUser, pErrInfo);
    if (RT_SUCCESS(rc) && rc != VINF_ASN1_NOT_ENCODED)
    {
        /*
         * If nothing is encapsulated, the core points to the content (if we have any).
         */
        if (!pThis->pEncapsulated)
        {
            if (pThis->Asn1Core.cb > 0)
                rc = pfnWriter(pThis->Asn1Core.uData.pu8, pThis->Asn1Core.cb, pvUser, pErrInfo);
        }
        /*
         * Call upon the encapsulated content to serialize itself.
         */
        else
            rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, pfnWriter, pvUser, pErrInfo);
    }
    return rc;
}


RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1OctetString_Vtable =
{
    "OctetString",
    sizeof(RTASN1OCTETSTRING),
    ASN1_TAG_OCTET_STRING,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1OctetString_Delete,
    (PFNRTASN1COREVTENUM)RTAsn1OctetString_Enum,
    (PFNRTASN1COREVTCLONE)RTAsn1OctetString_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1OctetString_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1OctetString_CheckSanity,
    RTAsn1OctetString_EncodePrep,
    RTAsn1OctetString_EncodeWrite
};


RTDECL(int) RTAsn1OctetString_Init(PRTASN1OCTETSTRING pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_ZERO(*pThis);

    RTAsn1Core_InitEx(&pThis->Asn1Core, ASN1_TAG_OCTET_STRING, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1OctetString_Vtable, RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    /*pThis->pEncapsulated = NULL;*/
    RTAsn1MemInitAllocation(&pThis->EncapsulatedAllocation, pAllocator);

    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1OctetString_Clone(PRTASN1OCTETSTRING pThis, PCRTASN1OCTETSTRING pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);

    RT_ZERO(*pThis);
    if (RTAsn1OctetString_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1OctetString_Vtable, VERR_INTERNAL_ERROR_3);

        int rc;
        if (!pSrc->pEncapsulated)
            rc = RTAsn1Core_CloneContent(&pThis->Asn1Core, &pSrc->Asn1Core, pAllocator);
        else
            rc = RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
        if (RT_FAILURE(rc))
            return rc;

        RTAsn1MemInitAllocation(&pThis->EncapsulatedAllocation, pAllocator);
        if (pSrc->pEncapsulated)
        {
            PCRTASN1COREVTABLE pOps = pSrc->pEncapsulated->pOps;
            Assert(!pOps || pOps->pfnClone);
            if (pOps && pOps->pfnClone)
            {
                /* We can clone the decoded encapsulated object. */
                rc = RTAsn1MemAllocZ(&pThis->EncapsulatedAllocation, (void **)&pThis->pEncapsulated, pOps->cbStruct);
                if (RT_SUCCESS(rc))
                {
                    rc = pOps->pfnClone(pThis->pEncapsulated, pSrc->pEncapsulated, pAllocator);
                    if (RT_FAILURE(rc))
                        RTAsn1MemFree(&pThis->EncapsulatedAllocation, pThis->pEncapsulated);
                }
            }
            else
            {
                /* Borrow the encapsulated pointer and use RTAsn1OctetString_RefreshContent
                   to get an accurate copy of the bytes. */
                pThis->pEncapsulated = pSrc->pEncapsulated;
                rc = RTAsn1OctetString_RefreshContent(pThis, RTASN1ENCODE_F_DER, pAllocator, NULL);
                pThis->pEncapsulated = NULL;
            }
            if (RT_FAILURE(rc))
            {
                RTAsn1ContentFree(&pThis->Asn1Core);
                RT_ZERO(*pThis);
                return rc;
            }
        }
    }
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1OctetString_Delete(PRTASN1OCTETSTRING pThis)
{
    if (   pThis
        && RTAsn1OctetString_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1OctetString_Vtable);

        /* Destroy the encapsulated object. */
        if (pThis->pEncapsulated)
        {
            RTAsn1VtDelete(pThis->pEncapsulated);
            if (pThis->EncapsulatedAllocation.cbAllocated)
                RTAsn1MemFree(&pThis->EncapsulatedAllocation, pThis->pEncapsulated);
        }

        /* Delete content and wipe the content. */
        RTAsn1ContentFree(&pThis->Asn1Core);
        RT_ZERO(*pThis);
    }
}


RTDECL(int) RTAsn1OctetString_Enum(PRTASN1OCTETSTRING pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    Assert(pThis && (!RTAsn1OctetString_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1OctetString_Vtable));

    /* Enumerate the encapsulated object if present. */
    if (pThis->pEncapsulated)
        return pfnCallback(pThis->pEncapsulated, "Encapsulated", uDepth + 1, pvUser);
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1OctetString_Compare(PCRTASN1OCTETSTRING pLeft, PCRTASN1OCTETSTRING pRight)
{
    Assert(pLeft  && (!RTAsn1OctetString_IsPresent(pLeft)  || pLeft->Asn1Core.pOps  == &g_RTAsn1OctetString_Vtable));
    Assert(pRight && (!RTAsn1OctetString_IsPresent(pRight) || pRight->Asn1Core.pOps == &g_RTAsn1OctetString_Vtable));

    int iDiff;
    if (RTAsn1OctetString_IsPresent(pLeft))
    {
        if (RTAsn1OctetString_IsPresent(pRight))
        {
            /* Since it's really hard to tell whether encapsulated objects have
               been modified or not, we might have to refresh both objects
               while doing this compare.  We'll try our best to avoid it though. */
            if (pLeft->pEncapsulated || pRight->pEncapsulated)
            {
                if (   pLeft->pEncapsulated
                    && pRight->pEncapsulated
                    && pLeft->pEncapsulated->pOps == pRight->pEncapsulated->pOps)
                    iDiff = pLeft->pEncapsulated->pOps->pfnCompare(pLeft->pEncapsulated, pRight->pEncapsulated);
                else
                {
                    /* No direct comparison of encapsulated objects possible,
                       make sure we've got the rigth bytes then.  */
                    if (   pLeft->pEncapsulated
                        && !RTAsn1OctetString_AreContentBytesValid(pLeft, RTASN1ENCODE_F_DER))
                    {
                        int rc = RTAsn1OctetString_RefreshContent((PRTASN1OCTETSTRING)pLeft, RTASN1ENCODE_F_DER,
                                                                  pLeft->EncapsulatedAllocation.pAllocator, NULL);
                        AssertRC(rc);
                    }

                    if (   pRight->pEncapsulated
                        && !RTAsn1OctetString_AreContentBytesValid(pRight, RTASN1ENCODE_F_DER))
                    {
                        int rc = RTAsn1OctetString_RefreshContent((PRTASN1OCTETSTRING)pRight, RTASN1ENCODE_F_DER,
                                                                  pRight->EncapsulatedAllocation.pAllocator, NULL);
                        AssertRC(rc);
                    }

                    /* Compare the content bytes. */
                    iDiff = RTAsn1Core_CompareEx(&pLeft->Asn1Core, &pRight->Asn1Core, true /*fIgnoreTagAndClass*/);
                }
            }
            /*
             * No encapsulated object, just compare the raw content bytes.
             */
            else
                iDiff = RTAsn1Core_CompareEx(&pLeft->Asn1Core, &pRight->Asn1Core, true /*fIgnoreTagAndClass*/);
        }
        else
            iDiff = -1;
    }
    else
        iDiff = 0 - (int)RTAsn1OctetString_IsPresent(pRight);
    return iDiff;
}


RTDECL(int) RTAsn1OctetString_CheckSanity(PCRTASN1OCTETSTRING pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    if (RT_UNLIKELY(!RTAsn1OctetString_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (OCTET STRING).", pszErrorTag);

    if (pThis->pEncapsulated)
        return pThis->pEncapsulated->pOps->pfnCheckSanity(pThis->pEncapsulated, fFlags & RTASN1_CHECK_SANITY_F_COMMON_MASK,
                                                          pErrInfo, pszErrorTag);
    return VINF_SUCCESS;
}


/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-octetstring-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

