/* $Id: pkcs7-template.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS \#7, Core APIs, Code Generator Template.
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

#define RTASN1TMPL_DECL         RTDECL

/*
 * One PCKS #7 IssuerAndSerialNumber.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7ISSUERANDSERIALNUMBER
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7IssuerAndSerialNumber
#define RTASN1TMPL_INT_NAME     rtCrPkcs7IssuerAndSerialNumber
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Name,               RTCRX509NAME,                   RTCrX509Name);
RTASN1TMPL_MEMBER(              SerialNumber,       RTASN1INTEGER,                  RTAsn1Integer);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One PCKS #7 Attribute.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7ATTRIBUTE
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7Attribute
#define RTASN1TMPL_INT_NAME     rtCrPkcs7Attribute
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Type,               RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER_DYN_BEGIN(Type, RTCRPKCS7ATTRIBUTETYPE, enmType, Allocation);
RTASN1TMPL_MEMBER_DYN(          uValues,    pOctetStrings,  MessageDigest,      RTASN1SETOFOCTETSTRINGS,    RTAsn1SetOfOctetStrings,    Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_OCTET_STRINGS,        RTCR_PKCS9_ID_MESSAGE_DIGEST_OID);
RTASN1TMPL_MEMBER_DYN(          uValues,    pObjIds,        ContentType,        RTASN1SETOFOBJIDS,          RTAsn1SetOfObjIds,          Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_OBJ_IDS,              RTCR_PKCS9_ID_CONTENT_TYPE_OID);
RTASN1TMPL_MEMBER_DYN(          uValues, pCounterSignatures,CounterSignatures,  RTCRPKCS7SIGNERINFOS,       RTCrPkcs7SignerInfos,       Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_COUNTER_SIGNATURES,   RTCR_PKCS9_ID_COUNTER_SIGNATURE_OID);
RTASN1TMPL_MEMBER_DYN(          uValues,    pSigningTime,   SigningTime,        RTASN1SETOFTIMES,           RTAsn1SetOfTimes,           Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_SIGNING_TIME,         RTCR_PKCS9_ID_SIGNING_TIME_OID);
RTASN1TMPL_MEMBER_DYN(          uValues,    pContentInfos,  MsTimestamp,        RTCRPKCS7SETOFCONTENTINFOS, RTCrPkcs7SetOfContentInfos, Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_MS_TIMESTAMP,         RTCR_PKCS9_ID_MS_TIMESTAMP);
RTASN1TMPL_MEMBER_DYN(          uValues,    pContentInfos,  MsNestedSignature,  RTCRPKCS7SETOFCONTENTINFOS, RTCrPkcs7SetOfContentInfos, Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_MS_NESTED_SIGNATURE,  RTCR_PKCS9_ID_MS_NESTED_SIGNATURE);
RTASN1TMPL_MEMBER_DYN(          uValues,    pObjIdSeqs,     MsStatementType,    RTASN1SETOFOBJIDSEQS,       RTAsn1SetOfObjIdSeqs,       Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_MS_STATEMENT_TYPE,    RTCR_PKCS9_ID_MS_STATEMENT_TYPE);
RTASN1TMPL_MEMBER_DYN(          uValues,    pOctetStrings,  AppleMultiCdPlist,  RTASN1SETOFOCTETSTRINGS,    RTAsn1SetOfOctetStrings,    Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_APPLE_MULTI_CD_PLIST, RTCR_PKCS9_ID_APPLE_MULTI_CD_PLIST);
RTASN1TMPL_MEMBER_DYN_DEFAULT(  uValues,    pCores,                             RTASN1SETOFCORES,           RTAsn1SetOfCores,           Allocation,
    Type, enmType, RTCRPKCS7ATTRIBUTETYPE_UNKNOWN);
RTASN1TMPL_MEMBER_DYN_END(Type, RTCRPKCS7ATTRIBUTETYPE, enmType, Allocation);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of PCKS #7 Attributes.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7ATTRIBUTES
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7Attributes
#define RTASN1TMPL_INT_NAME     rtCrPkcs7Attributes
RTASN1TMPL_SET_OF(RTCRPKCS7ATTRIBUTE, RTCrPkcs7Attribute);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One PCKS #7 SignerInfo.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7SIGNERINFO
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7SignerInfo
#define RTASN1TMPL_INT_NAME     rtCrPkcs7SignerInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Version,                    RTASN1INTEGER,                  RTAsn1Integer);
RTASN1TMPL_MEMBER(              IssuerAndSerialNumber,      RTCRPKCS7ISSUERANDSERIALNUMBER, RTCrPkcs7IssuerAndSerialNumber);
RTASN1TMPL_MEMBER(              DigestAlgorithm,            RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER_OPT_ITAG(     AuthenticatedAttributes,    RTCRPKCS7ATTRIBUTES,            RTCrPkcs7Attributes,     0);
RTASN1TMPL_MEMBER(              DigestEncryptionAlgorithm,  RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              EncryptedDigest,            RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_MEMBER_OPT_ITAG(     UnauthenticatedAttributes,  RTCRPKCS7ATTRIBUTES,            RTCrPkcs7Attributes,    1);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of PCKS #7 SignerInfos.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7SIGNERINFOS
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7SignerInfos
#define RTASN1TMPL_INT_NAME     rtCrPkcs7SignerInfos
RTASN1TMPL_SET_OF(RTCRPKCS7SIGNERINFO, RTCrPkcs7SignerInfo);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * PCKS #7 SignedData.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7SIGNEDDATA
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7SignedData
#define RTASN1TMPL_INT_NAME     rtCrPkcs7SignedData
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Version,                    RTASN1INTEGER,                  RTAsn1Integer);
RTASN1TMPL_MEMBER(              DigestAlgorithms,           RTCRX509ALGORITHMIDENTIFIERS,   RTCrX509AlgorithmIdentifiers);
RTASN1TMPL_MEMBER(              ContentInfo,                RTCRPKCS7CONTENTINFO,           RTCrPkcs7ContentInfo);
RTASN1TMPL_MEMBER_OPT_ITAG(     Certificates,               RTCRPKCS7SETOFCERTS,            RTCrPkcs7SetOfCerts,    0);
RTASN1TMPL_MEMBER_OPT_ITAG(     Crls,                       RTASN1CORE,                     RTAsn1Core,             1);
RTASN1TMPL_MEMBER(              SignerInfos,                RTCRPKCS7SIGNERINFOS,           RTCrPkcs7SignerInfos);
RTASN1TMPL_EXEC_CHECK_SANITY(   rc = rtCrPkcs7SignedData_CheckSanityExtra(pThis, fFlags, pErrInfo, pszErrorTag) ) /* no ; */
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of PCKS #7 SignedData.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7SETOFSIGNEDDATA
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7SetOfSignedData
#define RTASN1TMPL_INT_NAME     rtCrPkcs7SetOfSignedData
RTASN1TMPL_SET_OF(RTCRPKCS7SIGNEDDATA, RTCrPkcs7SignedData);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * PCKS #7 DigestInfo.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7DIGESTINFO
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7DigestInfo
#define RTASN1TMPL_INT_NAME     rtCrPkcs7DigestInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              DigestAlgorithm,            RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              Digest,                     RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * PCKS #7 ContentInfo.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7CONTENTINFO
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7ContentInfo
#define RTASN1TMPL_INT_NAME     rtCrPkcs7ContentInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              ContentType,                RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER_OPT_ITAG(     Content,                    RTASN1OCTETSTRING,              RTAsn1OctetString, 0);
RTASN1TMPL_EXEC_DECODE(         rc = rtCrPkcs7ContentInfo_DecodeExtra(pCursor, fFlags, pThis, pszErrorTag)) /* no ; */
RTASN1TMPL_EXEC_CLONE(          rc = rtCrPkcs7ContentInfo_CloneExtra(pThis) ) /* no ; */
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of PCKS #7 ContentInfo.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7SETOFCONTENTINFOS
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7SetOfContentInfos
#define RTASN1TMPL_INT_NAME     rtCrPkcs7SetOfContentInfos
RTASN1TMPL_SET_OF(RTCRPKCS7CONTENTINFO, RTCrPkcs7ContentInfo);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One PKCS #7 ExtendedCertificateOrCertificate or a CMS CertificateChoices (sic).
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7CERT
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7Cert
#define RTASN1TMPL_INT_NAME     rtCrPkcs7Cert
RTASN1TMPL_BEGIN_PCHOICE();
RTASN1TMPL_PCHOICE_ITAG_UC(     ASN1_TAG_SEQUENCE, RTCRPKCS7CERTCHOICE_X509, u.pX509Cert, X509Cert,     RTCRX509CERTIFICATE, RTCrX509Certificate);
RTASN1TMPL_PCHOICE_ITAG(        0, RTCRPKCS7CERTCHOICE_EXTENDED_PKCS6, u.pExtendedCert,   ExtendedCert, RTASN1CORE, RTAsn1Core);
RTASN1TMPL_PCHOICE_ITAG(        1, RTCRPKCS7CERTCHOICE_AC_V1,          u.pAcV1,           AcV1,         RTASN1CORE, RTAsn1Core);
RTASN1TMPL_PCHOICE_ITAG(        2, RTCRPKCS7CERTCHOICE_AC_V2,          u.pAcV2,           AcV2,         RTASN1CORE, RTAsn1Core);
RTASN1TMPL_PCHOICE_ITAG(        3, RTCRPKCS7CERTCHOICE_OTHER,          u.pOtherCert,      OtherCert,    RTASN1CORE, RTAsn1Core);
RTASN1TMPL_END_PCHOICE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of PKCS #7 ExtendedCertificateOrCertificate or a CMS CertificateChoices.
 */
#define RTASN1TMPL_TYPE         RTCRPKCS7SETOFCERTS
#define RTASN1TMPL_EXT_NAME     RTCrPkcs7SetOfCerts
#define RTASN1TMPL_INT_NAME     rtCrPkcs7SetOfCerts
RTASN1TMPL_SET_OF(RTCRPKCS7CERT, RTCrPkcs7Cert);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

