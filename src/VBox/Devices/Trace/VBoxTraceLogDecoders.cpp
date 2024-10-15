/* $Id: VBoxTraceLogDecoders.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * RTTraceLogDecoders - Implement decoders for the tracing driver.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/tracelog-decoder-plugin.h>

#include <iprt/formats/tpm.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * TPM decoder context.
 */
typedef struct TPMDECODECTX
{
    /** Pointer to the next data item. */
    const uint8_t       *pbBuf;
    /** Number of bytes left for the buffer. */
    size_t              cbLeft;
    /** Flag whether an error was encountered. */
    bool                fError;
} TPMDECODECTX;
/** Pointer to a TPM decoder context. */
typedef TPMDECODECTX *PTPMDECODECTX;


/**
 * Algorithm ID to string mapping.
 */
typedef struct TPMALGID2STR
{
    uint16_t    u16AlgId;
    const char *pszAlgId;
    size_t      cbDigest;
} TPMALGID2STR;
typedef const TPMALGID2STR *PCTPMALGID2STR;


/**
 * The TPM state.
 */
typedef struct TPMSTATE
{
    /** Command code. */
    uint32_t                            u32CmdCode;
    /** Command code dependent state. */
    union
    {
        /** TPM2_CC_GET_CAPABILITY related state. */
        struct
        {
            /** The capability group to query. */
            uint32_t                    u32Cap;
            /** Property to query. */
            uint32_t                    u32Property;
            /** Number of values to return. */
            uint32_t                    u32Count;
        } GetCapability;
        /** TPM2_GET_RANDOM related state. */
        struct
        {
            /** Number of bytes of random data to produce. */
            uint16_t                    cbRnd;
        } GetRandom;
    } u;
} TPMSTATE;
typedef TPMSTATE *PTPMSTATE;


/**
 */
typedef DECLCALLBACKTYPE(void, FNDECODETPM2CCREQ, (PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx));
/** Pointer to an event decode request callback. */
typedef FNDECODETPM2CCREQ *PFNFNDECODETPM2CCREQ;


/**
 */
typedef DECLCALLBACKTYPE(void, FNDECODETPM2CCRESP, (PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx));
/** Pointer to an event decode request callback. */
typedef FNDECODETPM2CCRESP *PFNFNDECODETPM2CCRESP;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Algorithm ID to string mapping array.
 */
static const RTTRACELOGDECODERSTRUCTBLDENUM g_aAlgId2Str[] =
{
#define TPM_ALGID_2_STR(a_AlgId) { a_AlgId, #a_AlgId, 0 }
#define TPM_ALGID_2_STR_DIGEST(a_AlgId, a_cbDigest) { a_AlgId, #a_AlgId, a_cbDigest }

    TPM_ALGID_2_STR(TPM2_ALG_ERROR),
    TPM_ALGID_2_STR(TPM2_ALG_RSA),
    TPM_ALGID_2_STR(TPM2_ALG_TDES),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA1,       20),
    TPM_ALGID_2_STR(TPM2_ALG_HMAC),
    TPM_ALGID_2_STR(TPM2_ALG_AES),
    TPM_ALGID_2_STR(TPM2_ALG_MGF1),
    TPM_ALGID_2_STR(TPM2_ALG_KEYEDHASH),
    TPM_ALGID_2_STR(TPM2_ALG_XOR),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA256,     32),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA384,     48),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA512,     64),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA256_192, 24),
    TPM_ALGID_2_STR(TPM2_ALG_NULL),
    TPM_ALGID_2_STR(TPM2_ALG_SM3_256),
    TPM_ALGID_2_STR(TPM2_ALG_SM4),
    TPM_ALGID_2_STR(TPM2_ALG_RSASSA),
    TPM_ALGID_2_STR(TPM2_ALG_RSAES),
    TPM_ALGID_2_STR(TPM2_ALG_RSAPSS),
    TPM_ALGID_2_STR(TPM2_ALG_OAEP),
    TPM_ALGID_2_STR(TPM2_ALG_ECDSA),
    TPM_ALGID_2_STR(TPM2_ALG_ECDH),
    TPM_ALGID_2_STR(TPM2_ALG_ECDAA),
    TPM_ALGID_2_STR(TPM2_ALG_SM2),
    TPM_ALGID_2_STR(TPM2_ALG_ECSCHNORR),
    TPM_ALGID_2_STR(TPM2_ALG_ECMQV),
    TPM_ALGID_2_STR(TPM2_ALG_KDF1_SP800_56A),
    TPM_ALGID_2_STR(TPM2_ALG_KDF2),
    TPM_ALGID_2_STR(TPM2_ALG_KDF1_SP800_108),
    TPM_ALGID_2_STR(TPM2_ALG_ECC),
    TPM_ALGID_2_STR(TPM2_ALG_SYMCIPHER),
    TPM_ALGID_2_STR(TPM2_ALG_CAMELLIA),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA3_256,     32),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA3_384,     48),
    TPM_ALGID_2_STR_DIGEST(TPM2_ALG_SHA3_512,     64),
    TPM_ALGID_2_STR(TPM2_ALG_SHAKE128),
    TPM_ALGID_2_STR(TPM2_ALG_SHAKE256),
    TPM_ALGID_2_STR(TPM2_ALG_SHAKE256_192),
    TPM_ALGID_2_STR(TPM2_ALG_SHAKE256_256),
    TPM_ALGID_2_STR(TPM2_ALG_SHAKE256_512),
    TPM_ALGID_2_STR(TPM2_ALG_CMAC),
    TPM_ALGID_2_STR(TPM2_ALG_CTR),
    TPM_ALGID_2_STR(TPM2_ALG_OFB),
    TPM_ALGID_2_STR(TPM2_ALG_CBC),
    TPM_ALGID_2_STR(TPM2_ALG_CFB),
    TPM_ALGID_2_STR(TPM2_ALG_ECB),
    TPM_ALGID_2_STR(TPM2_ALG_CCM),
    TPM_ALGID_2_STR(TPM2_ALG_GCM),
    TPM_ALGID_2_STR(TPM2_ALG_KW),
    TPM_ALGID_2_STR(TPM2_ALG_KWP),
    TPM_ALGID_2_STR(TPM2_ALG_EAX),
    TPM_ALGID_2_STR(TPM2_ALG_EDDSA),
    TPM_ALGID_2_STR(TPM2_ALG_EDDSA_PH),
    TPM_ALGID_2_STR(TPM2_ALG_LMS),
    TPM_ALGID_2_STR(TPM2_ALG_XMSS),
    TPM_ALGID_2_STR(TPM2_ALG_KEYEDXOF),
    TPM_ALGID_2_STR(TPM2_ALG_KMACXOF128),
    TPM_ALGID_2_STR(TPM2_ALG_KMACXOF256),
    TPM_ALGID_2_STR(TPM2_ALG_KMAC128),
    TPM_ALGID_2_STR(TPM2_ALG_KMAC256),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_TERM
#undef TPM_ALGID_2_STR
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

DECLINLINE(void) vboxTraceLogDecodeEvtTpmDecodeCtxInit(PTPMDECODECTX pCtx, const uint8_t *pbBuf, size_t cbBuf)
{
    pCtx->pbBuf  = pbBuf;
    pCtx->cbLeft = cbBuf;
    pCtx->fError = false;
}


static uint8_t vboxTraceLogDecodeEvtTpmDecodeCtxGetU8(PTPMDECODECTX pCtx, PRTTRACELOGDECODERHLP pHlp, const char *pszItem)
{
    if (RT_UNLIKELY(pCtx->fError))
        return 0;

    if (RT_UNLIKELY(!pCtx->cbLeft))
    {
        pHlp->pfnErrorMsg(pHlp, "Failed to decode '%s' as there is not enough space in the buffer (required %u, available %u)",
                          pszItem, sizeof(uint8_t), pCtx->cbLeft);
        AssertFailed();
        pCtx->fError = true;
        return 0;
    }

    pCtx->cbLeft--;
    return *pCtx->pbBuf++;
}


static uint16_t vboxTraceLogDecodeEvtTpmDecodeCtxGetU16(PTPMDECODECTX pCtx, PRTTRACELOGDECODERHLP pHlp, const char *pszItem)
{
    if (RT_UNLIKELY(pCtx->fError))
        return 0;

    if (RT_UNLIKELY(pCtx->cbLeft < sizeof(uint16_t)))
    {
        pHlp->pfnErrorMsg(pHlp, "Failed to decode '%s' as there is not enough space in the buffer (required %u, available %u)",
                          pszItem, sizeof(uint16_t), pCtx->cbLeft);
        AssertFailed();
        pCtx->fError = true;
        return 0;
    }

    uint16_t u16  = *(uint16_t *)pCtx->pbBuf;
    pCtx->pbBuf  += sizeof(uint16_t);
    pCtx->cbLeft -= sizeof(uint16_t);
    return RT_BE2H_U16(u16);
}


static uint32_t vboxTraceLogDecodeEvtTpmDecodeCtxGetU32(PTPMDECODECTX pCtx, PRTTRACELOGDECODERHLP pHlp, const char *pszItem)
{
    if (RT_UNLIKELY(pCtx->fError))
        return 0;

    if (RT_UNLIKELY(pCtx->cbLeft < sizeof(uint32_t)))
    {
        pHlp->pfnErrorMsg(pHlp, "Failed to decode '%s' as there is not enough space in the buffer (required %u, available %u)",
                          pszItem, sizeof(uint32_t), pCtx->cbLeft);
        AssertFailed();
        pCtx->fError = true;
        return 0;
    }

    uint32_t u32  = *(uint32_t *)pCtx->pbBuf;
    pCtx->pbBuf  += sizeof(uint32_t);
    pCtx->cbLeft -= sizeof(uint32_t);
    return RT_BE2H_U32(u32);
}


static int32_t vboxTraceLogDecodeEvtTpmDecodeCtxGetI32(PTPMDECODECTX pCtx, PRTTRACELOGDECODERHLP pHlp, const char *pszItem)
{
    return (int32_t)vboxTraceLogDecodeEvtTpmDecodeCtxGetU32(pCtx, pHlp, pszItem);
}


static uint64_t vboxTraceLogDecodeEvtTpmDecodeCtxGetU64(PTPMDECODECTX pCtx, PRTTRACELOGDECODERHLP pHlp, const char *pszItem)
{
    if (RT_UNLIKELY(pCtx->fError))
        return 0;

    if (RT_UNLIKELY(pCtx->cbLeft < sizeof(uint64_t)))
    {
        pHlp->pfnErrorMsg(pHlp, "Failed to decode '%s' as there is not enough space in the buffer (required %u, available %u)",
                          pszItem, sizeof(uint64_t), pCtx->cbLeft);
        AssertFailed();
        pCtx->fError = true;
        return 0;
    }

    uint64_t u64  = *(uint64_t *)pCtx->pbBuf;
    pCtx->pbBuf  += sizeof(uint64_t);
    pCtx->cbLeft -= sizeof(uint64_t);
    return RT_BE2H_U64(u64);
}


static const uint8_t *vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(PTPMDECODECTX pCtx, PRTTRACELOGDECODERHLP pHlp, const char *pszItem, size_t cbBuf)
{
    if (RT_UNLIKELY(pCtx->fError))
        return 0;

    if (RT_UNLIKELY(pCtx->cbLeft < cbBuf))
    {
        pHlp->pfnErrorMsg(pHlp, "Failed to decode '%s' as there is not enough space in the buffer (required %u, available %u)",
                          pszItem, cbBuf, pCtx->cbLeft);
        AssertFailed();
        pCtx->fError = true;
        return 0;
    }

    /* Just return nothing if nothing is requested. */
    if (!cbBuf)
        return NULL;

    const uint8_t *pb = pCtx->pbBuf;
    pCtx->pbBuf  += cbBuf;
    pCtx->cbLeft -= cbBuf;
    return pb;
}


static size_t vboxTraceLogDecodeEvtTpmAlgId2DigestSize(uint16_t u16AlgId)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aAlgId2Str); i++)
        if (g_aAlgId2Str[i].u64EnumVal == u16AlgId)
            return g_aAlgId2Str[i].uPtrUser;

    return 0;
}


#define TPM_DECODE_INIT() do {

#define TPM_DECODE_END()  \
    } while (0); \
    if (pCtx->fError) return

#define TPM_DECODE_BOOL(a_Var, a_Name) \
    uint8_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU8(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddBool(pHlp, #a_Name, 0 /*fFlags*/, RT_BOOL(a_Var))

#define TPM_DECODE_U8(a_Var, a_Name) \
    uint8_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU8(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU8(pHlp, #a_Name, 0 /*fFlags*/, a_Var)

#define TPM_DECODE_U16(a_Var, a_Name) \
    uint16_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU16(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU16(pHlp, #a_Name, 0 /*fFlags*/, a_Var)

#define TPM_DECODE_U32(a_Var, a_Name) \
    uint32_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU32(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU32(pHlp, #a_Name, 0 /*fFlags*/, a_Var)

#define TPM_DECODE_U64(a_Var, a_Name) \
    uint64_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU64(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU64(pHlp, #a_Name, 0 /*fFlags*/, a_Var)

#define TPM_DECODE_I32(a_Var, a_Name) \
    int32_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetI32(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddS32(pHlp, #a_Name, 0 /*fFlags*/, a_Var)

#define TPM_DECODE_U8_HEX(a_Var, a_Name) \
    uint8_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU8(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU8(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, a_Var)

#define TPM_DECODE_U16_HEX(a_Var, a_Name) \
    uint16_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU16(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU16(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, a_Var)

#define TPM_DECODE_U32_HEX(a_Var, a_Name) \
    uint32_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU32(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU32(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, a_Var)

#define TPM_DECODE_U32_HEX_STR(a_Var, a_pszName) \
    uint32_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU32(pCtx, pHlp, a_pszName); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU32(pHlp, a_pszName, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, a_Var)

#define TPM_DECODE_U64_HEX(a_Var, a_Name) \
    uint64_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU64(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddU64(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, a_Var)

#define TPM_DECODE_I32_HEX(a_Var, a_Name) \
    int32_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetI32(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddS32(pHlp, #a_Name, 0 /*fFlags*/, a_Var)

#define TPM_DECODE_BUF_EX(a_Var, a_Name, a_cb) \
    const uint8_t *a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(pCtx, pHlp, #a_Name, a_cb); \
    if (pCtx->fError) break

#define TPM_DECODE_BUF(a_Var, a_Name, a_cb) \
    const uint8_t *a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(pCtx, pHlp, #a_Name, a_cb); \
    if (pCtx->fError) break; \
    if (a_Var) \
        pHlp->pfnStructBldAddBuf(pHlp, #a_Name, 0 /*fFlags*/, a_Var, a_cb);

#define TPM_DECODE_BUF_HEX_STRING(a_Var, a_Name, a_cb) \
    const uint8_t *a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(pCtx, pHlp, #a_Name, a_cb); \
    if (pCtx->fError) break; \
    if (a_Var) \
        pHlp->pfnStructBldAddBuf(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX_DUMP_STR, a_Var, a_cb);

#define TPM_DECODE_U16_ENUM(a_Var, a_Name, a_aEnums) \
    uint16_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU16(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddEnum(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, 16, a_aEnums, a_Var)

#define TPM_DECODE_U32_ENUM(a_Var, a_Name, a_aEnums) \
    uint32_t a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetU32(pCtx, pHlp, #a_Name); \
    if (pCtx->fError) break; \
    pHlp->pfnStructBldAddEnum(pHlp, #a_Name, RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, 32, a_aEnums, a_Var)

#define TPM_DECODE_BUF_STR(a_Var, a_pszName, a_cb) \
    const uint8_t *a_Var = vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(pCtx, pHlp, a_pszName, a_cb); \
    if (pCtx->fError) break; \
    if (a_Var) \
        pHlp->pfnStructBldAddBuf(pHlp, a_pszName, 0 /*fFlags*/, a_Var, a_cb);

#define TPM_DECODE_END_IF_ERROR() \
    if (pCtx->fError) return

static void vboxTraceLogDecodePcrSelection(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U16_ENUM(u16AlgId,       u16AlgId,        g_aAlgId2Str  );
        TPM_DECODE_U8(      cbPcrSelection, u8SizeOfSelect                 );
        TPM_DECODE_BUF_EX(  pb,             PCRs,            cbPcrSelection);

        pHlp->pfnStructBldArrayBegin(pHlp, "PCRs");

        for (uint8_t idxPcr = 0; idxPcr < cbPcrSelection * 8; idxPcr++)
            if (RT_BOOL(*(pb + (idxPcr / 8)) & RT_BIT(idxPcr % 8)))
                pHlp->pfnStructBldAddU8(pHlp, NULL, 0 /*fFlags*/, idxPcr);

        pHlp->pfnStructBldArrayEnd(pHlp);

    TPM_DECODE_END();
}


static void vboxTraceLogDecodePcrSelectionList(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U32(u32Count, u32Count);

        pHlp->pfnStructBldBegin(pHlp, "aPcrSelection");

        /* Walk the list of PCR selection entries. */
        for (uint32_t i = 0; i < u32Count; i++)
        {
            vboxTraceLogDecodePcrSelection(pHlp, pCtx);
            TPM_DECODE_END_IF_ERROR();
        }

        pHlp->pfnStructBldEnd(pHlp);

    TPM_DECODE_END();
}


static void vboxTraceLogDecodeEvtTpmDigestList(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U32(u32DigestCount, u32DigestCount);

        pHlp->pfnStructBldBegin(pHlp, "aDigests");

        for (uint32_t i = 0; i < u32DigestCount; i++)
        {
            TPM_DECODE_U16(u16DigestSize, u16DigestSize);
            TPM_DECODE_BUF_HEX_STRING(pbDigest, abDigest, u16DigestSize);
        }

        pHlp->pfnStructBldEnd(pHlp);

    TPM_DECODE_END();
}


static void vboxTraceLogDecodeEvtTpmDigestValuesList(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U32(u32DigestCount, u32DigestCount);

        pHlp->pfnStructBldBegin(pHlp, "aDigests");

        for (uint32_t i = 0; i < u32DigestCount; i++)
        {
            TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);

            size_t cbDigest = vboxTraceLogDecodeEvtTpmAlgId2DigestSize(u16HashAlg);
            TPM_DECODE_BUF_HEX_STRING(pb, abDigest, cbDigest);
        }

        pHlp->pfnStructBldEnd(pHlp);

    TPM_DECODE_END();
}


static void vboxTraceLogDecodeEvtTpmAuthSessionReq(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U32_HEX(u32SessionHandle, u32SessionHandle);
        TPM_DECODE_U16(    u16NonceSize,     u16NonceSize);
        TPM_DECODE_BUF(    pbNonce,          abNonce, u16NonceSize);
        TPM_DECODE_U8(     u8Attr,           u8Attr);
        TPM_DECODE_U16(    u16HmacSize,      u16HmacSize);
        TPM_DECODE_BUF(    pbHmac,           abHmac,  u16HmacSize);
    TPM_DECODE_END();
}


static void vboxTraceLogDecodeEvtTpmAuthSessionResp(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx)
{
    pHlp->pfnStructBldBegin(pHlp, "AuthArea");
    TPM_DECODE_INIT();
        TPM_DECODE_U16(u16NonceSize,     u16NonceSize);
        TPM_DECODE_BUF(pbNonce,          abNonce, u16NonceSize);
        TPM_DECODE_U8( u8Attr,           u8Attr);
        TPM_DECODE_U16(u16AckSize,       u16AckSize);
        TPM_DECODE_BUF(pbAck,            abAck,  u16AckSize);
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecodeSizedBufU16(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    if (pszName)
        pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U16(u16Size, u16Size);
        if (u16Size)
        {
            TPM_DECODE_BUF_STR(pb, pszName, u16Size);
        }
    TPM_DECODE_END();
    if (pszName)
        pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecodeSymEncDef(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
        if (u16HashAlg != TPM2_ALG_NULL)
        {
            TPM_DECODE_U16(u16KeyBits, u16KeyBits);
            TPM_DECODE_U16_ENUM(u16SymMode, u16SymMode, g_aAlgId2Str);
            /* Symmetric details are not required as of now. */
        }
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecodeNvPublic(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U16(u16Size, u16Size);
        if (u16Size)
        {
            TPM_DECODE_U32_HEX(hNvIndex,    hNvIndex);
            TPM_DECODE_U16(u16HashAlgName,  u16HashAlgName);
            TPM_DECODE_U32(u32Attr,         fAttr);
            vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "AuthPolicy");
            TPM_DECODE_U16(u16DataSize, u16DataSize);
        }
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecodeTicket(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U16_HEX(u16Tag,      u16Tag);
        TPM_DECODE_U32_HEX(hHierarchy,  hHierarchy);
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Digest");
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPMT_SIGNATURE(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U16_ENUM(u16SigningAlg, u16SigningAlg, g_aAlgId2Str);
        if (u16SigningAlg != TPM2_ALG_NULL)
        {
            TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
        }
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPMT_SIG_SCHEME(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U16_ENUM(u16Scheme, u16Scheme, g_aAlgId2Str);
        if (u16Scheme != TPM2_ALG_NULL)
        {
            TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
        }
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPML_DIGEST(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U32(u32Count, u32Count);
        for (uint32_t i = 0; i < u32Count; i++)
        {
            vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Digest");
            TPM_DECODE_END_IF_ERROR();
        }
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPMS_CONTEXT(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        TPM_DECODE_U64(    u64Sequence, u64Sequence);
        TPM_DECODE_U32_HEX(hSaved,      hSaved);
        TPM_DECODE_U32_HEX(hHierarchy,  hHierarchy);
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "ContextBlob");
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPM2B_SENSITIVE_CREATE(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        /** @todo */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, NULL /*pszName*/);
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPM2B_TEMPLATE(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        /** @todo */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, NULL /*pszName*/);
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPM2B_PRIVATE(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        /** @todo */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, NULL /*pszName*/);
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPM2B_PUBLIC(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        /** @todo */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, NULL /*pszName*/);
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static void vboxTraceLogDecode_TPM2B_NAME(PRTTRACELOGDECODERHLP pHlp, PTPMDECODECTX pCtx, const char *pszName)
{
    pHlp->pfnStructBldBegin(pHlp, pszName);
    TPM_DECODE_INIT();
        /** @todo */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, NULL /*pszName*/);
    TPM_DECODE_END();
    pHlp->pfnStructBldEnd(pHlp);
}


static const char *g_apszHandlesEvictControlReq[] =
{
    "hAuth",
    "hObj",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeEvictControlReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U32_HEX(hPersistent, hPersistent);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeSelfTestReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_BOOL(fFullTest, fFullTest);
    TPM_DECODE_END();
}


static RTTRACELOGDECODERSTRUCTBLDENUM g_aStartupShutdownParam[] =
{
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_SU_CLEAR),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_SU_STATE)
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeStartupShutdownReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U16_ENUM(u16TpmSu, u16State, g_aStartupShutdownParam);
    TPM_DECODE_END();
}


static RTTRACELOGDECODERSTRUCTBLDENUM g_aTpm2Caps[] =
{
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_ALGS),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_HANDLES),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_COMMANDS),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_PP_COMMANDS),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_AUDIT_COMMANDS),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_PCRS),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_TPM_PROPERTIES),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_PCR_PROPERTIES),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_ECC_CURVES),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_AUTH_POLICIES),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_INIT(TPM2_CAP_ACT),
    RT_TRACELOG_DECODER_STRUCT_BLD_ENUM_TERM
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeGetCapabilityReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U32_ENUM(u32Cap,      u32Cap,      g_aTpm2Caps);
        TPM_DECODE_U32_HEX( u32Property, u32Property             );
        TPM_DECODE_U32(     u32Count,    u32Count                );

        pThis->u.GetCapability.u32Cap      = u32Cap;
        pThis->u.GetCapability.u32Property = u32Property;
        pThis->u.GetCapability.u32Count    = u32Count;
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeGetCapabilityResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_BOOL(    fMoreData,   fMoreData);
        TPM_DECODE_U32_ENUM(u32Cap,      u32Cap,      g_aTpm2Caps);

        switch (u32Cap)
        {
            case TPM2_CAP_PCRS:
            {
                vboxTraceLogDecodePcrSelectionList(pHlp, pCtx);
                break;
            }
            case TPM2_CAP_TPM_PROPERTIES:
            {
                TPM_DECODE_U32(u32Count, u32Count);
                pHlp->pfnStructBldBegin(pHlp, "aProperties");
                for (uint32_t i = 0; i < u32Count; i++)
                {
                    TPM_DECODE_U32_HEX(u32Property, u32Property);
                    TPM_DECODE_U32_HEX(u32Value,    u32Value);
                }
                pHlp->pfnStructBldEnd(pHlp);
                break;
            }
            case TPM2_CAP_COMMANDS:
            {
                TPM_DECODE_U32(u32Count, u32Count);
                pHlp->pfnStructBldBegin(pHlp, "aCommands");
                for (uint32_t i = 0; i < u32Count; i++)
                {
                    TPM_DECODE_U32_HEX(u32CmdCode, u32CmdCode);
                }
                pHlp->pfnStructBldEnd(pHlp);
                break;
            }
            case TPM2_CAP_HANDLES:
            {
                TPM_DECODE_U32(u32Count, u32Count);
                pHlp->pfnStructBldBegin(pHlp, "aCommands");
                for (uint32_t i = 0; i < u32Count; i++)
                {
                    TPM_DECODE_U32_HEX(u32Handle, u32Handle);
                }
                pHlp->pfnStructBldEnd(pHlp);
                break;
            }
            default:
                break;
        }
    TPM_DECODE_END();
}


static const char *g_apszHandlesReadPublicReq[] =
{
    "hObj",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeReadPublicResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutPublic");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Name");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "QualifiedName");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeGetRandomReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U16(u16RandomBytes, u16RandomBytes);
        pThis->u.GetRandom.cbRnd = u16RandomBytes;
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeGetRandomResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    TPM_DECODE_INIT();
        TPM_DECODE_U16(cbBuf, u16Size);
        if (pThis->u.GetRandom.cbRnd != cbBuf)
        {
            pHlp->pfnErrorMsg(pHlp, "Requested random data size doesn't match returned data size (requested %u, returned %u), using smaller value\n",
                              pThis->u.GetRandom.cbRnd, cbBuf);
            cbBuf = RT_MIN(cbBuf, pThis->u.GetRandom.cbRnd);
        }

        TPM_DECODE_BUF(pb, RndBuf, cbBuf);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePcrReadReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodePcrSelectionList(pHlp, pCtx);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePcrReadResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U32(u32PcrUpdateCounter, u32PcrUpdateCounter);
        vboxTraceLogDecodePcrSelectionList(pHlp, pCtx);
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeEvtTpmDigestList(pHlp, pCtx);
    TPM_DECODE_END();
}


static const char *g_apszHandlesPcrExtendReq[] =
{
    "hPcr",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePcrExtendReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeEvtTpmDigestValuesList(pHlp, pCtx);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeReadClockResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U64(u64Time, u64Time);

        /* Decode TPMS_CLOCK_INFO. */
        TPM_DECODE_U64( u64Clock,        u64Clock);
        TPM_DECODE_U32( u32ResetCount,   u32ResetCount);
        TPM_DECODE_U32( u32RestartCount, u32RestartCount);
        TPM_DECODE_BOOL(fSafe,           fSafe);
    TPM_DECODE_END();
}


static const char *g_apszHandlesContextLoadResp[] =
{
    "hLoaded",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeContextLoadReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecode_TPMS_CONTEXT(pHlp, pCtx, "Context");
    TPM_DECODE_END();
}


static const char *g_apszHandlesContextSaveReq[] =
{
    "hSave",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeContextSaveResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecode_TPMS_CONTEXT(pHlp, pCtx, "Context");
    TPM_DECODE_END();
}


static const char *g_apszHandlesNvReadPublicReq[] =
{
    "hNvIndex",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeNvReadPublicResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "NvPublic");
    TPM_DECODE_END();
}


static const char *g_apszHandlesStartAuthSessionReq[] =
{
    "hTpmKey",
    "hBind",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeStartAuthSessionReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "NonceCaller");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "EncryptedSalt");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U8_HEX(u8SessionType, u8SessionType);

        vboxTraceLogDecodeSymEncDef(pHlp, pCtx, "Symmetric");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
    TPM_DECODE_END();
}


static const char *g_apszHandlesStartAuthSessionResp[] =
{
    "hSession",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeStartAuthSessionResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "NonceTpm");
    TPM_DECODE_END();
}


static const char *g_apszHandlesClearReq[] =
{
    "hAuth",
    NULL
};


static const char *g_apszHandlesClearControlReq[] =
{
    "hAuth",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeClearControlReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_BOOL(fDisable, fDisable);
    TPM_DECODE_END();
}


static const char *g_apszHandlesClockSetReq[] =
{
    "hAuth",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeClockSetReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U64(u64NewTime, u64NewTime);
    TPM_DECODE_END();
}


static const char *g_apszHandlesHierarchyChangeAuthReq[] =
{
    "hAuth",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeHierarchyChangeAuthReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "NewAuth");
    TPM_DECODE_END();
}


static const char *g_apszHandlesHierarchyControlReq[] =
{
    "hAuth",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeHierarchyControlReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U32_HEX(hEnable, hEnable);
        TPM_DECODE_BOOL(   fState,  fState);
    TPM_DECODE_END();
}


static const char *g_apszHandlesNvUndefineSpaceReq[] =
{
    "hAuth",
    "hNvIndex",
    NULL
};


static const char *g_apszHandlesChangeEpsPpsReq[] =
{
    "hAuth",
    NULL
};


static const char *g_apszHandlesNvDefineSpaceReq[] =
{
    "hAuth",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeNvDefineSpaceReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Auth");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeNvPublic(pHlp, pCtx, "PublicInfo");
    TPM_DECODE_END();
}


static const char *g_apszHandlesSetPrimaryPolicyReq[] =
{
    "hAuth",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeSetPrimaryPolicyReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "AuthPolicy");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
    TPM_DECODE_END();
}


static const char *g_apszHandlesCreatePrimaryReq[] =
{
    "hPrimary",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCreatePrimaryReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        /** @todo Decode InSensitive. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InSensitive");
        TPM_DECODE_END_IF_ERROR();
        /** @todo Decode InPublic. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InPublic");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutsideInfo");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodePcrSelectionList(pHlp, pCtx);

    TPM_DECODE_END();
}


static const char *g_apszHandlesCreatePrimaryResp[] =
{
    "hObj",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCreatePrimaryResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        /** @todo Decode OutPublic. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutPublic");
        TPM_DECODE_END_IF_ERROR();
        /** @todo Decode CreationData. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CreationData");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CreationHash");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeTicket(pHlp, pCtx, "Ticket");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Name");
    TPM_DECODE_END();
}


static const char *g_apszHandlesNvIncrementReq[] =
{
    "hAuth",
    "hNvIndex",
    NULL
};


static const char *g_apszHandlesNvSetBitsReq[] =
{
    "hAuth",
    "hNvIndex",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeNvSetBitsReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U64_HEX(u64BitsOr, u64BitsOr);
    TPM_DECODE_END();
}


static const char *g_apszHandlesNvWriteReq[] =
{
    "hAuth",
    "hNvIndex",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeNvWriteReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Data");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U16(u16Offset, u16Offset);
    TPM_DECODE_END();
}


static const char *g_apszHandlesNvWriteLockReq[] =
{
    "hAuth",
    "hNvIndex",
    NULL
};


static const char *g_apszHandlesDictionaryAttackParametersReq[] =
{
    "hLock",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeDictionaryAttackParametersReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U32(u32NewMaxTries,      u32NewMaxTries);
        TPM_DECODE_U32(u32NewRecoveryTime,  u32NewRecoveryTime);
        TPM_DECODE_U32(u32LockoutRecovery,  u32LockoutRecovery);
    TPM_DECODE_END();
}


static const char *g_apszHandlesCertifyReq[] =
{
    "hSign",
    "hObj",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCertifyReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "QualifyingData");
        TPM_DECODE_END_IF_ERROR();

        /* Decode TPMT_SIG_SCHEME+ */
        TPM_DECODE_U16_ENUM(u16SigningScheme, u16SigningScheme, g_aAlgId2Str);
        if (u16SigningScheme != TPM2_ALG_NULL)
        {
            TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
        }
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCertifyResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CertifyInfo");
        vboxTraceLogDecode_TPMT_SIGNATURE(pHlp, pCtx, "Signature");
    TPM_DECODE_END();
}


static const char *g_apszHandlesCertifyCreationReq[] =
{
    "hSign",
    "hObj",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCertifyCreationReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "QualifyingData");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CreationHash");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U16_ENUM(u16SigningScheme, u16SigningScheme, g_aAlgId2Str);
        vboxTraceLogDecodeTicket(pHlp, pCtx, "CreationTicket");
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCertifyCreationResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CertifyInfo");
        vboxTraceLogDecode_TPMT_SIGNATURE(pHlp, pCtx, "Signature");
    TPM_DECODE_END();
}


static const char *g_apszHandlesNvReadReq[] =
{
    "hAuth",
    "hNvIndex",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeNvReadReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U16(u16Size,   u16Size);
        TPM_DECODE_U16(u16Offset, u16Offset);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeNvReadResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Data");
    TPM_DECODE_END();
}


static const char *g_apszHandlesPolicySecretReq[] =
{
    "hAuth",
    "hPolicySession",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePolicySecretReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "NonceTpm");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CpHashA");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "PolicyRef");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_I32(i32Expiration,   i32Expiration);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePolicySecretResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Timeout");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeTicket(pHlp, pCtx, "Ticket");
    TPM_DECODE_END();
}


static const char *g_apszHandlesCreateReq[] =
{
    "hParent",
    NULL
};

static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCreateReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        /** @todo Decode InSensitive. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InSensitive");
        TPM_DECODE_END_IF_ERROR();
        /** @todo Decode InPublic. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InPublic");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutsideInfo");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodePcrSelectionList(pHlp, pCtx);

    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCreateResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutPrivate");
        TPM_DECODE_END_IF_ERROR();
        /** @todo Decode OutPublic. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutPublic");
        TPM_DECODE_END_IF_ERROR();
        /** @todo Decode CreationData. */
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CreationData");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "CreationHash");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecodeTicket(pHlp, pCtx, "Ticket");
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeEccParametersReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U16(u16CurveId, u16CurveId);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeEccParametersResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U16_HEX( u16CurveId,       u16CurveId);
        TPM_DECODE_U16(     u16KeySize,       u16KeySize);
        TPM_DECODE_U16_ENUM(u16KdfScheme,     u16KdfScheme,     g_aAlgId2Str);
        TPM_DECODE_U16_ENUM(u16KdfSchemeHash, u16KdfSchemeHash, g_aAlgId2Str);
        TPM_DECODE_U16_ENUM(u16EccScheme,     u16EccScheme,     g_aAlgId2Str);
        if (u16EccScheme != TPM2_ALG_NULL)
        {
            TPM_DECODE_U16_ENUM(u16EccSchemeHash, u16EccSchemeHash, g_aAlgId2Str);
        }
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "p");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "a");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "b");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "gX");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "gY");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "n");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "h");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static const char *g_apszHandlesQuoteReq[] =
{
    "hSign",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeQuoteReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "QualifyingData");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U16_ENUM(u16SigScheme, u16SigScheme, g_aAlgId2Str);
        if (u16SigScheme != TPM2_ALG_NULL)
        {
            TPM_DECODE_U16_ENUM(u16SigSchemeHash, u16SigSchemeHash, g_aAlgId2Str);
        }

        vboxTraceLogDecodePcrSelectionList(pHlp, pCtx);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeQuoteResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Quoted");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecode_TPMT_SIGNATURE(pHlp, pCtx, "Signature");
    TPM_DECODE_END();
}


static const char *g_apszHandlesSignReq[] =
{
    "hKey",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeSignReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Digest");
        TPM_DECODE_END_IF_ERROR();

        vboxTraceLogDecode_TPMT_SIG_SCHEME(pHlp, pCtx, "InScheme");
        vboxTraceLogDecodeTicket(pHlp, pCtx,    "Validation"); /* TPMT_TK_HASHCHECK */
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeSignResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecode_TPMT_SIGNATURE(pHlp, pCtx, "Signature");
    TPM_DECODE_END();
}


static const char *g_apszHandlesHmacMacReq[] =
{
    "hSymKey",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeHmacMacReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Buffer");
        TPM_DECODE_END_IF_ERROR();

        TPM_DECODE_U16_ENUM(u16HashAlg, u16HashAlg, g_aAlgId2Str);
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeHmacMacResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutHmacMac");
    TPM_DECODE_END();
}


static const char *g_apszHandlesFlushContextReq[] =
{
    "hFlush",
    NULL
};


static const char *g_apszHandlesImportReq[] =
{
    "hParent",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeImportReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "EncryptionKey");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "ObjectPublic");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Duplicate");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InSymSeed");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSymEncDef(pHlp, pCtx, "SymmetricAlg");
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeImportResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "OutPrivate");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static const char *g_apszHandlesLoadReq[] =
{
    "hParent",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeLoadReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InPrivate");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "InPublic");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static const char *g_apszHandlesLoadResp[] =
{
    "hObj",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeLoadResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "Name");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static const char *g_apszHandlesPolicyGetDigestReq[] =
{
    "hPolicySession",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePolicyGetDigestResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecodeSizedBufU16(pHlp, pCtx, "PolicyDigest");
    TPM_DECODE_END();
}


static const char *g_apszHandlesPolicyAuthValueReq[] =
{
    "hPolicySession",
    NULL
};


static const char *g_apszHandlesPolicyCommandCodeReq[] =
{
    "hPolicySession",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePolicyCommandCodeReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        TPM_DECODE_U32_HEX(u32CmdCode, u32CmdCode);
    TPM_DECODE_END();
}


static const char *g_apszHandlesPolicyOrReq[] =
{
    "hPolicySession",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodePolicyOrReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecode_TPML_DIGEST(pHlp, pCtx, "HashList");
    TPM_DECODE_END();
}


static const char *g_apszHandlesCreateLoadedReq[] =
{
    "hParent",
    NULL
};


static const char *g_apszHandlesCreateLoadedResp[] =
{
    "hObj",
    NULL
};


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCreateLoadedReq(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecode_TPM2B_SENSITIVE_CREATE(pHlp, pCtx, "InSensitive");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecode_TPM2B_TEMPLATE(pHlp, pCtx, "InPublic");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static DECLCALLBACK(void) vboxTraceLogDecodeEvtTpmDecodeCreateLoadedResp(PRTTRACELOGDECODERHLP pHlp, PTPMSTATE pThis, PTPMDECODECTX pCtx)
{
    RT_NOREF(pThis);

    TPM_DECODE_INIT();
        vboxTraceLogDecode_TPM2B_PRIVATE(pHlp, pCtx, "OutPrivate");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecode_TPM2B_PUBLIC(pHlp, pCtx, "OutPublic");
        TPM_DECODE_END_IF_ERROR();
        vboxTraceLogDecode_TPM2B_NAME(pHlp, pCtx, "Name");
        TPM_DECODE_END_IF_ERROR();
    TPM_DECODE_END();
}


static struct
{
    uint32_t              u32CmdCode;
    const char            *pszCmdCode;
    const char            **papszHandlesReq;
    const char            **papszHandlesResp;
    PFNFNDECODETPM2CCREQ  pfnDecodeReq;
    PFNFNDECODETPM2CCRESP pfnDecodeResp;
} s_aTpmCmdCodes[] =
{
#define TPM_CMD_CODE_INIT_NOT_IMPL(a_CmdCode) { a_CmdCode, #a_CmdCode, NULL, NULL, NULL, NULL }
#define TPM_CMD_CODE_INIT(a_CmdCode, a_apszHandlesReq, a_apszHandlesResp, a_pfnReq, a_pfnResp) { a_CmdCode, #a_CmdCode, \
                                                                                                 a_apszHandlesReq, a_apszHandlesResp, \
                                                                                                 a_pfnReq, a_pfnResp }
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_UNDEFINE_SPACE_SPECIAL),
    TPM_CMD_CODE_INIT(         TPM2_CC_EVICT_CONTROL,                     g_apszHandlesEvictControlReq,                 NULL,                               vboxTraceLogDecodeEvtTpmDecodeEvictControlReq,                  NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_HIERARCHY_CONTROL,                 g_apszHandlesHierarchyControlReq,             NULL,                               vboxTraceLogDecodeEvtTpmDecodeHierarchyControlReq,              NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_UNDEFINE_SPACE,                 g_apszHandlesNvUndefineSpaceReq,              NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CHANGE_EPS,                        g_apszHandlesChangeEpsPpsReq,                 NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CHANGE_PPS,                        g_apszHandlesChangeEpsPpsReq,                 NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CLEAR,                             g_apszHandlesClearReq,                        NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CLEAR_CONTROL,                     g_apszHandlesClearControlReq,                 NULL,                               vboxTraceLogDecodeEvtTpmDecodeClearControlReq,                  NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CLOCK_SET,                         g_apszHandlesClockSetReq,                     NULL,                               vboxTraceLogDecodeEvtTpmDecodeClockSetReq,                      NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_HIERARCHY_CHANGE_AUTH,             g_apszHandlesHierarchyChangeAuthReq,          NULL,                               vboxTraceLogDecodeEvtTpmDecodeHierarchyChangeAuthReq,           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_DEFINE_SPACE,                   g_apszHandlesNvDefineSpaceReq,                NULL,                               vboxTraceLogDecodeEvtTpmDecodeNvDefineSpaceReq,                 NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_PCR_ALLOCATE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_PCR_SET_AUTH_POLICY),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_PP_COMMANDS),
    TPM_CMD_CODE_INIT(         TPM2_CC_SET_PRIMARY_POLICY,                g_apszHandlesSetPrimaryPolicyReq,             NULL,                               vboxTraceLogDecodeEvtTpmDecodeSetPrimaryPolicyReq,              NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_FIELD_UPGRADE_START),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_CLOCK_RATE_ADJUST),
    TPM_CMD_CODE_INIT(         TPM2_CC_CREATE_PRIMARY,                    g_apszHandlesCreatePrimaryReq,                g_apszHandlesCreatePrimaryResp,     vboxTraceLogDecodeEvtTpmDecodeCreatePrimaryReq,                 vboxTraceLogDecodeEvtTpmDecodeCreatePrimaryResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_GLOBAL_WRITE_LOCK),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_GET_COMMAND_AUDIT_DIGEST),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_INCREMENT,                      g_apszHandlesNvIncrementReq,                  NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_SET_BITS,                       g_apszHandlesNvSetBitsReq,                    NULL,                               vboxTraceLogDecodeEvtTpmDecodeNvSetBitsReq,                     NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_EXTEND),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_WRITE,                          g_apszHandlesNvWriteReq,                      NULL,                               vboxTraceLogDecodeEvtTpmDecodeNvWriteReq,                       NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_WRITE_LOCK,                     g_apszHandlesNvWriteLockReq,                  NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_DICTIONARY_ATTACK_LOCK_RESET),
    TPM_CMD_CODE_INIT(         TPM2_CC_DICTIONARY_ATTACK_PARAMETERS,      g_apszHandlesDictionaryAttackParametersReq,   NULL,                               vboxTraceLogDecodeEvtTpmDecodeDictionaryAttackParametersReq,    NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_CHANGE_AUTH),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_PCR_EVENT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_PCR_RESET),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_SEQUENCE_COMPLETE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_SET_ALGORITHM_SET),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_SET_COMMAND_CODE_AUDIT_STATUS),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_FIELD_UPGRADE_DATA),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_INCREMENTAL_SELF_TEST),
    TPM_CMD_CODE_INIT(         TPM2_CC_SELF_TEST,                        NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeSelfTestReq,                      NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_STARTUP,                          NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeStartupShutdownReq,               NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_SHUTDOWN,                         NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeStartupShutdownReq,               NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_STIR_RANDOM),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ACTIVATE_CREDENTIAL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CERTIFY,                          g_apszHandlesCertifyReq,                       NULL,                               vboxTraceLogDecodeEvtTpmDecodeCertifyReq,                       vboxTraceLogDecodeEvtTpmDecodeCertifyResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_NV),
    TPM_CMD_CODE_INIT(         TPM2_CC_CERTIFY_CREATION,                 g_apszHandlesCertifyCreationReq,               NULL,                               vboxTraceLogDecodeEvtTpmDecodeCertifyCreationReq,               vboxTraceLogDecodeEvtTpmDecodeCertifyCreationResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_DUPLICATE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_GET_TIME),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_GET_SESSION_AUDIT_DIGEST),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_READ,                          g_apszHandlesNvReadReq,                        NULL,                               vboxTraceLogDecodeEvtTpmDecodeNvReadReq,                        vboxTraceLogDecodeEvtTpmDecodeNvReadResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_READ_LOCK),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_OBJECT_CHANGE_AUTH),
    TPM_CMD_CODE_INIT(         TPM2_CC_POLICY_SECRET,                    g_apszHandlesPolicySecretReq,                  NULL,                               vboxTraceLogDecodeEvtTpmDecodePolicySecretReq,                  vboxTraceLogDecodeEvtTpmDecodePolicySecretResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_REWRAP),
    TPM_CMD_CODE_INIT(         TPM2_CC_CREATE,                           g_apszHandlesCreateReq,                        NULL,                               vboxTraceLogDecodeEvtTpmDecodeCreateReq,                        vboxTraceLogDecodeEvtTpmDecodeCreateResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ECDH_ZGEN),
    TPM_CMD_CODE_INIT(         TPM2_CC_HMAC_MAC,                         g_apszHandlesHmacMacReq,                       NULL,                               vboxTraceLogDecodeEvtTpmDecodeHmacMacReq,                       vboxTraceLogDecodeEvtTpmDecodeHmacMacResp),
    TPM_CMD_CODE_INIT(         TPM2_CC_IMPORT,                           g_apszHandlesImportReq,                        NULL,                               vboxTraceLogDecodeEvtTpmDecodeImportReq,                        vboxTraceLogDecodeEvtTpmDecodeImportResp),
    TPM_CMD_CODE_INIT(         TPM2_CC_LOAD,                             g_apszHandlesLoadReq,                          g_apszHandlesLoadResp,              vboxTraceLogDecodeEvtTpmDecodeLoadReq,                          vboxTraceLogDecodeEvtTpmDecodeLoadResp),
    TPM_CMD_CODE_INIT(         TPM2_CC_QUOTE,                            g_apszHandlesQuoteReq,                         NULL,                               vboxTraceLogDecodeEvtTpmDecodeQuoteReq,                         vboxTraceLogDecodeEvtTpmDecodeQuoteResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_RSA_DECRYPT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_HMAC_MAC_START),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_SEQUENCE_UPDATE),
    TPM_CMD_CODE_INIT(         TPM2_CC_SIGN,                             g_apszHandlesSignReq,                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeSignReq,                          vboxTraceLogDecodeEvtTpmDecodeSignResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_UNSEAL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_SIGNED),
    TPM_CMD_CODE_INIT(         TPM2_CC_CONTEXT_LOAD,                     NULL,                                          g_apszHandlesContextLoadResp,       vboxTraceLogDecodeEvtTpmDecodeContextLoadReq,                   NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_CONTEXT_SAVE,                     g_apszHandlesContextSaveReq,                   NULL,                               NULL,                                                           vboxTraceLogDecodeEvtTpmDecodeContextSaveResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ECDH_KEY_GEN),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ENCRYPT_DECRYPT),
    TPM_CMD_CODE_INIT(         TPM2_CC_FLUSH_CONTEXT,                    g_apszHandlesFlushContextReq,                  NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_LOAD_EXTERNAL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_MAKE_CREDENTIAL),
    TPM_CMD_CODE_INIT(         TPM2_CC_NV_READ_PUBLIC,                   g_apszHandlesNvReadPublicReq,                  NULL,                               NULL,                                                           vboxTraceLogDecodeEvtTpmDecodeNvReadPublicResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_AUTHORIZE),
    TPM_CMD_CODE_INIT(         TPM2_CC_POLICY_AUTH_VALUE,                g_apszHandlesPolicyAuthValueReq,               NULL,                               NULL,                                                           NULL),
    TPM_CMD_CODE_INIT(         TPM2_CC_POLICY_COMMAND_CODE,              g_apszHandlesPolicyCommandCodeReq,             NULL,                               vboxTraceLogDecodeEvtTpmDecodePolicyCommandCodeReq,             NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_COUNTER_TIMER),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_CP_HASH),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_LOCALITY),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_NAME_HASH),
    TPM_CMD_CODE_INIT(         TPM2_CC_POLICY_OR,                        g_apszHandlesPolicyOrReq,                      NULL,                               vboxTraceLogDecodeEvtTpmDecodePolicyOrReq,                      NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_TICKET),
    TPM_CMD_CODE_INIT(         TPM2_CC_READ_PUBLIC,                      g_apszHandlesReadPublicReq,                    NULL,                               NULL,                                                           vboxTraceLogDecodeEvtTpmDecodeReadPublicResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_RSA_ENCRYPT),
    TPM_CMD_CODE_INIT(         TPM2_CC_START_AUTH_SESSION,               g_apszHandlesStartAuthSessionReq,              g_apszHandlesStartAuthSessionResp,  vboxTraceLogDecodeEvtTpmDecodeStartAuthSessionReq,              vboxTraceLogDecodeEvtTpmDecodeStartAuthSessionResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_VERIFY_SIGNATURE),
    TPM_CMD_CODE_INIT(         TPM2_CC_ECC_PARAMETERS,                   NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeEccParametersReq,                 vboxTraceLogDecodeEvtTpmDecodeEccParametersResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_FIRMWARE_READ),
    TPM_CMD_CODE_INIT(         TPM2_CC_GET_CAPABILITY,                   NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeGetCapabilityReq,                 vboxTraceLogDecodeEvtTpmDecodeGetCapabilityResp),
    TPM_CMD_CODE_INIT(         TPM2_CC_GET_RANDOM,                       NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodeGetRandomReq,                     vboxTraceLogDecodeEvtTpmDecodeGetRandomResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_GET_TEST_RESULT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_GET_HASH),
    TPM_CMD_CODE_INIT(         TPM2_CC_PCR_READ,                         NULL,                                          NULL,                               vboxTraceLogDecodeEvtTpmDecodePcrReadReq,                       vboxTraceLogDecodeEvtTpmDecodePcrReadResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_PCR),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_RESTART),
    TPM_CMD_CODE_INIT(         TPM2_CC_READ_CLOCK,                       NULL,                                          NULL,                               NULL,                                                           vboxTraceLogDecodeEvtTpmDecodeReadClockResp),
    TPM_CMD_CODE_INIT(         TPM2_CC_PCR_EXTEND,                       g_apszHandlesPcrExtendReq,                     NULL,                               vboxTraceLogDecodeEvtTpmDecodePcrExtendReq,                     NULL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_PCR_SET_AUTH_VALUE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_CERTIFY),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_EVENT_SEQUENCE_COMPLETE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_HASH_SEQUENCE_START),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_PHYSICAL_PRESENCE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_DUPLICATION_SELECT),
    TPM_CMD_CODE_INIT(         TPM2_CC_POLICY_GET_DIGEST,                g_apszHandlesPolicyGetDigestReq,                NULL,                               NULL,                                                          vboxTraceLogDecodeEvtTpmDecodePolicyGetDigestResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_TEST_PARMS),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_COMMIT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_PASSWORD),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ZGEN_2PHASE),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_EC_EPHEMERAL),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_NV_WRITTEN),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_TEMPLATE),
    TPM_CMD_CODE_INIT(         TPM2_CC_CREATE_LOADED,                    g_apszHandlesCreateLoadedReq,                   g_apszHandlesCreateLoadedResp,      vboxTraceLogDecodeEvtTpmDecodeCreateLoadedReq,                 vboxTraceLogDecodeEvtTpmDecodeCreateLoadedResp),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_AUTHORIZE_NV),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ENCRYPT_DECRYPT_2),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_AC_GET_CAPABILITY),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_AC_SEND),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_AC_SEND_SELECT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_CERTIFY_X509),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ACT_SET_TIMEOUT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ECC_ENCRYPT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_ECC_DECRYPT),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_CAPABILITY),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_POLICY_PARAMETERS),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_DEFINE_SPACE_2),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_NV_READ_PUBLIC_2),
    TPM_CMD_CODE_INIT_NOT_IMPL(TPM2_CC_SET_CAPABILITY)
#undef TPM_CMD_CODE_INIT
};

static void vboxTraceLogDecodeEvtTpmDecodeCmdBuffer(PRTTRACELOGDECODERHLP pHlp, const uint8_t *pbCmd, size_t cbCmd)
{
    PCTPMREQHDR pHdr = (PCTPMREQHDR)pbCmd;
    if (cbCmd >= sizeof(*pHdr))
    {
        uint16_t  u16Tag       = RT_BE2H_U16(pHdr->u16Tag);
        uint32_t  u32CmdCode   = RT_BE2H_U32(pHdr->u32Ordinal);
        uint32_t  cbReqPayload = RT_BE2H_U32(pHdr->cbReq) - sizeof(*pHdr);
        PTPMSTATE pTpmState    = (PTPMSTATE)pHlp->pfnDecoderStateGet(pHlp);

        if (!pTpmState)
        {
            int rc = pHlp->pfnDecoderStateCreate(pHlp, sizeof(*pTpmState), NULL, (void **)&pTpmState);
            if (RT_SUCCESS(rc))
                pTpmState->u32CmdCode = u32CmdCode;
            else
                pHlp->pfnErrorMsg(pHlp, "Failed to allocate TPM decoder state: %Rrc\n", rc);
        }
        else
            pTpmState->u32CmdCode = u32CmdCode;

        for (uint32_t i = 0; i < RT_ELEMENTS(s_aTpmCmdCodes); i++)
        {
            if (s_aTpmCmdCodes[i].u32CmdCode == u32CmdCode)
            {
                pHlp->pfnStructBldBegin(pHlp, "Command");
                pHlp->pfnStructBldAddStr(pHlp, "CmdCode",     0 /*fFlags*/, s_aTpmCmdCodes[i].pszCmdCode);
                pHlp->pfnStructBldAddU32(pHlp, "PayloadSize", 0 /*fFlags*/, cbReqPayload);
                pHlp->pfnStructBldAddU16(pHlp, "u16Tag",      RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, u16Tag);

                TPMDECODECTX Ctx;
                PTPMDECODECTX pCtx = &Ctx;
                vboxTraceLogDecodeEvtTpmDecodeCtxInit(&Ctx, (const uint8_t *)(pHdr + 1), cbReqPayload);

                /* Decode the handle area. */
                if (s_aTpmCmdCodes[i].papszHandlesReq)
                {
                    pHlp->pfnStructBldBegin(pHlp, "Handles");

                    const char **papszHnd = s_aTpmCmdCodes[i].papszHandlesReq;
                    while (*papszHnd)
                    {
                        TPM_DECODE_U32_HEX_STR(u32Hnd, *papszHnd);
                        papszHnd++;
                    }

                    pHlp->pfnStructBldEnd(pHlp);
                }

                /* Decode authorization area if available. */
                if (u16Tag == TPM2_ST_SESSIONS)
                {
                    size_t cbAuthArea = 0;
                    TPM_DECODE_INIT();
                        TPM_DECODE_U32(u32AuthSize, u32AuthSize);
                        cbAuthArea = u32AuthSize;
                    TPM_DECODE_END();

                    if (cbAuthArea)
                    {
                        const uint8_t *pb = vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(pCtx, pHlp, "AuthArea", cbAuthArea);
                        if (!pCtx->fError)
                        {
                            TPMDECODECTX CtxAuth;
                            vboxTraceLogDecodeEvtTpmDecodeCtxInit(&CtxAuth, pb, cbAuthArea);

                            pHlp->pfnStructBldBegin(pHlp, "AuthArea");
                            while (   CtxAuth.cbLeft
                                   && !CtxAuth.fError)
                                vboxTraceLogDecodeEvtTpmAuthSessionReq(pHlp, &CtxAuth);
                            pHlp->pfnStructBldEnd(pHlp);
                        }
                    }
                }

                /* Decode parameters. */
                if (s_aTpmCmdCodes[i].pfnDecodeReq)
                {
                    pHlp->pfnStructBldBegin(pHlp, "Parameters");
                    s_aTpmCmdCodes[i].pfnDecodeReq(pHlp, pTpmState, &Ctx);
                    pHlp->pfnStructBldEnd(pHlp);
                }

                if (pCtx->cbLeft)
                    pHlp->pfnStructBldAddBuf(pHlp, "Leftover undecoded data", 0 /*fFlags*/, pCtx->pbBuf, pCtx->cbLeft);

                pHlp->pfnStructBldEnd(pHlp);
                return;
            }
        }
        pHlp->pfnPrintf(pHlp, "    <Unknown command code>: %#x\n", u32CmdCode);

        if (cbReqPayload)
            pHlp->pfnPrintf(pHlp, "%.*Rhxd\n", cbReqPayload, pHdr + 1);
    }
    else
        pHlp->pfnErrorMsg(pHlp, "Command buffer is smaller than the request header (required %u, given %zu\n", sizeof(*pHdr), cbCmd);
}


static void vboxTraceLogDecodeEvtTpmDecodeRespBuffer(PRTTRACELOGDECODERHLP pHlp, const uint8_t *pbResp, size_t cbResp)
{
    RT_NOREF(pHlp);

    PCTPMRESPHDR pHdr = (PCTPMRESPHDR)pbResp;
    if (cbResp >= sizeof(*pHdr))
    {
        uint32_t cbRespPayload = RT_BE2H_U32(pHdr->cbResp) - sizeof(*pHdr);
        uint32_t u32ErrCode    = RT_BE2H_U32(pHdr->u32ErrCode);
        uint16_t u16Tag        = RT_BE2H_U16(pHdr->u16Tag);

        pHlp->pfnStructBldBegin(pHlp, "Response");

        pHlp->pfnStructBldAddU16(pHlp, "u16Tag", RTTRACELOG_DECODER_HLP_STRUCT_BLD_F_HEX, u16Tag);
        pHlp->pfnStructBldAddU32(pHlp, "u32ErrCode", 0 /*fFlags*/, u32ErrCode);
        pHlp->pfnStructBldAddU32(pHlp, "cbResponse", 0 /*fFlags*/, cbRespPayload);

        PTPMSTATE pTpmState = (PTPMSTATE)pHlp->pfnDecoderStateGet(pHlp);

        /* Can only decode the response buffer if we know the command code. */
        if (   pTpmState
            && u32ErrCode == TPM_SUCCESS)
        {
            TPMDECODECTX Ctx;
            PTPMDECODECTX pCtx = &Ctx;
            vboxTraceLogDecodeEvtTpmDecodeCtxInit(&Ctx, (const uint8_t *)(pHdr + 1), cbRespPayload);

            for (uint32_t i = 0; i < RT_ELEMENTS(s_aTpmCmdCodes); i++)
            {
                if (s_aTpmCmdCodes[i].u32CmdCode == pTpmState->u32CmdCode)
                {
                    /* Decode the handle area. */
                    uint32_t cHandles = 0;
                    if (s_aTpmCmdCodes[i].papszHandlesResp)
                    {
                        pHlp->pfnStructBldBegin(pHlp, "Handles");

                        const char **papszHnd = s_aTpmCmdCodes[i].papszHandlesResp;
                        while (*papszHnd)
                        {
                            TPM_DECODE_U32_HEX_STR(u32Hnd, *papszHnd);
                            papszHnd++;
                            cHandles++;
                        }

                        pHlp->pfnStructBldEnd(pHlp);
                    }

                    size_t cbParams = cbRespPayload - cHandles * sizeof(uint32_t);
                    if (u16Tag == TPM2_ST_SESSIONS)
                    {
                        TPM_DECODE_INIT();
                            TPM_DECODE_U32(u32ParamSz, u32ParamSize);
                            cbParams = u32ParamSz;
                        TPM_DECODE_END();
                    }

                    /* Decode parameters. */
                    if (   s_aTpmCmdCodes[i].pfnDecodeResp
                        && cbParams)
                    {
                        const uint8_t *pb = vboxTraceLogDecodeEvtTpmDecodeCtxGetBuf(pCtx, pHlp, "Parameters", cbParams);
                        if (!pCtx->fError)
                        {
                            TPMDECODECTX CtxParams;
                            vboxTraceLogDecodeEvtTpmDecodeCtxInit(&CtxParams, pb, cbParams);

                            pHlp->pfnStructBldBegin(pHlp, "Parameters");
                            s_aTpmCmdCodes[i].pfnDecodeResp(pHlp, pTpmState, &CtxParams);

                            if (CtxParams.cbLeft)
                                pHlp->pfnStructBldAddBuf(pHlp, "Leftover undecoded data", 0 /*fFlags*/, CtxParams.pbBuf, CtxParams.cbLeft);
                            pHlp->pfnStructBldEnd(pHlp);
                        }
                    }

                    /* Decode authorization area if available. */
                    if (u16Tag == TPM2_ST_SESSIONS)
                        vboxTraceLogDecodeEvtTpmAuthSessionResp(pHlp, pCtx);

                    if (pCtx->cbLeft)
                        pHlp->pfnStructBldAddBuf(pHlp, "Leftover undecoded data", 0 /*fFlags*/, pCtx->pbBuf, pCtx->cbLeft);
                    pHlp->pfnStructBldEnd(pHlp);
                    return;
                }
            }
        }

        if (cbRespPayload)
            pHlp->pfnStructBldAddBuf(pHlp, "Data", 0 /*fFlags*/, (const uint8_t *)(pHdr + 1), cbRespPayload);

        pHlp->pfnStructBldEnd(pHlp);
    }
    else
        pHlp->pfnErrorMsg(pHlp, "Response buffer is smaller than the request header (required %u, given %zu\n", sizeof(*pHdr), cbResp);
}


static DECLCALLBACK(int) vboxTraceLogDecodeEvtTpm(PRTTRACELOGDECODERHLP pHlp, uint32_t idDecodeEvt, RTTRACELOGRDREVT hTraceLogEvt,
                                                  PCRTTRACELOGEVTDESC pEvtDesc, PRTTRACELOGEVTVAL paVals, uint32_t cVals)
{
    RT_NOREF(hTraceLogEvt, pEvtDesc);
    if (idDecodeEvt == 0)
    {
        for (uint32_t i = 0; i < cVals; i++)
        {
            /* Look for the pvCmd item which stores the command buffer. */
            if (   !strcmp(paVals[i].pItemDesc->pszName, "pvCmd")
                && paVals[i].pItemDesc->enmType == RTTRACELOGTYPE_RAWDATA)
            {
                vboxTraceLogDecodeEvtTpmDecodeCmdBuffer(pHlp, paVals[i].u.RawData.pb, paVals[i].u.RawData.cb);
                return VINF_SUCCESS;
            }
        }

        pHlp->pfnErrorMsg(pHlp, "Failed to find the TPM command data buffer for the given event\n");
    }
    else if (idDecodeEvt == 1)
    {
        for (uint32_t i = 0; i < cVals; i++)
        {
            /* Look for the pvCmd item which stores the response buffer. */
            if (   !strcmp(paVals[i].pItemDesc->pszName, "pvResp")
                && paVals[i].pItemDesc->enmType == RTTRACELOGTYPE_RAWDATA)
            {
                vboxTraceLogDecodeEvtTpmDecodeRespBuffer(pHlp, paVals[i].u.RawData.pb, paVals[i].u.RawData.cb);
                return VINF_SUCCESS;
            }
        }
        pHlp->pfnErrorMsg(pHlp, "Failed to find the TPM command response buffer for the given event\n");
    }

    pHlp->pfnErrorMsg(pHlp, "Decode event ID %u is not known to this decoder\n", idDecodeEvt);
    return VERR_NOT_FOUND;
}


/**
 * TPM decoder event IDs.
 */
static const RTTRACELOGDECODEEVT s_aDecodeEvtTpm[] =
{
    { "ITpmConnector.CmdExecReq",  0          },
    { "ITpmConnector.CmdExecResp", 1          },
    { NULL,                        UINT32_MAX }
};


/**
 * Decoder plugin interface.
 */
static const RTTRACELOGDECODERREG g_TraceLogDecoderTpm =
{
    /** pszName */
    "TPM",
    /** pszDesc */
    "Decodes events from the ITpmConnector interface generated with the IfTrace driver.",
    /** paEvtIds */
    s_aDecodeEvtTpm,
    /** pfnDecode */
    vboxTraceLogDecodeEvtTpm,
};


/**
 * Shared object initialization callback.
 */
extern "C" DECLCALLBACK(DECLEXPORT(int)) RTTraceLogDecoderLoad(void *pvUser, PRTTRACELOGDECODERREGISTER pRegisterCallbacks)
{
    AssertLogRelMsgReturn(pRegisterCallbacks->u32Version == RT_TRACELOG_DECODERREG_CB_VERSION,
                          ("pRegisterCallbacks->u32Version=%#x RT_TRACELOG_DECODERREG_CB_VERSION=%#x\n",
                          pRegisterCallbacks->u32Version, RT_TRACELOG_DECODERREG_CB_VERSION),
                          VERR_VERSION_MISMATCH);

    return pRegisterCallbacks->pfnRegisterDecoders(pvUser, &g_TraceLogDecoderTpm, 1);
}

