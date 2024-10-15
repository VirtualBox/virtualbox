/* $Id: store-inmem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - In Memory Cryptographic Certificate Store.
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
#include <iprt/crypto/store.h>

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "store-internal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A certificate entry in the in-memory store.
 */
typedef struct RTCRSTOREINMEMCERT
{
    /** The core certificate context. */
    RTCRCERTCTXINT          Core;
    /** Internal copy of the flag (paranoia). */
    uint32_t                fFlags;
    /** Decoded data. */
    union
    {
        /** ASN.1 core structure for generic access. */
        RTASN1CORE              Asn1Core;
        /** The decoded X.509 certificate (RTCRCERTCTX_F_ENC_X509_DER). */
        RTCRX509CERTIFICATE     X509Cert;
        /** The decoded trust anchor info (RTCRCERTCTX_F_ENC_TAF_DER). */
        RTCRTAFTRUSTANCHORINFO  TaInfo;
    } u;
    /** Pointer to the store if still in it (no reference). */
    struct RTCRSTOREINMEM  *pStore;
    /** The DER encoding of the certificate. */
    uint8_t                 abEncoded[1];
} RTCRSTOREINMEMCERT;
AssertCompileMembersAtSameOffset(RTCRSTOREINMEMCERT, u.X509Cert.SeqCore.Asn1Core, RTCRSTOREINMEMCERT, u.Asn1Core);
AssertCompileMembersAtSameOffset(RTCRSTOREINMEMCERT, u.TaInfo.SeqCore.Asn1Core,   RTCRSTOREINMEMCERT, u.Asn1Core);
/** Pointer to an in-memory store certificate entry. */
typedef RTCRSTOREINMEMCERT *PRTCRSTOREINMEMCERT;


/**
 * The per instance data of a in-memory crypto store.
 *
 * Currently we ASSUME we don't need serialization.  Add that when needed!
 */
typedef struct RTCRSTOREINMEM
{
    /** The number of certificates. */
    uint32_t                cCerts;
    /** The max number of certificates papCerts can store before growing it. */
    uint32_t                cCertsAlloc;
    /** Array of certificates. */
    PRTCRSTOREINMEMCERT    *papCerts;

    /** Parent store. */
    RTCRSTORE               hParentStore;
    /** The parent store callback table. */
    PCRTCRSTOREPROVIDER     pParentProvider;
    /** The parent store provider callback argument. */
    void                   *pvParentProvider;
} RTCRSTOREINMEM;
/** Pointer to an in-memory crypto store. */
typedef RTCRSTOREINMEM *PRTCRSTOREINMEM;




static DECLCALLBACK(void) rtCrStoreInMemCertEntry_Dtor(PRTCRCERTCTXINT pCertCtx)
{
    PRTCRSTOREINMEMCERT pEntry = (PRTCRSTOREINMEMCERT)pCertCtx;
    AssertRelease(!pEntry->pStore);

    pEntry->Core.pfnDtor = NULL;
    RTAsn1VtDelete(&pEntry->u.Asn1Core);
    RTMemFree(pEntry);
}


/**
 * Internal method for allocating and initalizing a certificate entry in the
 * in-memory store.
 *
 * @returns IPRT status code.
 * @param   pThis       The in-memory store instance.
 * @param   fEnc        RTCRCERTCTX_F_ENC_X509_DER or RTCRCERTCTX_F_ENC_TAF_DER.
 * @param   pbSrc       The DER encoded X.509 certificate to add.
 * @param   cbSrc       The size of the encoded certificate.
 * @param   pErrInfo    Where to return extended error info.  Optional.
 * @param   ppEntry     Where to return the pointer to the new entry.
 */
static int rtCrStoreInMemCreateCertEntry(PRTCRSTOREINMEM pThis, uint32_t fEnc, uint8_t const *pbSrc, uint32_t cbSrc,
                                         PRTERRINFO pErrInfo, PRTCRSTOREINMEMCERT *ppEntry)
{
    int                 rc;
    PRTCRSTOREINMEMCERT pEntry    = (PRTCRSTOREINMEMCERT)RTMemAllocZ(RT_UOFFSETOF_DYN(RTCRSTOREINMEMCERT, abEncoded[cbSrc]));
    if (pEntry)
    {
        memcpy(pEntry->abEncoded, pbSrc, cbSrc);
        pEntry->Core.u32Magic           = RTCRCERTCTXINT_MAGIC;
        pEntry->Core.cRefs              = 1;
        pEntry->Core.pfnDtor            = rtCrStoreInMemCertEntry_Dtor;
        pEntry->Core.Public.fFlags      = fEnc;
        pEntry->Core.Public.cbEncoded   = cbSrc;
        pEntry->Core.Public.pabEncoded  = &pEntry->abEncoded[0];
        if (fEnc == RTCRCERTCTX_F_ENC_X509_DER)
        {
            pEntry->Core.Public.pCert   = &pEntry->u.X509Cert;
            pEntry->Core.Public.pTaInfo = NULL;
        }
        else
        {
            pEntry->Core.Public.pCert   = NULL;
            pEntry->Core.Public.pTaInfo = &pEntry->u.TaInfo;
        }
        pEntry->pStore                  = pThis;

        RTASN1CURSORPRIMARY Cursor;
        RTAsn1CursorInitPrimary(&Cursor, &pEntry->abEncoded[0], cbSrc, pErrInfo, &g_RTAsn1DefaultAllocator,
                                RTASN1CURSOR_FLAGS_DER, "InMem");
        if (fEnc == RTCRCERTCTX_F_ENC_X509_DER)
            rc = RTCrX509Certificate_DecodeAsn1(&Cursor.Cursor, 0, &pEntry->u.X509Cert, "Cert");
        else
            rc = RTCrTafTrustAnchorInfo_DecodeAsn1(&Cursor.Cursor, 0, &pEntry->u.TaInfo, "TaInfo");
        if (RT_SUCCESS(rc))
        {
            if (fEnc == RTCRCERTCTX_F_ENC_X509_DER)
                rc = RTCrX509Certificate_CheckSanity(&pEntry->u.X509Cert, 0, pErrInfo, "Cert");
            else
                rc = RTCrTafTrustAnchorInfo_CheckSanity(&pEntry->u.TaInfo, 0, pErrInfo, "TaInfo");
            if (RT_SUCCESS(rc))
            {
                *ppEntry = pEntry;
                return VINF_SUCCESS;
            }

            RTAsn1VtDelete(&pEntry->u.Asn1Core);
        }
        RTMemFree(pEntry);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Grows the certificate pointer array to at least @a cMin entries.
 *
 * @returns IPRT status code.
 * @param   pThis       The in-memory store instance.
 * @param   cMin        The new minimum store size.
 */
static int rtCrStoreInMemGrow(PRTCRSTOREINMEM pThis, uint32_t cMin)
{
    AssertReturn(cMin <= _1M, VERR_OUT_OF_RANGE);
    AssertReturn(cMin > pThis->cCertsAlloc, VERR_INTERNAL_ERROR_3);

    if (cMin < 64)
        cMin = RT_ALIGN_32(cMin, 8);
    else
        cMin = RT_ALIGN_32(cMin, 32);

    void *pv = RTMemRealloc(pThis->papCerts, cMin * sizeof(pThis->papCerts[0]));
    if (pv)
    {
        pThis->papCerts = (PRTCRSTOREINMEMCERT *)pv;
        for (uint32_t i = pThis->cCertsAlloc; i < cMin; i++)
            pThis->papCerts[i] = NULL;
        pThis->cCertsAlloc = cMin;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}



/** @interface_method_impl{RTCRSTOREPROVIDER,pfnDestroyStore} */
static DECLCALLBACK(void) rtCrStoreInMem_DestroyStore(void *pvProvider)
{
    PRTCRSTOREINMEM pThis = (PRTCRSTOREINMEM)pvProvider;

    while (pThis->cCerts > 0)
    {
        uint32_t i = --pThis->cCerts;
        PRTCRSTOREINMEMCERT pEntry = pThis->papCerts[i];
        pThis->papCerts[i] = NULL;
        AssertPtr(pEntry);

        pEntry->pStore = NULL;
        RTCrCertCtxRelease(&pEntry->Core.Public);
    }

    RTMemFree(pThis->papCerts);
    pThis->papCerts = NULL;

    if (pThis->hParentStore != NIL_RTCRSTORE)
    {
        RTCrStoreRelease(pThis->hParentStore);
        pThis->hParentStore = NIL_RTCRSTORE;
    }

    RTMemFree(pThis);
}


/** @interface_method_impl{RTCRSTOREPROVIDER,pfnCertCtxQueryPrivateKey} */
static DECLCALLBACK(int) rtCrStoreInMem_CertCtxQueryPrivateKey(void *pvProvider, PRTCRCERTCTXINT pCertCtx,
                                                               uint8_t *pbKey, size_t cbKey, size_t *pcbKeyRet)
{
    RT_NOREF_PV(pvProvider); RT_NOREF_PV(pCertCtx); RT_NOREF_PV(pbKey); RT_NOREF_PV(cbKey); RT_NOREF_PV(pcbKeyRet);
    //PRTCRSTOREINMEM pThis = (PRTCRSTOREINMEM)pvProvider;
    return VERR_NOT_FOUND;
}


/** @interface_method_impl{RTCRSTOREPROVIDER,pfnCertFindAll} */
static DECLCALLBACK(int) rtCrStoreInMem_CertFindAll(void *pvProvider, PRTCRSTORECERTSEARCH pSearch)
{
    pSearch->auOpaque[0] = ~(uintptr_t)pvProvider;
    pSearch->auOpaque[1] = 0;
    pSearch->auOpaque[2] = ~(uintptr_t)0;  /* For the front-end API. */
    pSearch->auOpaque[3] = ~(uintptr_t)0;  /* For the front-end API. */
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTCRSTOREPROVIDER,pfnCertSearchNext} */
static DECLCALLBACK(PCRTCRCERTCTX) rtCrStoreInMem_CertSearchNext(void *pvProvider, PRTCRSTORECERTSEARCH pSearch)
{
    PRTCRSTOREINMEM pThis = (PRTCRSTOREINMEM)pvProvider;
    if (pSearch->auOpaque[0] == ~(uintptr_t)pvProvider)
    {
        uintptr_t i = pSearch->auOpaque[1];
        if (i < pThis->cCerts)
        {
            pSearch->auOpaque[1] = i + 1;
            PRTCRCERTCTXINT pCertCtx = &pThis->papCerts[i]->Core;
            ASMAtomicIncU32(&pCertCtx->cRefs);
            return &pCertCtx->Public;
        }

        /* Do we have a parent store to search? */
        if (pThis->hParentStore == NIL_RTCRSTORE)
            return NULL; /* no */
        if (   !pThis->pParentProvider->pfnCertFindAll
            || !pThis->pParentProvider->pfnCertSearchNext)
            return NULL;

        RTCRSTORECERTSEARCH const SavedSearch = *pSearch;
        int rc = pThis->pParentProvider->pfnCertFindAll(pThis->pvParentProvider, pSearch);
        AssertRCReturnStmt(rc, *pSearch = SavedSearch, NULL);

        /* Restore the store.cpp specifics: */
        AssertCompile(RT_ELEMENTS(SavedSearch.auOpaque) == 4);
        pSearch->auOpaque[2] = SavedSearch.auOpaque[2];
        pSearch->auOpaque[3] = SavedSearch.auOpaque[3];
    }

    AssertReturn(pThis->pParentProvider, NULL);
    AssertReturn(pThis->pParentProvider->pfnCertSearchNext, NULL);
    return pThis->pParentProvider->pfnCertSearchNext(pThis->pvParentProvider, pSearch);
}


/** @interface_method_impl{RTCRSTOREPROVIDER,pfnCertSearchDestroy} */
static DECLCALLBACK(void) rtCrStoreInMem_CertSearchDestroy(void *pvProvider, PRTCRSTORECERTSEARCH pSearch)
{
    PRTCRSTOREINMEM pThis = (PRTCRSTOREINMEM)pvProvider;
    if (pSearch->auOpaque[0] == ~(uintptr_t)pvProvider)
    {
        pSearch->auOpaque[0] = 0;
        pSearch->auOpaque[1] = 0;
        pSearch->auOpaque[2] = 0;
        pSearch->auOpaque[3] = 0;
    }
    else
    {
        AssertReturnVoid(pThis->pParentProvider);
        AssertReturnVoid(pThis->pParentProvider->pfnCertSearchDestroy);
        pThis->pParentProvider->pfnCertSearchDestroy(pThis->pvParentProvider, pSearch);
    }
}


/** @interface_method_impl{RTCRSTOREPROVIDER,pfnCertSearchDestroy} */
static DECLCALLBACK(int) rtCrStoreInMem_CertAddEncoded(void *pvProvider, uint32_t fFlags,
                                                       uint8_t const *pbEncoded, uint32_t cbEncoded, PRTERRINFO pErrInfo)
{
    PRTCRSTOREINMEM pThis = (PRTCRSTOREINMEM)pvProvider;
    int rc;

    AssertMsgReturn(   (fFlags & RTCRCERTCTX_F_ENC_MASK) == RTCRCERTCTX_F_ENC_X509_DER
                    || (fFlags & RTCRCERTCTX_F_ENC_MASK) == RTCRCERTCTX_F_ENC_TAF_DER
                    , ("Only X.509 and TAF DER are supported: %#x\n", fFlags), VERR_INVALID_FLAGS);

    /*
     * Check for duplicates if specified.
     */
    if (fFlags & RTCRCERTCTX_F_ADD_IF_NOT_FOUND)
    {
        uint32_t iCert = pThis->cCerts;
        while (iCert-- > 0)
        {
            PRTCRSTOREINMEMCERT pCert = pThis->papCerts[iCert];
            if (   pCert->Core.Public.cbEncoded == cbEncoded
                && pCert->Core.Public.fFlags == (fFlags & RTCRCERTCTX_F_ENC_MASK)
                && memcmp(pCert->Core.Public.pabEncoded, pbEncoded, cbEncoded) == 0)
                return VWRN_ALREADY_EXISTS;
        }
    }

    /*
     * Add it.
     */
    if (pThis->cCerts + 1 <= pThis->cCertsAlloc)
    { /* likely */ }
    else
    {
        rc = rtCrStoreInMemGrow(pThis, pThis->cCerts + 1);
        if (RT_FAILURE(rc))
            return rc;
    }

    rc = rtCrStoreInMemCreateCertEntry(pThis, fFlags & RTCRCERTCTX_F_ENC_MASK, pbEncoded, cbEncoded,
                                       pErrInfo, &pThis->papCerts[pThis->cCerts]);
    if (RT_SUCCESS(rc))
    {
        pThis->cCerts++;
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * In-memory store provider.
 */
static RTCRSTOREPROVIDER const g_rtCrStoreInMemProvider =
{
    "in-memory",
    rtCrStoreInMem_DestroyStore,
    rtCrStoreInMem_CertCtxQueryPrivateKey,
    rtCrStoreInMem_CertFindAll,
    rtCrStoreInMem_CertSearchNext,
    rtCrStoreInMem_CertSearchDestroy,
    rtCrStoreInMem_CertAddEncoded,
    NULL,
    42
};


/**
 * Common worker for RTCrStoreCreateInMem and future constructors...
 *
 * @returns IPRT status code.
 * @param   ppStore         Where to return the store instance.
 * @param   hParentStore    Optional parent store.  Consums reference on
 *                          success.
 */
static int rtCrStoreInMemCreateInternal(PRTCRSTOREINMEM *ppStore, RTCRSTORE hParentStore)
{
    PRTCRSTOREINMEM pStore = (PRTCRSTOREINMEM)RTMemAlloc(sizeof(*pStore));
    if (pStore)
    {
        pStore->cCerts           = 0;
        pStore->cCertsAlloc      = 0;
        pStore->papCerts         = NULL;
        pStore->hParentStore     = hParentStore;
        pStore->pParentProvider  = NULL;
        pStore->pvParentProvider = NULL;
        *ppStore = pStore;
        if (hParentStore == NIL_RTCRSTORE)
            return VINF_SUCCESS;
        if (~(uintptr_t)hParentStore != ~(uintptr_t)pStore)
        {
            pStore->pParentProvider = rtCrStoreGetProvider(hParentStore, &pStore->pvParentProvider);
            if (pStore->pParentProvider)
                return VINF_SUCCESS;
            AssertFailed();
        }
        RTMemFree(pStore);
    }
    *ppStore = NULL; /* shut up gcc-maybe-pita warning. */
    return VERR_NO_MEMORY;
}


RTDECL(int) RTCrStoreCreateInMemEx(PRTCRSTORE phStore, uint32_t cSizeHint, RTCRSTORE hParentStore)
{
    if (hParentStore != NIL_RTCRSTORE)
    {
        uint32_t cRefs = RTCrStoreRetain(hParentStore);
        AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);
    }

    PRTCRSTOREINMEM pStore;
    int rc = rtCrStoreInMemCreateInternal(&pStore, hParentStore);
    if (RT_SUCCESS(rc))
    {
        if (cSizeHint)
            rc = rtCrStoreInMemGrow(pStore, RT_MIN(cSizeHint, 512));
        if (RT_SUCCESS(rc))
        {
            rc = rtCrStoreCreate(&g_rtCrStoreInMemProvider, pStore, phStore);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
        }
        RTMemFree(pStore);
    }

    RTCrStoreRelease(hParentStore);
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCreateInMemEx);


RTDECL(int) RTCrStoreCreateInMem(PRTCRSTORE phStore, uint32_t cSizeHint)
{
    return RTCrStoreCreateInMemEx(phStore, cSizeHint, NIL_RTCRSTORE);
}
RT_EXPORT_SYMBOL(RTCrStoreCreateInMem);

