/* $Id: openssl-sha3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - SHA-3 hash functions, OpenSSL based implementation.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#if 1 /* For now: */
# include "alt-sha3.cpp"

#else


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>

#include "internal/openssl-pre.h"
#include <openssl/evp.h>
#include "internal/openssl-post.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTSHA3PRIVATECTX_MAGIC          UINT64_C(0xb6362d323c56b758)
#define RTSHA3PRIVATECTX_MAGIC_FINAL    UINT64_C(0x40890fe0e474215d)
#define RTSHA3PRIVATECTX_MAGIC_DEAD     UINT64_C(0xdead7a05081cbeef)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* Internal EVP structure that we fake here to avoid lots of casting. */
struct evp_md_ctx_st
{
    void *apvWhatever[10];
};

/** The OpenSSL private context structure. */
typedef struct RTSHA3PRIVATECTX
{
    /** RTSHA3PRIVATECTX_MAGIC / RTSHA3PRIVATECTX_MAGIC_FINAL / RTSHA3PRIVATECTX_MAGIC_DEAD */
    uint64_t                u64Magic;
    /** The OpenSSL context.  We cheat to avoid EVP_MD_CTX_new/free. */
    struct evp_md_ctx_st    MdCtx;
} RTSHA3PRIVATECTX;

#define RT_SHA3_PRIVATE_CONTEXT
#include <iprt/sha.h>
AssertCompile(RT_SIZEOFMEMB(RTSHA3CONTEXT, abPadding) >= RT_SIZEOFMEMB(RTSHA3CONTEXT, Private));



static int rtSha3Init(PRTSHA3CONTEXT pCtx, const EVP_MD *pMdType)
{
    RT_ZERO(*pCtx); /* This is what EVP_MD_CTX_new does. */
    pCtx->Private.u64Magic = RTSHA3PRIVATECTX_MAGIC;

    AssertReturnStmt(EVP_DigestInit_ex(&pCtx->Private.MdCtx, pMdType, NULL /*engine*/),
                     pCtx->Private.u64Magic = RTSHA3PRIVATECTX_MAGIC_DEAD,
                     VERR_CR_DIGEST_OSSL_DIGEST_INIT_ERROR);
    return VINF_SUCCESS;
}


static int rtSha3Update(PRTSHA3CONTEXT pCtx, uint8_t const *pbData, size_t cbData)
{
    AssertMsgReturn(pCtx->Private.u64Magic == RTSHA3PRIVATECTX_MAGIC, ("u64Magic=%RX64\n", pCtx->Private.u64Magic),
                    VERR_INVALID_CONTEXT);
    AssertReturn(EVP_DigestUpdate(&pCtx->Private.MdCtx, pbData, cbData), VERR_GENERAL_FAILURE);
    return VINF_SUCCESS;
}


static int rtSha3Final(PRTSHA3CONTEXT pCtx, uint8_t *pbDigest, size_t cbDigest)
{
    RT_BZERO(pbDigest, cbDigest);
    AssertMsgReturn(pCtx->Private.u64Magic == RTSHA3PRIVATECTX_MAGIC, ("u64Magic=%RX64\n", pCtx->Private.u64Magic),
                    VERR_INVALID_CONTEXT);
    AssertReturn(EVP_DigestFinal_ex(&pCtx->Private.MdCtx, pbDigest, NULL), VERR_GENERAL_FAILURE);

    /* Implicit cleanup. */
    EVP_MD_CTX_reset(&pCtx->Private.MdCtx);
    pCtx->Private.u64Magic = RTSHA3PRIVATECTX_MAGIC_FINAL;
    return VINF_SUCCESS;
}


static int rtSha3Cleanup(PRTSHA3CONTEXT pCtx)
{
    if (pCtx)
    {
        if (pCtx->Private.u64Magic == RTSHA3PRIVATECTX_MAGIC_FINAL)
        { /* likely */ }
        else if (pCtx->Private.u64Magic == RTSHA3PRIVATECTX_MAGIC)
            EVP_MD_CTX_reset(&pCtx->Private.MdCtx);
        else
            AssertMsgFailedReturn(("u64Magic=%RX64\n", pCtx->Private.u64Magic), VERR_INVALID_CONTEXT);
        pCtx->Private.u64Magic = RTSHA3PRIVATECTX_MAGIC_DEAD;
    }
    return VINF_SUCCESS;
}


static int rtSha3Clone(PRTSHA3CONTEXT pCtx, RTSHA3CONTEXT const *pCtxSrc)
{
    Assert(pCtx->Private.u64Magic != RTSHA3PRIVATECTX_MAGIC);
    RT_ZERO(*pCtx); /* This is what EVP_MD_CTX_new does. */

    AssertReturn(pCtxSrc->Private.u64Magic == RTSHA3PRIVATECTX_MAGIC, VERR_INVALID_CONTEXT);

    pCtx->Private.u64Magic = RTSHA3PRIVATECTX_MAGIC;
    AssertReturnStmt(EVP_MD_CTX_copy_ex(&pCtx->Private.MdCtx, &pCtxSrc->Private.MdCtx),
                     pCtx->Private.u64Magic = RTSHA3PRIVATECTX_MAGIC_DEAD,
                     VERR_CR_DIGEST_OSSL_DIGEST_CTX_COPY_ERROR);
    return VINF_SUCCESS;
}


static int rtSha3(const void *pvData, size_t cbData, const EVP_MD *pMdType, uint8_t *pabHash, size_t cbHash)
{
    RT_BZERO(pabHash, cbHash);

    int rc;
    EVP_MD_CTX *pCtx = EVP_MD_CTX_new();
    if (pCtx)
    {
        if (EVP_DigestInit_ex(pCtx, pMdType, NULL /*engine*/))
        {
            if (EVP_DigestUpdate(pCtx, pvData, cbData))
            {
                if (EVP_DigestFinal_ex(pCtx, pabHash, NULL))
                    rc = VINF_SUCCESS;
                else
                    AssertFailedStmt(rc = VERR_GENERAL_FAILURE);
            }
            else
                AssertFailedStmt(rc = VERR_GENERAL_FAILURE);
        }
        else
            AssertFailedStmt(rc = VERR_CR_DIGEST_OSSL_DIGEST_INIT_ERROR);
        EVP_MD_CTX_free(pCtx);
    }
    else
        AssertFailedStmt(rc = VERR_NO_MEMORY);
    return rc;
}


static bool rtSha3Check(const void *pvData, size_t cbData, const EVP_MD *pMdType,
                        const uint8_t *pabHash, uint8_t *pabHashTmp, size_t cbHash)
{
    int rc = rtSha3(pvData, cbData, pMdType, pabHashTmp, cbHash);
    return RT_SUCCESS(rc) && memcmp(pabHash, pabHashTmp, cbHash) == 0;
}


/** Macro for declaring the interface for a SHA3 variation.
 * @internal */
#define RTSHA3_DEFINE_VARIANT(a_cBits, a_pMdType) \
AssertCompile((a_cBits / 8) == RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)); \
\
RTDECL(int) RT_CONCAT(RTSha3t,a_cBits)(const void *pvBuf, size_t cbBuf, uint8_t pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    return rtSha3(pvBuf, cbBuf, a_pMdType, pabHash, (a_cBits) / 8); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT(RTSha3t,a_cBits)); \
\
\
RTDECL(bool) RT_CONCAT3(RTSha3t,a_cBits,Check)(const void *pvBuf, size_t cbBuf, \
                                               uint8_t const pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    uint8_t abHashTmp[(a_cBits) / 8]; \
    return rtSha3Check(pvBuf, cbBuf, a_pMdType, pabHash, abHashTmp, (a_cBits) / 8); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Check)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Init)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx) \
{ \
    return rtSha3Init(&pCtx->Sha3, a_pMdType); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Init)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Update)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx, const void *pvBuf, size_t cbBuf) \
{ \
    return rtSha3Update(&pCtx->Sha3, (uint8_t const *)pvBuf, cbBuf); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Update)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Final)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx, \
                                              uint8_t pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    return rtSha3Final(&pCtx->Sha3, pabHash, (a_cBits) / 8); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Final)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Cleanup)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx) \
{ \
    return rtSha3Cleanup(&pCtx->Sha3); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Cleanup)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,Clone)(RT_CONCAT3(PRTSHA3T,a_cBits,CONTEXT) pCtx, \
                                              RT_CONCAT3(RTSHA3T,a_cBits,CONTEXT) const *pCtxSrc) \
{ \
    return rtSha3Clone(&pCtx->Sha3, &pCtxSrc->Sha3); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,Clone)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,ToString)(uint8_t const pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)], \
                                                 char *pszDigest, size_t cchDigest) \
{ \
    return RTStrPrintHexBytes(pszDigest, cchDigest, pabHash, (a_cBits) / 8, 0 /*fFlags*/); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,ToString)); \
\
\
RTDECL(int) RT_CONCAT3(RTSha3t,a_cBits,FromString)(char const *pszDigest, uint8_t pabHash[RT_CONCAT3(RTSHA3_,a_cBits,_HASH_SIZE)]) \
{ \
    return RTStrConvertHexBytes(RTStrStripL(pszDigest), &pabHash[0], (a_cBits) / 8, 0 /*fFlags*/); \
} \
RT_EXPORT_SYMBOL(RT_CONCAT3(RTSha3t,a_cBits,FromString))


RTSHA3_DEFINE_VARIANT(224, EVP_sha3_224());
RTSHA3_DEFINE_VARIANT(256, EVP_sha3_256());
RTSHA3_DEFINE_VARIANT(384, EVP_sha3_384());
RTSHA3_DEFINE_VARIANT(512, EVP_sha3_512());

#endif /* !alt-sha3.cpp */
