/* $Id: store-internal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Cryptographic Store, Internal Header.
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

#ifndef IPRT_INCLUDED_SRC_common_crypto_store_internal_h
#define IPRT_INCLUDED_SRC_common_crypto_store_internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/**
 * Internal certificate context.
 *
 * In addition to the externally visible structure (RTCRCERTCTX) this has the
 * reference counter and store reference.  (This structure may again be part of
 * a larger structure internal to the store, depending on the source store.)
 */
typedef struct RTCRCERTCTXINT
{
    /** Magic number (RTCRCERTCTXINT_MAGIC). */
    uint32_t                u32Magic;
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /**
     * Destructor that gets called with cRefs reaches zero.
     * @param   pCertCtx        The internal certificate context.
     */
    DECLCALLBACKMEMBER(void, pfnDtor,(struct RTCRCERTCTXINT *pCertCtx));
    /** The public store context. */
    RTCRCERTCTX             Public;
} RTCRCERTCTXINT;
/** Pointer to an internal certificate context. */
typedef RTCRCERTCTXINT *PRTCRCERTCTXINT;

/** Magic value for RTCRCERTCTXINT::u32Magic (Alan Mathison Turing).  */
#define RTCRCERTCTXINT_MAGIC       UINT32_C(0x19120623)
/** Dead magic value for RTCRCERTCTXINT::u32Magic. */
#define RTCRCERTCTXINT_MAGIC_DEAD  UINT32_C(0x19540607)


/**
 * IPRT Cryptographic Store Provider.
 *
 * @remarks This is a very incomplete sketch.
 */
typedef struct RTCRSTOREPROVIDER
{
    /** The provider name.  */
    const char *pszName;

    /**
     * Called to destroy an open store.
     *
     * @param   pvProvider      The provider specific data.
     */
    DECLCALLBACKMEMBER(void, pfnDestroyStore,(void *pvProvider));

    /**
     * Queries the private key.
     *
     * @returns IPRT status code.
     * @retval  VERR_NOT_FOUND if not private key.
     * @retval  VERR_ACCESS_DENIED if the private key isn't allowed to leave the
     *          store.  One would then have to use the pfnCertCtxSign method.
     *
     * @param   pvProvider      The provider specific data.
     * @param   pCertCtx        The internal certificate context.
     * @param   pbKey           Where to return the key bytes.
     * @param   cbKey           The size of the buffer @a pbKey points to.
     * @param   pcbKeyRet       Where to return the size of the returned key.
     */
    DECLCALLBACKMEMBER(int, pfnCertCtxQueryPrivateKey,(void *pvProvider, PRTCRCERTCTXINT pCertCtx,
                                                       uint8_t *pbKey, size_t cbKey, size_t *pcbKeyRet));

    /**
     * Open an enumeration of all certificates.
     *
     * @returns IPRT status code
     * @param   pvProvider      The provider specific data.
     * @param   pSearch         Pointer to opaque search state structure.  The
     *                          provider should initalize this on success.
     */
    DECLCALLBACKMEMBER(int, pfnCertFindAll,(void *pvProvider, PRTCRSTORECERTSEARCH pSearch));

    /**
     * Get the next certificate.
     *
     * @returns Reference to the next certificate context (must be released by
     *          caller).  NULL if no more certificates in the search result.
     * @param   pvProvider      The provider specific data.
     * @param   pSearch         Pointer to opaque search state structure.
     */
    DECLCALLBACKMEMBER(PCRTCRCERTCTX, pfnCertSearchNext,(void *pvProvider, PRTCRSTORECERTSEARCH pSearch));

    /**
     * Closes a certficate search state.
     *
     * @param   pvProvider      The provider specific data.
     * @param   pSearch         Pointer to opaque search state structure to destroy.
     */
    DECLCALLBACKMEMBER(void, pfnCertSearchDestroy,(void *pvProvider, PRTCRSTORECERTSEARCH pSearch));

    /**
     * Adds a certificate to the store.
     *
     * @returns IPRT status code.
     * @retval  VWRN_ALREADY_EXISTS if the certificate is already present and
     *          RTCRCERTCTX_F_ADD_IF_NOT_FOUND was specified.
     * @param   pvProvider      The provider specific data.
     * @param   fFlags          RTCRCERTCTX_F_XXX.
     * @param   pbEncoded       The encoded certificate bytes.
     * @param   cbEncoded       The size of the encoded certificate.
     * @param   pErrInfo        Where to store extended error info. Optional.
     */
    DECLCALLBACKMEMBER(int, pfnCertAddEncoded,(void *pvProvider, uint32_t fFlags, uint8_t const *pbEncoded, uint32_t cbEncoded,
                                               PRTERRINFO pErrInfo));


    /* Optional: */

    /**
     * Find all certficates matching a given issuer and serial number.
     *
     * (Usually only one result.)
     *
     * @returns IPRT status code
     * @param   pvProvider      The provider specific data.
     * @param   phSearch        Pointer to a provider specific search handle.
     */
    DECLCALLBACKMEMBER(int, pfnCertFindByIssuerAndSerialNo,(void *pvProvider, PCRTCRX509NAME pIssuer, PCRTASN1INTEGER pSerialNo,
                                                            PRTCRSTORECERTSEARCH phSearch));
    /** Non-zero end marker. */
    uintptr_t               uEndMarker;
} RTCRSTOREPROVIDER;

/** Pointer to a store provider call table. */
typedef RTCRSTOREPROVIDER const *PCRTCRSTOREPROVIDER;


DECLHIDDEN(int) rtCrStoreCreate(PCRTCRSTOREPROVIDER pProvider, void *pvProvider, PRTCRSTORE phStore);
DECLHIDDEN(PCRTCRSTOREPROVIDER) rtCrStoreGetProvider(RTCRSTORE hStore, void **ppvProvider);

#endif /* !IPRT_INCLUDED_SRC_common_crypto_store_internal_h */

