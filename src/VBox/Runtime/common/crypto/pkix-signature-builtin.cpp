/* $Id: pkix-signature-builtin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Public Key Signature Schemas, Built-in providers.
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
#include <iprt/crypto/pkix.h>

#include <iprt/errcore.h>
#include <iprt/string.h>

#ifdef IPRT_WITH_OPENSSL
# include "internal/iprt-openssl.h"
# include "internal/openssl-pre.h"
# include <openssl/evp.h>
# include "internal/openssl-post.h"
#endif

#include "pkix-signature-builtin.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Array of built in message digest vtables.
 */
static PCRTCRPKIXSIGNATUREDESC const g_apPkixSignatureDescriptors[] =
{
    &g_rtCrPkixSigningHashWithRsaDesc,
#ifdef IPRT_WITH_OPENSSL
    &g_rtCrPkixSigningHashWithEcdsaDesc,
#endif
};



PCRTCRPKIXSIGNATUREDESC RTCrPkixSignatureFindByObjIdString(const char *pszObjId, void **ppvOpaque)
{
    if (ppvOpaque)
        *ppvOpaque = NULL;

    /*
     * Primary OIDs.
     */
    uint32_t i = RT_ELEMENTS(g_apPkixSignatureDescriptors);
    while (i-- > 0)
        if (strcmp(g_apPkixSignatureDescriptors[i]->pszObjId, pszObjId) == 0)
            return g_apPkixSignatureDescriptors[i];

    /*
     * Alias OIDs.
     */
    i = RT_ELEMENTS(g_apPkixSignatureDescriptors);
    while (i-- > 0)
    {
        const char * const *ppszAliases = g_apPkixSignatureDescriptors[i]->papszObjIdAliases;
        if (ppszAliases)
            for (; *ppszAliases; ppszAliases++)
                if (strcmp(*ppszAliases, pszObjId) == 0)
                    return g_apPkixSignatureDescriptors[i];
    }

#if 0//def IPRT_WITH_OPENSSL
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
                Assert(pEvpMdType->md_size);
                *ppvOpaque = (void *)pEvpMdType;
                return &g_rtCrPkixSignatureOpenSslDesc;
            }
        }
    }
#endif
    return NULL;
}


PCRTCRPKIXSIGNATUREDESC RTCrPkixSignatureFindByObjId(PCRTASN1OBJID pObjId, void **ppvOpaque)
{
    return RTCrPkixSignatureFindByObjIdString(pObjId->szObjId, ppvOpaque);
}


RTDECL(int) RTCrPkixSignatureCreateByObjIdString(PRTCRPKIXSIGNATURE phSignature, const char *pszObjId,
                                                 RTCRKEY hKey, PCRTASN1DYNTYPE pParams, bool fSigning)
{
    void *pvOpaque;
    PCRTCRPKIXSIGNATUREDESC pDesc = RTCrPkixSignatureFindByObjIdString(pszObjId, &pvOpaque);
    if (pDesc)
        return RTCrPkixSignatureCreate(phSignature, pDesc, pvOpaque, fSigning, hKey, pParams);
    return VERR_NOT_FOUND;
}


RTDECL(int) RTCrPkixSignatureCreateByObjId(PRTCRPKIXSIGNATURE phSignature, PCRTASN1OBJID pObjId,
                                           RTCRKEY hKey, PCRTASN1DYNTYPE pParams, bool fSigning)
{
    void *pvOpaque;
    PCRTCRPKIXSIGNATUREDESC pDesc = RTCrPkixSignatureFindByObjId(pObjId, &pvOpaque);
    if (pDesc)
        return RTCrPkixSignatureCreate(phSignature, pDesc, pvOpaque, fSigning, hKey, pParams);
    return VERR_NOT_FOUND;
}

