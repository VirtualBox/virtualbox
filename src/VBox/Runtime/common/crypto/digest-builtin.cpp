/* $Id: digest-builtin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Cryptographic Hash / Message Digest API, Built-in providers.
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

#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/md2.h>
#include <iprt/md4.h>
#include <iprt/md5.h>
#include <iprt/sha.h>
#include <iprt/crypto/pkix.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include "internal/openssl-post.h"
#endif



/*
 * MD2
 */
#ifndef IPRT_WITHOUT_DIGEST_MD2

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestMd2_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTMd2Update((PRTMD2CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestMd2_Final(void *pvState, uint8_t *pbHash)
{
    RTMd2Final((PRTMD2CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestMd2_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(fReInit); RT_NOREF_PV(pvOpaque);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTMd2Init((PRTMD2CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** MD2 alias ODIs. */
static const char * const g_apszMd2Aliases[] =
{
    RTCR_PKCS1_MD2_WITH_RSA_OID,
    "1.3.14.3.2.24" /* OIW md2WithRSASignature */,
    NULL
};

/** MD2 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestMd2Desc =
{
    "md2",
    "1.2.840.113549.2.2",
    g_apszMd2Aliases,
    RTDIGESTTYPE_MD2,
    RTMD2_HASH_SIZE,
    sizeof(RTMD2CONTEXT),
    RTCRDIGESTDESC_F_DEPRECATED,
    NULL,
    NULL,
    rtCrDigestMd2_Update,
    rtCrDigestMd2_Final,
    rtCrDigestMd2_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};
#endif /* !IPRT_WITHOUT_DIGEST_MD2 */


/*
 * MD4
 */
#ifndef IPRT_WITHOUT_DIGEST_MD4

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestMd4_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTMd4Update((PRTMD4CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestMd4_Final(void *pvState, uint8_t *pbHash)
{
    RTMd4Final((PRTMD4CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestMd4_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(fReInit); RT_NOREF_PV(pvOpaque);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTMd4Init((PRTMD4CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** MD4 alias ODIs. */
static const char * const g_apszMd4Aliases[] =
{
    RTCR_PKCS1_MD4_WITH_RSA_OID,
    NULL
};

/** MD4 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestMd4Desc =
{
    "md4",
    "1.2.840.113549.2.4",
    g_apszMd4Aliases,
    RTDIGESTTYPE_MD4,
    RTMD4_HASH_SIZE,
    sizeof(RTMD4CONTEXT),
    RTCRDIGESTDESC_F_DEPRECATED | RTCRDIGESTDESC_F_COMPROMISED | RTCRDIGESTDESC_F_SERVERELY_COMPROMISED,
    NULL,
    NULL,
    rtCrDigestMd4_Update,
    rtCrDigestMd4_Final,
    rtCrDigestMd4_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};

#endif /* !IPRT_WITHOUT_DIGEST_MD4 */


/*
 * MD5
 */
#ifndef IPRT_WITHOUT_DIGEST_MD5

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestMd5_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTMd5Update((PRTMD5CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestMd5_Final(void *pvState, uint8_t *pbHash)
{
    RTMd5Final(pbHash, (PRTMD5CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestMd5_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTMd5Init((PRTMD5CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** MD5 alias ODIs. */
static const char * const g_apszMd5Aliases[] =
{
    RTCR_PKCS1_MD5_WITH_RSA_OID,
    "1.3.14.3.2.25" /* OIW md5WithRSASignature */,
    NULL
};

/** MD5 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestMd5Desc =
{
    "md5",
    "1.2.840.113549.2.5",
    g_apszMd5Aliases,
    RTDIGESTTYPE_MD5,
    RTMD5_HASH_SIZE,
    sizeof(RTMD5CONTEXT),
    RTCRDIGESTDESC_F_COMPROMISED,
    NULL,
    NULL,
    rtCrDigestMd5_Update,
    rtCrDigestMd5_Final,
    rtCrDigestMd5_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};
#endif /* !IPRT_WITHOUT_DIGEST_MD5 */


/*
 * SHA-1
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha1_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha1Update((PRTSHA1CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha1_Final(void *pvState, uint8_t *pbHash)
{
    RTSha1Final((PRTSHA1CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha1_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha1Init((PRTSHA1CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-1 alias ODIs. */
static const char * const g_apszSha1Aliases[] =
{
    RTCR_PKCS1_SHA1_WITH_RSA_OID,
    "1.3.14.3.2.29" /* OIW sha1WithRSASignature */,
    RTCR_X962_ECDSA_WITH_SHA1_OID,
    NULL
};

/** SHA-1 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha1Desc =
{
    "sha-1",
    "1.3.14.3.2.26",
    g_apszSha1Aliases,
    RTDIGESTTYPE_SHA1,
    RTSHA1_HASH_SIZE,
    sizeof(RTSHA1CONTEXT),
    RTCRDIGESTDESC_F_DEPRECATED,
    NULL,
    NULL,
    rtCrDigestSha1_Update,
    rtCrDigestSha1_Final,
    rtCrDigestSha1_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};


/*
 * SHA-256
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha256_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha256Update((PRTSHA256CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha256_Final(void *pvState, uint8_t *pbHash)
{
    RTSha256Final((PRTSHA256CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha256_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha256Init((PRTSHA256CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-256 alias ODIs. */
static const char * const g_apszSha256Aliases[] =
{
    RTCR_PKCS1_SHA256_WITH_RSA_OID,
    RTCR_X962_ECDSA_WITH_SHA256_OID,
    NULL
};

/** SHA-256 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha256Desc =
{
    "sha-256",
    "2.16.840.1.101.3.4.2.1",
    g_apszSha256Aliases,
    RTDIGESTTYPE_SHA256,
    RTSHA256_HASH_SIZE,
    sizeof(RTSHA256CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha256_Update,
    rtCrDigestSha256_Final,
    rtCrDigestSha256_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};


/*
 * SHA-512
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha512_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha512Update((PRTSHA512CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha512_Final(void *pvState, uint8_t *pbHash)
{
    RTSha512Final((PRTSHA512CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha512_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha512Init((PRTSHA512CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-512 alias ODIs. */
static const char * const g_apszSha512Aliases[] =
{
    RTCR_PKCS1_SHA512_WITH_RSA_OID,
    RTCR_X962_ECDSA_WITH_SHA512_OID,
    NULL
};

/** SHA-512 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha512Desc =
{
    "sha-512",
    "2.16.840.1.101.3.4.2.3",
    g_apszSha512Aliases,
    RTDIGESTTYPE_SHA512,
    RTSHA512_HASH_SIZE,
    sizeof(RTSHA512CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha512_Update,
    rtCrDigestSha512_Final,
    rtCrDigestSha512_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};


/*
 * SHA-224
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha224_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha224Update((PRTSHA224CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha224_Final(void *pvState, uint8_t *pbHash)
{
    RTSha224Final((PRTSHA224CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha224_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha224Init((PRTSHA224CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-224 alias ODIs. */
static const char * const g_apszSha224Aliases[] =
{
    RTCR_PKCS1_SHA224_WITH_RSA_OID,
    RTCR_X962_ECDSA_WITH_SHA224_OID,
    NULL
};

/** SHA-224 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha224Desc =
{
    "sha-224",
    "2.16.840.1.101.3.4.2.4",
    g_apszSha224Aliases,
    RTDIGESTTYPE_SHA224,
    RTSHA224_HASH_SIZE,
    sizeof(RTSHA224CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha224_Update,
    rtCrDigestSha224_Final,
    rtCrDigestSha224_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};


/*
 * SHA-384
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha384_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha384Update((PRTSHA384CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha384_Final(void *pvState, uint8_t *pbHash)
{
    RTSha384Final((PRTSHA384CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha384_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha384Init((PRTSHA384CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-384 alias ODIs. */
static const char * const g_apszSha384Aliases[] =
{
    RTCR_PKCS1_SHA384_WITH_RSA_OID,
    RTCR_X962_ECDSA_WITH_SHA384_OID,
    NULL
};

/** SHA-384 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha384Desc =
{
    "sha-384",
    "2.16.840.1.101.3.4.2.2",
    g_apszSha384Aliases,
    RTDIGESTTYPE_SHA384,
    RTSHA384_HASH_SIZE,
    sizeof(RTSHA384CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha384_Update,
    rtCrDigestSha384_Final,
    rtCrDigestSha384_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};


#ifndef IPRT_WITHOUT_SHA512T224
/*
 * SHA-512/224
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha512t224_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha512t224Update((PRTSHA512T224CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha512t224_Final(void *pvState, uint8_t *pbHash)
{
    RTSha512t224Final((PRTSHA512T224CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha512t224_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha512t224Init((PRTSHA512T224CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-512/224 alias ODIs. */
static const char * const g_apszSha512t224Aliases[] =
{
    RTCR_PKCS1_SHA512T224_WITH_RSA_OID,
    NULL
};

/** SHA-512/224 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha512t224Desc =
{
    "sha-512/224",
    "2.16.840.1.101.3.4.2.5",
    g_apszSha512t224Aliases,
    RTDIGESTTYPE_SHA512T224,
    RTSHA512T224_HASH_SIZE,
    sizeof(RTSHA512T224CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha512t224_Update,
    rtCrDigestSha512t224_Final,
    rtCrDigestSha512t224_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};
#endif /* !IPRT_WITHOUT_SHA512T224 */


#ifndef IPRT_WITHOUT_SHA512T256
/*
 * SHA-512/256
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha512t256_Update(void *pvState, const void *pvData, size_t cbData)
{
    RTSha512t256Update((PRTSHA512T256CONTEXT)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha512t256_Final(void *pvState, uint8_t *pbHash)
{
    RTSha512t256Final((PRTSHA512T256CONTEXT)pvState, pbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha512t256_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque); RT_NOREF_PV(fReInit);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    RTSha512t256Init((PRTSHA512T256CONTEXT)pvState);
    return VINF_SUCCESS;
}

/** SHA-512/256 alias ODIs. */
static const char * const g_apszSha512t256Aliases[] =
{
    RTCR_PKCS1_SHA512T256_WITH_RSA_OID,
    NULL
};

/** SHA-512/256 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha512t256Desc =
{
    "sha-512/256",
    "2.16.840.1.101.3.4.2.6",
    g_apszSha512t256Aliases,
    RTDIGESTTYPE_SHA512T256,
    RTSHA512T256_HASH_SIZE,
    sizeof(RTSHA512T256CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha512t256_Update,
    rtCrDigestSha512t256_Final,
    rtCrDigestSha512t256_Init,
    NULL,
    NULL,
    NULL,
    NULL,
};
#endif /* !IPRT_WITHOUT_SHA512T256 */

#ifndef IPRT_WITHOUT_SHA3

/*
 * SHA3-224
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha3t224_Update(void *pvState, const void *pvData, size_t cbData)
{
    int rc = RTSha3t224Update((PRTSHA3T224CONTEXT)pvState, pvData, cbData);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha3t224_Final(void *pvState, uint8_t *pbHash)
{
    int rc = RTSha3t224Final((PRTSHA3T224CONTEXT)pvState, pbHash);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha3t224_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    if (fReInit)
        RTSha3t224Cleanup((PRTSHA3T224CONTEXT)pvState);
    return RTSha3t224Init((PRTSHA3T224CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(void) rtCrDigestSha3t224_Delete(void *pvState)
{
    RTSha3t224Cleanup((PRTSHA3T224CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(int) rtCrDigestSha3t224_Clone(void *pvState, void const *pvSrcState)
{
    return RTSha3t224Clone((PRTSHA3T224CONTEXT)pvState, (PRTSHA3T224CONTEXT)pvSrcState);
}

/** SHA3-224 alias ODIs. */
static const char * const g_apszSha3t224Aliases[] =
{
    "2.16.840.1.101.3.4.3.13",
    RTCR_NIST_SHA3_224_WITH_ECDSA_OID,
    NULL
};

/** SHA3-224 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha3t224Desc =
{
    "sha3-224",
    "2.16.840.1.101.3.4.2.7",
    g_apszSha3t224Aliases,
    RTDIGESTTYPE_SHA3_224,
    RTSHA3_224_HASH_SIZE,
    sizeof(RTSHA3T224CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha3t224_Update,
    rtCrDigestSha3t224_Final,
    rtCrDigestSha3t224_Init,
    rtCrDigestSha3t224_Delete,
    rtCrDigestSha3t224_Clone,
    NULL,
    NULL,
};


/*
 * SHA3-256
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha3t256_Update(void *pvState, const void *pvData, size_t cbData)
{
    int rc = RTSha3t256Update((PRTSHA3T256CONTEXT)pvState, pvData, cbData);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha3t256_Final(void *pvState, uint8_t *pbHash)
{
    int rc = RTSha3t256Final((PRTSHA3T256CONTEXT)pvState, pbHash);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha3t256_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    if (fReInit)
        RTSha3t256Cleanup((PRTSHA3T256CONTEXT)pvState);
    return RTSha3t256Init((PRTSHA3T256CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(void) rtCrDigestSha3t256_Delete(void *pvState)
{
    RTSha3t256Cleanup((PRTSHA3T256CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(int) rtCrDigestSha3t256_Clone(void *pvState, void const *pvSrcState)
{
    return RTSha3t256Clone((PRTSHA3T256CONTEXT)pvState, (PRTSHA3T256CONTEXT)pvSrcState);
}

/** SHA3-256 alias ODIs. */
static const char * const g_apszSha3t256Aliases[] =
{
    "2.16.840.1.101.3.4.3.14",
    RTCR_NIST_SHA3_256_WITH_ECDSA_OID,
    NULL
};

/** SHA3-256 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha3t256Desc =
{
    "sha3-256",
    "2.16.840.1.101.3.4.2.8",
    g_apszSha3t256Aliases,
    RTDIGESTTYPE_SHA3_256,
    RTSHA3_256_HASH_SIZE,
    sizeof(RTSHA3T256CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha3t256_Update,
    rtCrDigestSha3t256_Final,
    rtCrDigestSha3t256_Init,
    rtCrDigestSha3t256_Delete,
    rtCrDigestSha3t256_Clone,
    NULL,
    NULL,
};


/*
 * SHA3-384
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha3t384_Update(void *pvState, const void *pvData, size_t cbData)
{
    int rc = RTSha3t384Update((PRTSHA3T384CONTEXT)pvState, pvData, cbData);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha3t384_Final(void *pvState, uint8_t *pbHash)
{
    int rc = RTSha3t384Final((PRTSHA3T384CONTEXT)pvState, pbHash);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha3t384_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    if (fReInit)
        RTSha3t384Cleanup((PRTSHA3T384CONTEXT)pvState);
    return RTSha3t384Init((PRTSHA3T384CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(void) rtCrDigestSha3t384_Delete(void *pvState)
{
    RTSha3t384Cleanup((PRTSHA3T384CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(int) rtCrDigestSha3t384_Clone(void *pvState, void const *pvSrcState)
{
    return RTSha3t384Clone((PRTSHA3T384CONTEXT)pvState, (PRTSHA3T384CONTEXT)pvSrcState);
}

/** SHA3-384 alias ODIs. */
static const char * const g_apszSha3t384Aliases[] =
{
    "2.16.840.1.101.3.4.3.15",
    RTCR_NIST_SHA3_384_WITH_ECDSA_OID,
    NULL
};

/** SHA3-384 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha3t384Desc =
{
    "sha3-384",
    "2.16.840.1.101.3.4.2.9",
    g_apszSha3t384Aliases,
    RTDIGESTTYPE_SHA3_384,
    RTSHA3_384_HASH_SIZE,
    sizeof(RTSHA3T384CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha3t384_Update,
    rtCrDigestSha3t384_Final,
    rtCrDigestSha3t384_Init,
    rtCrDigestSha3t384_Delete,
    rtCrDigestSha3t384_Clone,
    NULL,
    NULL,
};


/*
 * SHA3-512
 */

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestSha3t512_Update(void *pvState, const void *pvData, size_t cbData)
{
    int rc = RTSha3t512Update((PRTSHA3T512CONTEXT)pvState, pvData, cbData);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestSha3t512_Final(void *pvState, uint8_t *pbHash)
{
    int rc = RTSha3t512Final((PRTSHA3T512CONTEXT)pvState, pbHash);
    AssertRC(rc);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestSha3t512_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    RT_NOREF_PV(pvOpaque);
    AssertReturn(pvOpaque == NULL, VERR_INVALID_PARAMETER);
    if (fReInit)
        RTSha3t512Cleanup((PRTSHA3T512CONTEXT)pvState);
    return RTSha3t512Init((PRTSHA3T512CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(void) rtCrDigestSha3t512_Delete(void *pvState)
{
    RTSha3t512Cleanup((PRTSHA3T512CONTEXT)pvState);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnDelete} */
static DECLCALLBACK(int) rtCrDigestSha3t512_Clone(void *pvState, void const *pvSrcState)
{
    return RTSha3t512Clone((PRTSHA3T512CONTEXT)pvState, (PRTSHA3T512CONTEXT)pvSrcState);
}

/** SHA3-512 alias ODIs. */
static const char * const g_apszSha3t512Aliases[] =
{
    "2.16.840.1.101.3.4.3.16",
    RTCR_NIST_SHA3_512_WITH_ECDSA_OID,
    NULL
};

/** SHA3-512 descriptor. */
static RTCRDIGESTDESC const g_rtCrDigestSha3t512Desc =
{
    "sha3-512",
    "2.16.840.1.101.3.4.2.10",
    g_apszSha3t512Aliases,
    RTDIGESTTYPE_SHA3_512,
    RTSHA3_512_HASH_SIZE,
    sizeof(RTSHA3T512CONTEXT),
    0,
    NULL,
    NULL,
    rtCrDigestSha3t512_Update,
    rtCrDigestSha3t512_Final,
    rtCrDigestSha3t512_Init,
    rtCrDigestSha3t512_Delete,
    rtCrDigestSha3t512_Clone,
    NULL,
    NULL,
};

#endif /* !IPRT_WITHOUT_SHA3 */


/**
 * Array of built in message digest vtables.
 */
static PCRTCRDIGESTDESC const g_apDigestOps[] =
{
#ifndef IPRT_WITHOUT_DIGEST_MD2
    &g_rtCrDigestMd2Desc,
#endif
#ifndef IPRT_WITHOUT_DIGEST_MD4
    &g_rtCrDigestMd4Desc,
#endif
#ifndef IPRT_WITHOUT_DIGEST_MD5
    &g_rtCrDigestMd5Desc,
#endif
    &g_rtCrDigestSha1Desc,
    &g_rtCrDigestSha256Desc,
    &g_rtCrDigestSha512Desc,
    &g_rtCrDigestSha224Desc,
    &g_rtCrDigestSha384Desc,
#ifndef IPRT_WITHOUT_SHA512T224
    &g_rtCrDigestSha512t224Desc,
#endif
#ifndef IPRT_WITHOUT_SHA512T256
    &g_rtCrDigestSha512t256Desc,
#endif
#ifndef IPRT_WITHOUT_SHA3
    &g_rtCrDigestSha3t224Desc,
    &g_rtCrDigestSha3t256Desc,
    &g_rtCrDigestSha3t384Desc,
    &g_rtCrDigestSha3t512Desc,
#endif
};


#ifdef IPRT_WITH_OPENSSL
/*
 * OpenSSL EVP.
 */

# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
/** @impl_interface_method{RTCRDIGESTDESC::pfnNew} */
static DECLCALLBACK(void*) rtCrDigestOsslEvp_New(void)
{
    return EVP_MD_CTX_new();
}

static DECLCALLBACK(void) rtCrDigestOsslEvp_Free(void *pvState)
{
    EVP_MD_CTX_free((EVP_MD_CTX*)pvState);
}

# endif

/** @impl_interface_method{RTCRDIGESTDESC::pfnUpdate} */
static DECLCALLBACK(void) rtCrDigestOsslEvp_Update(void *pvState, const void *pvData, size_t cbData)
{
    EVP_DigestUpdate((EVP_MD_CTX *)pvState, pvData, cbData);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnFinal} */
static DECLCALLBACK(void) rtCrDigestOsslEvp_Final(void *pvState, uint8_t *pbHash)
{
    unsigned int cbHash = EVP_MAX_MD_SIZE;
    EVP_DigestFinal((EVP_MD_CTX *)pvState, (unsigned char *)pbHash, &cbHash);
}

/** @impl_interface_method{RTCRDIGESTDESC::pfnInit} */
static DECLCALLBACK(int) rtCrDigestOsslEvp_Init(void *pvState, void *pvOpaque, bool fReInit)
{
    EVP_MD_CTX   *pThis    = (EVP_MD_CTX *)pvState;
    EVP_MD const *pEvpType = (EVP_MD const *)pvOpaque;

    if (fReInit)
    {
        pEvpType = EVP_MD_CTX_md(pThis);
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
        EVP_MD_CTX_reset(pThis);
# else
        EVP_MD_CTX_cleanup(pThis);
# endif
    }

    AssertPtrReturn(pEvpType, VERR_INVALID_PARAMETER);
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
    Assert(EVP_MD_block_size(pEvpType));
# else
    Assert(pEvpType->md_size);
# endif
    if (EVP_DigestInit(pThis, pEvpType))
        return VINF_SUCCESS;
    return VERR_CR_DIGEST_OSSL_DIGEST_INIT_ERROR;
}


/** @impl_interface_method{RTCRDIGESTDESC::pfn} */
static DECLCALLBACK(void) rtCrDigestOsslEvp_Delete(void *pvState)
{
    EVP_MD_CTX *pThis = (EVP_MD_CTX *)pvState;
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
    EVP_MD_CTX_reset(pThis);
# else
    EVP_MD_CTX_cleanup(pThis);
# endif
}


/** @impl_interface_method{RTCRDIGESTDESC::pfnClone} */
static DECLCALLBACK(int) rtCrDigestOsslEvp_Clone(void *pvState, void const *pvSrcState)
{
    EVP_MD_CTX        *pThis = (EVP_MD_CTX *)pvState;
    EVP_MD_CTX const  *pSrc  = (EVP_MD_CTX const *)pvSrcState;

    if (EVP_MD_CTX_copy(pThis, pSrc))
        return VINF_SUCCESS;
    return VERR_CR_DIGEST_OSSL_DIGEST_CTX_COPY_ERROR;
}


/** @impl_interface_method{RTCRDIGESTDESC::pfnGetHashSize} */
static DECLCALLBACK(uint32_t) rtCrDigestOsslEvp_GetHashSize(void *pvState)
{
    EVP_MD_CTX *pThis = (EVP_MD_CTX *)pvState;
    return EVP_MD_size(EVP_MD_CTX_md(pThis));
}


/** @impl_interface_method{RTCRDIGESTDESC::pfnGetHashSize} */
static DECLCALLBACK(RTDIGESTTYPE) rtCrDigestOsslEvp_GetDigestType(void *pvState)
{
    RT_NOREF_PV(pvState); //EVP_MD_CTX *pThis = (EVP_MD_CTX *)pvState;
    /** @todo figure which digest algorithm it is! */
    return RTDIGESTTYPE_UNKNOWN;
}


/** Descriptor for the OpenSSL EVP base message digest provider. */
static RTCRDIGESTDESC const g_rtCrDigestOpenSslDesc =
{
    "OpenSSL EVP",
    NULL,
    NULL,
    RTDIGESTTYPE_UNKNOWN,
    EVP_MAX_MD_SIZE,
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
    0,
# else
    sizeof(EVP_MD_CTX),
# endif
    0,
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
    rtCrDigestOsslEvp_New,
    rtCrDigestOsslEvp_Free,
# else
    NULL,
    NULL,
# endif
    rtCrDigestOsslEvp_Update,
    rtCrDigestOsslEvp_Final,
    rtCrDigestOsslEvp_Init,
    rtCrDigestOsslEvp_Delete,
    rtCrDigestOsslEvp_Clone,
    rtCrDigestOsslEvp_GetHashSize,
    rtCrDigestOsslEvp_GetDigestType
};

#endif /* IPRT_WITH_OPENSSL */


RTDECL(PCRTCRDIGESTDESC) RTCrDigestFindByObjIdString(const char *pszObjId, void **ppvOpaque)
{
    if (ppvOpaque)
        *ppvOpaque = NULL;

    /*
     * Primary OIDs.
     */
    uint32_t i = RT_ELEMENTS(g_apDigestOps);
    while (i-- > 0)
        if (strcmp(g_apDigestOps[i]->pszObjId, pszObjId) == 0)
            return g_apDigestOps[i];

    /*
     * Alias OIDs.
     */
    i = RT_ELEMENTS(g_apDigestOps);
    while (i-- > 0)
    {
        const char * const *ppszAliases = g_apDigestOps[i]->papszObjIdAliases;
        if (ppszAliases)
            for (; *ppszAliases; ppszAliases++)
                if (strcmp(*ppszAliases, pszObjId) == 0)
                    return g_apDigestOps[i];
    }

#ifdef IPRT_WITH_OPENSSL
    /*
     * Try EVP and see if it knows the algorithm.
     */
    if (ppvOpaque)
    {
        rtCrOpenSslInit();
        int iAlgoNid = OBJ_txt2nid(pszObjId);
        if (iAlgoNid != NID_undef)
        {
            const char *pszAlogSn = OBJ_nid2sn(iAlgoNid);
            const EVP_MD *pEvpMdType = EVP_get_digestbyname(pszAlogSn);
            if (pEvpMdType)
            {
                /*
                 * Return the OpenSSL provider descriptor and the EVP_MD address.
                 */
# if OPENSSL_VERSION_NUMBER >= 0x10100000 && !defined(LIBRESSL_VERSION_NUMBER)
                Assert(EVP_MD_block_size(pEvpMdType));
# else
                Assert(pEvpMdType->md_size);
# endif
                *ppvOpaque = (void *)pEvpMdType;
                return &g_rtCrDigestOpenSslDesc;
            }
        }
    }
#endif
    return NULL;
}


RTDECL(PCRTCRDIGESTDESC) RTCrDigestFindByObjId(PCRTASN1OBJID pObjId, void **ppvOpaque)
{
    return RTCrDigestFindByObjIdString(pObjId->szObjId, ppvOpaque);
}


RTDECL(int) RTCrDigestCreateByObjIdString(PRTCRDIGEST phDigest, const char *pszObjId)
{
    void *pvOpaque;
    PCRTCRDIGESTDESC pDesc = RTCrDigestFindByObjIdString(pszObjId, &pvOpaque);
    if (pDesc)
        return RTCrDigestCreate(phDigest, pDesc, pvOpaque);
    return VERR_NOT_FOUND;
}


RTDECL(int) RTCrDigestCreateByObjId(PRTCRDIGEST phDigest, PCRTASN1OBJID pObjId)
{
    void *pvOpaque;
    PCRTCRDIGESTDESC pDesc = RTCrDigestFindByObjId(pObjId, &pvOpaque);
    if (pDesc)
        return RTCrDigestCreate(phDigest, pDesc, pvOpaque);
    return VERR_NOT_FOUND;
}


RTDECL(PCRTCRDIGESTDESC) RTCrDigestFindByType(RTDIGESTTYPE enmDigestType)
{
    AssertReturn(enmDigestType > RTDIGESTTYPE_INVALID && enmDigestType <= RTDIGESTTYPE_END, NULL);

    uint32_t i = RT_ELEMENTS(g_apDigestOps);
    while (i-- > 0)
        if (g_apDigestOps[i]->enmType == enmDigestType)
            return g_apDigestOps[i];
    return NULL;
}


RTDECL(int) RTCrDigestCreateByType(PRTCRDIGEST phDigest, RTDIGESTTYPE enmDigestType)
{
    PCRTCRDIGESTDESC pDesc = RTCrDigestFindByType(enmDigestType);
    if (pDesc)
        return RTCrDigestCreate(phDigest, pDesc, NULL);
    return VERR_NOT_FOUND;
}

