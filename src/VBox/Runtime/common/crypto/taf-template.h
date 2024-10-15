/* $Id: taf-template.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Trust Anchor Format (RFC-5914), Code Generator Template.
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
 * CertPathControls (not sequence-/set-of).
 */
#define RTASN1TMPL_TYPE         RTCRTAFCERTPATHCONTROLS
#define RTASN1TMPL_EXT_NAME     RTCrTafCertPathControls
#define RTASN1TMPL_INT_NAME     rtCrTafCertPathControls
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              TaName,             RTCRX509NAME,                   RTCrX509Name);
RTASN1TMPL_MEMBER_OPT_ITAG(     Certificate,        RTCRX509CERTIFICATE,            RTCrX509Certificate,            0);
RTASN1TMPL_MEMBER_OPT_ITAG(     PolicySet,          RTCRX509CERTIFICATEPOLICIES,    RTCrX509CertificatePolicies,    1);
RTASN1TMPL_MEMBER_OPT_ITAG_BITSTRING(PolicyFlags,    3 /* max bits */,                                              2);
RTASN1TMPL_MEMBER_OPT_ITAG(     NameConstr,         RTCRX509NAMECONSTRAINTS,        RTCrX509NameConstraints,        3);
RTASN1TMPL_MEMBER_OPT_ITAG_EX(  PathLenConstraint,  RTASN1INTEGER,                  RTAsn1Integer,                  4, RTASN1TMPL_ITAG_F_CP, RT_NOTHING);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * TrustAnchorInfo.
 */
#define RTASN1TMPL_TYPE         RTCRTAFTRUSTANCHORINFO
#define RTASN1TMPL_EXT_NAME     RTCrTafTrustAnchorInfo
#define RTASN1TMPL_INT_NAME     rtCrTafTrustAnchorInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_DEF_ITAG_UP(  Version,            RTASN1INTEGER,                  RTAsn1Integer, ASN1_TAG_INTEGER, RTCRTAFTRUSTANCHORINFO_V1);
RTASN1TMPL_MEMBER(              PubKey,             RTCRX509SUBJECTPUBLICKEYINFO,   RTCrX509SubjectPublicKeyInfo);
RTASN1TMPL_MEMBER(              KeyIdentifier,      RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_MEMBER_OPT_UTF8_STRING_EX(TaTitle,       RTASN1TMPL_MEMBER_CONSTR_MIN_MAX(TaTitleLangTag, RTASN1STRING, RTAsn1String, 1, 64, RT_NOTHING));
RTASN1TMPL_MEMBER_OPT_ITAG_EX(  CertPath,           RTCRTAFCERTPATHCONTROLS,        RTCrTafCertPathControls, ASN1_TAG_SEQUENCE, RTASN1TMPL_ITAG_F_UC, RT_NOTHING);
RTASN1TMPL_MEMBER_OPT_XTAG(     T1, CtxTag1, Exts,  RTCRX509EXTENSIONS,             RTCrX509Extensions, 1);
RTASN1TMPL_MEMBER_OPT_UTF8_STRING_EX(TaTitleLangTag, RTASN1TMPL_MEMBER_CONSTR_MIN_MAX(TaTitleLangTag, RTASN1STRING, RTAsn1String, 2, 4, RT_NOTHING));
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * TrustAnchorChoice.
 */
#define RTASN1TMPL_TYPE         RTCRTAFTRUSTANCHORCHOICE
#define RTASN1TMPL_EXT_NAME     RTCrTafTrustAnchorChoice
#define RTASN1TMPL_INT_NAME     rtCrTafTrustAnchorChoice
RTASN1TMPL_BEGIN_PCHOICE();
RTASN1TMPL_PCHOICE_ITAG(ASN1_TAG_SEQUENCE, RTCRTAFTRUSTANCHORCHOICEVAL_CERTIFICATE,         u.pCertificate, Certificate, RTCRX509CERTIFICATE, RTCrX509Certificate);
RTASN1TMPL_PCHOICE_XTAG(1,                 RTCRTAFTRUSTANCHORCHOICEVAL_TBS_CERTIFICATE,     u.pT1, CtxTag1, TbsCert,  RTCRX509TBSCERTIFICATE, RTCrX509TbsCertificate);
RTASN1TMPL_PCHOICE_XTAG(2,                 RTCRTAFTRUSTANCHORCHOICEVAL_TRUST_ANCHOR_INFO,   u.pT2, CtxTag2, TaInfo,   RTCRTAFTRUSTANCHORINFO, RTCrTafTrustAnchorInfo);
RTASN1TMPL_END_PCHOICE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * TrustAnchorList
 */
#define RTASN1TMPL_TYPE         RTCRTAFTRUSTANCHORLIST
#define RTASN1TMPL_EXT_NAME     RTCrTafTrustAnchorList
#define RTASN1TMPL_INT_NAME     rtCrTafTrustAnchorList
RTASN1TMPL_SEQ_OF(RTCRTAFTRUSTANCHORCHOICE, RTCrTafTrustAnchorChoice);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

