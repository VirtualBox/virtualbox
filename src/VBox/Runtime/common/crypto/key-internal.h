/* $Id: key-internal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Cryptographic Keys, Internal Header.
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

#ifndef IPRT_INCLUDED_SRC_common_crypto_key_internal_h
#define IPRT_INCLUDED_SRC_common_crypto_key_internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/crypto/key.h>
#include <iprt/bignum.h>


/**
 * Cryptographic key - core bits.
 */
typedef struct RTCRKEYINT
{
    /** Magic value (RTCRKEYINT_MAGIC). */
    uint32_t                    u32Magic;
    /** Reference counter. */
    uint32_t volatile           cRefs;
    /** The key type. */
    RTCRKEYTYPE                 enmType;
    /** Flags, RTCRKEYINT_F_XXX.  */
    uint32_t                    fFlags;
    /** Number of bits in the key. */
    uint32_t                    cBits;

    /** Type specific data. */
    union
    {
        /** RTCRKEYTYPE_RSA_PRIVATE. */
        struct
        {
            /** The modulus.  */
            RTBIGNUM                Modulus;
            /** The private exponent.  */
            RTBIGNUM                PrivateExponent;
            /** The public exponent.  */
            RTBIGNUM                PublicExponent;
            /** @todo add more bits as needed. */
        } RsaPrivate;

        /** RTCRKEYTYPE_RSA_PUBLIC. */
        struct
        {
            /** The modulus.  */
            RTBIGNUM                Modulus;
            /** The exponent.  */
            RTBIGNUM                Exponent;
        } RsaPublic;

        /** RTCRKEYTYPE_ECDSA_PUBLIC. */
        struct
        {
            /** The named curve. */
            RTASN1OBJID             NamedCurve;
            /** @todo ECPoint. */
        } EcdsaPublic;
    } u;

#if defined(IPRT_WITH_OPENSSL)
    /** Size of raw key copy. */
    uint32_t                    cbEncoded;
    /** Raw copy of the key, for openssl and such.
     * If sensitive, this is a safer allocation, otherwise it follows the structure. */
    uint8_t                    *pbEncoded;
#endif
} RTCRKEYINT;
/** Pointer to a crypographic key. */
typedef RTCRKEYINT *PRTCRKEYINT;
/** Pointer to a const crypographic key. */
typedef RTCRKEYINT const *PCRTCRKEYINT;



/** @name RTCRKEYINT_F_XXX.
 * @{ */
/** Key contains sensitive information, so no unnecessary copies. */
#define RTCRKEYINT_F_SENSITIVE          UINT32_C(0x00000001)
/** Set if private key bits are present. */
#define RTCRKEYINT_F_PRIVATE            UINT32_C(0x00000002)
/** Set if public key bits are present. */
#define RTCRKEYINT_F_PUBLIC             UINT32_C(0x00000004)
/** Set if the cbEncoded/pbEncoded members are present. */
#define RTCRKEYINT_F_INCLUDE_ENCODED    UINT32_C(0x00000008)
/** @} */

DECLHIDDEN(int) rtCrKeyCreateWorker(PRTCRKEYINT *ppThis, RTCRKEYTYPE enmType, uint32_t fFlags,
                                    void const *pvEncoded, uint32_t cbEncoded);
DECLHIDDEN(int) rtCrKeyCreateRsaPublic(PRTCRKEY phKey, const void *pvKeyBits, uint32_t cbKeyBits,
                                       PRTERRINFO pErrInfo, const char *pszErrorTag);
DECLHIDDEN(int) rtCrKeyCreateRsaPrivate(PRTCRKEY phKey, const void *pvKeyBits, uint32_t cbKeyBits,
                                        PRTERRINFO pErrInfo, const char *pszErrorTag);
DECLHIDDEN(int) rtCrKeyCreateEcdsaPublic(PRTCRKEY phKey, PCRTASN1DYNTYPE pParameters,
                                         const void *pvKeyBits, uint32_t cbKeyBits, PRTERRINFO pErrInfo, const char *pszErrorTag);

#endif /* !IPRT_INCLUDED_SRC_common_crypto_key_internal_h */
