/* $Id: RTCrStoreCertAddFromJavaKeyStore.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Cryptographic (Certificate) Store, RTCrStoreCertAddFromJavaKeyStore.
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
#define LOG_GROUP RTLOGGROUP_CRYPTO
#include "internal/iprt.h"
#include <iprt/crypto/store.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/log.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The java key store magic number (file endian). */
#define JKS_MAGIC       RT_H2BE_U32_C(UINT32_C(0xfeedfeed))
/** Java key store format version 2 (file endian). */
#define JKS_VERSION_2   RT_H2BE_U32_C(2)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Java key store (JKS) header.
 */
typedef struct JKSHEADER
{
    /** The magic (big endian) - JKS_MAGIC. */
    uint32_t        uMagic;
    /** Format version number (big endian) - JKS_VERSION_2.   */
    uint32_t        uVersion;
    /** The number of keystore entries (big endian). */
    uint32_t        cEntries;
} JKSHEADER;
/** Pointer to a const java key store header.   */
typedef JKSHEADER const *PCJKSHEADER;


RTDECL(int) RTCrStoreCertAddFromJavaKeyStoreInMem(RTCRSTORE hStore, uint32_t fFlags, void const *pvContent, size_t cbContent,
                                                  const char *pszErrorName, PRTERRINFO pErrInfo)
{
    uint8_t const *pbContent = (uint8_t const *)pvContent;

    /*
     * Check the header.
     */
    if (cbContent < sizeof(JKSHEADER) + RTSHA1_HASH_SIZE)
        return RTErrInfoAddF(pErrInfo, VERR_WRONG_TYPE /** @todo better status codes */,
                             "  Too small (%zu bytes) for java key store (%s)", cbContent, pszErrorName);
    PCJKSHEADER pHdr = (PCJKSHEADER)pbContent;
    if (pHdr->uMagic != JKS_MAGIC)
        return RTErrInfoAddF(pErrInfo, VERR_WRONG_TYPE /** @todo better status codes */,
                             "  Not java key store magic %#x (%s)", RT_BE2H_U32(pHdr->uMagic), pszErrorName);
    if (pHdr->uVersion != JKS_VERSION_2)
        return RTErrInfoAddF(pErrInfo, VERR_WRONG_TYPE /** @todo better status codes */,
                             "  Unsupported java key store version %#x (%s)", RT_BE2H_U32(pHdr->uVersion), pszErrorName);
    uint32_t const cEntries = RT_BE2H_U32(pHdr->cEntries);
    if (cEntries > cbContent / 24) /* 24 = 4 for type, 4+ alias, 8 byte timestamp, 4 byte len,  "X.509" or 4 cert count  */
        return RTErrInfoAddF(pErrInfo, VERR_WRONG_TYPE /** @todo better status codes */,
                             "  Entry count %u is to high for %zu byte JKS (%s)", cEntries, cbContent, pszErrorName);

    /*
     * Here we should check the store signature. However, it always includes
     * some kind of password, and that's somewhere we don't want to go right
     * now. Later perhaps.
     *
     * We subtract it from the content size to make EOF checks simpler.
     */
    int rc = VINF_SUCCESS;
#if 0 /* later */
    RTSHA1CONTEXT Ctx;
    RTSha1Init(&Ctx);

    const char *pszCur = pszPassword;
    for (;;)
    {
        RTUNICP Cp;
        rc = RTStrGetCpEx(&pszCur, &Cp);
        AssertRCReturn(rc, rc);
        if (!Cp)
            break;
        uint8_t abWChar[2];
        abWChar[0] = RT_BYTE2(Cp);
        abWChar[1] = RT_BYTE1(Cp);
        RTSha1Update(&Ctx, &abWChar, sizeof(abWChar));
    }

    RTSha1Update(&Ctx, RT_STR_TUPLE("Mighty Aphrodite"));

    RTSha1Update(&Ctx, pbContent, cbContent - RTSHA1_HASH_SIZE);

    uint8_t abSignature[RTSHA1_HASH_SIZE];
    RTSha1Final(&Ctx, abSignature);

    if (memcmp(&pbContent[cbContent - RTSHA1_HASH_SIZE], abSignature, RTSHA1_HASH_SIZE) != 0)
    {
        rc = RTErrInfoAddF(pErrInfo, VERR_MISMATCH, "  File SHA-1 signature mismatch, %.*Rhxs instead of %.*Rhxs, for '%s'",
                           RTSHA1_HASH_SIZE, abSignature,
                           RTSHA1_HASH_SIZE, &pbContent[cbContent - RTSHA1_HASH_SIZE],
                           pszErrorName);
        if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
            return rc;
    }
#endif
    cbContent -= RTSHA1_HASH_SIZE;


    /*
     * A bunch of macros to make decoding easier.
     */
#define ENSURE_CONTENT_OR_BREAK_EX(a_cbNeeded, a_pszWhat) \
    do { \
        if (RT_LIKELY(off + (a_cbNeeded) <= cbContent)) \
        { /* likely */ } \
        else  \
        { \
            rc = RTErrInfoAddF(pErrInfo, VERR_EOF, "  Unexpected end of data at %#x need %u bytes for %s (entry #%u in %s)",  \
                               off, a_cbNeeded, a_pszWhat, iEntry, pszErrorName); \
            break; \
        } \
    } while (0)
#define ENSURE_CONTENT_OR_BREAK(a_Var) ENSURE_CONTENT_OR_BREAK_EX(sizeof(a_Var), #a_Var)
#define GET_BE_U32_OR_BREAK(a_uVar) \
    do { \
        ENSURE_CONTENT_OR_BREAK(a_uVar); \
        AssertCompile(sizeof(a_uVar) == sizeof(uint32_t)); \
        a_uVar = RT_MAKE_U32_FROM_U8(pbContent[off + 3], pbContent[off + 2], pbContent[off + 1], pbContent[off + 0]); \
        off   += sizeof(uint32_t); \
    } while (0)
#define GET_BE_U16_OR_BREAK(a_uVar) \
    do { \
        ENSURE_CONTENT_OR_BREAK(a_uVar); \
        AssertCompile(sizeof(a_uVar) == sizeof(uint16_t)); \
        a_uVar = RT_MAKE_U16(pbContent[off + 1], pbContent[off + 0]); \
        off   += sizeof(uint16_t); \
    } while (0)
#define SKIP_CONTENT_BYTES_OR_BREAK(a_cbToSkip, a_pszWhat) \
    do { \
        ENSURE_CONTENT_OR_BREAK_EX(a_cbToSkip, a_pszWhat); \
        off   += a_cbToSkip; \
    } while (0)
#define CHECK_OR_BREAK(a_Expr, a_RTErrInfoAddFArgs) \
    do { \
        if (RT_LIKELY(a_Expr)) \
        { /* likely */ } \
        else \
        { \
            rc = RTErrInfoAddF a_RTErrInfoAddFArgs; \
            break; \
        } \
    } while (0)

    /*
     * Work our way thru the keystore.
     */
    Log(("JKS: %u entries - '%s'\n", cEntries, pszErrorName));
    size_t   off    = sizeof(JKSHEADER);
    uint32_t iEntry = 0;
    for (;;)
    {
        size_t const offEntry = off; NOREF(offEntry);

        /* The entry type. */
        uint32_t uType;
        GET_BE_U32_OR_BREAK(uType);
        CHECK_OR_BREAK(uType == 1 || uType == 2,
                       (pErrInfo, VERR_WRONG_TYPE, "  uType=%#x (entry #%u in %s)", uType, iEntry, pszErrorName));

        /* Skip the alias string. */
        uint16_t cbAlias;
        GET_BE_U16_OR_BREAK(cbAlias);
        SKIP_CONTENT_BYTES_OR_BREAK(cbAlias, "szAlias");

        /* Skip the creation timestamp. */
        SKIP_CONTENT_BYTES_OR_BREAK(sizeof(uint64_t), "tsCreated");

        uint32_t cTrustCerts = 0;
        if (uType == 1)
        {
            /*
             * It is a private key.
             */
            Log(("JKS: %#08zx: entry #%u: Private key\n", offEntry, iEntry));

            /* The encoded key. */
            uint32_t cbKey;
            GET_BE_U32_OR_BREAK(cbKey);
            SKIP_CONTENT_BYTES_OR_BREAK(cbKey, "key data");

            /* The number of trust certificates following it. */
            GET_BE_U32_OR_BREAK(cTrustCerts);
        }
        else if (uType == 2)
        {
            /*
             * It is a certificate.
             */
            Log(("JKS: %#08zx: entry #%u: Trust certificate\n", offEntry, iEntry));
            cTrustCerts = 1;
        }
        else
            AssertFailedBreakStmt(rc = VERR_INTERNAL_ERROR_2);

        /*
         * Decode trust certificates. Keys have 0 or more of these associated with them.
         */
        for (uint32_t iCert = 0; iCert < cTrustCerts; iCert++)
        {
            /* X.509 signature */
            static const char a_achCertType[] = { 0, 5, 'X', '.', '5', '0', '9' };
            ENSURE_CONTENT_OR_BREAK(a_achCertType);
            CHECK_OR_BREAK(memcmp(&pbContent[off], a_achCertType, sizeof(a_achCertType)) == 0,
                           (pErrInfo, VERR_WRONG_TYPE, "  Unsupported certificate type %.7Rhxs (entry #%u in %s)",
                            &pbContent[off], iEntry, pszErrorName));
            off += sizeof(a_achCertType);

            /* The encoded certificate length. */
            uint32_t cbEncoded;
            GET_BE_U32_OR_BREAK(cbEncoded);
            ENSURE_CONTENT_OR_BREAK_EX(cbEncoded, "certificate data");
            Log(("JKS: %#08zx: %#x certificate bytes\n", off, cbEncoded));

            /* Try add the certificate. */
            RTERRINFOSTATIC StaticErrInfo;
            int rc2 = RTCrStoreCertAddEncoded(hStore,
                                              RTCRCERTCTX_F_ENC_X509_DER | (fFlags & RTCRCERTCTX_F_ADD_IF_NOT_FOUND),
                                              &pbContent[off], cbEncoded, RTErrInfoInitStatic(&StaticErrInfo));
            if (RT_FAILURE(rc2))
            {
                if (RTErrInfoIsSet(&StaticErrInfo.Core))
                    rc = RTErrInfoAddF(pErrInfo, rc2, "  entry #%u: %s", iEntry, StaticErrInfo.Core.pszMsg);
                else
                    rc = RTErrInfoAddF(pErrInfo, rc2, "  entry #%u: %Rrc adding cert", iEntry, rc2);
                if (!(fFlags & RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR))
                    break;
            }
            off += cbEncoded;
        }

        /*
         * Advance.
         */
        iEntry++;
        if (iEntry >= cEntries)
        {
            if (off != cbContent)
                rc = RTErrInfoAddF(pErrInfo, VERR_TOO_MUCH_DATA, "  %zu tailing bytes (%s)", cbContent - off, pszErrorName);
            break;
        }
    }

    return rc;
}


RTDECL(int) RTCrStoreCertAddFromJavaKeyStore(RTCRSTORE hStore, uint32_t fFlags, const char *pszFilename, PRTERRINFO pErrInfo)
{
    AssertReturn(!(fFlags & ~(RTCRCERTCTX_F_ADD_IF_NOT_FOUND | RTCRCERTCTX_F_ADD_CONTINUE_ON_ERROR)), VERR_INVALID_FLAGS);

    /*
     * Read the whole thing into memory as that's much more convenient to work
     * with and we don't expect a java key store to take up a lot of space.
     */
    size_t      cbContent;
    void        *pvContent;
    int rc = RTFileReadAllEx(pszFilename, 0, 32U*_1M, RTFILE_RDALL_O_DENY_WRITE, &pvContent, &cbContent);
    if (RT_SUCCESS(rc))
    {
        rc = RTCrStoreCertAddFromJavaKeyStoreInMem(hStore, fFlags, pvContent, cbContent, pszFilename, pErrInfo);
        RTFileReadAllFree(pvContent, cbContent);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, rc, "RTFileReadAllEx failed with %Rrc on '%s'", rc, pszFilename);
    return rc;
}
RT_EXPORT_SYMBOL(RTCrStoreCertAddFromJavaKeyStore);

