/* $Id: digest-core.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Cryptographic Hash / Message Digest API
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
#include <iprt/crypto/digest.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/crypto/x509.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Generic message digest instance.
 */
typedef struct RTCRDIGESTINT
{
    /** Magic value (RTCRDIGESTINT_MAGIC). */
    uint32_t            u32Magic;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** Pointer to the message digest descriptor. */
    PCRTCRDIGESTDESC    pDesc;
    /** The offset into abState of the storage space .  At
     * least RTCRDIGESTDESC::cbHash bytes is available at that location. */
    uint32_t            offHash;
    /** State. */
    uint32_t            uState;
    /** The number of bytes consumed. */
    uint64_t            cbConsumed;
    /** Pointer to the data specific to the message digest algorithm. Points
     * either to &abState[0] or to memory allocated with pDesc->pfnNew. */
    void               *pvState;
    /** Opaque data specific to the message digest algorithm, size given by
     * RTCRDIGESTDESC::cbState.  This is followed by space for the final hash
     * at offHash with size RTCRDIGESTDESC::cbHash.  The data specific to the
     * message digest algorithm can also be 0. In this case, pDesc->pfnNew()
     * and pDesc->pfnFree() must not be NULL. */
    uint8_t             abState[1];
} RTCRDIGESTINT;
/** Pointer to a message digest instance. */
typedef RTCRDIGESTINT *PRTCRDIGESTINT;

/** Magic value for RTCRDIGESTINT::u32Magic (Ralph C. Merkle). */
#define RTCRDIGESTINT_MAGIC         UINT32_C(0x19520202)

/** @name RTCRDIGESTINT::uState values.
 * @{ */
/** Ready for more data. */
#define RTCRDIGEST_STATE_READY      UINT32_C(1)
/** The hash has been finalized and can be found at offHash. */
#define RTCRDIGEST_STATE_FINAL      UINT32_C(2)
/** Busted state, can happen after re-init. */
#define RTCRDIGEST_STATE_BUSTED     UINT32_C(3)
/** @} */



/**
 * Used for successful returns which wants to hit at digest security.
 *
 * @retval  VINF_SUCCESS
 * @retval  VINF_CR_DIGEST_DEPRECATED
 * @retval  VINF_CR_DIGEST_COMPROMISED
 * @retval  VINF_CR_DIGEST_SEVERELY_COMPROMISED
 * @param   pDesc               The digest descriptor.
 */
DECLINLINE(int) rtCrDigestSuccessWithDigestWarnings(PCRTCRDIGESTDESC pDesc)
{
    uint32_t const fFlags = pDesc->fFlags
                          & (RTCRDIGESTDESC_F_DEPRECATED | RTCRDIGESTDESC_F_COMPROMISED | RTCRDIGESTDESC_F_SERVERELY_COMPROMISED);
    if (!fFlags)
        return VINF_SUCCESS;
    if (fFlags & RTCRDIGESTDESC_F_SERVERELY_COMPROMISED)
        return VINF_CR_DIGEST_SEVERELY_COMPROMISED;
    if (fFlags & RTCRDIGESTDESC_F_COMPROMISED)
        return VINF_CR_DIGEST_COMPROMISED;
    return VINF_CR_DIGEST_DEPRECATED;
}


RTDECL(int) RTCrDigestCreate(PRTCRDIGEST phDigest, PCRTCRDIGESTDESC pDesc, void *pvOpaque)
{
    AssertPtrReturn(phDigest, VERR_INVALID_POINTER);
    AssertPtrReturn(pDesc, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    uint32_t const offHash = RT_ALIGN_32(pDesc->cbState, 8);
    AssertReturn(pDesc->pfnNew || offHash, VERR_INVALID_PARAMETER);
    AssertReturn(!pDesc->pfnNew || (pDesc->pfnFree && pDesc->pfnInit && pDesc->pfnClone), VERR_INVALID_PARAMETER);
    PRTCRDIGESTINT pThis = (PRTCRDIGESTINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTCRDIGESTINT, abState[offHash + pDesc->cbHash]));
    if (pThis)
    {
        if (pDesc->pfnNew)
            pThis->pvState = pDesc->pfnNew();
        else
            pThis->pvState = &pThis->abState[0];
        if (pThis->pvState)
        {
            pThis->u32Magic = RTCRDIGESTINT_MAGIC;
            pThis->cRefs    = 1;
            pThis->offHash  = offHash;
            pThis->pDesc    = pDesc;
            pThis->uState   = RTCRDIGEST_STATE_READY;
            if (pDesc->pfnInit)
                rc = pDesc->pfnInit(pThis->pvState, pvOpaque, false /*fReInit*/);
            if (RT_SUCCESS(rc))
            {
                *phDigest = pThis;
                return rtCrDigestSuccessWithDigestWarnings(pDesc);
            }
            if (pDesc->pfnFree)
                pDesc->pfnFree(pThis->pvState);
        }
        else
            rc = VERR_NO_MEMORY;
        pThis->u32Magic = 0;
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


RTDECL(int) RTCrDigestClone(PRTCRDIGEST phDigest, RTCRDIGEST hSrc)
{
    AssertPtrReturn(phDigest, VERR_INVALID_POINTER);
    AssertPtrReturn(hSrc, VERR_INVALID_HANDLE);
    AssertReturn(hSrc->u32Magic == RTCRDIGESTINT_MAGIC, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    uint32_t const offHash = hSrc->offHash;
    PRTCRDIGESTINT pThis = (PRTCRDIGESTINT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTCRDIGESTINT, abState[offHash + hSrc->pDesc->cbHash]));
    if (pThis)
    {
        if (hSrc->pDesc->pfnNew)
            pThis->pvState = hSrc->pDesc->pfnNew();
        else
            pThis->pvState = &pThis->abState[0];
        if (pThis->pvState)
        {
            pThis->u32Magic = RTCRDIGESTINT_MAGIC;
            pThis->cRefs    = 1;
            pThis->offHash  = offHash;
            pThis->pDesc    = hSrc->pDesc;
            if (hSrc->pDesc->pfnClone)
                rc = hSrc->pDesc->pfnClone(pThis->pvState, hSrc->pvState);
            else
            {
                Assert(!hSrc->pDesc->pfnNew);
                memcpy(pThis->pvState, hSrc->pvState, offHash);
            }
            memcpy(&pThis->abState[offHash], &hSrc->abState[offHash], hSrc->pDesc->cbHash);
            pThis->uState = hSrc->uState;

            if (RT_SUCCESS(rc))
            {
                *phDigest = pThis;
                return rtCrDigestSuccessWithDigestWarnings(pThis->pDesc);
            }
            if (hSrc->pDesc->pfnFree)
                hSrc->pDesc->pfnFree(pThis->pvState);
        }
        else
            rc = VERR_NO_MEMORY;
        pThis->u32Magic = 0;
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


RTDECL(int) RTCrDigestReset(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, VERR_INVALID_HANDLE);

    pThis->cbConsumed = 0;
    pThis->uState = RTCRDIGEST_STATE_READY;

    int rc = VINF_SUCCESS;
    if (pThis->pDesc->pfnInit)
    {
        rc = pThis->pDesc->pfnInit(pThis->pvState, NULL, true /*fReInit*/);
        if (RT_FAILURE(rc))
            pThis->uState = RTCRDIGEST_STATE_BUSTED;
        RT_BZERO(&pThis->abState[pThis->offHash], pThis->pDesc->cbHash);
    }
    else
    {
        Assert(!pThis->pDesc->pfnNew);
        RT_BZERO(pThis->pvState, pThis->offHash + pThis->pDesc->cbHash);
    }
    return rc;
}


RTDECL(uint32_t) RTCrDigestRetain(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < 64);
    return cRefs;
}


RTDECL(uint32_t) RTCrDigestRelease(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    if (pThis == NIL_RTCRDIGEST)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (!cRefs)
    {
        pThis->u32Magic = ~RTCRDIGESTINT_MAGIC;
        if (pThis->pDesc->pfnDelete)
            pThis->pDesc->pfnDelete(pThis->pvState);
        if (pThis->pDesc->pfnFree)
            pThis->pDesc->pfnFree(pThis->pvState);
        RTMemFree(pThis);
    }
    Assert(cRefs < 64);
    return cRefs;
}


RTDECL(int) RTCrDigestUpdate(RTCRDIGEST hDigest, void const *pvData, size_t cbData)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uState == RTCRDIGEST_STATE_READY, VERR_INVALID_STATE);

    pThis->pDesc->pfnUpdate(pThis->pvState, pvData, cbData);
    pThis->cbConsumed += cbData;
    return VINF_SUCCESS;
}


RTDECL(int) RTCrDigestFinal(RTCRDIGEST hDigest, void *pvHash, size_t cbHash)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(pThis->uState == RTCRDIGEST_STATE_READY || pThis->uState == RTCRDIGEST_STATE_FINAL, VERR_INVALID_STATE);
    AssertPtrNullReturn(pvHash, VERR_INVALID_POINTER);

    /*
     * Make sure the hash calculation is final.
     */
    if (pThis->uState == RTCRDIGEST_STATE_READY)
    {
        pThis->pDesc->pfnFinal(pThis->pvState, &pThis->abState[pThis->offHash]);
        pThis->uState = RTCRDIGEST_STATE_FINAL;
    }
    else
        AssertReturn(pThis->uState == RTCRDIGEST_STATE_FINAL, VERR_INVALID_STATE);

    /*
     * Copy out the hash if requested.
     */
    if (cbHash > 0)
    {
        uint32_t cbNeeded = pThis->pDesc->cbHash;
        if (pThis->pDesc->pfnGetHashSize)
            cbNeeded = pThis->pDesc->pfnGetHashSize(pThis->pvState);
        Assert(cbNeeded > 0);

        if (cbNeeded == cbHash)
            memcpy(pvHash, &pThis->abState[pThis->offHash], cbNeeded);
        else if (cbNeeded < cbHash)
        {
            memcpy(pvHash, &pThis->abState[pThis->offHash], cbNeeded);
            memset((uint8_t *)pvHash + cbNeeded, 0, cbHash - cbNeeded);
            return VINF_BUFFER_UNDERFLOW;
        }
        else
        {
            memcpy(pvHash, &pThis->abState[pThis->offHash], cbHash);
            return VERR_BUFFER_OVERFLOW;
        }
    }

    return rtCrDigestSuccessWithDigestWarnings(pThis->pDesc);
}


RTDECL(bool) RTCrDigestMatch(RTCRDIGEST hDigest, void const *pvHash, size_t cbHash)
{
    PRTCRDIGESTINT pThis = hDigest;

    int rc = RTCrDigestFinal(pThis, NULL, 0);
    AssertRCReturn(rc, false);

    AssertPtrReturn(pvHash, false);
    return pThis->pDesc->cbHash == cbHash
        && !memcmp(&pThis->abState[pThis->offHash], pvHash, cbHash);
}


RTDECL(uint8_t const *) RTCrDigestGetHash(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, NULL);

    int rc = RTCrDigestFinal(pThis, NULL, 0);
    AssertRCReturn(rc, NULL);

    return &pThis->abState[pThis->offHash];
}


RTDECL(uint32_t) RTCrDigestGetHashSize(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, 0);
    if (pThis->pDesc->pfnGetHashSize)
    {
        uint32_t cbHash = pThis->pDesc->pfnGetHashSize(pThis->pvState);
        Assert(cbHash <= pThis->pDesc->cbHash);
        return cbHash;
    }
    return pThis->pDesc->cbHash;
}


RTDECL(uint64_t) RTCrDigestGetConsumedSize(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, 0);
    return pThis->cbConsumed;
}


RTDECL(bool) RTCrDigestIsFinalized(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, false);
    return pThis->uState == RTCRDIGEST_STATE_FINAL;
}


RTDECL(RTDIGESTTYPE) RTCrDigestGetType(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, RTDIGESTTYPE_INVALID);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, RTDIGESTTYPE_INVALID);

    RTDIGESTTYPE enmType = pThis->pDesc->enmType;
    if (pThis->pDesc->pfnGetDigestType)
        enmType = pThis->pDesc->pfnGetDigestType(pThis->pvState);
    return enmType;
}


RTDECL(const char *) RTCrDigestGetAlgorithmOid(RTCRDIGEST hDigest)
{
    return RTCrDigestTypeToAlgorithmOid(RTCrDigestGetType(hDigest));
}


RTDECL(uint32_t) RTCrDigestGetFlags(RTCRDIGEST hDigest)
{
    PRTCRDIGESTINT pThis = hDigest;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRDIGESTINT_MAGIC, UINT32_MAX);
    return pThis->pDesc->fFlags;
}


RTDECL(const char *) RTCrDigestTypeToAlgorithmOid(RTDIGESTTYPE enmDigestType)
{
    switch (enmDigestType)
    {
        case RTDIGESTTYPE_MD2:          return RTCRX509ALGORITHMIDENTIFIERID_MD2;
        case RTDIGESTTYPE_MD4:          return RTCRX509ALGORITHMIDENTIFIERID_MD4;
        case RTDIGESTTYPE_MD5:          return RTCRX509ALGORITHMIDENTIFIERID_MD5;
        case RTDIGESTTYPE_SHA1:         return RTCRX509ALGORITHMIDENTIFIERID_SHA1;
        case RTDIGESTTYPE_SHA224:       return RTCRX509ALGORITHMIDENTIFIERID_SHA224;
        case RTDIGESTTYPE_SHA256:       return RTCRX509ALGORITHMIDENTIFIERID_SHA256;
        case RTDIGESTTYPE_SHA384:       return RTCRX509ALGORITHMIDENTIFIERID_SHA384;
        case RTDIGESTTYPE_SHA512:       return RTCRX509ALGORITHMIDENTIFIERID_SHA512;
        case RTDIGESTTYPE_SHA512T224:   return RTCRX509ALGORITHMIDENTIFIERID_SHA512T224;
        case RTDIGESTTYPE_SHA512T256:   return RTCRX509ALGORITHMIDENTIFIERID_SHA512T256;
        case RTDIGESTTYPE_SHA3_224:     return RTCRX509ALGORITHMIDENTIFIERID_SHA3_224;
        case RTDIGESTTYPE_SHA3_256:     return RTCRX509ALGORITHMIDENTIFIERID_SHA3_256;
        case RTDIGESTTYPE_SHA3_384:     return RTCRX509ALGORITHMIDENTIFIERID_SHA3_384;
        case RTDIGESTTYPE_SHA3_512:     return RTCRX509ALGORITHMIDENTIFIERID_SHA3_512;
        default:                        return NULL;
    }
}


RTDECL(const char *) RTCrDigestTypeToName(RTDIGESTTYPE enmDigestType)
{
    switch (enmDigestType)
    {
        case RTDIGESTTYPE_CRC32:        return "CRC32";
        case RTDIGESTTYPE_CRC64:        return "CRC64";
        case RTDIGESTTYPE_MD2:          return "MD2";
        case RTDIGESTTYPE_MD4:          return "MD4";
        case RTDIGESTTYPE_MD5:          return "MD5";
        case RTDIGESTTYPE_SHA1:         return "SHA-1";
        case RTDIGESTTYPE_SHA224:       return "SHA-224";
        case RTDIGESTTYPE_SHA256:       return "SHA-256";
        case RTDIGESTTYPE_SHA384:       return "SHA-384";
        case RTDIGESTTYPE_SHA512:       return "SHA-512";
        case RTDIGESTTYPE_SHA512T224:   return "SHA-512/224";
        case RTDIGESTTYPE_SHA512T256:   return "SHA-512/256";
        case RTDIGESTTYPE_SHA3_224:     return "SHA3-224";
        case RTDIGESTTYPE_SHA3_256:     return "SHA3-256";
        case RTDIGESTTYPE_SHA3_384:     return "SHA3-384";
        case RTDIGESTTYPE_SHA3_512:     return "SHA3-512";
        default:                        return NULL;
    }
}


RTDECL(uint32_t) RTCrDigestTypeToHashSize(RTDIGESTTYPE enmDigestType)
{
    switch (enmDigestType)
    {
        case RTDIGESTTYPE_CRC32:        return  32 / 8;
        case RTDIGESTTYPE_CRC64:        return  64 / 8;
        case RTDIGESTTYPE_MD2:          return 128 / 8;
        case RTDIGESTTYPE_MD4:          return 128 / 8;
        case RTDIGESTTYPE_MD5:          return 128 / 8;
        case RTDIGESTTYPE_SHA1:         return 160 / 8;
        case RTDIGESTTYPE_SHA224:       return 224 / 8;
        case RTDIGESTTYPE_SHA256:       return 256 / 8;
        case RTDIGESTTYPE_SHA384:       return 384 / 8;
        case RTDIGESTTYPE_SHA512:       return 512 / 8;
        case RTDIGESTTYPE_SHA512T224:   return 224 / 8;
        case RTDIGESTTYPE_SHA512T256:   return 256 / 8;
        case RTDIGESTTYPE_SHA3_224:     return 224 / 8;
        case RTDIGESTTYPE_SHA3_256:     return 256 / 8;
        case RTDIGESTTYPE_SHA3_384:     return 384 / 8;
        case RTDIGESTTYPE_SHA3_512:     return 512 / 8;
        default:
            AssertFailed();
            return 0;
    }
}

