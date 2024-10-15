/* $Id: pkix-util.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Public Key Infrastructure API, Utilities.
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

#include <iprt/asn1.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/crypto/rsa.h>


RTDECL(const char *) RTCrPkixGetCiperOidFromSignatureAlgorithmOid(const char *pszSignatureOid)
{
    return RTCrX509AlgorithmIdentifier_GetEncryptionOidFromOid(pszSignatureOid, true /*fMustIncludeHash*/);
}


RTDECL(const char *) RTCrPkixGetCiperOidFromSignatureAlgorithm(PCRTASN1OBJID pAlgorithm)
{
    return RTCrX509AlgorithmIdentifier_GetEncryptionOidFromOid(pAlgorithm->szObjId, true /*fMustIncludeHash*/);
}


RTDECL(bool) RTCrPkixPubKeyCanHandleDigestType(PCRTCRX509SUBJECTPUBLICKEYINFO pPublicKeyInfo, RTDIGESTTYPE enmDigestType,
                                               PRTERRINFO pErrInfo)
{
    bool fRc = false;
    if (RTCrX509SubjectPublicKeyInfo_IsPresent(pPublicKeyInfo))
    {
        void const * const  pvKeyBits = RTASN1BITSTRING_GET_BIT0_PTR(&pPublicKeyInfo->SubjectPublicKey);
        uint32_t const      cbKeyBits = RTASN1BITSTRING_GET_BYTE_SIZE(&pPublicKeyInfo->SubjectPublicKey);
        RTASN1CURSORPRIMARY PrimaryCursor;
        union
        {
            RTCRRSAPUBLICKEY    RsaPublicKey;
        } u;

        if (RTAsn1ObjId_CompareWithString(&pPublicKeyInfo->Algorithm.Algorithm, RTCR_PKCS1_RSA_OID) == 0)
        {
            /*
             * RSA.
             */
            RTAsn1CursorInitPrimary(&PrimaryCursor, pvKeyBits, cbKeyBits, pErrInfo, &g_RTAsn1DefaultAllocator,
                                    RTASN1CURSOR_FLAGS_DER, "rsa");

            RT_ZERO(u.RsaPublicKey);
            int rc = RTCrRsaPublicKey_DecodeAsn1(&PrimaryCursor.Cursor, 0, &u.RsaPublicKey, "PublicKey");
            if (RT_SUCCESS(rc))
                fRc = RTCrRsaPublicKey_CanHandleDigestType(&u.RsaPublicKey, enmDigestType, pErrInfo);
            RTCrRsaPublicKey_Delete(&u.RsaPublicKey);
        }
        else
        {
            /** @todo ECDSA when adding signing support.  */
            RTErrInfoSetF(pErrInfo, VERR_CR_PKIX_CIPHER_ALGO_NOT_KNOWN, "%s", pPublicKeyInfo->Algorithm.Algorithm.szObjId);
            AssertMsgFailed(("unknown key algorithm: %s\n", pPublicKeyInfo->Algorithm.Algorithm.szObjId));
            fRc = true;
        }
    }
    return fRc;
}


RTDECL(bool) RTCrPkixCanCertHandleDigestType(PCRTCRX509CERTIFICATE pCertificate, RTDIGESTTYPE enmDigestType, PRTERRINFO pErrInfo)
{
    if (RTCrX509Certificate_IsPresent(pCertificate))
        return RTCrPkixPubKeyCanHandleDigestType(&pCertificate->TbsCertificate.SubjectPublicKeyInfo, enmDigestType, pErrInfo);
    return false;
}

