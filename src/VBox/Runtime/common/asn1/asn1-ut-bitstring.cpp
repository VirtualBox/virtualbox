/* $Id: asn1-ut-bitstring.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Bit String Type.
 *
 * @remarks This file should remain very similar to asn1-ut-octetstring.cpp.
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
typedef struct RTASN1BITSTRINGWRITERCTX
{
    /** Pointer to the output buffer. */
    uint8_t        *pbBuf;
    /** The current buffer offset. */
    uint32_t        offBuf;
    /** The size of the buffer. */
    uint32_t        cbBuf;
} RTASN1BITSTRINGWRITERCTX;


/** @callback_method_impl{FNRTASN1ENCODEWRITER,
 *  Used to refresh the content of octet and bit strings. } */
static DECLCALLBACK(int) rtAsn1BitStringEncodeWriter(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RTASN1BITSTRINGWRITERCTX *pCtx = (RTASN1BITSTRINGWRITERCTX *)pvUser;
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
static DECLCALLBACK(int) rtAsn1BitStringEncodeCompare(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RTASN1BITSTRINGWRITERCTX *pCtx = (RTASN1BITSTRINGWRITERCTX *)pvUser;
    AssertReturn(cbToWrite <= pCtx->cbBuf - pCtx->offBuf, VERR_BUFFER_OVERFLOW);
    if (memcmp(&pCtx->pbBuf[pCtx->offBuf], pvBuf, cbToWrite) != 0)
        return VERR_NOT_EQUAL;
    pCtx->offBuf += (uint32_t)cbToWrite;
    RT_NOREF_PV(pErrInfo);
    return VINF_SUCCESS;
}



/*
 * ASN.1 BIT STRING - Special Methods.
 */

RTDECL(uint64_t) RTAsn1BitString_GetAsUInt64(PCRTASN1BITSTRING pThis)
{
    /*
     * Extract the first 64 bits in host order.
     */
    uint8_t const  *pb     = pThis->uBits.pu8;
    uint64_t        uRet   = 0;
    uint32_t        cShift = 0;
    uint32_t        cBits  = RT_MIN(pThis->cBits, 64);
    while (cBits > 0)
    {
        uint8_t b = *pb++;
#if 1 /* We don't have a bit-order constant... */
        b = ((b & 0x01) << 7)
          | ((b & 0x02) << 5)
          | ((b & 0x04) << 3)
          | ((b & 0x08) << 1)
          | ((b & 0x10) >> 1)
          | ((b & 0x20) >> 3)
          | ((b & 0x40) >> 5)
          | ((b & 0x80) >> 7);
#endif
        if (cBits < 8)
        {
            b &= RT_BIT_32(cBits) - 1;
            uRet |= (uint64_t)b << cShift;
            break;
        }
        uRet   |= (uint64_t)b << cShift;
        cShift += 8;
        cBits  -= 8;
    }

    return uRet;
}


RTDECL(int) RTAsn1BitString_RefreshContent(PRTASN1BITSTRING pThis, uint32_t fFlags,
                                           PCRTASN1ALLOCATORVTABLE pAllocator, PRTERRINFO pErrInfo)
{
    AssertReturn(pThis->pEncapsulated, VERR_INVALID_STATE);

    uint32_t cbEncoded;
    int rc = RTAsn1EncodePrepare(pThis->pEncapsulated, fFlags, &cbEncoded, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        pThis->Asn1Core.cb = 1 + cbEncoded;
        pThis->cBits       = cbEncoded * 8;
        AssertReturn(pThis->cBits / 8 == cbEncoded, RTErrInfoSetF(pErrInfo, VERR_TOO_MUCH_DATA, "cbEncoded=%#x", cbEncoded));

        rc = RTAsn1ContentReallocZ(&pThis->Asn1Core, cbEncoded + 1, pAllocator);
        if (RT_SUCCESS(rc))
        {
            pThis->uBits.pu8 = pThis->Asn1Core.uData.pu8 + 1;

            /* Initialize the writer context and write the first byte concerning unused bits. */
            RTASN1BITSTRINGWRITERCTX Ctx;
            Ctx.pbBuf  = (uint8_t *)pThis->Asn1Core.uData.pu8;
            Ctx.cbBuf  = cbEncoded + 1;
            Ctx.offBuf = 1;
            *Ctx.pbBuf = 0;

            rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, rtAsn1BitStringEncodeWriter, &Ctx, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                if (Ctx.offBuf == cbEncoded + 1)
                    return VINF_SUCCESS;

                rc = RTErrInfoSetF(pErrInfo, rc, "Expected %#x + 1 bytes, got %#x", cbEncoded, Ctx.offBuf);
            }
        }
        else
            rc = RTErrInfoSetF(pErrInfo, rc, "Error allocating %#x + 1 bytes for storing content\n", cbEncoded);
    }
    return rc;
}


RTDECL(bool) RTAsn1BitString_AreContentBitsValid(PCRTASN1BITSTRING pThis, uint32_t fFlags)
{
    if (pThis->pEncapsulated)
    {
        if (pThis->cBits & 7)
            return false;

        /* Check the encoded length of the bits. */
        uint32_t cbEncoded;
        int rc = RTAsn1EncodePrepare(pThis->pEncapsulated, fFlags, &cbEncoded, NULL);
        if (RT_FAILURE(rc))
            return false;
        if (pThis->Asn1Core.cb != 1 + cbEncoded)
            return false;

        /* Check the encoded bits, if there are any. */
        if (cbEncoded)
        {
            if (!pThis->Asn1Core.uData.pv)
                return false;

            /* Check the first byte, the unused bit count. */
            if (*pThis->Asn1Core.uData.pu8 != 0)
                return false;

            /* Check the other bytes. */
            RTASN1BITSTRINGWRITERCTX Ctx;
            Ctx.pbBuf  = (uint8_t *)pThis->Asn1Core.uData.pu8;
            Ctx.cbBuf  = cbEncoded + 1;
            Ctx.offBuf = 1;
            rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, rtAsn1BitStringEncodeCompare, &Ctx, NULL);
            if (RT_FAILURE(rc))
                return false;
        }
    }
    return true;
}




/*
 * ASN.1 BIT STRING - Standard Methods.
 */

/** @interface_method_impl{FNRTASN1COREVTENCODEPREP} */
static DECLCALLBACK(int) RTAsn1BitString_EncodePrep(PRTASN1CORE pThisCore, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    PRTASN1BITSTRING pThis = (PRTASN1BITSTRING)pThisCore;
    if (!pThis->pEncapsulated)
    {
        Assert(pThis->cBits == 0 || pThis->Asn1Core.uData.pv);
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
            bool fMustFree = pThis->Asn1Core.cb != 1 + cbEncoded || (pThis->cBits & 7);
            if (!fMustFree)
            {
                RTASN1BITSTRINGWRITERCTX Ctx;
                Ctx.pbBuf  = (uint8_t *)pThis->Asn1Core.uData.pu8;
                Ctx.cbBuf  = 1 + cbEncoded;
                Ctx.offBuf = 1;
                fMustFree  = *Ctx.pbBuf != 0;
                if (!fMustFree)
                {
                    rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, rtAsn1BitStringEncodeCompare, &Ctx, NULL);
                    fMustFree = RT_FAILURE_NP(rc);
                }
            }
            if (fMustFree)
            {
                pThis->uBits.pv = NULL;
                RTAsn1ContentFree(&pThis->Asn1Core);
            }
        }
        pThis->Asn1Core.cb = 1 + cbEncoded;
        pThis->cBits       = cbEncoded * 8;

        rc = RTAsn1EncodeRecalcHdrSize(&pThis->Asn1Core, fFlags, pErrInfo);
    }
    return rc;
}


/** @interface_method_impl{FNRTASN1COREVTENCODEWRITE} */
static DECLCALLBACK(int) RTAsn1BitString_EncodeWrite(PRTASN1CORE pThisCore, uint32_t fFlags, PFNRTASN1ENCODEWRITER pfnWriter,
                                                     void *pvUser, PRTERRINFO pErrInfo)
{
    PRTASN1BITSTRING pThis = (PRTASN1BITSTRING)pThisCore;

    AssertReturn(RT_ALIGN(pThis->cBits, 8) / 8 + 1 == pThis->Asn1Core.cb, VERR_INTERNAL_ERROR_3);

    /*
     * First the header.
     */
    int rc = RTAsn1EncodeWriteHeader(&pThis->Asn1Core, fFlags, pfnWriter, pvUser, pErrInfo);
    if (RT_SUCCESS(rc) && rc != VINF_ASN1_NOT_ENCODED)
    {
        /*
         * The content starts with an unused bit count. Calculate it in case we
         * need to write it out.
         */
        uint8_t cUnusedBits = 0;
        if ((pThis->cBits & 7) != 0)
            cUnusedBits = 8 - (pThis->cBits & 7);

        /*
         * If nothing is encapsulated, the core points to the content (if we have any).
         */
        if (!pThis->pEncapsulated)
        {
            if (pThis->cBits > 0)
            {
                Assert(pThis->Asn1Core.uData.pu8[0] == cUnusedBits);
                rc = pfnWriter(pThis->Asn1Core.uData.pu8, pThis->Asn1Core.cb, pvUser, pErrInfo);
            }
            else
                rc = pfnWriter(&cUnusedBits, sizeof(cUnusedBits), pvUser, pErrInfo);
        }
        /*
         * Write the unused bit count and then call upon the encapsulated
         * content to serialize itself.
         */
        else
        {
            rc = pfnWriter(&cUnusedBits, sizeof(cUnusedBits), pvUser, pErrInfo);
            if (RT_SUCCESS(rc))
                rc = RTAsn1EncodeWrite(pThis->pEncapsulated, fFlags, pfnWriter, pvUser, pErrInfo);
        }
    }
    return rc;
}


RT_DECL_DATA_CONST(RTASN1COREVTABLE const) g_RTAsn1BitString_Vtable =
{
    "RTAsn1BitString",
    sizeof(RTASN1BITSTRING),
    ASN1_TAG_BIT_STRING,
    ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
    0,
    (PFNRTASN1COREVTDTOR)RTAsn1BitString_Delete,
    (PFNRTASN1COREVTENUM)RTAsn1BitString_Enum,
    (PFNRTASN1COREVTCLONE)RTAsn1BitString_Clone,
    (PFNRTASN1COREVTCOMPARE)RTAsn1BitString_Compare,
    (PFNRTASN1COREVTCHECKSANITY)RTAsn1BitString_CheckSanity,
    RTAsn1BitString_EncodePrep,
    RTAsn1BitString_EncodeWrite
};


RTDECL(int) RTAsn1BitString_Init(PRTASN1BITSTRING pThis, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RT_ZERO(*pThis);

    RTAsn1Core_InitEx(&pThis->Asn1Core, ASN1_TAG_BIT_STRING, ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_PRIMITIVE,
                      &g_RTAsn1BitString_Vtable, RTASN1CORE_F_PRESENT | RTASN1CORE_F_PRIMITE_TAG_STRUCT);
    /*pThis->cBits = 0;
    pThis->cMaxBits = 0;
    pThis->uBits.pv = NULL;
    pThis->pEncapsulated = NULL; */
    RTAsn1MemInitAllocation(&pThis->EncapsulatedAllocation, pAllocator);

    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1BitString_InitWithData(PRTASN1BITSTRING pThis, void const *pvSrc, uint32_t cSrcBits,
                                         PCRTASN1ALLOCATORVTABLE pAllocator)
{
    RTAsn1BitString_Init(pThis, pAllocator);
    Assert(pThis->pEncapsulated == NULL);

    uint32_t cbToCopy = (cSrcBits + 7) / 8;
    int rc = RTAsn1ContentAllocZ(&pThis->Asn1Core, cbToCopy + 1, pAllocator);
    if (RT_SUCCESS(rc))
    {
        pThis->cBits    = cSrcBits;
        uint8_t *pbDst  = (uint8_t *)pThis->Asn1Core.uData.pu8;
        pThis->uBits.pv = pbDst + 1;
        *pbDst = (cSrcBits & 7) != 0 ? 8 - (cSrcBits & 7) : 0; /* unused bits */
        memcpy(pbDst + 1, pvSrc, cbToCopy);
    }
    return rc;
}


RTDECL(int) RTAsn1BitString_Clone(PRTASN1BITSTRING pThis, PCRTASN1BITSTRING pSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtr(pSrc); AssertPtr(pThis); AssertPtr(pAllocator);

    RT_ZERO(*pThis);
    if (RTAsn1BitString_IsPresent(pSrc))
    {
        AssertReturn(pSrc->Asn1Core.pOps == &g_RTAsn1BitString_Vtable, VERR_INTERNAL_ERROR_3);

        int rc;
        if (!pSrc->pEncapsulated)
            rc = RTAsn1Core_CloneContent(&pThis->Asn1Core, &pSrc->Asn1Core, pAllocator);
        else
            rc = RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
        if (RT_FAILURE(rc))
            return rc;

        RTAsn1MemInitAllocation(&pThis->EncapsulatedAllocation, pAllocator);
        pThis->cBits    = pSrc->cBits;
        pThis->cMaxBits = pSrc->cMaxBits;
        if (!pSrc->pEncapsulated)
            pThis->uBits.pv = pThis->Asn1Core.uData.pu8 ? pThis->Asn1Core.uData.pu8 + 1 : NULL;
        else
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
                /* Borrow the encapsulated pointer and use RTAsn1BitString_RefreshContent
                   to get an accurate copy of the bytes. */
                pThis->pEncapsulated = pSrc->pEncapsulated;
                rc = RTAsn1BitString_RefreshContent(pThis, RTASN1ENCODE_F_DER, pAllocator, NULL);
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


RTDECL(void) RTAsn1BitString_Delete(PRTASN1BITSTRING pThis)
{
    if (   pThis
        && RTAsn1BitString_IsPresent(pThis))
    {
        Assert(pThis->Asn1Core.pOps == &g_RTAsn1BitString_Vtable);

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


RTDECL(int) RTAsn1BitString_Enum(PRTASN1BITSTRING pThis, PFNRTASN1ENUMCALLBACK pfnCallback, uint32_t uDepth, void *pvUser)
{
    Assert(pThis && (!RTAsn1BitString_IsPresent(pThis) || pThis->Asn1Core.pOps == &g_RTAsn1BitString_Vtable));

    /* Enumerate the encapsulated object if present. */
    if (pThis->pEncapsulated)
        return pfnCallback(pThis->pEncapsulated, "Encapsulated", uDepth + 1, pvUser);
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1BitString_Compare(PCRTASN1BITSTRING pLeft, PCRTASN1BITSTRING pRight)
{
    Assert(pLeft  && (!RTAsn1BitString_IsPresent(pLeft)  || pLeft->Asn1Core.pOps  == &g_RTAsn1BitString_Vtable));
    Assert(pRight && (!RTAsn1BitString_IsPresent(pRight) || pRight->Asn1Core.pOps == &g_RTAsn1BitString_Vtable));

    int iDiff;
    if (RTAsn1BitString_IsPresent(pLeft))
    {
        if (RTAsn1BitString_IsPresent(pRight))
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
                        && !RTAsn1BitString_AreContentBitsValid(pLeft, RTASN1ENCODE_F_DER))
                    {
                        int rc = RTAsn1BitString_RefreshContent((PRTASN1BITSTRING)pLeft, RTASN1ENCODE_F_DER,
                                                                pLeft->EncapsulatedAllocation.pAllocator, NULL);
                        AssertRC(rc);
                    }

                    if (   pRight->pEncapsulated
                        && !RTAsn1BitString_AreContentBitsValid(pRight, RTASN1ENCODE_F_DER))
                    {
                        int rc = RTAsn1BitString_RefreshContent((PRTASN1BITSTRING)pRight, RTASN1ENCODE_F_DER,
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
        iDiff = 0 - (int)RTAsn1BitString_IsPresent(pRight);
    return iDiff;
}


RTDECL(int) RTAsn1BitString_CheckSanity(PCRTASN1BITSTRING pThis, uint32_t fFlags, PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    if (RT_UNLIKELY(!RTAsn1BitString_IsPresent(pThis)))
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Missing (BIT STRING).", pszErrorTag);

    if (pThis->cBits > pThis->cMaxBits)
        return RTErrInfoSetF(pErrInfo, VERR_ASN1_BITSTRING_OUT_OF_BOUNDS, "%s: Exceeding max bits: cBits=%u cMaxBits=%u.",
                             pszErrorTag, pThis->cBits, pThis->cMaxBits);

    if (pThis->pEncapsulated)
        return pThis->pEncapsulated->pOps->pfnCheckSanity(pThis->pEncapsulated, fFlags & RTASN1_CHECK_SANITY_F_COMMON_MASK,
                                                          pErrInfo, pszErrorTag);
    return VINF_SUCCESS;
}

/*
 * Generate code for the associated collection types.
 */
#define RTASN1TMPL_TEMPLATE_FILE "../common/asn1/asn1-ut-bitstring-template.h"
#include <iprt/asn1-generator-internal-header.h>
#include <iprt/asn1-generator-core.h>
#include <iprt/asn1-generator-init.h>
#include <iprt/asn1-generator-sanity.h>

