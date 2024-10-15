/* $Id: asn1-encode.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Encoding.
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

#include <iprt/assert.h>
#include <iprt/bignum.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Argument package for rtAsn1EncodePrepareCallback passed by RTAsn1EncodePrepare.
 */
typedef struct RTASN1ENCODEPREPARGS
{
    /** The size at this level. */
    uint32_t        cb;
    /** RTASN1ENCODE_F_XXX. */
    uint32_t        fFlags;
    /** Pointer to the error info. (optional) */
    PRTERRINFO      pErrInfo;
} RTASN1ENCODEPREPARGS;


/**
 * Argument package for rtAsn1EncodeWriteCallback passed by RTAsn1EncodeWrite.
 */
typedef struct RTASN1ENCODEWRITEARGS
{
    /** RTASN1ENCODE_F_XXX. */
    uint32_t                fFlags;
    /** Pointer to the writer funtion. */
    PFNRTASN1ENCODEWRITER   pfnWriter;
    /** User argument to the writer function. */
    void                   *pvUser;
    /** Pointer to the error info. (optional) */
    PRTERRINFO              pErrInfo;
} RTASN1ENCODEWRITEARGS;

/**
 * Argument package for rtAsn1EncodeToBufferCallback passed by
 * RTAsn1EncodeToBuffer.
 */
typedef struct RTASN1ENCODETOBUFARGS
{
    /** The destination buffer position (incremented while writing). */
    uint8_t                *pbDst;
    /** The size of the destination buffer left (decremented while writing). */
    size_t                  cbDst;
} RTASN1ENCODETOBUFARGS;


RTDECL(int) RTAsn1EncodeRecalcHdrSize(PRTASN1CORE pAsn1Core, uint32_t fFlags, PRTERRINFO pErrInfo)
{
    AssertReturn((fFlags & RTASN1ENCODE_F_RULE_MASK) == RTASN1ENCODE_F_DER, VERR_INVALID_FLAGS);
    int rc = VINF_SUCCESS;

    uint8_t cbHdr;
    if ((pAsn1Core->fFlags & (RTASN1CORE_F_PRESENT | RTASN1CORE_F_DUMMY | RTASN1CORE_F_DEFAULT)) == RTASN1CORE_F_PRESENT)
    {
        /*
         * The minimum header size is two bytes.
         */
        cbHdr = 2;

        /*
         * Add additional bytes for encoding the tag.
         */
        uint32_t uTag = pAsn1Core->uTag;
        if (uTag >= ASN1_TAG_USE_LONG_FORM)
        {
            AssertReturn(pAsn1Core->uTag != UINT32_MAX, RTErrInfoSet(pErrInfo, VERR_ASN1_DUMMY_OBJECT, "uTag=UINT32_MAX"));
            do
            {
                cbHdr++;
                uTag >>= 7;
            } while (uTag > 0);
        }

        /*
         * Add additional bytes for encoding the content length.
         */
        uint32_t cb = pAsn1Core->cb;
        if (cb >= 0x80)
        {
            AssertReturn(cb < _1G, RTErrInfoSetF(pErrInfo, VERR_ASN1_TOO_LONG, "cb=%u (%#x)", cb, cb));

            if (cb <= UINT32_C(0xffff))
            {
                if (cb <= UINT32_C(0xff))
                    cbHdr += 1;
                else
                    cbHdr += 2;
            }
            else
            {
                if (cb <= UINT32_C(0xffffff))
                    cbHdr += 3;
                else
                    cbHdr += 4;
            }
        }
    }
    /*
     * Not present, dummy or otherwise not encoded.
     */
    else
    {
        cbHdr = 0;
        if (pAsn1Core->fFlags & RTASN1CORE_F_DEFAULT)
            rc = VINF_ASN1_NOT_ENCODED;
        else
        {
            Assert(RTASN1CORE_IS_DUMMY(pAsn1Core));
            Assert(pAsn1Core->pOps && pAsn1Core->pOps->pfnEnum);
            rc = VINF_SUCCESS;
        }
    }

    /*
     * Update the header length.
     */
    pAsn1Core->cbHdr = cbHdr;
    return rc;
}


/**
 * @callback_method_impl{FNRTASN1ENUMCALLBACK}
 */
static DECLCALLBACK(int) rtAsn1EncodePrepareCallback(PRTASN1CORE pAsn1Core, const char *pszName, uint32_t uDepth, void *pvUser)
{
    RTASN1ENCODEPREPARGS *pArgs = (RTASN1ENCODEPREPARGS *)pvUser;
    RT_NOREF_PV(pszName);
    if (RTASN1CORE_IS_PRESENT(pAsn1Core))
    {
        /*
         * Depth first, where relevant.
         */
        uint32_t const cbSaved = pArgs->cb;
        if (pAsn1Core->pOps)
        {
            /*
             * Use the encoding preparation method when available.
             */
            int rc;
            if (pAsn1Core->pOps->pfnEncodePrep)
                rc = pAsn1Core->pOps->pfnEncodePrep(pAsn1Core, pArgs->fFlags, pArgs->pErrInfo);
            else if (pAsn1Core->pOps->pfnEnum)
            {
                /*
                 * Recurse to prepare the child objects (if any).
                 */
                rc = pAsn1Core->pOps->pfnEnum(pAsn1Core, rtAsn1EncodePrepareCallback, uDepth + 1, pArgs);
                if (RT_SUCCESS(rc))
                    pAsn1Core->cb = pArgs->cb - cbSaved;
            }
            else
            {
                /*
                 * Must be a primitive type if DER.
                 */
                if (   (pAsn1Core->fClass & ASN1_TAGFLAG_CONSTRUCTED)
                    && (pArgs->fFlags & RTASN1ENCODE_F_DER) )
                    return RTErrInfoSetF(pArgs->pErrInfo, VERR_ASN1_EXPECTED_PRIMITIVE,
                                         "Expected primitive ASN.1 object: uTag=%#x fClass=%#x cb=%u",
                                         RTASN1CORE_GET_TAG(pAsn1Core), pAsn1Core->fClass, pAsn1Core->cb);
                rc = VINF_SUCCESS;
            }
            if (RT_SUCCESS(rc))
                rc = RTAsn1EncodeRecalcHdrSize(pAsn1Core, pArgs->fFlags, pArgs->pErrInfo);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
        {
            AssertFailed();
            pAsn1Core->cb    = 0;
            pAsn1Core->cbHdr = 0;
        }

        /*
         * Recalculate the output size, thus far.  Dummy objects propagates the
         * content size, but the header size is zero.  Other objects with
         * header size zero are not encoded and should be omitted entirely.
         */
        if (pAsn1Core->cbHdr > 0 || RTASN1CORE_IS_DUMMY(pAsn1Core))
            pArgs->cb = RTASN1CORE_GET_RAW_ASN1_SIZE(pAsn1Core) + cbSaved;
        else
            pArgs->cb = cbSaved;
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1EncodePrepare(PRTASN1CORE pRoot, uint32_t fFlags, uint32_t *pcbEncoded, PRTERRINFO pErrInfo)
{
    AssertReturn((fFlags & RTASN1ENCODE_F_RULE_MASK) == RTASN1ENCODE_F_DER, VERR_INVALID_FLAGS);

    /*
     * This is implemented as a recursive enumeration of the ASN.1 object structure.
     */
    RTASN1ENCODEPREPARGS Args;
    Args.cb       = 0;
    Args.fFlags   = fFlags;
    Args.pErrInfo = pErrInfo;
    int rc = rtAsn1EncodePrepareCallback(pRoot, "root", 0, &Args);
    if (pcbEncoded)
        *pcbEncoded = RTASN1CORE_GET_RAW_ASN1_SIZE(pRoot);
    return rc;
}


RTDECL(int) RTAsn1EncodeWriteHeader(PCRTASN1CORE pAsn1Core, uint32_t fFlags, FNRTASN1ENCODEWRITER pfnWriter, void *pvUser,
                                    PRTERRINFO pErrInfo)
{
    AssertReturn((fFlags & RTASN1ENCODE_F_RULE_MASK) == RTASN1ENCODE_F_DER, VERR_INVALID_FLAGS);

    if ((pAsn1Core->fFlags & (RTASN1CORE_F_PRESENT | RTASN1CORE_F_DUMMY | RTASN1CORE_F_DEFAULT)) == RTASN1CORE_F_PRESENT)
    {
        uint8_t  abHdr[16];  /* 2 + max 5 tag + max 4 length = 11 */
        uint8_t *pbDst = &abHdr[0];

        /*
         * Encode the tag.
         */
        uint32_t uTag = pAsn1Core->uTag;
        if (uTag < ASN1_TAG_USE_LONG_FORM)
            *pbDst++ = (uint8_t)uTag | (pAsn1Core->fClass & ~ASN1_TAG_MASK);
        else
        {
            AssertReturn(pAsn1Core->uTag != UINT32_MAX, RTErrInfoSet(pErrInfo, VERR_ASN1_DUMMY_OBJECT, "uTag=UINT32_MAX"));

            /* In the long form, the tag is encoded MSB style with the 8th bit
               of each byte indicating the whether there are more byte. */
            *pbDst++ = ASN1_TAG_USE_LONG_FORM | (pAsn1Core->fClass & ~ASN1_TAG_MASK);
            if (uTag <= UINT32_C(0x7f))
                *pbDst++ = uTag;
            else if (uTag <= UINT32_C(0x3fff))  /* 2**(7*2) = 0x4000 (16384) */
            {
                *pbDst++ = (uTag >> 7) | 0x80;
                *pbDst++ = uTag & 0x7f;
            }
            else if (uTag <= UINT32_C(0x1fffff)) /* 2**(7*3) = 0x200000 (2097152) */
            {
                *pbDst++ =  (uTag >> 14)         | 0x80;
                *pbDst++ = ((uTag >>  7) & 0x7f) | 0x80;
                *pbDst++ =   uTag        & 0x7f;
            }
            else if (uTag <= UINT32_C(0xfffffff)) /* 2**(7*4) = 0x10000000 (268435456) */
            {
                *pbDst++ =  (uTag >> 21)         | 0x80;
                *pbDst++ = ((uTag >> 14) & 0x7f) | 0x80;
                *pbDst++ = ((uTag >>  7) & 0x7f) | 0x80;
                *pbDst++ =   uTag        & 0x7f;
            }
            else
            {
                *pbDst++ =  (uTag >> 28)         | 0x80;
                *pbDst++ = ((uTag >> 21) & 0x7f) | 0x80;
                *pbDst++ = ((uTag >> 14) & 0x7f) | 0x80;
                *pbDst++ = ((uTag >>  7) & 0x7f) | 0x80;
                *pbDst++ =   uTag        & 0x7f;
            }
        }

        /*
         * Encode the length.
         */
        uint32_t cb = pAsn1Core->cb;
        if (cb < 0x80)
            *pbDst++ = (uint8_t)cb;
        else
        {
            AssertReturn(cb < _1G, RTErrInfoSetF(pErrInfo, VERR_ASN1_TOO_LONG, "cb=%u (%#x)", cb, cb));

            if (cb <= UINT32_C(0xffff))
            {
                if (cb <= UINT32_C(0xff))
                {
                    pbDst[0] = 0x81;
                    pbDst[1] = (uint8_t)cb;
                    pbDst += 2;
                }
                else
                {
                    pbDst[0] = 0x82;
                    pbDst[1] = cb >> 8;
                    pbDst[2] = (uint8_t)cb;
                    pbDst += 3;
                }
            }
            else
            {
                if (cb <= UINT32_C(0xffffff))
                {
                    pbDst[0] = 0x83;
                    pbDst[1] = (uint8_t)(cb >> 16);
                    pbDst[2] = (uint8_t)(cb >> 8);
                    pbDst[3] = (uint8_t)cb;
                    pbDst += 4;
                }
                else
                {
                    pbDst[0] = 0x84;
                    pbDst[1] = (uint8_t)(cb >> 24);
                    pbDst[2] = (uint8_t)(cb >> 16);
                    pbDst[3] = (uint8_t)(cb >> 8);
                    pbDst[4] = (uint8_t)cb;
                    pbDst += 5;
                }
            }
        }

        size_t const cbHdr = pbDst - &abHdr[0];
        Assert(sizeof(abHdr) >= cbHdr);
        Assert(pAsn1Core->cbHdr == cbHdr);

        /*
         * Write it.
         */
        return pfnWriter(abHdr, cbHdr, pvUser, pErrInfo);
    }

    /*
     * Not present, dummy or otherwise not encoded.
     */
    Assert(pAsn1Core->cbHdr == 0);
    if (pAsn1Core->fFlags & RTASN1CORE_F_DEFAULT)
        return VINF_ASN1_NOT_ENCODED;
    Assert(RTASN1CORE_IS_DUMMY(pAsn1Core));
    Assert(pAsn1Core->pOps && pAsn1Core->pOps->pfnEnum);
    return  VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNRTASN1ENUMCALLBACK}
 */
static DECLCALLBACK(int) rtAsn1EncodeWriteCallback(PRTASN1CORE pAsn1Core, const char *pszName, uint32_t uDepth, void *pvUser)
{
    RTASN1ENCODEWRITEARGS *pArgs = (RTASN1ENCODEWRITEARGS *)pvUser;
    RT_NOREF_PV(pszName);
    int rc;
    if (RTASN1CORE_IS_PRESENT(pAsn1Core))
    {
        /*
         * If there is an write method, use it.
         */
        if (   pAsn1Core->pOps
            && pAsn1Core->pOps->pfnEncodeWrite)
            rc = pAsn1Core->pOps->pfnEncodeWrite(pAsn1Core, pArgs->fFlags, pArgs->pfnWriter, pArgs->pvUser, pArgs->pErrInfo);
        else
        {
            /*
             * Generic path. Start by writing the header for this object.
             */
            rc = RTAsn1EncodeWriteHeader(pAsn1Core, pArgs->fFlags, pArgs->pfnWriter, pArgs->pvUser, pArgs->pErrInfo);
            if (RT_SUCCESS(rc))
            {
                /*
                 * If there is an enum function, call it to assemble the content.
                 * Otherwise ASSUME the pointer in the header points to the content.
                 */
                if (   pAsn1Core->pOps
                    && pAsn1Core->pOps->pfnEnum)
                {
                    if (rc != VINF_ASN1_NOT_ENCODED)
                        rc = pAsn1Core->pOps->pfnEnum(pAsn1Core, rtAsn1EncodeWriteCallback, uDepth + 1, pArgs);
                }
                else if (pAsn1Core->cb && rc != VINF_ASN1_NOT_ENCODED)
                {
                    Assert(!RTASN1CORE_IS_DUMMY(pAsn1Core));
                    AssertPtrReturn(pAsn1Core->uData.pv,
                                    RTErrInfoSetF(pArgs->pErrInfo, VERR_ASN1_INVALID_DATA_POINTER,
                                                  "Invalid uData pointer %p for no pfnEnum object with %#x bytes of content",
                                                  pAsn1Core->uData.pv, pAsn1Core->cb));
                    rc = pArgs->pfnWriter(pAsn1Core->uData.pv, pAsn1Core->cb, pArgs->pvUser, pArgs->pErrInfo);
                }
            }
        }
        if (RT_SUCCESS(rc))
            rc = VINF_SUCCESS;
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


RTDECL(int) RTAsn1EncodeWrite(PCRTASN1CORE pRoot, uint32_t fFlags, FNRTASN1ENCODEWRITER pfnWriter, void *pvUser,
                              PRTERRINFO pErrInfo)
{
    AssertReturn((fFlags & RTASN1ENCODE_F_RULE_MASK) == RTASN1ENCODE_F_DER, VERR_INVALID_FLAGS);

    /*
     * This is implemented as a recursive enumeration of the ASN.1 object structure.
     */
    RTASN1ENCODEWRITEARGS Args;
    Args.fFlags     = fFlags;
    Args.pfnWriter  = pfnWriter;
    Args.pvUser     = pvUser;
    Args.pErrInfo   = pErrInfo;
    return rtAsn1EncodeWriteCallback((PRTASN1CORE)pRoot, "root", 0, &Args);
}


static DECLCALLBACK(int) rtAsn1EncodeToBufferCallback(const void *pvBuf, size_t cbToWrite, void *pvUser, PRTERRINFO pErrInfo)
{
    RTASN1ENCODETOBUFARGS *pArgs = (RTASN1ENCODETOBUFARGS *)pvUser;
    if (RT_LIKELY(pArgs->cbDst >= cbToWrite))
    {
        memcpy(pArgs->pbDst, pvBuf, cbToWrite);
        pArgs->cbDst -= cbToWrite;
        pArgs->pbDst += cbToWrite;
        return VINF_SUCCESS;
    }

    /*
     * Overflow.
     */
    if (pArgs->cbDst)
    {
        memcpy(pArgs->pbDst, pvBuf, pArgs->cbDst);
        pArgs->pbDst -= pArgs->cbDst;
        pArgs->cbDst  = 0;
    }
    RT_NOREF_PV(pErrInfo);
    return VERR_BUFFER_OVERFLOW;
}


RTDECL(int) RTAsn1EncodeToBuffer(PCRTASN1CORE pRoot, uint32_t fFlags, void *pvBuf, size_t cbBuf, PRTERRINFO pErrInfo)
{
    RTASN1ENCODETOBUFARGS Args;
    Args.pbDst = (uint8_t *)pvBuf;
    Args.cbDst = cbBuf;
    return RTAsn1EncodeWrite(pRoot, fFlags, rtAsn1EncodeToBufferCallback, &Args, pErrInfo);
}


RTDECL(int) RTAsn1EncodeQueryRawBits(PRTASN1CORE pRoot, const uint8_t **ppbRaw, uint32_t *pcbRaw,
                                     void **ppvFree, PRTERRINFO pErrInfo)
{
    /*
     * ASSUME that if we've got pointers here, they are valid...
     */
    if (   pRoot->uData.pv
        && !(pRoot->fFlags & RTASN1CORE_F_INDEFINITE_LENGTH) /* BER, not DER. */
        && (pRoot->fFlags & RTASN1CORE_F_DECODED_CONTENT) )
    {
        /** @todo Check that it's DER encoding. */
        *ppbRaw  = RTASN1CORE_GET_RAW_ASN1_PTR(pRoot);
        *pcbRaw  = RTASN1CORE_GET_RAW_ASN1_SIZE(pRoot);
        *ppvFree = NULL;
        return VINF_SUCCESS;
    }

    /*
     * Encode it into a temporary heap buffer.
     */
    uint32_t cbEncoded = 0;
    int rc = RTAsn1EncodePrepare(pRoot, RTASN1ENCODE_F_DER, &cbEncoded, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        void *pvEncoded = RTMemTmpAllocZ(cbEncoded);
        if (pvEncoded)
        {
            rc = RTAsn1EncodeToBuffer(pRoot, RTASN1ENCODE_F_DER, pvEncoded, cbEncoded, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                *ppvFree = pvEncoded;
                *ppbRaw  = (unsigned char *)pvEncoded;
                *pcbRaw  = cbEncoded;
                return VINF_SUCCESS;
            }
            RTMemTmpFree(pvEncoded);
        }
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_NO_TMP_MEMORY, "RTMemTmpAllocZ(%u)", cbEncoded);
    }

    *ppvFree = NULL;
    *ppbRaw  = NULL;
    *pcbRaw  = 0;
    return rc;
}

