/* $Id: x509-template.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - X.509, Code Generator Template.
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
 * X.509 Validity.
 */
#define RTASN1TMPL_TYPE         RTCRX509VALIDITY
#define RTASN1TMPL_EXT_NAME     RTCrX509Validity
#define RTASN1TMPL_INT_NAME     rtCrX509Validity
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              NotBefore,          RTASN1TIME,                     RTAsn1Time);
RTASN1TMPL_MEMBER(              NotAfter,           RTASN1TIME,                     RTAsn1Time);
RTASN1TMPL_EXEC_CHECK_SANITY(   rc = rtCrX509Validity_CheckSanityExtra(pThis, fFlags, pErrInfo, pszErrorTag) )
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 Algorithm Identifier.
 */
#define RTASN1TMPL_TYPE         RTCRX509ALGORITHMIDENTIFIER
#define RTASN1TMPL_EXT_NAME     RTCrX509AlgorithmIdentifier
#define RTASN1TMPL_INT_NAME     rtCrX509AlgorithmIdentifier
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Algorithm,          RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER_OPT_ANY(      Parameters,         RTASN1DYNTYPE,                  RTAsn1DynType);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of X.509 Algorithm Identifiers.
 */
#define RTASN1TMPL_TYPE         RTCRX509ALGORITHMIDENTIFIERS
#define RTASN1TMPL_EXT_NAME     RTCrX509AlgorithmIdentifiers
#define RTASN1TMPL_INT_NAME     rtCrX509AlgorithmIdentifiers
RTASN1TMPL_SET_OF(RTCRX509ALGORITHMIDENTIFIER, RTCrX509AlgorithmIdentifier);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 AttributeTypeAndValue.
 */
#define RTASN1TMPL_TYPE         RTCRX509ATTRIBUTETYPEANDVALUE
#define RTASN1TMPL_EXT_NAME     RTCrX509AttributeTypeAndValue
#define RTASN1TMPL_INT_NAME     rtCrX509AttributeTypeAndValue
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Type,               RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER(              Value,              RTASN1DYNTYPE,                  RTAsn1DynType);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of X.509 AttributeTypeAndValues / X.509 RelativeDistinguishedName.
 */
#define RTASN1TMPL_TYPE         RTCRX509ATTRIBUTETYPEANDVALUES
#define RTASN1TMPL_EXT_NAME     RTCrX509AttributeTypeAndValues
#define RTASN1TMPL_INT_NAME     rtCrX509AttributeTypeAndValues
RTASN1TMPL_SET_OF(RTCRX509ATTRIBUTETYPEANDVALUE, RTCrX509AttributeTypeAndValue);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

/*
 * X.509 Name.
 */
#define RTASN1TMPL_TYPE         RTCRX509NAME
#define RTASN1TMPL_EXT_NAME     RTCrX509Name
#define RTASN1TMPL_INT_NAME     rtCrX509Name
#undef  RTASN1TMPL_SET_SEQ_EXEC_CHECK_SANITY
#define RTASN1TMPL_SET_SEQ_EXEC_CHECK_SANITY() rc = rtCrX509Name_CheckSanityExtra(pThis, fFlags, pErrInfo, pszErrorTag)
RTASN1TMPL_SEQ_OF(RTCRX509RELATIVEDISTINGUISHEDNAME, RTCrX509RelativeDistinguishedName);
#undef  RTASN1TMPL_SET_SEQ_EXEC_CHECK_SANITY
#define RTASN1TMPL_SET_SEQ_EXEC_CHECK_SANITY() do { } while (0)
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

/*
 * One X.509 OtherName.
 * Note! This is simplified and might not work correctly for all types with
 *       non-DER compatible encodings.
 */
#define RTASN1TMPL_TYPE         RTCRX509OTHERNAME
#define RTASN1TMPL_EXT_NAME     RTCrX509OtherName
#define RTASN1TMPL_INT_NAME     rtCrX509OtherName
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              TypeId,             RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER(              Value,              RTASN1DYNTYPE,                  RTAsn1DynType);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 GeneralName.
 * Note! This is simplified and might not work correctly for all types with
 *       non-DER compatible encodings.
 */
#define RTASN1TMPL_TYPE         RTCRX509GENERALNAME
#define RTASN1TMPL_EXT_NAME     RTCrX509GeneralName
#define RTASN1TMPL_INT_NAME     rtCrX509GeneralName
RTASN1TMPL_BEGIN_PCHOICE();
RTASN1TMPL_PCHOICE_ITAG(        0, RTCRX509GENERALNAMECHOICE_OTHER_NAME,     u.pT0_OtherName, OtherName,    RTCRX509OTHERNAME, RTCrX509OtherName);
RTASN1TMPL_PCHOICE_ITAG_CP(     1, RTCRX509GENERALNAMECHOICE_RFC822_NAME,    u.pT1_Rfc822,   Rfc822,        RTASN1STRING,  RTAsn1Ia5String);
RTASN1TMPL_PCHOICE_ITAG_CP(     2, RTCRX509GENERALNAMECHOICE_DNS_NAME,       u.pT2_DnsName,  DnsType,       RTASN1STRING,  RTAsn1Ia5String);
RTASN1TMPL_PCHOICE_XTAG(        3, RTCRX509GENERALNAMECHOICE_X400_ADDRESS,   u.pT3, CtxTag3, X400Address,   RTASN1DYNTYPE, RTAsn1DynType); /** @todo */
RTASN1TMPL_PCHOICE_XTAG(        4, RTCRX509GENERALNAMECHOICE_DIRECTORY_NAME, u.pT4, CtxTag4, DirectoryName, RTCRX509NAME,  RTCrX509Name);
RTASN1TMPL_PCHOICE_XTAG(        5, RTCRX509GENERALNAMECHOICE_EDI_PARTY_NAME, u.pT5, CtxTag5, EdiPartyName,  RTASN1DYNTYPE, RTAsn1DynType); /** @todo */
RTASN1TMPL_PCHOICE_ITAG_CP(     6, RTCRX509GENERALNAMECHOICE_URI,            u.pT6_Uri,      Uri,           RTASN1STRING,  RTAsn1Ia5String);
RTASN1TMPL_PCHOICE_ITAG_CP(     7, RTCRX509GENERALNAMECHOICE_IP_ADDRESS,     u.pT7_IpAddress, IpAddress,    RTASN1OCTETSTRING, RTAsn1OctetString); /** @todo Constraints */
RTASN1TMPL_PCHOICE_ITAG_CP(     8, RTCRX509GENERALNAMECHOICE_REGISTERED_ID,  u.pT8_RegisteredId,RegisteredId,RTASN1OBJID,  RTAsn1ObjId);
RTASN1TMPL_END_PCHOICE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Sequence of X.509 GeneralNames.
 */
#define RTASN1TMPL_TYPE         RTCRX509GENERALNAMES
#define RTASN1TMPL_EXT_NAME     RTCrX509GeneralNames
#define RTASN1TMPL_INT_NAME     rtCrX509GeneralNames
RTASN1TMPL_SEQ_OF(RTCRX509GENERALNAME, RTCrX509GeneralName);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 UniqueIdentifier - RTASN1BITSTRING alias.
 */


/*
 * X.509 SubjectPublicKeyInfo.
 */
#define RTASN1TMPL_TYPE         RTCRX509SUBJECTPUBLICKEYINFO
#define RTASN1TMPL_EXT_NAME     RTCrX509SubjectPublicKeyInfo
#define RTASN1TMPL_INT_NAME     rtCrX509SubjectPublicKeyInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Algorithm,          RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              SubjectPublicKey,   RTASN1BITSTRING,                RTAsn1BitString);
RTASN1TMPL_EXEC_CHECK_SANITY(   rc = rtCrX509SubjectPublicKeyInfo_CheckSanityExtra(pThis, fFlags, pErrInfo, pszErrorTag) )
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 AuthorityKeyIdentifier (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509AUTHORITYKEYIDENTIFIER
#define RTASN1TMPL_EXT_NAME     RTCrX509AuthorityKeyIdentifier
#define RTASN1TMPL_INT_NAME     rtCrX509AuthorityKeyIdentifier
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_OPT_ITAG_CP(  KeyIdentifier,                      RTASN1OCTETSTRING,      RTAsn1OctetString,      0);
RTASN1TMPL_MEMBER_OPT_ITAG(     AuthorityCertIssuer,                RTCRX509GENERALNAMES,   RTCrX509GeneralNames,   1);
RTASN1TMPL_MEMBER_OPT_ITAG_CP(  AuthorityCertSerialNumber,          RTASN1INTEGER,          RTAsn1Integer,          2);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 OldAuthorityKeyIdentifier (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509OLDAUTHORITYKEYIDENTIFIER
#define RTASN1TMPL_EXT_NAME     RTCrX509OldAuthorityKeyIdentifier
#define RTASN1TMPL_INT_NAME     rtCrX509OldAuthorityKeyIdentifier
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_OPT_ITAG_CP(  KeyIdentifier,                      RTASN1OCTETSTRING,      RTAsn1OctetString,      0);
RTASN1TMPL_MEMBER_OPT_XTAG(     T1, CtxTag1, AuthorityCertIssuer,   RTCRX509NAME,           RTCrX509Name,           1);
RTASN1TMPL_MEMBER_OPT_ITAG_CP(  AuthorityCertSerialNumber,          RTASN1INTEGER,          RTAsn1Integer,          2);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 PolicyQualifierInfo.
 */
#define RTASN1TMPL_TYPE         RTCRX509POLICYQUALIFIERINFO
#define RTASN1TMPL_EXT_NAME     RTCrX509PolicyQualifierInfo
#define RTASN1TMPL_INT_NAME     rtCrX509PolicyQualifierInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              PolicyQualifierId,                  RTASN1OBJID,            RTAsn1ObjId);
RTASN1TMPL_MEMBER(              Qualifier,                          RTASN1DYNTYPE,          RTAsn1DynType);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Sequence of X.509 PolicyQualifierInfo.
 */
#define RTASN1TMPL_TYPE         RTCRX509POLICYQUALIFIERINFOS
#define RTASN1TMPL_EXT_NAME     RTCrX509PolicyQualifierInfos
#define RTASN1TMPL_INT_NAME     rtCrX509PolicyQualifierInfos
RTASN1TMPL_SEQ_OF(RTCRX509POLICYQUALIFIERINFO, RTCrX509PolicyQualifierInfo);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 PolicyInformation.
 */
#define RTASN1TMPL_TYPE         RTCRX509POLICYINFORMATION
#define RTASN1TMPL_EXT_NAME     RTCrX509PolicyInformation
#define RTASN1TMPL_INT_NAME     rtCrX509PolicyInformation
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              PolicyIdentifier, RTASN1OBJID,                  RTAsn1ObjId);
RTASN1TMPL_MEMBER_OPT_ITAG_UC(  PolicyQualifiers, RTCRX509POLICYQUALIFIERINFOS, RTCrX509PolicyQualifierInfos, ASN1_TAG_SEQUENCE);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Sequence of X.509 CertificatePolicies.
 */
#define RTASN1TMPL_TYPE         RTCRX509CERTIFICATEPOLICIES
#define RTASN1TMPL_EXT_NAME     RTCrX509CertificatePolicies
#define RTASN1TMPL_INT_NAME     rtCrX509CertificatePolicies
RTASN1TMPL_SEQ_OF(RTCRX509POLICYINFORMATION, RTCrX509PolicyInformation);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 PolicyMapping (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509POLICYMAPPING
#define RTASN1TMPL_EXT_NAME     RTCrX509PolicyMapping
#define RTASN1TMPL_INT_NAME     rtCrX509PolicyMapping
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              IssuerDomainPolicy,                 RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER(              SubjectDomainPolicy,                RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Sequence of X.509 PolicyMappings (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509POLICYMAPPINGS
#define RTASN1TMPL_EXT_NAME     RTCrX509PolicyMappings
#define RTASN1TMPL_INT_NAME     rtCrX509PolicyMappings
RTASN1TMPL_SEQ_OF(RTCRX509POLICYMAPPING, RTCrX509PolicyMapping);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 BasicConstraints (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509BASICCONSTRAINTS
#define RTASN1TMPL_EXT_NAME     RTCrX509BasicConstraints
#define RTASN1TMPL_INT_NAME     rtCrX509BasicConstraints
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_DEF_ITAG_UP(  CA,                                 RTASN1BOOLEAN,      RTAsn1Boolean,    ASN1_TAG_BOOLEAN, false);
RTASN1TMPL_MEMBER_OPT_ITAG_UP(  PathLenConstraint,                  RTASN1INTEGER,      RTAsn1Integer,    ASN1_TAG_INTEGER);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 GeneralSubtree (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509GENERALSUBTREE
#define RTASN1TMPL_EXT_NAME     RTCrX509GeneralSubtree
#define RTASN1TMPL_INT_NAME     rtCrX509GeneralSubtree
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Base,                               RTCRX509GENERALNAME,            RTCrX509GeneralName);
RTASN1TMPL_MEMBER_DEF_ITAG_UP(  Minimum,                            RTASN1INTEGER,                  RTAsn1Integer,    ASN1_TAG_INTEGER, 0);
RTASN1TMPL_MEMBER_OPT_ITAG_UP(  Maximum,                            RTASN1INTEGER,                  RTAsn1Integer,    ASN1_TAG_INTEGER);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME



/*
 * Sequence of X.509 GeneralSubtrees (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509GENERALSUBTREES
#define RTASN1TMPL_EXT_NAME     RTCrX509GeneralSubtrees
#define RTASN1TMPL_INT_NAME     rtCrX509GeneralSubtrees
RTASN1TMPL_SEQ_OF(RTCRX509GENERALSUBTREE, RTCrX509GeneralSubtree);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 NameConstraints (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509NAMECONSTRAINTS
#define RTASN1TMPL_EXT_NAME     RTCrX509NameConstraints
#define RTASN1TMPL_INT_NAME     rtCrX509NameConstraints
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_OPT_XTAG(     T0, CtxTag0, PermittedSubtrees,     RTCRX509GENERALSUBTREES,        RTCrX509GeneralSubtrees, 0);
RTASN1TMPL_MEMBER_OPT_XTAG(     T1, CtxTag1, ExcludedSubtrees,      RTCRX509GENERALSUBTREES,        RTCrX509GeneralSubtrees, 1);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 PolicyConstraints (IPRT representation).
 */
#define RTASN1TMPL_TYPE         RTCRX509POLICYCONSTRAINTS
#define RTASN1TMPL_EXT_NAME     RTCrX509PolicyConstraints
#define RTASN1TMPL_INT_NAME     rtCrX509PolicyConstraints
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_OPT_ITAG_CP(  RequireExplicitPolicy,              RTASN1INTEGER,                  RTAsn1Integer, 0);
RTASN1TMPL_MEMBER_OPT_ITAG_CP(  InhibitPolicyMapping,               RTASN1INTEGER,                  RTAsn1Integer, 1);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 Extension.
 */
#define RTASN1TMPL_TYPE         RTCRX509EXTENSION
#define RTASN1TMPL_EXT_NAME     RTCrX509Extension
#define RTASN1TMPL_INT_NAME     rtCrX509Extension
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              ExtnId,                             RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER_DEF_ITAG_UP(  Critical,                           RTASN1BOOLEAN,                  RTAsn1Boolean, ASN1_TAG_BOOLEAN, false);
RTASN1TMPL_MEMBER(              ExtnValue,                          RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_EXEC_DECODE(rc = RTCrX509Extension_ExtnValue_DecodeAsn1(pCursor, fFlags, pThis, "ExtnValue"))
RTASN1TMPL_EXEC_CLONE( rc = rtCrX509Extension_ExtnValue_Clone(pThis, pSrc))
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Sequence of X.509 Extensions.
 */
#define RTASN1TMPL_TYPE         RTCRX509EXTENSIONS
#define RTASN1TMPL_EXT_NAME     RTCrX509Extensions
#define RTASN1TMPL_INT_NAME     rtCrX509Extensions
RTASN1TMPL_SEQ_OF(RTCRX509EXTENSION, RTCrX509Extension);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * X.509 TbsCertificate.
 */
#define RTASN1TMPL_TYPE         RTCRX509TBSCERTIFICATE
#define RTASN1TMPL_EXT_NAME     RTCrX509TbsCertificate
#define RTASN1TMPL_INT_NAME     rtCrX509TbsCertificate
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_OPT_XTAG(     T0, CtxTag0, Version,               RTASN1INTEGER,                  RTAsn1Integer, 0);
RTASN1TMPL_MEMBER(              SerialNumber,                       RTASN1INTEGER,                  RTAsn1Integer);
RTASN1TMPL_MEMBER(              Signature,                          RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              Issuer,                             RTCRX509NAME,                   RTCrX509Name);
RTASN1TMPL_MEMBER(              Validity,                           RTCRX509VALIDITY,               RTCrX509Validity);
RTASN1TMPL_MEMBER(              Subject,                            RTCRX509NAME,                   RTCrX509Name);
RTASN1TMPL_MEMBER(              SubjectPublicKeyInfo,               RTCRX509SUBJECTPUBLICKEYINFO,   RTCrX509SubjectPublicKeyInfo);
RTASN1TMPL_MEMBER_OPT_XTAG(     T1, CtxTag1, IssuerUniqueId,        RTCRX509UNIQUEIDENTIFIER,       RTCrX509UniqueIdentifier, 1);
RTASN1TMPL_MEMBER_OPT_XTAG(     T2, CtxTag2, SubjectUniqueId,       RTCRX509UNIQUEIDENTIFIER,       RTCrX509UniqueIdentifier, 2);
RTASN1TMPL_MEMBER_OPT_XTAG(     T3, CtxTag3, Extensions,            RTCRX509EXTENSIONS,             RTCrX509Extensions, 3);
RTASN1TMPL_EXEC_DECODE(         rc = RTCrX509TbsCertificate_ReprocessExtensions(pThis, pCursor->pPrimary->pErrInfo) )
RTASN1TMPL_EXEC_CLONE(          rc = RTCrX509TbsCertificate_ReprocessExtensions(pThis, NULL) )
RTASN1TMPL_EXEC_CHECK_SANITY(   rc = rtCrX509TbsCertificate_CheckSanityExtra(pThis, fFlags, pErrInfo, pszErrorTag) )
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One X.509 Certificate.
 */
#define RTASN1TMPL_TYPE         RTCRX509CERTIFICATE
#define RTASN1TMPL_EXT_NAME     RTCrX509Certificate
#define RTASN1TMPL_INT_NAME     rtCrX509Certificate
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              TbsCertificate,                     RTCRX509TBSCERTIFICATE,         RTCrX509TbsCertificate);
RTASN1TMPL_MEMBER(              SignatureAlgorithm,                 RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              SignatureValue,                     RTASN1BITSTRING,                RTAsn1BitString);
RTASN1TMPL_EXEC_CHECK_SANITY(   rc = rtCrX509Certificate_CheckSanityExtra(pThis, fFlags, pErrInfo, pszErrorTag) )
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Set of X.509 Certificates.
 */
/** @todo Microsoft Hacks. ExtendedCertificates. */
#define RTASN1TMPL_TYPE         RTCRX509CERTIFICATES
#define RTASN1TMPL_EXT_NAME     RTCrX509Certificates
#define RTASN1TMPL_INT_NAME     rtCrX509Certificates
RTASN1TMPL_SET_OF(RTCRX509CERTIFICATE, RTCrX509Certificate);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

