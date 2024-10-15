/* $Id: x509-asn1-decoder.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - X.509, Decoder for ASN.1.
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
#include <iprt/crypto/x509.h>

#include <iprt/errcore.h>
#include <iprt/string.h>

#include "x509-internal.h"


/*
 * One X.509 Extension.
 */
RTDECL(int) RTCrX509Extension_ExtnValue_DecodeAsn1(PRTASN1CURSOR pCursor, uint32_t fFlags,
                                                   PRTCRX509EXTENSION pThis, const char *pszErrorTag)
{
    RT_NOREF_PV(fFlags); RT_NOREF_PV(pszErrorTag);

    pThis->enmValue = RTCRX509EXTENSIONVALUE_UNKNOWN;

    /*
     * Decode the encapsulated extension bytes if know the format.
     */
    RTASN1CURSOR ValueCursor;
    int rc = RTAsn1CursorInitSubFromCore(pCursor, &pThis->ExtnValue.Asn1Core, &ValueCursor, "ExtnValue");
    if (RT_FAILURE(rc))
        return rc;
    pCursor = &ValueCursor;

    if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_AUTHORITY_KEY_IDENTIFIER_OID) == 0)
    {
        /* 4.2.1.1 Authority Key Identifier */
        PRTCRX509AUTHORITYKEYIDENTIFIER pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_AUTHORITY_KEY_IDENTIFIER;
            rc = RTCrX509AuthorityKeyIdentifier_DecodeAsn1(&ValueCursor, 0, pThat, "AuthorityKeyIdentifier");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_OLD_AUTHORITY_KEY_IDENTIFIER_OID) == 0)
    {
        /* Old and obsolete version of the above, still found in microsoft certificates. */
        PRTCRX509OLDAUTHORITYKEYIDENTIFIER pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_OLD_AUTHORITY_KEY_IDENTIFIER;
            rc = RTCrX509OldAuthorityKeyIdentifier_DecodeAsn1(&ValueCursor, 0, pThat, "OldAuthorityKeyIdentifier");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_SUBJECT_KEY_IDENTIFIER_OID) == 0)
    {
        /* 4.2.1.2 Subject Key Identifier */
        PRTASN1OCTETSTRING pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_OCTET_STRING;
            rc = RTAsn1CursorGetOctetString(&ValueCursor, 0, pThat, "SubjectKeyIdentifier");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_KEY_USAGE_OID) == 0)
    {
        /* 4.2.1.3 Key Usage */
        PRTASN1BITSTRING pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_BIT_STRING;
            rc = RTAsn1CursorGetBitStringEx(&ValueCursor, 0, 9, pThat, "KeyUsage");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_CERTIFICATE_POLICIES_OID) == 0)
    {
        /* 4.2.1.4 Certificate Policies */
        PRTCRX509CERTIFICATEPOLICIES pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_CERTIFICATE_POLICIES;
            rc = RTCrX509CertificatePolicies_DecodeAsn1(&ValueCursor, 0, pThat, "CertPolicies");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_POLICY_MAPPINGS_OID) == 0)
    {
        /* 4.2.1.5 Policy Mappings */
        PRTCRX509POLICYMAPPINGS pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_POLICY_MAPPINGS;
            rc = RTCrX509PolicyMappings_DecodeAsn1(&ValueCursor, 0, pThat, "PolicyMapppings");
        }
    }
    else if (   RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_SUBJECT_ALT_NAME_OID) == 0
             || RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_ISSUER_ALT_NAME_OID) == 0)
    {
        /* 4.2.1.6 Subject Alternative Name  /  4.2.1.7 Issuer Alternative Name */
        PRTCRX509GENERALNAMES pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_GENERAL_NAMES;
            rc = RTCrX509GeneralNames_DecodeAsn1(&ValueCursor, 0, pThat, "AltName");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_BASIC_CONSTRAINTS_OID) == 0)
    {
        /* 4.2.1.9 Basic Constraints */
        PRTCRX509BASICCONSTRAINTS pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_BASIC_CONSTRAINTS;
            rc = RTCrX509BasicConstraints_DecodeAsn1(&ValueCursor, 0, pThat, "BasicConstraints");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_NAME_CONSTRAINTS_OID) == 0)
    {
        /* 4.2.1.10 Name Constraints */
        PRTCRX509NAMECONSTRAINTS pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_NAME_CONSTRAINTS;
            rc = RTCrX509NameConstraints_DecodeAsn1(&ValueCursor, 0, pThat, "NameConstraints");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_POLICY_CONSTRAINTS_OID) == 0)
    {
        /* 4.2.1.11 Policy Constraints */
        PRTCRX509POLICYCONSTRAINTS pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_POLICY_CONSTRAINTS;
            rc = RTCrX509PolicyConstraints_DecodeAsn1(&ValueCursor, 0, pThat, "PolicyConstraints");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_EXT_KEY_USAGE_OID) == 0)
    {
        /* 4.2.1.12 Extended Key Usage */
        PRTASN1SEQOFOBJIDS pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->SeqCore.Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_SEQ_OF_OBJ_IDS;
            rc = RTAsn1SeqOfObjIds_DecodeAsn1(&ValueCursor, 0, pThat, "ExKeyUsage");
        }
    }
    else if (RTAsn1ObjId_CompareWithString(&pThis->ExtnId, RTCRX509_ID_CE_EXT_KEY_USAGE_OID) == 0)
    {
        /* 4.2.1.14 Inhibit anyPolicy */
        PRTASN1INTEGER pThat;
        rc = RTAsn1MemAllocZ(&pThis->ExtnValue.EncapsulatedAllocation, (void **)&pThat, sizeof(*pThat));
        if (RT_SUCCESS(rc))
        {
            pThis->ExtnValue.pEncapsulated = &pThat->Asn1Core;
            pThis->enmValue = RTCRX509EXTENSIONVALUE_INTEGER;
            rc = RTAsn1CursorGetInteger(&ValueCursor, 0, pThat, "InhibitAnyPolicy");
        }
    }
    else
        return VINF_SUCCESS;

    if (RT_SUCCESS(rc))
        rc = RTAsn1CursorCheckEnd(&ValueCursor);

    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;
    return rc;
}


/*
 * Generate the code.
 */
#include <iprt/asn1-generator-asn1-decoder.h>

