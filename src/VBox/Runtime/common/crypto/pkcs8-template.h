/* $Id: pkcs8-template.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - PKCS \#8, Code Generator Template.
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
 * PKCS\#8 Private key info
 */
#define RTASN1TMPL_TYPE         RTCRPKCS8PRIVATEKEYINFO
#define RTASN1TMPL_EXT_NAME     RTCrPkcs8PrivateKeyInfo
#define RTASN1TMPL_INT_NAME     RTCrPkcs8PrivateKeyInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Version,                RTASN1INTEGER,                  RTAsn1Integer);
RTASN1TMPL_MEMBER(              PrivateKeyAlgorithm,    RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              PrivateKey,             RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_MEMBER_OPT_ITAG(     Attributes,             RTCRPKCS7ATTRIBUTES,            RTCrPkcs7Attributes,     0);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Encrypted private key info
 */
#define RTASN1TMPL_TYPE         RTCRPKCS8ENCRYPTEDPRIVATEKEYINFO
#define RTASN1TMPL_EXT_NAME     RTCrPkcs8EncryptedPrivateKeyInfo
#define RTASN1TMPL_INT_NAME     rtCrPkcs8EncryptedPrivateKeyInfo
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              EncryptionAlgorithm,    RTCRX509ALGORITHMIDENTIFIER,    RTCrX509AlgorithmIdentifier);
RTASN1TMPL_MEMBER(              EncryptedData,          RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

