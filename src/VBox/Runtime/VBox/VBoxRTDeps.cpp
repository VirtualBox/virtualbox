/* $Id: VBoxRTDeps.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - VBoxRT.dll/so dependencies.
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
#include <VBox/sup.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/localipc.h>
#include <iprt/buildconfig.h>
#include <iprt/system.h>

#include <libxml/tree.h>
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif
#include "internal/openssl-pre.h"
#include <openssl/md5.h>
#include <openssl/rc4.h>
#include <openssl/pem.h> /* drags in Windows.h */
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/aes.h>
#include "internal/openssl-post.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
struct CLANG11NONSENSE { PFNRT pfn; } g_VBoxRTDeps[] =
{
    { (PFNRT)SUPR3Init },
    { (PFNRT)SUPR3PageAllocEx },
    { (PFNRT)SUPR3LoadVMM },
    { (PFNRT)SUPSemEventCreate },
    { (PFNRT)SUPIsTscFreqCompatibleEx },
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    { (PFNRT)SUPTracerFireProbe }, /** @todo port me @bugref{9898} ? */
    { (PFNRT)SUPGetTscDeltaSlow },
#endif
    { (PFNRT)xmlNewDocComment },
    { (PFNRT)RTLocalIpcServerCreate },
    { (PFNRT)MD5_Init },
    { (PFNRT)RC4 },
    { (PFNRT)RC4_set_key },
    { (PFNRT)PEM_read_bio_X509 },
    { (PFNRT)PEM_read_bio_PrivateKey },
    { (PFNRT)X509_free },
    { (PFNRT)X509_verify_cert_error_string },
    { (PFNRT)i2d_X509 },
    { (PFNRT)i2d_X509 },
    { (PFNRT)i2d_PublicKey },
    { (PFNRT)DH_generate_parameters_ex }, /* gsoap */
    { (PFNRT)DH_new }, /* gsoap */
#if OPENSSL_VERSION_NUMBER >= 0x10100000
    { (PFNRT)OpenSSL_version_num }, /* gsoap */
    { (PFNRT)ASN1_STRING_get0_data }, /* gsoap */
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER) || defined(OPENSSL_MANGLER)
    { (PFNRT)RSA_generate_key }, /* gsoap */
#endif
    { (PFNRT)RSA_generate_key_ex },
#if OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER) || defined(OPENSSL_MANGLER)
    { (PFNRT)DH_generate_parameters }, /* gsoap */
#endif
    { (PFNRT)DH_generate_parameters_ex },
    { (PFNRT)RAND_load_file },
#if OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER)
    { (PFNRT)CRYPTO_set_dynlock_create_callback },
    { (PFNRT)CRYPTO_set_dynlock_lock_callback },
    { (PFNRT)CRYPTO_set_dynlock_destroy_callback },
#endif
    { (PFNRT)RTAssertShouldPanic },
    { (PFNRT)ASMAtomicReadU64 },
    { (PFNRT)ASMAtomicCmpXchgU64 },
    { (PFNRT)ASMBitFirstSet },
    { (PFNRT)RTBldCfgRevision },
    { (PFNRT)SSL_free },
#if OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER)
    { (PFNRT)SSL_library_init },
    { (PFNRT)SSL_load_error_strings },
#endif
    { (PFNRT)SSL_CTX_free },
    { (PFNRT)SSL_CTX_use_certificate_file },
    { (PFNRT)SSLv23_method },
#if OPENSSL_VERSION_NUMBER < 0x10100000 || defined(LIBRESSL_VERSION_NUMBER)
    { (PFNRT)TLSv1_server_method },
#endif
    { (PFNRT)AES_ofb128_encrypt }, /* libtpms */
    { NULL }
};

