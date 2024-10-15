/* $Id: iprt-openssl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal header for the OpenSSL helpers.
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

#ifndef IPRT_INCLUDED_INTERNAL_iprt_openssl_h
#define IPRT_INCLUDED_INTERNAL_iprt_openssl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/crypto/x509.h>
#include <iprt/crypto/pkcs7.h>

RT_C_DECLS_BEGIN
struct evp_md_st;
struct evp_pkey_st;

DECLHIDDEN(void) rtCrOpenSslInit(void);
DECLHIDDEN(int)  rtCrOpenSslErrInfoCallback(const char *pach, size_t cch, void *pvUser);
DECLHIDDEN(int)  rtCrOpenSslConvertX509Cert(void **ppvOsslCert, PCRTCRX509CERTIFICATE pCert, PRTERRINFO pErrInfo);
DECLHIDDEN(void) rtCrOpenSslFreeConvertedX509Cert(void *pvOsslCert);
DECLHIDDEN(int)  rtCrOpenSslAddX509CertToStack(void *pvOsslStack, PCRTCRX509CERTIFICATE pCert, PRTERRINFO pErrInfo);
DECLHIDDEN(const void /*EVP_MD*/ *) rtCrOpenSslConvertDigestType(RTDIGESTTYPE enmDigestType, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  rtCrOpenSslConvertPkcs7Attribute(void **ppvOsslAttrib, PCRTCRPKCS7ATTRIBUTE pAttrib, PRTERRINFO pErrInfo);
DECLHIDDEN(void) rtCrOpenSslFreeConvertedPkcs7Attribute(void *pvOsslAttrib);

DECLHIDDEN(int)  rtCrKeyToOpenSslKey(RTCRKEY hKey, bool fNeedPublic, void /*EVP_PKEY*/ **ppEvpKey, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  rtCrKeyToOpenSslKeyEx(RTCRKEY hKey, bool fNeedPublic, const char *pszAlgoObjId,
                                       void /*EVP_PKEY*/ **ppEvpKey, const void /*EVP_MD*/ **ppEvpMdType, PRTERRINFO pErrInfo);
DECLHIDDEN(int)  rtCrOpenSslWriteMemBioToNewFile(void *pvMemBio, const char *pszFilename, PRTERRINFO pErrInfo);

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_iprt_openssl_h */

