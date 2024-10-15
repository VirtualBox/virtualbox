/* $Id: cipher-openssl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Symmetric Cipher using OpenSSL.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt.h"
# include <iprt/crypto/cipher.h>

# include <iprt/asm.h>
# include <iprt/assert.h>
# include <iprt/err.h>
# include <iprt/mem.h>
# include <iprt/string.h>

# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include "internal/openssl-post.h"

# include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(EVP_CTRL_AEAD_GET_TAG)
# define MY_EVP_CTRL_AEAD_GET_TAG EVP_CTRL_AEAD_GET_TAG
#else
# define MY_EVP_CTRL_AEAD_GET_TAG EVP_CTRL_GCM_GET_TAG
#endif

#if defined(EVP_CTRL_AEAD_SET_TAG)
# define MY_EVP_CTRL_AEAD_SET_TAG EVP_CTRL_AEAD_SET_TAG
#else
# define MY_EVP_CTRL_AEAD_SET_TAG EVP_CTRL_GCM_SET_TAG
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * OpenSSL cipher instance data.
 */
typedef struct RTCRCIPHERINT
{
    /** Magic value (RTCRCIPHERINT_MAGIC). */
    uint32_t            u32Magic;
    /** Reference count. */
    uint32_t volatile   cRefs;
    /** The cihper. */
    const EVP_CIPHER   *pCipher;
    /** The IPRT cipher type, if we know it. */
    RTCRCIPHERTYPE      enmType;
} RTCRCIPHERINT;


/**
 * OpenSSL cipher context data
 */
typedef struct RTCRCIPHERCTXINT
{
    /** Pointer to cipher instance data */
    RTCRCIPHERINT      *phCipher;
    /** Pointer to cipher context */
    EVP_CIPHER_CTX     *pCipherCtx;
    /** Is decryption */
    bool                fDecryption;
} RTCRCIPHERCTXINT;


RTDECL(int) RTCrCipherOpenByType(PRTCRCIPHER phCipher, RTCRCIPHERTYPE enmType, uint32_t fFlags)
{
    AssertPtrReturn(phCipher, VERR_INVALID_POINTER);
    *phCipher = NIL_RTCRCIPHER;
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

    /*
     * Translate the IPRT cipher type to EVP cipher.
     */
    const EVP_CIPHER *pCipher = NULL;
    switch (enmType)
    {
        case RTCRCIPHERTYPE_XTS_AES_128:
            pCipher = EVP_aes_128_xts();
            break;
        case RTCRCIPHERTYPE_XTS_AES_256:
            pCipher = EVP_aes_256_xts();
            break;
        case RTCRCIPHERTYPE_GCM_AES_128:
            pCipher = EVP_aes_128_gcm();
            break;
        case RTCRCIPHERTYPE_GCM_AES_256:
            pCipher = EVP_aes_256_gcm();
            break;
        case RTCRCIPHERTYPE_CTR_AES_128:
            pCipher = EVP_aes_128_ctr();
            break;
        case RTCRCIPHERTYPE_CTR_AES_256:
            pCipher = EVP_aes_256_ctr();
            break;

        /* no default! */
        case RTCRCIPHERTYPE_INVALID:
        case RTCRCIPHERTYPE_END:
        case RTCRCIPHERTYPE_32BIT_HACK:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }
    AssertReturn(pCipher, VERR_CR_CIPHER_NOT_SUPPORTED);

    /*
     * Create the instance.
     */
    RTCRCIPHERINT *pThis = (RTCRCIPHERINT *)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->u32Magic = RTCRCIPHERINT_MAGIC;
        pThis->cRefs    = 1;
        pThis->pCipher  = pCipher;
        pThis->enmType  = enmType;
        *phCipher = pThis;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


RTDECL(uint32_t) RTCrCipherRetain(RTCRCIPHER hCipher)
{
    RTCRCIPHERINT *pThis = hCipher;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1 && cRefs < 1024);
    return cRefs;
}


/**
 * Destroys the cipher instance.
 */
static uint32_t rtCrCipherDestroy(RTCRCIPHER pThis)
{
    pThis->u32Magic= ~RTCRCIPHERINT_MAGIC;
    pThis->pCipher = NULL;
    RTMemFree(pThis);
    return 0;
}


RTDECL(uint32_t) RTCrCipherRelease(RTCRCIPHER hCipher)
{
    RTCRCIPHERINT *pThis = hCipher;
    if (pThis == NIL_RTCRCIPHER)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < 1024);
    if (cRefs == 0)
        return rtCrCipherDestroy(pThis);
    return cRefs;
}


RTDECL(uint32_t) RTCrCipherGetKeyLength(RTCRCIPHER hCipher)
{
    RTCRCIPHERINT *pThis = hCipher;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, 0);

    return EVP_CIPHER_key_length(pThis->pCipher);
}


RTDECL(uint32_t) RTCrCipherGetInitializationVectorLength(RTCRCIPHER hCipher)
{
    RTCRCIPHERINT *pThis = hCipher;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, 0);

    return EVP_CIPHER_iv_length(pThis->pCipher);
}


RTDECL(uint32_t) RTCrCipherGetBlockSize(RTCRCIPHER hCipher)
{
    RTCRCIPHERINT *pThis = hCipher;
    AssertPtrReturn(pThis, 0);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, 0);

    return EVP_CIPHER_block_size(pThis->pCipher);
}


RTDECL(int) RTCrCipherCtxFree(RTCRCIPHERCTX hCipherCtx)
{
    AssertReturn(hCipherCtx, VERR_INVALID_PARAMETER);
    RTCRCIPHERCTXINT *pCtx = hCipherCtx;

# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
        EVP_CIPHER_CTX_free(pCtx->pCipherCtx);
# else
        EVP_CIPHER_CTX_cleanup(pCtx->pCipherCtx);
        RTMemFree(pCtx->pCipherCtx);
# endif
        RTMemFree(pCtx);

    return VINF_SUCCESS;
}


RTDECL(int) RTCrCipherCtxEncryptInit(RTCRCIPHER hCipher, void const *pvKey, size_t cbKey,
                                     void const *pvInitVector, size_t cbInitVector,
                                     void const *pvAuthData, size_t cbAuthData,
                                     PRTCRCIPHERCTX phCipherCtx)
{
     /*
     * Validate input.
     */
    RTCRCIPHERINT *pThis = hCipher;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn((ssize_t)cbKey == EVP_CIPHER_key_length(pThis->pCipher),
                    ("%zu, expected %d\n", cbKey, EVP_CIPHER_key_length(pThis->pCipher)),
                    VERR_CR_CIPHER_INVALID_KEY_LENGTH);
    AssertMsgReturn((ssize_t)cbInitVector == EVP_CIPHER_iv_length(pThis->pCipher),
                    ("%zu, expected %d\n", cbInitVector, EVP_CIPHER_iv_length(pThis->pCipher)),
                    VERR_CR_CIPHER_INVALID_INITIALIZATION_VECTOR_LENGTH);

    Assert(EVP_CIPHER_block_size(pThis->pCipher) <= 1); /** @todo more complicated ciphers later */

    /*
     * Allocate and initialize the cipher context.
     */
    int rc = VERR_NO_MEMORY;
    /*
     * Create the instance.
     */
    RTCRCIPHERCTXINT *pCtx = (RTCRCIPHERCTXINT *)RTMemAlloc(sizeof(RTCRCIPHERCTXINT));
    if (pCtx)
    {
        pCtx->phCipher = hCipher;
        pCtx->fDecryption = false;
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
        pCtx->pCipherCtx = EVP_CIPHER_CTX_new();
        if (pCtx->pCipherCtx)
# else
        pCtx->pCipherCtx = (EVP_CIPHER_CTX *)RTMemAllocZ(sizeof(EVP_CIPHER_CTX));
# endif
        {
            if (EVP_EncryptInit(pCtx->pCipherCtx, pCtx->phCipher->pCipher, (unsigned char const *)pvKey,
                                (unsigned char const *)pvInitVector))
            {
                if (pvAuthData && cbAuthData)
                {
                    /* Add auth data. */
                    int cbEncryptedAuth = 0;
                    rc = EVP_EncryptUpdate(pCtx->pCipherCtx, NULL, &cbEncryptedAuth,
                                           (unsigned char const *)pvAuthData, (int)cbAuthData) ? VINF_SUCCESS
                         : VERR_CR_CIPHER_OSSL_ENCRYPT_UPDATE_FAILED;
                }
                else
                    rc = VINF_SUCCESS;
            }
            else
                rc = VERR_CR_CIPHER_OSSL_ENCRYPT_INIT_FAILED;
        }
    }

    if (RT_SUCCESS(rc))
        *phCipherCtx = pCtx;
    else
        RTCrCipherCtxFree(pCtx);
    return rc;
}


RTDECL(int) RTCrCipherCtxEncryptProcess(RTCRCIPHERCTX hCipherCtx, void const *pvPlainText, size_t cbPlainText,
                                        void *pvEncrypted, size_t cbEncrypted, size_t *pcbEncrypted)
{
    AssertReturn(hCipherCtx, VERR_INVALID_PARAMETER);
    AssertReturn(cbPlainText > 0, VERR_NO_DATA);
    AssertReturn((size_t)(int)cbPlainText == cbPlainText && (int)cbPlainText > 0, VERR_OUT_OF_RANGE);
    AssertReturn(cbEncrypted >= cbPlainText, VERR_BUFFER_OVERFLOW);

    RTCRCIPHERCTXINT *pCtx = hCipherCtx;
    AssertReturn(!pCtx->fDecryption, VERR_INVALID_STATE);
    int cbEncrypted1 = 0;
    int rc = VERR_CR_CIPHER_OSSL_ENCRYPT_UPDATE_FAILED;
    if (EVP_EncryptUpdate(pCtx->pCipherCtx, (unsigned char *)pvEncrypted, &cbEncrypted1,
                          (unsigned char const *)pvPlainText, (int)cbPlainText))
    {
        *pcbEncrypted = cbEncrypted1;
        rc = VINF_SUCCESS;
    }
    return rc;
}


RTDECL(int) RTCrCipherCtxEncryptFinish(RTCRCIPHERCTX hCipherCtx,
                                       void *pvEncrypted, size_t *pcbEncrypted,
                                       void *pvTag, size_t cbTag, size_t *pcbTag)
{
    AssertReturn(hCipherCtx, VERR_INVALID_PARAMETER);
    RTCRCIPHERCTXINT *pCtx = hCipherCtx;
    AssertReturn(!pCtx->fDecryption, VERR_INVALID_STATE);
    AssertReturn(!pvTag || (pvTag && cbTag == 16), VERR_CR_CIPHER_INVALID_TAG_LENGTH);
    int cbEncrypted2 = 0;
    int rc = VERR_CR_CIPHER_OSSL_ENCRYPT_FINAL_FAILED;
    if (EVP_EncryptFinal(pCtx->pCipherCtx, (uint8_t *)pvEncrypted, &cbEncrypted2))
    {
        if (pvTag && cbTag)
        {
            if (EVP_CIPHER_CTX_ctrl(pCtx->pCipherCtx, MY_EVP_CTRL_AEAD_GET_TAG, (int)cbTag, pvTag))
            {
                *pcbTag = cbTag;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_CR_CIPHER_OSSL_GET_TAG_FAILED;
        }
        else
            rc = VINF_SUCCESS;

        if (RT_SUCCESS(rc) && pcbEncrypted)
            *pcbEncrypted = cbEncrypted2;
    }

    return rc;
}


RTDECL(int) RTCrCipherCtxDecryptInit(RTCRCIPHER hCipher, void const *pvKey, size_t cbKey,
                                     void const *pvInitVector, size_t cbInitVector,
                                     void const *pvAuthData, size_t cbAuthData,
                                     void *pvTag, size_t cbTag, PRTCRCIPHERCTX phCipherCtx)
{
    /*
     * Validate input.
     */
    RTCRCIPHERINT *pThis = hCipher;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTCRCIPHERINT_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn((ssize_t)cbKey == EVP_CIPHER_key_length(pThis->pCipher),
                    ("%zu, expected %d\n", cbKey, EVP_CIPHER_key_length(pThis->pCipher)),
                    VERR_CR_CIPHER_INVALID_KEY_LENGTH);
    AssertMsgReturn((ssize_t)cbInitVector == EVP_CIPHER_iv_length(pThis->pCipher),
                    ("%zu, expected %d\n", cbInitVector, EVP_CIPHER_iv_length(pThis->pCipher)),
                    VERR_CR_CIPHER_INVALID_INITIALIZATION_VECTOR_LENGTH);
    AssertReturn(!pvTag || (pvTag && cbTag == 16), VERR_CR_CIPHER_INVALID_TAG_LENGTH);

    Assert(EVP_CIPHER_block_size(pThis->pCipher) <= 1); /** @todo more complicated ciphers later */

    /*
     * Allocate and initialize the cipher context.
     */
    int rc = VERR_NO_MEMORY;
    /*
     * Create the instance.
     */
    RTCRCIPHERCTXINT *pCtx = (RTCRCIPHERCTXINT *)RTMemAlloc(sizeof(RTCRCIPHERCTXINT));
    if (pCtx)
    {
        pCtx->phCipher = hCipher;
        pCtx->fDecryption = true;
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
        pCtx->pCipherCtx = EVP_CIPHER_CTX_new();
# else
        pCtx->pCipherCtx = (EVP_CIPHER_CTX *)RTMemAllocZ(sizeof(EVP_CIPHER_CTX));
# endif

        if (EVP_DecryptInit(pCtx->pCipherCtx, pThis->pCipher, (unsigned char const *)pvKey,
                            (unsigned char const *)pvInitVector))
        {
            rc = VINF_SUCCESS;
            if (pvTag && cbTag && !EVP_CIPHER_CTX_ctrl(pCtx->pCipherCtx, MY_EVP_CTRL_AEAD_SET_TAG, (int)cbTag, pvTag))
                rc = VERR_CR_CIPHER_OSSL_SET_TAG_FAILED;

            if (RT_SUCCESS(rc) && pvAuthData && cbAuthData)
            {
                /* Add auth data. */
                int cbDecryptedAuth = 0;
                if (!EVP_DecryptUpdate(pCtx->pCipherCtx, NULL, &cbDecryptedAuth,
                                        (unsigned char const *)pvAuthData, (int)cbAuthData))
                    rc = VERR_CR_CIPHER_OSSL_DECRYPT_UPDATE_FAILED;
            }
        }
        else
            rc = VERR_CR_CIPHER_OSSL_DECRYPT_INIT_FAILED;
    }

    if (RT_SUCCESS(rc))
        *phCipherCtx = pCtx;
    else
        RTCrCipherCtxFree(pCtx);

    return rc;
}


RTDECL(int) RTCrCipherCtxDecryptProcess(RTCRCIPHERCTX hCipherCtx,
                                        void const *pvEncrypted, size_t cbEncrypted,
                                        void *pvPlainText, size_t cbPlainText, size_t *pcbPlainText)
{
    AssertReturn(hCipherCtx, VERR_INVALID_PARAMETER);
    AssertReturn(cbEncrypted > 0, VERR_NO_DATA);
    AssertReturn((size_t)(int)cbEncrypted == cbEncrypted && (int)cbEncrypted > 0, VERR_OUT_OF_RANGE);
    AssertReturn(cbPlainText >= cbEncrypted, VERR_BUFFER_OVERFLOW);

    RTCRCIPHERCTXINT *pCtx = hCipherCtx;
    AssertReturn(pCtx->fDecryption, VERR_INVALID_STATE);
    int rc = VERR_CR_CIPHER_OSSL_DECRYPT_UPDATE_FAILED;
    int cbDecrypted1 = 0;
    if (EVP_DecryptUpdate(pCtx->pCipherCtx, (unsigned char *)pvPlainText, &cbDecrypted1,
                          (unsigned char const *)pvEncrypted, (int)cbEncrypted))
    {
        *pcbPlainText = cbDecrypted1;
        rc = VINF_SUCCESS;
    }
    return rc;
}


RTDECL(int) RTCrCipherCtxDecryptFinish(RTCRCIPHERCTX hCipherCtx,
                                       void *pvPlainText, size_t *pcbPlainText)
{
    AssertReturn(hCipherCtx, VERR_INVALID_PARAMETER);
    RTCRCIPHERCTXINT *pCtx = hCipherCtx;
    AssertReturn(pCtx->fDecryption, VERR_INVALID_STATE);
    int cbDecrypted2 = 0;
    int rc = VERR_CR_CIPHER_OSSL_ENCRYPT_FINAL_FAILED;
    if (EVP_DecryptFinal(pCtx->pCipherCtx, (uint8_t *)pvPlainText, &cbDecrypted2))
    {
        rc = VINF_SUCCESS;
        if (pcbPlainText)
            *pcbPlainText = cbDecrypted2;
    }

    return rc;
}


RTDECL(int) RTCrCipherEncrypt(RTCRCIPHER hCipher, void const *pvKey, size_t cbKey,
                              void const *pvInitVector, size_t cbInitVector,
                              void const *pvPlainText, size_t cbPlainText,
                              void *pvEncrypted, size_t cbEncrypted, size_t *pcbEncrypted)
{
    return RTCrCipherEncryptEx(hCipher, pvKey, cbKey, pvInitVector, cbInitVector,
                               NULL, 0, pvPlainText, cbPlainText, pvEncrypted, cbEncrypted,
                               pcbEncrypted, NULL, 0, NULL);
}


RTDECL(int) RTCrCipherDecrypt(RTCRCIPHER hCipher, void const *pvKey, size_t cbKey,
                              void const *pvInitVector, size_t cbInitVector,
                              void const *pvEncrypted, size_t cbEncrypted,
                              void *pvPlainText, size_t cbPlainText, size_t *pcbPlainText)
{
    return RTCrCipherDecryptEx(hCipher, pvKey, cbKey, pvInitVector, cbInitVector,
                               NULL, 0, NULL, 0, pvEncrypted, cbEncrypted,
                               pvPlainText, cbPlainText, pcbPlainText);
}


RTDECL(int) RTCrCipherEncryptEx(RTCRCIPHER hCipher, void const *pvKey, size_t cbKey,
                                void const *pvInitVector, size_t cbInitVector,
                                void const *pvAuthData, size_t cbAuthData,
                                void const *pvPlainText, size_t cbPlainText,
                                void *pvEncrypted, size_t cbEncrypted, size_t *pcbEncrypted,
                                void *pvTag, size_t cbTag, size_t *pcbTag)
{
    size_t const cbNeeded = cbPlainText;
    if (pcbEncrypted)
    {
        *pcbEncrypted = cbNeeded;
        AssertReturn(cbEncrypted >= cbNeeded, VERR_BUFFER_OVERFLOW);
    }
    else
        AssertReturn(cbEncrypted == cbNeeded, VERR_INVALID_PARAMETER);
    AssertReturn((size_t)(int)cbPlainText == cbPlainText && (int)cbPlainText > 0, VERR_OUT_OF_RANGE);

    RTCRCIPHERCTXINT *pCtx = NIL_RTCRCIPHERCTX;

    int rc = RTCrCipherCtxEncryptInit(hCipher, pvKey, cbKey, pvInitVector, cbInitVector,
                                      pvAuthData, cbAuthData, &pCtx);
    if (RT_SUCCESS(rc))
    {
        size_t cbEncrypted1 = 0;
        rc = RTCrCipherCtxEncryptProcess(pCtx, pvPlainText, cbPlainText, pvEncrypted, cbEncrypted, &cbEncrypted1);
        if (RT_SUCCESS(rc))
        {
            size_t cbEncrypted2 = 0;
            rc = RTCrCipherCtxEncryptFinish(pCtx, (unsigned char *)pvEncrypted + cbEncrypted1,
                                            &cbEncrypted2, pvTag, cbTag, pcbTag);
            if (RT_SUCCESS(rc))
            {
                Assert(cbEncrypted1 + cbEncrypted2 == cbNeeded);
                if (pcbEncrypted)
                    *pcbEncrypted = cbEncrypted1 + cbEncrypted2;
            }
        }
    }

    if (pCtx != NIL_RTCRCIPHERCTX)
        RTCrCipherCtxFree(pCtx);

    return rc;
}


RTDECL(int) RTCrCipherDecryptEx(RTCRCIPHER hCipher, void const *pvKey, size_t cbKey,
                                void const *pvInitVector, size_t cbInitVector,
                                void const *pvAuthData, size_t cbAuthData,
                                void *pvTag, size_t cbTag,
                                void const *pvEncrypted, size_t cbEncrypted,
                                void *pvPlainText, size_t cbPlainText, size_t *pcbPlainText)
{
    size_t const cbNeeded = cbEncrypted;
    if (pcbPlainText)
    {
        *pcbPlainText = cbNeeded;
        AssertReturn(cbPlainText >= cbNeeded, VERR_BUFFER_OVERFLOW);
    }
    else
        AssertReturn(cbPlainText == cbNeeded, VERR_INVALID_PARAMETER);
    AssertReturn((size_t)(int)cbEncrypted == cbEncrypted && (int)cbEncrypted > 0, VERR_OUT_OF_RANGE);

    RTCRCIPHERCTXINT *pCtx = NIL_RTCRCIPHERCTX;

    int rc = RTCrCipherCtxDecryptInit(hCipher, pvKey, cbKey, pvInitVector, cbInitVector,
                                      pvAuthData, cbAuthData, pvTag, cbTag, &pCtx);
    if (RT_SUCCESS(rc))
    {
        size_t cbDecrypted1 = 0;
        rc = RTCrCipherCtxDecryptProcess(pCtx, pvEncrypted, cbEncrypted, pvPlainText, cbPlainText, &cbDecrypted1);
        if (RT_SUCCESS(rc))
        {
            size_t cbDecrypted2 = 0;
            rc = RTCrCipherCtxDecryptFinish(pCtx, (unsigned char *)pvPlainText + cbDecrypted1,
                                            &cbDecrypted2);
            if (RT_SUCCESS(rc))
            {
                Assert(cbDecrypted1 + cbDecrypted2 == cbNeeded);
                if (pcbPlainText)
                    *pcbPlainText = cbDecrypted1 + cbDecrypted2;
            }
        }
    }

    if (pCtx != NIL_RTCRCIPHERCTX)
        RTCrCipherCtxFree(pCtx);

    return rc;
}

#endif /* IPRT_WITH_OPENSSL */

